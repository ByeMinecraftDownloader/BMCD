#pragma once

#include <QObject>
#include <QSet>
#include <QString>
#include <QtQml/qqmlregistration.h>

// Knows which versions are already present in the target directory, so the list
// can show an "installed" badge without the user opening the folder.
class InstalledVersions : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(int count READ count NOTIFY changed)
    Q_PROPERTY(int revision READ revision NOTIFY changed)
    Q_PROPERTY(QString root READ root CONSTANT)

public:
    explicit InstalledVersions(QObject *parent = nullptr);

    int count() const { return static_cast<int>(m_ids.size()); }
    int revision() const { return m_revision; }
    QString root() const;

    Q_INVOKABLE bool contains(const QString &versionId) const { return m_ids.contains(versionId); }
    Q_INVOKABLE void rescan();

signals:
    void changed();

private:
    QSet<QString> m_ids;
    int m_revision = 0;
};
