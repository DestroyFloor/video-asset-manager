#include "Db.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QVariant>

const QString Db::kDbDirName = QStringLiteral("工作台数据库");
const QString Db::kThumbDirName = QStringLiteral("thumbnails");

Db::Db(QObject *parent)
    : QObject(parent)
    , m_connectionName(QStringLiteral("workbench_main"))
{
}

Db *Db::instance()
{
    static Db db;
    return &db;
}

QString Db::dbPath() const
{
    return m_root + QLatin1Char('/') + kDbDirName + QStringLiteral("/clips.db");
}

QString Db::thumbDir() const
{
    return m_root + QLatin1Char('/') + kDbDirName + QLatin1Char('/') + kThumbDirName;
}

QString Db::thumbPath(int videoId) const
{
    return thumbDir() + QLatin1Char('/') + QString::number(videoId) + QStringLiteral(".jpg");
}

bool Db::open(const QString &workspaceRoot, QString *error)
{
    const QString root = QDir::fromNativeSeparators(workspaceRoot);
    if (root.isEmpty()) {
        if (error)
            *error = QStringLiteral("未指定工作区文件夹");
        return false;
    }

    if (isOpen() && root == m_root)
        return true;

    close();
    m_root = root;

    QDir().mkpath(m_root + QLatin1Char('/') + kDbDirName);
    QDir().mkpath(thumbDir());

    // 只在「第一次创建这个库」时灌入默认标签。
    // 老库一律不动：否则每次登录都会用 INSERT OR IGNORE 把用户已经删掉的默认标签补回来
    // （tags.name 是 UNIQUE，删掉后名字不存在 → INSERT OR IGNORE 就会重新插入）。
    const bool newDatabase = !QFileInfo::exists(dbPath());

    if (QSqlDatabase::contains(m_connectionName))
        QSqlDatabase::removeDatabase(m_connectionName);

    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    m_db.setDatabaseName(QDir::toNativeSeparators(dbPath()));

    if (!m_db.open()) {
        if (error)
            *error = QStringLiteral("无法打开数据库：%1").arg(m_db.lastError().text());
        return false;
    }

    // 与 backend/db.js 的 PRAGMA 保持一致，但网络路径（\\server\share\…）上不能用 WAL：
    // WAL 依赖共享内存文件（-shm），在 SMB 上不保证可用 —— 尤其是「从一个空目录新建库」时，
    // 切到 WAL 后建表就会失败（表现为登录时报连不上）。共享盘上退回传统 rollback journal。
    const bool networked = m_root.startsWith(QLatin1String("\\\\"))
                           || m_root.startsWith(QLatin1String("//"));
    execute(networked ? QStringLiteral("PRAGMA journal_mode = DELETE")
                      : QStringLiteral("PRAGMA journal_mode = WAL"));
    execute(QStringLiteral("PRAGMA synchronous = NORMAL"));
    execute(QStringLiteral("PRAGMA foreign_keys = ON"));
    execute(QStringLiteral("PRAGMA busy_timeout = 8000"));

    if (!createSchema(error)) {
        close();
        return false;
    }
    if (newDatabase)
        seedDefaultTags();
    return true;
}

bool Db::execute(const QString &sql)
{
    QSqlQuery q(m_db);
    return q.exec(sql);
}

bool Db::createSchema(QString *error)
{
    static const char *kSchema = R"SQL(
CREATE TABLE IF NOT EXISTS videos (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  path TEXT UNIQUE NOT NULL,
  name TEXT,
  parent TEXT,
  ext TEXT,
  size INTEGER DEFAULT 0,
  mtime INTEGER DEFAULT 0,
  duration_ms INTEGER DEFAULT 0,
  width INTEGER DEFAULT 0,
  height INTEGER DEFAULT 0,
  usage_count INTEGER DEFAULT 0,
  codec TEXT DEFAULT '',
  indexed_at INTEGER,
  updated_at INTEGER
);
CREATE TABLE IF NOT EXISTS tags (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  name TEXT UNIQUE NOT NULL,
  category TEXT NOT NULL,
  is_system INTEGER DEFAULT 0,
  created_at INTEGER
);
CREATE TABLE IF NOT EXISTS video_tags (
  video_id INTEGER NOT NULL,
  tag_id INTEGER NOT NULL,
  PRIMARY KEY (video_id, tag_id)
);
CREATE INDEX IF NOT EXISTS idx_videos_parent ON videos(parent);
CREATE INDEX IF NOT EXISTS idx_videos_name ON videos(name);
CREATE INDEX IF NOT EXISTS idx_tags_category ON tags(category);
CREATE INDEX IF NOT EXISTS idx_video_tags_tag ON video_tags(tag_id);
CREATE INDEX IF NOT EXISTS idx_video_tags_video ON video_tags(video_id);
CREATE TABLE IF NOT EXISTS meta (key TEXT PRIMARY KEY, value INTEGER);
)SQL";

    const QStringList statements = QString::fromUtf8(kSchema).split(QLatin1Char(';'), Qt::SkipEmptyParts);
    for (const QString &stmt : statements) {
        const QString sql = stmt.trimmed();
        if (sql.isEmpty())
            continue;
        QSqlQuery q(m_db);
        if (!q.exec(sql)) {
            if (error)
                *error = QStringLiteral("建表失败：%1 (%2)").arg(q.lastError().text(), sql.left(48));
            return false;
        }
    }

    // 旧库可能缺列（对应 db.js 的 migrate()）
    QStringList columns;
    QSqlQuery info(m_db);
    if (info.exec(QStringLiteral("PRAGMA table_info(videos)"))) {
        while (info.next())
            columns << info.value(1).toString();
    }
    if (!columns.contains(QStringLiteral("codec")))
        execute(QStringLiteral("ALTER TABLE videos ADD COLUMN codec TEXT DEFAULT ''"));

    QSqlQuery meta(m_db);
    meta.prepare(QStringLiteral("INSERT OR IGNORE INTO meta (key, value) VALUES ('version', 0)"));
    meta.exec();
    return true;
}

void Db::seedDefaultTags()
{
    // 与 db.js 的 seedDefaultTags() 一致：三个分类的默认示例标签
    struct Seed { const char *category; QStringList names; };
    const QVector<Seed> seeds = {
        { "shot", { QStringLiteral("啃咬过程"), QStringLiteral("产品特写"), QStringLiteral("狗狗反应"),
                    QStringLiteral("开袋"), QStringLiteral("投喂") } },
        { "product", { QStringLiteral("磨牙骨棒"), QStringLiteral("鸭肉黄瓜脆"), QStringLiteral("薄切肉干") } },
        { "availability", { QStringLiteral("可自出"), QStringLiteral("需调色") } },
    };

    QSqlQuery ins(m_db);
    ins.prepare(QStringLiteral(
        "INSERT OR IGNORE INTO tags (name, category, is_system, created_at) VALUES (?, ?, 1, ?)"));

    for (const Seed &seed : seeds) {
        for (const QString &name : seed.names) {
            ins.bindValue(0, name);
            ins.bindValue(1, QString::fromLatin1(seed.category));
            ins.bindValue(2, QDateTime::currentMSecsSinceEpoch());
            ins.exec();
        }
    }
}

void Db::close()
{
    if (m_db.isValid()) {
        if (m_db.isOpen())
            m_db.close();
        m_db = QSqlDatabase();
    }
    if (QSqlDatabase::contains(m_connectionName))
        QSqlDatabase::removeDatabase(m_connectionName);
    m_root.clear();
}

void Db::reopen(const QString &workspaceRoot)
{
    close();
    open(workspaceRoot);
}

int Db::version() const
{
    QSqlQuery q(const_cast<Db *>(this)->m_db);
    if (!q.exec(QStringLiteral("SELECT value FROM meta WHERE key='version'")) || !q.next())
        return 0;
    return q.value(0).toInt();
}

void Db::bumpVersion()
{
    execute(QStringLiteral("UPDATE meta SET value = value + 1 WHERE key='version'"));
}
