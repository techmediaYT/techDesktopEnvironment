#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QScreen>

#include <LayerShellQt/Window>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("tde-moonlight-wallpaper"));
    app.setOrganizationName(QStringLiteral("tDE"));

    QQmlApplicationEngine engine;

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
                // Background layer -- sits behind every other layer-shell
                // surface (panels, dock) and behind normal application
                // windows too.
                layerWindow->setLayer(LayerShellQt::Window::LayerBackground);

                // Anchored to all four edges: this surface covers the
                // entire screen, unlike the topbar/dock which only
                // reserve a strip.
                layerWindow->setAnchors(
                    LayerShellQt::Window::Anchors(
                        LayerShellQt::Window::AnchorTop |
                        LayerShellQt::Window::AnchorBottom |
                        LayerShellQt::Window::AnchorLeft |
                        LayerShellQt::Window::AnchorRight));

                layerWindow->setScope(QStringLiteral("tde-moonlight-wallpaper"));
                layerWindow->setKeyboardInteractivity(
                    LayerShellQt::Window::KeyboardInteractivityNone);
            }

            if (window->screen()) {
                window->setGeometry(window->screen()->geometry());
            }

            window->show();
        },
        Qt::QueuedConnection);

    engine.loadFromModule("TDEMoonlight.Wallpaper", "WallpaperSurface");

    if (engine.rootObjects().isEmpty()) {
        return -1;
    }

    return app.exec();
}
