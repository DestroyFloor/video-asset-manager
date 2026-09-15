#include "Scanner.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QPair>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>
#include <QVector>

#include "Db.h"
#include "MediaMeta.h"

namespace {

// 与 backend/scanner.js / db.js 的 EXCLUDE_DIRS 保持一致；另外跳过隐藏目录
bool isExcludedDir(const QString &name)
{
    static const QStringList skip = {
        QStringLiteral("工作台数据库"), QStringLiteral("thumbnails"),
        QStringLiteral("proxies"),      QStringLiteral("previews"),
        QStringLiteral(".clipwork"),    QStringLiteral(".ds_workbench"),
        QStringLiteral("工作台"),
    };
    return skip.contains(name) || name.startsWith(QLatin1Char('.'));
}

} // namespace

Scanner::Scanner(QObject *parent) : QObject(parent)
{
    m_timer.setSingleShot(true);
    m_timer.setInterval(0); // 每批之间让出一次事件循环
    connect(&m_timer, &QTimer::timeout, this, &Scanner::step);
}

void Scanner::start()
{
    if (scanning())
        return;

    Db *db = Db::instance();
    if (!db->isOpen()) {
        m_error = QStringLiteral("数据库未打开");
        emit stateChanged();
        emit failed(m_error);
        return;
    }

    m_root = db->workspaceRoot();
    m_error.clear();
    m_message.clear();
    m_phaseText = QStringLiteral("扫描目录");
    m_pendingDirs = QStringList{ m_root };
    m_files.clear();
    m_visited.clear();
    m_indexPos = 0;
    m_metaLastId = 0;
    m_done = 0;
    m_total = 0;
    m_fileCount = 0;

    // 旧版只在「首次扫描」（库还是空的）时自动导入产品标签
    m_firstScan = false;
    QSqlQuery q(db->handle());
    if (q.exec(QStringLiteral("SELECT COUNT(*) FROM videos")) && q.next())
        m_firstScan = (q.value(0).toInt() == 0);

    m_phase = Phase::Walking;
    emit stateChanged();
    scheduleNext();
}

void Scanner::cancel()
{
    if (!scanning())
        return;
    m_timer.stop();
    m_phase = Phase::Idle;
    m_pendingDirs.clear();
    m_files.clear();
    m_phaseText.clear();
    m_message = QStringLiteral("已取消");
    emit stateChanged();
}

void Scanner::scheduleNext()
{
    m_timer.start();
}

void Scanner::step()
{
    switch (m_phase) {
    case Phase::Walking:
        walkStep();
        break;
    case Phase::Indexing:
        indexStep();
        break;
    case Phase::Metadata:
        metadataStep();
        break;
    case Phase::AutoTags:
        autoProductTags();
        break;
    default:
        break;
    }
}

void Scanner::walkStep()
{
    // 每批最多展开 80 个目录，之后就交还事件循环
    int budget = 80;
    while (!m_pendingDirs.isEmpty() && budget-- > 0) {
        const QString dir = m_pendingDirs.takeLast();

        // 去重只做「纯字符串规范化」，故意不用 canonicalFilePath()：
        // 后者为了解析符号链接/真实大小写，会对路径的每一级都做一次网络查询，
        // 几百个目录就是几十秒的额外卡顿，而且这里的重复只可能来自 junction，
        // cleanPath 足够挡住绕圈。
        QString key = QDir::cleanPath(dir).toLower();
        if (key.isEmpty())
            key = dir.toLower();
        if (m_visited.contains(key))
            continue;
        m_visited.insert(key);

        QDir d(dir);
        const QFileInfoList entries =
            d.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
        for (const QFileInfo &fi : entries) {
            if (fi.isDir()) {
                if (!isExcludedDir(fi.fileName()))
                    m_pendingDirs.append(fi.absoluteFilePath());
            } else if (fi.isFile() && MediaMeta::isVideoFile(fi.fileName())) {
                m_files.append(fi.absoluteFilePath());
            }
        }
    }

    m_total = int(m_files.size());
    m_done = 0;
    m_phaseText = QStringLiteral("扫描目录");
    emit stateChanged();

    if (!m_pendingDirs.isEmpty()) {
        scheduleNext();
        return;
    }

    m_phase = Phase::Indexing;
    m_phaseText = QStringLiteral("写入索引");
    emit stateChanged();
    scheduleNext();
}

void Scanner::indexStep()
{
    QSqlDatabase &db = Db::instance()->handle();

    if (m_indexPos >= m_files.size()) {
        m_fileCount = int(m_files.size());
        m_phase = Phase::Metadata;
        m_phaseText = QStringLiteral("解析时长");
        m_done = 0;
        m_total = 0;
        emit stateChanged();
        scheduleNext();
        return;
    }

    const int batchEnd = qMin(m_indexPos + 400, int(m_files.size()));
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    db.transaction();
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT INTO videos (path,name,parent,ext,size,mtime,indexed_at,updated_at) "
        "VALUES (?,?,?,?,?,?,?,?) "
        "ON CONFLICT(path) DO UPDATE SET size=excluded.size, mtime=excluded.mtime, "
        "updated_at=excluded.updated_at"));

    for (int i = m_indexPos; i < batchEnd; ++i) {
        const QFileInfo fi(m_files.at(i));
        // 用 bindValue 而不是 addBindValue：同一个 prepared query 反复 exec 时，
        // addBindValue 是「追加」，绑定会一批批堆积起来（参数数量对不上）。
        q.bindValue(0, QDir::toNativeSeparators(fi.absoluteFilePath()));
        q.bindValue(1, fi.fileName());
        q.bindValue(2, QDir::toNativeSeparators(fi.absolutePath()));
        q.bindValue(3, QLatin1Char('.') + fi.suffix().toLower());
        q.bindValue(4, qint64(fi.size()));
        q.bindValue(5, qint64(fi.lastModified().toMSecsSinceEpoch()));
        q.bindValue(6, now);
        q.bindValue(7, now);
        if (!q.exec())
            qWarning("[scan] upsert 失败: %s", qPrintable(q.lastError().text()));
    }
    if (!db.commit())
        qWarning("[scan] commit 失败: %s", qPrintable(db.lastError().text()));

    m_indexPos = batchEnd;
    m_done = m_indexPos;
    m_fileCount = m_indexPos;
    emit stateChanged();
    scheduleNext();
}

void Scanner::metadataStep()
{
    QSqlDatabase &db = Db::instance()->handle();

    // 用 id 游标向前推进：解析失败的文件不会被反复取出来（否则会死循环）
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT id, path FROM videos "
                             "WHERE (duration_ms = 0 OR codec = '') AND id > ? "
                             "ORDER BY id LIMIT 120"));
    q.addBindValue(m_metaLastId);
    if (!q.exec()) {
        m_error = q.lastError().text();
        emit stateChanged();
        finish();
        return;
    }

    QVector<QPair<int, QString>> rows;
    while (q.next())
        rows.append({ q.value(0).toInt(), q.value(1).toString() });

    if (rows.isEmpty()) {
        if (m_firstScan) {
            m_phase = Phase::AutoTags;
            m_phaseText = QStringLiteral("导入产品标签");
            emit stateChanged();
            scheduleNext();
        } else {
            finish();
        }
        return;
    }

    QSqlQuery upd(db);
    upd.prepare(
        QStringLiteral("UPDATE videos SET duration_ms=?, width=?, height=?, codec=? WHERE id=?"));

    for (const auto &row : rows) {
        m_metaLastId = row.first;
        const MediaMeta::Info info = MediaMeta::read(row.second);
        if (info.durationMs <= 0)
            continue; // 解析不出来的（非 MP4/MOV 容器）留 0，靠游标跳过
        // 同样用 bindValue：同一个 prepared query 反复 exec 时 addBindValue 会不断追加绑定
        upd.bindValue(0, info.durationMs);
        upd.bindValue(1, info.width);
        upd.bindValue(2, info.height);
        upd.bindValue(3, info.codec);
        upd.bindValue(4, row.first);
        upd.exec();
    }

    m_done += int(rows.size());
    if (m_done > m_total)
        m_total = m_done;
    emit stateChanged();
    scheduleNext();
}

// 首次扫描：把每个视频所在文件夹的名字登记成「产品」标签，并给尚未打标的视频打上。
// 与 backend/scanner.js 的 autoProductTags() 一致。
void Scanner::autoProductTags()
{
    QSqlDatabase &db = Db::instance()->handle();
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    // 一条 SQL 就拿出「还没有任何标签」的视频及其所在文件夹。
    // 原来分三步查（先取全部文件夹 → 每个文件夹查一次它的视频 → 每个视频再查一次标签数），
    // 几千条素材时是上万次查询 —— 首次扫描会卡在这一步很久。
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral(
            "SELECT v.parent, v.id FROM videos v "
            "WHERE NOT EXISTS (SELECT 1 FROM video_tags vt WHERE vt.video_id = v.id)"))) {
        finish();
        return;
    }

    QMap<QString, QVector<int>> byParent;
    while (q.next())
        byParent[q.value(0).toString()].append(q.value(1).toInt());

    QSqlQuery insTag(db);
    insTag.prepare(QStringLiteral("INSERT OR IGNORE INTO tags (name, category, is_system, "
                                  "created_at) VALUES (?, 'product', 1, ?)"));
    QSqlQuery getTag(db);
    getTag.prepare(QStringLiteral("SELECT id FROM tags WHERE name=? AND category='product'"));
    QSqlQuery assign(db);
    assign.prepare(
        QStringLiteral("INSERT OR IGNORE INTO video_tags (video_id, tag_id) VALUES (?,?)"));

    // 打包进一个事务：几百个标签 / 上千条关联一次提交，比逐条提交快一两个数量级
    db.transaction();
    for (auto it = byParent.constBegin(); it != byParent.constEnd(); ++it) {
        const QString folderName = QFileInfo(it.key()).fileName();
        if (folderName.isEmpty())
            continue;

        insTag.bindValue(0, folderName);
        insTag.bindValue(1, now);
        insTag.exec();

        getTag.bindValue(0, folderName);
        if (!getTag.exec() || !getTag.next())
            continue;
        const int tagId = getTag.value(0).toInt();

        for (int vid : it.value()) {
            assign.bindValue(0, vid);
            assign.bindValue(1, tagId);
            assign.exec();
        }
    }
    if (!db.commit())
        qWarning("[scan] 标签导入提交失败: %s", qPrintable(db.lastError().text()));

    finish();
}

void Scanner::finish()
{
    Db::instance()->bumpVersion();

    m_phase = Phase::Done;
    m_phaseText = QStringLiteral("完成");
    if (m_error.isEmpty())
        m_message = QStringLiteral("索引完成：%1 个视频").arg(m_fileCount);
    emit stateChanged();
    emit finished(m_fileCount);

    // 回到 Idle，允许再次扫描
    m_phase = Phase::Idle;
    m_phaseText.clear();
    emit stateChanged();
}
