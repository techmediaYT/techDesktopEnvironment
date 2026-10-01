#pragma once

#include <QObject>
#include <QQuickWindow>

#include <LayerShellQt/Window>

#ifdef TDE_BLUR_ENABLED
#include "PanelBlur.h"
#endif

// Converts a plain QML Window into a wlr-layer-shell "overlay" surface.
// Shared by AppMenu.qml (dock) and ControlPanel.qml (topbar) -- both are
// plain QML Windows instantiated as children of another QML file's root
// Window, not something main.cpp's objectCreated handler ever sees (that
// callback only fires for the root object of an engine.load()/
// loadFromModule() call, not a Window nested inside that root's QML
// tree). So there's no existing hook to run the same "create() ->
// configure layer-shell -> show()" sequence main.cpp runs for the
// topbar/dock's own root windows. This class is that hook, callable
// directly from QML.
//
// Without this, a window like AppMenu stays a plain xdg_toplevel --
// which is exactly why it was showing up with a Sway title bar and
// getting tiled into half the screen like any ordinary app window: a
// regular toplevel is fair game for the compositor's normal window
// management. A layer-shell surface on the overlay layer never is, by
// protocol design -- no decorations, no tiling, always on top, same as
// the topbar and dock.
class LayerShellHelper : public QObject
{
    Q_OBJECT

public:
#ifdef TDE_BLUR_ENABLED
    LayerShellHelper(BackgroundEffectManager *manager, BackgroundEffectCompositor *compositor,
                      QObject *parent = nullptr)
        : QObject(parent), m_effectManager(manager), m_effectCompositor(compositor)
    {
    }
#else
    explicit LayerShellHelper(QObject *parent = nullptr) : QObject(parent) {}
#endif

    Q_INVOKABLE void configureOverlay(QQuickWindow *window, const QString &scope)
    {
        if (!window) {
            return;
        }

        // Same ordering constraint as main.cpp's own layer-shell setup:
        // the underlying wl_surface has to exist (create()) before
        // LayerShellQt can convert it, and that conversion has to
        // happen before the surface is ever mapped/shown -- this is
        // called from Component.onCompleted, while isOpen (and
        // therefore visible) is still false, so that ordering holds.
        window->create();

        if (auto *layerWindow = LayerShellQt::Window::get(window)) {
            layerWindow->setLayer(LayerShellQt::Window::LayerOverlay);
            layerWindow->setAnchors(
                LayerShellQt::Window::Anchors(
                    LayerShellQt::Window::AnchorTop |
                    LayerShellQt::Window::AnchorBottom |
                    LayerShellQt::Window::AnchorLeft |
                    LayerShellQt::Window::AnchorRight));
            // -1 means "ignore other surfaces' exclusive zones" -- this
            // overlay should cover the whole screen, including the
            // strips the topbar/dock reserve for themselves, since it's
            // meant to sit visually above both of them when open.
            layerWindow->setExclusiveZone(-1);
            // Each surface using this helper needs its own distinct
            // scope name (this used to be hardcoded to
            // "tde-moonlight-appmenu", which was fine when AppMenu was
            // the only caller, but silently wrong now that ControlPanel
            // shares this same helper -- two different layer-shell
            // surfaces reporting the same scope is a real bug, not just
            // a cosmetic label).
            layerWindow->setScope(scope);
            // Unlike the topbar/dock (KeyboardInteractivityNone -- they
            // never want focus), this needs to accept typing into the
            // search box while open. OnDemand hands it focus only while
            // it's actually the interacted-with surface, not always.
            layerWindow->setKeyboardInteractivity(
                LayerShellQt::Window::KeyboardInteractivityOnDemand);
        }

#ifdef TDE_BLUR_ENABLED
        // Same real compositor blur the topbar/dock panels get -- see
        // PanelBlur.h. Note this is currently a no-op under Sway
        // specifically (it doesn't implement ext-background-effect-v1
        // as of 1.11), same known limitation as the topbar/dock already
        // have; this isn't a new gap, just applying the same call here
        // for consistency and so it works once the compositor does
        // support it.
        if (m_effectManager && m_effectCompositor) {
            applyPanelBlur(window, m_effectManager, m_effectCompositor);
        }
#endif
    }

private:
#ifdef TDE_BLUR_ENABLED
    BackgroundEffectManager *m_effectManager = nullptr;
    BackgroundEffectCompositor *m_effectCompositor = nullptr;
#endif
};
