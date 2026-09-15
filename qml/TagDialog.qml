import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Workbench

// 打标弹层（对应旧版 index.html 的 #tagModal）：
// 按「镜头内容 / 产品 / 可用性」三组勾选，批量给选中视频增删标签。
//
// 注意：这里**不能**自己写 `dlg.shown`。外部（LibraryView）是用 `shown: view.tagDialogShown`
// 绑定进来的，内部一旦赋值就会打断绑定 → 表现为「第一次能打开，关掉之后再点就打不开了」。
// 所以关闭一律通过 closed() 信号交给外部处理。
Item {
    id: dlg

    property bool shown: false
    property var categories: ({ "shot": [], "product": [], "availability": [] })
    property var videoIds: []
    property var commonTags: []        // 多选视频共有的标签 id
    property string newTagName: ""

    signal toggleTag(int tagId, bool add)
    signal createTagAndAssign(string category, string name)
    signal closed()

    visible: shown
    z: 100

    // 遮罩（点击空白处关闭）
    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(16 / 255, 22 / 255, 38 / 255, 0.42)

        MouseArea {
            anchors.fill: parent
            onClicked: dlg.closed()
        }
    }

    Rectangle {
        id: card
        anchors.centerIn: parent
        width: Math.min(parent.width - 60, 560)
        // content 的主体是 Flickable（Layout.fillHeight），它自身没有 implicitHeight，
        // 只按 content.implicitHeight 算高度会把卡片压成一条（标签区和按钮全被挤没）。
        // 这里直接按可用高度给一个足够大的高度，内部用 Flickable 滚动。
        height: Math.min(parent.height - 60, Math.max(360, content.implicitHeight + 44))
        radius: 18
        color: "white"
        border.width: 1
        border.color: Theme.line

        ColumnLayout {
            id: content
            anchors.fill: parent
            anchors.margins: 22
            spacing: 14

            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                Text {
                    Layout.fillWidth: true
                    text: qsTr("设置标签")
                    color: Theme.txt
                    font.pixelSize: 16
                    font.bold: true
                }
                Text {
                    text: dlg.videoIds.length + qsTr(" 个视频")
                    color: Theme.soft
                    font.pixelSize: 12
                }
                // 右上角关闭：不走 dlg.shown，交给外部改 tagDialogShown
                Rectangle {
                    implicitWidth: 28
                    implicitHeight: 28
                    radius: 9
                    color: closeHover.containsMouse ? "#ffe9e9" : Theme.panel2

                    Text {
                        anchors.centerIn: parent
                        text: "✕"
                        color: closeHover.containsMouse ? Theme.danger : Theme.sub
                        font.pixelSize: 13
                    }
                    MouseArea {
                        id: closeHover
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: dlg.closed()
                    }
                }
            }

            Flickable {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                contentHeight: groups.implicitHeight
                boundsBehavior: Flickable.StopAtBounds

                ColumnLayout {
                    id: groups
                    width: parent.width
                    spacing: 14

                    Repeater {
                        model: [
                            { cat: "shot", title: qsTr("镜头内容") },
                            { cat: "product", title: qsTr("产品") },
                            { cat: "availability", title: qsTr("可用性") }
                        ]
                        delegate: ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 7

                            Text {
                                Layout.fillWidth: true
                                text: modelData.title
                                color: Theme.sub
                                font.pixelSize: 12
                                font.bold: true
                            }

                            Flow {
                                Layout.fillWidth: true
                                spacing: 7

                                Repeater {
                                    model: dlg.categories[modelData.cat] || []
                                    delegate: Rectangle {
                                        property bool on: dlg.commonTags.indexOf(modelData.id) >= 0
                                        implicitWidth: chipLabel.implicitWidth + 20
                                        implicitHeight: 28
                                        radius: Theme.radiusSm
                                        color: on ? Theme.brandSoft : "white"
                                        border.width: 1
                                        border.color: on ? Theme.brand : Theme.line

                                        Text {
                                            id: chipLabel
                                            anchors.centerIn: parent
                                            text: modelData.name
                                            color: on ? Theme.brandDark : Theme.txt
                                            font.pixelSize: 12
                                        }
                                        MouseArea {
                                            anchors.fill: parent
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: dlg.toggleTag(modelData.id, !on)
                                        }
                                    }
                                }

                                // 这里原来有个「＋ 新标签」入口，按需求已去掉：
                                // 标签统一在别处维护，打标弹层只负责勾选已有标签。
                            }
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                Button {
                    implicitWidth: 96
                    implicitHeight: 34
                    text: qsTr("完成")
                    onClicked: dlg.closed()

                    contentItem: Text {
                        text: parent.text
                        color: "white"
                        font.pixelSize: 13
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        radius: Theme.radiusSm
                        gradient: Gradient {
                            GradientStop { position: 0; color: Theme.brand }
                            GradientStop { position: 1; color: Theme.brand2 }
                        }
                    }
                }
            }
        }
    }
}
