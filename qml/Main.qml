import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Layouts
import Workbench

ApplicationWindow {
    id: root

    width: 1360
    height: 900
    minimumWidth: 1080
    minimumHeight: 680
    visible: true
    title: qsTr("定制视频分类工作台")
    // 无边框 + 透明底：圆角之外应当是真正透明的
    color: "transparent"
    flags: Qt.Window | Qt.FramelessWindowHint
    font.family: Theme.fontFamily

    // 最大化或全屏（播放器「视频全屏」）时：圆角与描边都要收掉，窗口铺满整屏
    readonly property bool maxed: visibility === Window.Maximized
                                  || visibility === Window.FullScreen
    // 当前显示器可用区（已扣掉任务栏），贴边判定都用它。
    // 注意：Window.screen 是 QQuickScreenInfo，它没有 availableGeometry，
    // 要用 virtualX/virtualY + desktopAvailableWidth/Height 拼出来。
    readonly property rect avail: screen
                                  ? Qt.rect(screen.virtualX, screen.virtualY,
                                            screen.desktopAvailableWidth, screen.desktopAvailableHeight)
                                  : Qt.rect(0, 0, 1920, 1080)

    // ---------------------------------------------------------------- 窗口圆角
    // 圆角由 shell 自己画（窗口是透明的）；C++ 侧另外对窗口区域做了一次裁切兜底
    // （见 main.cpp 的 applyRoundedMask），所以这里不需要再做什么。

    // ---------------------------------------------------------------- 拖动 / 贴边
    // 不再用 startSystemMove()：系统拖动不会给无边框窗口做贴边处理，
    // 这正是「拖到屏幕顶部不会自动最大化」的原因，只能自己实现。
    property int snapMode: 0          // 0 无 / 1 顶部（最大化）/ 2 左半屏 / 3 右半屏
    property bool dragging: false
    property bool maxedDrag: false    // 从最大化状态开始拖（松手前要先还原）
    property point pressGlobal: Qt.point(0, 0)
    property point winAtPress: Qt.point(0, 0)

    function beginDrag(gx, gy) {
        dragging = true
        pressGlobal = Qt.point(gx, gy)
        winAtPress = Qt.point(root.x, root.y)
        maxedDrag = root.visibility === Window.Maximized
    }

    function updateDrag(gx, gy) {
        if (!dragging)
            return

        if (maxedDrag) {
            // 最大化状态下开始拖 = 先还原成窗口，并让窗口按原比例落在鼠标下（和 Windows 一致）
            maxedDrag = false
            const ratio = Math.max(0.08, Math.min(0.92,
                                               (pressGlobal.x - winAtPress.x) / Math.max(1, root.width)))
            root.showNormal()
            root.x = Math.round(gx - root.width * ratio)
            root.y = Math.round(Math.max(root.avail.y, gy - Theme.topbarHeight / 2))
            winAtPress = Qt.point(root.x, root.y)
            pressGlobal = Qt.point(gx, gy)
        }

        root.x = winAtPress.x + (gx - pressGlobal.x)
        root.y = winAtPress.y + (gy - pressGlobal.y)

        const g = root.avail
        if (gy <= g.y + 2)
            snapMode = 1
        else if (gx <= g.x + 2)
            snapMode = 2
        else if (gx >= g.x + g.width - 2)
            snapMode = 3
        else
            snapMode = 0
    }

    function endDrag() {
        if (!dragging)
            return
        dragging = false
        const mode = snapMode
        snapMode = 0
        if (mode === 0)
            return

        const g = root.avail
        if (mode === 1) {
            root.showMaximized()
        } else {
            root.showNormal()
            root.width = Math.round(g.width / 2)
            root.height = g.height
            root.x = (mode === 2) ? g.x : (g.x + g.width - root.width)
            root.y = g.y
        }
    }

    Component.onCompleted: {
        // 首次打开居中
        const g = root.avail
        root.x = Math.round(g.x + (g.width - root.width) / 2)
        root.y = Math.round(g.y + (g.height - root.height) / 2)
        App.tryAutoLogin()
    }

    // ---------------------------------------------------------------- 便捷组件
    component WinCtrlButton: Rectangle {
        id: ctrl
        property string glyph
        property bool danger: false
        signal activated

        width: 32
        height: 28
        radius: 8
        color: ma.containsMouse ? (ctrl.danger ? Theme.danger : Qt.rgba(1, 1, 1, 0.55)) : "transparent"

        Behavior on color { ColorAnimation { duration: Theme.durFast } }

        Text {
            anchors.centerIn: parent
            text: ctrl.glyph
            font.pixelSize: 12
            color: ctrl.danger ? (ma.containsMouse ? "white" : Theme.sub) : Theme.txt
        }
        MouseArea {
            id: ma
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: ctrl.activated()
        }
    }

    // ---------------------------------------------------------------- 外壳
    // 圆角 + 渐变都在这一层，窗口本身透明，四角才不会是直角
    Rectangle {
        id: shell
        anchors.fill: parent
        radius: root.maxed ? 0 : Theme.radiusWin
        clip: true
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: Theme.bgFrom }
            GradientStop { position: 0.5; color: Theme.bgMid }
            GradientStop { position: 1.0; color: Theme.bgTo }
        }
        border.width: root.maxed ? 0 : 1
        border.color: Qt.rgba(1, 1, 1, 0.75)

        // 点界面空白处收起输入焦点（比如侧栏「调用次数」那个输入框），
        // 否则点完别的地方光标还在闪，看着像还在编辑状态。
        // TapHandler 是被动的：有别的控件吃掉点击时它不会触发。
        TapHandler {
            onTapped: {
                if (root.activeFocusItem)
                    root.activeFocusItem.focus = false
            }
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            // ------------------------------------------------------------ 顶栏
            // 淡蓝渐变（比整窗底色更深一档，把「标题区」和内容区区分开）
            Rectangle {
                id: topbar
                Layout.fillWidth: true
                implicitHeight: Theme.topbarHeight
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0.0; color: Theme.topbarFrom }
                    GradientStop { position: 0.55; color: Theme.topbarMid }
                    GradientStop { position: 1.0; color: Theme.topbarTo }
                }

                // 拖动窗口（含贴边）。必须最先声明：QML 同层级后声明者在上层，
                // 否则这层会把右边的窗口按钮点击全部吃掉。
                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton
                    onPressed: (m) => {
                        const g = mapToGlobal(m.x, m.y)
                        root.beginDrag(g.x, g.y)
                    }
                    onPositionChanged: (m) => {
                        if (!pressed)
                            return
                        const g = mapToGlobal(m.x, m.y)
                        root.updateDrag(g.x, g.y)
                    }
                    onReleased: root.endDrag()
                    onDoubleClicked: root.maxed ? root.showNormal() : root.showMaximized()
                }

                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: 1
                    color: Qt.rgba(0.13, 0.19, 0.42, 0.07)
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 14
                    anchors.rightMargin: 8
                    spacing: 12

                    Row {
                        spacing: 9
                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            width: 26
                            height: 26
                            radius: 8
                            gradient: Gradient {
                                GradientStop { position: 0; color: Theme.brand }
                                GradientStop { position: 1; color: Theme.brand2 }
                            }
                            Text {
                                anchors.centerIn: parent
                                text: "▶"
                                color: "white"
                                font.pixelSize: 11
                            }
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("定制视频分类工作台")
                            color: Theme.txt
                            font.pixelSize: 14
                            font.bold: true
                        }
                    }

                    Item { Layout.fillWidth: true }

                    // 连接状态：一个小圆点 + 文案，比单独一行灰字更好认
                    Row {
                        spacing: 6
                        visible: App.statusMessage !== ""
                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            width: 6
                            height: 6
                            radius: 3
                            color: App.connected ? Theme.green : Theme.soft
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: App.statusMessage
                            color: Theme.sub
                            font.pixelSize: 11
                            elide: Text.ElideRight
                        }
                    }

                    Row {
                        spacing: 2
                        WinCtrlButton {
                            glyph: "─"
                            onActivated: root.showMinimized()
                        }
                        WinCtrlButton {
                            glyph: root.maxed ? "❐" : "▢"
                            onActivated: root.maxed ? root.showNormal() : root.showMaximized()
                        }
                        WinCtrlButton {
                            glyph: "✕"
                            danger: true
                            onActivated: root.close()
                        }
                    }
                }
            }

            // ------------------------------------------------------------ 主体
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                LoginView {
                    anchors.fill: parent
                    visible: !App.connected
                }
                LibraryView {
                    anchors.fill: parent
                    visible: App.connected
                }
            }
        }
    }

    // ---------------------------------------------------------------- 无边框缩放
    // 最大化时没有可拖的边框，直接关掉这层热区
    MouseArea {
        x: 0; y: 6
        width: 6; height: root.height - 12
        enabled: !root.maxed
        cursorShape: Qt.SizeHorCursor
        onPressed: root.startSystemResize(Qt.LeftEdge)
    }
    MouseArea {
        x: root.width - 6; y: 6
        width: 6; height: root.height - 12
        enabled: !root.maxed
        cursorShape: Qt.SizeHorCursor
        onPressed: root.startSystemResize(Qt.RightEdge)
    }
    MouseArea {
        x: 6; y: 0
        width: root.width - 12; height: 6
        enabled: !root.maxed
        cursorShape: Qt.SizeVerCursor
        onPressed: root.startSystemResize(Qt.TopEdge)
    }
    MouseArea {
        x: 6; y: root.height - 6
        width: root.width - 12; height: 6
        enabled: !root.maxed
        cursorShape: Qt.SizeVerCursor
        onPressed: root.startSystemResize(Qt.BottomEdge)
    }
    MouseArea {
        x: 0; y: 0
        width: 8; height: 8
        enabled: !root.maxed
        cursorShape: Qt.SizeFDiagCursor
        onPressed: root.startSystemResize(Qt.LeftEdge | Qt.TopEdge)
    }
    MouseArea {
        x: root.width - 16; y: root.height - 16
        width: 16; height: 16
        enabled: !root.maxed
        cursorShape: Qt.SizeFDiagCursor
        onPressed: root.startSystemResize(Qt.RightEdge | Qt.BottomEdge)
    }
}
