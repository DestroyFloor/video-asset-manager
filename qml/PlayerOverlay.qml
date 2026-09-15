import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import QtQuick.Window
import Workbench

// 播放器浮层（对应旧版 index.html 的 #playerTpl / .player-card）
//
// 旧版结构：左侧主区（标题栏 54px 可拖动 + meta + 视频区 + 控制条 + 操作条）
//          右侧 236px「当前文件夹」播放列表（缩略图 + 名称 + 时长，点击切换）
// 标题栏：片名 + [镜头内容][产品][可用性] + [◂ 上一个][下一个 ▸] + [✕]
Item {
    id: player

    property var playlist: []      // 当前网格里的视频（用于播放列表与上下一个）
    property int currentId: 0
    property string currentPath: ""
    property string currentName: ""
    property var currentItem: ({})

    signal closed()
    signal tagRequested(int videoId, string category)
    signal currentChanged(int videoId)
    // 删除当前视频（由 LibraryView 弹出确认，再走回收站）
    signal deleteRequested(int videoId)

    visible: currentId !== 0
    z: 90

    function fmtTime(sec) {
        if (!sec || sec < 0)
            sec = 0
        const t = Math.floor(sec)
        const m = Math.floor(t / 60)
        const s = t % 60
        return m + ":" + (s < 10 ? "0" : "") + s
    }

    function open(item) {
        if (!item)
            return
        player.currentId = item.id
        player.currentItem = item
        player.currentName = item.name
        player.currentPath = item.path
        player.currentChanged(item.id)
    }

    function step(dir) {
        if (player.playlist.length === 0)
            return
        const ids = player.playlist.map(v => v.id)
        let idx = ids.indexOf(player.currentId)
        idx = idx < 0 ? 0 : (idx + dir + ids.length) % ids.length
        const nv = player.playlist[idx]
        if (nv)
            player.open(nv)
    }

    // 「视频全屏」：让播放卡片本身铺满整个浮层（= 铺满屏幕），同时隐藏标题/元信息/操作条
    // 与右侧播放列表，只留下视频 + 底部控制条。
    // 注意重点：这里全屏的是「视频」而不是「软件」——窗口之所以跟着进全屏，
    // 只是为了让视频能铺到屏幕边缘；卡片多大仍然由下面的 card 尺寸控制。
    property bool fullscreen: false

    function toggleFullscreen() {
        const w = Window.window
        fullscreen = !fullscreen
        if (w)
            w.visibility = fullscreen ? Window.FullScreen : Window.Windowed
    }

    function exitFullscreen() {
        if (!fullscreen)
            return
        fullscreen = false
        const w = Window.window
        if (w)
            w.visibility = Window.Windowed
    }

    // 遮罩（点击不关闭，避免误关；全屏时不需要）
    Rectangle {
        anchors.fill: parent
        visible: !player.fullscreen
        color: Qt.rgba(8 / 255, 10 / 255, 16 / 255, 0.55)
    }

    // ------------------------------------------------------------ 播放卡片
    // 用 x/y 定位而不是 anchors.centerIn：初始居中，之后用户拖动标题栏就停在拖到的位置。
    // 全屏时改为铺满整个浮层，四角也不需要圆角了。
    Rectangle {
        id: card
        x: player.fullscreen ? 0 : Math.round((parent.width - width) / 2)
        y: player.fullscreen ? 0 : Math.round((parent.height - height) / 2)
        width: player.fullscreen ? parent.width : Math.min(parent.width - 60, 1120)
        height: player.fullscreen ? parent.height : Math.min(parent.height - 60, 760)
        radius: player.fullscreen ? 0 : 16
        color: "#161a22"
        border.width: player.fullscreen ? 0 : 1
        border.color: Qt.rgba(1, 1, 1, 0.12)
        clip: true

        RowLayout {
            anchors.fill: parent
            // 让出卡片自身那 1px 描边：视频区是不透明的，直接铺到卡片最外一列会把左边框
            // 那一条盖成黑色（截图里就是「黑框比上面的标题栏往左多出 1px、像被硬塞进去」）。
            // 其余区域是半透明色，盖上去只是把描边混亮一点，所以只有左边看着「凸」出来。
            anchors.margins: card.border.width
            spacing: 0

            // ==================================================== 左侧主区
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                // ---- 标题栏（可拖动）----
                // 全屏时收起给视频让位；退出全屏用控制条上的 ⛶ 或 ESC。
                Rectangle {
                    id: titleBar
                    visible: !player.fullscreen
                    Layout.fillWidth: true
                    implicitHeight: 54
                    color: Qt.rgba(1, 1, 1, 0.05)

                    MouseArea {
                        id: headDrag
                        anchors.fill: parent
                        acceptedButtons: Qt.LeftButton
                        cursorShape: Qt.SizeAllCursor
                        property point pressPos: Qt.point(0, 0)   // 按下点在 player 坐标系中的位置
                        property point cardStart: Qt.point(0, 0)  // 按下时卡片左上角
                        onPressed: (m) => {
                            pressPos = headDrag.mapToItem(player, m.x, m.y)
                            cardStart = Qt.point(card.x, card.y)
                        }
                        // 拖动标题栏移动播放器。两个关键点：
                        //  1) 用 mapToItem(player, …) 换算到父坐标再算位移。MouseArea 长在卡片里，
                        //     卡片一移动它的局部坐标也跟着变 —— 用局部坐标做增量会正反馈抖动（就是「抽搐」）。
                        //  2) 上下限用 player 而不是 parent：parent 是这条 54px 高的标题栏，
                        //     拿它算 Math.min(parent.height-60, y) 会把 y 永远夹成 0（表现为「只能左右拖」）。
                        onPositionChanged: (m) => {
                            if (!pressed)
                                return
                            const cur = headDrag.mapToItem(player, m.x, m.y)
                            let nx = cardStart.x + (cur.x - pressPos.x)
                            let ny = cardStart.y + (cur.y - pressPos.y)
                            nx = Math.max(-card.width + 120, Math.min(player.width - 120, nx))
                            ny = Math.max(0, Math.min(player.height - 90, ny))
                            card.x = nx
                            card.y = ny
                        }
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 18
                        anchors.rightMargin: 12
                        spacing: 8

                        Text {
                            Layout.fillWidth: true
                            text: player.currentName
                            color: "white"
                            font.pixelSize: 15
                            font.bold: true
                            elide: Text.ElideMiddle
                        }

                        // 打标：三个分类本来就在同一个弹层里，所以这里只留一个入口
                        Rectangle {
                            implicitWidth: tagLabel.implicitWidth + 22
                            implicitHeight: 32
                            radius: 9
                            color: tagHover.containsMouse ? Qt.rgba(1, 1, 1, 0.2) : Qt.rgba(1, 1, 1, 0.08)
                            border.width: 1
                            border.color: Qt.rgba(1, 1, 1, 0.12)

                            Text {
                                id: tagLabel
                                anchors.centerIn: parent
                                text: qsTr("设置标签")
                                color: "#e8ecf5"
                                font.pixelSize: 12
                            }
                            MouseArea {
                                id: tagHover
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: player.tagRequested(player.currentId, "shot")
                            }
                        }

                        Rectangle {
                            implicitWidth: 74
                            implicitHeight: 32
                            radius: 9
                            color: prevHover.containsMouse ? Qt.rgba(1, 1, 1, 0.2) : Qt.rgba(1, 1, 1, 0.08)
                            Text {
                                anchors.centerIn: parent
                                text: qsTr("◂ 上一个")
                                color: "#e8ecf5"
                                font.pixelSize: 12
                            }
                            MouseArea {
                                id: prevHover
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: player.step(-1)
                            }
                        }
                        Rectangle {
                            implicitWidth: 74
                            implicitHeight: 32
                            radius: 9
                            color: nextHover.containsMouse ? Qt.rgba(1, 1, 1, 0.2) : Qt.rgba(1, 1, 1, 0.08)
                            Text {
                                anchors.centerIn: parent
                                text: qsTr("下一个 ▸")
                                color: "#e8ecf5"
                                font.pixelSize: 12
                            }
                            MouseArea {
                                id: nextHover
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: player.step(1)
                            }
                        }

                        // 关闭（旧版 .player-close：hover 变红）
                        Rectangle {
                            implicitWidth: 34
                            implicitHeight: 34
                            radius: 10
                            color: closeHover.containsMouse ? "#e04747" : Qt.rgba(1, 1, 1, 0.14)

                            Text {
                                anchors.centerIn: parent
                                text: "✕"
                                color: "white"
                                font.pixelSize: 15
                            }
                            MouseArea {
                                id: closeHover
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: player.closed()
                            }
                        }
                    }
                }

                // ---- meta 条：调用次数 / 解码信息 / 帧率 / 标签 ----
                // 全屏时收起（视频吃满屏幕）
                Rectangle {
                    id: metaBar
                    visible: !player.fullscreen
                    Layout.fillWidth: true
                    implicitHeight: 30
                    color: Qt.rgba(1, 1, 1, 0.03)

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 18
                        anchors.rightMargin: 14
                        spacing: 6

                        Text {
                            text: qsTr("调用 ") + (player.currentItem.usage_count || 0) + qsTr(" 次")
                            color: "white"
                            font.pixelSize: 12
                            font.bold: true
                            leftPadding: 8
                            rightPadding: 8
                            topPadding: 2
                            bottomPadding: 2
                            Rectangle {
                                anchors.fill: parent
                                z: -1
                                radius: 7
                                color: Qt.rgba(91 / 255, 108 / 255, 1, 0.3)
                            }
                        }

                        Repeater {
                            model: (player.currentItem.tags || [])
                            delegate: Rectangle {
                                implicitWidth: tagTxt.implicitWidth + 16
                                implicitHeight: 20
                                radius: 7
                                color: Qt.rgba(1, 1, 1, 0.12)
                                Text {
                                    id: tagTxt
                                    anchors.centerIn: parent
                                    text: modelData.name
                                    color: "#dfe3ee"
                                    font.pixelSize: 11
                                }
                            }
                        }

                        Item { Layout.fillWidth: true }

                        Text {
                            text: "GPU 解码"
                            visible: false
                            color: "#8ad3a8"
                            font.pixelSize: 11
                        }
                    }
                }

                // ---- 视频区 ----
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    // 与卡片面板同色。mpv 的 letterbox（竖屏素材两侧的填充）也设成同一个色
                    // （见 MpvItem 的 background-color），于是填充就是面板的一部分，
                    // 而不是既不透明又贴边、看起来被硬塞进卡片的一块「黑框」。
                    color: "#161a22"

                    MpvVideo {
                        id: video
                        anchors.fill: parent
                        source: player.currentPath
                    }

                    // 空态
                    Column {
                        anchors.centerIn: parent
                        spacing: 8
                        visible: player.currentPath === ""
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: "🎞️"
                            font.pixelSize: 34
                            opacity: 0.5
                        }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: qsTr("选择左侧素材开始播放")
                            color: "#8b93a7"
                            font.pixelSize: 12
                        }
                    }

                    // 转码进度（超过 1080p 的素材自动生成代理）
                    Rectangle {
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: 16
                        width: Math.min(parent.width - 60, 440)
                        height: 58
                        radius: 10
                        color: Qt.rgba(0, 0, 0, 0.78)
                        visible: Proxy.busy

                        Column {
                            anchors.centerIn: parent
                            spacing: 7
                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: qsTr("正在生成 1080p 代理…")
                                      + (Proxy.progress >= 0 ? "  " + Proxy.progress + "%" : "")
                                color: "#e8ecf6"
                                font.pixelSize: 12
                            }
                            Rectangle {
                                width: 360
                                height: 5
                                radius: 3
                                color: "#343b4b"
                                Rectangle {
                                    width: parent.width * Math.max(0, Math.min(100, Proxy.progress)) / 100
                                    height: parent.height
                                    radius: 3
                                    color: Theme.brand
                                }
                            }
                        }
                    }
                }

                // ---- 控制条 ----
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 52
                    color: Qt.rgba(1, 1, 1, 0.05)

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 14
                        anchors.rightMargin: 14
                        spacing: 10

                        Rectangle {
                            implicitWidth: 34
                            implicitHeight: 34
                            radius: 10
                            color: playHover.containsMouse ? Qt.rgba(1, 1, 1, 0.14) : "transparent"
                            Text {
                                anchors.centerIn: parent
                                // 播完后 keep-open 停在最后一帧但 paused 仍是 false，
                                // 所以要单独看 eof，否则按钮永远显示成「暂停中」
                                text: video.eof ? "↻" : (video.paused ? "▶" : "⏸")
                                color: "#e8ecf5"
                                font.pixelSize: 15
                            }
                            MouseArea {
                                id: playHover
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    if (video.eof)
                                        video.replay()
                                    else
                                        video.togglePause()
                                }
                            }
                        }

                        Text {
                            text: player.fmtTime(video.position) + " / " + player.fmtTime(video.duration)
                            color: "#cdd5e6"
                            font.pixelSize: 12
                        }

                        Rectangle {
                            id: track
                            Layout.fillWidth: true
                            implicitHeight: 6
                            radius: 10
                            color: Qt.rgba(1, 1, 1, 0.16)

                            Rectangle {
                                width: track.width * (video.duration > 0
                                                      ? Math.max(0, Math.min(1, video.position / video.duration)) : 0)
                                height: parent.height
                                radius: 10
                                gradient: Gradient {
                                    GradientStop { position: 0; color: "#5b6cff" }
                                    GradientStop { position: 1; color: "#8a5bff" }
                                }
                            }
                            MouseArea {
                                anchors.fill: parent
                                anchors.margins: -8
                                cursorShape: Qt.PointingHandCursor
                                onClicked: function (m) {
                                    if (video.duration > 0)
                                        video.seekSeconds(video.duration * Math.max(0, Math.min(1, m.x / track.width)))
                                }
                            }
                        }

                        Rectangle {
                            implicitWidth: 34
                            implicitHeight: 34
                            radius: 10
                            color: muteHover.containsMouse ? Qt.rgba(1, 1, 1, 0.14) : "transparent"
                            Text {
                                anchors.centerIn: parent
                                text: video.muted ? "🔇" : "🔊"
                                font.pixelSize: 14
                            }
                            MouseArea {
                                id: muteHover
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: video.muted = !video.muted
                            }
                        }

                        // 音量：自绘滑块（左小右大，方向明确）。
                        // 原来用 Controls 的 Slider，在 Basic 风格下与 video.volume 的绑定互相覆盖，
                        // 拖起来方向混乱（表现为「音量是反的」），这里换成完全可控的自绘条。
                        Row {
                            Layout.alignment: Qt.AlignVCenter
                            spacing: 6

                            Rectangle {
                                id: volTrack
                                anchors.verticalCenter: parent.verticalCenter
                                width: 84
                                height: 6
                                radius: 3
                                color: Qt.rgba(1, 1, 1, 0.18)

                                Rectangle {
                                    width: parent.width * Math.max(0, Math.min(100, video.volume)) / 100
                                    height: parent.height
                                    radius: 3
                                    color: video.muted ? "#6b7286" : "#5b6cff"
                                }
                                Rectangle {
                                    x: parent.width * Math.max(0, Math.min(100, video.volume)) / 100 - width / 2
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 11
                                    height: 11
                                    radius: 6
                                    color: "white"
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    anchors.margins: -8
                                    cursorShape: Qt.PointingHandCursor
                                    function applyAt(x) {
                                        video.volume = Math.round(Math.max(0, Math.min(1, x / volTrack.width)) * 100)
                                    }
                                    onPressed: (m) => applyAt(m.x - 8)
                                    onPositionChanged: (m) => {
                                        if (pressed)
                                            applyAt(m.x - 8)
                                    }
                                }
                            }

                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: Math.round(video.volume) + "%"
                                color: "#cdd5e6"
                                font.pixelSize: 11
                                width: 32
                            }
                        }

                        Text {
                            text: video.renderFps.toFixed(0) + " fps"
                            color: "#9fb4ff"
                            font.pixelSize: 11
                        }
                        Text {
                            visible: video.decoderInfo !== ""
                            text: video.decoderInfo
                            color: video.decoderInfo.indexOf("软解") >= 0 ? "#e8b45f" : "#8ad3a8"
                            font.pixelSize: 11
                            elide: Text.ElideRight
                            Layout.maximumWidth: 260
                        }

                        // 全屏（老框架控制条里的 ⛶）：全屏的是「视频」，不是软件窗口
                        Rectangle {
                            implicitWidth: 34
                            implicitHeight: 34
                            radius: 10
                            color: fsHover.containsMouse ? Qt.rgba(1, 1, 1, 0.14) : "transparent"
                            Text {
                                anchors.centerIn: parent
                                text: player.fullscreen ? "🗗" : "⛶"
                                color: "#e8ecf5"
                                font.pixelSize: 15
                            }
                            MouseArea {
                                id: fsHover
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: player.toggleFullscreen()
                            }
                        }
                    }
                }

                // ---- 操作条（旧版 .player-actions）----
                // 全屏时收起（沉浸看片时用不到这几个动作）
                Rectangle {
                    id: actionsBar
                    visible: !player.fullscreen
                    Layout.fillWidth: true
                    implicitHeight: 50
                    color: Qt.rgba(1, 1, 1, 0.05)

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 14
                        anchors.rightMargin: 14
                        spacing: 10

                        Repeater {
                            model: [
                                { id: "folder", label: qsTr("打开所在文件夹") },
                                { id: "external", label: qsTr("外部播放器打开") }
                            ]
                            delegate: Rectangle {
                                implicitWidth: actTxt.implicitWidth + 26
                                implicitHeight: 32
                                radius: Theme.radiusSm
                                color: actHover.containsMouse ? Qt.rgba(1, 1, 1, 0.18) : Qt.rgba(1, 1, 1, 0.08)
                                border.width: 1
                                border.color: Qt.rgba(1, 1, 1, 0.18)

                                Text {
                                    id: actTxt
                                    anchors.centerIn: parent
                                    text: modelData.label
                                    color: "#e8ecf5"
                                    font.pixelSize: 12
                                }
                                MouseArea {
                                    id: actHover
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        if (modelData.id === "folder") {
                                            Library.openFolder(player.currentItem.parent || "")
                                        } else {
                                            // 外部播放器：没配过（换电脑后首次用）就让用户选一个，
                                            // 选完记进配置，之后直接打开
                                            if (!Library.openWithPlayer(player.currentPath))
                                                playerPicker.open()
                                        }
                                    }
                                }
                            }
                        }

                        // 删除当前视频（走 LibraryView 的确认 + 回收站流程）
                        Rectangle {
                            id: delBtn
                            implicitWidth: delTxt.implicitWidth + 30
                            implicitHeight: 32
                            radius: Theme.radiusSm
                            color: delHover.containsMouse ? Theme.danger : Qt.rgba(224 / 255, 71 / 255, 71 / 255, 0.16)
                            border.width: 1
                            border.color: delHover.containsMouse ? Theme.danger : Qt.rgba(224 / 255, 71 / 255, 71 / 255, 0.45)

                            Behavior on color { ColorAnimation { duration: Theme.durBase } }
                            Behavior on border.color { ColorAnimation { duration: Theme.durBase } }

                            Text {
                                id: delTxt
                                anchors.centerIn: parent
                                text: qsTr("删除该视频")
                                color: delHover.containsMouse ? "white" : "#ff9d9d"
                                font.pixelSize: 12
                            }
                            MouseArea {
                                id: delHover
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: player.deleteRequested(player.currentId)
                            }
                        }

                        Item { Layout.fillWidth: true }
                    }
                }
            }

            // ==================================================== 右侧播放列表
            // 全屏时整列收起，把宽度让给视频
            Rectangle {
                visible: !player.fullscreen
                Layout.preferredWidth: 236
                Layout.fillHeight: true
                color: Qt.rgba(1, 1, 1, 0.04)

                Rectangle {
                    anchors.left: parent.left
                    width: 1
                    height: parent.height
                    color: Qt.rgba(1, 1, 1, 0.1)
                }

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: 14
                        Layout.rightMargin: 14
                        Layout.topMargin: 14
                        Layout.bottomMargin: 8
                        Text {
                            Layout.fillWidth: true
                            text: qsTr("当前文件夹")
                            color: "white"
                            font.pixelSize: 13
                            font.bold: true
                        }
                        Text {
                            text: player.playlist.length ? ("· " + player.playlist.length) : ""
                            color: "#9aa6c0"
                            font.pixelSize: 11
                        }
                    }

                    ListView {
                        id: plList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.leftMargin: 10
                        Layout.rightMargin: 10
                        Layout.bottomMargin: 12
                        clip: true
                        spacing: 6
                        model: player.playlist
                        boundsBehavior: Flickable.StopAtBounds

                        delegate: Rectangle {
                            id: plRow
                            width: plList.width
                            height: 46
                            radius: 10
                            color: modelData.id === player.currentId
                                   ? Qt.rgba(91 / 255, 108 / 255, 1, 0.3)
                                   : (plHover.containsMouse ? Qt.rgba(1, 1, 1, 0.08) : "transparent")

                            Row {
                                anchors.fill: parent
                                anchors.margins: 6
                                spacing: 8

                                Image {
                                    width: 34
                                    height: 34
                                    source: Library.thumbUrl(modelData.id)
                                    fillMode: Image.PreserveAspectCrop
                                    asynchronous: true
                                    cache: false

                                    Rectangle {
                                        anchors.fill: parent
                                        color: "#333"
                                        z: -1
                                    }
                                }
                                Column {
                                    width: parent.width - 42
                                    height: parent.height
                                    Text {
                                        width: parent.width
                                        text: modelData.name
                                        color: "#dfe3ee"
                                        font.pixelSize: 12
                                        elide: Text.ElideMiddle
                                    }
                                    Text {
                                        text: player.fmtTime((modelData.duration_ms || 0) / 1000)
                                        color: "#9aa6c0"
                                        font.pixelSize: 10
                                    }
                                }
                            }

                            MouseArea {
                                id: plHover
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: player.open(modelData)
                            }
                        }
                    }
                }
            }
        }
    }

    // 打开素材后自动开始播放
    onCurrentPathChanged: {
        if (player.currentPath !== "")
            video.paused = false
    }

    // 外部播放器没配过（或者配置里那个程序已经不在了，比如换了一台电脑）时，
    // 让用户挑一个播放器；选完存进配置，下次直接用它打开。
    FileDialog {
        id: playerPicker
        title: qsTr("选择用来播放视频的程序")
        nameFilters: [qsTr("可执行程序 (*.exe)")]
        onAccepted: {
            const p = Library.pathFromUrl(selectedFile)
            if (p !== "") {
                Library.setPlayerExe(p)
                Library.openWithPlayer(player.currentPath)
            }
        }
    }
}
