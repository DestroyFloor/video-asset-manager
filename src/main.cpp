#include <QCoreApplication>
#include <QDebug>
#include <QFont>
#include <QGuiApplication>
#include <QPainterPath>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QRegion>
#include <QSGRendererInterface>

#include "Config.h"

namespace {

// 与 Theme.radiusWin 保持一致
constexpr int kWindowRadius = 12;

// 无边框窗口的圆角兜底。
//
// QML 侧已经把窗口设成透明、并由 shell 自己画圆角，但「透明窗口」在部分机器/驱动上
// 拿不到（窗口本体仍按矩形合成，四角就露出直角边缘 —— 截图里左上/右上角是直角就是这种情况）。
// 这里对窗口区域再做一次裁切：透明生效时它不影响观感，透明没生效时它保证四角是圆的。
// 最大化/全屏时清除裁切，让窗口铺满整屏。
void applyRoundedMask(QQuickWindow *win)
{
    if (!win)
        return;

    const bool full = win->visibility() == QWindow::Maximized
                      || win->visibility() == QWindow::FullScreen;
    if (full || win->width() <= 0 || win->height() <= 0) {
        win->setMask(QRegion());
        return;
    }

    QPainterPath path;
    path.addRoundedRect(QRectF(0, 0, win->width(), win->height()), kWindowRadius, kWindowRadius);
    win->setMask(QRegion(path.toFillPolygon().toPolygon()));
}

} // namespace

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("定制视频分类工作台"));
    app.setApplicationVersion(QStringLiteral("0.1.0-qt"));
    app.setOrganizationName(QStringLiteral("ClipsWorkbench"));

    // libmpv 的 render API 走 OpenGL；Qt 6 在 Windows 上默认用 D3D11，
    // 这里统一为 OpenGL，MpvItem 才能把 mpv 画面直接画进 Qt 的场景图。
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);

    // Controls 用 Basic 风格，界面样式全部由 QML 自绘（对齐旧版毛玻璃风格）
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    QFont font(QStringLiteral("Microsoft YaHei UI"));
    font.setPixelSize(14);
    app.setFont(font);

    // --- 启动自检：确认 Config 层可用（目录会被创建） ---
    qInfo() << "[wb] dataDir   =" << Config::instance()->dataDir();
    qInfo() << "[wb] configPath=" << Config::instance()->configPath();
    qInfo() << "[wb] shareRoot =" << Config::instance()->shareRoot();
    qInfo() << "[wb] account   =" << Config::instance()->account();

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        []() { QCoreApplication::exit(-1); }, Qt::QueuedConnection);

    engine.loadFromModule("Workbench", "Main");
    qInfo() << "[wb] rootObjects =" << engine.rootObjects().size();

    if (engine.rootObjects().isEmpty())
        return -1;

    // 窗口尺寸或显示状态一变就重算圆角区域（还原成窗口 → 圆角；最大化 → 直角）
    if (auto *win = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst())) {
        auto sync = [win]() { applyRoundedMask(win); };
        QObject::connect(win, &QWindow::widthChanged, win, [sync](int) { sync(); });
        QObject::connect(win, &QWindow::heightChanged, win, [sync](int) { sync(); });
        QObject::connect(win, &QWindow::visibilityChanged, win,
                         [sync](QWindow::Visibility) { sync(); });
        sync();
    }

    return app.exec();
}
