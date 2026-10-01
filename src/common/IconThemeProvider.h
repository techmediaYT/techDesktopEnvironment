#pragma once

#include <QObject>
#include <QQuickImageProvider>
#include <QIcon>
#include <QColor>
#include <QPainter>

// Registered as a QML singleton (see each main.cpp) alongside the
// "image://icontheme/" provider below. Exists because the provider's
// own fallback-to-generic-icon behavior (see requestPixmap below) is
// exactly wrong for things like "bluetooth-disabled-symbolic": if the
// current icon theme doesn't ship a dedicated disabled/off variant,
// silently drawing the generic Bluetooth glyph instead would make an
// OFF radio look identically to an ON one. QML-side code (StatusIcon,
// TopBar) uses hasIcon() to decide whether to show an icon at all
// instead, per this project's explicit requirement: no icon rather
// than a misleading one.
class IconExistenceChecker : public QObject
{
    Q_OBJECT

public:
    explicit IconExistenceChecker(QObject *parent = nullptr) : QObject(parent) {}

    Q_INVOKABLE bool hasIcon(const QString &iconName) const
    {
        if (iconName.isEmpty()) {
            return false;
        }
        return QIcon::hasThemeIcon(iconName);
    }
};

// Exposes QIcon::fromTheme() to QML as "image://icontheme/<name>". This
// pulls real icons from whatever theme is installed system-wide
// (Adwaita, Breeze, etc) at runtime, instead of bundling our own SVG
// files -- matches the project decision to use the system icon set
// rather than a custom one.
//
// Usage in QML: Image { source: "image://icontheme/network-wireless-symbolic" }
//
// Optionally append "?tint=<hexcolor, no #>" to recolor a symbolic
// (silhouette) icon to a specific solid color, e.g.
// "image://icontheme/network-wireless-symbolic?tint=ffffff". This used
// to be done in QML with MultiEffect's colorization property, but
// MultiEffect is a GPU shader effect -- it doesn't work at all under
// QT_QUICK_BACKEND=software, and even under the normal backend it still
// forces a real GL/EGL context to exist, which is exactly what was
// producing "importing the supplied dmabufs failed" protocol errors
// under Sway in a VM with no working GPU passthrough. Doing the tint
// here instead is a handful of lines of plain CPU pixel compositing --
// no GL context, no shader, works identically on every backend.
class IconThemeProvider : public QQuickImageProvider
{
public:
    IconThemeProvider() : QQuickImageProvider(QQuickImageProvider::Pixmap) {}

    QPixmap requestPixmap(const QString &id, QSize *size, const QSize &requestedSize) override
    {
        // requestedSize is (0,0) if QML didn't set explicit
        // sourceSize -- fall back to a sensible icon size in that case.
        const int w = requestedSize.width() > 0 ? requestedSize.width() : 24;
        const int h = requestedSize.height() > 0 ? requestedSize.height() : 24;

        // Split off an optional "?tint=RRGGBB" suffix. Not using
        // QUrlQuery here since id is already just the raw string after
        // "image://icontheme/", not a full URL -- a plain split is all
        // that's needed for the one parameter this supports.
        QString iconName = id;
        QColor tint;
        const int queryPos = id.indexOf(QLatin1Char('?'));
        if (queryPos >= 0) {
            iconName = id.left(queryPos);
            const QString query = id.mid(queryPos + 1);
            for (const QString &part : query.split(QLatin1Char('&'), Qt::SkipEmptyParts)) {
                const QStringList kv = part.split(QLatin1Char('='));
                if (kv.size() == 2 && kv[0] == QLatin1String("tint")) {
                    tint = QColor(QLatin1Char('#') + kv[1]);
                }
            }
        }

        QIcon icon = QIcon::fromTheme(iconName);
        QPixmap pixmap = icon.pixmap(QSize(w, h));

        if (pixmap.isNull() && iconName != QLatin1String("application-x-executable-symbolic")) {
            // Requested icon name doesn't exist in the current theme --
            // fall back to Adwaita's generic "unknown app" icon rather
            // than leaving a blank gap. The name-equality check avoids
            // infinite recursion/pointlessness if even the fallback
            // itself is missing (e.g. the icon theme isn't installed
            // at all -- see the second fallback below).
            icon = QIcon::fromTheme(QStringLiteral("application-x-executable-symbolic"));
            pixmap = icon.pixmap(QSize(w, h));
        }

        if (pixmap.isNull()) {
            // Still nothing -- likely the icon theme package itself
            // (e.g. adwaita-icon-theme) isn't installed on this system
            // at all. Render blank rather than crash; the topbar/dock
            // will still come up, just without icon glyphs.
            pixmap = QPixmap(w, h);
            pixmap.fill(Qt::transparent);
        } else if (tint.isValid()) {
            // Recolor every opaque/semi-transparent pixel to the
            // requested tint, keeping the original alpha shape --
            // exactly what MultiEffect's colorization used to do, just
            // via QPainter's raster paint engine instead of a shader.
            QPixmap recolored(pixmap.size());
            recolored.fill(Qt::transparent);
            QPainter painter(&recolored);
            painter.drawPixmap(0, 0, pixmap);
            painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
            painter.fillRect(recolored.rect(), tint);
            painter.end();
            pixmap = recolored;
        }

        if (size) {
            *size = pixmap.size();
        }

        return pixmap;
    }
};
