import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Workbench

// 中间素材网格（对应旧版 index.html 的 main.center）
//
// 关键约定：
//   1) 铺满整块的 MouseArea 必须最先声明（QML 后声明者在上层）
//   2) 文件一律走 Library.fileUrl() 转成合法 URL（UNC 共享盘不能直接拼 file:///）
//   3) 预览播放器放在「卡片内部」（Loader 按需实例化），随卡片一起滚动
//   4) 卡片用显式坐标（不用 anchors）才能做 hover 上浮动画
Item {
    id: pane

    property var videos: []
    property var selectedIds: []
    property int total: 0
    property string sortBy: ""
    property string sortDir: "desc"
    property bool loading: false
    // 是否还有没取回来的页（分页由 LibraryView 负责）
    property bool hasMore: false

    signal toggleSelect(int id)
    signal setSelectAll(bool on)
    signal selectDetail(int id)
    signal openVideo(var item)
    signal sortChanged(string by)
    signal sortDirToggled()
    signal requestDrag(var videoIds)
    signal loadMoreRequested()

    // 滚到接近底部（留 600px 提前量）就向上要下一页
    function maybeLoadMore() {
        if (!pane.hasMore || pane.videos.length === 0)
            return
        if (grid.contentHeight <= 0)
            return
        if (grid.contentY + grid.height >= grid.contentHeight - 600)
            pane.loadMoreRequested()
    }

    function isSelected(id) {
        return pane.selectedIds.indexOf(id) >= 0
    }
    function fmtTime(ms) {
        const s = Math.round((ms || 0) / 1000)
        if (!s)
            return "0:00"
        return Math.floor(s / 60) + ":" + String(s % 60).padStart(2, "0")
    }

    // 每列基准从 218 收到 190：卡片整体小一圈，同屏能看到更多素材
    readonly property int columns: Math.max(2, Math.floor(width / 190))

    // ---------------------------------------------------------------- 原地预览
    property int previewId: 0
    property string previewSource: ""

    function startPreview(item) {
        if (pane.previewId === item.id) {
            pane.stopPreview()
            return
        }
        pane.previewSource = Proxy.proxyExists(item.path)
                             ? Proxy.proxyPathFor(item.path) : item.path
        pane.previewId = item.id
    }
    function stopPreview() {
        pane.previewId = 0
        pane.previewSource = ""
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ------------------------------------------------------ 工具条
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 48
            color: "transparent"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 6
                anchors.rightMargin: 6
                spacing: 10

                // 全选
                Row {
                    spacing: 7

                    Rectangle {
                        id: checkAllBox
                        anchors.verticalCenter: parent.verticalCenter
                        implicitWidth: 18
                        implicitHeight: 18
                        radius: 6
                        color: checkedAll ? Theme.brand : "white"
                        border.width: 1.6
                        border.color: checkedAll ? Theme.brand
                                                 : (checkAllHover.containsMouse ? Theme.brand : "#cfd6e6")
                        scale: checkAllHover.pressed ? 0.9 : 1
                        Behavior on scale { NumberAnimation { duration: 110 } }
                        Behavior on color { ColorAnimation { duration: 130 } }
                        Behavior on border.color { ColorAnimation { duration: 130 } }

                        property bool checkedAll: pane.videos.length > 0
                                                  && pane.selectedIds.length === pane.videos.length

                        Text {
                            anchors.centerIn: parent
                            visible: parent.checkedAll
                            text: "✓"
                            color: "white"
                            font.pixelSize: 11
                        }
                        MouseArea {
                            id: checkAllHover
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: pane.setSelectAll(!parent.checkedAll)
                        }
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("全选")
                        color: Theme.sub
                        font.pixelSize: 12
                    }
                }

                Text {
                    Layout.fillWidth: true
                    leftPadding: 10
                    text: {
                        // 扫描几千个文件要跑一会儿，把当前阶段和进度显示出来，
                        // 否则用户会以为界面卡死了
                        if (Scanner.scanning)
                            return qsTr("扫描中 · ") + Scanner.phase + "  "
                                   + Scanner.done + " / " + Math.max(Scanner.total, Scanner.done)
                        if (pane.loading)
                            return qsTr("加载中…")
                        return pane.videos.length + qsTr(" 个视频 · 共 ") + pane.total
                    }
                    color: Scanner.scanning ? Theme.brand : Theme.soft
                    font.pixelSize: 12
                    elide: Text.ElideRight
                }

                // 排序下拉：自绘 Dropdown（Basic 风格的原生 ComboBox 弹层是块灰方块，很突兀）
                Dropdown {
                    Layout.preferredWidth: 132
                    prefix: qsTr("排序")
                    options: [qsTr("默认"), qsTr("调用次数"), qsTr("视频时长"), qsTr("上传时间"), qsTr("标签数量")]
                    currentIndex: {
                        const map = ["", "usage", "duration", "date", "tags"]
                        const i = map.indexOf(pane.sortBy)
                        return i >= 0 ? i : 0
                    }
                    onActivated: (index) => {
                        const map = ["", "usage", "duration", "date", "tags"]
                        pane.sortChanged(map[index])
                    }
                }

                Btn {
                    Layout.preferredWidth: 92
                    glyph: pane.sortDir === "asc" ? "↑" : "↓"
                    text: pane.sortDir === "asc" ? qsTr("升序") : qsTr("降序")
                    onClicked: pane.sortDirToggled()
                }
            }
        }

        // ------------------------------------------------------ 网格
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            // 网格底：一层内凹的浅灰面板，让白卡片浮起来
            Rectangle {
                anchors.fill: parent
                anchors.margins: 4
                radius: Theme.radius
                color: "#eaeef7"
                border.width: 1
                border.color: "#dfe5f2"
            }

            GridView {
                id: grid
                anchors.fill: parent
                anchors.topMargin: 12
                anchors.leftMargin: 14
                anchors.rightMargin: 18
                anchors.bottomMargin: 12
                clip: true
                cellWidth: (width - 4) / pane.columns
                cellHeight: cellWidth * 1.46
                boundsBehavior: Flickable.StopAtBounds
                model: pane.videos
                // 缓存视口外的卡片少一点：几百张卡片同时实例化（还要各自触发生成封面）
                // 也是卡顿的一大来源
                cacheBuffer: 400

                // 滚到接近底部就提前要下一页，滚动不会顿住
                onContentYChanged: pane.maybeLoadMore()
                onAtYEndChanged: pane.maybeLoadMore()

                // 新进入视口的卡片淡入 + 轻微上浮（消除"生硬跳出"的感觉）
                add: Transition {
                    NumberAnimation {
                        property: "opacity"
                        from: 0
                        to: 1
                        duration: 240
                        easing.type: Easing.OutCubic
                    }
                    NumberAnimation {
                        property: "scale"
                        from: 0.94
                        to: 1
                        duration: 240
                        easing.type: Easing.OutCubic
                    }
                }

                ScrollBar.vertical: ScrollBar {
                    id: vbar
                    policy: ScrollBar.AsNeeded
                    padding: 2
                    contentItem: Rectangle {
                        implicitWidth: 7
                        radius: 4
                        color: vbar.pressed ? Theme.brand : "#b9c2d6"
                        opacity: vbar.active ? 0.95 : 0
                        Behavior on opacity { NumberAnimation { duration: 260 } }
                    }
                    background: Item {}
                }

                delegate: Item {
                    id: cell
                    width: grid.cellWidth
                    height: grid.cellHeight

                    property var item: modelData
                    property bool sel: pane.isSelected(modelData.id)
                    readonly property bool hevc: (item.codec || "").toLowerCase().indexOf("hvc") === 0
                    property bool dragging: false
                    property var pressPos: Qt.point(0, 0)
                    readonly property bool hovered: cardHover.containsMouse && !cell.dragging
                    readonly property bool lifted: hovered || cell.sel

                    // 阴影（两层柔和的投影；hover 时加深、下移，形成"浮起"感）
                    Repeater {
                        model: 2
                        delegate: Rectangle {
                            x: card.x + 2
                            y: card.y + (cell.lifted ? 6 : 4) + index * 3
                            width: card.width - 4
                            height: card.height - 4
                            radius: card.radius + index
                            color: Qt.rgba(0.13, 0.19, 0.42, (cell.lifted ? 0.13 : 0.08) - index * 0.035)
                            z: -2 + index
                            Behavior on y { NumberAnimation { duration: 170; easing.type: Easing.OutCubic } }
                            Behavior on color { ColorAnimation { duration: 170 } }
                        }
                    }

                    Rectangle {
                        id: card
                        x: 7
                        y: (cell.lifted ? 4 : 7)
                        width: parent.width - 14
                        height: parent.height - 12
                        // 四角再放大一档（卡片变小后，小圆角会显得硬）
                        radius: 22
                        color: "white"
                        border.width: 1.6
                        border.color: cell.sel ? Theme.brand
                                               : (cell.hovered ? "#9fb0ff" : "#dbe2f2")
                        clip: true
                        scale: cardHover.pressed ? 0.985 : 1

                        Behavior on y { NumberAnimation { duration: 170; easing.type: Easing.OutCubic } }
                        Behavior on scale { NumberAnimation { duration: 130; easing.type: Easing.OutCubic } }
                        Behavior on border.color { ColorAnimation { duration: 160 } }

                        // ① 背景层：单击选中；双击播放；横向拖动 = 拖拽复制
                        MouseArea {
                            id: cardHover
                            anchors.fill: parent
                            hoverEnabled: true
                            acceptedButtons: Qt.LeftButton
                            cursorShape: Qt.PointingHandCursor
                            onClicked: pane.selectDetail(cell.item.id)
                            onDoubleClicked: pane.openVideo(cell.item)
                            onPressed: (m) => {
                                cell.pressPos = Qt.point(m.x, m.y)
                                cell.dragging = false
                            }
                            onPositionChanged: (m) => {
                                if (!pressed || cell.dragging)
                                    return
                                if (Math.abs(m.x - cell.pressPos.x) < 10
                                        && Math.abs(m.y - cell.pressPos.y) < 10)
                                    return
                                cell.dragging = true
                                const ids = (cell.sel && pane.selectedIds.length > 1)
                                            ? pane.selectedIds.slice() : [cell.item.id]
                                pane.requestDrag(ids)
                            }
                            onReleased: cell.dragging = false
                        }

                        // ② 内容层
                        ColumnLayout {
                            anchors.fill: parent
                            spacing: 0

                            // ---- 封面 ----
                            Rectangle {
                                id: thumb
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                color: "#e9e5dd"
                                clip: true

                                Image {
                                    id: cover
                                    anchors.fill: parent
                                    property string url: Library.thumbUrl(cell.item.id)
                                    source: url
                                    fillMode: Image.PreserveAspectCrop
                                    asynchronous: true
                                    cache: false
                                    visible: status === Image.Ready
                                    opacity: visible ? 1 : 0
                                    scale: visible ? 1 : 1.04
                                    Behavior on opacity { NumberAnimation { duration: 260; easing.type: Easing.OutCubic } }
                                    Behavior on scale { NumberAnimation { duration: 320; easing.type: Easing.OutCubic } }

                                    Connections {
                                        target: Library
                                        function onThumbReady(vid) {
                                            if (vid === cell.item.id)
                                                cover.url = Library.thumbUrlOf(cell.item.id)
                                        }
                                    }
                                }

                                Column {
                                    anchors.centerIn: parent
                                    spacing: 4
                                    visible: !cover.visible
                                    Text {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: "🎬"
                                        font.pixelSize: 22
                                        opacity: 0.25
                                    }
                                }

                                // 内嵌预览（随卡片滚动）
                                Loader {
                                    id: previewLoader
                                    anchors.fill: parent
                                    active: pane.previewId === cell.item.id
                                    visible: active

                                    sourceComponent: Component {
                                        MpvVideo {
                                            width: previewLoader.width
                                            height: previewLoader.height
                                            source: pane.previewSource
                                            muted: true
                                        }
                                    }
                                }

                                // 预览中：右上关闭
                                Rectangle {
                                    visible: pane.previewId === cell.item.id
                                    anchors.right: parent.right
                                    anchors.top: parent.top
                                    anchors.margins: 8
                                    implicitWidth: 24
                                    implicitHeight: 24
                                    radius: 8
                                    color: closePHover.containsMouse ? "#e04747" : Qt.rgba(0, 0, 0, 0.55)
                                    Behavior on color { ColorAnimation { duration: 140 } }

                                    Text {
                                        anchors.centerIn: parent
                                        text: "✕"
                                        color: "white"
                                        font.pixelSize: 12
                                    }
                                    MouseArea {
                                        id: closePHover
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: pane.stopPreview()
                                    }
                                }

                                // 左上：爆 / HEVC
                                Row {
                                    anchors.left: parent.left
                                    anchors.top: parent.top
                                    anchors.margins: 8
                                    spacing: 4

                                    Rectangle {
                                        visible: cell.item.usage_count > 0
                                        implicitWidth: 26
                                        implicitHeight: 20
                                        radius: 7
                                        color: "#e2910e"
                                        Text {
                                            anchors.centerIn: parent
                                            text: qsTr("爆")
                                            color: "white"
                                            font.pixelSize: 11
                                            font.bold: true
                                        }
                                    }
                                    Rectangle {
                                        visible: cell.hevc
                                        implicitWidth: 46
                                        implicitHeight: 20
                                        radius: 7
                                        color: Qt.rgba(100 / 255, 78 / 255, 1, 0.94)
                                        Text {
                                            anchors.centerIn: parent
                                            text: "HEVC"
                                            color: "white"
                                            font.pixelSize: 10
                                            font.bold: true
                                        }
                                    }
                                }

                                // 中央：播放键（hover 才明显，平时半透明）
                                Rectangle {
                                    anchors.centerIn: parent
                                    implicitWidth: 46
                                    implicitHeight: 46
                                    radius: 23
                                    color: "white"
                                    opacity: cell.hovered ? 0.97 : 0.62
                                    scale: cell.hovered ? 1.06 : 1
                                    visible: pane.previewId !== cell.item.id
                                    Behavior on opacity { NumberAnimation { duration: 180 } }
                                    Behavior on scale { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }

                                    Rectangle {
                                        anchors.fill: parent
                                        anchors.margins: -2
                                        radius: parent.radius + 2
                                        color: "transparent"
                                        border.width: 1
                                        border.color: Qt.rgba(0.13, 0.19, 0.42, 0.10)
                                        z: -1
                                    }

                                    Text {
                                        anchors.centerIn: parent
                                        anchors.horizontalCenterOffset: 2
                                        text: "▶"
                                        color: Theme.brandDark
                                        font.pixelSize: 16
                                    }
                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: pane.openVideo(cell.item)
                                    }
                                }

                                // 右下：时长
                                Rectangle {
                                    anchors.right: parent.right
                                    anchors.bottom: parent.bottom
                                    anchors.margins: 8
                                    implicitWidth: durText.implicitWidth + 14
                                    implicitHeight: 20
                                    radius: 7
                                    color: cell.item.duration_ms ? Qt.rgba(0, 0, 0, 0.66)
                                                                 : Qt.rgba(0, 0, 0, 0.38)

                                    Text {
                                        id: durText
                                        anchors.centerIn: parent
                                        text: pane.fmtTime(cell.item.duration_ms)
                                        color: "white"
                                        font.pixelSize: 11
                                    }
                                }

                                // 右上：多选
                                Rectangle {
                                    visible: pane.previewId !== cell.item.id
                                    anchors.right: parent.right
                                    anchors.top: parent.top
                                    anchors.margins: 8
                                    implicitWidth: 23
                                    implicitHeight: 23
                                    radius: 8
                                    color: cell.sel ? Theme.brand : Qt.rgba(1, 1, 1, 0.94)
                                    border.width: 1.6
                                    border.color: cell.sel ? Theme.brand
                                                           : (selHover.containsMouse ? Theme.brand : "#d5dcec")
                                    scale: selHover.pressed ? 0.9 : 1
                                    Behavior on scale { NumberAnimation { duration: 110 } }
                                    Behavior on color { ColorAnimation { duration: 130 } }
                                    Behavior on border.color { ColorAnimation { duration: 130 } }

                                    Text {
                                        anchors.centerIn: parent
                                        visible: cell.sel
                                        text: "✓"
                                        color: "white"
                                        font.pixelSize: 12
                                    }
                                    MouseArea {
                                        id: selHover
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: pane.toggleSelect(cell.item.id)
                                    }
                                }
                            }

                            // ---- 信息区 ----
                            // 高度从 96 收到 78，省下来的空间让给封面；字号也跟着收一档
                            Rectangle {
                                Layout.fillWidth: true
                                implicitHeight: 78
                                color: "white"

                                Rectangle {
                                    anchors.top: parent.top
                                    width: parent.width
                                    height: 1
                                    color: Theme.line
                                }

                                ColumnLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 11
                                    anchors.rightMargin: 11
                                    anchors.topMargin: 7
                                    anchors.bottomMargin: 7
                                    spacing: 4

                                    Text {
                                        Layout.fillWidth: true
                                        text: cell.item.name
                                        color: Theme.txt
                                        font.pixelSize: 12
                                        font.bold: true
                                        elide: Text.ElideMiddle
                                    }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 5

                                        // 「调用 N 次」胶囊
                                        Rectangle {
                                            implicitWidth: usageText.implicitWidth + 12
                                            implicitHeight: 18
                                            radius: 9
                                            color: (cell.item.usage_count || 0) > 0
                                                   ? Qt.rgba(91 / 255, 108 / 255, 1, 0.16) : Theme.panel2
                                            border.width: 1
                                            border.color: (cell.item.usage_count || 0) > 0
                                                          ? Qt.rgba(91 / 255, 108 / 255, 1, 0.34) : Theme.line

                                            Text {
                                                id: usageText
                                                anchors.centerIn: parent
                                                text: qsTr("调用 ") + (cell.item.usage_count || 0) + qsTr(" 次")
                                                color: (cell.item.usage_count || 0) > 0 ? Theme.brandDark : Theme.sub
                                                font.pixelSize: 10
                                            }
                                        }

                                        // 该素材当前的全部标签（胶囊样式；放不下就在右侧裁掉，不撑破卡片）
                                        Row {
                                            id: cardTags
                                            Layout.fillWidth: true
                                            Layout.preferredWidth: 0
                                            Layout.minimumWidth: 0
                                            clip: true
                                            spacing: 4

                                            Repeater {
                                                model: cell.item.tags || []
                                                delegate: Rectangle {
                                                    readonly property string cat: modelData.category
                                                    implicitWidth: cardTagText.implicitWidth + 12
                                                    implicitHeight: 18
                                                    radius: 9
                                                    color: cat === "shot" ? "#eef1ff"
                                                         : (cat === "product" ? "#fff5e6" : "#e9f8f0")
                                                    border.width: 1
                                                    border.color: cat === "shot" ? "#c9d2ff"
                                                                : (cat === "product" ? "#f5d9ac" : "#bfe7d2")

                                                    Text {
                                                        id: cardTagText
                                                        anchors.centerIn: parent
                                                        text: modelData.name
                                                        color: cat === "shot" ? "#4457d6"
                                                             : (cat === "product" ? "#b5711f" : "#28835a")
                                                        font.pixelSize: 10
                                                    }
                                                }
                                            }
                                        }
                                        Rectangle {
                                            implicitWidth: 50
                                            implicitHeight: 21
                                            radius: 8
                                            color: (pane.previewId === cell.item.id || pvHover.containsMouse)
                                                   ? Theme.brandSoft : "#f6f8fd"
                                            border.width: 1
                                            border.color: pvHover.containsMouse ? Theme.brand : "#e3e8f4"
                                            Behavior on color { ColorAnimation { duration: 130 } }
                                            Behavior on border.color { ColorAnimation { duration: 130 } }

                                            Text {
                                                anchors.centerIn: parent
                                                text: pane.previewId === cell.item.id ? qsTr("停止") : qsTr("预览")
                                                color: pvHover.containsMouse ? Theme.brandDark : Theme.sub
                                                font.pixelSize: 10
                                            }
                                            MouseArea {
                                                id: pvHover
                                                anchors.fill: parent
                                                hoverEnabled: true
                                                cursorShape: Qt.PointingHandCursor
                                                onClicked: pane.startPreview(cell.item)
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // 空态
            Column {
                anchors.centerIn: parent
                spacing: 10
                visible: !pane.loading && pane.videos.length === 0

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "🎞️"
                    font.pixelSize: 34
                    opacity: 0.35
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: qsTr("没有符合条件的视频")
                    color: Theme.sub
                    font.pixelSize: 13
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: qsTr("换个筛选条件，或点右上角 ⟳ 重新扫描素材库")
                    color: Theme.soft
                    font.pixelSize: 11
                }
            }
        }
    }
}
