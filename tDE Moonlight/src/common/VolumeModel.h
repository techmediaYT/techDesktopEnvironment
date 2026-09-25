#pragma once

#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QRegularExpression>
#include <QDebug>

// Real system volume/mute state via `wpctl` (WirePlumber's CLI, part of
// PipeWire) -- PipeWire has replaced PulseAudio as the default audio
// server on effectively every mainstream distro this project targets
// (Fedora, Arch, Ubuntu 22.10+), and wpctl ships with it, so this needs
// no extra runtime dependency beyond what the audio stack already
// provides.
//
// There's no D-Bus interface for this (PipeWire's own IPC isn't D-Bus,
// and the old PulseAudio D-Bus module isn't loaded by default anywhere)
// -- process invocation is the actual standard way desktop shells talk
// to it without linking libpipewire directly, which would be a much
// heavier dependency for a quick-settings volume icon.
//
// Registered as a QML singleton (see topbar/main.cpp).
class VolumeModel : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(int volumePercent READ volumePercent WRITE setVolumePercent NOTIFY changed)
    Q_PROPERTY(bool muted READ muted WRITE setMuted NOTIFY changed)

public:
    explicit VolumeModel(QObject *parent = nullptr) : QObject(parent)
    {
        // Confirm wpctl actually exists before relying on it -- a
        // missing binary makes every QProcess call below fail
        // individually anyway, but checking once up front lets
        // `available` reflect that cleanly instead of guessing from
        // repeated process-start failures.
        QProcess which;
        which.start(QStringLiteral("which"), {QStringLiteral("wpctl")});
        which.waitForFinished(1000);
        m_available = (which.exitCode() == 0);

        if (!m_available) {
            qWarning() << "VolumeModel: wpctl not found on PATH -- volume tile will show as unavailable";
            return;
        }

        refresh();

        // wpctl has no push/subscribe mode -- poll instead. 1s is
        // frequent enough that external changes (media keys, another
        // app adjusting volume) show up promptly without meaningfully
        // taxing anything.
        m_pollTimer.setInterval(1000);
        connect(&m_pollTimer, &QTimer::timeout, this, &VolumeModel::refresh);
        m_pollTimer.start();
    }

    bool available() const { return m_available; }
    int volumePercent() const { return m_volumePercent; }
    bool muted() const { return m_muted; }

    void setVolumePercent(int percent)
    {
        if (!m_available) {
            return;
        }
        const int clamped = qBound(0, percent, 100);
        // wpctl takes a 0.0-1.0 fraction, not a percent, for
        // set-volume.
        const QString fraction = QString::number(clamped / 100.0, 'f', 2);
        QProcess::execute(QStringLiteral("wpctl"),
                           {QStringLiteral("set-volume"), QStringLiteral("@DEFAULT_AUDIO_SINK@"), fraction});
        refresh();
    }

    void setMuted(bool muted)
    {
        if (!m_available) {
            return;
        }
        QProcess::execute(QStringLiteral("wpctl"),
                           {QStringLiteral("set-mute"), QStringLiteral("@DEFAULT_AUDIO_SINK@"),
                            muted ? QStringLiteral("1") : QStringLiteral("0")});
        refresh();
    }

public slots:
    Q_INVOKABLE void toggleMute() { setMuted(!m_muted); }
    // +/-5 steps match the increment most desktops bind to media keys,
    // so a click on the volume tile's step buttons feels familiar.
    Q_INVOKABLE void increaseVolume() { setVolumePercent(m_volumePercent + 5); }
    Q_INVOKABLE void decreaseVolume() { setVolumePercent(m_volumePercent - 5); }

signals:
    void changed();

private:
    void refresh()
    {
        QProcess getVolume;
        getVolume.start(QStringLiteral("wpctl"),
                         {QStringLiteral("get-volume"), QStringLiteral("@DEFAULT_AUDIO_SINK@")});
        if (!getVolume.waitForFinished(1000)) {
            return;
        }
        // Output looks like: "Volume: 0.45" or "Volume: 0.45 [MUTED]"
        const QString output = QString::fromUtf8(getVolume.readAllStandardOutput());
        const QRegularExpression re(QStringLiteral("Volume:\\s*([0-9.]+)"));
        const QRegularExpressionMatch match = re.match(output);

        bool changedAny = false;

        if (match.hasMatch()) {
            const double fraction = match.captured(1).toDouble();
            const int newPercent = qRound(fraction * 100.0);
            if (newPercent != m_volumePercent) { m_volumePercent = newPercent; changedAny = true; }
        }

        const bool newMuted = output.contains(QStringLiteral("[MUTED]"));
        if (newMuted != m_muted) { m_muted = newMuted; changedAny = true; }

        if (changedAny) {
            emit changed();
        }
    }

    bool m_available = false;
    int m_volumePercent = 0;
    bool m_muted = false;
    QTimer m_pollTimer;
};
