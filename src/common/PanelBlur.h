#pragma once

#include <QObject>
#include <QWindow>
#include <QGuiApplication>
#include <qpa/qplatformnativeinterface.h>

#include <QtWaylandClient/QWaylandClientExtension>

#include "qwayland-ext-background-effect-v1.h"
#include "qwayland-wayland.h"

// Binds the ext_background_effect_manager_v1 global (if the compositor
// advertises it) -- this is the modern, cross-compositor replacement
// for KDE's older org_kde_kwin_blur_manager protocol (which KWin itself
// dropped in Plasma 6.7). See protocols/ext-background-effect-v1.xml
// for the protocol source and compositor support notes.
//
// isActive() (inherited from QWaylandClientExtensionTemplate) reports
// whether the compositor actually advertised this global at all -- on
// a compositor that doesn't implement it (Sway, as of 1.11, does not),
// this stays false forever and every call below becomes a safe no-op.
class BackgroundEffectManager
    : public QWaylandClientExtensionTemplate<BackgroundEffectManager>,
      public QtWayland::ext_background_effect_manager_v1
{
    Q_OBJECT

public:
    BackgroundEffectManager() : QWaylandClientExtensionTemplate<BackgroundEffectManager>(1) {}

protected:
    void ext_background_effect_manager_v1_capabilities(uint32_t flags) override
    {
        // capability::blur == 1 per the protocol's bitfield enum.
        m_supportsBlur = (flags & 1) != 0;
    }

public:
    bool supportsBlur() const { return m_supportsBlur; }

private:
    bool m_supportsBlur = false;
};

// Minimal wl_compositor binding, used only to create a throwaway
// wl_region describing "the whole surface" to pass to set_blur_region.
// Qt's own internal wl_compositor binding isn't exposed through public
// API, so this binds a second, independent handle to the same global
// purely for region creation -- harmless, wl_compositor supports
// multiple clients/binds by design.
class BackgroundEffectCompositor
    : public QWaylandClientExtensionTemplate<BackgroundEffectCompositor>,
      public QtWayland::wl_compositor
{
    Q_OBJECT

public:
    BackgroundEffectCompositor() : QWaylandClientExtensionTemplate<BackgroundEffectCompositor>(4) {}
};

// Requests full-surface blur for the given window. Safe to call
// regardless of compositor support -- if the manager/compositor
// extension hasn't bound (or never will, because the compositor
// doesn't implement the protocol), this does nothing.
//
// Uses QPlatformNativeInterface::nativeResourceForWindow(), a private
// (but stable-in-practice) Qt API -- there is no public per-window
// Wayland native interface exposing wl_surface as of current Qt (only
// QNativeInterface::QWaylandApplication and QWaylandScreen exist
// publicly; there's no public QWaylandWindow equivalent). Requires
// linking Qt6::GuiPrivate, which in turn requires the qt6-base-private-dev
// package on Debian/Ubuntu -- qt6-base-dev alone ships the CMake config
// stub but not the actual private headers it depends on.
//
// Known limitation: only applies once, sized to the window's
// dimensions at call time. Doesn't re-apply on resize -- fine for the
// topbar/dock, which don't resize after initial layout-shell setup.
inline void applyPanelBlur(QWindow *window, BackgroundEffectManager *manager,
                            BackgroundEffectCompositor *compositor)
{
    if (!manager->isActive() || !compositor->isActive()) {
        return;
    }

    auto *nativeInterface = QGuiApplication::platformNativeInterface();
    if (!nativeInterface) {
        return;
    }

    void *surfacePtr = nativeInterface->nativeResourceForWindow("surface", window);
    if (!surfacePtr) {
        return;
    }
    auto *wlSurface = static_cast<struct ::wl_surface *>(surfacePtr);

    struct ::ext_background_effect_surface_v1 *effectSurface =
        manager->get_background_effect(wlSurface);
    struct ::wl_region *region = compositor->create_region();

    wl_region_add(region, 0, 0, window->width(), window->height());
    ext_background_effect_surface_v1_set_blur_region(effectSurface, region);

    // Region has copy semantics per the protocol -- safe to destroy
    // immediately after set_blur_region.
    wl_region_destroy(region);

    // Blur region is double-buffered state; needs an actual
    // wl_surface.commit to take effect, same as any other pending
    // surface state.
    wl_surface_commit(wlSurface);
}
