#pragma once

#include <QString>
#include <QStringList>

// 对应旧版 backend/metadata.js。
//
// 直接解析 MP4/MOV 的 box 结构拿「时长 / 分辨率 / 视频编码」，不依赖 ffprobe——
// 扫描上千个素材时，省掉一次进程启动的开销，快非常多。
namespace MediaMeta {

struct Info
{
    qint64 durationMs = 0;
    int width = 0;
    int height = 0;
    QString codec; // avc1 / hvc1 / hev1 / vp09 / av01 ...
};

// 与 backend/metadata.js 的 VIDEO_EXTS 保持一致
bool isVideoFile(const QString &fileName);
QStringList videoExtensions();

// 解析失败时返回全 0（不抛异常），与旧版行为一致
Info read(const QString &filePath);

} // namespace MediaMeta
