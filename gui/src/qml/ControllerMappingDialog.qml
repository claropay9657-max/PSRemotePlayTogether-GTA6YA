import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

import org.streetpea.chiaking

import "controls" as C

// Teaching a controller its buttons: for each PlayStation button, the one or two buttons
// of this controller that do it (Chiaki.currentControllerMapping). Click one (or Return
// on it), then press the controller's button; the button at the top right saves. The
// Controllers page opens this once the controller was picked by one of its buttons.
//
// Keys: the buttons sit in a grid; the arrows move in it, Up from its top row goes to
// "Also the sticks and L2 / R2", Return or Space picks a button.
DialogView {
    id: dialog
    property bool finalClosing: false
    property bool quitButtonMapping: true
    title: Chiaki.currentControllerType
    header: qsTr("Teach a controller its buttons")
    buttonText: qsTr("Save the buttons")
    buttonEnabled: Chiaki.controllerMappingAltered
    onAccepted: {
        Chiaki.controllerMappingApply();
        Chiaki.controllerMappingQuit();
    }
    Component.onDestruction: {
        if(Chiaki.controllerMappingInProgress)
        {
            finalClosing = true;
            Chiaki.controllerMappingButtonQuit();
            Chiaki.controllerMappingQuit();
        }
    }

    readonly property int gutter: width < 600 ? 16 : 32

    /** The PlayStation buttons by Chiaki's values (settings.h), named as on the Keyboard & mouse page. */
    readonly property var buttonNames: ({
        1: qsTr("Cross"), 2: qsTr("Circle"), 4: qsTr("Square"), 8: qsTr("Triangle"),
        16: qsTr("D-pad left"), 32: qsTr("D-pad right"), 64: qsTr("D-pad up"), 128: qsTr("D-pad down"),
        256: "L1", 512: "R1", 1024: "L3", 2048: "R3",
        4096: qsTr("Options"), 8192: qsTr("Create / Share"), 16384: qsTr("Touchpad"), 32768: qsTr("PS button"),
        65536: "L2", 131072: "R2",
        67108864: qsTr("Left stick X"), 134217728: qsTr("Left stick Y"),
        268435456: qsTr("Right stick X"), 536870912: qsTr("Right stick Y"),
        1073741824: qsTr("Mic button")
    })

    /** Button `which` (0 or 1) of the PlayStation button at `index` gets the focus, if it is there. */
    function focusSlot(index, which) {
        const tile = chiakiButtons.itemAt(index);
        const slot = tile ? tile.slot(which) : null;
        if (slot) {
            // Off and on again: after the pop-up it may have the focus already, without the ring
            slot.focus = false;
            slot.forceActiveFocus(Qt.TabFocusReason);
        }
        return !!slot;
    }

    /** The arrows in the grid: by rows and columns, never past its edges. */
    function slotKey(slot, event) {
        const cols = grid.columns;
        const i = slot.mapping;
        switch (event.key) {
        case Qt.Key_Left:
            if (slot.which === 1) {
                focusSlot(i, 0);
                event.accepted = true;
            } else if (i % cols !== 0) {
                focusSlot(i - 1, 1);
                event.accepted = true;
            }
            break;
        case Qt.Key_Right:
            if (slot.which === 0) {
                focusSlot(i, 1);
                event.accepted = true;
            } else if (i % cols !== cols - 1 && i + 1 < chiakiButtons.count) {
                focusSlot(i + 1, 0);
                event.accepted = true;
            }
            break;
        case Qt.Key_Up:
            if (i >= cols)
                focusSlot(i - cols, slot.which);
            else
                analogStickMapping.forceActiveFocus(Qt.TabFocusReason);
            event.accepted = true;
            break;
        case Qt.Key_Down:
            if (i + cols < chiakiButtons.count) {
                focusSlot(i + cols, slot.which);
                event.accepted = true;
            }
            break;
        case Qt.Key_Return:
        case Qt.Key_Enter:
            if (slot.visualFocus)
                slot.clicked();
            event.accepted = true;
            break;
        }
    }

    function inside(item, ancestor) {
        for (let p = item; p; p = p.parent) {
            if (p === ancestor)
                return true;
        }
        return false;
    }

    /** The row or tile (or the item) that should be on screen when `item` has the focus. */
    function rowOf(item, flick) {
        for (let p = item; p && p !== flick.contentItem; p = p.parent) {
            if (p.isSettingRow === true)
                return p;
        }
        return item;
    }

    function scrollTo(item, flick, margin) {
        const top = item.mapToItem(flick.contentItem, 0, 0).y;
        const bottom = top + item.height;
        const max = Math.max(0, flick.contentHeight - flick.height);
        if (top - margin < flick.contentY)
            flick.contentY = Math.max(0, Math.min(max, top - margin));
        else if (bottom + margin > flick.contentY + flick.height)
            flick.contentY = Math.max(0, Math.min(max, bottom + margin - flick.height));
    }

    /** One of the two buttons of this controller for a PlayStation button: shows which, a click captures a new one. */
    component Slot: AbstractButton {
        id: slot
        /** Where in Chiaki.currentControllerMapping, and which of its two. */
        property int mapping
        property int which
        property int buttonValue
        property string buttonName
        property bool firstInFocusChain: false
        property bool lastInFocusChain: false
        readonly property bool empty: text.length === 0

        implicitWidth: 76
        implicitHeight: 34
        hoverEnabled: true
        focusPolicy: Qt.StrongFocus
        Accessible.name: which === 0 ? qsTr("%1: %2").arg(buttonName).arg(empty ? qsTr("not set") : text)
                                     : qsTr("%1, second button: %2").arg(buttonName).arg(empty ? qsTr("none") : text)
        ToolTip.visible: hovered && empty && which === 1
        ToolTip.delay: 600
        ToolTip.text: qsTr("Add a second button")

        Component.onDestruction: {
            if (visualFocus) {
                let item = nextItemInFocusChain();
                if (item)
                    item.forceActiveFocus(Qt.TabFocusReason);
            }
        }

        Keys.onPressed: (event) => dialog.slotKey(slot, event)

        onClicked: {
            buttonDialog.show({
                value: buttonValue,
                buttonIndex: which,
                mappingIndex: mapping,
                name: buttonName
            });
        }

        background: Rectangle {
            radius: 10
            color: slot.empty ? "transparent" : C.Theme.field
            border.width: slot.visualFocus ? 2 : 1
            border.color: slot.visualFocus ? C.Theme.focus
                        : Qt.rgba(1, 1, 1, slot.hovered ? 0.22 : slot.empty ? 0.09 : 0.14)

            Rectangle {
                anchors.fill: parent
                radius: parent.radius
                color: "#ffffff"
                opacity: slot.down ? 0.10 : slot.hovered ? 0.05 : 0
            }
        }

        contentItem: Item {
            Text {
                visible: !slot.empty
                anchors.centerIn: parent
                width: Math.min(implicitWidth, parent.width - 8)
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
                text: slot.text
                color: C.Theme.text
                font.pixelSize: 13
                font.weight: Font.ExtraBold
            }

            Row {
                visible: slot.empty
                anchors.centerIn: parent
                spacing: 4

                C.Icon {
                    visible: slot.which === 1
                    anchors.verticalCenter: parent.verticalCenter
                    iconName: "plus"
                    size: 13
                    color: C.Theme.textDim
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: slot.which === 0 ? qsTr("Not set") : qsTr("Add")
                    color: C.Theme.textDim
                    font.pixelSize: 12
                    font.weight: Font.Bold
                }
            }
        }
    }

    Item {
        // Keeps the button with the focus on screen
        Connections {
            target: dialog.Window.window
            function onActiveFocusItemChanged() {
                const item = dialog.Window.window ? dialog.Window.window.activeFocusItem : null;
                if (item && dialog.inside(item, mappingFlick.contentItem))
                    dialog.scrollTo(dialog.rowOf(item, mappingFlick), mappingFlick, 12);
            }
        }

        Flickable {
            id: mappingFlick
            anchors.fill: parent
            clip: true
            contentWidth: width
            contentHeight: mappingColumn.implicitHeight + 48
            flickableDirection: Flickable.VerticalFlick
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {
                policy: mappingFlick.contentHeight > mappingFlick.height ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
                width: 10
            }

            ColumnLayout {
                id: mappingColumn
                x: Math.max(dialog.gutter, Math.round((mappingFlick.width - width) / 2))
                y: 24
                width: Math.min(mappingFlick.width - 2 * dialog.gutter, 980)
                spacing: 18

                C.SettingsSection {
                    title: qsTr("Controller")

                    C.SettingRow {
                        title: qsTr("Also the sticks and L2 / R2")
                        description: qsTr("Teaches the sticks and the analogue triggers as well.")
                        controlWidth: 120

                        Item { Layout.fillWidth: true }
                        C.Toggle {
                            id: analogStickMapping
                            firstInFocusChain: true
                            Accessible.name: qsTr("Also the sticks and L2 / R2")
                            checked: Chiaki.enableAnalogStickMapping
                            onToggled: Chiaki.enableAnalogStickMapping = checked
                            // As before: Right goes on to the first button as well
                            Keys.onRightPressed: dialog.focusSlot(0, 0)
                        }
                    }
                }

                C.SettingsSection {
                    title: qsTr("Buttons")
                    description: qsTr("Click a button, then press the one on your controller that should do it. A second one is optional. Save at the top right.")

                    GridLayout {
                        id: grid
                        Layout.fillWidth: true
                        Layout.topMargin: 16
                        Layout.bottomMargin: 14
                        columns: mappingColumn.width >= 900 ? 3 : mappingColumn.width >= 560 ? 2 : 1
                        rowSpacing: 8
                        columnSpacing: 10

                        Repeater {
                            id: chiakiButtons
                            model: Chiaki.currentControllerMapping

                            Rectangle {
                                id: tile
                                required property var modelData
                                required property int index
                                readonly property string name: dialog.buttonNames[modelData.buttonValue] || modelData.buttonName
                                readonly property var physical: modelData.physicalButton || []
                                /** Kept on screen whole when one of its buttons has the focus. */
                                readonly property bool isSettingRow: true

                                function slot(which) {
                                    return which === 0 ? firstSlot : secondSlot;
                                }

                                Layout.fillWidth: true
                                Layout.preferredWidth: 1
                                implicitHeight: 50
                                radius: 12
                                color: Qt.rgba(1, 1, 1, 0.03)
                                border.width: 1
                                border.color: C.Theme.line

                                RowLayout {
                                    anchors {
                                        fill: parent
                                        leftMargin: 14
                                        rightMargin: 8
                                    }
                                    spacing: 6

                                    Text {
                                        Layout.fillWidth: true
                                        text: tile.name
                                        elide: Text.ElideRight
                                        color: C.Theme.text
                                        font.pixelSize: 14
                                        font.weight: Font.Bold
                                    }

                                    Slot {
                                        id: firstSlot
                                        mapping: tile.index
                                        which: 0
                                        buttonValue: tile.modelData.buttonValue
                                        buttonName: tile.name
                                        text: tile.physical.length > 0 ? tile.physical[0] : ""
                                    }

                                    Slot {
                                        id: secondSlot
                                        mapping: tile.index
                                        which: 1
                                        buttonValue: tile.modelData.buttonValue
                                        buttonName: tile.name
                                        lastInFocusChain: tile.index == (chiakiButtons.count - 1)
                                        text: tile.physical.length > 1 ? tile.physical[1] : ""
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // Waits for a button of the controller (Chiaki.controllerMappingSelectButton)
        C.PopupCard {
            id: buttonDialog
            property int buttonValue
            property int buttonIndex
            property int mappingIndex
            property string buttonName
            property bool resetFocus: true
            focus: false
            title: buttonIndex === 0 ? qsTr("Press the button for %1").arg(buttonName)
                                     : qsTr("Press a second button for %1").arg(buttonName)
            closePolicy: Popup.CloseOnPressOutside
            onOpened: {
                Chiaki.controllerMappingSelectButton();
                buttonLabel.forceActiveFocus((Qt.TabFocusReason));
            }
            onClosed: {
                if(quitButtonMapping)
                    Chiaki.controllerMappingButtonQuit();
                else
                    quitButtonMapping = true;
                if(resetFocus)
                    dialog.focusSlot(mappingIndex, buttonIndex);
                else
                    resetFocus = true;
                buttonLabel.focus = false;
                focus = false;
            }

            function show(opts) {
                buttonValue = opts.value;
                buttonIndex = opts.buttonIndex;
                mappingIndex = opts.mappingIndex;
                buttonName = opts.name;
                open();
            }

            ColumnLayout {
                spacing: 18

                RowLayout {
                    spacing: 14

                    Rectangle {
                        Layout.alignment: Qt.AlignTop
                        implicitWidth: 48
                        implicitHeight: 48
                        radius: 14
                        color: Qt.rgba(140 / 255, 82 / 255, 1, 0.16)

                        C.Icon {
                            anchors.centerIn: parent
                            iconName: "gamepad"
                            size: 26
                            color: "#cdb6ff"
                        }
                    }

                    Text {
                        id: buttonLabel
                        Layout.preferredWidth: 340
                        text: qsTr("Press the button on your %1 that should be %2, or cancel.")
                              .arg(Chiaki.currentControllerType || qsTr("controller")).arg(buttonDialog.buttonName)
                        wrapMode: Text.WordWrap
                        lineHeight: 1.2
                        color: C.Theme.textSoft
                        font.pixelSize: 14
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Rectangle {
                        implicitWidth: 8
                        implicitHeight: 8
                        radius: 4
                        color: C.Theme.wait

                        SequentialAnimation on opacity {
                            running: buttonDialog.visible
                            loops: Animation.Infinite
                            NumberAnimation { to: 0.25; duration: 650; easing.type: Easing.InOutSine }
                            NumberAnimation { to: 1; duration: 650; easing.type: Easing.InOutSine }
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Waiting for a button…")
                        color: "#fbd98c"
                        font.pixelSize: 13
                        font.weight: Font.Bold
                    }

                    C.PillButton {
                        small: true
                        text: qsTr("Cancel")
                        onClicked: buttonDialog.reject()
                    }
                }
            }
        }

        Connections {
            target: Chiaki

            function onControllerMappingInProgressChanged()
            {
                if(!Chiaki.controllerMappingInProgress && !finalClosing)
                    dialog.close();
            }

            function onControllerMappingButtonSelected(pressedButton, chiaki_button_value, chiaki_button_name)
            {
                Chiaki.updateButton(buttonDialog.buttonValue, pressedButton, buttonDialog.buttonIndex);
                quitButtonMapping = false;
                buttonDialog.close();
            }
        }
    }
}
