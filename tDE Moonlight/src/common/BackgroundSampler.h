#pragma once

#include <QObject>
#include <QWindow>
#include <QScreen>
#include <QGuiApplication>
#include <QTimer>
#include <QFile>
#include <qpa/qplatformnativeinterface.h>

// Always-registered QML-facing state holder -- exists regardless of
// whether TDE_ENABLE_BACKGROUND_SAMPLING is on, specifically so
// TopBar.qml can bind to BackgroundState.isDarkBackground unconditionally
// without needing two different QML code paths depending on a build
// flag it has no way to check. When background sampling is compiled in,
// BackgroundSampler (below) updates this object's property as real
// samples come in; when it isn't, this class is still registered from
// main.cpp with its own hardcoded default, wired to nothing, and
// TopBar.qml is none the wiser.
class TopBarBackgroundState : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool isDarkBackground READ isDarkBackground WRITE setIsDarkBackground NOTIFY changed)

public:
    explicit TopBarBackgroundState(QObject *parent = nullptr) : QObject(parent) {}

    bool isDarkBackground() const { return m_isDarkBackground; }
    void setIsDarkBackground(bool dark)
    {
        if (dark != m_isDarkBackground) {
            m_isDarkBackground = dark;
            emit changed();
        }
    }

signals:
    void changed();

private:
    bool m_isDarkBackground = true; // safe default, matches TopBar.qml's prior hardcoded value
};

#ifdef TDE_BACKGROUND_SAMPLING_ENABLED

#include <QtWaylandClient/QWaylandClientExtension>

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "qwayland-ext-image-capture-source-v1.h"
#include "qwayland-ext-image-copy-capture-v1.h"
#include "qwayland-wayland.h"

// Real background-luminance sampling for the topbar's isDarkBackground
// property (see TopBar.qml), via ext-image-copy-capture-v1 +
// ext-image-capture-source-v1 -- the modern replacement for wlroots'
// wlr-screencopy-unstable-v1, which upstream itself now points users
// away from (see protocols/ext-image-copy-capture-v1.xml's own
// copyright/description). Sway has supported both since 1.11
// (June 2025).
//
// SCOPE: this captures the primary output ONCE per sample request (not
// a live per-frame video feed -- that would be needless overhead for
// "should topbar text be light or dark") and reads back a thin
// horizontal strip of pixels from the region directly under the
// topbar's own height, then reports whether the average of those
// pixels is dark or light. It does not composite, does not handle
// multi-output setups beyond "the primary screen topbar lives on", and
// does not attempt cursor overlay compositing (paint_cursors option
// left off deliberately -- the cursor's own color shouldn't skew a
// "what's my background" reading).
//
// Like BackgroundEffectManager in PanelBlur.h, every step here is
// guarded by isActive()/protocol-availability checks: on a compositor
// that doesn't implement these protocols at all, sampling silently
// never happens and TopBarBackgroundState keeps whatever default value it
// was constructed with -- this is designed to degrade to "do nothing"
// exactly like the blur feature does, not to error or crash.
class ImageCaptureSourceManager
    : public QWaylandClientExtensionTemplate<ImageCaptureSourceManager>,
      public QtWayland::ext_output_image_capture_source_manager_v1
{
    Q_OBJECT
public:
    ImageCaptureSourceManager() : QWaylandClientExtensionTemplate<ImageCaptureSourceManager>(1) {}
};

class ImageCopyCaptureManager
    : public QWaylandClientExtensionTemplate<ImageCopyCaptureManager>,
      public QtWayland::ext_image_copy_capture_manager_v1
{
    Q_OBJECT
public:
    ImageCopyCaptureManager() : QWaylandClientExtensionTemplate<ImageCopyCaptureManager>(1) {}
};

// One capture attempt: create a source for the output, create a
// session, wait for buffer_size/shm_format/done, allocate a matching
// shm buffer, create a frame, capture it, wait for ready/failed, read
// back pixels, then tear the whole thing down. Every capture starts
// fresh rather than keeping a session alive between samples -- this
// only samples occasionally (see BackgroundSampler's timer below), so
// there's no benefit to keeping a live session/buffer around between
// calls, and starting fresh avoids having to handle the session
// re-negotiating constraints mid-lifetime.
class ScreenCaptureAttempt : public QObject,
                             public QtWayland::ext_image_capture_source_v1,
                             public QtWayland::ext_image_copy_capture_session_v1,
                             public QtWayland::ext_image_copy_capture_frame_v1
{
    Q_OBJECT
public:
    ScreenCaptureAttempt(ImageCaptureSourceManager *sourceManager,
                         ImageCopyCaptureManager *copyManager,
                         struct ::wl_output *output,
                         struct ::wl_shm *shm,
                         int sampleHeight,
                         QObject *parent)
        : QObject(parent), m_shm(shm), m_sampleHeight(sampleHeight)
    {
        QtWayland::ext_image_capture_source_v1::init(sourceManager->create_source(output));
        m_session.reset(new SessionHelper(this, copyManager->create_session(
            QtWayland::ext_image_capture_source_v1::object(), 0 /* no paint_cursors */)));
    }

    ~ScreenCaptureAttempt() override
    {
        if (m_buffer) {
            wl_buffer_destroy(m_buffer);
        }
        if (m_shmData && m_shmData != MAP_FAILED) {
            munmap(m_shmData, m_shmSize);
        }
        if (m_shmFd >= 0) {
            close(m_shmFd);
        }
        // Each interface's own destroy request, per protocol -- the
        // frame only exists once ext_image_copy_capture_session_v1_done
        // has fired (see there), so guard it; the session and source
        // always exist once the constructor has run.
        if (QtWayland::ext_image_copy_capture_frame_v1::object()) {
            QtWayland::ext_image_copy_capture_frame_v1::destroy();
        }
        QtWayland::ext_image_copy_capture_session_v1::destroy();
        QtWayland::ext_image_capture_source_v1::destroy();
    }

signals:
    // luminance in [0, 1], averaged over the sampled strip. Emitted
    // exactly once per ScreenCaptureAttempt, on either success or
    // failure -- failure emits with sampleValid=false so the caller
    // can tell "we didn't get an answer" apart from "we sampled a very
    // dark image" (both would otherwise look like luminance near 0).
    void sampleFinished(bool sampleValid, qreal averageLuminance);

private:
    // Wraps the session's virtual event handlers -- kept as a nested
    // helper (rather than ScreenCaptureAttempt itself inheriting
    // ext_image_copy_capture_session_v1 AND ext_image_copy_capture_frame_v1
    // both) purely so the two objects' same-named events
    // (buffer_size/done exist conceptually on the session; ready/failed
    // on the frame) can't be confused for each other while reading this
    // file -- QtWayland's generated base classes use virtual methods
    // named after the protocol event, and both interfaces happen to be
    // implemented on the same C++ object below for simplicity, so this
    // comment is the disambiguation since the method names themselves
    // don't repeat the interface name.
    struct SessionHelper
    {
        SessionHelper(ScreenCaptureAttempt *owner, struct ::ext_image_copy_capture_session_v1 *session)
            : owner(owner)
        {
            owner->QtWayland::ext_image_copy_capture_session_v1::init(session);
        }
        ScreenCaptureAttempt *owner;
    };

protected:
    // --- ext_image_copy_capture_session_v1 events ---
    void ext_image_copy_capture_session_v1_buffer_size(uint32_t width, uint32_t height) override
    {
        m_bufferWidth = width;
        m_bufferHeight = height;
    }

    void ext_image_copy_capture_session_v1_shm_format(uint32_t format) override
    {
        // Prefer the first format offered whose Wayland shm format we
        // actually know how to read back as plain RGB(A) bytes --
        // xrgb8888/argb8888 (little-endian, matching WL_SHM_FORMAT_*)
        // covers what every compositor advertises first in practice.
        // If nothing suitable turns up, m_haveUsableFormat stays false
        // and the whole attempt reports itself invalid rather than
        // guessing at an unknown pixel layout.
        if (!m_haveUsableFormat && (format == WL_SHM_FORMAT_XRGB8888 || format == WL_SHM_FORMAT_ARGB8888)) {
            m_shmFormat = format;
            m_haveUsableFormat = true;
        }
    }

    void ext_image_copy_capture_session_v1_done() override
    {
        if (!m_haveUsableFormat || m_bufferWidth == 0 || m_bufferHeight == 0) {
            emit sampleFinished(false, 0.0);
            return;
        }
        if (!allocateShmBuffer()) {
            emit sampleFinished(false, 0.0);
            return;
        }

        QtWayland::ext_image_copy_capture_frame_v1::init(
            QtWayland::ext_image_copy_capture_session_v1::create_frame());
        QtWayland::ext_image_copy_capture_frame_v1::attach_buffer(m_buffer);
        // Full-buffer damage, as required on first capture per the
        // protocol's own damage_buffer documentation.
        QtWayland::ext_image_copy_capture_frame_v1::damage_buffer(
            0, 0, static_cast<int32_t>(m_bufferWidth), static_cast<int32_t>(m_bufferHeight));
        QtWayland::ext_image_copy_capture_frame_v1::capture();
    }

    void ext_image_copy_capture_session_v1_stopped() override
    {
        emit sampleFinished(false, 0.0);
    }

    // --- ext_image_copy_capture_frame_v1 events ---
    void ext_image_copy_capture_frame_v1_ready() override
    {
        emit sampleFinished(true, computeAverageLuminance());
    }

    void ext_image_copy_capture_frame_v1_failed(uint32_t) override
    {
        emit sampleFinished(false, 0.0);
    }

private:
    bool allocateShmBuffer()
    {
        m_shmSize = static_cast<size_t>(m_bufferWidth) * m_bufferHeight * 4;

        char shmName[64];
        snprintf(shmName, sizeof(shmName), "/tde-bgsample-%d", getpid());
        m_shmFd = shm_open(shmName, O_CREAT | O_RDWR | O_EXCL, 0600);
        if (m_shmFd < 0) {
            return false;
        }
        shm_unlink(shmName); // unlinked immediately -- fd alone keeps it alive
        if (ftruncate(m_shmFd, static_cast<off_t>(m_shmSize)) != 0) {
            return false;
        }
        m_shmData = mmap(nullptr, m_shmSize, PROT_READ | PROT_WRITE, MAP_SHARED, m_shmFd, 0);
        if (m_shmData == MAP_FAILED) {
            return false;
        }

        struct ::wl_shm_pool *pool = wl_shm_create_pool(m_shm, m_shmFd, static_cast<int32_t>(m_shmSize));
        m_buffer = wl_shm_pool_create_buffer(
            pool, 0,
            static_cast<int32_t>(m_bufferWidth), static_cast<int32_t>(m_bufferHeight),
            static_cast<int32_t>(m_bufferWidth) * 4,
            m_shmFormat);
        wl_shm_pool_destroy(pool); // buffer keeps the underlying pool data alive

        return m_buffer != nullptr;
    }

    qreal computeAverageLuminance() const
    {
        if (!m_shmData || m_shmData == MAP_FAILED) {
            return 0.0;
        }

        // Sample a horizontal strip m_sampleHeight tall from the very
        // top of the captured output -- that's where the topbar
        // actually sits, so this is "what's behind the topbar", not
        // "what's the average color of the whole desktop".
        const uint32_t rowsToSample = qMin(m_bufferHeight, static_cast<uint32_t>(m_sampleHeight));
        const auto *pixels = static_cast<const uint8_t *>(m_shmData);
        const uint32_t stride = m_bufferWidth * 4;

        double luminanceSum = 0.0;
        uint64_t sampleCount = 0;

        // Every 4th pixel horizontally, every row in the strip --
        // plenty for a stable average without reading all four million-
        // odd bytes of a 1080p-wide strip on every sample.
        for (uint32_t y = 0; y < rowsToSample; ++y) {
            for (uint32_t x = 0; x < m_bufferWidth; x += 4) {
                const uint8_t *pixel = pixels + y * stride + x * 4;
                // wl_shm xrgb8888/argb8888 is little-endian 0xAARRGGBB
                // in memory as bytes B,G,R,A.
                const uint8_t b = pixel[0];
                const uint8_t g = pixel[1];
                const uint8_t r = pixel[2];
                // Standard perceptual luminance weights -- same
                // coefficients TitleBar.qml's isDarkBackground check
                // uses, so a titlebar sampling its own solid
                // background color and the topbar sampling real
                // captured pixels agree on what counts as "dark".
                luminanceSum += (0.299 * r + 0.587 * g + 0.114 * b) / 255.0;
                ++sampleCount;
            }
        }

        return sampleCount > 0 ? (luminanceSum / static_cast<double>(sampleCount)) : 0.0;
    }

    QScopedPointer<SessionHelper> m_session;
    struct ::wl_shm *m_shm = nullptr;
    int m_sampleHeight = 56;

    uint32_t m_bufferWidth = 0;
    uint32_t m_bufferHeight = 0;
    bool m_haveUsableFormat = false;
    uint32_t m_shmFormat = WL_SHM_FORMAT_XRGB8888;

    int m_shmFd = -1;
    void *m_shmData = nullptr;
    size_t m_shmSize = 0;
    struct ::wl_buffer *m_buffer = nullptr;
};

// Minimal wl_shm binding -- Qt's own internal wl_shm isn't exposed
// through public API, same reasoning as BackgroundEffectCompositor in
// PanelBlur.h binding its own second wl_compositor handle.
class ShmProvider
    : public QWaylandClientExtensionTemplate<ShmProvider>,
      public QtWayland::wl_shm
{
    Q_OBJECT
public:
    ShmProvider() : QWaylandClientExtensionTemplate<ShmProvider>(1) {}
};

// Periodically samples the background behind the topbar and pushes the
// result into a TopBarBackgroundState (see above) for TopBar.qml to
// bind against -- not a QML singleton itself, since only
// TopBarBackgroundState needs to be QML-visible (see that class's own
// comment for why). Falls back to leaving the state object at its
// constructed default on any compositor that doesn't support these
// protocols, or before the first successful sample completes.
class BackgroundSampler : public QObject
{
    Q_OBJECT

public:
    // state must outlive this object; main.cpp constructs both as
    // stack locals in main() so that's guaranteed.
    explicit BackgroundSampler(TopBarBackgroundState *state, QObject *parent = nullptr)
        : QObject(parent), m_state(state)
    {
        m_sourceManager = new ImageCaptureSourceManager();
        m_copyManager = new ImageCopyCaptureManager();
        m_shmProvider = new ShmProvider();

        // Re-sample periodically rather than once at startup --
        // desktop background/wallpaper can change, windows can move
        // under the topbar, etc. 4s is frequent enough to feel
        // responsive without capturing many times a second for
        // something that only affects text/icon color.
        m_timer.setInterval(4000);
        connect(&m_timer, &QTimer::timeout, this, &BackgroundSampler::sampleOnce);
    }

    bool active() const { return m_sourceManager->isActive() && m_copyManager->isActive(); }

public slots:
    // Call once the topbar's own QWindow/QScreen is available -- needs
    // a real wl_output to capture, which only exists once a window has
    // been shown on a screen.
    void start(QWindow *topbarWindow, int sampleHeight)
    {
        m_topbarWindow = topbarWindow;
        m_sampleHeight = sampleHeight;
        if (active()) {
            sampleOnce();
            m_timer.start();
        } else {
            // Protocol not supported by this compositor -- never
            // sample, TopBarBackgroundState keeps its constructed
            // default forever, same degrade-to-nothing behavior as
            // blur.
            m_timer.stop();
        }
    }

private slots:
    void sampleOnce()
    {
        if (!m_topbarWindow || !active()) {
            return;
        }
        auto *nativeInterface = QGuiApplication::platformNativeInterface();
        if (!nativeInterface) {
            return;
        }
        QScreen *screen = m_topbarWindow->screen();
        if (!screen) {
            return;
        }
        void *outputPtr = nativeInterface->nativeResourceForScreen("output", screen);
        if (!outputPtr) {
            return;
        }
        auto *wlOutput = static_cast<struct ::wl_output *>(outputPtr);

        // Owned by 'this' via QObject parenting -- deletes itself once
        // sampleFinished fires (see the lambda below).
        auto *attempt = new ScreenCaptureAttempt(
            m_sourceManager, m_copyManager, wlOutput, m_shmProvider->object(), m_sampleHeight, this);
        connect(attempt, &ScreenCaptureAttempt::sampleFinished, this,
                [this, attempt](bool valid, qreal luminance) {
                    if (valid) {
                        m_state->setIsDarkBackground(luminance < 0.5);
                    }
                    attempt->deleteLater();
                });
    }

private:
    ImageCaptureSourceManager *m_sourceManager = nullptr;
    ImageCopyCaptureManager *m_copyManager = nullptr;
    ShmProvider *m_shmProvider = nullptr;
    TopBarBackgroundState *m_state = nullptr;
    QWindow *m_topbarWindow = nullptr;
    int m_sampleHeight = 56;
    QTimer m_timer;
};

#endif // TDE_BACKGROUND_SAMPLING_ENABLED
