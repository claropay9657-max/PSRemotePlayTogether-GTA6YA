import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC

import org.streetpea.chiaking

import "../controls" as C

// Controllers: the ones connected now (Chiaki.controllers), teaching one its buttons,
// how buttons work, rumble, and the touchpad from the D-pad.
//
// Teaching (the old "Map your controller"): you pick the controller by pressing one of
// its buttons (Chiaki.beginControllerMapping). Then the app opens ControllerMappingDialog
// (root.showControllerMappingDialog) or, for a reset, gives it back the buttons it came
// with. The pop-up for that wait, which also says when Steam has the controller, is at
// the end of this file.
C.SettingsPage {
    id: page
    title: qsTr("Controllers")
    description: qsTr("The controllers on this computer, how their buttons work, and how much they rumble.")

    /** The buttons a combination can use, by Chiaki.settings.dpadTouchShortcut1…4; 0 is none. */
    readonly property var buttonNames: [qsTr("None"), qsTr("Cross"), qsTr("Circle"), qsTr("Square"), qsTr("Triangle"),
        qsTr("D-pad left"), qsTr("D-pad right"), qsTr("D-pad up"), qsTr("D-pad down"),
        "L1", "R1", "L3", "R3", qsTr("Options"), qsTr("Create"), qsTr("Touchpad"), "PS"]
    /** The same on a key cap. */
    readonly property var buttonCaps: ["", "✕", "○", "□", "△", qsTr("D-pad left"), qsTr("D-pad right"), qsTr("D-pad up"),
        qsTr("D-pad down"), "L1", "R1", "L3", "R3", qsTr("Options"), qsTr("Create"), qsTr("Touchpad"), "PS"]
    readonly property var switchButtons: [Chiaki.settings.dpadTouchShortcut1, Chiaki.settings.dpadTouchShortcut2,
        Chiaki.settings.dpadTouchShortcut3, Chiaki.settings.dpadTouchShortcut4].filter((b) => b > 0)
    property bool changingSwitch: false

    /** Chiaki.settings.rumbleHapticsIntensity: 0 off … 5 very strong. */
    readonly property var rumbleNames: [qsTr("Off"), qsTr("Very weak"), qsTr("Weak"), qsTr("Normal"), qsTr("Strong"), qsTr("Very strong")]

    /** A controller the app may not know: one that is not PlayStation's, Steam's or built in. */
    readonly property bool otherPad: {
        const pads = Chiaki.controllers;
        for (let i = 0; i < pads.length; ++i) {
            if (!pads[i].playStation && !pads[i].steamVirtual && !pads[i].handheld)
                return true;
        }
        return false;
    }

    /** What the app knows of a controller (a QmlController): only what it is. */
    function padInfo(pad) {
        let name = qsTr("Controller");
        if (pad.handheld)
            name = qsTr("Built-in controls");
        else if (pad.dualSenseEdge)
            name = qsTr("DualSense Edge");
        else if (pad.dualSense)
            name = qsTr("DualSense");
        else if (pad.playStation)
            name = qsTr("PlayStation controller");
        else if (pad.steamVirtual)
            name = qsTr("Controller through Steam");

        let badges = [];
        if (pad.dualSense || pad.dualSenseEdge)
            badges.push({ text: qsTr("Haptics and triggers"), tone: "good" });
        if (pad.steamVirtual)
            badges.push({ text: qsTr("Steam Input"), tone: "wait" });

        let note = "";
        if (pad.steamVirtual)
            note = qsTr("Its buttons are set in Steam.");
        else if (!pad.playStation && !pad.handheld)
            note = qsTr("If its buttons mix up, teach them below.");

        return { name: name, known: pad.playStation || pad.handheld, badges: badges, note: note };
    }

    /** Another page of the settings window, by its key, as a link: SettingsView.openPage(). */
    function goToPage(key, byKeys) {
        for (let p = page.parent; p; p = p.parent) {
            if (typeof p.openPage === "function") {
                p.openPage(key, "", byKeys);
                return;
            }
        }
    }

    Component.onDestruction: {
        // The window closes while waiting for a button: the controllers stop waiting too
        if (capture.visible && capture.quitMapping)
            Chiaki.controllerMappingQuit();
    }

    // ------------------------------------------------ connected now

    C.SettingsGroup {
        title: qsTr("Connected now")

        Repeater {
            model: Chiaki.controllers

            Item {
                id: padRow
                required property var modelData
                required property int index
                readonly property var info: page.padInfo(modelData)

                Layout.fillWidth: true
                implicitHeight: Math.max(40, padTexts.implicitHeight) + 24

                Rectangle {
                    visible: padRow.index > 0
                    width: parent.width
                    height: 1
                    color: Qt.rgba(1, 1, 1, 0.06)
                }

                RowLayout {
                    anchors {
                        fill: parent
                        leftMargin: 16
                        rightMargin: 18
                    }
                    spacing: 12

                    Rectangle {
                        implicitWidth: 40
                        implicitHeight: 40
                        radius: 12
                        color: padRow.info.known ? Qt.rgba(140 / 255, 82 / 255, 1, 0.16) : Qt.rgba(245 / 255, 185 / 255, 66 / 255, 0.12)

                        C.Icon {
                            anchors.centerIn: parent
                            iconName: "gamepad"
                            size: 22
                            color: padRow.info.known ? "#cdb6ff" : "#fbd98c"
                        }
                    }

                    ColumnLayout {
                        id: padTexts
                        Layout.fillWidth: true
                        spacing: 4

                        Text {
                            Layout.fillWidth: true
                            text: padRow.info.name
                            elide: Text.ElideRight
                            color: C.Theme.text
                            font.pixelSize: 15
                            font.weight: Font.ExtraBold
                        }

                        Flow {
                            Layout.fillWidth: true
                            visible: padRow.info.badges.length > 0
                            spacing: 5

                            Repeater {
                                model: padRow.info.badges

                                C.Badge {
                                    required property var modelData
                                    small: true
                                    text: modelData.text
                                    tone: modelData.tone
                                }
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            visible: text.length > 0
                            text: padRow.info.note
                            wrapMode: Text.WordWrap
                            color: C.Theme.textDim
                            font.pixelSize: 13
                        }
                    }

                    Text {
                        text: qsTr("Ready")
                        color: "#9ff0cc"
                        font.pixelSize: 13
                        font.weight: Font.Bold
                    }
                }
            }
        }

        // None connected
        Item {
            visible: Chiaki.controllers.length === 0
            Layout.fillWidth: true
            implicitHeight: Math.max(40, noneTexts.implicitHeight) + 24

            RowLayout {
                anchors {
                    fill: parent
                    leftMargin: 16
                    rightMargin: 18
                }
                spacing: 12

                Rectangle {
                    implicitWidth: 40
                    implicitHeight: 40
                    radius: 12
                    color: Qt.rgba(1, 1, 1, 0.06)

                    C.Icon {
                        anchors.centerIn: parent
                        iconName: "gamepad"
                        size: 22
                        color: C.Theme.textDim
                    }
                }

                ColumnLayout {
                    id: noneTexts
                    Layout.fillWidth: true
                    spacing: 2

                    Text {
                        Layout.fillWidth: true
                        text: qsTr("No controller connected")
                        wrapMode: Text.WordWrap
                        color: C.Theme.text
                        font.pixelSize: 15
                        font.weight: Font.ExtraBold
                    }

                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Plug one in with a cable or pair it over Bluetooth. The keyboard can be a controller too.")
                        wrapMode: Text.WordWrap
                        color: C.Theme.textDim
                        font.pixelSize: 13
                    }

                    C.TextLink {
                        Layout.topMargin: 2
                        text: qsTr("Keyboard & mouse")
                        iconName: "chevron-right"
                        onClicked: page.goToPage("keyboard", visualFocus)
                    }
                }
            }
        }

        C.SettingsRow {
            title: qsTr("Teach a controller its buttons")
            subtitle: qsTr("For a controller whose buttons mix up: you show it which button is which. Reset gives it back the buttons it came with.")
            keywords: qsTr("map mapping remap change mapping capture layout wrong buttons reset steam input")
            enabled: Chiaki.controllers.length > 0

            C.PillButton {
                id: teachButton
                small: true
                kind: page.otherPad ? "primary" : "ghost"
                text: qsTr("Teach…")
                onClicked: capture.start(false, teachButton)
            }

            C.PillButton {
                id: resetButton
                small: true
                text: qsTr("Reset…")
                onClicked: capture.start(true, resetButton)
            }
        }
    }

    // ------------------------------------------------ buttons and rumble | touchpad from the D-pad

    GridLayout {
        id: split
        readonly property real columnWidth: columns === 2 ? (width - columnSpacing) / 2 : width

        Layout.fillWidth: true
        columns: width >= 740 ? 2 : 1
        columnSpacing: 20
        rowSpacing: 18

        ColumnLayout {
            Layout.alignment: Qt.AlignTop
            Layout.fillWidth: true
            Layout.preferredWidth: split.columnWidth
            spacing: 18

            C.SettingsGroup {
                title: qsTr("Buttons")

                C.SettingsRow {
                    title: qsTr("Also when another window is in front")
                    subtitle: qsTr("Keeps reading the controller in the background.")
                    keywords: qsTr("controller input in the background focus joystick window")
                    textMinimum: 150
                    switchItem: inBackground

                    C.Toggle {
                        id: inBackground
                        Accessible.name: qsTr("Controller input in the background")
                        checked: Chiaki.settings.allowJoystickBackgroundEvents
                        onToggled: Chiaki.settings.allowJoystickBackgroundEvents = checked
                    }
                }

                C.SettingsRow {
                    title: qsTr("Buttons by position")
                    subtitle: qsTr("By where they sit, not their label: for Nintendo-style layouts.")
                    keywords: qsTr("nintendo switch layout swap a b x y face buttons")
                    textMinimum: 150
                    switchItem: byPosition

                    C.Toggle {
                        id: byPosition
                        Accessible.name: qsTr("Buttons by position")
                        checked: Chiaki.settings.buttonsByPosition
                        onToggled: Chiaki.settings.buttonsByPosition = checked
                    }
                }
            }

            C.SettingsGroup {
                title: qsTr("Rumble")

                C.SettingsRow {
                    title: qsTr("Rumble strength")
                    subtitle: qsTr("For controllers without haptics.")
                    keywords: qsTr("vibration vibrate force feedback intensity weak strong off")
                    textMinimum: 150

                    C.ValueSlider {
                        Layout.preferredWidth: 210
                        valueWidth: 80
                        Accessible.name: qsTr("Rumble strength")
                        from: 0
                        to: 5
                        stepSize: 1
                        value: Chiaki.settings.rumbleHapticsIntensity
                        valueText: page.rumbleNames[Math.round(value)] || ""
                        onMoved: Chiaki.settings.rumbleHapticsIntensity = Math.round(value)
                    }
                }

                C.SettingsRow {
                    title: qsTr("DualSense haptics")
                    subtitle: qsTr("Compared to what the console sets.")
                    keywords: qsTr("haptic haptics strength vibration override dualsense")
                    textMinimum: 150

                    C.ValueSlider {
                        Layout.preferredWidth: 210
                        valueWidth: 80
                        Accessible.name: qsTr("DualSense haptics")
                        from: 0
                        to: 2
                        stepSize: 0.1
                        value: Chiaki.settings.hapticOverride
                        valueText: value < 0.005 ? qsTr("Off")
                                 : Math.abs(value - 1) < 0.01 ? qsTr("As set")
                                 : qsTr("%1 %").arg(Math.round(value * 100))
                        onMoved: Chiaki.settings.hapticOverride = Math.round(value * 10) / 10
                    }
                }
            }
        }

        C.SettingsGroup {
            Layout.alignment: Qt.AlignTop
            Layout.preferredWidth: split.columnWidth
            title: qsTr("Touchpad from the D-pad")
            note: qsTr("for controllers without one")

            C.SettingsRow {
                title: qsTr("Swipe with the D-pad")
                subtitle: qsTr("Switch the D-pad to swiping on the touchpad.")
                keywords: qsTr("dpad touchpad touch swipe d-pad touchpad")
                textMinimum: 150
                switchItem: dpadTouch

                C.Toggle {
                    id: dpadTouch
                    Accessible.name: qsTr("Swipe with the D-pad")
                    checked: Chiaki.settings.dpadTouchEnabled
                    onToggled: Chiaki.settings.dpadTouchEnabled = checked
                }
            }

            C.SettingsRow {
                sub: true
                enabled: Chiaki.settings.dpadTouchEnabled
                title: qsTr("Swipe step")
                subtitle: qsTr("How far one press moves.")
                keywords: qsTr("touch increment distance speed dpad")
                textMinimum: 120

                C.ValueSlider {
                    Layout.preferredWidth: 210
                    valueWidth: 80
                    Accessible.name: qsTr("Swipe step")
                    from: 1
                    to: 1079
                    stepSize: 1
                    value: Chiaki.settings.dpadTouchIncrement
                    valueText: qsTr("%1 mm").arg(Math.round(value) / 100)
                    onMoved: Chiaki.settings.dpadTouchIncrement = Math.round(value)
                }
            }

            C.SettingsRow {
                sub: true
                enabled: Chiaki.settings.dpadTouchEnabled
                title: qsTr("Switch between D-pad and touchpad")
                subtitle: qsTr("Hold these together.")
                keywords: qsTr("button combination shortcut toggle dpad touch")

                Row {
                    spacing: 5

                    Repeater {
                        model: page.switchButtons

                        Row {
                            required property var modelData
                            required property int index
                            spacing: 5

                            Text {
                                visible: parent.index > 0
                                anchors.verticalCenter: parent.verticalCenter
                                text: "+"
                                color: C.Theme.textDim
                                font.pixelSize: 12
                            }

                            C.Keycap {
                                anchors.verticalCenter: parent.verticalCenter
                                text: page.buttonCaps[parent.modelData] || "?"
                            }
                        }
                    }

                    Text {
                        visible: page.switchButtons.length === 0
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("No buttons")
                        color: C.Theme.textDim
                        font.pixelSize: 13
                    }
                }

                C.PillButton {
                    Layout.leftMargin: 4
                    small: true
                    text: page.changingSwitch ? qsTr("Done") : qsTr("Change…")
                    onClicked: page.changingSwitch = !page.changingSwitch
                }
            }

            C.SettingsRow {
                visible: page.changingSwitch
                sub: true
                enabled: Chiaki.settings.dpadTouchEnabled
                title: qsTr("Buttons")
                subtitle: qsTr("Up to four, held together.")
                textMinimum: 120

                GridLayout {
                    columns: 2
                    columnSpacing: 8
                    rowSpacing: 8

                    Repeater {
                        model: 4

                        C.Dropdown {
                            required property int index
                            readonly property string setting: "dpadTouchShortcut" + (index + 1)
                            Accessible.name: qsTr("Button %1").arg(index + 1)
                            model: page.buttonNames
                            currentIndex: Chiaki.settings[setting]
                            onActivated: (choice) => Chiaki.settings[setting] = choice
                        }
                    }
                }
            }
        }
    }

    // ------------------------------------------------ waiting for a controller's button

    Connections {
        target: Chiaki

        function onControllerMappingInProgressChanged() {
            // A controller was picked by one of its buttons
            if (Chiaki.controllerMappingInProgress && capture.visible && capture.waiting)
                picked.restart();
        }

        function onControllerMappingSteamControllerSelected() {
            if (!capture.visible || !capture.waiting)
                return;
            // The app has stopped the teaching already
            capture.quitMapping = false;
            capture.show("steam");
        }
    }

    // A moment after the pick, as before, so that the button press has gone through
    Timer {
        id: picked
        interval: 100
        onTriggered: {
            if (!capture.visible || !Chiaki.controllerMappingInProgress)
                return;
            if (capture.resetting) {
                capture.quitMapping = false;
                if (Chiaki.controllerMappingDefaultMapping) {
                    // Never taught: nothing to give back
                    Chiaki.controllerMappingQuit();
                    capture.show("untaught");
                } else {
                    // Also stops the teaching
                    Chiaki.controllerMappingReset();
                    capture.show("done");
                }
            } else {
                // The settings come back to this button after the mapping dialog
                teachButton.forceActiveFocus(Qt.TabFocusReason);
                root.showControllerMappingDialog();
                capture.quitMapping = false;
                capture.keepFocus = true;
                capture.close();
            }
        }
    }

    // Right after a result shows, the key the controller's button sent is not an answer to it
    Timer {
        id: settle
        interval: 500
        onTriggered: capture.settling = false
    }

    QQC.Popup {
        id: capture
        /** "teach" or "reset" while waiting for a button; then "steam", "done" or "untaught". */
        property string mode: "teach"
        property bool resetting: false
        /** The button that opened it: the focus goes back there. */
        property Item opener: null
        /** Closing it stops the teaching (Chiaki.controllerMappingQuit), unless that happened already. */
        property bool quitMapping: true
        /** The focus went on with the teaching: it does not go back to the opener. */
        property bool keepFocus: false
        property bool settling: false
        /** Opened with the keys or a controller: its button shows the focus ring. */
        property bool byKeys: false
        readonly property bool waiting: mode === "teach" || mode === "reset"

        readonly property var words: ({
            teach: [qsTr("Press a button on the controller"),
                    qsTr("Any button on the controller you want to teach. Then you show it which button is which.")],
            reset: [qsTr("Press a button on the controller"),
                    qsTr("Any button on the controller that should get back the buttons it came with.")],
            steam: [qsTr("This controller is set up in Steam"),
                    qsTr("Change its buttons in Steam, or switch Steam Input off for it and teach it here.")],
            done: [qsTr("Its buttons are back"),
                   qsTr("The controller has the buttons it came with again.")],
            untaught: [qsTr("Nothing to reset"),
                       qsTr("This controller was never taught here: it has the buttons it came with.")]
        })

        function start(reset, from) {
            resetting = reset;
            mode = reset ? "reset" : "teach";
            opener = from;
            byKeys = from.visualFocus;
            quitMapping = true;
            keepFocus = false;
            settling = false;
            open();
        }

        function show(result) {
            mode = result;
            settling = true;
            settle.restart();
            focusButton();
        }

        function focusButton() {
            // Off and on again: the pop-up gives it the focus without a reason the ring shows for
            actionButton.focus = false;
            actionButton.forceActiveFocus(byKeys ? Qt.TabFocusReason : Qt.PopupFocusReason);
        }

        /** Cancel or OK, by click or key. */
        function dismiss() {
            if (settling)
                return;
            // A controller has just been picked: what comes next comes by itself
            if (waiting && Chiaki.controllerMappingInProgress)
                return;
            close();
        }

        parent: QQC.Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(460, parent ? parent.width - 32 : 460)
        modal: true
        focus: true
        // Escape and Return are the keys a controller's Circle and Cross send: handled below
        closePolicy: QQC.Popup.NoAutoClose
        padding: 24

        QQC.Overlay.modal: Rectangle {
            color: Qt.rgba(5 / 255, 5 / 255, 20 / 255, 0.62)
        }

        background: Rectangle {
            radius: 20
            color: C.Theme.popup
            border.width: 1
            border.color: C.Theme.lineStrong
        }

        onOpened: {
            focusButton();
            if (waiting)
                Chiaki.beginControllerMapping(resetting);
        }

        onClosed: {
            picked.stop();
            if (!keepFocus && opener) {
                // The pop-up hands the focus back by itself, without the ring: off and on again
                opener.focus = false;
                opener.forceActiveFocus(byKeys ? Qt.TabFocusReason : Qt.MouseFocusReason);
            }
            if (quitMapping)
                Chiaki.controllerMappingQuit();
            quitMapping = true;
            keepFocus = false;
        }

        contentItem: ColumnLayout {
            spacing: 16

            // Nothing gets past the pop-up; Escape cancels or closes it
            Keys.onPressed: (event) => {
                event.accepted = true;
                if (event.key === Qt.Key_Escape && !event.isAutoRepeat)
                    capture.dismiss();
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 14

                Rectangle {
                    Layout.alignment: Qt.AlignTop
                    implicitWidth: 48
                    implicitHeight: 48
                    radius: 14
                    color: capture.mode === "steam" ? Qt.rgba(245 / 255, 185 / 255, 66 / 255, 0.14)
                         : capture.mode === "done" ? Qt.rgba(52 / 255, 211 / 255, 153 / 255, 0.14)
                         : Qt.rgba(140 / 255, 82 / 255, 1, 0.16)

                    C.Icon {
                        anchors.centerIn: parent
                        iconName: capture.mode === "done" ? "check" : capture.mode === "untaught" ? "info" : "gamepad"
                        size: 26
                        color: capture.mode === "steam" ? "#fbd98c" : capture.mode === "done" ? "#9ff0cc" : "#cdb6ff"
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 6

                    Text {
                        Layout.fillWidth: true
                        text: (capture.words[capture.mode] || ["", ""])[0]
                        wrapMode: Text.WordWrap
                        color: C.Theme.text
                        font.pixelSize: 18
                        font.weight: Font.ExtraBold
                    }

                    Text {
                        Layout.fillWidth: true
                        text: (capture.words[capture.mode] || ["", ""])[1]
                        wrapMode: Text.WordWrap
                        lineHeight: 1.15
                        color: C.Theme.textSoft
                        font.pixelSize: 14
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Rectangle {
                    visible: capture.waiting
                    implicitWidth: 8
                    implicitHeight: 8
                    radius: 4
                    color: C.Theme.wait

                    SequentialAnimation on opacity {
                        running: capture.visible && capture.waiting
                        loops: Animation.Infinite
                        NumberAnimation { to: 0.25; duration: 650; easing.type: Easing.InOutSine }
                        NumberAnimation { to: 1; duration: 650; easing.type: Easing.InOutSine }
                    }
                }

                Text {
                    visible: capture.waiting
                    Layout.fillWidth: true
                    text: qsTr("Waiting for a button…")
                    color: "#fbd98c"
                    font.pixelSize: 13
                    font.weight: Font.Bold
                }

                Item {
                    visible: !capture.waiting
                    Layout.fillWidth: true
                }

                C.PillButton {
                    id: actionButton
                    small: true
                    kind: capture.waiting ? "ghost" : "primary"
                    text: capture.waiting ? qsTr("Cancel") : qsTr("OK")
                    firstInFocusChain: true
                    lastInFocusChain: true
                    onClicked: capture.dismiss()
                    // Also without the focus ring (opened with the mouse)
                    Keys.onReturnPressed: (event) => {
                        event.accepted = true;
                        if (!event.isAutoRepeat)
                            capture.dismiss();
                    }
                    Keys.onEnterPressed: (event) => {
                        event.accepted = true;
                        if (!event.isAutoRepeat)
                            capture.dismiss();
                    }
                }
            }
        }
    }
}
