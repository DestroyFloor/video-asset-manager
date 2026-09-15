import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 自绘下拉框（替代 Controls 的 ComboBox）。
//
// 为什么不用 ComboBox：Basic 风格下它的弹层是一整块灰色方块、没有圆角/hover/选中态，
// 放在这套浅色渐变界面里非常突兀（截图里那个「默认 / 调用次数 / …」的弹窗就是它）。
// 这里用 Popup 自绘：圆角、淡投影、hover 高亮、当前项打勾，并可带动画展开。
Item {
    id: dd

    property var options: []          // ["默认", "调用次数", …] 或 [{ text, value }]
    property int currentIndex: 0
    property int hPad: 12
    property string prefix: ""        // 前缀说明文字（可选，如「排序」）

    signal activated(int index)

    implicitWidth: 138
    implicitHeight: 34

    function labelAt(i) {
        const o = dd.options[i]
        if (o === undefined || o === null)
            return ""
        return (typeof o === "object") ? String(o.text) : String(o)
    }

    Rectangle {
        id: head
        anchors.fill: parent
        radius: Theme.radiusSm
        color: Theme.panel
        border.width: 1
        border.color: pop.visible ? Theme.brand : (headHover.containsMouse ? Theme.brand : Theme.line)

        Behavior on border.color { ColorAnimation { duration: Theme.durBase } }

        // 用 RowLayout 而不是 Row：Row 里给 Text 写死 width 的话，
        // 一旦带上 prefix（如「排序」）总宽就超出头部，右侧箭头会被挤到框外。
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: dd.hPad
            anchors.rightMargin: dd.hPad
            spacing: 6

            Text {
                Layout.alignment: Qt.AlignVCenter
                visible: dd.prefix !== ""
                text: dd.prefix
                color: Theme.soft
                font.pixelSize: 11
            }
            Text {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
                text: dd.labelAt(dd.currentIndex)
                color: Theme.txt
                font.pixelSize: 12
                elide: Text.ElideRight
            }
            Text {
                Layout.alignment: Qt.AlignVCenter
                text: "▾"
                color: pop.visible ? Theme.brand : Theme.soft
                font.pixelSize: 11
            }
        }

        MouseArea {
            id: headHover
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: pop.visible ? pop.close() : pop.open()
        }
    }

    Popup {
        id: pop
        parent: dd
        y: dd.height + 6
        width: Math.max(dd.width, 132)
        implicitHeight: list.implicitHeight + 12
        padding: 6
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        transformOrigin: Item.Top

        enter: Transition {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durFast }
            NumberAnimation { property: "scale"; from: 0.96; to: 1; duration: Theme.durBase; easing.type: Easing.OutCubic }
        }
        exit: Transition {
            NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 90 }
        }

        background: Item {
            // 伪投影：往下错开一点再画一层半透明圆角块
            Rectangle {
                anchors.fill: parent
                anchors.topMargin: 4
                anchors.leftMargin: 1
                anchors.rightMargin: 1
                radius: 13
                color: Theme.shadow
            }
            Rectangle {
                anchors.fill: parent
                anchors.bottomMargin: 4
                radius: 13
                color: Theme.panel
                border.width: 1
                border.color: Theme.line
            }
        }

        contentItem: Column {
            id: list
            spacing: 2

            Repeater {
                model: dd.options

                delegate: Rectangle {
                    id: row
                    required property int index
                    required property var modelData

                    width: list.width
                    height: 32
                    radius: 9
                    color: (rowHover.containsMouse || dd.currentIndex === index) ? Theme.brandSoft : "transparent"

                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        text: (typeof row.modelData === "object") ? String(row.modelData.text)
                                                                   : String(row.modelData)
                        color: dd.currentIndex === row.index ? Theme.brandDark : Theme.txt
                        font.pixelSize: 12
                        font.bold: dd.currentIndex === row.index
                    }
                    Text {
                        anchors.right: parent.right
                        anchors.rightMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        visible: dd.currentIndex === row.index
                        text: "✓"
                        color: Theme.brand
                        font.pixelSize: 11
                    }

                    MouseArea {
                        id: rowHover
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            // 只发信号：currentIndex 交给使用方的绑定决定。
                            // 这里命令式赋值会把绑定打断，之后父层再改排序就不跟手了。
                            dd.activated(row.index)
                            pop.close()
                        }
                    }
                }
            }
        }
    }
}
