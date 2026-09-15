import QtQuick

// 全站统一按钮。四种外观：
//   primary  渐变主按钮（新建分类文件之类的主操作）
//   soft     白底描边（默认，次级操作）
//   ghost    无底色（工具条里的轻量按钮）
//   danger   危险操作（删除）
// 统一了圆角、hover 底色、按下回弹与禁用态，避免各页面各写一套。
Rectangle {
    id: btn

    property string text: ""
    property string glyph: ""          // 文字前的字形，如 "＋"、"🗑"
    property string kind: "soft"
    property int hPad: 15
    property bool small: false

    signal clicked()

    readonly property bool _primary: kind === "primary"
    readonly property bool _danger: kind === "danger"
    readonly property bool _flat: _primary || kind === "ghost"
    readonly property bool _hot: press.containsMouse && enabled

    implicitWidth: content.implicitWidth + hPad * 2
    implicitHeight: small ? 28 : 34
    radius: Theme.radiusSm
    scale: press.containsPress && enabled ? 0.975 : 1
    opacity: enabled ? 1 : 0.42

    color: {
        if (_primary)
            return "transparent"
        if (_danger)
            return _hot ? Theme.danger : Theme.panel
        if (kind === "ghost")
            return _hot ? Theme.panel3 : "transparent"
        return _hot ? Theme.panel2 : Theme.panel
    }
    gradient: (_primary && enabled) ? (_hot ? gradHot : gradNormal) : null
    border.width: _flat ? 0 : 1
    border.color: _danger ? (_hot ? Theme.danger : Theme.dangerLine)
                          : (_hot ? Theme.brand : Theme.line)

    Behavior on scale { NumberAnimation { duration: Theme.durFast; easing.type: Easing.OutCubic } }
    Behavior on color { ColorAnimation { duration: Theme.durBase } }
    Behavior on border.color { ColorAnimation { duration: Theme.durBase } }

    Gradient {
        id: gradNormal
        GradientStop { position: 0; color: Theme.brand }
        GradientStop { position: 1; color: Theme.brand2 }
    }
    Gradient {
        id: gradHot
        GradientStop { position: 0; color: Theme.brandDark }
        GradientStop { position: 1; color: "#7a4df0" }
    }

    Row {
        id: content
        anchors.centerIn: parent
        spacing: btn.glyph !== "" && btn.text !== "" ? 6 : 0

        Text {
            anchors.verticalCenter: parent.verticalCenter
            visible: btn.glyph !== ""
            text: btn.glyph
            color: {
                if (btn._primary)
                    return "white"
                return btn._danger ? (btn._hot ? "white" : Theme.danger) : Theme.txt
            }
            font.pixelSize: btn.small ? 11 : 12
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            visible: btn.text !== ""
            text: btn.text
            color: {
                if (btn._primary)
                    return "white"
                return btn._danger ? (btn._hot ? "white" : Theme.danger) : Theme.txt
            }
            font.pixelSize: btn.small ? 11 : 12
            font.bold: btn._primary
        }
    }

    MouseArea {
        id: press
        anchors.fill: parent
        hoverEnabled: true
        enabled: btn.enabled
        cursorShape: Qt.PointingHandCursor
        onClicked: btn.clicked()
    }
}
