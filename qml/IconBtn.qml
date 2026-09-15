import QtQuick
import QtQuick.Controls

// 方形图标按钮（顶部工具条 / 播放器控制条通用）。
// hover 出底色、按下回弹，可选常亮（active）与危险色（danger）。
Rectangle {
    id: ib

    property string glyph: ""
    property string tip: ""
    property bool active: false
    property bool danger: false
    property bool small: false

    signal clicked()

    implicitWidth: small ? 28 : 34
    implicitHeight: small ? 28 : 34
    radius: small ? 9 : 10
    scale: press.containsPress ? 0.92 : 1
    color: press.containsMouse ? (danger ? Theme.dangerSoft : Theme.panel3)
                               : (active ? Theme.brandSoft : "transparent")

    Behavior on scale { NumberAnimation { duration: Theme.durFast; easing.type: Easing.OutCubic } }
    Behavior on color { ColorAnimation { duration: Theme.durBase } }

    Text {
        anchors.centerIn: parent
        text: ib.glyph
        font.pixelSize: ib.small ? 13 : 15
        color: ib.danger ? Theme.danger : (ib.active ? Theme.brandDark : Theme.sub)
    }

    MouseArea {
        id: press
        anchors.fill: parent
        hoverEnabled: true
        enabled: ib.enabled
        cursorShape: Qt.PointingHandCursor
        onClicked: ib.clicked()
    }

    ToolTip.visible: ib.tip !== "" && press.containsMouse
    ToolTip.delay: 520
    ToolTip.text: ib.tip
}
