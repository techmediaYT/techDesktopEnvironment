#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QScreen>
#include <QFont>
#include <QIcon>

#include <LayerShellQt/Window>

#include "BatteryMonitor.h"
#include "IconThemeProvider.h"
#include "LayerShellHelper.h"
#include "NetworkStatusModel.h"
#include "BluetoothStatusModel.h"
#include "MediaPlayerModel.h"
#include "VolumeModel.h"
#include "PowerManager.h"
#include "DockRemote.h"
#include "SettingsLauncher.h"
#ifdef TDE_BLUR_ENABLED
#include "PanelBlur.h"
#endif
#ifdef TDE_BACKGROUND_SAMPLING_ENABLED
#include "BackgroundSampler.h"
#endif

// Height of the top bar in logical pixels. Kept here (not just in QML)
// because the layer-shell exclusive zone needs to match this exactly --
// mismatch and other windows will overlap or leave a gap under the bar.
constexpr int kBarHeight = 56;

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("tde-moonlight-topbar"));
    app.setOrganizationName(QStringLiteral("tDE"));

    // tOS ships Inter pre-installed as its UI font -- set it app-wide
    // rather than per-Text-element font.family strings, so anything
    // that doesn't explicitly override it (QtQuick.Controls popups,
    // menus, etc) still gets the right typeface.
    QFont applicationFont(QStringLiteral("Inter"));
    applicationFont.setStyleHint(QFont::SansSerif);
    app.setFont(applicationFont);

    // tOS ships Adwaita as its icon theme, so pin it explicitly rather
    // than relying on whatever QIcon::themeName() falls back to by
    // default (which varies by distro/session and isn't guaranteed to
    // be Adwaita even when it's installed).
    QIcon::setThemeName(QStringLiteral("Adwaita"));

    // Lives for the duration of main() -- the QML singleton registration
    // below stores a pointer to it, so it must outlive the QML engine.
    BatteryMonitor batteryMonitor;
    NetworkStatusModel networkStatus;
    BluetoothStatusModel bluetoothStatus;
    MediaPlayerModel mediaPlayer;
    VolumeModel volumeModel;
    PowerManager powerManager;
    DockRemote dockRemote;
    IconExistenceChecker iconExistenceChecker;
    SettingsLauncher settingsLauncher;

    // Real compositor-side blur (see PanelBlur.h). Binds asynchronously;
    // isActive() only becomes true once the compositor's response
    // round-trips, and stays permanently false on a compositor that
    // doesn't implement the protocol at all (e.g. Sway as of 1.11).
    // Guarded by TDE_ENABLE_BLUR (see CMakeLists.txt) since this
    // currently depends on Qt's private headers, which aren't
    // guaranteed to be installed on every system.
#ifdef TDE_BLUR_ENABLED
    BackgroundEffectManager backgroundEffectManager;
    BackgroundEffectCompositor backgroundEffectCompositor;
    LayerShellHelper layerShellHelper(&backgroundEffectManager, &backgroundEffectCompositor);
#else
    LayerShellHelper layerShellHelper;
#endif

    // TopBarBackgroundState is always constructed and always registered
    // to QML, regardless of TDE_ENABLE_BACKGROUND_SAMPLING -- see its
    // own class comment in BackgroundSampler.h for why: TopBar.qml
    // binds to BackgroundState.isDarkBackground unconditionally, so the
    // type must exist on every build configuration. BackgroundSampler
    // itself (the part that actually depends on the Wayland protocol
    // headers) stays behind the compile guard and, when built, updates
    // backgroundState's property as real samples come in.
    TopBarBackgroundState backgroundState;
#ifdef TDE_BACKGROUND_SAMPLING_ENABLED
    BackgroundSampler backgroundSampler(&backgroundState);
#endif

    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("icontheme"), new IconThemeProvider());

    qmlRegisterSingletonInstance<BatteryMonitor>(
        "TDEMoonlight.TopBar", 1, 0, "BatteryMonitor", &batteryMonitor);
    qmlRegisterSingletonInstance<LayerShellHelper>(
        "TDEMoonlight.TopBar", 1, 0, "LayerShellHelper", &layerShellHelper);
    qmlRegisterSingletonInstance<NetworkStatusModel>(
        "TDEMoonlight.TopBar", 1, 0, "NetworkStatus", &networkStatus);
    qmlRegisterSingletonInstance<BluetoothStatusModel>(
        "TDEMoonlight.TopBar", 1, 0, "BluetoothStatus", &bluetoothStatus);
    qmlRegisterSingletonInstance<MediaPlayerModel>(
        "TDEMoonlight.TopBar", 1, 0, "MediaPlayer", &mediaPlayer);
    qmlRegisterSingletonInstance<VolumeModel>(
        "TDEMoonlight.TopBar", 1, 0, "Volume", &volumeModel);
    qmlRegisterSingletonInstance<PowerManager>(
        "TDEMoonlight.TopBar", 1, 0, "PowerManager", &powerManager);
    qmlRegisterSingletonInstance<DockRemote>(
        "TDEMoonlight.TopBar", 1, 0, "DockRemote", &dockRemote);
    qmlRegisterSingletonInstance<IconExistenceChecker>(
        "TDEMoonlight.TopBar", 1, 0, "IconChecker", &iconExistenceChecker);
    qmlRegisterSingletonInstance<SettingsLauncher>(
        "TDEMoonlight.TopBar", 1, 0, "SettingsLauncher", &settingsLauncher);
    qmlRegisterSingletonInstance<TopBarBackgroundState>(
        "TDEMoonlight.TopBar", 1, 0, "BackgroundState", &backgroundState);

    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreated,
        &app,
        [&](QObject *object, const QUrl &) {
            auto *window = qobject_cast<QQuickWindow *>(object);
            if (!window) {
                return;
            }

            // LayerShellQt::Window::get() needs the window's native
            // platform handle to already exist -- force creation before
            // asking for the layer-shell handle, or get() silently
            // returns null and the window falls back to an ordinary
            // floating window with no anchoring at all.
            window->create();

            if (auto *layerWindow = LayerShellQt::Window::get(window)) {
                layerWindow->setLayer(LayerShellQt::Window::LayerTop);

                layerWindow->setAnchors(
                    LayerShellQt::Window::Anchors(
                        LayerShellQt::Window::AnchorTop |
                        LayerShellQt::Window::AnchorLeft |
                        LayerShellQt::Window::AnchorRight));

                layerWindow->setExclusiveZone(kBarHeight);
                layerWindow->setScope(QStringLiteral("tde-moonlight-topbar"));
                layerWindow->setKeyboardInteractivity(
                    LayerShellQt::Window::KeyboardInteractivityNone);
            }

            window->setHeight(kBarHeight);
            if (window->screen()) {
                window->setWidth(window->screen()->geometry().width());
            }

            // Show only now, after the layer-shell role/anchors/exclusive
            // zone are configured. Showing earlier would let Qt assign an
            // ordinary xdg_toplevel role first, which then conflicts with
            // the layer-shell conversion above and kills the Wayland
            // connection with a protocol error.
            window->show();

#ifdef TDE_BACKGROUND_SAMPLING_ENABLED
            // Needs a real wl_output, which only exists once the window
            // has an assigned screen -- safe to call right after show()
            // since the layer-shell anchoring above already ran, so the
            // window is on a screen by this point.
            backgroundSampler.start(window, kBarHeight);
#endif

#ifdef TDE_BLUR_ENABLED
            // Try immediately (harmless no-op if the extension hasn't
            // bound yet) and again once it actually finishes binding --
            // binding is an async round-trip, so on a compositor that
            // does support this protocol, the first attempt often loses
            // the race and only the second actually applies blur.
            applyPanelBlur(window, &backgroundEffectManager, &backgroundEffectCompositor);
            QObject::connect(&backgroundEffectManager, &QWaylandClientExtension::activeChanged,
                              window, [window, &backgroundEffectManager, &backgroundEffectCompositor]() {
                                  applyPanelBlur(window, &backgroundEffectManager, &backgroundEffectCompositor);
                              });
#endif
        },
        Qt::QueuedConnection);

    engine.loadFromModule("TDEMoonlight.TopBar", "TopBar");

    if (engine.rootObjects().isEmpty()) {
        return -1;
    }

    return app.exec();
}
