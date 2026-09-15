#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>

// 配置持久化在 config.json，键名沿用既有约定
// （账号、工作区、且/或、外部播放器）。
class Config : public QObject
{
    Q_OBJECT

public:
    static Config *instance();

    // 首次启动时「工作区」的初值
    static const QString kDefaultShare;

    QString shareRoot() const;
    QString rootSeen() const;
    QString account() const;
    bool rememberAccount() const;
    QString andOr() const;
    qint64 lastScanAt() const;
    QString playerExe() const;

    QString dataDir() const { return m_dataDir; }
    QString configPath() const { return m_configPath; }

    QJsonObject all() const { return m_cache; }
    void set(const QJsonObject &patch);
    void save();

private:
    explicit Config(QObject *parent = nullptr);

    void load();
    static QString resolveConfigPath(QString *dataDirOut);

    QString m_dataDir;
    QString m_configPath;
    QJsonObject m_cache;
};
