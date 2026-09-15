pragma Singleton
import QtQuick

// 全局视觉令牌：颜色 / 圆角 / 动效时长 / 尺寸。
//
// 所有面板与控件都只从这里取值，风格才不会各写各的（「整体协调」的前提）。
// 图标统一用文字符号（项目不引图标字体），字号/字重也在这里定档。
QtObject {
    // ---------------------------------------------------------------- 底色
    readonly property color bg: "#eef1f7"
    readonly property color bg2: "#e7ecf6"
    readonly property color bgFrom: "#e9efff"
    readonly property color bgMid: "#eef1f7"
    readonly property color bgTo: "#f4edff"

    // ---------------------------------------------------------------- 面板
    readonly property color panel: "#ffffff"
    readonly property color panel2: "#f6f8fd"   // 次级面：输入框底、hover
    readonly property color panel3: "#edf1f9"   // 更深一级：分段控件底、按下态
    readonly property color inset: "#e9edf7"    // 内凹面：网格底

    // ---------------------------------------------------------------- 顶栏
    // 比整窗底色深一档的淡蓝渐变，把「标题区」和内容区分开
    readonly property color topbarFrom: "#dbe6ff"
    readonly property color topbarMid: "#e1e7ff"
    readonly property color topbarTo: "#e9e0ff"

    // ---------------------------------------------------------------- 线条
    readonly property color line: "#e7ebf5"
    readonly property color line2: "#d7dfee"

    // ---------------------------------------------------------------- 文本
    readonly property color txt: "#161a22"
    readonly property color sub: "#5b6472"
    readonly property color soft: "#98a1b3"

    // ---------------------------------------------------------------- 品牌
    readonly property color brand: "#5b6cff"
    readonly property color brand2: "#8a5bff"
    readonly property color brandDark: "#4353e6"
    readonly property color brandSoft: "#eef0ff"

    // ---------------------------------------------------------------- 语义色
    readonly property color green: "#2f9e6b"
    readonly property color greenSoft: "#e7f7f0"
    readonly property color amber: "#e2910e"
    readonly property color amberSoft: "#fff4e0"
    readonly property color danger: "#e04747"
    readonly property color dangerSoft: "#ffeceb"
    readonly property color dangerLine: "#f1c3c1"

    // ---------------------------------------------------------------- 投影
    // 浅色主题下用很淡的蓝灰叠加做「伪投影」（不引入 GraphicalEffects 依赖）
    readonly property color shadow: Qt.rgba(0.13, 0.19, 0.42, 0.10)
    readonly property color shadowSoft: Qt.rgba(0.13, 0.19, 0.42, 0.06)

    // ---------------------------------------------------------------- 圆角
    readonly property int radius: 14
    readonly property int radiusSm: 9
    readonly property int radiusCard: 16
    readonly property int radiusWin: 12

    // ---------------------------------------------------------------- 动效
    readonly property int durFast: 120
    readonly property int durBase: 180
    readonly property int durSlow: 280

    // ---------------------------------------------------------------- 尺寸
    readonly property int leftWidth: 216
    readonly property int rightWidth: 288
    // 顶栏只放「品牌 + 状态 + 窗口按钮」，44 足够；之前 58 太占高度
    readonly property int topbarHeight: 44

    readonly property string fontFamily: "Microsoft YaHei UI"
}
