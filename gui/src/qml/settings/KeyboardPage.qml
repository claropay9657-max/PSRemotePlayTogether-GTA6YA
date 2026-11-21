import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC

import org.streetpea.chiaking

import "../controls" as C

// Keyboard & mouse: the keyboard as a controller, the mouse as its touchpad, and which
// key is which controller button.
//
// A key is changed right where it is shown, without a pop-up: click it (or press Return
// or Space on it) and it waits; the next key pressed is the new one
// (Chiaki.settings.changeControllerKey). Escape, the focus going elsewhere or a click
// anywhere else keeps the old one. While a key waits it takes every key press, Escape,
// Return, the arrows and Page Up / Page Down included, so none of them reaches the window.
C.SettingsPage {
    id: page
    title: qsTr("Keyboard & mouse")
    description: qsTr("Play with the keyboard as a controller. Click a key, then press the new one.")

    /** The key cap waiting for a new key, or null. */
    property Item waitingCap: null

    /** buttonValue → the key's name, from Chiaki.settings.controllerMapping. */
    readonly property var keyNames: {
        const names = {};
        const list = Chiaki.settings.controllerMapping || [];
        for (let i = 0; i < list.length; ++i)
            names[list[i].buttonValue] = list[i].keyName;
        return names;
    }

    /** The buttons, by Chiaki's values (CHIAKI_CONTROLLER_BUTTON_* and ControllerButtonExt in settings.h). */
    readonly property var sections: [
        {
            title: qsTr("Face buttons"), columns: 1,
            keywords: qsTr("cross circle square triangle moon box pyramid x o"),
            keys: [
                { name: qsTr("Cross"), full: qsTr("Cross"), button: 1 },
                { name: qsTr("Circle"), full: qsTr("Circle"), button: 2 },
                { name: qsTr("Square"), full: qsTr("Square"), button: 4 },
                { name: qsTr("Triangle"), full: qsTr("Triangle"), button: 8 }
            ]
        },
        {
            title: qsTr("D-pad"), columns: 1,
            keywords: qsTr("dpad direction arrows up down left right"),
            keys: [
                { name: qsTr("Up"), full: qsTr("D-pad up"), button: 64 },
                { name: qsTr("Down"), full: qsTr("D-pad down"), button: 128 },
                { name: qsTr("Left"), full: qsTr("D-pad left"), button: 16 },
                { name: qsTr("Right"), full: qsTr("D-pad right"), button: 32 }
            ]
        },
        {
            title: qsTr("Middle"), columns: 1,
            keywords: qsTr("options create share touchpad ps button home"),
            keys: [
                { name: qsTr("Options"), full: qsTr("Options"), button: 4096 },
                { name: qsTr("Create / Share"), full: qsTr("Create"), button: 8192 },
                { name: qsTr("Touchpad"), full: qsTr("Touchpad"), button: 16384 },
                { name: qsTr("PS button"), full: qsTr("PS button"), button: 32768 }
            ]
        },
        {
            title: qsTr("Shoulders and triggers"), columns: 2,
            keywords: qsTr("l1 r1 l2 r2 l3 r3 bumpers triggers stick press"),
            keys: [
                { name: "L1", full: "L1", button: 256 },
                { name: "R1", full: "R1", button: 512 },
                { name: "L2", full: "L2", button: 65536 },
                { name: "R2", full: "R2", button: 131072 },
                { name: "L3", full: "L3", button: 1024 },
                { name: "R3", full: "R3", button: 2048 }
            ]
        },
        {
            title: qsTr("Left stick"), columns: 2,
            keywords: qsTr("analog stick move up down left right"),
            keys: [
                { name: qsTr("Up"), full: qsTr("Left stick up"), button: 1 << 20 },
                { name: qsTr("Down"), full: qsTr("Left stick down"), button: 1 << 21 },
                { name: qsTr("Left"), full: qsTr("Left stick left"), button: 1 << 19 },
                { name: qsTr("Right"), full: qsTr("Left stick right"), button: 1 << 18 }
            ]
        },
        {
            title: qsTr("Right stick"), columns: 2,
            keywords: qsTr("analog stick camera up down left right"),
            keys: [
                { name: qsTr("Up"), full: qsTr("Right stick up"), button: 1 << 24 },
                { name: qsTr("Down"), full: qsTr("Right stick down"), button: 1 << 25 },
                { name: qsTr("Left"), full: qsTr("Right stick left"), button: 1 << 23 },
                { name: qsTr("Right"), full: qsTr("Right stick right"), button: 1 << 22 }
            ]
        }
    ]

    /** How a key's name (QKeySequence text) is shown on its cap. */
    function keyLabel(name) {
        if (!name)
            return "–";
        const mac = Qt.platform.os === "osx";
        switch (name) {
        case "Up": return "↑";
        case "Down": return "↓";
        case "Left": return "←";
        case "Right": return "→";
        case "PgUp": return qsTr("Page Up");
        case "PgDown": return qsTr("Page Down");
        case "Ins": return qsTr("Insert");
        case "Del": return qsTr("Delete");
        // On a Mac, Qt calls ⌘ "Ctrl" and the Control key "Meta"
        case "Ctrl": return mac ? qsTr("Cmd") : qsTr("Ctrl");
        case "Meta": return mac ? qsTr("Control") : (Qt.platform.os === "windows" ? qsTr("Win") : qsTr("Meta"));
        case "Alt": return mac ? qsTr("Option") : qsTr("Alt");
        }
        return name;
    }

    /** The other buttons on the same key, by their full names. */
    function othersOn(keyName, button) {
        if (!keyName)
            return [];
        let out = [];
        for (const section of sections) {
            for (const key of section.keys) {
                if (key.button !== button && keyNames[key.button] === keyName)
                    out.push(key.full);
            }
        }
        return out;
    }

    function startWaiting(cap) {
        waitingCap = cap;
        if (!cap.activeFocus)
            cap.forceActiveFocus(Qt.MouseFocusReason);
    }

    function stopWaiting() {
        waitingCap = null;
    }

    /** Keys on a key cap: Return starts waiting; while it waits, the key pressed is the new one. */
    function capKey(cap, event) {
        if (waitingCap !== cap) {
            if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                if (!event.isAutoRepeat)
                    startWaiting(cap);
                event.accepted = true;
                return;
            }
            C.FocusChain.handle(cap, event);
            return;
        }
        // Waiting: nothing goes on to the window
        event.accepted = true;
        if (event.isAutoRepeat)
            return;
        if (event.key === Qt.Key_Escape) {
            stopWaiting();
            return;
        }
        if (event.key === 0 || event.key === Qt.Key_unknown)
            return;
        Chiaki.settings.changeControllerKey(cap.button, event.key);
        stopWaiting();
    }

    // A click anywhere else on the page keeps the old key
    TapHandler {
        onTapped: (eventPoint) => {
            const cap = page.waitingCap;
            if (!cap)
                return;
            const p = cap.mapFromItem(null, eventPoint.scenePosition.x, eventPoint.scenePosition.y);
            if (!cap.contains(p))
                page.stopWaiting();
        }
    }

    // ------------------------------------------------ the two switches and the reset

    Rectangle {
        id: bar
        Layout.fillWidth: true
        implicitHeight: barLayout.implicitHeight + 20
        radius: 18
        color: C.Theme.card
        border.width: 1
        border.color: C.Theme.line

        GridLayout {
            id: barLayout
            readonly property bool wide: bar.width >= 600

            x: 14
            y: 10
            width: parent.width - 28
            columns: wide ? 5 : 1
            columnSpacing: 12
            rowSpacing: 8

            // Each part is a row for the search: what it is called and the words it is found by
            Item {
                readonly property bool isSettingRow: true
                readonly property string title: qsTr("Keyboard as controller")
                readonly property string subtitle: qsTr("Play with the keys below while streaming.")
                readonly property string keywords: qsTr("keys keyboard controller play enable")
                implicitWidth: keyboardSwitch.implicitWidth
                implicitHeight: 36

                C.Toggle {
                    id: keyboardSwitch
                    anchors.verticalCenter: parent.verticalCenter
                    text: parent.title
                    font.pixelSize: 14
                    font.weight: Font.ExtraBold
                    Accessible.name: text
                    checked: Chiaki.settings.keyboardEnabled
                    onToggled: Chiaki.settings.keyboardEnabled = checked
                    QQC.ToolTip.visible: hovered
                    QQC.ToolTip.text: parent.subtitle
                    QQC.ToolTip.delay: 600
                }
            }

            Rectangle {
                visible: barLayout.wide
                implicitWidth: 1
                implicitHeight: 24
                color: Qt.rgba(1, 1, 1, 0.10)
            }

            Item {
                readonly property bool isSettingRow: true
                readonly property string title: qsTr("Mouse as touchpad")
                readonly property string subtitle: qsTr("Drag on the picture to swipe on the touchpad; another mouse button presses it.")
                readonly property string keywords: qsTr("mouse touchpad touch swipe click trackpad enable")
                implicitWidth: mouseSwitch.implicitWidth
                implicitHeight: 36

                C.Toggle {
                    id: mouseSwitch
                    anchors.verticalCenter: parent.verticalCenter
                    text: parent.title
                    font.pixelSize: 14
                    font.weight: Font.ExtraBold
                    Accessible.name: text
                    checked: Chiaki.settings.mouseTouchEnabled
                    onToggled: Chiaki.settings.mouseTouchEnabled = checked
                    QQC.ToolTip.visible: hovered
                    QQC.ToolTip.text: parent.subtitle
                    QQC.ToolTip.delay: 600
                }
            }

            Item {
                visible: barLayout.wide
                Layout.fillWidth: true
            }

            Item {
                readonly property bool isSettingRow: true
                readonly property string title: qsTr("Reset all keys")
                readonly property string subtitle: qsTr("Every button gets the key it came with.")
                readonly property string keywords: qsTr("default defaults clear key mapping restore")
                implicitWidth: resetKeys.implicitWidth
                implicitHeight: 36

                C.PillButton {
                    id: resetKeys
                    anchors.verticalCenter: parent.verticalCenter
                    small: true
                    text: parent.title
                    onClicked: root.showConfirmDialog(qsTr("Reset all keys"),
                                                      qsTr("Every button gets the key it came with again."),
                                                      () => Chiaki.settings.clearKeyMapping())
                }
            }
        }
    }

    // ------------------------------------------------ the keys, one card per part of the controller

    GridLayout {
        id: keyGrid
        readonly property real cardWidth: (width - columnSpacing * (columns - 1)) / columns

        Layout.fillWidth: true
        // Off while the keyboard does not play
        enabled: Chiaki.settings.keyboardEnabled
        opacity: enabled ? 1 : 0.5
        columns: width >= 720 ? 3 : width >= 420 ? 2 : 1
        columnSpacing: 14
        rowSpacing: 14

        Repeater {
            model: page.sections

            Rectangle {
                id: card
                required property var modelData
                // A row for the search: the window finds it by these, and its first key gets the focus
                readonly property bool isSettingRow: true
                readonly property string title: modelData.title
                readonly property string subtitle: modelData.keys.map((k) => k.full).join(", ")
                readonly property string keywords: qsTr("key keys") + " " + modelData.keywords

                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: keyGrid.cardWidth
                implicitHeight: cardColumn.implicitHeight + 24
                radius: 18
                color: C.Theme.card
                border.width: 1
                border.color: C.Theme.line

                ColumnLayout {
                    id: cardColumn
                    x: 14
                    y: 12
                    width: parent.width - 28
                    spacing: 8

                    C.SectionLabel {
                        text: card.title
                    }

                    GridLayout {
                        id: keyLines
                        Layout.fillWidth: true
                        columns: card.modelData.columns
                        columnSpacing: 14
                        rowSpacing: 4

                        Repeater {
                            model: card.modelData.keys

                            RowLayout {
                                id: line
                                required property var modelData

                                Layout.fillWidth: true
                                Layout.minimumHeight: 34
                                spacing: 8

                                Text {
                                    Layout.fillWidth: true
                                    text: line.modelData.name
                                    elide: Text.ElideRight
                                    color: C.Theme.text
                                    font.pixelSize: 14
                                    font.weight: Font.Bold
                                }

                                QQC.AbstractButton {
                                    id: cap
                                    readonly property int button: line.modelData.button
                                    readonly property string keyName: page.keyNames[button] || ""
                                    readonly property bool waiting: page.waitingCap === cap
                                    readonly property var others: page.othersOn(keyName, button)
                                    property bool firstInFocusChain: false
                                    property bool lastInFocusChain: false

                                    implicitWidth: card.modelData.columns > 1 ? Math.max(40, capText.implicitWidth + 16)
                                                                              : Math.max(44, capText.implicitWidth + 20)
                                    implicitHeight: 30
                                    focusPolicy: Qt.StrongFocus
                                    hoverEnabled: true
                                    text: waiting ? qsTr("Press a key…") : page.keyLabel(keyName)
                                    Accessible.name: qsTr("%1: %2").arg(line.modelData.full).arg(page.keyLabel(keyName))
                                    Accessible.description: waiting ? qsTr("Press the new key, or Escape to keep this one.")
                                                                    : qsTr("Press to change the key.")
                                    QQC.ToolTip.visible: hovered && !waiting && others.length > 0
                                    QQC.ToolTip.text: qsTr("Also on %1. A key works for one button only.").arg(others.join(", "))
                                    QQC.ToolTip.delay: 300

                                    onClicked: {
                                        if (waiting)
                                            page.stopWaiting();
                                        else
                                            page.startWaiting(cap);
                                    }
                                    onActiveFocusChanged: {
                                        if (!activeFocus && waiting)
                                            page.stopWaiting();
                                    }
                                    Component.onDestruction: {
                                        if (page.waitingCap === cap)
                                            page.stopWaiting();
                                    }

                                    // While waiting, shortcuts (⌘F, …) wait as well: the key press comes here
                                    Keys.onShortcutOverride: (event) => {
                                        if (cap.waiting)
                                            event.accepted = true;
                                    }
                                    Keys.onPressed: (event) => page.capKey(cap, event)
                                    Keys.onReleased: (event) => {
                                        if (cap.waiting)
                                            event.accepted = true;
                                    }

                                    background: Item {
                                        // The key: a darker edge, thicker at the bottom
                                        Rectangle {
                                            anchors.fill: parent
                                            visible: !cap.waiting
                                            radius: 8
                                            color: cap.others.length > 0 ? C.Theme.bad : "#39325e"

                                            Rectangle {
                                                anchors {
                                                    fill: parent
                                                    leftMargin: 1
                                                    rightMargin: 1
                                                    topMargin: 1
                                                    bottomMargin: 3
                                                }
                                                radius: 7
                                                color: cap.down ? "#1a1632" : cap.hovered ? "#2a2449" : "#221d3e"
                                            }
                                        }

                                        // Waiting for the new key
                                        Rectangle {
                                            id: waitingLook
                                            anchors.fill: parent
                                            visible: cap.waiting
                                            radius: 8
                                            color: Qt.rgba(245 / 255, 185 / 255, 66 / 255, 0.14)
                                            border.width: 1
                                            border.color: C.Theme.wait

                                            SequentialAnimation on opacity {
                                                running: cap.waiting
                                                loops: Animation.Infinite
                                                alwaysRunToEnd: true
                                                NumberAnimation { to: 0.6; duration: 700; easing.type: Easing.InOutSine }
                                                NumberAnimation { to: 1; duration: 700; easing.type: Easing.InOutSine }
                                            }
                                        }

                                        Rectangle {
                                            anchors.fill: parent
                                            anchors.margins: -3
                                            radius: 11
                                            color: "transparent"
                                            border.width: 2
                                            border.color: C.Theme.focus
                                            visible: cap.visualFocus
                                        }
                                    }

                                    contentItem: Text {
                                        id: capText
                                        text: cap.text
                                        horizontalAlignment: Text.AlignHCenter
                                        verticalAlignment: Text.AlignVCenter
                                        // On the key's face, above its thicker bottom edge
                                        bottomPadding: cap.waiting ? 0 : 2
                                        color: cap.waiting ? "#fde4a8" : C.Theme.text
                                        font.pixelSize: 12
                                        font.weight: Font.ExtraBold
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    Text {
        Layout.fillWidth: true
        text: qsTr("Changing a key happens right here: the key you picked waits for the next key you press. Escape, or a click anywhere else, keeps the old one, so Escape itself only comes back with Reset all keys.")
        wrapMode: Text.WordWrap
        color: C.Theme.textDim
        font.pixelSize: 13
    }
}
