#pragma once

#include <QObject>
#include <QProcess>
#include <QStandardPaths>
#include <QString>

// Backs the "All settings.." row in ControlPanel.qml. tSettings itself
// (the actual settings app, with real Wi-Fi/Bluetooth/display/sound
// panels) isn't built yet -- this launcher is written against where it
// will live once it exists, and degrades to an honest "coming soon"
// signal in the meantime rather than silently doing nothing on click
// (which is what a bare console.log leaves the user with).
//
// Registered as a QML singleton (see topbar/main.cpp).
class SettingsLauncher : public QObject
{
    Q_OBJECT

public:
    explicit SettingsLauncher(QObject *parent = nullptr) : QObject(parent) {}

public slots:
    // Looks for a "tsettings" binary on PATH (this is what `tsettings`
    // in scripts/ or a future package would install) and launches it
    // detached if found. If it isn't installed yet -- the expected case
    // right now -- emits notImplementedYet() instead so the UI can show
    // a "tSettings -- coming soon" toast/state rather than a dead click.
    Q_INVOKABLE void openSettings(const QString &page = QString())
    {
        const QString settingsBinary = QStandardPaths::findExecutable(QStringLiteral("tsettings"));

        if (settingsBinary.isEmpty()) {
            emit notImplementedYet();
            return;
        }

        QStringList args;
        if (!page.isEmpty()) {
            // Deep-link into a specific panel, e.g. openSettings("network")
            // -> `tsettings --page network`, once tSettings actually
            // parses that flag.
            args << QStringLiteral("--page") << page;
        }

        if (!QProcess::startDetached(settingsBinary, args)) {
            emit notImplementedYet();
        }
    }

signals:
    // UI-facing signal for "tSettings isn't available on this system
    // yet" -- ControlPanel.qml listens for this to show a brief
    // "tSettings -- coming soon" state instead of doing nothing.
    void notImplementedYet();
};
