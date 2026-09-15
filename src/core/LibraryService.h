#pragma once

#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <qqmlintegration.h>

// 素材库的全部数据操作，对应旧版 backend/api.js + copy.js + open.js：
//   查询/过滤/排序、标签增删、调用次数、文件夹树、新建分类文件夹、打开文件或目录、封面、预览。
//
// 所有操作都是「读库/写库/复制文件」，**绝不移动或删除原视频**（与旧版原则一致）。
class LibraryService : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Library)
    QML_SINGLETON

    // meta.version：别的机器改库时它自增，界面据此刷新（对应旧版 SSE 推送的等价物）
    Q_PROPERTY(int version READ version NOTIFY libraryChanged)

public:
    explicit LibraryService(QObject *parent = nullptr);

    int version() const;

    // ---- 查询 ----
    // filter: { shot:[tagId], product:[tagId], avail:[tagId], andOr:"and"|"or",
    //           search:"", folder:"", onlyHot:bool, sortBy:"usage"|"duration"|"date"|"tags",
    //           sortDir:"asc"|"desc", limit:int }
    // 返回 { total:int, items:[ {...video, tag_count, tags:[{id,name,category}]} ] }
    Q_INVOKABLE QVariantMap query(const QVariantMap &filter);
    Q_INVOKABLE QVariantMap videoDetail(int videoId);

    // ---- 标签 ----
    Q_INVOKABLE QVariantMap categories();
    Q_INVOKABLE QVariantMap createTag(const QString &category, const QString &name);
    Q_INVOKABLE bool renameTag(int tagId, const QString &name);
    Q_INVOKABLE bool deleteTag(int tagId);
    Q_INVOKABLE bool assignTags(const QVariantList &videoIds, const QVariantList &tagIds,
                                const QString &action);

    // ---- 调用次数 ----
    Q_INVOKABLE bool adjustUsage(const QVariantList &videoIds, int delta);

    // ---- 删除素材（移到系统回收站，可从回收站找回）----
    // 返回 { ok:bool, removed:[id], errors:[{name, error}] }
    Q_INVOKABLE QVariantMap deleteVideos(const QVariantList &videoIds);

    // ---- 文件夹树 ----
    Q_INVOKABLE QVariantList folders();
    Q_INVOKABLE QVariantMap deleteFolders(const QStringList &paths);
    Q_INVOKABLE QVariantMap copyFolders(const QStringList &sources, const QString &dest);

    // ---- 新建分类文件夹（复制，绝不动原视频）----
    Q_INVOKABLE QVariantMap createCategoryFolder(const QVariantList &videoIds, const QString &account);

    // ---- 打开 ----
    Q_INVOKABLE void openFolder(const QString &path);
    Q_INVOKABLE void openFile(const QString &path);

    // ---- 外部播放器 ----
    // 没配过播放器（或者配的那个已经不在了）时 openWithPlayer() 返回 false，
    // 界面据此弹「选一个播放器程序」的对话框，选完写进 config.json（playerExe）。
    Q_INVOKABLE QString playerExe() const;
    Q_INVOKABLE void setPlayerExe(const QString &path);
    Q_INVOKABLE bool openWithPlayer(const QString &path);

    // ---- 拖拽 / 拖放 ----
    // 用 C++ 的 QDrag 发起真正的系统级拖拽（带文件 URL）：
    //   拖到资源管理器 / 剪辑软件 → 复制素材文件
    //   拖到应用内的 DropArea      → 同样收得到（Qt 支持应用内外同一条拖拽流）
    Q_INVOKABLE void startFileDrag(const QStringList &paths);
    // 素材 id → 原生路径
    Q_INVOKABLE QStringList pathsOfVideos(const QVariantList &videoIds) const;
    // 拖放拿到的 URL → 原生路径
    Q_INVOKABLE QString pathFromUrl(const QString &url) const;
    // 把拖过来的文件/文件夹复制到目标目录（视频会顺带入索引并继承标签）
    Q_INVOKABLE QVariantMap copyPathsTo(const QStringList &srcPaths, const QString &destDir);

    // ---- 路径 → URL ----
    // QML 的 Image.source / MediaPlayer.source 需要 URL；UNC 共享盘不能靠字符串拼 "file:///"，
    // 必须交给 QUrl::fromLocalFile（本地盘 → file:///D:/…，共享盘 → file://server/share/…）。
    Q_INVOKABLE QString fileUrl(const QString &path) const;

    // ---- 封面（不存在时触发 ffmpeg 异步生成，完成后发 thumbReady）----
    Q_INVOKABLE QString thumbUrl(int videoId);         // 不存在则触发生成并返回空
    Q_INVOKABLE QString thumbUrlOf(int videoId) const; // 只返回现有封面 URL，不触发生成
    Q_INVOKABLE QString thumbPath(int videoId) const;  // 原生路径（供外部播放器等用）

    // ---- 预览片段（低清、几秒，用于卡片原地预览；对应旧版 /api/preview/:id）----
    Q_INVOKABLE QString previewFor(int videoId);        // 不存在则触发生成并返回空
    Q_INVOKABLE QString previewPathOf(int videoId) const;

    void bumpVersion();

signals:
    void libraryChanged();
    void thumbReady(int videoId);
    void previewReady(int videoId);

private:
    QSet<int> idsForAny(const QList<int> &tagIds) const;
    QSet<int> idsForAll(const QList<int> &tagIds) const;
    QSet<int> searchTagIds(const QString &term) const;
    QVariantMap buildVideoItem(int id, const QMap<int, int> &tagCounts) const;

    void requestThumb(int videoId);
    void requestPreview(int videoId);
    void ensureThumbDir() const;
    QString ffmpegExe() const;
    void startNextThumb();   // 一个封面生成完就补下一个（并发上限见实现）

    QSet<int> m_thumbPending;
    QSet<int> m_previewPending;
    // 等待生成封面的队列：并发满了先排着而不是丢弃 —— 丢了那些卡片会一直空着
    QList<int> m_thumbQueue;
    QSet<int> m_thumbQueued;

    // folders() 的缓存：遍历整个工作区（几千个文件、几百个目录）在网络盘上要好几秒，
    // 而界面每次刷新都会调它 —— 不缓存的话点一次「重新扫描」就像卡死。
    QVariantList m_folderCache;
    qint64 m_folderCacheAt = 0;
    int m_folderCacheVersion = -1;
};
