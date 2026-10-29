import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Controls.Material 2.2

import AppModel 1.0
import ComputerManager 1.0
import SdlGamepadKeyNavigation 1.0
import StreamProcessManager 1.0
import StreamProfiles 1.0

// One row per stream profile of the host, each with the host's apps: clicking an app streams it
// with that row's profile. A host without profiles gets one row without a title, which streams
// with the global settings.
ListView {
    property int computerIndex
    property AppModel appModel : createModel()
    property bool activated
    property bool showHiddenGames
    property bool showGames
    property var hostProfiles: StreamProfiles.profilesForHost(appModel.getComputerUuid())
    property int appCount: 0

    Connections {
        target: StreamProfiles
        function onProfilesChanged(hostUuid) {
            if (hostUuid === appModel.getComputerUuid()) {
                hostProfiles = StreamProfiles.profilesForHost(hostUuid)
            }
        }
    }

    // Each stream runs in its own process, so this window stays open while it streams.
    function startStream(appName, profile) {
        StreamProcessManager.start(appModel.getComputerUuid(), appModel.getComputerName(), appName,
                                   profile ? profile : {})
    }

    function describeProfile(profile) {
        return qsTr("%1  %2x%3 at %4 FPS").arg(profile.name).arg(profile.width).arg(profile.height).arg(profile.fps)
    }

    // Arrow keys leave a row's grid at its top or bottom edge for the same column of the next row
    // Called from the rows' delegates, where the list's members are not in scope unqualified
    function focusRow(rowIndex, column, fromBelow) {
        var row = profileRows.itemAtIndex(rowIndex)
        if (row) {
            var columns = Math.max(1, row.grid.itemsPerRow)
            var lastRowStart = Math.floor((row.grid.count - 1) / columns) * columns
            row.grid.currentIndex = Math.min(row.grid.count - 1, (fromBelow ? lastRowStart : 0) + column)
            profileRows.currentIndex = rowIndex
            profileRows.positionViewAtIndex(rowIndex, ListView.Contain)
        }
    }

    id: profileRows
    focus: true
    activeFocusOnTab: true
    // The rows' grids take the arrow keys; the list itself only scrolls to the focused row
    keyNavigationEnabled: false
    topMargin: 20
    bottomMargin: 5
    spacing: 10
    model: hostProfiles.length > 0 ? hostProfiles : [null]

    function computerLost()
    {
        // Go back to the PC view on PC loss
        stackView.pop()
    }

    StackView.onActivated: {
        appModel.computerLost.connect(computerLost)
        activated = true

        // Attached handlers do not see the list's members unqualified
        var firstRow = profileRows.itemAtIndex(0)

        // Highlight the first item, so Enter launches it without an arrow key first
        if (firstRow && firstRow.grid.currentIndex == -1) {
            firstRow.grid.currentIndex = 0
        }

        if (firstRow && !showGames && !showHiddenGames) {
            // Check if there's a direct launch app
            var directLaunchAppIndex = appModel.getDirectLaunchAppIndex();
            if (directLaunchAppIndex >= 0) {
                // Start the direct launch app with the first row's profile if nothing else is running
                firstRow.grid.currentIndex = directLaunchAppIndex
                firstRow.grid.currentItem.launchOrResumeSelectedApp(false)

                // Set showGames so we will not loop when the stream ends
                showGames = true
            }
        }
    }

    StackView.onDeactivating: {
        appModel.computerLost.disconnect(computerLost)
        activated = false
    }

    function createModel()
    {
        var model = Qt.createQmlObject('import AppModel 1.0; AppModel {}', parent, '')
        model.initialize(ComputerManager, computerIndex, showHiddenGames)
        return model
    }

    // A focus scope per row: the list gives focus to its current row, the row to its grid
    delegate: FocusScope {
        id: profileRow
        property var profile: modelData
        property int rowIndex: index
        property alias grid: appGrid

        width: profileRows.width
        height: (rowTitle.visible ? rowTitle.height + 5 : 0) + appGrid.height

        Label {
            id: rowTitle
            visible: profileRow.profile !== null
            text: profileRow.profile ? describeProfile(profileRow.profile) : ""
            font.pointSize: 18
            leftPadding: appGrid.leftMargin + 10
        }

        CenteredGridView {
            id: appGrid
            anchors.top: rowTitle.visible ? rowTitle.bottom : parent.top
            anchors.topMargin: rowTitle.visible ? 5 : 0
            width: parent.width
            height: contentHeight
            focus: true
            // The list scrolls, not the row; keyNavigationEnabled would follow interactive
            interactive: false
            keyNavigationEnabled: true
            cellWidth: 230; cellHeight: 297;
            model: appModel
            currentIndex: -1

            onCountChanged: {
                if (profileRow.rowIndex === 0) {
                    profileRows.appCount = count
                }
            }

            delegate: NavigableItemDelegate {
                width: 220; height: 287;
                grid: appGrid

                property alias appContextMenu: appContextMenuLoader.item
                property alias appNameText: appNameTextLoader.item

                // Dim the app if it's hidden
                opacity: model.hidden ? 0.4 : 1.0

                leaveUp: profileRow.rowIndex > 0 ? function(current) {
                    profileRows.focusRow(profileRow.rowIndex - 1, current % Math.max(1, appGrid.itemsPerRow), true)
                } : null
                leaveDown: profileRow.rowIndex < profileRows.count - 1 ? function(current) {
                    profileRows.focusRow(profileRow.rowIndex + 1, current % Math.max(1, appGrid.itemsPerRow), false)
                } : null

                Image {
                    property bool isPlaceholder: false

                    id: appIcon
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: 10
                    source: model.boxart

                    onSourceSizeChanged: {
                        // Nearly all of Nvidia's official box art does not match the dimensions of placeholder
                        // images, however the one known exception is Overcooked. Therefore, we only execute
                        // the image size checks if this is not an app collector game. We know the officially
                        // supported games all have box art, so this check is not required.
                        if (!model.isAppCollectorGame &&
                            ((sourceSize.width == 130 && sourceSize.height == 180) || // GFE 2.0 placeholder image
                             (sourceSize.width == 628 && sourceSize.height == 888) || // GFE 3.0 placeholder image
                             (sourceSize.width == 200 && sourceSize.height == 266)))  // Our no_app_image.png
                        {
                            isPlaceholder = true
                        }
                        else
                        {
                            isPlaceholder = false
                        }

                        width = 200
                        height = 267
                    }

                    // Display a tooltip with the full name if it's truncated
                    ToolTip.text: model.name
                    ToolTip.delay: 1000
                    ToolTip.timeout: 5000
                    ToolTip.visible: (parent.hovered || parent.highlighted) && (!appNameText || appNameText.truncated)
                }

                Loader {
                    active: model.running
                    asynchronous: true
                    anchors.fill: appIcon

                    sourceComponent: Item {
                        RoundButton {
                            anchors.horizontalCenterOffset: appIcon.isPlaceholder ? -47 : 0
                            anchors.verticalCenterOffset: appIcon.isPlaceholder ? -75 : -60
                            anchors.centerIn: parent
                            implicitWidth: 85
                            implicitHeight: 85

                            Image {
                                source: "qrc:/res/play_arrow_FILL1_wght700_GRAD200_opsz48.svg"
                                anchors.centerIn: parent
                                sourceSize {
                                    width: 75
                                    height: 75
                                }
                            }

                            onClicked: {
                                launchOrResumeSelectedApp(true)
                            }

                            ToolTip.text: qsTr("Resume Game")
                            ToolTip.delay: 1000
                            ToolTip.timeout: 3000
                            ToolTip.visible: hovered

                            Material.background: "#D0808080"
                        }

                        RoundButton {
                            anchors.horizontalCenterOffset: appIcon.isPlaceholder ? 47 : 0
                            anchors.verticalCenterOffset: appIcon.isPlaceholder ? -75 : 60
                            anchors.centerIn: parent
                            implicitWidth: 85
                            implicitHeight: 85

                            Image {
                                source: "qrc:/res/stop_FILL1_wght700_GRAD200_opsz48.svg"
                                anchors.centerIn: parent
                                sourceSize {
                                    width: 75
                                    height: 75
                                }
                            }

                            onClicked: {
                                doQuitGame()
                            }

                            ToolTip.text: qsTr("Quit Game")
                            ToolTip.delay: 1000
                            ToolTip.timeout: 3000
                            ToolTip.visible: hovered

                            Material.background: "#D0808080"
                        }
                    }
                }

                Loader {
                    id: appNameTextLoader
                    active: appIcon.isPlaceholder

                    // This loader is not asynchronous to avoid noticeable differences
                    // in the time in which the text loads for each game.

                    width: appIcon.width
                    height: model.running ? 175 : appIcon.height

                    anchors.left: appIcon.left
                    anchors.right: appIcon.right
                    anchors.bottom: appIcon.bottom

                    sourceComponent: Label {
                        id: appNameText
                        text: model.name
                        font.pointSize: 22
                        leftPadding: 20
                        rightPadding: 20
                        verticalAlignment: Text.AlignVCenter
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                        elide: Text.ElideRight
                    }
                }

                function launchOrResumeSelectedApp(quitExistingApp)
                {
                    // When another app is running on the host, the stream process asks
                    // whether to quit it, so there is nothing to check here
                    startStream(model.name, profileRow.profile)
                }

                onClicked: {
                    // Only allow clicking on the box art for non-running games.
                    // For running games, buttons will appear to resume or quit which
                    // will handle starting the game and clicks on the box art will
                    // be ignored.
                    if (!model.running) {
                        launchOrResumeSelectedApp(true)
                    }
                }

                onPressAndHold: {
                    // popup() ensures the menu appears under the mouse cursor
                    if (appContextMenu.popup) {
                        appContextMenu.popup()
                    }
                    else {
                        // Qt 5.9 doesn't have popup()
                        appContextMenu.open()
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.RightButton;
                    onClicked: {
                        parent.pressAndHold()
                    }
                }

                Keys.onReturnPressed: {
                    // Open the app context menu if activated via the gamepad or keyboard
                    // for running games. If the game isn't running, the above onClicked
                    // method will handle the launch.
                    if (model.running) {
                        // This will be keyboard/gamepad driven so use
                        // open() instead of popup()
                        appContextMenu.open()
                    }
                }

                Keys.onEnterPressed: {
                    // Open the app context menu if activated via the gamepad or keyboard
                    // for running games. If the game isn't running, the above onClicked
                    // method will handle the launch.
                    if (model.running) {
                        // This will be keyboard/gamepad driven so use
                        // open() instead of popup()
                        appContextMenu.open()
                    }
                }

                Keys.onMenuPressed: {
                    // This will be keyboard/gamepad driven so use open() instead of popup()
                    appContextMenu.open()
                }

                function doQuitGame() {
                    quitAppDialog.appName = appModel.getRunningAppName()
                    quitAppDialog.open()
                }

                Loader {
                    id: appContextMenuLoader
                    asynchronous: true
                    sourceComponent: NavigableMenu {
                        id: appContextMenu

                        NavigableMenuItem {
                            parentMenu: appContextMenu
                            text: model.running ? qsTr("Resume Game") : qsTr("Launch Game")
                            onTriggered: launchOrResumeSelectedApp(true)
                        }
                        NavigableMenuItem {
                            parentMenu: appContextMenu
                            text: qsTr("Quit Game")
                            onTriggered: doQuitGame()
                            visible: model.running
                        }
                        NavigableMenuItem {
                            parentMenu: appContextMenu
                            checkable: true
                            checked: model.directLaunch
                            text: qsTr("Direct Launch")
                            onTriggered: appModel.setAppDirectLaunch(model.index, !model.directLaunch)
                            enabled: !model.hidden

                            ToolTip.text: qsTr("Launch this app immediately when the host is selected, bypassing the app selection grid.")
                            ToolTip.delay: 1000
                            ToolTip.timeout: 3000
                            ToolTip.visible: hovered
                        }
                        NavigableMenuItem {
                            parentMenu: appContextMenu
                            checkable: true
                            checked: model.hidden
                            text: qsTr("Hide Game")
                            onTriggered: appModel.setAppHidden(model.index, !model.hidden)
                            enabled: model.hidden || (!model.running && !model.directLaunch)

                            ToolTip.text: qsTr("Hide this game from the app grid. To access hidden games, right-click on the host and choose %1.").arg(qsTr("View All Apps"))
                            ToolTip.delay: 1000
                            ToolTip.timeout: 5000
                            ToolTip.visible: hovered
                        }
                    }
                }
            }
        }
    }

    Row {
        anchors.centerIn: parent
        spacing: 5
        visible: appCount === 0

        Label {
            text: qsTr("This computer doesn't seem to have any applications or some applications are hidden")
            font.pointSize: 20
            verticalAlignment: Text.AlignVCenter
            wrapMode: Text.Wrap
        }
    }

    NavigableMessageDialog {
        id: quitAppDialog
        property string appName : ""
        text:qsTr("Are you sure you want to quit %1? Any unsaved progress will be lost.").arg(appName)
        standardButtons: Dialog.Yes | Dialog.No

        function quitApp() {
            var component = Qt.createComponent("QuitSegue.qml")
            var params = {"appName": appName, "quitRunningAppFn": function() { appModel.quitRunningApp() },
                          "nextAppName": null, "nextSession": null}
            stackView.push(component.createObject(stackView, params))
        }

        onAccepted: quitApp()
    }

    ScrollBar.vertical: ScrollBar {}
}
