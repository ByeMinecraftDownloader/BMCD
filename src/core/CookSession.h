#pragma once

#include <QObject>
#include <QPointer>
#include <QVariantList>
#include <QVector>
#include <QtQml/qqmlregistration.h>

class JavaLocator;
class LoaderInstaller;
class PackController;
class VersionDetail;

// One cooking job, three ordered steps: game body -> loader -> mods.
//
// The old QML screen merged three independent controllers itself, which made
// "which one failed?" depend on the order of property bindings. This class is
// the single state machine behind the cooking screen: it starts each step
// only after the previous one finished, keeps per-step state/progress for the
// UI, and retries only the failed step.
class CookSession : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QString state READ state NOTIFY changed)          // idle|running|paused|done|error
    Q_PROPERTY(double progress READ progress NOTIFY changed)
    Q_PROPERTY(QVariantList steps READ steps NOTIFY changed)
    Q_PROPERTY(QString currentStep READ currentStep NOTIFY changed)
    Q_PROPERTY(QString currentStepTitle READ currentStepTitle NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(bool hasPlan READ hasPlan NOTIFY changed)
    Q_PROPERTY(QString planName READ planName NOTIFY changed)
    Q_PROPERTY(QString hashLabel READ hashLabel NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool canPause READ canPause NOTIFY changed)

    // Loader choice made on the order screen (not the mods picker). Empty or
    // "vanilla" means no loader step.
    Q_PROPERTY(QString loaderType READ loaderType WRITE setLoaderType NOTIFY changed)
    Q_PROPERTY(QString loaderVersion READ loaderVersion WRITE setLoaderVersion NOTIFY changed)
    Q_PROPERTY(QString minecraftVersion READ minecraftVersion NOTIFY changed)
    Q_PROPERTY(bool hasLoader READ hasLoader NOTIFY changed)
    Q_PROPERTY(QString requiredLoader READ requiredLoader NOTIFY changed)
    Q_PROPERTY(bool loaderSelectedForPlan READ loaderSelectedForPlan NOTIFY changed)
    Q_PROPERTY(bool loaderReady READ loaderReady NOTIFY changed)
    Q_PROPERTY(bool javaReady READ javaReady NOTIFY changed)
    Q_PROPERTY(QString javaVersion READ javaVersion NOTIFY changed)
    Q_PROPERTY(QString loaderProfileId READ loaderProfileId NOTIFY changed)
    Q_PROPERTY(QString loaderProfileName READ loaderProfileName NOTIFY changed)
    Q_PROPERTY(QString loaderVersionId READ loaderVersionId NOTIFY changed)
    Q_PROPERTY(QString loaderLogPath READ loaderLogPath NOTIFY changed)
    Q_PROPERTY(bool loaderInstalled READ loaderInstalled NOTIFY changed)

    Q_PROPERTY(QVariantList loaderVersions READ loaderVersions NOTIFY changed)
    Q_PROPERTY(bool loaderVersionsLoading READ loaderVersionsLoading NOTIFY changed)
    Q_PROPERTY(QString loaderVersionsError READ loaderVersionsError NOTIFY changed)

public:
    explicit CookSession(QObject *parent = nullptr);

    QString state() const { return m_state; }
    double progress() const;
    QVariantList steps() const;
    QString currentStep() const;
    QString currentStepTitle() const;
    QString error() const { return m_error; }
    bool hasPlan() const;
    QString planName() const;
    QString hashLabel() const;
    bool busy() const { return m_state == QStringLiteral("running") || m_state == QStringLiteral("paused"); }
    bool canPause() const;

    QString loaderType() const { return m_loaderType; }
    void setLoaderType(const QString &type);
    QString loaderVersion() const { return m_loaderVersion; }
    void setLoaderVersion(const QString &version);
    QString minecraftVersion() const;
    bool hasLoader() const;
    QString requiredLoader() const;
    bool loaderSelectedForPlan() const;
    bool loaderReady() const;
    bool javaReady() const;
    QString javaVersion() const;
    QString loaderProfileId() const;
    QString loaderProfileName() const;
    QString loaderVersionId() const { return m_loaderVersionId; }
    QString loaderLogPath() const;
    bool loaderInstalled() const { return m_loaderInstalled; }

    QVariantList loaderVersions() const;
    bool loaderVersionsLoading() const;
    QString loaderVersionsError() const;

    Q_INVOKABLE void prepare();
    Q_INVOKABLE void start();
    Q_INVOKABLE void retry();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void resume();
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void refreshLoaderVersions();

signals:
    void changed();

private:
    struct Step
    {
        QString key;
        QString title;
        QString state;      // pending | running | done | skipped | error
        QString detail;
    };

    bool ensureAttached();
    void resetSteps();
    void buildSteps();
    Step *step(const QString &key);
    const Step *step(const QString &key) const;
    void markStep(const QString &key, const QString &state, const QString &detail = QString());
    void advance();
    void beginLoader();
    void refreshInstalledLoader();
    void finish();
    void fail(const QString &message, const QString &stepKey);
    double stepProgress(const Step &step) const;
    double gameProgress() const;
    double packProgress() const;
    bool gameDone() const;
    bool loaderDone() const;
    bool modsDone() const;
    bool gameFromPack() const;
    void onVersionChanged();
    void onPackChanged();
    void onLoaderChanged();
    void onLoaderFinished(bool ok);

    QPointer<VersionDetail> m_version;
    QPointer<PackController> m_pack;
    QPointer<JavaLocator> m_java;
    LoaderInstaller *m_loader = nullptr;
    bool m_attached = false;

    QString m_state = QStringLiteral("idle");
    QString m_error;
    QString m_failedStep;
    QString m_loaderType = QStringLiteral("vanilla");
    QString m_loaderVersion;
    QString m_loaderVersionId;
    bool m_loaderInstalled = false;

    bool m_gameStarted = false;
    bool m_loaderStarted = false;
    bool m_modsStarted = false;
    QVector<Step> m_steps;
};
