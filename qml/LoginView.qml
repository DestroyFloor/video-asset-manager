import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import Workbench

// 对应旧版 renderer/index.html 的 #loginOverlay
Item {
    id: view

    FolderDialog {
        id: folderDialog
        title: qsTr("选择工作区文件夹")
        // 交给 C++ 的 QUrl 去解析，不要自己按字符串截：
        // 共享盘的 URL 是 file://server/share/…（file:// 后面直接跟服务器名，没有第三个斜杠），
        // 用 substring(7) 截出来会变成 server\share\…，开头的 \\ 丢了 —— 选共享盘就会「连不上」。
        onAccepted: {
            const p = Library.pathFromUrl(selectedFolder)
            if (p !== "")
                workspaceField.text = p
        }
    }

    component LoginField: TextField {
        id: field
        color: Theme.txt
        font.pixelSize: 14
        placeholderTextColor: Theme.soft
        leftPadding: 13
        rightPadding: 13
        background: Rectangle {
            radius: 10
            color: field.activeFocus ? "#ffffff" : "#fbfcfe"
            border.width: 1.5
            border.color: field.activeFocus ? Theme.brand : Theme.line
        }
    }

    Rectangle {
        id: card
        anchors.centerIn: parent
        width: 400
        radius: 24
        color: Qt.rgba(1, 1, 1, 0.86)
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, 0.6)
        implicitHeight: content.implicitHeight + 68

        ColumnLayout {
            id: content
            anchors.fill: parent
            anchors.margins: 34
            spacing: 0

            Text {
                Layout.alignment: Qt.AlignHCenter
                text: "🎬"
                font.pixelSize: 40
            }
            Text {
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: 8
                text: qsTr("定制视频分类工作台")
                color: Theme.txt
                font.pixelSize: 21
                font.bold: true
            }
            Text {
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: 2
                Layout.bottomMargin: 20
                text: qsTr("连接共享素材库")
                color: Theme.sub
                font.pixelSize: 13
            }

            Text {
                text: qsTr("账号")
                color: Theme.sub
                font.pixelSize: 12
            }
            LoginField {
                id: accountField
                Layout.fillWidth: true
                Layout.topMargin: 6
                placeholderText: qsTr("请输入账号")
                text: App.account
            }

            Text {
                Layout.topMargin: 12
                text: qsTr("账号密码（云盘/共享接入）")
                color: Theme.sub
                font.pixelSize: 12
            }
            LoginField {
                id: passwordField
                Layout.fillWidth: true
                Layout.topMargin: 6
                placeholderText: qsTr("登录密码")
                echoMode: TextInput.Password
                onAccepted: loginClick()
            }

            Text {
                Layout.topMargin: 12
                text: qsTr("工作区文件夹")
                color: Theme.sub
                font.pixelSize: 12
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 6
                spacing: 8

                LoginField {
                    id: workspaceField
                    Layout.fillWidth: true
                    placeholderText: qsTr("点击「浏览」选择工作区文件夹")
                    text: App.shareRoot
                }
                Button {
                    Layout.preferredHeight: workspaceField.implicitHeight
                    Layout.preferredWidth: 76
                    text: qsTr("浏览…")
                    onClicked: folderDialog.open()

                    contentItem: Text {
                        text: parent.text
                        color: Theme.brandDark
                        font.pixelSize: 14
                        font.bold: true
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        radius: 10
                        color: "#ffffff"
                        border.width: 1.5
                        border.color: Theme.line
                    }
                }
            }

            Text {
                Layout.fillWidth: true
                Layout.topMargin: 10
                text: qsTr("本地文件夹直接用；若是云盘/共享路径，请填入账号密码接入。")
                color: Theme.sub
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }

            Row {
                Layout.topMargin: 12
                spacing: 7
                CheckBox {
                    id: rememberBox
                    checked: App.rememberAccount
                    indicator: Rectangle {
                        implicitWidth: 16
                        implicitHeight: 16
                        radius: 4
                        anchors.verticalCenter: parent.verticalCenter
                        color: rememberBox.checked ? Theme.brand : "#ffffff"
                        border.width: 1.5
                        border.color: rememberBox.checked ? Theme.brand : Theme.line
                        Text {
                            anchors.centerIn: parent
                            visible: rememberBox.checked
                            text: "✓"
                            color: "white"
                            font.pixelSize: 11
                        }
                    }
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("记住账号")
                    color: Theme.sub
                    font.pixelSize: 13
                }
            }

            Button {
                id: loginButton
                Layout.fillWidth: true
                Layout.topMargin: 8
                implicitHeight: 42
                enabled: !App.busy
                text: App.busy ? qsTr("连接中…") : qsTr("连接并登录")
                onClicked: loginClick()

                contentItem: Text {
                    text: loginButton.text
                    color: "white"
                    font.pixelSize: 14
                    font.bold: true
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    radius: 11
                    opacity: loginButton.enabled ? 1 : 0.55
                    gradient: Gradient {
                        GradientStop { position: 0; color: Theme.brand }
                        GradientStop { position: 1; color: Theme.brand2 }
                    }
                }
            }

            Text {
                Layout.fillWidth: true
                Layout.topMargin: 12
                horizontalAlignment: Text.AlignHCenter
                visible: view.message !== ""
                text: view.message
                color: Theme.danger
                font.pixelSize: 13
                wrapMode: Text.WordWrap
            }
        }
    }

    property string message: ""

    function loginClick() {
        view.message = ""
        if (workspaceField.text.trim() === "") {
            view.message = qsTr("请先选择工作区文件夹")
            return
        }
        App.login(accountField.text, passwordField.text, workspaceField.text, rememberBox.checked)
    }

    Connections {
        target: App
        function onLoginFinished(ok, msg) {
            if (!ok)
                view.message = msg
        }
    }
}
