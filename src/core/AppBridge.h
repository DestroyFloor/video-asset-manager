#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <qqmlintegration.h>

// 暴露给 QML 的应用门面。
// QML 中的名字是 App（由 QML_NAMED_ELEMENT(App) 指定），用法：App.shareRoot、App.login(...)。
// 注意：若这里写成 QML_ELEMENT，QML 里的类型名会变成 AppBridge，所有 App.xxx 绑定都会失败。
// 登录流程对应旧版 backend/api.js 的 login() / netUse()。
class AppBridge : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(App)
    QML_SINGLETON

    Q_PROPERTY(QString account READ account NOTIFY changed)
    Q_PROPERTY(QString shareRoot READ shareRoot NOTIFY changed)
    Q_PROPERTY(bool rememberAccount READ rememberAccount NOTIFY changed)
    Q_PROPERTY(bool connected READ connected NOTIFY changed)
    Q_PROPERTY(QString dbPath READ dbPath NOTIFY changed)
    Q_PROPERTY(QString dataDir READ dataDir NOTIFY changed)
    Q_PROPERTY(QString andOr READ andOr NOTIFY changed)
    Q_PROPERTY(int dbVersion READ dbVersion NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY changed)
    Q_PROPERTY(QString appVersion READ appVersion CONSTANT)

public:
    explicit AppBridge(QObject *parent = nullptr);

    QString account() const;
    QString shareRoot() const;
    bool rememberAccount() const;
    bool connected() const { return m_connected; }
    QString dbPath() const;
    QString dataDir() const;
    QString andOr() const;
    int dbVersion() const { return m_dbVersion; }
    bool busy() const { return m_busy; }
    QString statusMessage() const { return m_statusMessage; }
    QString appVersion() const;

    // 连接并登录（对应 /api/login）
    Q_INVOKABLE void login(const QString &account, const QString &password,
                           const QString &shareRoot, bool remember);
    // 启动时若已记住账号则尝试直接进入
    Q_INVOKABLE void tryAutoLogin();
    Q_INVOKABLE void logout();
    // 切换工作目录（对应顶部「切换工作目录」，会重开数据库）
    Q_INVOKABLE void switchWorkspace(const QString &path);
    Q_INVOKABLE void setAndOr(const QString &mode);
    Q_INVOKABLE void refreshDbVersion();

signals:
    void changed();
    void busyChanged();
    void loginFinished(bool ok, const QString &message);

private:
    static bool pathAccessible(const QString &root);
    static bool mountShare(const QString &root, const QString &account, const QString &password);
    void setBusy(bool busy, const QString &message);
    void applyConnected(bool ok, const QString &message);

    bool m_connected = false;
    bool m_busy = false;
    int m_dbVersion = 0;
    QString m_statusMessage;
};
