#include "MpvItem.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QMetaObject>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions>
#include <QtGlobal>

#include <mpv/client.h>
#include <mpv/render_gl.h>

namespace {

// mpv 通过这个回调拿到它需要的 OpenGL 函数
void *mpvGetProcAddress(void *ctx, const char *name)
{
    Q_UNUSED(ctx)
    QOpenGLContext *gl = QOpenGLContext::currentContext();
    if (!gl)
        return nullptr;
    return reinterpret_cast<void *>(gl->getProcAddress(QByteArray(name)));
}

// mpv 有新帧要显示时调用（在 mpv 的 vo 线程，不是 GUI 线程）
//
// 这里必须走 MpvUpdateGate：MpvItem 可能在 GUI 线程被销毁（卡片「预览→停止」就是），
// 而这个回调仍会从 vo 线程打进来。裸指针版本会在这里跳去执行已释放对象的内存（0xC0000005）。
void mpvRenderUpdate(void *ctx)
{
    auto *gate = static_cast<MpvUpdateGate *>(ctx);
    if (!gate)
        return;

    QMutexLocker lock(&gate->mutex);
    MpvItem *item = gate->item;
    if (!item)
        return; // Item 已在析构，不再投递任何事件

    QMetaObject::invokeMethod(item, "requestFrame", Qt::QueuedConnection);
}

QString mpvStringProp(mpv_handle *h, const char *name)
{
    char *s = mpv_get_property_string(h, name);
    const QString out = s ? QString::fromUtf8(s) : QString();
    if (s)
        mpv_free(s);
    return out;
}

QString glString(QOpenGLFunctions *f, GLenum name)
{
    const auto *s = f->glGetString(name);
    return s ? QString::fromLatin1(reinterpret_cast<const char *>(s)) : QString();
}

} // namespace

MpvState::~MpvState()
{
    // 顺序很重要：先释放 render context，再销毁 mpv 句柄
    if (ctx) {
        mpv_render_context_free(ctx);
        ctx = nullptr;
    }
    if (mpv) {
        mpv_terminate_destroy(mpv);
        mpv = nullptr;
    }
}

// ---------------------------------------------------------------- MpvItem

MpvItem::MpvItem(QQuickItem *parent)
    : QQuickFramebufferObject(parent)
{
    m_state.reset(new MpvState);
    // 闸门从这里生效：mpv 的 vo 线程回调只会往这个 item 上投递
    m_state->gate.item = this;

    mpv_handle *h = mpv_create();
    if (!h) {
        m_error = QStringLiteral("mpv_create() 失败");
        return;
    }
    m_state->mpv = h;

    qInfo("[mpv] client api version: %lu", mpv_client_api_version());

    // 参数基本照搬旧版 mpv-player.js 的调优；关掉 mpv 自带 OSC 与键位，控制条由 Qt 自绘。
    // 这里不再需要 --wid：画面通过 render API 直接进 Qt 场景图。
    mpv_set_option_string(h, "config", "no");
    mpv_set_option_string(h, "terminal", "no");
    mpv_set_option_string(h, "msg-level", "all=warn");
    mpv_set_option_string(h, "vo", "libmpv");
    // auto-copy：自动挑硬解，但用「GPU 解码 + 帧拷回内存」的方式，
    // 不依赖 GL/D3D11 之间的纹理互操作，在 Qt 的 WGL 上下文里兼容性最好。
    // 可在播放中通过 hwdecMode 属性切换（d3d11va / nvdec / no ...）。
    mpv_set_option_string(h, "hwdec", m_hwdecMode.toUtf8().constData());
    mpv_set_option_string(h, "vd-lavc-threads", "0");
    // 软解时让解码器直接写进 mpv 的输出缓冲，省掉一次整帧内存拷贝（4K 一帧约 25MB）
    mpv_set_option_string(h, "vd-lavc-dr", "yes");
    // 4K 10-bit 一帧约 25MB，60fps 即 1.5GB/s 的 CPU→GPU 上传；
    // 用 PBO 异步上传，避免每帧都阻塞 CPU 等纹理拷完（卡顿的主要来源之一）。
    mpv_set_option_string(h, "opengl-pbo", "yes");
    mpv_set_option_string(h, "cache", "yes");
    // 素材在网络共享（SMB）上，读取带宽有限：预读要留够（60 秒 / 512MB 上限），
    // 否则播放中读取跟不上就会丢帧（日志里表现为 Audio/Video desynchronisation）。
    mpv_set_option_string(h, "cache-secs", "60");
    mpv_set_option_string(h, "demuxer-readahead-secs", "60");
    mpv_set_option_string(h, "demuxer-max-bytes", "512MiB");
    mpv_set_option_string(h, "stream-buffer-size", "4MiB");
    mpv_set_option_string(h, "demuxer-thread", "yes");
    mpv_set_option_string(h, "force-seekable", "yes");
    mpv_set_option_string(h, "keep-open", "yes");
    mpv_set_option_string(h, "idle", "yes");
    mpv_set_option_string(h, "scale", "bilinear");
    // 视频 letterbox（画面之外的填充）颜色：跟播放器面板同一个色，看起来才是一体，
    // 而不是嵌在卡片里的一块黑框。
    //
    // 必须写成 background-color：`background` 的值域是 auto/none/color/tiles，用来选
    // 「背景怎么画」，把颜色塞进它（"color=#xxxxxx"）会被 mpv 判为非法值、静默失败，
    // render API 于是仍按默认的纯黑清屏 —— 那正是画面两侧那块「黑框」的来源。
    if (mpv_set_option_string(h, "background", "color") < 0)
        qWarning("[mpv] background 选项设置失败");
    if (mpv_set_option_string(h, "background-color", "#161a22") < 0)
        qWarning("[mpv] background-color 设置失败，letterbox 会退回纯黑");
    mpv_set_option_string(h, "osc", "no");
    mpv_set_option_string(h, "osd-level", "0");
    mpv_set_option_string(h, "input-default-bindings", "no");
    mpv_set_option_string(h, "input-vo-keyboard", "no");
    mpv_set_option_string(h, "audio-client-name", "workbench_qt");

    const int r = mpv_initialize(h);
    if (r < 0) {
        m_error = QStringLiteral("mpv_initialize 失败：%1")
                      .arg(QString::fromUtf8(mpv_error_string(r)));
        return;
    }

    // 开发期把 mpv 的 info 日志吐到控制台，便于确认硬解是否真的生效
    mpv_request_log_messages(h, "info");

    m_fpsTimer.start();
    m_timer.setInterval(120);
    connect(&m_timer, &QTimer::timeout, this, &MpvItem::poll);
    m_timer.start();
}

MpvItem::~MpvItem()
{
    // 第一件事就摘掉回调闸门：此刻 render context 往往还活着（MpvRenderer 仍持有 MpvState，
    // 要等 Qt 在渲染线程里删掉 Renderer 才会 free），mpv 的 vo 线程随时可能回调进来。
    // 持锁置空后，回调只会拿到 nullptr 并直接返回，绝不会再碰正在析构的 this。
    {
        QMutexLocker lock(&m_state->gate.mutex);
        m_state->gate.item = nullptr;
    }

    m_timer.stop();
    // m_state 由本对象与 Renderer 共享，最后一个持有者负责销毁
}

QQuickFramebufferObject::Renderer *MpvItem::createRenderer() const
{
    return new MpvRenderer(const_cast<MpvItem *>(this));
}

void MpvItem::requestFrame()
{
    // mpv 只在「有新帧」时才调这里，所以这个计数就是实际渲染帧数
    ++m_frameCount;
    update();
}

void MpvItem::setHwdecMode(const QString &mode)
{
    const QString m = mode.trimmed().isEmpty() ? QStringLiteral("auto-copy") : mode.trimmed();
    if (m_hwdecMode == m)
        return;
    m_hwdecMode = m;
    emit hwdecModeChanged();

    if (!m_state->mpv)
        return;

    const QByteArray v = m.toUtf8();
    const int rc = mpv_set_property_string(m_state->mpv, "hwdec", v.constData());
    if (rc < 0)
        qWarning("[mpv] 切换 hwdec=%s 失败: %s", v.constData(), mpv_error_string(rc));
    else
        qInfo("[mpv] hwdec 切换为 %s", v.constData());

    updateDecoderInfo();
}

void MpvItem::setGpuInfo(const QString &info)
{
    if (m_gpuInfo == info)
        return;
    m_gpuInfo = info;
    qInfo("[mpv] GPU: %s", qPrintable(info));
    emit gpuInfoChanged();
}

void MpvItem::setSource(const QString &path)
{
    // 统一成原生分隔符（QML 侧可能给出 / 分隔的路径），并去掉首尾空白
    const QString source = QDir::toNativeSeparators(path.trimmed());
    if (m_source == source)
        return;
    m_source = source;
    emit sourceChanged();

    // 换片时先清掉分辨率信息，避免用上一个素材的尺寸做判断
    if (m_videoWidth != 0 || m_videoHeight != 0) {
        m_videoWidth = 0;
        m_videoHeight = 0;
        emit videoSizeChanged();
    }

    if (!m_state->mpv)
        return;

    if (source.isEmpty()) {
        const char *cmd[] = { "stop", nullptr };
        mpv_command(m_state->mpv, cmd);
        m_position = 0.0;
        m_duration = 0.0;
        emit positionChanged();
        emit durationChanged();
        return;
    }

    m_eof = false;
    m_position = 0.0;
    m_duration = 0.0;
    emit positionChanged();
    emit durationChanged();

    // 自己先检查一次：路径不对时给出比 mpv 的 "loading failed" 更有用的提示
    const QFileInfo fi(source);
    if (!fi.exists() || !fi.isFile()) {
        m_error = QStringLiteral("文件不存在：") + source;
        qWarning("[mpv] 文件不存在: %s", qPrintable(source));
        emit errorTextChanged();
        return;
    }

    qInfo("[mpv] loadfile: %s", qPrintable(source));

    const QByteArray file = source.toUtf8();
    const char *cmd[] = { "loadfile", file.constData(), nullptr };
    const int rc = mpv_command(m_state->mpv, cmd);
    if (rc < 0) {
        m_error = QStringLiteral("loadfile 调用失败：%1")
                      .arg(QString::fromUtf8(mpv_error_string(rc)));
        emit errorTextChanged();
        return;
    }
    setPaused(false);
}

void MpvItem::setPaused(bool paused)
{
    if (m_paused == paused)
        return;
    m_paused = paused;
    emit pausedChanged();

    if (m_state->mpv) {
        int flag = paused ? 1 : 0;
        mpv_set_property(m_state->mpv, "pause", MPV_FORMAT_FLAG, &flag);
    }
}

void MpvItem::setVolume(int volume)
{
    volume = qBound(0, volume, 200);
    if (m_volume == volume)
        return;
    m_volume = volume;
    emit volumeChanged();

    if (m_state->mpv) {
        double v = volume;
        mpv_set_property(m_state->mpv, "volume", MPV_FORMAT_DOUBLE, &v);
    }
}

void MpvItem::setMuted(bool muted)
{
    if (m_muted == muted)
        return;
    m_muted = muted;
    emit mutedChanged();

    if (m_state->mpv) {
        int flag = muted ? 1 : 0;
        mpv_set_property(m_state->mpv, "mute", MPV_FORMAT_FLAG, &flag);
    }
}

void MpvItem::togglePause()
{
    setPaused(!m_paused);
}

// 从头重播。keep-open 下播完后 mpv 自己会把 pause 打开并停在最后一帧，
// 所以这里必须**直接给 mpv 发命令**：只走 setPaused(false) 会在
// 「我们的 m_paused 恰好已经是 false」时被 early-return 短路，命令根本发不出去
// （那正是「点了重播没反应、要按好几次才播」的原因）。
void MpvItem::replay()
{
    if (!m_state->mpv)
        return;

    const char *cmd[] = { "seek", "0", "absolute+exact", nullptr };
    mpv_command(m_state->mpv, cmd);
    mpv_set_property_string(m_state->mpv, "pause", "no");

    m_eofSuppress.restart();   // 这 600ms 内不采信 eof-reached，免得按钮立刻弹回「重播」
    m_paused = false;
    emit pausedChanged();
    setEof(false);
    m_position = 0.0;
    emit positionChanged();
    update();
}

// 只在真正变化时发信号，避免 QML 侧的按钮状态来回抖
void MpvItem::setEof(bool eof)
{
    if (m_eof == eof)
        return;
    m_eof = eof;
    emit eofChanged();
}

void MpvItem::seekSeconds(double seconds)
{
    if (!m_state->mpv)
        return;
    const QByteArray v = QByteArray::number(qMax(0.0, seconds), 'f', 3);
    const char *cmd[] = { "seek", v.constData(), "absolute", nullptr };
    mpv_command(m_state->mpv, cmd);

    m_position = qMax(0.0, seconds);
    emit positionChanged();
    update();
}

void MpvItem::seekRelative(double seconds)
{
    if (!m_state->mpv)
        return;
    const QByteArray v = QByteArray::number(seconds, 'f', 3);
    const char *cmd[] = { "seek", v.constData(), "relative", nullptr };
    mpv_command(m_state->mpv, cmd);
    update();
}

bool MpvItem::readDouble(const char *name, double *out) const
{
    if (!m_state->mpv)
        return false;
    double v = 0.0;
    if (mpv_get_property(m_state->mpv, name, MPV_FORMAT_DOUBLE, &v) < 0)
        return false;
    if (out)
        *out = v;
    return true;
}

void MpvItem::updateDecoderInfo()
{
    if (!m_state->mpv)
        return;

    const QString codec = mpvStringProp(m_state->mpv, "video-codec");
    const QString hw = mpvStringProp(m_state->mpv, "hwdec-current");

    if (hw != m_hwdecCurrent) {
        m_hwdecCurrent = hw;
        emit hwdecCurrentChanged();
    }

    QString info;
    if (!codec.isEmpty()) {
        info = codec;
        if (!hw.isEmpty() && hw != QLatin1String("no")) {
            info += QStringLiteral(" · 硬解(") + hw + QLatin1Char(')');
        } else {
            info += QStringLiteral(" · 软解(请求=") + m_hwdecMode + QLatin1Char(')');
        }
    }

    // 追加流缓存状态：读取速度（MB/s）与已缓冲秒数。
    // 用来区分「共享盘读不动」和「解码/显示跟不上」：前者看读取速度，后者看帧率。
    double speed = 0.0;
    double cached = 0.0;
    const bool hasSpeed = readDouble("cache-speed", &speed);
    const bool hasCached = readDouble("demuxer-cache-duration", &cached);
    if (hasSpeed || hasCached) {
        QString tail;
        if (hasSpeed)
            tail += QStringLiteral("读 %1MB/s").arg(speed / 1048576.0, 0, 'f', 1);
        if (hasCached) {
            if (!tail.isEmpty())
                tail += QStringLiteral(" / ");
            tail += QStringLiteral("缓 %1s").arg(cached, 0, 'f', 0);
        }
        if (!info.isEmpty())
            info += QStringLiteral(" · ");
        info += tail;
    }

    if (info != m_decoderInfo) {
        m_decoderInfo = info;
        emit decoderInfoChanged();
    }
}

void MpvItem::updateVideoSize()
{
    if (!m_state->mpv)
        return;

    double w = 0.0;
    double h = 0.0;
    if (!readDouble("video-params/w", &w) || !readDouble("video-params/h", &h))
        return;

    const int iw = int(w + 0.5);
    const int ih = int(h + 0.5);
    if (iw == m_videoWidth && ih == m_videoHeight)
        return;

    m_videoWidth = iw;
    m_videoHeight = ih;
    emit videoSizeChanged();
}

void MpvItem::poll()
{
    handleMpvEvents();

    if (!m_state->mpv)
        return;

    // 把 mpv 的真实 pause 状态同步回来：keep-open 播完时 mpv 会自己把 pause 打开，
    // 如果只维护我们自己的 m_paused，界面图标就会和真实播放状态脱节
    // （表现就是「点重播像是没生效，要按好几次才播」）。
    int pauseFlag = 0;
    if (mpv_get_property(m_state->mpv, "pause", MPV_FORMAT_FLAG, &pauseFlag) >= 0) {
        const bool p = pauseFlag != 0;
        if (p != m_paused) {
            m_paused = p;
            emit pausedChanged();
        }
    }

    // 播放结束状态以 mpv 的 eof-reached 为准（keep-open 下 pause 与 eof 是两回事）。
    // 刚点过重播的几百毫秒里 mpv 还没清掉 eof-reached，这里先不采信，避免按钮立刻弹回「重播」。
    if (!m_eofSuppress.isValid() || m_eofSuppress.elapsed() > 600) {
        int eofFlag = 0;
        if (mpv_get_property(m_state->mpv, "eof-reached", MPV_FORMAT_FLAG, &eofFlag) >= 0)
            setEof(eofFlag != 0);
    }

    double d = 0.0;
    if (readDouble("duration", &d) && qAbs(d - m_duration) > 0.01) {
        m_duration = d;
        emit durationChanged();
    }

    double p = 0.0;
    if (readDouble("time-pos", &p) && qAbs(p - m_position) > 0.005) {
        m_position = p;
        emit positionChanged();
    }

    updateDecoderInfo();
    updateVideoSize();

    // 每满 1 秒结算一次「实际渲染帧率」
    const qint64 elapsed = m_fpsTimer.elapsed();
    if (elapsed >= 1000) {
        const double fps = m_frameCount * 1000.0 / double(elapsed);
        m_frameCount = 0;
        m_fpsTimer.restart();
        if (qAbs(fps - m_renderFps) > 0.5) {
            m_renderFps = fps;
            emit renderFpsChanged();
        }
    }
}

void MpvItem::handleMpvEvents()
{
    if (!m_state->mpv)
        return;

    for (;;) {
        mpv_event *ev = mpv_wait_event(m_state->mpv, 0);
        if (!ev || ev->event_id == MPV_EVENT_NONE)
            break;

        switch (ev->event_id) {
        case MPV_EVENT_LOG_MESSAGE: {
            const auto *lm = static_cast<mpv_event_log_message *>(ev->data);
            if (lm) {
                QString text = QString::fromUtf8(lm->text ? lm->text : "");
                while (text.endsWith(QLatin1Char('\n')))
                    text.chop(1);
                qInfo("[mpv:%s] %s", lm->prefix ? lm->prefix : "?", qPrintable(text));
            }
            break;
        }

        case MPV_EVENT_FILE_LOADED:
            setEof(false);
            if (!m_error.isEmpty()) {
                m_error.clear();
                emit errorTextChanged();
            }
            updateDecoderInfo();
            updateVideoSize();
            update();
            break;

        case MPV_EVENT_END_FILE: {
            const auto *ef = static_cast<mpv_event_end_file *>(ev->data);
            if (ef && ef->reason == MPV_END_FILE_REASON_ERROR) {
                const QString msg = QString::fromUtf8(mpv_error_string(ef->error));
                m_error = QStringLiteral("播放失败：") + msg;
                qWarning("[mpv] end-file error: %s", qPrintable(msg));
                emit errorTextChanged();
            }
            // 只有「播到结尾（EOF）」才算播放结束；切歌 / stop 触发 end-file 不算，
            // 否则按钮会一直停在「重播」上。
            setEof(ef && ef->reason == MPV_END_FILE_REASON_EOF);
            break;
        }

        default:
            break;
        }
    }
}

// ---------------------------------------------------------------- Renderer

MpvRenderer::MpvRenderer(MpvItem *item)
    : m_item(item)
    , m_state(item ? item->state() : QSharedPointer<MpvState>())
{
}

MpvRenderer::~MpvRenderer()
{
    // 在渲染线程释放；此后 m_state 只剩 Item 持有，由其负责 terminate_destroy
    if (m_state && m_state->ctx) {
        mpv_render_context_free(m_state->ctx);
        m_state->ctx = nullptr;
    }
}

QOpenGLFramebufferObject *MpvRenderer::createFramebufferObject(const QSize &size)
{
    // 视频渲染用不到深度/模板附件，省一层显存与带宽
    return new QOpenGLFramebufferObject(size);
}

void MpvRenderer::reportGpuInfo()
{
    QOpenGLContext *glctx = QOpenGLContext::currentContext();
    if (!glctx || !m_item)
        return;

    QOpenGLFunctions *f = glctx->functions();
    const QString info = QStringLiteral("%1 | %2 | GL %3")
                             .arg(glString(f, GL_VENDOR),
                                  glString(f, GL_RENDERER),
                                  glString(f, GL_VERSION));

    if (!m_state)
        return;

    // m_item 是裸指针：若 item 已在析构（Qt 在渲染线程删掉 Renderer 之前的那段窗口），
    // 同样不该往它身上投事件，统一走闸门。事件以 item 为 context object 投递，
    // 万一它在事件执行前析构，Qt 会连同事件一起丢弃。
    QMutexLocker lock(&m_state->gate.mutex);
    MpvItem *item = m_state->gate.item;
    if (!item)
        return;

    QMetaObject::invokeMethod(
        item, [item, info]() { item->setGpuInfo(info); }, Qt::QueuedConnection);
}

bool MpvRenderer::ensureRenderContext()
{
    if (!m_state)
        return false;
    if (m_state->ctx)
        return true;
    if (!m_state->mpv)
        return false;

    mpv_opengl_init_params glParams;
    glParams.get_proc_address = &mpvGetProcAddress;
    glParams.get_proc_address_ctx = nullptr;

    int advanced = 0;
    mpv_render_param params[] = {
        { MPV_RENDER_PARAM_API_TYPE, const_cast<char *>(MPV_RENDER_API_TYPE_OPENGL) },
        { MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &glParams },
        { MPV_RENDER_PARAM_ADVANCED_CONTROL, &advanced },
        { MPV_RENDER_PARAM_INVALID, nullptr }
    };

    mpv_render_context *ctx = nullptr;
    const int r = mpv_render_context_create(&ctx, m_state->mpv, params);
    if (r < 0) {
        qWarning("[mpv] mpv_render_context_create 失败: %s", mpv_error_string(r));
        return false;
    }

    // 回调参数用闸门而不是裸 MpvItem*：vo 线程回调与 GUI 线程析构之间靠它同步
    mpv_render_context_set_update_callback(ctx, &mpvRenderUpdate, &m_state->gate);
    m_state->ctx = ctx;

    reportGpuInfo();
    return true;
}

void MpvRenderer::render()
{
    if (!ensureRenderContext())
        return;

    QOpenGLFramebufferObject *fbo = framebufferObject();
    if (!fbo)
        return;

    {
        // 先把整块 FBO 刷成和面板同一个深色再交给 mpv：mpv 只覆盖它自己的视频区域，
        // FBO 其余部分若不清屏就会残留上一帧内容，表现为「黑框边缘隐隐约约多出一块」。
        QOpenGLContext *gl = QOpenGLContext::currentContext();
        if (gl) {
            QOpenGLFunctions *f = gl->functions();
            f->glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(fbo->handle()));
            f->glViewport(0, 0, fbo->width(), fbo->height());
            f->glDisable(GL_SCISSOR_TEST);
            // 与 PlayerOverlay 的视频区/mpv 的 background-color 用同一个色（#161a22），
            // 这样视频区看起来就是播放器面板的一部分，而不是嵌进去的黑框
            f->glClearColor(22 / 255.f, 26 / 255.f, 34 / 255.f, 1.f);
            f->glClear(GL_COLOR_BUFFER_BIT);
        }
    }

    mpv_opengl_fbo mpvFbo;
    mpvFbo.fbo = static_cast<int>(fbo->handle());
    mpvFbo.w = fbo->width();
    mpvFbo.h = fbo->height();
    mpvFbo.internal_format = 0;

    // Qt 的 FBO 纹理已经是"上为原点"的朝向，让 mpv 不要再翻一次，否则画面上下颠倒
    int flipY = 0;
    mpv_render_param params[] = {
        { MPV_RENDER_PARAM_OPENGL_FBO, &mpvFbo },
        { MPV_RENDER_PARAM_FLIP_Y, &flipY },
        { MPV_RENDER_PARAM_INVALID, nullptr }
    };
    mpv_render_context_render(m_state->ctx, params);

    // Qt 6 移除了 QQuickWindow::resetOpenGLState()，这里手动把 mpv 改动过的
    // GL 状态恢复成 Qt 场景图期望的样子（否则叠加控件/背景会错乱）。
    QOpenGLContext *glctx = QOpenGLContext::currentContext();
    if (glctx) {
        QOpenGLFunctions *f = glctx->functions();
        f->glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(fbo->handle()));
        f->glViewport(0, 0, fbo->width(), fbo->height());
        f->glDisable(GL_SCISSOR_TEST);
        f->glDisable(GL_BLEND);
        f->glActiveTexture(GL_TEXTURE0);
    }
}
