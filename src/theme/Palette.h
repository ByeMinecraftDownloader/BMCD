#pragma once

#include "theme/TokenBuilder.h"

#include <QColor>
#include <QFileSystemWatcher>
#include <QObject>
#include <QString>
#include <qqmlintegration.h>

// Reads the KDE ColorScheme INI that matugen / DankMaterialShell generates
// (typically ~/.local/share/color-schemes/DankMatugen.colors) and exposes it
// to QML as a singleton, so the downloader always matches the desktop palette.
class Palette : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QString schemeName READ schemeName NOTIFY changed)
    Q_PROPERTY(QString sourcePath READ sourcePath NOTIFY changed)
    Q_PROPERTY(bool live READ live NOTIFY changed)

    Q_PROPERTY(QColor background READ background NOTIFY changed)
    Q_PROPERTY(QColor onBackground READ onBackground NOTIFY changed)
    Q_PROPERTY(QColor surface READ surface NOTIFY changed)
    Q_PROPERTY(QColor surfaceAlt READ surfaceAlt NOTIFY changed)
    Q_PROPERTY(QColor onSurface READ onSurface NOTIFY changed)
    Q_PROPERTY(QColor onSurfaceMuted READ onSurfaceMuted NOTIFY changed)
    Q_PROPERTY(QColor primary READ primary NOTIFY changed)
    Q_PROPERTY(QColor onPrimary READ onPrimary NOTIFY changed)
    Q_PROPERTY(QColor outline READ outline NOTIFY changed)
    Q_PROPERTY(QColor error READ error NOTIFY changed)
    Q_PROPERTY(QColor warning READ warning NOTIFY changed)
    Q_PROPERTY(QColor success READ success NOTIFY changed)
    Q_PROPERTY(QColor link READ link NOTIFY changed)

    // Token surface added for the 1.0 token contract. Kept separate from the
    // legacy palette properties consumed by Tokens.qml.
    Q_PROPERTY(QColor surfaceContainer READ surfaceContainer NOTIFY changed)
    Q_PROPERTY(QColor surfaceContainerHigh READ surfaceContainerHigh NOTIFY changed)
    Q_PROPERTY(QColor primaryContainer READ primaryContainer NOTIFY changed)
    Q_PROPERTY(QColor onPrimaryContainer READ onPrimaryContainer NOTIFY changed)
    Q_PROPERTY(QColor onSurfaceVariant READ onSurfaceVariant NOTIFY changed)
    Q_PROPERTY(QString tokenMode READ tokenMode NOTIFY changed)
    Q_PROPERTY(bool reduceMotion READ reduceMotion NOTIFY changed)

public:
    explicit Palette(QObject *parent = nullptr);
    ~Palette() override = default;

    QString schemeName() const { return m_schemeName; }
    QString sourcePath() const { return m_sourcePath; }
    bool live() const { return m_live; }

    QColor background() const { return m_background; }
    QColor onBackground() const { return m_onBackground; }
    QColor surface() const { return m_surface; }
    QColor surfaceAlt() const { return m_surfaceAlt; }
    QColor onSurface() const { return m_onSurface; }
    QColor onSurfaceMuted() const { return m_onSurfaceMuted; }
    QColor primary() const { return m_primary; }
    QColor onPrimary() const { return m_onPrimary; }
    QColor outline() const { return m_outline; }
    QColor error() const { return m_error; }
    QColor warning() const { return m_warning; }
    QColor success() const { return m_success; }
    QColor link() const { return m_link; }

    QColor surfaceContainer() const { return m_surfaceContainer; }
    QColor surfaceContainerHigh() const { return m_surfaceContainerHigh; }
    QColor primaryContainer() const { return m_primaryContainer; }
    QColor onPrimaryContainer() const { return m_onPrimaryContainer; }
    QColor onSurfaceVariant() const { return m_onSurfaceVariant; }
    QString tokenMode() const { return m_tokenMode; }
    bool reduceMotion() const { return m_reduceMotion; }

    Q_INVOKABLE void reload();
    Q_INVOKABLE QString tokensJson();

signals:
    void changed();

private:
    void discover();
    bool loadFromFile(const QString &path);
    void applyFallback();
    void applyTokens(const theme::Tokens &tokens);
    void rewatch();

    QFileSystemWatcher m_watcher;
    QString m_schemeName;
    QString m_sourcePath;
    bool m_live = false;

    QColor m_background;
    QColor m_onBackground;
    QColor m_surface;
    QColor m_surfaceAlt;
    QColor m_onSurface;
    QColor m_onSurfaceMuted;
    QColor m_primary;
    QColor m_onPrimary;
    QColor m_outline;
    QColor m_error;
    QColor m_warning;
    QColor m_success;
    QColor m_link;

    QColor m_surfaceContainer;
    QColor m_surfaceContainerHigh;
    QColor m_primaryContainer;
    QColor m_onPrimaryContainer;
    QColor m_onSurfaceVariant;
    QString m_tokenMode = QStringLiteral("fallback");
    theme::Tokens m_tokens;
    bool m_reduceMotion = false;
};
