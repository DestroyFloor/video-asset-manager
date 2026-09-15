#include "MediaMeta.h"

#include <QByteArray>
#include <QFile>
#include <QFileInfo>

namespace {

// 与 backend/metadata.js 的 VIDEO_EXTS 一致
const char *const kVideoExts[] = {
    ".mp4",  ".mov", ".m4v",  ".avi",  ".mkv",  ".flv", ".wmv", ".ts", ".m2ts", ".mts",
    ".webm", ".mpg", ".mpeg", ".3gp",  ".rm",   ".rmvb", ".vob", ".asf", ".mod"
};

// MOV/MP4 里出现这些 fourcc 即认为是视频轨的编码
const char *const kVideoCodecs[] = { "avc1", "hvc1", "hev1", "hev2", "vp09",
                                     "av01", "mp4v", "dvhe", "dvh1", "mpeg4" };

inline quint32 readU32BE(const QByteArray &b, int off)
{
    return (quint32(quint8(b.at(off))) << 24) | (quint32(quint8(b.at(off + 1))) << 16)
           | (quint32(quint8(b.at(off + 2))) << 8) | quint32(quint8(b.at(off + 3)));
}

inline quint64 readU64BE(const QByteArray &b, int off)
{
    return (quint64(readU32BE(b, off)) << 32) | quint64(readU32BE(b, off + 4));
}

// 遍历一层 box；回调返回 false 可提前结束
template <typename Fn>
void walkBoxes(const QByteArray &buf, int start, int len, Fn cb)
{
    const int end = start + len;
    int off = start;
    while (off + 8 <= end) {
        quint64 size = readU32BE(buf, off);
        const QByteArray type = buf.mid(off + 4, 4);
        int header = 8;
        if (size == 1) {
            if (off + 16 > end)
                break;
            size = readU64BE(buf, off + 8);
            header = 16;
        } else if (size == 0) {
            size = quint64(end - off);
        }
        if (size < quint64(header))
            break;

        const int dataOff = off + header;
        const int dataLen = int(qMin<quint64>(size - quint64(header), quint64(end - dataOff)));
        if (!cb(type, dataOff, dataLen))
            break;
        off += int(size);
    }
}

// mvhd：timescale + duration → 毫秒
qint64 parseMvhd(const QByteArray &buf, int off, int len)
{
    if (len < 20)
        return 0;
    const int version = quint8(buf.at(off));
    if (version == 1) {
        if (len < 28)
            return 0;
        const quint32 timescale = readU32BE(buf, off + 20);
        const quint64 duration = readU64BE(buf, off + 24);
        if (!timescale)
            return 0;
        return qint64(duration * 1000 / timescale);
    }
    const quint32 timescale = readU32BE(buf, off + 12);
    const quint32 duration = readU32BE(buf, off + 16);
    if (!timescale)
        return 0;
    return qint64(duration) * 1000 / qint64(timescale);
}

// tkhd：宽高是 16.16 定点数
bool parseTkhd(const QByteArray &buf, int off, int len, int *w, int *h)
{
    const int version = quint8(buf.at(off));
    int widthOff = 0;
    int heightOff = 0;
    if (version == 1) {
        if (len < 96)
            return false;
        widthOff = off + 88;
        heightOff = off + 92;
    } else {
        if (len < 84)
            return false;
        widthOff = off + 76;
        heightOff = off + 80;
    }
    *w = int(readU32BE(buf, widthOff) / 65536);
    *h = int(readU32BE(buf, heightOff) / 65536);
    return true;
}

void parseMoov(const QByteArray &buf, MediaMeta::Info *out)
{
    walkBoxes(buf, 0, buf.size(), [&](const QByteArray &type, int off, int len) -> bool {
        if (type == "mvhd") {
            const qint64 d = parseMvhd(buf, off, len);
            if (d > 0)
                out->durationMs = d;
        } else if (type == "trak") {
            walkBoxes(buf, off, len, [&](const QByteArray &t2, int o2, int l2) -> bool {
                if (t2 == "tkhd" && out->width == 0) {
                    int w = 0;
                    int h = 0;
                    if (parseTkhd(buf, o2, l2, &w, &h) && w > 0) {
                        out->width = w;
                        out->height = h;
                        return false;
                    }
                }
                return true;
            });
        }
        return true;
    });
}

// 与 JS 版一致：在 moov 里找 stsd，紧跟着的 fourcc 就是视频编码
QString findCodec(const QByteArray &buf)
{
    int i = buf.indexOf("stsd");
    while (i >= 0 && i + 20 <= buf.size()) {
        const QByteArray t = buf.mid(i + 16, 4);
        for (const char *c : kVideoCodecs) {
            if (t == c)
                return QString::fromLatin1(t);
        }
        i = buf.indexOf("stsd", i + 4);
    }
    return QString();
}

} // namespace

namespace MediaMeta {

bool isVideoFile(const QString &fileName)
{
    const QString ext = QFileInfo(fileName).suffix().toLower();
    if (ext.isEmpty())
        return false;
    const QString withDot = QLatin1Char('.') + ext;
    for (const char *e : kVideoExts) {
        if (withDot == QLatin1String(e))
            return true;
    }
    return false;
}

QStringList videoExtensions()
{
    QStringList out;
    for (const char *e : kVideoExts)
        out << QString::fromLatin1(e);
    return out;
}

Info read(const QString &filePath)
{
    Info info;

    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly))
        return info;

    const qint64 fileSize = f.size();

    // 顶层 box 里找 moov（最多扫 800 个 box，与 JS 版一致），
    // moov 只读前 96MB 足够覆盖 mvhd/tkhd/stsd
    QByteArray moov;
    qint64 off = 0;
    for (int i = 0; i < 800 && off < fileSize; ++i) {
        if (!f.seek(off))
            break;
        const QByteArray hdr = f.read(16);
        if (hdr.size() < 8)
            break;

        quint64 size = readU32BE(hdr, 0);
        const QByteArray type = hdr.mid(4, 4);
        int header = 8;
        if (size == 1) {
            if (hdr.size() < 16)
                break;
            size = readU64BE(hdr, 8);
            header = 16;
        } else if (size == 0) {
            size = quint64(fileSize - off);
        }
        if (size < quint64(header))
            break;

        if (type == "moov") {
            const qint64 moovSize =
                qMin<qint64>(qint64(size) - header, qint64(96) * 1024 * 1024);
            f.seek(off + header);
            moov = f.read(moovSize);
            break;
        }
        off += qint64(size);
    }

    if (moov.isEmpty())
        return info;

    parseMoov(moov, &info);
    info.codec = findCodec(moov);
    return info;
}

} // namespace MediaMeta
