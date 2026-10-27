import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3

import StreamingPreferences 1.0
import StreamProfiles 1.0

NavigableDialog {
    id: profilesDialog

    property string hostUuid
    property string hostName
    property var profiles: []

    title: qsTr("Stream profiles for %1").arg(hostName)
    standardButtons: Dialog.Close
    width: Math.min(parent.width - 40, 760)

    function reload() {
        profiles = StreamProfiles.profilesForHost(hostUuid)
    }

    function store(newProfiles) {
        StreamProfiles.setProfilesForHost(hostUuid, hostName, newProfiles)
        reload()
    }

    function displayModeText(mode) {
        switch (mode) {
        case "fullscreen": return qsTr("Fullscreen")
        case "borderless": return qsTr("Borderless windowed")
        case "windowed": return qsTr("Windowed")
        }
        return qsTr("Default display mode")
    }

    function describe(profile) {
        var text = qsTr("%1x%2 at %3 FPS, %4 Mbps, %5")
                       .arg(profile.width).arg(profile.height).arg(profile.fps)
                       .arg((profile.bitrateKbps / 1000).toFixed(1))
                       .arg(displayModeText(profile.displayMode))
        if (profile.screen) {
            text += qsTr(", on %1").arg(profile.screen)
        }
        if (profile.allScreens) {
            text += qsTr(", starts %1 with all screens").arg(profile.app)
        }
        return text
    }

    function editProfile(row) {
        profileEditor.row = row
        profileEditor.open()
    }

    function removeProfile(row) {
        if (row >= 0 && row < profiles.length) {
            var newProfiles = profiles.slice()
            newProfiles.splice(row, 1)
            store(newProfiles)
        }
    }

    function makeDefault(row) {
        if (row > 0 && row < profiles.length) {
            var newProfiles = profiles.slice()
            var profile = newProfiles.splice(row, 1)[0]
            newProfiles.unshift(profile)
            store(newProfiles)
            profileList.currentIndex = 0
        }
    }

    onOpened: {
        reload()
        profileList.currentIndex = profiles.length > 0 ? 0 : -1
        if (profiles.length > 0) {
            profileList.forceActiveFocus()
        }
        else {
            addButton.forceActiveFocus()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: profiles.length > 0 ?
                      qsTr("Clicking an app streams with the first profile. All profiles are in the app's menu.") :
                      qsTr("No profiles yet: apps stream with the settings from the Settings page.")
        }

        ListView {
            id: profileList
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(contentHeight, 320)
            visible: profiles.length > 0
            clip: true
            focus: true
            activeFocusOnTab: true
            keyNavigationEnabled: true
            model: profiles

            delegate: ItemDelegate {
                width: ListView.view.width
                highlighted: ListView.isCurrentItem
                text: (index === 0 ? "★ " : "") + modelData.name + "\n" + describe(modelData)
                onClicked: {
                    profileList.currentIndex = index
                    editProfile(index)
                }
            }

            Keys.onReturnPressed: editProfile(currentIndex)
            Keys.onEnterPressed: editProfile(currentIndex)
            Keys.onDeletePressed: removeProfile(currentIndex)
            // The Mac keyboard's delete key is Backspace
            Keys.onPressed: function(event) {
                if (event.key === Qt.Key_Backspace) {
                    removeProfile(currentIndex)
                    event.accepted = true
                }
            }
        }

        RowLayout {
            spacing: 10

            Button {
                id: addButton
                text: qsTr("Add")
                onClicked: editProfile(-1)
            }

            Button {
                text: qsTr("Edit")
                enabled: profileList.currentIndex >= 0
                onClicked: editProfile(profileList.currentIndex)
            }

            Button {
                text: qsTr("Remove")
                enabled: profileList.currentIndex >= 0
                onClicked: removeProfile(profileList.currentIndex)
            }

            Button {
                text: qsTr("Make default")
                enabled: profileList.currentIndex > 0
                onClicked: makeDefault(profileList.currentIndex)
            }
        }
    }

    NavigableDialog {
        id: profileEditor

        // Row being edited, -1 for a new profile
        property int row: -1
        property var screens: []

        title: row < 0 ? qsTr("New stream profile") : qsTr("Edit stream profile")
        standardButtons: Dialog.Save | Dialog.Cancel
        width: Math.min(parent.width - 40, 620)

        function windowModeName(mode) {
            if (mode === StreamingPreferences.WM_WINDOWED) {
                return "windowed"
            }
            else if (mode === StreamingPreferences.WM_FULLSCREEN_DESKTOP) {
                return "borderless"
            }
            return "fullscreen"
        }

        function selectedScreen() {
            var name = screenBox.currentIndex > 0 ? screens[screenBox.currentIndex - 1].name : window.screen.name
            for (var i = 0; i < screens.length; i++) {
                if (screens[i].name === name) {
                    return screens[i]
                }
            }
            return null
        }

        function applyScreenSize(usePixels) {
            var screen = selectedScreen()
            if (screen !== null) {
                widthBox.value = usePixels ? screen.nativeWidth : screen.width
                heightBox.value = usePixels ? screen.nativeHeight : screen.height
                if (screen.refreshRate > 0) {
                    fpsBox.value = screen.refreshRate
                }
            }
        }

        onOpened: {
            screens = StreamProfiles.screens()
            var profile = row >= 0 ? profilesDialog.profiles[row] : {
                "name": "",
                "width": StreamingPreferences.width,
                "height": StreamingPreferences.height,
                "fps": StreamingPreferences.fps,
                "bitrateKbps": StreamingPreferences.bitrateKbps,
                "displayMode": windowModeName(StreamingPreferences.windowMode),
                "screen": "",
                "app": "Desktop",
                "allScreens": false
            }

            nameField.text = profile.name
            widthBox.value = profile.width
            heightBox.value = profile.height
            fpsBox.value = profile.fps
            bitrateBox.value = profile.bitrateKbps
            displayModeBox.currentIndex = Math.max(0, ["fullscreen", "borderless", "windowed"].indexOf(profile.displayMode))
            appBox.editText = profile.app ? profile.app : "Desktop"
            allScreensBox.checked = profile.allScreens === true || profile.allScreens === "true"

            screenBox.currentIndex = 0
            for (var i = 0; i < screens.length; i++) {
                if (screens[i].name === profile.screen) {
                    screenBox.currentIndex = i + 1
                }
            }

            nameField.forceActiveFocus()
        }

        onAccepted: {
            var profile = {
                "name": nameField.text.trim() !== "" ? nameField.text.trim() :
                                                      qsTr("%1x%2").arg(widthBox.value).arg(heightBox.value),
                "width": widthBox.value,
                "height": heightBox.value,
                "fps": fpsBox.value,
                "bitrateKbps": bitrateBox.value,
                "displayMode": ["fullscreen", "borderless", "windowed"][displayModeBox.currentIndex],
                "screen": screenBox.currentIndex > 0 ? screens[screenBox.currentIndex - 1].name : "",
                "app": appBox.editText.trim() !== "" ? appBox.editText.trim() : "Desktop",
                "allScreens": allScreensBox.checked
            }

            var newProfiles = profilesDialog.profiles.slice()
            if (row >= 0) {
                newProfiles[row] = profile
            }
            else {
                newProfiles.push(profile)
            }
            profilesDialog.store(newProfiles)
            profileList.currentIndex = row >= 0 ? row : newProfiles.length - 1
        }

        GridLayout {
            anchors.fill: parent
            columns: 2
            columnSpacing: 10
            rowSpacing: 8

            Label { text: qsTr("Name:") }
            TextField {
                id: nameField
                Layout.fillWidth: true
                placeholderText: qsTr("e.g. MacBook display")
                Keys.onReturnPressed: profileEditor.accept()
                Keys.onEnterPressed: profileEditor.accept()
            }

            Label { text: qsTr("Screen:") }
            ComboBox {
                id: screenBox
                Layout.fillWidth: true
                model: {
                    var entries = [qsTr("The screen the Artemis window is on")]
                    for (var i = 0; i < profileEditor.screens.length; i++) {
                        var screen = profileEditor.screens[i]
                        entries.push(qsTr("%1 (%2x%3, %4x%5 pixels)").arg(screen.name)
                                     .arg(screen.width).arg(screen.height)
                                     .arg(screen.nativeWidth).arg(screen.nativeHeight))
                    }
                    return entries
                }
            }

            Label { text: qsTr("Resolution:") }
            RowLayout {
                spacing: 6
                SpinBox {
                    id: widthBox
                    editable: true
                    from: 320
                    to: 7680
                }
                Label { text: "x" }
                SpinBox {
                    id: heightBox
                    editable: true
                    from: 200
                    to: 4320
                }
            }

            Item { width: 1; height: 1 }
            RowLayout {
                spacing: 6
                Button {
                    text: qsTr("Use screen size")
                    onClicked: profileEditor.applyScreenSize(false)

                    ToolTip.text: qsTr("The size the screen's desktop is laid out at")
                    ToolTip.delay: 1000
                    ToolTip.visible: hovered
                }
                Button {
                    text: qsTr("Use screen pixels")
                    onClicked: profileEditor.applyScreenSize(true)

                    ToolTip.text: qsTr("Every physical pixel of the screen, for Retina and other high-DPI screens")
                    ToolTip.delay: 1000
                    ToolTip.visible: hovered
                }
            }

            Label { text: qsTr("Frame rate:") }
            SpinBox {
                id: fpsBox
                editable: true
                from: 10
                to: 480
            }

            Label { text: qsTr("Bitrate (Kbps):") }
            RowLayout {
                spacing: 6
                SpinBox {
                    id: bitrateBox
                    editable: true
                    from: 500
                    to: 500000
                    stepSize: 1000
                }
                Button {
                    text: qsTr("Default for this resolution")
                    onClicked: bitrateBox.value = StreamingPreferences.getDefaultBitrate(widthBox.value, heightBox.value,
                                                                                         fpsBox.value, false)
                }
            }

            Label { text: qsTr("Display mode:") }
            ComboBox {
                id: displayModeBox
                Layout.fillWidth: true
                model: [qsTr("Fullscreen"), qsTr("Borderless windowed"), qsTr("Windowed")]
            }

            Item { width: 1; height: 1 }
            CheckBox {
                id: allScreensBox
                text: qsTr("Start with \"Stream on all screens\"")
                enabled: screenBox.currentIndex > 0

                ToolTip.text: qsTr("Needs a pinned screen: profiles started together each go to their own screen")
                ToolTip.delay: 1000
                ToolTip.visible: hovered
            }

            Label {
                text: qsTr("App:")
                enabled: allScreensBox.checked
            }
            ComboBox {
                id: appBox
                Layout.fillWidth: true
                editable: true
                enabled: allScreensBox.checked
                model: ["Desktop", "Virtual Display"]
            }
        }
    }
}
