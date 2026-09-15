import QtQuick

// 开关（例：「仅显示有标签」）。滑块位置、底色都做补间，点起来有反馈。
Rectangle {
    id: tg

    property bool checked: false

    signal toggled(bool on)

    implicitWidth: 42
    implicitHeight: 24
    radius: height / 2
    color: checked ? Theme.brand : "#dfe5f2"

    Behavior on color { ColorAnimation { duration: Theme.durBase } }

    Rectangle {
        width: 18
        height: 18
        y: 3
        x: tg.checked ? tg.width - width - 3 : 3
        radius: 9
        color: Theme.panel

        Behavior on x { NumberAnimation { duration: Theme.durBase; easing.type: Easing.OutCubic } }
    }

    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        // 只发信号、不自己改 checked：让父层的绑定继续驱动显示。
        // 命令式赋值会打断绑定，之后父层再改状态开关就不跟手了。
        onClicked: tg.toggled(!tg.checked)
    }
}
