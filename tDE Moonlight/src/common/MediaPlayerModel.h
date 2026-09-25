#pragma once

#include <QObject>
#include <QTimer>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusArgument>
#include <QDBusReply>
#include <QVariantMap>
#include <QDebug>

// Real "now playing" state and transport controls via MPRIS
// (org.mpris.MediaPlayer2.*, session bus) -- the standard Linux media-
// player control protocol implemented by browsers (Firefox, Chrome),
// Spotify, VLC, mpv (with a plugin), and most other players. This is
// what GNOME/KDE/every other desktop's media widget actually talks to.
//
// Picks the first player it finds advertising an
// org.mpris.MediaPlayer2.* bus name. Multiple simultaneously-playing
// players (rare) aren't disambiguated -- a real "pick which player"
// switcher is a reasonable follow-up, out of scope for a quick-settings
// widget that mainly needs to answer "is anything playing, and let me
// pause/skip it".
class MediaPlayerModel : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(QString playerName READ playerName NOTIFY changed)
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(QString artist READ artist NOTIFY changed)
    Q_PROPERTY(bool playing READ playing NOTIFY changed)
    Q_PROPERTY(bool canGoNext READ canGoNext NOTIFY changed)
    Q_PROPERTY(bool canGoPrevious READ canGoPrevious NOTIFY changed)

public:
    explicit MediaPlayerModel(QObject *parent = nullptr) : QObject(parent)
    {
        if (!QDBusConnection::sessionBus().isConnected()) {
            qWarning() << "MediaPlayerModel: no session bus -- media controls will show as unavailable";
            return;
        }

        // NameOwnerChanged fires whenever any MPRIS player appears or
        // disappears (starts/quits, or just registers/deregisters its
        // MPRIS interface) -- that's the trigger to re-scan for a
        // player, rather than polling.
        QDBusConnection::sessionBus().connect(
            QStringLiteral("org.freedesktop.DBus"),
            QStringLiteral("/org/freedesktop/DBus"),
            QStringLiteral("org.freedesktop.DBus"),
            QStringLiteral("NameOwnerChanged"),
            this, SLOT(handleNameOwnerChanged(QString, QString, QString)));

        findPlayer();
    }

    bool available() const { return !m_serviceName.isEmpty(); }
    QString playerName() const { return m_playerName; }
    QString title() const { return m_title; }
    QString artist() const { return m_artist; }
    bool playing() const { return m_playing; }
    bool canGoNext() const { return m_canGoNext; }
    bool canGoPrevious() const { return m_canGoPrevious; }

public slots:
    Q_INVOKABLE void playPause() { callPlayerMethod(QStringLiteral("PlayPause")); }
    Q_INVOKABLE void next() { callPlayerMethod(QStringLiteral("Next")); }
    Q_INVOKABLE void previous() { callPlayerMethod(QStringLiteral("Previous")); }

signals:
    void changed();

private slots:
    void handleNameOwnerChanged(const QString &name, const QString &oldOwner, const QString &newOwner)
    {
        Q_UNUSED(oldOwner)
        if (!name.startsWith(QLatin1String("org.mpris.MediaPlayer2."))) {
            return;
        }
        if (newOwner.isEmpty() && name == m_serviceName) {
            // Our tracked player just quit/deregistered -- drop it and
            // look for any other player that might still be running.
            m_serviceName.clear();
        }
        findPlayer();
    }

    // Polls the currently-tracked player's Metadata/PlaybackStatus.
    // MPRIS players are supposed to emit PropertiesChanged too, but
    // support for that is inconsistent enough across real players
    // (browsers especially) that a short poll is more reliably correct
    // than trusting every player to signal properly -- battery/CPU cost
    // is negligible at this interval for a background poll.
    void pollActivePlayer()
    {
        if (m_serviceName.isEmpty()) {
            return;
        }
        refreshFromService(m_serviceName);
    }

private:
    void findPlayer()
    {
        if (!m_serviceName.isEmpty()) {
            // Already tracking a live player -- keep it rather than
            // arbitrarily jumping to a different one just because
            // another player also appeared.
            refreshFromService(m_serviceName);
            return;
        }

        QDBusConnectionInterface *busIface = QDBusConnection::sessionBus().interface();
        if (!busIface) {
            return;
        }
        const QStringList allNames = busIface->registeredServiceNames();
        for (const QString &name : allNames) {
            if (name.startsWith(QLatin1String("org.mpris.MediaPlayer2."))) {
                m_serviceName = name;
                refreshFromService(name);
                if (!m_pollTimer.isActive()) {
                    m_pollTimer.setInterval(1500);
                    connect(&m_pollTimer, &QTimer::timeout, this, &MediaPlayerModel::pollActivePlayer);
                    m_pollTimer.start();
                }
                return;
            }
        }

        // Nothing found -- clear to the honest "no app selected for
        // media play" state rather than leaving stale metadata behind
        // from whatever last played.
        if (available()) {
            m_serviceName.clear();
            m_playerName.clear();
            m_title.clear();
            m_artist.clear();
            m_playing = false;
            m_canGoNext = false;
            m_canGoPrevious = false;
            emit changed();
        }
    }

    void refreshFromService(const QString &serviceName)
    {
        QDBusInterface propsIface(
            serviceName,
            QStringLiteral("/org/mpris/MediaPlayer2"),
            QStringLiteral("org.freedesktop.DBus.Properties"),
            QDBusConnection::sessionBus());
        if (!propsIface.isValid()) {
            m_serviceName.clear();
            return;
        }

        bool changedAny = false;

        QDBusReply<QString> identityReply = QDBusInterface(
            serviceName, QStringLiteral("/org/mpris/MediaPlayer2"),
            QStringLiteral("org.freedesktop.DBus.Properties"), QDBusConnection::sessionBus())
            .call(QStringLiteral("Get"), QStringLiteral("org.mpris.MediaPlayer2"), QStringLiteral("Identity"));
        const QString newPlayerName = identityReply.isValid() ? identityReply.value() : serviceName;
        if (newPlayerName != m_playerName) { m_playerName = newPlayerName; changedAny = true; }

        QDBusReply<QVariant> statusReply = propsIface.call(
            QStringLiteral("Get"), QStringLiteral("org.mpris.MediaPlayer2.Player"), QStringLiteral("PlaybackStatus"));
        const bool newPlaying = statusReply.isValid() && statusReply.value().toString() == QLatin1String("Playing");
        if (newPlaying != m_playing) { m_playing = newPlaying; changedAny = true; }

        QDBusReply<QVariant> canNextReply = propsIface.call(
            QStringLiteral("Get"), QStringLiteral("org.mpris.MediaPlayer2.Player"), QStringLiteral("CanGoNext"));
        const bool newCanNext = canNextReply.isValid() && canNextReply.value().toBool();
        if (newCanNext != m_canGoNext) { m_canGoNext = newCanNext; changedAny = true; }

        QDBusReply<QVariant> canPrevReply = propsIface.call(
            QStringLiteral("Get"), QStringLiteral("org.mpris.MediaPlayer2.Player"), QStringLiteral("CanGoPrevious"));
        const bool newCanPrev = canPrevReply.isValid() && canPrevReply.value().toBool();
        if (newCanPrev != m_canGoPrevious) { m_canGoPrevious = newCanPrev; changedAny = true; }

        QDBusReply<QVariant> metadataReply = propsIface.call(
            QStringLiteral("Get"), QStringLiteral("org.mpris.MediaPlayer2.Player"), QStringLiteral("Metadata"));
        QString newTitle;
        QString newArtist;
        if (metadataReply.isValid()) {
            const QVariantMap metadata = qdbus_cast<QVariantMap>(metadataReply.value().value<QDBusArgument>());
            newTitle = metadata.value(QStringLiteral("xesam:title")).toString();
            const QStringList artists = metadata.value(QStringLiteral("xesam:artist")).toStringList();
            if (!artists.isEmpty()) {
                newArtist = artists.join(QStringLiteral(", "));
            }
        }
        if (newTitle != m_title) { m_title = newTitle; changedAny = true; }
        if (newArtist != m_artist) { m_artist = newArtist; changedAny = true; }

        if (changedAny) {
            emit changed();
        }
    }

    void callPlayerMethod(const QString &method)
    {
        if (m_serviceName.isEmpty()) {
            return;
        }
        QDBusInterface playerIface(
            m_serviceName,
            QStringLiteral("/org/mpris/MediaPlayer2"),
            QStringLiteral("org.mpris.MediaPlayer2.Player"),
            QDBusConnection::sessionBus());
        if (playerIface.isValid()) {
            playerIface.call(QDBus::NoBlock, method);
            // Refresh shortly after -- PlayPause/Next/Previous change
            // PlaybackStatus/Metadata, and re-polling right away (rather
            // than waiting for the next 1.5s tick) makes the button feel
            // responsive instead of laggy.
            QTimer::singleShot(150, this, [this]() { refreshFromService(m_serviceName); });
        }
    }

    QString m_serviceName;
    QString m_playerName;
    QString m_title;
    QString m_artist;
    bool m_playing = false;
    bool m_canGoNext = false;
    bool m_canGoPrevious = false;
    QTimer m_pollTimer;
};
