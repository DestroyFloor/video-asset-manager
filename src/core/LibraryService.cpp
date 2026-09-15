#include "LibraryService.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QDrag>
#include <QFile>
#include <QFileInfo>
#include <QMimeData>
#include <QProcess>
#include <QRegularExpression>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUrl>
#include <QVariant>

#include <functional>

#include "Config.h"
#include "Db.h"
#include "MediaMeta.h"

namespace {

// 与旧版 db.js / api.js 一致：这些目录不参与扫描，也不允许删除
const QStringList kProtectedNames = {
    QStringLiteral("工作台数据库"), QStringLiteral("thumbnails"),  QStringLiteral("proxies"),
    QStringLiteral("previews"),     QStringLiteral("工作台"),      QStringLiteral(".ds_workbench"),
    QStringLiteral(".clipwork"),
};

const QStringList kVideoSuffixes = {
    QStringLiteral("mp4"),  QStringLiteral("mov"),  QStringLiteral("m4v"), QStringLiteral("avi"),
    QStringLiteral("mkv"),  QStringLiteral("flv"),  QStringLiteral("wmv"), QStringLiteral("ts"),
    QStringLiteral("webm"), QStringLiteral("mpg"),  QStringLiteral("mpeg"),
    QStringLiteral("3gp"),  QStringLiteral("m2ts"), QStringLiteral("mts"),
};

QString normalizePath(const QString &p)
{
    QString s = QDir::toNativeSeparators(p);
    while (s.endsWith(QLatin1Char('/')) || s.endsWith(QLatin1Char('\\')))
        s.chop(1);
    return s;
}

QString sanitizeName(const QString &s)
{
    static const QRegularExpression bad(QStringLiteral("[<>:\"/\\\\|?*\\x00-\\x1f]"));
    QString out = s;
    out.remove(bad);
    out = out.trimmed();
    if (out.size() > 120)
        out = out.left(120);
    return out.isEmpty() ? QStringLiteral("未命名") : out;
}

bool isProtected(const QString &path, const QString &root)
{
    if (path.isEmpty())
        return true;
    const QString p = normalizePath(path).toLower();
    const QString r = normalizePath(root).toLower();
    if (p == r)
        return true;
    if (!p.startsWith(r + QLatin1Char('\\')))
        return true; // 越出工作区，一律拒绝
    const QString name = p.section(QLatin1Char('\\'), -1);
    return kProtectedNames.contains(name);
}

QString likeEscaped(const QString &s)
{
    QString out = s;
    out.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    out.replace(QLatin1Char('%'), QStringLiteral("\\%"));
    out.replace(QLatin1Char('_'), QStringLiteral("\\_"));
    return out;
}

// 分类文件夹名里的时间戳：精确到分钟（与旧版 new Date() 格式一致）
QString tsMinuteNow()
{
    return QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmm"));
}

} // namespace

LibraryService::LibraryService(QObject *parent) : QObject(parent) {}

int LibraryService::version() const
{
    return Db::instance()->isOpen() ? Db::instance()->version() : 0;
}

void LibraryService::bumpVersion()
{
    Db::instance()->bumpVersion();
    emit libraryChanged();
}

// ---------------------------------------------------------------- 标签

QVariantMap LibraryService::categories()
{
    QVariantMap out;
    out[QStringLiteral("shot")] = QVariantList();
    out[QStringLiteral("product")] = QVariantList();
    out[QStringLiteral("availability")] = QVariantList();
    if (!Db::instance()->isOpen())
        return out;

    QSqlQuery q(Db::instance()->handle());
    if (!q.exec(QStringLiteral("SELECT t.id, t.name, t.category, "
                               "(SELECT COUNT(*) FROM video_tags vt WHERE vt.tag_id=t.id) AS cnt "
                               "FROM tags t ORDER BY t.id")))
        return out;

    while (q.next()) {
        const QString cat = q.value(2).toString();
        if (!out.contains(cat))
            continue;
        QVariantMap t;
        t[QStringLiteral("id")] = q.value(0).toInt();
        t[QStringLiteral("name")] = q.value(1).toString();
        t[QStringLiteral("category")] = cat;
        t[QStringLiteral("count")] = q.value(3).toInt();
        QVariantList list = out.value(cat).toList();
        list.append(t);
        out[cat] = list;
    }
    return out;
}

QVariantMap LibraryService::createTag(const QString &category, const QString &name)
{
    QVariantMap res;
    res[QStringLiteral("ok")] = false;
    static const QStringList allowed = { QStringLiteral("shot"), QStringLiteral("product"),
                                         QStringLiteral("availability") };
    if (!allowed.contains(category)) {
        res[QStringLiteral("message")] = QStringLiteral("非法的分类");
        return res;
    }
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        res[QStringLiteral("message")] = QStringLiteral("标签名不能为空");
        return res;
    }

    QSqlQuery q(Db::instance()->handle());
    q.prepare(QStringLiteral("INSERT INTO tags (name, category, is_system, created_at) "
                             "VALUES (?,?,0,?)"));
    q.addBindValue(trimmed);
    q.addBindValue(category);
    q.addBindValue(QDateTime::currentMSecsSinceEpoch());
    if (!q.exec()) {
        res[QStringLiteral("message")] = q.lastError().text().contains(QLatin1String("UNIQUE"))
                                             ? QStringLiteral("该标签已存在")
                                             : q.lastError().text();
        return res;
    }
    bumpVersion();
    res[QStringLiteral("ok")] = true;
    res[QStringLiteral("id")] = q.lastInsertId().toInt();
    return res;
}

bool LibraryService::renameTag(int tagId, const QString &name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty())
        return false;
    QSqlQuery q(Db::instance()->handle());
    q.prepare(QStringLiteral("UPDATE tags SET name=? WHERE id=?"));
    q.addBindValue(trimmed);
    q.addBindValue(tagId);
    if (!q.exec() || q.numRowsAffected() == 0)
        return false;
    bumpVersion();
    return true;
}

bool LibraryService::deleteTag(int tagId)
{
    QSqlDatabase &db = Db::instance()->handle();
    db.transaction();
    QSqlQuery q(db);
    q.prepare(QStringLiteral("DELETE FROM video_tags WHERE tag_id=?"));
    q.addBindValue(tagId);
    q.exec();
    q.prepare(QStringLiteral("DELETE FROM tags WHERE id=?"));
    q.addBindValue(tagId);
    q.exec();
    db.commit();
    bumpVersion();
    return true;
}

bool LibraryService::assignTags(const QVariantList &videoIds, const QVariantList &tagIds,
                                const QString &action)
{
    QSqlDatabase &db = Db::instance()->handle();
    const bool remove = (action == QLatin1String("remove"));
    if (videoIds.isEmpty() || tagIds.isEmpty())
        return false;

    db.transaction();
    QSqlQuery q(db);
    q.prepare(remove ? QStringLiteral("DELETE FROM video_tags WHERE video_id=? AND tag_id=?")
                     : QStringLiteral("INSERT OR IGNORE INTO video_tags (video_id, tag_id) "
                                      "VALUES (?,?)"));
    for (const QVariant &v : videoIds) {
        for (const QVariant &t : tagIds) {
            q.addBindValue(v.toInt());
            q.addBindValue(t.toInt());
            q.exec();
        }
    }
    db.commit();
    bumpVersion();
    return true;
}

bool LibraryService::adjustUsage(const QVariantList &videoIds, int delta)
{
    if (videoIds.isEmpty())
        return false;
    QStringList ph;
    for (int i = 0; i < videoIds.size(); ++i)
        ph << QStringLiteral("?");

    QSqlQuery q(Db::instance()->handle());
    q.prepare(QStringLiteral("UPDATE videos SET usage_count = MAX(0, usage_count + ?) "
                             "WHERE id IN (%1)")
                  .arg(ph.join(QLatin1Char(','))));
    q.addBindValue(delta);
    for (const QVariant &v : videoIds)
        q.addBindValue(v.toInt());
    if (!q.exec())
        return false;
    bumpVersion();
    return true;
}

// ---------------------------------------------------------------- 查询

QSet<int> LibraryService::idsForAny(const QList<int> &tagIds) const
{
    QSet<int> out;
    if (tagIds.isEmpty())
        return out;
    QStringList ph;
    for (int i = 0; i < tagIds.size(); ++i)
        ph << QStringLiteral("?");

    QSqlQuery q(Db::instance()->handle());
    q.prepare(QStringLiteral("SELECT DISTINCT video_id FROM video_tags WHERE tag_id IN (%1)")
                  .arg(ph.join(QLatin1Char(','))));
    for (int id : tagIds)
        q.addBindValue(id);
    if (q.exec()) {
        while (q.next())
            out.insert(q.value(0).toInt());
    }
    return out;
}

QSet<int> LibraryService::idsForAll(const QList<int> &tagIds) const
{
    QSet<int> out;
    if (tagIds.isEmpty())
        return out;
    QStringList ph;
    for (int i = 0; i < tagIds.size(); ++i)
        ph << QStringLiteral("?");

    QSqlQuery q(Db::instance()->handle());
    q.prepare(QStringLiteral("SELECT video_id FROM video_tags WHERE tag_id IN (%1) "
                             "GROUP BY video_id HAVING COUNT(DISTINCT tag_id) = ?")
                  .arg(ph.join(QLatin1Char(','))));
    for (int id : tagIds)
        q.addBindValue(id);
    q.addBindValue(tagIds.size());
    if (q.exec()) {
        while (q.next())
            out.insert(q.value(0).toInt());
    }
    return out;
}

QSet<int> LibraryService::searchTagIds(const QString &term) const
{
    QSet<int> out;
    QSqlQuery q(Db::instance()->handle());
    q.prepare(QStringLiteral("SELECT DISTINCT vt.video_id FROM video_tags vt "
                             "JOIN tags t ON t.id = vt.tag_id WHERE t.name LIKE ?"));
    q.addBindValue(QLatin1Char('%') + term + QLatin1Char('%'));
    if (q.exec()) {
        while (q.next())
            out.insert(q.value(0).toInt());
    }
    return out;
}

QVariantMap LibraryService::buildVideoItem(int id, const QMap<int, int> &tagCounts) const
{
    QVariantMap item;
    QSqlQuery q(Db::instance()->handle());
    q.prepare(QStringLiteral("SELECT id,path,name,parent,ext,size,duration_ms,width,height,"
                             "usage_count,codec,updated_at FROM videos WHERE id=?"));
    q.addBindValue(id);
    if (!q.exec() || !q.next())
        return item;

    item[QStringLiteral("id")] = q.value(0).toInt();
    item[QStringLiteral("path")] = q.value(1).toString();
    item[QStringLiteral("name")] = q.value(2).toString();
    item[QStringLiteral("parent")] = q.value(3).toString();
    item[QStringLiteral("ext")] = q.value(4).toString();
    item[QStringLiteral("size")] = q.value(5).toLongLong();
    item[QStringLiteral("duration_ms")] = q.value(6).toLongLong();
    item[QStringLiteral("width")] = q.value(7).toInt();
    item[QStringLiteral("height")] = q.value(8).toInt();
    item[QStringLiteral("usage_count")] = q.value(9).toInt();
    item[QStringLiteral("codec")] = q.value(10).toString();
    item[QStringLiteral("updated_at")] = q.value(11).toLongLong();
    item[QStringLiteral("tag_count")] = tagCounts.value(id, 0);

    QVariantList tags;
    QSqlQuery tq(Db::instance()->handle());
    tq.prepare(QStringLiteral("SELECT t.id,t.name,t.category FROM tags t "
                              "JOIN video_tags vt ON vt.tag_id=t.id WHERE vt.video_id=? "
                              "ORDER BY t.category, t.name"));
    tq.addBindValue(id);
    if (tq.exec()) {
        while (tq.next()) {
            QVariantMap t;
            t[QStringLiteral("id")] = tq.value(0).toInt();
            t[QStringLiteral("name")] = tq.value(1).toString();
            t[QStringLiteral("category")] = tq.value(2).toString();
            tags.append(t);
        }
    }
    item[QStringLiteral("tags")] = tags;
    return item;
}

// 一次把一整批视频的完整信息取回来（含标签），不再按 id 逐条查。
// 之前是「每条素材跑 2 条 SQL」：3916 条素材 = 7832 次查询，而库还在网络盘上，
// 每次都是网络往返 —— 界面会直接卡死。
static QVariantList buildVideoItems(const QList<int> &ids)
{
    QVariantList items;
    if (ids.isEmpty())
        return items;

    QMap<int, QVariantMap> byId;
    QMap<int, QVariantList> tagsById;

    for (int i = 0; i < ids.size(); i += 400) {
        const QList<int> chunk = ids.mid(i, 400);
        QStringList ph;
        for (int k = 0; k < chunk.size(); ++k)
            ph << QStringLiteral("?");
        const QString inList = ph.join(QLatin1Char(','));

        QSqlQuery q(Db::instance()->handle());
        q.prepare(QStringLiteral("SELECT id,path,name,parent,ext,size,duration_ms,width,height,"
                                 "usage_count,codec,updated_at FROM videos WHERE id IN (%1)")
                      .arg(inList));
        for (int id : chunk)
            q.addBindValue(id);
        if (q.exec()) {
            while (q.next()) {
                QVariantMap item;
                item[QStringLiteral("id")] = q.value(0).toInt();
                item[QStringLiteral("path")] = q.value(1).toString();
                item[QStringLiteral("name")] = q.value(2).toString();
                item[QStringLiteral("parent")] = q.value(3).toString();
                item[QStringLiteral("ext")] = q.value(4).toString();
                item[QStringLiteral("size")] = q.value(5).toLongLong();
                item[QStringLiteral("duration_ms")] = q.value(6).toLongLong();
                item[QStringLiteral("width")] = q.value(7).toInt();
                item[QStringLiteral("height")] = q.value(8).toInt();
                item[QStringLiteral("usage_count")] = q.value(9).toInt();
                item[QStringLiteral("codec")] = q.value(10).toString();
                item[QStringLiteral("updated_at")] = q.value(11).toLongLong();
                byId.insert(item[QStringLiteral("id")].toInt(), item);
            }
        }

        QSqlQuery tq(Db::instance()->handle());
        tq.prepare(QStringLiteral("SELECT vt.video_id, t.id, t.name, t.category "
                                  "FROM video_tags vt JOIN tags t ON t.id = vt.tag_id "
                                  "WHERE vt.video_id IN (%1) ORDER BY t.category, t.name")
                       .arg(inList));
        for (int id : chunk)
            tq.addBindValue(id);
        if (tq.exec()) {
            while (tq.next()) {
                QVariantMap t;
                t[QStringLiteral("id")] = tq.value(1).toInt();
                t[QStringLiteral("name")] = tq.value(2).toString();
                t[QStringLiteral("category")] = tq.value(3).toString();
                tagsById[tq.value(0).toInt()].append(t);
            }
        }
    }

    // 按传入顺序拼装（传入顺序就是 SQL 的排序结果）
    for (int id : ids) {
        if (!byId.contains(id))
            continue;
        QVariantMap item = byId.value(id);
        const QVariantList tags = tagsById.value(id);
        item[QStringLiteral("tags")] = tags;
        item[QStringLiteral("tag_count")] = tags.size();
        items.append(item);
    }
    return items;
}

QVariantMap LibraryService::query(const QVariantMap &filter)
{
    QVariantMap out;
    QVariantList items;
    out[QStringLiteral("total")] = 0;
    out[QStringLiteral("items")] = items;
    if (!Db::instance()->isOpen())
        return out;

    auto toIds = [](const QVariant &v) {
        QList<int> ids;
        const QVariantList list = v.toList();
        for (const QVariant &x : list) {
            const int id = x.toInt();
            if (id > 0)
                ids.append(id);
        }
        return ids;
    };

    const QList<int> shot = toIds(filter.value(QStringLiteral("shot")));
    const QList<int> product = toIds(filter.value(QStringLiteral("product")));
    const QList<int> avail = toIds(filter.value(QStringLiteral("avail")));
    const bool useOr = filter.value(QStringLiteral("andOr")).toString() == QLatin1String("or");

    // 标签条件先用集合算好（与旧版一致的语义：任一 / 全部 / 可用性恒为「且」）
    QSet<int> filtered;
    bool hasFiltered = false;
    if (!shot.isEmpty() && !product.isEmpty()) {
        const QSet<int> a = idsForAny(shot);
        const QSet<int> b = idsForAny(product);
        if (useOr) {
            filtered = a;
            filtered.unite(b);
        } else {
            // 「且」时镜头与产品都要满足：分别对两个分组各自取「全部」
            filtered = idsForAll(shot);
            filtered.intersect(idsForAll(product));
        }
        hasFiltered = true;
    } else if (!shot.isEmpty()) {
        filtered = idsForAll(shot);
        hasFiltered = true;
    } else if (!product.isEmpty()) {
        filtered = idsForAll(product);
        hasFiltered = true;
    }
    if (!avail.isEmpty()) {
        const QSet<int> a = idsForAll(avail);
        filtered = hasFiltered ? QSet<int>(filtered & a) : a;
        hasFiltered = true;
    }

    const QString search = filter.value(QStringLiteral("search")).toString().trimmed();
    if (!search.isEmpty()) {
        const QSet<int> s = searchTagIds(search);
        filtered = hasFiltered ? QSet<int>(filtered & s) : s;
        hasFiltered = true;
    }

    // 文件夹前缀 / 只看跑量 用 SQL 条件叠加
    QStringList extra;
    QVariantList extraBinds;
    const QString folder = filter.value(QStringLiteral("folder")).toString().trimmed();
    if (!folder.isEmpty()) {
        extra << QStringLiteral("parent LIKE ? ESCAPE '\\'");
        extraBinds << likeEscaped(normalizePath(folder)) + QStringLiteral("%");
    }
    if (filter.value(QStringLiteral("onlyHot")).toBool())
        extra << QStringLiteral("usage_count > 0");

    // 视图筛选：最小调用次数 + 仅显示有标签的素材
    const int minUsage = filter.value(QStringLiteral("minUsage")).toInt();
    if (minUsage > 0) {
        extra << QStringLiteral("usage_count >= ?");
        extraBinds << minUsage;
    }
    if (filter.value(QStringLiteral("onlyTagged")).toBool())
        extra << QStringLiteral("(SELECT COUNT(*) FROM video_tags vt WHERE vt.video_id = videos.id) > 0");

    QString sql = QStringLiteral("SELECT id FROM videos");
    QStringList where = extra;
    if (hasFiltered) {
        const QList<int> ids = QSet<int>(filtered).values();
        if (ids.isEmpty()) {
            out[QStringLiteral("total")] = 0;
            out[QStringLiteral("hasMore")] = false;
            return out;
        }
        QStringList ph;
        for (int i = 0; i < ids.size(); ++i)
            ph << QStringLiteral("?");
        where << QStringLiteral("id IN (%1)").arg(ph.join(QLatin1Char(',')));
        for (int id : ids)
            extraBinds << id;
    }
    QString whereSql;
    if (!where.isEmpty())
        whereSql = QStringLiteral(" WHERE ") + where.join(QStringLiteral(" AND "));

    // 排序（与旧版一致的四种字段）
    const QString sortBy = filter.value(QStringLiteral("sortBy")).toString();
    const bool asc = filter.value(QStringLiteral("sortDir")).toString() == QLatin1String("asc");
    const QString dir = asc ? QStringLiteral("ASC") : QStringLiteral("DESC");
    QString orderSql;
    if (sortBy == QLatin1String("usage"))
        orderSql = QStringLiteral(" ORDER BY usage_count ") + dir + QStringLiteral(", updated_at DESC, id DESC");
    else if (sortBy == QLatin1String("duration"))
        orderSql = QStringLiteral(" ORDER BY duration_ms ") + dir + QStringLiteral(", updated_at DESC, id DESC");
    else if (sortBy == QLatin1String("date"))
        // 「上传时间」按文件自身的修改时间排：updated_at 是「入库时间」，同一次扫描进来的
        // 文件几乎完全一样，按它排等于没排。库里 mtime 字段在扫描时写入的是文件 lastModified。
        orderSql = QStringLiteral(" ORDER BY mtime ") + dir + QStringLiteral(", id DESC");
    else if (sortBy == QLatin1String("tags"))
        orderSql = QStringLiteral(" ORDER BY (SELECT COUNT(*) FROM video_tags vt WHERE vt.video_id = videos.id) ")
                   + dir + QStringLiteral(", updated_at DESC, id DESC");
    else
        orderSql = QStringLiteral(" ORDER BY updated_at DESC, id DESC");

    // 分页：默认一页 300 条。几千条素材一次性构造再交给 QML，界面一样会卡；
    // 改成滚到接近底部时再取下一页（offset / limit 由界面传进来）。
    int pageSize = filter.value(QStringLiteral("limit")).toInt();
    if (pageSize <= 0 || pageSize > 1000)
        pageSize = 300;
    int offset = filter.value(QStringLiteral("offset")).toInt();
    if (offset < 0)
        offset = 0;

    auto bindExtra = [&extraBinds](QSqlQuery &query) {
        for (const QVariant &b : extraBinds)
            query.addBindValue(b);
    };

    // ① 先取总数：界面要显示「共 N」，也靠它判断还有没有下一页
    int total = 0;
    {
        QSqlQuery cq(Db::instance()->handle());
        cq.prepare(QStringLiteral("SELECT COUNT(*) FROM videos") + whereSql);
        bindExtra(cq);
        if (!cq.exec() || !cq.next()) {
            const QString err = cq.lastError().text();
            qWarning("[library] query 失败: %s", qPrintable(err));
            // 共享盘上的 SQLite 偶尔会返回 "database disk image is malformed"（正好读到别台
            // 机器写入中的页），这种失败是瞬时的：标出来让界面隔一会儿自动重试。
            out[QStringLiteral("readError")] = true;
            out[QStringLiteral("error")] = err;
            return out;
        }
        total = cq.value(0).toInt();
    }

    // ② 再取这一页的 id
    QSqlQuery q(Db::instance()->handle());
    q.prepare(QStringLiteral("SELECT id FROM videos") + whereSql + orderSql
              + QStringLiteral(" LIMIT ? OFFSET ?"));
    bindExtra(q);
    q.addBindValue(pageSize);
    q.addBindValue(offset);
    if (!q.exec()) {
        const QString err = q.lastError().text();
        qWarning("[library] query 失败: %s", qPrintable(err));
        out[QStringLiteral("readError")] = true;
        out[QStringLiteral("error")] = err;
        return out;
    }

    QList<int> pageIds;
    while (q.next())
        pageIds.append(q.value(0).toInt());

    // ③ 一次性把这一页的完整信息（含标签）取回来 —— 两条 SQL 搞定，不再逐条查
    const QVariantList page = buildVideoItems(pageIds);

    out[QStringLiteral("total")] = total;
    out[QStringLiteral("items")] = page;
    out[QStringLiteral("offset")] = offset;
    out[QStringLiteral("hasMore")] = (offset + page.size()) < total;
    return out;
}

QVariantMap LibraryService::videoDetail(int videoId)
{
    QVariantMap out;
    if (!Db::instance()->isOpen())
        return out;

    QMap<int, int> counts;
    QVariantMap video = buildVideoItem(videoId, counts);
    if (video.isEmpty())
        return out;
    out[QStringLiteral("video")] = video;

    QVariantList tags = video.value(QStringLiteral("tags")).toList();
    out[QStringLiteral("tags")] = tags;

    // 相关素材：与当前视频共享标签最多的 12 个（与旧版一致）
    QVariantList similar;
    QSqlQuery q(Db::instance()->handle());
    q.prepare(QStringLiteral(
        "SELECT DISTINCT v2.id, COUNT(DISTINCT vt2.tag_id) AS shared "
        "FROM video_tags vt1 "
        "JOIN video_tags vt2 ON vt2.tag_id = vt1.tag_id AND vt2.video_id != vt1.video_id "
        "JOIN videos v2 ON v2.id = vt2.video_id "
        "WHERE vt1.video_id = ? GROUP BY v2.id ORDER BY shared DESC, v2.updated_at DESC LIMIT 12"));
    q.addBindValue(videoId);
    if (q.exec()) {
        while (q.next()) {
            QMap<int, int> c;
            QVariantMap item = buildVideoItem(q.value(0).toInt(), c);
            if (!item.isEmpty()) {
                item[QStringLiteral("shared")] = q.value(1).toInt();
                similar.append(item);
            }
        }
    }
    out[QStringLiteral("similar")] = similar;
    return out;
}

// ---------------------------------------------------------------- 文件夹树

QVariantList LibraryService::folders()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const int version = Db::instance()->version();

    // 缓存：遍历整个工作区（几百个目录、几千个文件）在网络盘上要好几秒，
    // 而界面每次刷新都会调它（扫描完成、删除、打标……），不缓存的话点一次
    // 「重新扫描」就像卡死一样。库版本没变且不超过 60 秒就直接复用。
    if (!m_folderCache.isEmpty() && m_folderCacheVersion == version
        && now - m_folderCacheAt < 60000)
        return m_folderCache;

    QVariantList out;
    const QString root = normalizePath(Db::instance()->workspaceRoot());
    if (root.isEmpty())
        return out;

    // 一次递归、自底向上把视频数汇总给父目录。
    // 原来的写法是「每遇到一个目录，就把它的整棵子树再数一遍」，同一批目录被反复遍历，
    // 复杂度接近 O(n²) —— 几千个文件时慢得离谱，而且每次都是网络往返。
    std::function<int(const QString &, int)> walk = [&](const QString &dir, int depth) -> int {
        int total = 0;
        const QFileInfoList entries =
            QDir(dir).entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
        for (const QFileInfo &fi : entries) {
            if (fi.isDir()) {
                if (kProtectedNames.contains(fi.fileName())
                    || fi.fileName().startsWith(QLatin1Char('.')))
                    continue;
                total += walk(normalizePath(fi.absoluteFilePath()), depth + 1);
            } else if (kVideoSuffixes.contains(fi.suffix().toLower())) {
                ++total;
            }
        }

        // 只有第 1、2 层目录作为节点展示；更深层仍然把数量往上传，但不单独列出
        if (total > 0 && depth >= 1 && depth <= 2) {
            QVariantMap m;
            m[QStringLiteral("path")] = dir;
            m[QStringLiteral("name")] = QFileInfo(dir).fileName();
            m[QStringLiteral("dir")] = normalizePath(QFileInfo(dir).absolutePath());
            m[QStringLiteral("count")] = total;
            out.append(m);
        }
        return total;
    };
    walk(root, 0);

    m_folderCache = out;
    m_folderCacheAt = now;
    m_folderCacheVersion = version;
    return out;
}

QVariantMap LibraryService::deleteFolders(const QStringList &paths)
{
    QVariantMap res;
    QVariantList deleted;
    QVariantList errors;
    const QString root = Db::instance()->workspaceRoot();

    for (const QString &p : paths) {
        const QString np = normalizePath(p);
        if (isProtected(np, root)) {
            QVariantMap e;
            e[QStringLiteral("path")] = np;
            e[QStringLiteral("error")] = QStringLiteral("受保护目录，不允许删除");
            errors.append(e);
            continue;
        }
        QSqlDatabase &db = Db::instance()->handle();
        const QString esc = likeEscaped(np);
        QSqlQuery q(db);
        q.prepare(QStringLiteral("SELECT id FROM videos WHERE parent LIKE ? ESCAPE '\\'"));
        q.addBindValue(esc + QStringLiteral("%"));
        QList<int> ids;
        if (q.exec()) {
            while (q.next())
                ids.append(q.value(0).toInt());
        }
        if (!ids.isEmpty()) {
            QStringList ph;
            for (int i = 0; i < ids.size(); ++i)
                ph << QStringLiteral("?");
            QSqlQuery d(db);
            d.prepare(QStringLiteral("DELETE FROM video_tags WHERE video_id IN (%1)")
                          .arg(ph.join(QLatin1Char(','))));
            for (int id : ids)
                d.addBindValue(id);
            d.exec();
            QSqlQuery d2(db);
            d2.prepare(QStringLiteral("DELETE FROM videos WHERE id IN (%1)")
                           .arg(ph.join(QLatin1Char(','))));
            for (int id : ids)
                d2.addBindValue(id);
            d2.exec();
        }
        if (QDir(np).exists() && !QDir(np).removeRecursively()) {
            QVariantMap e;
            e[QStringLiteral("path")] = np;
            e[QStringLiteral("error")] = QStringLiteral("删除失败（可能被占用）");
            errors.append(e);
            continue;
        }
        deleted.append(np);
    }

    bumpVersion();
    res[QStringLiteral("ok")] = !deleted.isEmpty();
    res[QStringLiteral("deleted")] = deleted;
    res[QStringLiteral("errors")] = errors;
    return res;
}

// 删除素材：文件移进系统回收站（可以在回收站找回），同步清掉数据库记录与封面/预览缓存。
// 顺序上「先动文件、再动库」：移入回收站失败就完全不碰数据库，
// 避免出现「库里查不到、盘上文件还在」的对不上状态。
QVariantMap LibraryService::deleteVideos(const QVariantList &videoIds)
{
    QVariantMap res;
    QVariantList removed;
    QVariantList errors;

    if (!Db::instance()->isOpen()) {
        QVariantMap e;
        e[QStringLiteral("name")] = QString();
        e[QStringLiteral("error")] = QStringLiteral("数据库未打开");
        errors.append(e);
        res[QStringLiteral("ok")] = false;
        res[QStringLiteral("removed")] = removed;
        res[QStringLiteral("errors")] = errors;
        return res;
    }

    QSqlDatabase &db = Db::instance()->handle();
    const QString root = Db::instance()->workspaceRoot();

    // 统计「回收站放不进去、只能直接删掉」的项（网络位置就是这种情况），
    // 结果带回去让界面提示用户这些是不可恢复的。
    int permanentCount = 0;

    for (const QVariant &v : videoIds) {
        const int id = v.toInt();
        if (id <= 0)
            continue;

        QString path;
        QString name;
        {
            QSqlQuery q(db);
            q.prepare(QStringLiteral("SELECT path, name FROM videos WHERE id = ?"));
            q.addBindValue(id);
            if (!q.exec() || !q.next()) {
                QVariantMap e;
                e[QStringLiteral("name")] = QStringLiteral("#%1").arg(id);
                e[QStringLiteral("error")] = QStringLiteral("记录不存在");
                errors.append(e);
                continue;
            }
            path = q.value(0).toString();
            name = q.value(1).toString();
        }

        if (isProtected(path, root)) {
            QVariantMap e;
            e[QStringLiteral("name")] = name;
            e[QStringLiteral("error")] = QStringLiteral("受保护目录，不允许删除");
            errors.append(e);
            continue;
        }

        if (QFileInfo::exists(path)) {
            // 网络位置（\\server\share\… 或 //server/share/…）在 Windows 上根本没有回收站，
            // moveToTrash 必然失败。素材正好都在共享盘上，所以这里必须能退回直接删除，
            // 否则删除永远只会弹「移入回收站失败」。
            const bool networked = path.startsWith(QLatin1String("\\\\"))
                                   || path.startsWith(QLatin1String("//"));
            if (!QFile::moveToTrash(path)) {
                if (!networked || !QFile::remove(path)) {
                    QVariantMap e;
                    e[QStringLiteral("name")] = name;
                    e[QStringLiteral("error")] = networked
                        ? QStringLiteral("删除失败（文件可能被占用）")
                        : QStringLiteral("移入回收站失败（文件可能被占用）");
                    errors.append(e);
                    continue;
                }
                ++permanentCount;   // 走的是直接删除，不可恢复
            }
        }

        QSqlQuery d(db);
        d.prepare(QStringLiteral("DELETE FROM video_tags WHERE video_id = ?"));
        d.addBindValue(id);
        d.exec();

        QSqlQuery d2(db);
        d2.prepare(QStringLiteral("DELETE FROM videos WHERE id = ?"));
        d2.addBindValue(id);
        d2.exec();

        // 封面与预览片段一并清掉，免得留下孤儿文件
        const QString thumb = thumbPath(id);
        if (!thumb.isEmpty())
            QFile::remove(thumb);
        const QString preview = previewPathOf(id);
        if (!preview.isEmpty())
            QFile::remove(preview);

        removed.append(id);
    }

    if (!removed.isEmpty())
        bumpVersion();

    res[QStringLiteral("ok")] = !removed.isEmpty();
    res[QStringLiteral("removed")] = removed;
    res[QStringLiteral("errors")] = errors;
    // >0 表示其中有文件是「直接删掉」的（网络位置没有回收站，不可还原）
    res[QStringLiteral("permanent")] = permanentCount;
    return res;
}

QVariantMap LibraryService::copyFolders(const QStringList &sources, const QString &dest)
{
    QVariantMap res;
    QVariantList copied;
    QVariantList errors;
    QVariantMap err;

    const QString root = Db::instance()->workspaceRoot();
    const QString target = normalizePath(dest);
    if (target.isEmpty()) {
        err[QStringLiteral("error")] = QStringLiteral("未指定目标文件夹");
        errors.append(err);
        res[QStringLiteral("ok")] = false;
        res[QStringLiteral("copied")] = copied;
        res[QStringLiteral("errors")] = errors;
        return res;
    }
    if (isProtected(target, root)) {
        err[QStringLiteral("error")] = QStringLiteral("目标位置受保护");
        errors.append(err);
        res[QStringLiteral("ok")] = false;
        res[QStringLiteral("copied")] = copied;
        res[QStringLiteral("errors")] = errors;
        return res;
    }
    QDir().mkpath(target);

    std::function<bool(const QString &, const QString &)> copyTree = [&](const QString &src,
                                                                         const QString &dst) -> bool {
        if (!QDir().mkpath(dst))
            return false;
        const QFileInfoList entries =
            QDir(src).entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot);
        for (const QFileInfo &fi : entries) {
            if (kProtectedNames.contains(fi.fileName()) || fi.fileName().startsWith(QLatin1Char('.')))
                continue;
            const QString to = dst + QLatin1Char('/') + fi.fileName();
            if (fi.isDir()) {
                if (!copyTree(fi.absoluteFilePath(), to))
                    return false;
            } else if (!QFile::copy(fi.absoluteFilePath(), to)) {
                return false;
            }
        }
        return true;
    };

    for (const QString &s : sources) {
        const QString src = normalizePath(s);
        QVariantMap e;
        if (isProtected(src, root)) {
            e[QStringLiteral("path")] = src;
            e[QStringLiteral("error")] = QStringLiteral("受保护目录，不允许复制");
            errors.append(e);
            continue;
        }
        if (!QDir(src).exists()) {
            e[QStringLiteral("path")] = src;
            e[QStringLiteral("error")] = QStringLiteral("源目录不存在");
            errors.append(e);
            continue;
        }
        // 禁止复制到自身或其子孙（会无限嵌套）
        if (target == src || target.startsWith(src + QLatin1Char('/'))) {
            e[QStringLiteral("path")] = src;
            e[QStringLiteral("error")] = QStringLiteral("不能复制到自身或其子目录");
            errors.append(e);
            continue;
        }

        QString dst = target + QLatin1Char('/') + QFileInfo(src).fileName();
        if (QDir(dst).exists()) {
            const QString base = QFileInfo(src).fileName();
            for (int k = 2; QDir(dst).exists(); ++k)
                dst = target + QLatin1Char('/') + base + QStringLiteral(" (") + QString::number(k)
                      + QLatin1Char(')');
        }
        if (copyTree(src, dst)) {
            QVariantMap c;
            c[QStringLiteral("src")] = src;
            c[QStringLiteral("dest")] = dst;
            copied.append(c);
        } else {
            e[QStringLiteral("path")] = src;
            e[QStringLiteral("error")] = QStringLiteral("复制失败");
            errors.append(e);
        }
    }

    if (!copied.isEmpty())
        bumpVersion();

    res[QStringLiteral("ok")] = !copied.isEmpty();
    res[QStringLiteral("copied")] = copied;
    res[QStringLiteral("errors")] = errors;
    return res;
}

// ---------------------------------------------------------------- 新建分类文件夹

QVariantMap LibraryService::createCategoryFolder(const QVariantList &videoIds, const QString &account)
{
    QVariantMap res;
    QVariantList files;
    res[QStringLiteral("ok")] = false;
    res[QStringLiteral("files")] = files;

    if (videoIds.isEmpty()) {
        res[QStringLiteral("message")] = QStringLiteral("未选择视频");
        return res;
    }

    QSqlDatabase &db = Db::instance()->handle();
    QStringList ph;
    for (int i = 0; i < videoIds.size(); ++i)
        ph << QStringLiteral("?");

    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT id,path,name FROM videos WHERE id IN (%1)")
                  .arg(ph.join(QLatin1Char(','))));
    for (const QVariant &v : videoIds)
        q.addBindValue(v.toInt());
    if (!q.exec()) {
        res[QStringLiteral("message")] = q.lastError().text();
        return res;
    }
    struct Row { int id; QString path; QString name; };
    QVector<Row> rows;
    while (q.next())
        rows.append({ q.value(0).toInt(), q.value(1).toString(), q.value(2).toString() });
    if (rows.isEmpty()) {
        res[QStringLiteral("message")] = QStringLiteral("未找到选中视频");
        return res;
    }

    // 文件夹名：时间到分钟 _ 账号 _ 前 6 个标签（与旧版一致）
    QStringList tagNames;
    QSet<QString> seen;
    QSqlQuery tq(db);
    for (const Row &r : rows) {
        tq.prepare(QStringLiteral("SELECT t.name FROM tags t JOIN video_tags vt ON vt.tag_id=t.id "
                                  "WHERE vt.video_id=? ORDER BY t.category, t.name"));
        tq.addBindValue(r.id);
        if (!tq.exec())
            continue;
        while (tq.next()) {
            const QString n = tq.value(0).toString();
            if (!seen.contains(n)) {
                seen.insert(n);
                tagNames.append(sanitizeName(n));
            }
        }
    }

    QString folderName = tsMinuteNow() + QLatin1Char('_')
                         + sanitizeName(account.isEmpty() ? QStringLiteral("未登录") : account);
    for (int i = 0; i < tagNames.size() && i < 6; ++i)
        folderName += QLatin1Char('_') + tagNames.at(i);

    const QString root = Db::instance()->workspaceRoot();
    const QString base = normalizePath(root) + QStringLiteral("/已分类视频");
    const QString target = base + QLatin1Char('/') + folderName;
    QDir().mkpath(target);

    // 只做复制，绝不动原视频
    QVariantList indexed;
    for (const Row &r : rows) {
        QString dst = target + QLatin1Char('/') + r.name;
        if (QFile::exists(dst)) {
            const QFileInfo fi(r.name);
            const QString baseName = fi.completeBaseName();
            const QString ext = fi.suffix();
            for (int k = 2; QFile::exists(dst); ++k)
                dst = target + QLatin1Char('/') + baseName + QStringLiteral(" (") + QString::number(k)
                      + QLatin1Char(')') + (ext.isEmpty() ? QString() : QLatin1Char('.') + ext);
        }
        if (!QFile::copy(r.path, dst))
            continue;

        // 复制完成立即增量入库，并把原视频的标签继承过来（旧版同样处理）
        const QFileInfo di(dst);
        const QDateTime now = QDateTime::currentDateTime();
        QSqlQuery ins(db);
        ins.prepare(QStringLiteral(
            "INSERT INTO videos (path,name,parent,ext,size,mtime,indexed_at,updated_at) "
            "VALUES (?,?,?,?,?,?,?,?) ON CONFLICT(path) DO UPDATE SET "
            "size=excluded.size, mtime=excluded.mtime, updated_at=excluded.updated_at"));
        ins.addBindValue(normalizePath(dst));
        ins.addBindValue(di.fileName());
        ins.addBindValue(normalizePath(di.absolutePath()));
        ins.addBindValue(QLatin1Char('.') + di.suffix().toLower());
        ins.addBindValue(qint64(di.size()));
        ins.addBindValue(qint64(di.lastModified().toMSecsSinceEpoch()));
        ins.addBindValue(now.toMSecsSinceEpoch());
        ins.addBindValue(now.toMSecsSinceEpoch());
        if (!ins.exec())
            continue;

        const MediaMeta::Info meta = MediaMeta::read(dst);
        QSqlQuery fid(db);
        fid.prepare(QStringLiteral("SELECT id FROM videos WHERE path=?"));
        fid.addBindValue(normalizePath(dst));
        if (!fid.exec() || !fid.next())
            continue;
        const int newId = fid.value(0).toInt();

        if (meta.durationMs > 0) {
            QSqlQuery upd(db);
            upd.prepare(QStringLiteral(
                "UPDATE videos SET duration_ms=?,width=?,height=?,codec=? WHERE id=?"));
            upd.addBindValue(meta.durationMs);
            upd.addBindValue(meta.width);
            upd.addBindValue(meta.height);
            upd.addBindValue(meta.codec);
            upd.addBindValue(newId);
            upd.exec();
        }

        QSqlQuery cp(db);
        cp.prepare(QStringLiteral("INSERT OR IGNORE INTO video_tags (video_id,tag_id) "
                                  "SELECT ?, tag_id FROM video_tags WHERE video_id=?"));
        cp.addBindValue(newId);
        cp.addBindValue(r.id);
        cp.exec();

        QVariantMap f;
        f[QStringLiteral("src")] = r.path;
        f[QStringLiteral("dest")] = dst;
        f[QStringLiteral("id")] = newId;
        indexed.append(f);
    }

    bumpVersion();
    res[QStringLiteral("ok")] = !indexed.isEmpty();
    res[QStringLiteral("folder")] = target;
    res[QStringLiteral("base")] = base;
    res[QStringLiteral("count")] = indexed.size();
    res[QStringLiteral("files")] = indexed;
    return res;
}

// ---------------------------------------------------------------- 打开

void LibraryService::openFolder(const QString &path)
{
    if (path.trimmed().isEmpty())
        return;
    const QString target = QDir::toNativeSeparators(path.trimmed());
    QProcess::startDetached(QStringLiteral("explorer.exe"), { target });
}

void LibraryService::openFile(const QString &path)
{
    if (path.trimmed().isEmpty())
        return;
    const QString target = QDir::toNativeSeparators(path.trimmed());
    QProcess::startDetached(QStringLiteral("cmd.exe"),
                            { QStringLiteral("/c"), QStringLiteral("start"), QString(), target });
}

// ---------------------------------------------------------------- 外部播放器

QString LibraryService::playerExe() const
{
    return Config::instance()->playerExe();
}

void LibraryService::setPlayerExe(const QString &path)
{
    const QString exe = QDir::toNativeSeparators(path.trimmed());
    Config::instance()->set(QJsonObject{ { QStringLiteral("playerExe"), exe } });
    qInfo("[library] 外部播放器设为: %s", qPrintable(exe));
}

// 用配置好的播放器打开视频。没配过、或者配的程序已经不在了 → 返回 false，
// 让界面去问用户选一个（换电脑 / 首次使用时就是这个路径）。
bool LibraryService::openWithPlayer(const QString &path)
{
    const QString file = path.trimmed();
    if (file.isEmpty() || !QFileInfo::exists(file))
        return false;

    const QString exe = Config::instance()->playerExe().trimmed();
    if (exe.isEmpty() || !QFileInfo::exists(exe))
        return false;

    return QProcess::startDetached(exe, { QDir::toNativeSeparators(file) });
}

// ---------------------------------------------------------------- 封面

QString LibraryService::thumbPath(int videoId) const
{
    return Db::instance()->thumbPath(videoId);
}

void LibraryService::ensureThumbDir() const
{
    QDir().mkpath(Db::instance()->thumbDir());
}

QString LibraryService::fileUrl(const QString &path) const
{
    if (path.isEmpty())
        return QString();
    // 关键：UNC 共享盘不能靠字符串拼 "file:///"，必须交给 QUrl::fromLocalFile：
    //   D:\a.jpg             → file:///D:/a.jpg
    //   \\server\share\a.jpg → file://server/share/a.jpg
    return QUrl::fromLocalFile(QDir::toNativeSeparators(path)).toString();
}

QString LibraryService::thumbUrlOf(int videoId) const
{
    const QString p = thumbPath(videoId);
    if (QFileInfo(p).isFile() && QFileInfo(p).size() > 0)
        return fileUrl(p);
    return QString();
}

QString LibraryService::thumbUrl(int videoId)
{
    if (!Db::instance()->isOpen())
        return QString();

    const QString p = thumbPath(videoId);
    if (QFileInfo(p).isFile() && QFileInfo(p).size() > 0)
        return fileUrl(p);

    requestThumb(videoId);
    return QString();
}

// 同时最多跑几个封面生成任务（每个都要在共享盘上读视频抽帧）
static const int kThumbConcurrency = 4;

void LibraryService::requestThumb(int videoId)
{
    if (m_thumbPending.contains(videoId) || m_thumbQueued.contains(videoId))
        return;

    // 并发满了就**排队**，不要丢弃：界面只在卡片被重新创建时才会再请求一次封面，
    // 丢弃的话那些卡片会一直空着 —— 这正是「很多封面迟迟加载不出来」的原因。
    if (m_thumbPending.size() >= kThumbConcurrency) {
        m_thumbQueued.insert(videoId);
        m_thumbQueue.append(videoId);
        return;
    }

    QSqlQuery q(Db::instance()->handle());
    q.prepare(QStringLiteral("SELECT path, duration_ms FROM videos WHERE id=?"));
    q.addBindValue(videoId);
    if (!q.exec() || !q.next())
        return;
    const QString src = q.value(0).toString();
    const qint64 durMs = q.value(1).toLongLong();
    if (!QFileInfo::exists(src))
        return;

    ensureThumbDir();
    const QString out = thumbPath(videoId);

    // ffmpeg 从约 30% 处取一帧，480 宽，够卡片用
    const double ss = durMs > 0 ? qMax(0.0, double(durMs) / 1000.0 * 0.3) : 0.5;
    QString ffmpeg = QCoreApplication::applicationDirPath() + QStringLiteral("/ffmpeg.exe");
    if (!QFileInfo::exists(ffmpeg))
        ffmpeg = QCoreApplication::applicationDirPath() + QStringLiteral("/../third_party/ffmpeg/ffmpeg.exe");
    if (!QFileInfo::exists(ffmpeg))
        return;

    m_thumbPending.insert(videoId);

    auto *proc = new QProcess(this);
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, proc, videoId, out](int code, QProcess::ExitStatus st) {
                m_thumbPending.remove(videoId);
                proc->deleteLater();
                if (code == 0 && st == QProcess::NormalExit && QFileInfo(out).size() > 0)
                    emit thumbReady(videoId);
                startNextThumb();   // 空出一个位置，把排队的补上来
            });
    proc->start(ffmpeg, { QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"),
                          QStringLiteral("error"), QStringLiteral("-ss"),
                          QString::number(ss, 'f', 2), QStringLiteral("-i"),
                          QDir::toNativeSeparators(src), QStringLiteral("-frames:v"),
                          QStringLiteral("1"), QStringLiteral("-vf"),
                          QStringLiteral("scale=480:-2"), QStringLiteral("-q:v"),
                          QStringLiteral("4"), QStringLiteral("-y"),
                          QDir::toNativeSeparators(out) });
}

// 一个任务结束后，把队列里等着的按顺序补上来（直到占满并发额度）
void LibraryService::startNextThumb()
{
    while (!m_thumbQueue.isEmpty() && m_thumbPending.size() < kThumbConcurrency) {
        const int next = m_thumbQueue.takeFirst();
        m_thumbQueued.remove(next);
        requestThumb(next);
    }
}

// ---------------------------------------------------------------- 预览片段

QString LibraryService::ffmpegExe() const
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList cands = {
        appDir + QStringLiteral("/ffmpeg.exe"),
        appDir + QStringLiteral("/third_party/ffmpeg/ffmpeg.exe"),
        appDir + QStringLiteral("/../third_party/ffmpeg/ffmpeg.exe"),
        appDir + QStringLiteral("/../../third_party/ffmpeg/ffmpeg.exe"),
    };
    for (const QString &c : cands) {
        if (QFileInfo::exists(c))
            return c;
    }
    return QString();
}

QString LibraryService::previewPathOf(int videoId) const
{
    return Db::instance()->workspaceRoot() + QStringLiteral("/工作台数据库/previews/")
           + QString::number(videoId) + QStringLiteral(".mp4");
}

QString LibraryService::previewFor(int videoId)
{
    if (!Db::instance()->isOpen())
        return QString();
    const QString p = previewPathOf(videoId);
    if (QFileInfo(p).isFile() && QFileInfo(p).size() > 512)
        return p;
    requestPreview(videoId);
    return QString();
}

// 低清短片（320 宽 / 10fps / 4 秒），生成很快，够卡片"看个大概"用。
// 对应旧版 backend/preview.js 的 /api/preview/:id。
void LibraryService::requestPreview(int videoId)
{
    if (m_previewPending.contains(videoId))
        return;

    const QString ffmpeg = ffmpegExe();
    if (ffmpeg.isEmpty())
        return;

    QSqlQuery q(Db::instance()->handle());
    q.prepare(QStringLiteral("SELECT path, duration_ms FROM videos WHERE id=?"));
    q.addBindValue(videoId);
    if (!q.exec() || !q.next())
        return;
    const QString src = q.value(0).toString();
    const qint64 durMs = q.value(1).toLongLong();
    if (!QFileInfo::exists(src))
        return;

    const QString out = previewPathOf(videoId);
    QDir().mkpath(QFileInfo(out).absolutePath());
    const double ss = durMs > 0 ? qMax(0.0, double(durMs) / 1000.0 * 0.3) : 0.5;

    m_previewPending.insert(videoId);

    auto *proc = new QProcess(this);
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, proc, videoId, out](int code, QProcess::ExitStatus st) {
                m_previewPending.remove(videoId);
                proc->deleteLater();
                if (code == 0 && st == QProcess::NormalExit && QFileInfo(out).size() > 512)
                    emit previewReady(videoId);
            });
    proc->start(ffmpeg, { QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"),
                          QStringLiteral("error"),
                          QStringLiteral("-ss"), QString::number(ss, 'f', 2),
                          QStringLiteral("-t"), QStringLiteral("4"),
                          QStringLiteral("-i"), QDir::toNativeSeparators(src),
                          QStringLiteral("-vf"), QStringLiteral("scale=320:-2"),
                          QStringLiteral("-r"), QStringLiteral("10"),
                          QStringLiteral("-an"),
                          QStringLiteral("-c:v"), QStringLiteral("libx264"),
                          QStringLiteral("-preset"), QStringLiteral("ultrafast"),
                          QStringLiteral("-crf"), QStringLiteral("30"),
                          QStringLiteral("-movflags"), QStringLiteral("+faststart"),
                          QStringLiteral("-f"), QStringLiteral("mp4"),
                          QStringLiteral("-y"), QDir::toNativeSeparators(out) });
}

// ---------------------------------------------------------------- 拖拽 / 拖放

// 发起真正的系统级拖拽：带文件 URL。拖到资源管理器 / 剪辑软件 = 复制素材；
// 拖到应用内的 DropArea 同样能收到（Qt 把应用内外统一成一条拖拽流）。
void LibraryService::startFileDrag(const QStringList &paths)
{
    if (paths.isEmpty())
        return;

    auto *mime = new QMimeData;
    QList<QUrl> urls;
    QStringList native;
    for (const QString &p : paths) {
        const QString np = QDir::toNativeSeparators(p.trimmed());
        if (np.isEmpty() || !QFileInfo::exists(np))
            continue;
        native << np;
        urls << QUrl::fromLocalFile(np);
    }
    if (urls.isEmpty()) {
        delete mime;
        return;
    }
    mime->setUrls(urls);
    mime->setText(native.join(QLatin1Char('\n')));

    auto *drag = new QDrag(this);
    drag->setMimeData(mime);
    drag->exec(Qt::CopyAction); // 阻塞到拖放结束（OS 行为）
    drag->deleteLater();
}

QStringList LibraryService::pathsOfVideos(const QVariantList &videoIds) const
{
    QStringList out;
    if (videoIds.isEmpty() || !Db::instance()->isOpen())
        return out;

    QSqlQuery q(Db::instance()->handle());
    q.prepare(QStringLiteral("SELECT path FROM videos WHERE id=?"));
    for (const QVariant &v : videoIds) {
        q.addBindValue(v.toInt());
        if (q.exec() && q.next()) {
            const QString p = q.value(0).toString();
            if (QFileInfo::exists(p))
                out << QDir::toNativeSeparators(p);
        }
    }
    return out;
}

QString LibraryService::pathFromUrl(const QString &url) const
{
    const QUrl u(url);
    if (!u.isLocalFile())
        return QString();
    return QDir::toNativeSeparators(u.toLocalFile());
}

// 把拖过来的文件/文件夹复制到目标目录；视频会顺带入索引并继承原素材的标签。
QVariantMap LibraryService::copyPathsTo(const QStringList &srcPaths, const QString &destDir)
{
    QVariantMap res;
    QVariantList copied;
    QVariantList errors;
    QVariantMap err;

    const QString root = Db::instance()->workspaceRoot();
    const QString target = normalizePath(destDir);
    if (target.isEmpty()) {
        err[QStringLiteral("error")] = QStringLiteral("未指定目标文件夹");
        errors.append(err);
        res[QStringLiteral("ok")] = false;
        res[QStringLiteral("copied")] = copied;
        res[QStringLiteral("errors")] = errors;
        return res;
    }
    if (isProtected(target, root)) {
        err[QStringLiteral("error")] = QStringLiteral("目标位置受保护");
        errors.append(err);
        res[QStringLiteral("ok")] = false;
        res[QStringLiteral("copied")] = copied;
        res[QStringLiteral("errors")] = errors;
        return res;
    }
    QDir().mkpath(target);

    auto uniqueDest = [&](const QString &dir, const QString &name) {
        QString dst = dir + QLatin1Char('/') + name;
        if (!QFileInfo::exists(dst))
            return dst;
        const QFileInfo fi(name);
        const QString base = fi.completeBaseName();
        const QString ext = fi.suffix();
        for (int k = 2; QFileInfo::exists(dst); ++k) {
            dst = dir + QLatin1Char('/') + base + QStringLiteral(" (") + QString::number(k)
                  + QLatin1Char(')') + (ext.isEmpty() ? QString() : QLatin1Char('.') + ext);
        }
        return dst;
    };

    std::function<bool(const QString &, const QString &)> copyTree =
        [&](const QString &src, const QString &dst) -> bool {
        if (!QDir().mkpath(dst))
            return false;
        const QFileInfoList entries =
            QDir(src).entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot);
        for (const QFileInfo &fi : entries) {
            if (kProtectedNames.contains(fi.fileName())
                || fi.fileName().startsWith(QLatin1Char('.')))
                continue;
            const QString to = dst + QLatin1Char('/') + fi.fileName();
            if (fi.isDir()) {
                if (!copyTree(fi.absoluteFilePath(), to))
                    return false;
            } else if (!QFile::copy(fi.absoluteFilePath(), to)) {
                return false;
            }
        }
        return true;
    };

    QVector<QPair<QString, QString>> newFiles; // dest → src（用于继承标签）

    for (const QString &s : srcPaths) {
        const QString src = normalizePath(s);
        if (isProtected(src, root)) {
            QVariantMap e;
            e[QStringLiteral("path")] = src;
            e[QStringLiteral("error")] = QStringLiteral("受保护目录，不允许复制");
            errors.append(e);
            continue;
        }
        const QFileInfo fi(src);
        if (fi.isDir()) {
            const QString sl = src.toLower();
            const QString tl = target.toLower();
            if (tl == sl || tl.startsWith(sl + QLatin1Char('\\'))) {
                QVariantMap e;
                e[QStringLiteral("path")] = src;
                e[QStringLiteral("error")] = QStringLiteral("不能复制到自身或其子目录");
                errors.append(e);
                continue;
            }
            const QString dst = uniqueDest(target, fi.fileName());
            if (copyTree(src, dst)) {
                copied.append(dst);
            } else {
                QVariantMap e;
                e[QStringLiteral("path")] = src;
                e[QStringLiteral("error")] = QStringLiteral("复制失败");
                errors.append(e);
            }
        } else if (fi.isFile()) {
            const QString dst = uniqueDest(target, fi.fileName());
            if (QFile::copy(src, dst)) {
                copied.append(dst);
                if (MediaMeta::isVideoFile(dst))
                    newFiles.append({ dst, src });
            } else {
                QVariantMap e;
                e[QStringLiteral("path")] = src;
                e[QStringLiteral("error")] = QStringLiteral("复制失败");
                errors.append(e);
            }
        }
    }

    // 新复制进来的视频：入索引 + 继承原素材标签（与 createCategoryFolder 一致）
    QSqlDatabase &db = Db::instance()->handle();
    for (const auto &pair : newFiles) {
        const QString &dest = pair.first;
        const QString &src = pair.second;
        const QFileInfo di(dest);
        const qint64 now = QDateTime::currentMSecsSinceEpoch();

        QSqlQuery ins(db);
        ins.prepare(QStringLiteral(
            "INSERT INTO videos (path,name,parent,ext,size,mtime,indexed_at,updated_at) "
            "VALUES (?,?,?,?,?,?,?,?) ON CONFLICT(path) DO UPDATE SET "
            "size=excluded.size, mtime=excluded.mtime, updated_at=excluded.updated_at"));
        ins.addBindValue(normalizePath(di.absoluteFilePath()));
        ins.addBindValue(di.fileName());
        ins.addBindValue(normalizePath(di.absolutePath()));
        ins.addBindValue(QLatin1Char('.') + di.suffix().toLower());
        ins.addBindValue(qint64(di.size()));
        ins.addBindValue(qint64(di.lastModified().toMSecsSinceEpoch()));
        ins.addBindValue(now);
        ins.addBindValue(now);
        if (!ins.exec())
            continue;

        QSqlQuery fid(db);
        fid.prepare(QStringLiteral("SELECT id FROM videos WHERE path=?"));
        fid.addBindValue(normalizePath(di.absoluteFilePath()));
        if (!fid.exec() || !fid.next())
            continue;
        const int newId = fid.value(0).toInt();

        const MediaMeta::Info meta = MediaMeta::read(dest);
        if (meta.durationMs > 0) {
            QSqlQuery upd(db);
            upd.prepare(QStringLiteral(
                "UPDATE videos SET duration_ms=?,width=?,height=?,codec=? WHERE id=?"));
            upd.addBindValue(meta.durationMs);
            upd.addBindValue(meta.width);
            upd.addBindValue(meta.height);
            upd.addBindValue(meta.codec);
            upd.addBindValue(newId);
            upd.exec();
        }

        QSqlQuery srcId(db);
        srcId.prepare(QStringLiteral("SELECT id FROM videos WHERE path=?"));
        srcId.addBindValue(normalizePath(src));
        if (srcId.exec() && srcId.next()) {
            QSqlQuery cp(db);
            cp.prepare(QStringLiteral("INSERT OR IGNORE INTO video_tags (video_id,tag_id) "
                                      "SELECT ?, tag_id FROM video_tags WHERE video_id=?"));
            cp.addBindValue(newId);
            cp.addBindValue(srcId.value(0).toInt());
            cp.exec();
        }
    }

    if (!copied.isEmpty())
        bumpVersion();

    res[QStringLiteral("ok")] = !copied.isEmpty();
    res[QStringLiteral("copied")] = copied;
    res[QStringLiteral("errors")] = errors;
    return res;
}
