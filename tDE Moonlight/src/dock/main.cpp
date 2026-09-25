#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QScreen>
#include <QFont>
#include <QIcon>
#include <QDBusConnection>
#include <QDebug>

#include <LayerShellQt/Window>

#include "AppModel.h"
#include "IconThemeProvider.h"
#include "DockController.h"
#include "LayerShellHelper.h"
#ifdef TDE_BLUR_ENABLED
#include "PanelBlur.h"
#endif

// Dock is now a horizontal strip anchored to the bottom-left corner of
// the screen (matching the mockups), not the old full-height left-edge
// vertical strip -- kDockHeight is the layer-shell exclusive zone height
// (reserves screen space so windows don't render underneath it), and
// the strip is only as wide as its content needs (set from QML, not
// forced full-width), anchored Left+Bottom only rather than
// Top+Bottom+Left.
constexpr int kDockHeight = 96;

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("tde-moonlight-dock"));
    app.setOrganizationName(QStringLiteral("tDE"));

    QFont applicationFont(QStringLiteral("Inter"));
    applicationFont.setStyleHint(QFont::SansSerif);
    app.setFont(applicationFont);
    QIcon::setThemeName(QStringLiteral("Adwaita"));

    AppModel appModel;
    DockController dockController;
#ifdef TDE_BLUR_ENABLED
    BackgroundEffectManager backgroundEffectManager;
    BackgroundEffectCompositor backgroundEffectCompositor;
    LayerShellHelper layerShellHelper(&backgroundEffectManager, &backgroundEffectCompositor);
#else
    LayerShellHelper layerShellHelper;
#endif

    // Exposes DockController::toggleAppMenu() over D-Bus so a compositor
    // keybinding (or later, the GlobalShortcuts portal) can open the app
    // menu without the dock needing keyboard focus. See DockController.h
    // for why this can't just be a QML Shortcut item, and the project
    // README/chat history for the actual Sway `bindsym` line to use.
    // Registration failing (e.g. no session bus, or another instance of
    // tde-dock already running) isn't fatal -- the app menu still opens
    // fine from the grid-icon button either way.
    if (!QDBusConnection::sessionBus().registerService(QStringLiteral("org.tde.Moonlight.Dock"))) {
        qWarning() << "tde-dock: could not claim D-Bus service org.tde.Moonlight.Dock"
                   << "(Super+A binding via dbus-send won't work; is another instance running?)";
    } else if (!QDBusConnection::sessionBus().registerObject(
                   QStringLiteral("/org/tde/Moonlight/Dock"),
                   &dockController,
                   QDBusConnection::ExportScriptableSlots)) {
        qWarning() << "tde-dock: could not register /org/tde/Moonlight/Dock on the session bus";
    }

    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("icontheme"), new IconThemeProvider());

    qmlRegisterSingletonInstance<AppModel>(
        "TDEMoonlight.Dock", 1, 0, "AppSystemModel", &appModel);
    qmlRegisterSingletonInstance<DockController>(
        "TDEMoonlight.Dock", 1, 0, "DockController", &dockController);
    qmlRegisterSingletonInstance<LayerShellHelper>(
        "TDEMoonlight.Dock", 1, 0, "LayerShellHelper", &layerShellHelper);

    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreated,
        &app,
        [&](QObject *object, const QUrl &) {
            auto *window = qobject_cast<QQuickWindow *>(object);
            if (!window) {
                return;
            }

            window->create();

            if (auto *layerWindow = LayerShellQt::Window::get(window)) {
                layerWindow->setLayer(LayerShellQt::Window::LayerTop);

                // Bottom-left corner: anchor only the two edges that
                // corner touches. Anchoring Left+Right (or Top+Bottom)
                // instead would stretch the surface across the whole
                // screen on that axis, which is right for the topbar's
                // full-width strip but wrong for a corner-docked capsule
                // that should only be as big as its content.
                layerWindow->setAnchors(
                    LayerShellQt::Window::Anchors(
                        LayerShellQt::Window::AnchorLeft |
                        LayerShellQt::Window::AnchorBottom));

                layerWindow->setExclusiveZone(kDockHeight);
                layerWindow->setScope(QStringLiteral("tde-moonlight-dock"));
                layerWindow->setKeyboardInteractivity(
                    LayerShellQt::Window::KeyboardInteractivityNone);
            }

            window->setHeight(kDockHeight);
            // Width is content-driven now (Dock.qml sizes dockCapsule to
            // its row of icons), not forced to the screen's full
            // dimension on either axis -- a fixed generous width is set
            // here as the window's own bound and Dock.qml's capsule
            // sizes itself within it; this only matters for extremely
            // long pinned-app lists, which isn't a real scenario for a
            // school-club/portfolio project's dock but is more correct
            // than silently clipping is.
            window->setWidth(960);

            window->show();

#ifdef TDE_BLUR_ENABLED
            applyPanelBlur(window, &backgroundEffectManager, &backgroundEffectCompositor);
            QObject::connect(&backgroundEffectManager, &QWaylandClientExtension::activeChanged,
                              window, [window, &backgroundEffectManager, &backgroundEffectCompositor]() {
                                  applyPanelBlur(window, &backgroundEffectManager, &backgroundEffectCompositor);
                              });
#endif
        },
        Qt::QueuedConnection);

    engine.loadFromModule("TDEMoonlight.Dock", "Dock");

    if (engine.rootObjects().isEmpty()) {
        return -1;
    }

    return app.exec();
}
