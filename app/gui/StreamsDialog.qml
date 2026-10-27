import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3

import StreamProcessManager 1.0

NavigableDialog {
    id: streamsDialog

    title: qsTr("Running streams")
    standardButtons: Dialog.Close
    width: Math.min(parent.width - 40, 760)

    function stateText(state, detail) {
        switch (state) {
        case "starting": return qsTr("Starting")
        case "connecting": return qsTr("Connecting")
        case "waiting": return qsTr("Waiting for %1 to quit").arg(detail)
        case "streaming": return qsTr("Streaming")
        case "ended": return qsTr("Ended")
        case "failed": return qsTr("Failed: %1").arg(detail)
        }
        return state
    }

    onOpened: {
        streamList.currentIndex = StreamProcessManager.count > 0 ? 0 : -1
        streamList.forceActiveFocus()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        Label {
            visible: StreamProcessManager.count === 0
            text: qsTr("No streams are running.")
        }

        ListView {
            id: streamList
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(contentHeight, 360)
            visible: StreamProcessManager.count > 0
            clip: true
            focus: true
            keyNavigationEnabled: true
            model: StreamProcessManager

            delegate: ItemDelegate {
                width: ListView.view.width
                highlighted: ListView.isCurrentItem
                onClicked: streamList.currentIndex = index
                onDoubleClicked: StreamProcessManager.raise(index)

                contentItem: RowLayout {
                    spacing: 10

                    Label {
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        text: qsTr("%1 on %2").arg(model.appName).arg(model.hostName) +
                              (model.profileName ? " · " + model.profileName : "") + "\n" +
                              stateText(model.state, model.detail)
                    }

                    Button {
                        text: qsTr("Show")
                        focusPolicy: Qt.NoFocus
                        onClicked: StreamProcessManager.raise(index)
                    }

                    Button {
                        text: qsTr("Stop")
                        focusPolicy: Qt.NoFocus
                        onClicked: StreamProcessManager.stop(index)
                    }
                }
            }

            Keys.onReturnPressed: StreamProcessManager.raise(currentIndex)
            Keys.onEnterPressed: StreamProcessManager.raise(currentIndex)
            Keys.onDeletePressed: StreamProcessManager.stop(currentIndex)
            // The Mac keyboard's delete key is Backspace
            Keys.onPressed: function(event) {
                if (event.key === Qt.Key_Backspace) {
                    StreamProcessManager.stop(currentIndex)
                    event.accepted = true
                }
            }
        }

        Label {
            visible: StreamProcessManager.count > 0
            font.pointSize: 10
            opacity: 0.7
            text: qsTr("Return shows the selected stream, Delete stops it.")
        }
    }
}
