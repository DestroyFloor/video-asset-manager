#pragma once

#include <QObject>
#include <QProcess>
#include <QString>
#include <qqmlintegration.h>

// 1080p H.264 代理转码（对应旧版 backend/playback.js 的思路）。
//
// 为什么需要它：4K HEVC 素材在很多机器上「解码能力不足」。以本机为例，
// 实测纯解码这个 4K60 10-bit 素材只有 ~26fps（素材要 60fps），任何播放器都必然丢帧。
// 转成 1080p H.264 之后，**纯 CPU 软解**就能轻松跑满 60fps，不依赖任何显卡硬解能力，
// 于是「同一套配置推到任意机器都能流畅播放」这件事才成立。
//
// 编码统一走 CPU（libx264 ultrafast）：一是要能推到没有独显的机器上，二是实测
// 转码瓶颈在「解码 4K」这一步，编码再快也提升不了整体速度。
//
// 代理文件优先写到工作区的 工作台数据库\proxies\，全组复用：谁先打开谁转一次，其他人秒开。
// 转码过程先写本机临时文件，成功后再搬到缓存目录（避免读写共享盘互相抢带宽）。
class ProxyService : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Proxy)
    QML_SINGLETON

    Q_PROPERTY(bool ffmpegAvailable READ ffmpegAvailable NOTIFY ffmpegAvailableChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(int progress READ progress NOTIFY progressChanged) // 0-100，负数=未知
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)

public:
    explicit ProxyService(QObject *parent = nullptr);
    ~ProxyService() override;

    bool ffmpegAvailable() const { return !m_ffmpeg.isEmpty(); }
    bool busy() const { return m_proc != nullptr; }
    int progress() const { return m_progress; }
    QString statusText() const { return m_statusText; }
    QString ffmpegPath() const { return m_ffmpeg; }

    // 代理文件路径（只计算，不转码）
    Q_INVOKABLE QString proxyPathFor(const QString &srcPath) const;
    Q_INVOKABLE bool proxyExists(const QString &srcPath) const;

    // 异步转码；durationSeconds 用于算进度（可传 0）
    Q_INVOKABLE void ensureProxy(const QString &srcPath, double durationSeconds);
    Q_INVOKABLE void cancel();

    // 代理缓存根目录（有工作区时在共享盘，否则在本机缓存目录）
    Q_INVOKABLE QString cacheDir() const;

signals:
    void ffmpegAvailableChanged();
    void busyChanged();
    void progressChanged();
    void statusTextChanged();
    void finished(const QString &srcPath, const QString &proxyPath);
    void failed(const QString &srcPath, const QString &message);

private:
    void startProcess(const QString &srcPath, const QString &outPath, double duration);
    void onStdOut();
    void onFinished(int exitCode, QProcess::ExitStatus status);
    void setStatus(const QString &text);
    void setProgress(int p);
    static QString findFfmpeg();

    QString m_ffmpeg;
    QProcess *m_proc = nullptr;
    QString m_srcPath;
    QString m_outPath;
    QString m_tmpPath;
    QString m_stdErr;
    double m_duration = 0.0;
    int m_progress = -1;
    QString m_statusText;
};
