#include "ProxyService.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

#include "Db.h"

namespace {

// 统一缩放到「长边不超过 1920」：横版→1920x1080，竖版→1080x1920，都保持宽高比与偶数边
const char *kScaleFilter =
    "scale=1920:1920:force_original_aspect_ratio=decrease:force_divisible_by=2";

// 源文件指纹（路径 + 大小 + 修改时间）：源被替换就换 key，代理自然失效重转
QString sourceKey(const QString &srcPath)
{
    const QFileInfo fi(QDir::toNativeSeparators(srcPath));
    return QStringLiteral("%1|%2|%3")
        .arg(fi.absoluteFilePath(),
             QString::number(fi.size()),
             QString::number(fi.lastModified().toMSecsSinceEpoch()));
}

QString hashOf(const QString &key)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha1).toHex().left(20));
}

} // namespace

ProxyService::ProxyService(QObject *parent) : QObject(parent)
{
    m_ffmpeg = findFfmpeg();
    if (!m_ffmpeg.isEmpty())
        qInfo("[proxy] ffmpeg: %s", qPrintable(m_ffmpeg));
    else
        qWarning("[proxy] 未找到 ffmpeg.exe，无法转码");
}

ProxyService::~ProxyService()
{
    if (m_proc) {
        m_proc->kill();
        m_proc->waitForFinished(3000);
    }
}

QString ProxyService::findFfmpeg()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        appDir + QStringLiteral("/ffmpeg.exe"),
        appDir + QStringLiteral("/third_party/ffmpeg/ffmpeg.exe"),
        appDir + QStringLiteral("/../third_party/ffmpeg/ffmpeg.exe"),
        appDir + QStringLiteral("/../../third_party/ffmpeg/ffmpeg.exe"),
    };
    for (const QString &c : candidates) {
        const QFileInfo fi(c);
        if (fi.isFile())
            return fi.absoluteFilePath();
    }
    return QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
}

QString ProxyService::cacheDir() const
{
    // 有工作区就放共享盘（全组复用），否则本机缓存
    if (Db::instance()->isOpen() && !Db::instance()->workspaceRoot().isEmpty()) {
        return Db::instance()->workspaceRoot() + QStringLiteral("/工作台数据库/proxies");
    }
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
           + QStringLiteral("/proxies");
}

QString ProxyService::proxyPathFor(const QString &srcPath) const
{
    return cacheDir() + QLatin1Char('/') + hashOf(sourceKey(srcPath)) + QStringLiteral(".mp4");
}

bool ProxyService::proxyExists(const QString &srcPath) const
{
    const QFileInfo fi(proxyPathFor(srcPath));
    return fi.isFile() && fi.size() > 1024;
}

void ProxyService::setStatus(const QString &text)
{
    if (m_statusText == text)
        return;
    m_statusText = text;
    emit statusTextChanged();
}

void ProxyService::setProgress(int p)
{
    if (m_progress == p)
        return;
    m_progress = p;
    emit progressChanged();
}

void ProxyService::ensureProxy(const QString &srcPath, double durationSeconds)
{
    if (m_proc) {
        emit failed(srcPath, QStringLiteral("已有转码任务在进行"));
        return;
    }
    if (m_ffmpeg.isEmpty()) {
        emit failed(srcPath, QStringLiteral("未找到 ffmpeg.exe"));
        return;
    }

    const QFileInfo fi(QDir::toNativeSeparators(srcPath));
    if (!fi.isFile()) {
        emit failed(srcPath, QStringLiteral("源文件不存在"));
        return;
    }

    const QString out = proxyPathFor(srcPath);
    if (QFileInfo(out).isFile() && QFileInfo(out).size() > 1024) {
        setProgress(100);
        setStatus(QStringLiteral("代理已就绪"));
        emit finished(srcPath, out);
        return;
    }

    // 转码先输出到本机临时文件，成功后再搬到缓存目录：
    // 边读共享盘（源）边写共享盘（结果）会互相抢带宽，明显拖慢转码。
    m_tmpPath = QDir::tempPath() + QStringLiteral("/wbproxy_") + hashOf(sourceKey(srcPath))
                + QStringLiteral(".mp4");

    startProcess(srcPath, out, durationSeconds);
}

void ProxyService::startProcess(const QString &srcPath, const QString &outPath, double duration)
{
    m_srcPath = QDir::toNativeSeparators(srcPath);
    m_outPath = outPath;
    m_duration = duration;
    m_stdErr.clear();
    QFile::remove(m_tmpPath);

    QStringList args;
    args << "-hide_banner" << "-loglevel" << "error"
         << "-nostats" << "-progress" << "pipe:1" << "-y"
         << "-i" << m_srcPath
         // 只取第一条视频流 + 可选的第一条音频流（素材常带多条音轨）
         << "-map" << "0:v:0" << "-map" << "0:a:0?" << "-sn"
         << "-vf" << QString::fromLatin1(kScaleFilter)
         // 编码统一走 CPU：一是要能推到没有独显的机器上，二是实测转码瓶颈在
         // 「解码 4K」（约 26fps），编码再快也提升不了整体速度。
         << "-c:v" << "libx264" << "-preset" << "ultrafast" << "-crf" << "26"
         << "-c:a" << "aac" << "-b:a" << "128k" << "-ac" << "2"
         << "-movflags" << "+faststart"
         << "-f" << "mp4"
         << m_tmpPath;

    m_proc = new QProcess(this);
    connect(m_proc, &QProcess::readyReadStandardOutput, this, &ProxyService::onStdOut);
    connect(m_proc, &QProcess::readyReadStandardError, this, [this]() {
        if (!m_proc)
            return;
        m_stdErr += QString::fromLocal8Bit(m_proc->readAllStandardError());
        if (m_stdErr.size() > 4000)
            m_stdErr = m_stdErr.right(4000);
    });
    connect(m_proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &ProxyService::onFinished);

    setStatus(QStringLiteral("正在转码 1080p 代理…"));
    setProgress(0);
    m_proc->start(m_ffmpeg, args);
    emit busyChanged();
}

void ProxyService::onStdOut()
{
    if (!m_proc)
        return;
    const QByteArray data = m_proc->readAllStandardOutput();
    const QList<QByteArray> lines = data.split('\n');
    for (const QByteArray &raw : lines) {
        const QByteArray line = raw.trimmed();
        if (!line.startsWith("out_time_us="))
            continue;
        bool ok = false;
        const qint64 us = line.mid(int(qstrlen("out_time_us="))).toLongLong(&ok);
        if (!ok)
            continue;
        if (m_duration > 0.5) {
            const double sec = double(us) / 1e6;
            setProgress(int(qBound(0.0, sec / m_duration * 100.0, 100.0)));
        }
    }
}

void ProxyService::onFinished(int exitCode, QProcess::ExitStatus status)
{
    if (m_proc) {
        m_stdErr += QString::fromLocal8Bit(m_proc->readAllStandardError());
        m_proc->deleteLater();
        m_proc = nullptr;
    }
    emit busyChanged();

    const QString src = m_srcPath;
    const QString out = m_outPath;
    const QString tmp = m_tmpPath;

    const QFileInfo tmpInfo(tmp);
    if (exitCode == 0 && status == QProcess::NormalExit && tmpInfo.isFile()
        && tmpInfo.size() > 1024) {
        setStatus(QStringLiteral("正在保存代理…"));
        QDir().mkpath(QFileInfo(out).absolutePath());
        QFile::remove(out);

        bool moved = QFile::rename(tmp, out);
        if (!moved) {
            // 本机临时目录 → 共享盘 属跨卷，rename 会失败，退化成复制
            moved = QFile::copy(tmp, out);
            if (moved)
                QFile::remove(tmp);
        }
        if (moved) {
            setProgress(100);
            setStatus(QStringLiteral("代理已就绪"));
            emit finished(src, out);
            return;
        }
        setProgress(-1);
        setStatus(QStringLiteral("代理保存失败"));
        emit failed(src, QStringLiteral("无法把代理写入缓存目录：") + out);
        return;
    }

    QFile::remove(tmp);
    setProgress(-1);
    setStatus(QStringLiteral("转码失败"));
    QString msg = m_stdErr.trimmed();
    if (msg.isEmpty())
        msg = QStringLiteral("ffmpeg 退出码 %1").arg(exitCode);
    qWarning("[proxy] 转码失败: %s", qPrintable(msg));
    emit failed(src, msg.right(400));
}

void ProxyService::cancel()
{
    if (!m_proc)
        return;
    m_proc->kill();
    setStatus(QStringLiteral("已取消转码"));
    setProgress(-1);
}
