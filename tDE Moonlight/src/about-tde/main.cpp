#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>

// About tDE -- matches tDE-Moonlight-Interface-AppBar.png (gradient
// body, "techDesktopEnvironment" / "Developed by tCorporation", with
// the standard tDE titlebar from qml/decoration/TitleBar.qml on top).
//
// Version/build strings come from real CMake-injected compile
// definitions (see CMakeLists.txt's about-tde target below), not
// hardcoded placeholder text -- TDE_VERSION_STRING is
// PROJECT_VERSION (currently 0.1.0), and TDE_BUILD_INFO is a short git
// commit hash captured at configure time when the source tree is a git
// checkout, or "unknown (not a git checkout)" otherwise, so this never
// silently claims a build identity it doesn't actually have.
int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("about-tde"));
    app.setOrganizationName(QStringLiteral("tDE"));

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(
        QStringLiteral("tdeVersionString"), QStringLiteral(TDE_VERSION_STRING));
    engine.rootContext()->setContextProperty(
        QStringLiteral("tdeBuildInfo"), QStringLiteral(TDE_BUILD_INFO));

    engine.loadFromModule("TDEMoonlight.About", "AboutWindow");

    if (engine.rootObjects().isEmpty()) {
        return -1;
    }

    return app.exec();
}
