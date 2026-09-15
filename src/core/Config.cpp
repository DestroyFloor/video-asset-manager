#include "Config.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QStandardPaths>

// 首次启动时「工作区」输入框的初值。留空，由用户自己填。
const QString Config::kDefaultShare = QString();

Config::Config(QObject *parent) : QObject(parent)
{
    m_configPath = resolveConfigPath(&m_dataDir);
    load();
}

Config *Config::instance()
{
    static Config cfg;
    return &cfg;
}

// 配置目录优先级（沿用旧版 config.js 的取舍）：
//   1. CLIPWORK_DATA 环境变量（旧版 Electron 主进程注入的 userData）
//   2. %APPDATA%\clips-workbench（旧版打包后的 Electron userData 目录名）
//   3. %APPDATA%\ClipsWorkbench
//   4. 可执行文件旁的 data 目录
QString Config::resolveConfigPath(QString *dataDirOut)
{
    QString dir;

    const QByteArray env = qgetenv("CLIPWORK_DATA");
    if (!env.isEmpty()) {
        dir = QString::fromLocal8Bit(env);
    } else {
        const QString appData = qEnvironmentVariable("APPDATA");
        if (!appData.isEmpty()) {
            const QString legacy = appData + QStringLiteral("/clips-workbench");
            if (QFileInfo::exists(legacy + QStringLiteral("/config.json"))) {
                dir = legacy;
            } else {
                dir = appData + QStringLiteral("/ClipsWorkbench");
            }
        } else {
            dir = QCoreApplication::applicationDirPath() + QStringLiteral("/data");
        }
    }

    if (dataDirOut)
        *dataDirOut = dir;
    return dir + QStringLiteral("/config.json");
}

void Config::load()
{
    QDir().mkpath(m_dataDir);

    QFile f(m_configPath);
    if (f.open(QIODevice::ReadOnly)) {
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        if (doc.isObject())
            m_cache = doc.object();
        f.close();
    }

    // 默认值与 backend/config.js 的 load() 对齐
    if (!m_cache.contains(QStringLiteral("shareRoot")))
        m_cache[QStringLiteral("shareRoot")] = kDefaultShare;
    if (!m_cache.contains(QStringLiteral("rootSeen")))
        m_cache[QStringLiteral("rootSeen")] = m_cache.value(QStringLiteral("shareRoot")).toString();
    if (!m_cache.contains(QStringLiteral("account")))
        m_cache[QStringLiteral("account")] = QString();
    if (!m_cache.contains(QStringLiteral("rememberAccount")))
        m_cache[QStringLiteral("rememberAccount")] = false;
    if (!m_cache.contains(QStringLiteral("andOr")))
        m_cache[QStringLiteral("andOr")] = QStringLiteral("and");
    if (!m_cache.contains(QStringLiteral("lastScanAt")))
        m_cache[QStringLiteral("lastScanAt")] = 0;
    if (!m_cache.contains(QStringLiteral("playerExe")))
        m_cache[QStringLiteral("playerExe")] = QString();
}

void Config::save()
{
    QDir().mkpath(m_dataDir);
    QFile f(m_configPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    f.write(QJsonDocument(m_cache).toJson(QJsonDocument::Indented));
    f.close();
}

void Config::set(const QJsonObject &patch)
{
    for (auto it = patch.begin(); it != patch.end(); ++it)
        m_cache[it.key()] = it.value();
    save();
}

QString Config::shareRoot() const { return m_cache.value(QStringLiteral("shareRoot")).toString(); }
QString Config::rootSeen() const { return m_cache.value(QStringLiteral("rootSeen")).toString(); }
QString Config::account() const { return m_cache.value(QStringLiteral("account")).toString(); }
bool Config::rememberAccount() const { return m_cache.value(QStringLiteral("rememberAccount")).toBool(); }
QString Config::andOr() const { return m_cache.value(QStringLiteral("andOr")).toString(); }
qint64 Config::lastScanAt() const { return static_cast<qint64>(m_cache.value(QStringLiteral("lastScanAt")).toDouble()); }
QString Config::playerExe() const { return m_cache.value(QStringLiteral("playerExe")).toString(); }
