#pragma once

#include <QObject>
#include <QSqlDatabase>
#include <QString>

// 对应旧版 backend/db.js。
// 库文件位置、表结构、PRAGMA 全部与旧版逐字段一致，新旧程序读写同一个云盘数据库。
class Db : public QObject
{
    Q_OBJECT

public:
    static Db *instance();

    static const QString kDbDirName;    // 工作台数据库（云盘共享目录）
    static const QString kThumbDirName; // thumbnails（封面缓存）

    // 打开 <workspaceRoot>/工作台数据库/clips.db；失败时 error 带原因。
    // 由 AppBridge 在登录 / 切换工作目录时调用。
    bool open(const QString &workspaceRoot, QString *error = nullptr);
    void close();
    void reopen(const QString &workspaceRoot);

    bool isOpen() const { return m_db.isValid() && m_db.isOpen(); }
    QString workspaceRoot() const { return m_root; }
    QString dbPath() const;
    QString thumbDir() const;
    QString thumbPath(int videoId) const;

    // meta.version（旧版用它做多端实时同步的变更计数）
    int version() const;
    void bumpVersion();

    QSqlDatabase &handle() { return m_db; }

private:
    explicit Db(QObject *parent = nullptr);

    bool execute(const QString &sql);
    bool createSchema(QString *error);
    void seedDefaultTags();

    QSqlDatabase m_db;
    QString m_root;
    QString m_connectionName;
};
