import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Workbench

// 右侧面板（对应旧版 index.html 的 aside.right）
//
// 上半：选中素材的详情（分辨率/时长/编码/计数/标签）
// 下半：文件夹树 —— 扁平树样式：行本身不是白卡片，默认透明，
//       hover / 选中 / 生效项才浮出底色；层级用细引导线 + 窄缩进表达，
//       子项不再被推得越来越靠右。
//
// 注意：铺满节点的 MouseArea（fldHover）必须最先声明，否则会吃掉
// 展开箭头 / 多选框 / 🗑 的点击。
Item {
    id: pane

    property var detail: ({})
    property var folders: []
    property var folderSel: []
    property var folderOpen: ({})
    property string activeFolder: ""
    property string folderSearch: ""

    signal removeTag(int tagId)
    signal addTagRequested()
    signal selectFolder(string path)
    signal toggleFolderOpen(string path)
    signal deleteFolder(string path)
    signal toggleFolderSel(string path)
    signal deleteSelectedFolders()
    signal dragFolderTo(string from, string to)
    signal notifyCopied(int count)   // 拖放复制完成（count < 0 表示失败）

    function fmtTime(ms) {
        const s = Math.round((ms || 0) / 1000)
        if (!s)
            return "0:00"
        return Math.floor(s / 60) + ":" + String(s % 60).padStart(2, "0")
    }
    function fmtSize(n) {
        n = Number(n || 0)
        if (n >= 1073741824)
            return (n / 1073741824).toFixed(1) + "GB"
        if (n >= 1048576)
            return (n / 1048576).toFixed(1) + "MB"
        if (n >= 1024)
            return (n / 1024).toFixed(0) + "KB"
        return n + "B"
    }
    function shortFolder(p) {
        const parts = String(p || "").split(/[\\/]/).filter(Boolean)
        return parts.length ? parts[parts.length - 1] : p
    }

    // 把扁平文件夹列表还原成树，并按展开状态压平成可见行（等价于旧版 renderFolders）
    readonly property var treeRows: {
        const list = pane.folders || []
        const term = (pane.folderSearch || "").trim().toLowerCase()

        if (term) {
            return list.filter(f => (f.name || "").toLowerCase().indexOf(term) >= 0)
                       .sort((a, b) => b.count - a.count)
                       .map(f => ({ f: f, depth: 0, hasKids: false, open: false }))
        }

        const byDir = {}
        for (const f of list)
            (byDir[f.dir] = byDir[f.dir] || []).push(f)
        const pathSet = new Set(list.map(f => f.path))
        const roots = list.filter(f => !pathSet.has(f.dir)).sort((a, b) => b.count - a.count)

        const rows = []
        const walk = (f, depth) => {
            const kids = (byDir[f.path] || []).sort((a, b) => b.count - a.count)
            const open = pane.folderOpen[f.path] === true
            rows.push({ f: f, depth: depth, hasKids: kids.length > 0, open: open })
            if (open)
                for (const c of kids) walk(c, depth + 1)
        }
        for (const r of roots) walk(r, 0)
        return rows
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        // ---------------------------------------------------------------- 查找
        // 搜索文件夹的输入框（原来在「文件夹」板块里面，按需求挪到面板顶部，
        // 也就是「选择一个素材查看详情」这一行所在的位置）
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 32
            radius: Theme.radiusSm
            color: Theme.panel2
            border.width: 1
            border.color: folderSearchField.activeFocus ? Theme.brand : Theme.line

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 9
                spacing: 6

                Text {
                    text: "🔍"
                    color: Theme.soft
                    font.pixelSize: 11
                }
                TextField {
                    id: folderSearchField
                    Layout.fillWidth: true
                    placeholderText: qsTr("搜索文件夹…")
                    color: Theme.txt
                    font.pixelSize: 12
                    background: Item {}
                    onTextChanged: pane.folderSearch = text
                }
                Text {
                    visible: folderSearchField.text !== ""
                    text: "✕"
                    color: Theme.soft
                    font.pixelSize: 12
                    MouseArea {
                        anchors.fill: parent
                        anchors.margins: -4
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            folderSearchField.text = ""
                            pane.folderSearch = ""
                        }
                    }
                }
            }
        }

        // ======================================================= 详情
        // 没选中素材时整张卡收掉：原来会留一张写着「选择一个素材查看详情」的空卡，
        // 占地方也没什么用
        Rectangle {
            visible: !!pane.detail.video
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(380, detailBody.implicitHeight + 30)
            radius: Theme.radius
            color: Theme.panel
            border.width: 1
            border.color: Theme.line

            ColumnLayout {
                id: detailBody
                anchors.fill: parent
                anchors.margins: 15
                spacing: 7

                Text {
                    Layout.fillWidth: true
                    visible: !pane.detail.video
                    text: qsTr("选择一个素材查看详情")
                    color: Theme.soft
                    font.pixelSize: 12
                }

                Text {
                    Layout.fillWidth: true
                    visible: !!pane.detail.video
                    text: pane.detail.video ? pane.detail.video.name : ""
                    color: Theme.txt
                    font.pixelSize: 14
                    font.bold: true
                    wrapMode: Text.WrapAnywhere
                    maximumLineCount: 2
                    elide: Text.ElideMiddle
                }
                Text {
                    Layout.fillWidth: true
                    visible: !!pane.detail.video
                    text: {
                        if (!pane.detail.video)
                            return ""
                        const v = pane.detail.video
                        const res = (v.width && v.height) ? (v.width + "×" + v.height) : qsTr("未知分辨率")
                        return res + " · " + pane.fmtTime(v.duration_ms) + " · " + pane.fmtSize(v.size)
                    }
                    color: Theme.sub
                    font.pixelSize: 11
                }
                Text {
                    Layout.fillWidth: true
                    visible: !!pane.detail.video
                    text: {
                        if (!pane.detail.video)
                            return ""
                        const v = pane.detail.video
                        const codec = (v.codec || "").toLowerCase().indexOf("hvc") >= 0
                                      ? "HEVC (H.265)" : ((v.codec || "").toUpperCase() || "—")
                        return qsTr("编码 ") + codec + " · " + qsTr("格式 ")
                               + (v.ext || "").replace(".", "").toUpperCase()
                    }
                    color: Theme.sub
                    font.pixelSize: 11
                }
                Text {
                    Layout.fillWidth: true
                    visible: !!pane.detail.video
                    text: qsTr("文件夹 ") + (pane.detail.video ? pane.shortFolder(pane.detail.video.parent) : "")
                    color: Theme.sub
                    font.pixelSize: 11
                    elide: Text.ElideMiddle
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 5
                    spacing: 8
                    visible: !!pane.detail.video

                    Repeater {
                        model: [
                            { k: qsTr("调用次数"), v: pane.detail.video ? String(pane.detail.video.usage_count || 0) : "0", c: Theme.brand },
                            { k: qsTr("标签"), v: String((pane.detail.tags || []).length), c: Theme.txt },
                            { k: qsTr("相关"), v: String((pane.detail.similar || []).length), c: Theme.txt }
                        ]
                        delegate: Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: 48
                            radius: Theme.radiusSm
                            color: Theme.panel2

                            Column {
                                anchors.centerIn: parent
                                spacing: 1
                                Text {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    text: modelData.v
                                    color: modelData.c
                                    font.pixelSize: 16
                                    font.bold: true
                                }
                                Text {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    text: modelData.k
                                    color: Theme.soft
                                    font.pixelSize: 10
                                }
                            }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 5
                    visible: !!pane.detail.video
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("标签")
                        color: Theme.sub
                        font.pixelSize: 12
                        font.bold: true
                    }
                    Text {
                        text: "＋"
                        color: Theme.brand
                        font.pixelSize: 14
                        MouseArea {
                            anchors.fill: parent
                            anchors.margins: -6
                            cursorShape: Qt.PointingHandCursor
                            onClicked: pane.addTagRequested()
                        }
                    }
                }

                Flow {
                    Layout.fillWidth: true
                    spacing: 6
                    visible: !!pane.detail.video

                    Repeater {
                        model: pane.detail.tags || []
                        delegate: Rectangle {
                            implicitWidth: chipText.implicitWidth + 24
                            implicitHeight: 23
                            radius: 20
                            color: Theme.brandSoft
                            border.width: 1
                            border.color: Theme.brand

                            Row {
                                anchors.centerIn: parent
                                spacing: 4
                                Text {
                                    id: chipText
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: modelData.name
                                    color: Theme.brandDark
                                    font.pixelSize: 11
                                }
                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: "✕"
                                    color: Theme.brand
                                    font.pixelSize: 10
                                    MouseArea {
                                        anchors.fill: parent
                                        anchors.margins: -5
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: pane.removeTag(modelData.id)
                                    }
                                }
                            }
                        }
                    }
                    Text {
                        visible: (pane.detail.tags || []).length === 0
                        text: qsTr("暂无标签，点 ＋ 添加")
                        color: Theme.soft
                        font.pixelSize: 11
                    }
                }

                Item { Layout.fillHeight: true }
            }
        }

        // ======================================================= 文件夹树
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: Theme.radius
            color: Theme.panel
            border.width: 1
            border.color: Theme.line

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 8

                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("文件夹")
                        color: Theme.txt
                        font.pixelSize: 13
                        font.bold: true
                    }
                    Rectangle {
                        implicitWidth: 66
                        implicitHeight: 24
                        radius: 7
                        color: delHover.containsMouse ? Theme.dangerSoft : Theme.panel2
                        Text {
                            anchors.centerIn: parent
                            text: qsTr("删除选中")
                            color: pane.folderSel.length ? Theme.danger : Theme.soft
                            font.pixelSize: 11
                        }
                        MouseArea {
                            id: delHover
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: pane.deleteSelectedFolders()
                        }
                    }
                }

                // 原来这里有个「搜索文件夹…」输入框，已按需求移到面板顶部
                // （就是「选择一个素材查看详情」那一行的位置）。

                ListView {
                    id: folderList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    // 每行都有边框，间距给 3px 让胶囊之间留出呼吸感
                    spacing: 3
                    model: pane.treeRows
                    boundsBehavior: Flickable.StopAtBounds

                    // 扁平树：行默认透明，hover / 选中 / 生效项才浮出底色，层级靠细线表达
                    delegate: Rectangle {
                        id: fld
                        required property var modelData
                        readonly property var f: modelData.f
                        readonly property bool selected: pane.folderSel.indexOf(f.path) >= 0
                        readonly property bool active: pane.activeFolder === f.path
                        readonly property bool hovered: fldHover.containsMouse
                        readonly property bool isChild: modelData.depth > 0
                        property bool dragActive: false
                        property bool dragging: false
                        property var pressPos: Qt.point(0, 0)

                        width: folderList.width
                        height: fld.isChild ? 30 : 34
                        radius: 9
                        // 分两层做层次，别一列全是灰块：
                        //   根目录 = 有底色的「分组条」（分层感）
                        //   子目录 = 白底 + 极浅描边（轻，不抢眼）
                        // 每一行都保留底色/边框，胶囊边界始终看得见。
                        color: {
                            if (fld.dragActive)
                                return Theme.greenSoft
                            if (fld.active)
                                return Theme.brandSoft
                            if (fld.selected)
                                return "#e6ebff"
                            if (fld.hovered)
                                return fld.isChild ? "#f4f7ff" : "#e6ecf8"
                            return fld.isChild ? "#ffffff" : "#eef2fa"
                        }
                        border.width: 1
                        border.color: fld.active ? "transparent"
                                                 : (fld.isChild ? "#e8eef8" : "#dbe3f2")

                        Behavior on color { ColorAnimation { duration: Theme.durFast } }

                        // 「当前生效的筛选项」用左侧一条品牌色指示条标注
                        Rectangle {
                            visible: fld.active
                            anchors.left: parent.left
                            anchors.leftMargin: 2
                            anchors.verticalCenter: parent.verticalCenter
                            width: 3
                            height: 15
                            radius: 2
                            color: Theme.brand
                        }

                        // ① 背景层：单击 = 用该文件夹筛选；横向拖动 = 拖拽复制
                        MouseArea {
                            id: fldHover
                            anchors.fill: parent
                            hoverEnabled: true
                            acceptedButtons: Qt.LeftButton
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                if (fld.dragging)
                                    return
                                pane.selectFolder(f.path)
                            }
                            onPressed: (m) => {
                                fld.pressPos = Qt.point(m.x, m.y)
                                fld.dragging = false
                            }
                            onPositionChanged: (m) => {
                                if (!pressed || fld.dragging)
                                    return
                                if (Math.abs(m.x - fld.pressPos.x) < 10
                                        && Math.abs(m.y - fld.pressPos.y) < 10)
                                    return
                                fld.dragging = true
                                // 已多选就拖整批（与旧版一致）
                                const paths = (fld.selected && pane.folderSel.length > 1)
                                              ? pane.folderSel.slice() : [f.path]
                                Library.startFileDrag(paths)
                            }
                            onReleased: fld.dragging = false
                        }

                        // （原来这里画了层级竖线 + 横线，按需求去掉了）
                        // ③ 内容层：子级只往右让一丁点（每级 6px），留一点点空隙表明从属就够
                        Row {
                            anchors.verticalCenter: parent.verticalCenter
                            x: 5 + modelData.depth * 6
                            spacing: 6

                            // 展开箭头：热区做成整格（18 × 行高），原来只有 12px 宽的小三角，
                            // 很难点中
                            Item {
                                anchors.verticalCenter: parent.verticalCenter
                                width: 18
                                height: 26

                                Text {
                                    anchors.centerIn: parent
                                    text: modelData.hasKids ? (modelData.open ? "▾" : "▸") : ""
                                    color: modelData.hasKids ? Theme.sub : "transparent"
                                    font.pixelSize: 12
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    enabled: modelData.hasKids
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: pane.toggleFolderOpen(f.path)
                                }
                            }

                            // 多选框：平时弱化，hover / 选中才清晰
                            Rectangle {
                                anchors.verticalCenter: parent.verticalCenter
                                width: 14
                                height: 14
                                radius: 4
                                opacity: (fld.selected || fld.hovered) ? 1 : 0.28
                                color: fld.selected ? Theme.brand : Theme.panel
                                border.width: 1.4
                                border.color: fld.selected ? Theme.brand : Theme.line2

                                Behavior on opacity { NumberAnimation { duration: Theme.durFast } }

                                Text {
                                    anchors.centerIn: parent
                                    visible: fld.selected
                                    text: "✓"
                                    color: "white"
                                    font.pixelSize: 9
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: pane.toggleFolderSel(f.path)
                                }
                            }

                            // 名字：从左边开始显示，尽量给足宽度（右侧的计数/删除让位），
                            // 实在放不下才右侧省略；鼠标停在行上会弹出全称
                            Text {
                                id: nameText
                                anchors.verticalCenter: parent.verticalCenter
                                text: f.name
                                color: fld.active ? Theme.brandDark : (fld.isChild ? Theme.sub : Theme.txt)
                                font.pixelSize: fld.isChild ? 11 : 12
                                font.bold: !fld.isChild
                                elide: Text.ElideRight
                                width: Math.max(40, fld.width - x - 46)

                                ToolTip.visible: fld.hovered && nameText.truncated
                                ToolTip.text: f.name
                                ToolTip.delay: 350
                            }
                        }

                        // 右侧：数量用纯文字（比胶囊更轻），删除键只在该行 hover 时出现
                        Row {
                            anchors.right: parent.right
                            anchors.rightMargin: 8
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 8

                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: f.count
                                color: fld.isChild ? Theme.soft : Theme.sub
                                font.pixelSize: 11
                            }
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: "🗑"
                                font.pixelSize: 11
                                opacity: fldHover.containsMouse ? 1 : 0
                                Behavior on opacity { NumberAnimation { duration: Theme.durFast } }

                                MouseArea {
                                    anchors.fill: parent
                                    anchors.margins: -5
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: pane.deleteFolder(f.path)
                                }
                            }
                        }

                        // ⑨ 接收拖放：拖过来的素材/文件夹复制到本文件夹
                        DropArea {
                            anchors.fill: parent
                            onEntered: fld.dragActive = true
                            onExited: fld.dragActive = false
                            onDropped: (drop) => {
                                fld.dragActive = false
                                if (!drop.hasUrls)
                                    return
                                const paths = []
                                for (let i = 0; i < drop.urls.length; ++i) {
                                    const p = Library.pathFromUrl(drop.urls[i])
                                    if (p !== "")
                                        paths.push(p)
                                }
                                if (paths.length === 0)
                                    return
                                const r = Library.copyPathsTo(paths, f.path)
                                pane.notifyCopied(r.ok ? (r.copied ? r.copied.length : 0) : -1)
                            }
                        }
                    }
                }

                Text {
                    Layout.fillWidth: true
                    visible: pane.treeRows.length === 0
                    text: pane.folderSearch !== "" ? qsTr("无匹配文件夹") : qsTr("暂无文件夹")
                    color: Theme.soft
                    font.pixelSize: 11
                }
            }
        }
    }

    // 文件夹拖拽复制：状态与信号桥接（放下时触发 copyFolders）
    property string dragSource: ""
    function beginFolderDrag(path) {
        dragSource = path
    }
    function endFolderDrag() {
        dragSource = ""
    }
    function dropFolderOn(target) {
        if (dragSource !== "" && dragSource !== target)
            pane.dragFolderTo(dragSource, target)
        dragSource = ""
    }
}
