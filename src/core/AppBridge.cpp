#include "AppBridge.h"

#include "Config.h"
#include "Db.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QStringList>

AppBridge::AppBridge(QObject *parent) : QObject(parent) {}

QString AppBridge::account() const { return Config::instance()->account(); }
QString AppBridge::shareRoot() const { return Config::instance()->shareRoot(); }
bool AppBridge::rememberAccount() const { return Config::instance()->rememberAccount(); }
QString AppBridge::dataDir() const { return Config::instance()->dataDir(); }
QString AppBridge::andOr() const { return Config::instance()->andOr(); }
QString AppBridge::appVersion() const { return QCoreApplication::applicationVersion(); }

QString AppBridge::dbPath() const
{
    return Db::instance()->isOpen() ? Db::instance()->dbPath() : QString();
}

bool AppBridge::pathAccessible(const QString &root)
{
    if (root.trimmed().isEmpty())
        return false;
    const QFileInfo fi(root);
    return fi.exists() && fi.isDir();
}

// 对应 backend/api.js 的 netUse()：\\server\share 级别挂载，带可选凭证
bool AppBridge::mountShare(const QString &root, const QString &account, const QString &password)
{
    const QString seg =
        QString(root).replace(QRegularExpression(QStringLiteral("[\\\\/]+")), QStringLiteral("\\"));
    const QStringList parts = seg.split(QLatin1Char('\\'), Qt::SkipEmptyParts);
    if (parts.size() < 2)
        return false;

    const QString unc = QStringLiteral("\\\\%1\\%2").arg(parts.at(0), parts.at(1));
    QStringList args{ QStringLiteral("use"), unc };
    if (!account.isEmpty())
        args << QStringLiteral("/user:") + account;
    if (!password.isEmpty())
        args << password;

    QProcess proc;
    proc.start(QStringLiteral("net"), args, QIODevice::ReadOnly);
    if (!proc.waitForStarted(5000))
        return false;
    if (!proc.waitForFinished(30000)) {
        proc.kill();
        proc.waitForFinished(2000);
        return false;
    }
    return proc.exitCode() == 0;
}

void AppBridge::setBusy(bool busy, const QString &message)
{
    if (m_busy == busy && m_statusMessage == message)
        return;
    m_busy = busy;
    m_statusMessage = message;
    emit busyChanged();
    emit changed();
}

void AppBridge::applyConnected(bool ok, const QString &message)
{
    m_connected = ok;
    m_statusMessage = message;
    m_dbVersion = ok ? Db::instance()->version() : 0;
    emit changed();
}

// 工作区路径规范化。从聊天窗口 / 资源管理器复制来的路径经常带引号、正斜杠，
// 或者少了 UNC 的前导 \\（例如 server\共享\目录）—— 少了它会变成「相对路径」，
// 数据库就被建到程序自己的目录里去了，界面上看起来就是「连上了但什么都没有」。
static QString normalizeSharePath(const QString &in)
{
    QString s = in.trimmed();
    if (s.size() >= 2 && s.startsWith(QLatin1Char('"')) && s.endsWith(QLatin1Char('"')))
        s = s.mid(1, s.size() - 2).trimmed();
    s.replace(QLatin1Char('/'), QLatin1Char('\\'));
    while (s.endsWith(QLatin1Char('\\')) && s.size() > 2)
        s.chop(1);
    if (s.startsWith(QLatin1String("\\\\")))
        return s;

    // 形如 server\share\… 或 host.domain\share\… → 补上 UNC 前缀
    const int cut = s.indexOf(QLatin1Char('\\'));
    if (cut > 0) {
        const QString host = s.left(cut);
        if (host.contains(QLatin1Char('.')) && !host.contains(QLatin1Char(' ')))
            return QStringLiteral("\\\\") + s;
    }
    return s;
}

void AppBridge::login(const QString &account, const QString &password, const QString &shareRoot,
                      bool remember)
{
    setBusy(true, QStringLiteral("正在连接共享路径…"));

    Config *cfg = Config::instance();
    const QString typed = normalizeSharePath(shareRoot);
    QJsonObject patch;
    if (!typed.isEmpty())
        patch[QStringLiteral("shareRoot")] = typed;
    patch[QStringLiteral("account")] = account.trimmed();
    patch[QStringLiteral("rememberAccount")] = remember;
    cfg->set(patch);

    // 直接用刚输入并规范化过的路径，不要回头读 config：
    // 输入框为空时读回来的是上一次的旧值，会连到别的目录还以为连上了。
    const QString root = typed.isEmpty() ? normalizeSharePath(cfg->shareRoot()) : typed;
    QString message;

    bool ok = pathAccessible(root);
    if (!ok)
        ok = mountShare(root, account.trimmed(), password);

    if (!ok) {
        setBusy(false, QStringLiteral("无法访问工作区：") + root);
        applyConnected(false, m_statusMessage);
        emit loginFinished(false, m_statusMessage);
        return;
    }

    cfg->set(QJsonObject{ { QStringLiteral("rootSeen"), root } });

    QString error;
    if (!Db::instance()->open(root, &error)) {
        setBusy(false, error);
        applyConnected(false, error);
        emit loginFinished(false, error);
        return;
    }

    // 状态里带上实际连到的目录，避免「连上了但不知道连的是哪儿」
    message = QStringLiteral("已连接 · ") + root;
    setBusy(false, message);
    applyConnected(true, message);
    emit loginFinished(true, message);
}

// 启动时：已记住账号则尝试直接进入（无密码，只够用已挂载的共享）
void AppBridge::tryAutoLogin()
{
    Config *cfg = Config::instance();
    if (cfg->account().trimmed().isEmpty()) {
        emit changed();
        return;
    }
    const QString root = normalizeSharePath(cfg->rootSeen().isEmpty() ? cfg->shareRoot()
                                                                     : cfg->rootSeen());
    QString error;
    if (Db::instance()->open(root, &error))
        applyConnected(true, QStringLiteral("已连接 · ") + root);
    else
        applyConnected(false, error);
}

void AppBridge::logout()
{
    Db::instance()->close();
    applyConnected(false, QString());
}

void AppBridge::switchWorkspace(const QString &path)
{
    const QString target = normalizeSharePath(path);
    if (target.isEmpty())
        return;

    Config::instance()->set(QJsonObject{
        { QStringLiteral("shareRoot"), target },
        { QStringLiteral("rootSeen"), target },
    });

    QString error;
    const bool ok = Db::instance()->open(target, &error);
    applyConnected(ok,
                   ok ? QStringLiteral("已切换工作目录 · ") + target + QStringLiteral("（建议重新扫描素材库）")
                      : error);
}

void AppBridge::setAndOr(const QString &mode)
{
    const QString value = (mode == QLatin1String("or")) ? QStringLiteral("or") : QStringLiteral("and");
    if (Config::instance()->andOr() == value)
        return;
    Config::instance()->set(QJsonObject{ { QStringLiteral("andOr"), value } });
    emit changed();
}

void AppBridge::refreshDbVersion()
{
    const int v = Db::instance()->isOpen() ? Db::instance()->version() : 0;
    if (v == m_dbVersion)
        return;
    m_dbVersion = v;
    emit changed();
}
