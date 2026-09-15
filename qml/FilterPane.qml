import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Workbench

// 左侧筛选栏（对应旧版 index.html 的 .sidebar）
//
// 旧版关键样式（style.css）：
//   .tags{display:flex;flex-wrap:wrap;gap:7px}
//   .tag{padding:5px 10px;border:1.5px solid var(--line);border-radius:20px;background:#fff}
//   .tag.on{background:var(--brand-grad);border-color:transparent;color:#fff;font-weight:600}
//   .tag .n{font-size:10px;background:var(--panel2);border-radius:8px;padding:1px 6px}
//   .tag .x{opacity:0} .tag:hover .x{opacity:1}
//   .andor-row{background:var(--panel2);border:1px solid var(--line);border-radius:12px;padding:9px 11px}
//
// 注意：QML 同层级「后声明者在上层」，所以铺满整块的 MouseArea 必须最先声明，
// 否则会把里面 ✕ 等子元素的点击全部吃掉。
Item {
    id: pane

    property var categories: ({ "shot": [], "product": [], "availability": [] })
    property var selectedShot: []
    property var selectedProduct: []
    property var selectedAvail: []
    property string andOr: "and"

    property int minUsage: 0
    property bool onlyTagged: false

    signal toggleTag(string cat, int tagId)
    signal createTag(string cat)
    signal removeTag(string cat, int tagId, string name)
    signal setAndOr(string mode)
    signal setMinUsage(int value)
    signal setOnlyTagged(bool on)

    function isOn(cat, id) {
        if (cat === "shot")
            return pane.selectedShot.indexOf(id) >= 0
        if (cat === "product")
            return pane.selectedProduct.indexOf(id) >= 0
        return pane.selectedAvail.indexOf(id) >= 0
    }

    Gradient {
        id: chipGrad
        GradientStop { position: 0; color: Theme.brand }
        GradientStop { position: 1; color: Theme.brand2 }
    }

    // 胶囊标签
    component TagChip: Rectangle {
        id: chip
        property int tagId: 0
        property string tagName: ""
        property int tagCount: 0
        property bool on: false
        property string cat: ""

        implicitWidth: chipRow.implicitWidth + 20
        implicitHeight: 28
        radius: 20
        color: chip.on ? "transparent" : (chipHover.containsMouse ? "#fbfcff" : "white")
        gradient: chip.on ? chipGrad : null
        border.width: 1.5
        border.color: chip.on ? "transparent" : (chipHover.containsMouse ? Theme.brand : Theme.line)

        // ① 背景层（先声明 = 在最下层）：点它 = 切换筛选
        MouseArea {
            id: chipHover
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: pane.toggleTag(chip.cat, chip.tagId)
        }

        // ② 内容层（在上层）：里面的 ✕ 能正常收到点击
        Row {
            id: chipRow
            anchors.centerIn: parent
            spacing: 5

            Text {
                text: chip.tagName
                color: chip.on ? "white" : (chipHover.containsMouse ? Theme.brandDark : Theme.sub)
                font.pixelSize: 12
                font.bold: chip.on
            }

            // 计数（小胶囊）
            Rectangle {
                visible: chip.tagCount > 0
                anchors.verticalCenter: parent.verticalCenter
                implicitWidth: cntText.implicitWidth + 12
                implicitHeight: 16
                radius: 8
                color: chip.on ? Qt.rgba(1, 1, 1, 0.22) : Theme.panel2

                Text {
                    id: cntText
                    anchors.centerIn: parent
                    text: chip.tagCount
                    color: chip.on ? Qt.rgba(1, 1, 1, 0.85) : Theme.soft
                    font.pixelSize: 10
                }
            }

            // 删除：默认隐藏，hover 才淡入（旧版 .tag .x{opacity:0}）
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: "✕"
                color: chip.on ? "white" : Theme.soft
                font.pixelSize: 10
                opacity: chipHover.containsMouse ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: 100 } }

                MouseArea {
                    anchors.fill: parent
                    anchors.margins: -5
                    cursorShape: Qt.PointingHandCursor
                    onClicked: pane.removeTag(chip.cat, chip.tagId, chip.tagName)
                }
            }
        }
    }

    // 一个分组：带边框的板块（标题 + ＋ + 内部可滚动的标签区）。
    // 加边框是为了让三块的分界一眼可见 —— 只有标题的话容易糊成一片。
    component TagGroup: Rectangle {
        id: group
        property string cat: ""
        property string title: ""
        property var items: []

        Layout.fillWidth: true
        radius: Theme.radiusSm
        color: "transparent"
        border.width: 1
        border.color: Theme.line2

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 9
            spacing: 8

            RowLayout {
                Layout.fillWidth: true
                spacing: 4

                Text {
                    Layout.fillWidth: true
                    text: group.title
                    color: Theme.txt
                    font.pixelSize: 13
                    font.bold: true
                }
                Text {
                    text: "＋"
                    color: addHover.containsMouse ? Theme.brandDark : Theme.brand
                    font.pixelSize: 14
                    MouseArea {
                        id: addHover
                        anchors.fill: parent
                        anchors.margins: -6
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: pane.createTag(group.cat)
                    }
                }
            }

            // 板块内部自己滚动：标签多的时候在里面翻找，
            // 不会把整个侧栏撑高，也不会挤动别的板块（三个板块的位置是固定的）
            Flickable {
                id: chipScroll
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 50
                clip: true
                contentWidth: width
                contentHeight: chips.implicitHeight
                boundsBehavior: Flickable.StopAtBounds

                ScrollBar.vertical: ScrollBar {
                    id: chipBar
                    policy: ScrollBar.AsNeeded
                    contentItem: Rectangle {
                        implicitWidth: 5
                        radius: 3
                        color: chipBar.pressed ? Theme.brand : "#c6cee0"
                        opacity: chipBar.active ? 0.9 : 0
                        Behavior on opacity { NumberAnimation { duration: Theme.durSlow } }
                    }
                    background: Item {}
                }

                Flow {
                    id: chips
                    width: chipScroll.width - 8
                    spacing: 7

                    Repeater {
                        model: group.items
                        delegate: TagChip {
                            required property var modelData
                            tagId: modelData.id
                            tagName: modelData.name
                            tagCount: modelData.count
                            cat: group.cat
                            on: pane.isOn(group.cat, modelData.id)
                        }
                    }
                }
            }
        }
    }

    // 三个分类板块的位置固定：外层不再整块滚动，改成每块内部自己滚（见 TagGroup）
    ColumnLayout {
        id: body
        anchors.fill: parent
        anchors.margins: 12
        spacing: 12

            // ---- 视图筛选：调用次数下限 / 是否打过标签（与下面的标签筛选叠加生效）----
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 8

                Text {
                    Layout.fillWidth: true
                    text: qsTr("视图筛选")
                    color: Theme.txt
                    font.pixelSize: 13
                    font.bold: true
                }

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 40
                    radius: 11
                    color: Theme.panel2
                    border.width: 1
                    border.color: Theme.line

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 11
                        anchors.rightMargin: 8
                        spacing: 7

                        Text {
                            Layout.fillWidth: true
                            text: qsTr("调用次数 ≥")
                            color: Theme.sub
                            font.pixelSize: 11
                            elide: Text.ElideRight
                        }
                        TextField {
                            id: usageField
                            Layout.preferredWidth: 54
                            Layout.preferredHeight: 28
                            // 空着就显示 0（不再留空）
                            text: pane.minUsage > 0 ? String(pane.minUsage) : "0"
                            horizontalAlignment: TextInput.AlignHCenter
                            color: Theme.txt
                            font.pixelSize: 12
                            selectByMouse: true
                            validator: IntValidator { bottom: 0; top: 99999 }
                            background: Rectangle {
                                radius: 8
                                color: Theme.panel
                                border.width: 1
                                border.color: usageField.activeFocus ? Theme.brand : Theme.line
                            }
                            // 回车 / 失焦都算「填完了」：提交数值并收起焦点。
                            // 以前只发信号不收焦点，所以回车之后光标还在框里一直闪。
                            function commit() {
                                const v = Math.max(0, parseInt(text) || 0)
                                pane.setMinUsage(v)
                                text = v > 0 ? String(v) : "0"
                                focus = false
                            }
                            onEditingFinished: commit()
                            onActiveFocusChanged: {
                                if (activeFocus)
                                    selectAll()
                                else
                                    commit()
                            }
                        }
                        Text {
                            text: qsTr("次")
                            color: Theme.soft
                            font.pixelSize: 11
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 40
                    radius: 11
                    color: Theme.panel2
                    border.width: 1
                    border.color: Theme.line

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 11
                        anchors.rightMargin: 9
                        spacing: 7

                        Text {
                            Layout.fillWidth: true
                            text: qsTr("仅显示有标签")
                            color: Theme.sub
                            font.pixelSize: 11
                            elide: Text.ElideRight
                        }
                        Toggle {
                            checked: pane.onlyTagged
                            onToggled: (on) => pane.setOnlyTagged(on)
                        }
                    }
                }
            }

            TagGroup {
                cat: "shot"
                title: qsTr("镜头内容")
                items: pane.categories.shot || []
                // 前两个板块大一些，各自内部滚动
                Layout.fillHeight: true
                Layout.preferredHeight: 240
            }

            // 镜头内容 × 产品的组合方式（旧版 .andor-row）
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 42
                radius: 12
                color: Theme.panel2
                border.width: 1
                border.color: Theme.line

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 11
                    anchors.rightMargin: 8
                    spacing: 8

                    Text {
                        Layout.fillWidth: true
                        text: qsTr("镜头内容 × 产品")
                        color: Theme.sub
                        font.pixelSize: 11
                        elide: Text.ElideRight
                    }

                    // 分段控件：且 / 或
                    Rectangle {
                        implicitWidth: 104
                        implicitHeight: 26
                        radius: 8
                        color: "#e4e9f5"

                        Rectangle {
                            x: pane.andOr === "or" ? parent.width / 2 : 0
                            width: parent.width / 2
                            height: parent.height
                            radius: 8
                            color: Theme.brand

                            Behavior on x { NumberAnimation { duration: 120 } }
                        }
                        Row {
                            anchors.fill: parent
                            Repeater {
                                model: [
                                    { label: qsTr("且"), mode: "and" },
                                    { label: qsTr("或"), mode: "or" }
                                ]
                                delegate: Item {
                                    width: parent.width / 2
                                    height: parent.height
                                    Text {
                                        anchors.centerIn: parent
                                        text: modelData.label
                                        color: pane.andOr === modelData.mode ? "white" : Theme.sub
                                        font.pixelSize: 12
                                        font.bold: pane.andOr === modelData.mode
                                    }
                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: pane.setAndOr(modelData.mode)
                                    }
                                }
                            }
                        }
                    }
                }
            }

            TagGroup {
                cat: "product"
                title: qsTr("产品")
                items: pane.categories.product || []
                // 前两个板块大一些，各自内部滚动
                Layout.fillHeight: true
                Layout.preferredHeight: 240
            }

            TagGroup {
                cat: "availability"
                title: qsTr("可用性（仅且）")
                items: pane.categories.availability || []
                // 第三个板块固定矮一些
                Layout.fillHeight: false
                Layout.preferredHeight: 132
            }

            Item { implicitHeight: 2 }
        }
}
