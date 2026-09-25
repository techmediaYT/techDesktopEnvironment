#include <QCoreApplication>

#include "DesktopDaemon.h"

// No GUI here -- QCoreApplication, not QGuiApplication. This binary is
// meant to run once per session in the background (started by the
// session's autostart/systemd user unit, not launched by the user),
// owning shared state (pinned apps) that the topbar/dock/app-menu
// processes talk to over D-Bus.
int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("tded"));
    app.setOrganizationName(QStringLiteral("tDE"));

    DesktopDaemon daemon;

    return app.exec();
}
