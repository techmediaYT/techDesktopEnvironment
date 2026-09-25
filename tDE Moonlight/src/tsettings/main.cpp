#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QCommandLineParser>
#include <QCommandLineOption>

// tSettings -- the full system settings app referenced from
// ControlPanel.qml's "All settings.." row. Not implemented yet: this is
// a real, buildable placeholder (not a dangling reference to a binary
// that doesn't exist) that shows a single "coming soon" window and
// accepts the --page flag SettingsLauncher already passes, so wiring
// deep-links from other panels (Wi-Fi tile's "..." button, for example)
// doesn't need to change again once the real panels land -- only
// PlaceholderWindow.qml's content does.
int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("tsettings"));
    app.setOrganizationName(QStringLiteral("tDE"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("tDE Moonlight system settings"));
    parser.addHelpOption();
    QCommandLineOption pageOption(
        QStringLiteral("page"),
        QStringLiteral("Open directly to a specific settings page (not implemented yet)."),
        QStringLiteral("page"));
    parser.addOption(pageOption);
    parser.process(app);

    QQmlApplicationEngine engine;
    engine.setInitialProperties({
        {QStringLiteral("requestedPage"), parser.value(pageOption)}
    });
    engine.loadFromModule("TDEMoonlight.Settings", "PlaceholderWindow");

    if (engine.rootObjects().isEmpty()) {
        return -1;
    }

    return app.exec();
}
