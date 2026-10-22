#pragma once

#include <QObject>
#include <QString>
#include <QUrl>

// Small persistent settings surface shared by QML and the download paths.
// Values live in QSettings, so they survive restarts without a database.
class AppSettings : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString targetDir READ targetDir WRITE setTargetDir NOTIFY targetDirChanged)
    // Parent directory chosen by the user. Empty means the platform cache.
    Q_PROPERTY(QString downloadTempDir READ downloadTempDir WRITE setDownloadTempDir
                       NOTIFY downloadTempDirChanged)
    // Effective app-owned download root (default cache, or <chosen>/BMCD/downloads).
    Q_PROPERTY(QString downloadRoot READ downloadRoot NOTIFY downloadTempDirChanged)
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)
    Q_PROPERTY(int downloadConcurrency READ downloadConcurrency WRITE setDownloadConcurrency
                       NOTIFY downloadConcurrencyChanged)
    Q_PROPERTY(bool reduceMotion READ reduceMotion WRITE setReduceMotion NOTIFY reduceMotionChanged)
    Q_PROPERTY(QString cdnBase READ cdnBase WRITE setCdnBase NOTIFY cdnBaseChanged)
    Q_PROPERTY(QString mirrorBase READ mirrorBase WRITE setMirrorBase NOTIFY mirrorBaseChanged)
    Q_PROPERTY(bool mirrorFailover READ mirrorFailover WRITE setMirrorFailover
                       NOTIFY mirrorFailoverChanged)
    Q_PROPERTY(QString javaPath READ javaPath WRITE setJavaPath NOTIFY javaPathChanged)

public:
    explicit AppSettings(QObject *parent = nullptr);

    QString targetDir() const;
    void setTargetDir(const QString &dir);

    QString downloadTempDir() const;
    void setDownloadTempDir(const QString &dir);
    QString downloadRoot() const;
    // QML FolderDialog hands us a URL; convert it without percent-encoding
    // spaces in the selected path.
    Q_INVOKABLE QString localPath(const QUrl &url) const;

    QString language() const;
    void setLanguage(const QString &language);

    int downloadConcurrency() const;
    void setDownloadConcurrency(int concurrency);

    bool reduceMotion() const;
    void setReduceMotion(bool reduced);

    QString cdnBase() const;
    void setCdnBase(const QString &base);
    QString mirrorBase() const;
    void setMirrorBase(const QString &base);
    bool mirrorFailover() const;
    void setMirrorFailover(bool enabled);

    // Empty means "auto-detect". The value is a user-selected executable (or
    // JDK home); relative paths are ignored.
    QString javaPath() const;
    void setJavaPath(const QString &path);
    static QString configuredJavaPath();

    // Read-only helpers for C++ call sites; the environment override stays
    // available for tests and for the CLI's --target.
    static QString resolvedTargetDir();
    static QString configuredDownloadTempDir();
    static QString resolvedDownloadRoot();
    static int concurrency();
    static bool reducedMotion();
    static QString configuredCdnBase();
    static QString configuredMirrorBase();
    static bool failoverEnabled();

signals:
    void targetDirChanged();
    void downloadTempDirChanged();
    void languageChanged();
    void downloadConcurrencyChanged();
    void reduceMotionChanged();
    void cdnBaseChanged();
    void mirrorBaseChanged();
    void mirrorFailoverChanged();
    void javaPathChanged();

private:
    static QString storedTargetDir();
};
