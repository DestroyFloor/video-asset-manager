#pragma once

#include <QElapsedTimer>
#include <QMutex>
#include <QQuickFramebufferObject>
#include <QSharedPointer>
#include <QString>
#include <QTimer>
#include <qqmlintegration.h>

struct mpv_handle;
struct mpv_render_context;

class MpvItem;

// mpv 的 render update 回调运行在 mpv 的 vo 线程（既不是 GUI 线程，也不是 Qt 的渲染线程），
// 而 MpvItem 是在 GUI 线程析构的。之前直接把裸 MpvItem* 交给 mpv，于是出现这样的窗口：
// Item 已经析构，但 render context 还活着（MpvRenderer 仍持有 MpvState，等着在渲染线程释放），
// vo 线程照旧回调，对野指针调用 QMetaObject::invokeMethod。
// 卡片「预览 → 停止」的崩溃现场正是这条路径（dump 符号化：vo 线程 → mpvRenderUpdate()
// → QMetaObject::invokeMethod() → 0xC0000005，RIP 落在堆地址上）。
//
// 用一个与 render context 同生命周期的闸门把回调与对象生命周期串起来：
// 回调持锁读 item，MpvItem 析构的第一件事就是持锁把 item 置空。
struct MpvUpdateGate
{
    QMutex mutex;
    MpvItem *item = nullptr; // 仅 mpv 的 vo 线程读、MpvItem 构造/析构写
};

// mpv 实例 + render context 的共享持有体。
// Item 与 Renderer 各持一份 QSharedPointer，保证「先释放 render context、再销毁 mpv」的顺序，
// 且谁先析构都不会让另一方拿到野指针。
struct MpvState
{
    mpv_handle *mpv = nullptr;
    mpv_render_context *ctx = nullptr;
    MpvUpdateGate gate;
    ~MpvState();
};

// 用 libmpv 的 render API 把 mpv 画面直接画进 Qt 的场景图——而不是旧版那种
// `mpv.exe --wid=<子窗口 HWND>` 注窗。于是圆角、叠加控件、多实例、全屏都只是普通 QML 组件。
class MpvItem : public QQuickFramebufferObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(MpvVideo)

    Q_PROPERTY(QString source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(bool paused READ paused WRITE setPaused NOTIFY pausedChanged)
    Q_PROPERTY(double position READ position NOTIFY positionChanged)
    Q_PROPERTY(double duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(int volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(bool muted READ muted WRITE setMuted NOTIFY mutedChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY errorTextChanged)
    Q_PROPERTY(QString decoderInfo READ decoderInfo NOTIFY decoderInfoChanged)
    // 硬件解码方式，可在播放中切换（auto-copy / d3d11va / nvdec / no ...）
    Q_PROPERTY(QString hwdecMode READ hwdecMode WRITE setHwdecMode NOTIFY hwdecModeChanged)
    // 当前实际生效的解码器：空/“no” 表示软解
    Q_PROPERTY(QString hwdecCurrent READ hwdecCurrent NOTIFY hwdecCurrentChanged)
    // 视频原始分辨率（用于判断要不要转 1080p 代理）
    Q_PROPERTY(int videoWidth READ videoWidth NOTIFY videoSizeChanged)
    Q_PROPERTY(int videoHeight READ videoHeight NOTIFY videoSizeChanged)
    // 实际渲染帧率（由 mpv 的「有新帧」回调统计），用来判断卡不卡
    Q_PROPERTY(double renderFps READ renderFps NOTIFY renderFpsChanged)
    // 显卡 / OpenGL 信息，由 Renderer 在创建渲染上下文时回填
    Q_PROPERTY(QString gpuInfo READ gpuInfo NOTIFY gpuInfoChanged)
    // 是否已经播到结尾。keep-open 会把画面停在最后一帧，但 pause 仍是 false，
    // 所以界面不能只看 paused 决定按钮显示「播放」还是「重播」。
    Q_PROPERTY(bool eof READ eof NOTIFY eofChanged)

public:
    explicit MpvItem(QQuickItem *parent = nullptr);
    ~MpvItem() override;

    QQuickFramebufferObject::Renderer *createRenderer() const override;

    QString source() const { return m_source; }
    void setSource(const QString &path);

    bool paused() const { return m_paused; }
    void setPaused(bool paused);

    double position() const { return m_position; }
    double duration() const { return m_duration; }

    int volume() const { return m_volume; }
    void setVolume(int volume);

    bool muted() const { return m_muted; }
    void setMuted(bool muted);

    bool eof() const { return m_eof; }

    QString errorText() const { return m_error; }
    QString decoderInfo() const { return m_decoderInfo; }

    QString hwdecMode() const { return m_hwdecMode; }
    void setHwdecMode(const QString &mode);

    QString hwdecCurrent() const { return m_hwdecCurrent; }

    int videoWidth() const { return m_videoWidth; }
    int videoHeight() const { return m_videoHeight; }

    double renderFps() const { return m_renderFps; }

    QString gpuInfo() const { return m_gpuInfo; }
    void setGpuInfo(const QString &info); // 由 Renderer 从渲染线程调回

    QSharedPointer<MpvState> state() const { return m_state; }

    // mpv 的 update callback 可能在别的线程，用 QueuedConnection 调到这里
    Q_INVOKABLE void requestFrame();

    Q_INVOKABLE void togglePause();
    // 从头重播（播完之后按钮显示为「重播」，点它走这里）
    Q_INVOKABLE void replay();
    Q_INVOKABLE void seekSeconds(double seconds);
    Q_INVOKABLE void seekRelative(double seconds);

signals:
    void sourceChanged();
    void pausedChanged();
    void positionChanged();
    void durationChanged();
    void volumeChanged();
    void mutedChanged();
    void errorTextChanged();
    void decoderInfoChanged();
    void hwdecModeChanged();
    void hwdecCurrentChanged();
    void videoSizeChanged();
    void renderFpsChanged();
    void gpuInfoChanged();
    void eofChanged();

private:
    void poll();               // 定时读时间轴/时长/解码器信息
    void handleMpvEvents();    // 处理 mpv 事件队列
    bool readDouble(const char *name, double *out) const;
    void updateDecoderInfo();
    void updateVideoSize();
    void setEof(bool eof);     // 只在真正变化时发 eofChanged

    QSharedPointer<MpvState> m_state;
    QString m_source;
    QString m_error;
    QString m_decoderInfo;
    QString m_hwdecMode = QStringLiteral("auto-copy");
    QString m_hwdecCurrent;
    QString m_gpuInfo;
    QTimer m_timer;

    bool m_paused = true;
    bool m_eof = false;
    double m_position = 0.0;
    double m_duration = 0.0;
    int m_volume = 100;
    bool m_muted = false;
    int m_videoWidth = 0;
    int m_videoHeight = 0;

    // 渲染帧率统计
    qint64 m_frameCount = 0;
    double m_renderFps = 0.0;
    QElapsedTimer m_fpsTimer;
    QElapsedTimer m_eofSuppress;   // 刚点过重播后的短暂抑制窗口（见 poll）
};

class MpvRenderer : public QQuickFramebufferObject::Renderer
{
public:
    explicit MpvRenderer(MpvItem *item);
    ~MpvRenderer() override;

    void render() override;
    QOpenGLFramebufferObject *createFramebufferObject(const QSize &size) override;

private:
    bool ensureRenderContext();
    void reportGpuInfo();

    MpvItem *m_item = nullptr;
    QSharedPointer<MpvState> m_state;
};
