import QtQuick
import QtQuick.Layouts
import QtQuick.Shapes
import QtQuick.Controls as QQC

import org.streetpea.chiaking

import "../controls" as C

// Overlays & OBS: what is drawn on top of the game picture for the player themself
// (Chiaki.hud), and the overlay for viewers that OBS shows as a Browser Source
// (Chiaki.settings.overlay* and Chiaki.overlayLayout). Both keep their settings in
// presets: whatever is changed here goes into the preset chosen above it, at once.
C.SettingsPage {
    id: page
    title: qsTr("Overlays & OBS")
    description: qsTr("What sits on top of the game for you, and what your viewers see in OBS. Every player can have both.")

    /** On the game picture: live values of the chosen preset (a property map, see presetsettings.h). */
    readonly property var hud: Chiaki.hud ? Chiaki.hud.values : ({})
    /** The OBS overlay's values that are not ordinary settings: battery, notices, chat, style, free. */
    readonly property var obs: Chiaki.overlayLayout ? Chiaki.overlayLayout.values : ({})
    readonly property bool obsOn: !!Chiaki.settings.overlayEnabled

    /** Two cards side by side where there is room for both. */
    readonly property bool wide: width >= 740
    readonly property real columnWidth: wide ? Math.floor((width - 20) / 2) : width

    /** The looks both can have, as Chiaki.hud / Chiaki.overlayLayout store them. */
    readonly property var styles: ["solid", "glass", "transparent"]
    readonly property var styleNames: [qsTr("Dark boxes"), qsTr("Glass boxes"), qsTr("No boxes")]

    function setHud(key, value) {
        if (Chiaki.hud)
            Chiaki.hud.values[key] = value;
    }

    function setObs(key, value) {
        if (Chiaki.overlayLayout)
            Chiaki.overlayLayout.values[key] = value;
    }

    function capitalised(text) {
        return text.length > 0 ? text.charAt(0).toUpperCase() + text.slice(1) : text;
    }

    /** For a link to another page: the settings window does it (SettingsView.openPage). */
    function openPage(key, rowTitle, byKeys) {
        for (let p = page.parent; p; p = p.parent) {
            if (typeof p.openPage === "function") {
                p.openPage(key, rowTitle, byKeys);
                return;
            }
        }
    }

    // ------------------------------------------------ building blocks

    /**
     * One thing to show, as a small on/off button. The search finds it by its text, like a
     * row, and a link can open the page on it (SettingsView.openPage(key, text)).
     */
    component Chip: Item {
        id: chip
        property string text
        property bool checked
        property string keywords
        readonly property string title: text
        readonly property string subtitle: ""
        readonly property bool isSettingRow: true
        signal toggled(bool on)

        implicitWidth: button.implicitWidth
        implicitHeight: button.implicitHeight

        C.ChipToggle {
            id: button
            anchors.fill: parent
            text: chip.text
            checked: chip.checked
            iconName: checked ? "check" : "plus"
            Accessible.name: chip.text
            onToggled: chip.toggled(checked)
        }
    }

    /** A few chips under a heading, in a group's card like a row. */
    component ChipRow: Item {
        id: chipRow
        property string title
        property string subtitle
        default property alias chips: flow.data
        /** No line above the first thing in the card (as SettingsRow). */
        readonly property bool firstInColumn: {
            const kids = parent ? parent.visibleChildren : [];
            for (let i = 0; i < kids.length; ++i) {
                if (kids[i].isSettingRow === true || kids[i].height > 0)
                    return kids[i] === chipRow;
            }
            return true;
        }

        Layout.fillWidth: true
        implicitHeight: texts.implicitHeight + 10 + flow.implicitHeight + 24
        opacity: enabled ? 1 : 0.5

        Rectangle {
            visible: !chipRow.firstInColumn
            width: parent.width
            height: 1
            color: Qt.rgba(1, 1, 1, 0.06)
        }

        ColumnLayout {
            id: texts
            x: 18
            y: 12
            width: parent.width - 36
            spacing: 2

            Text {
                Layout.fillWidth: true
                text: chipRow.title
                wrapMode: Text.WordWrap
                color: C.Theme.text
                font.pixelSize: 15
                font.weight: Font.ExtraBold
            }

            Text {
                Layout.fillWidth: true
                visible: text.length > 0
                text: chipRow.subtitle
                wrapMode: Text.WordWrap
                color: C.Theme.textDim
                font.pixelSize: 13
            }
        }

        Flow {
            id: flow
            x: 18
            y: texts.y + texts.height + 10
            width: parent.width - 36
            spacing: 6
        }
    }

    /** A line of a preset's menu. */
    component MenuEntry: QQC.MenuItem {
        id: entry
        property string iconName
        /** Throws something away: red. */
        property bool danger: false

        height: visible ? implicitHeight : 0
        implicitHeight: 38
        leftPadding: 10
        rightPadding: 16
        topPadding: 0
        bottomPadding: 0
        font.pixelSize: 14
        font.weight: danger ? Font.Bold : Font.DemiBold
        opacity: enabled ? 1 : 0.45
        indicator: Item {}
        arrow: Item {}

        contentItem: Item {
            // Measured at once: the menu takes its width before it opens
            readonly property int iconSpace: entry.iconName.length > 0 ? 27 : 0
            implicitWidth: iconSpace + entryText.implicitWidth
            implicitHeight: 20

            C.Icon {
                anchors.verticalCenter: parent.verticalCenter
                visible: entry.iconName.length > 0
                iconName: entry.iconName
                size: 17
                color: entry.danger ? "#ff8fa8" : C.Theme.textSoft
            }

            Text {
                id: entryText
                anchors.verticalCenter: parent.verticalCenter
                x: parent.iconSpace
                text: entry.text
                font: entry.font
                color: entry.danger ? "#ffb3c4" : C.Theme.text
            }
        }

        background: Rectangle {
            implicitHeight: 38
            radius: 10
            color: entry.highlighted ? Qt.rgba(140 / 255, 82 / 255, 1, 0.20) : "transparent"
        }
    }

    /**
     * Choose, add, rename, delete and reset the presets of Chiaki.hud or
     * Chiaki.overlayLayout. Naming one happens in the row itself.
     */
    component PresetRow: C.SettingsRow {
        id: presetRow
        property var group: null
        /** "" choosing; "new" or "rename" while a name is typed */
        property string mode: ""
        readonly property var names: group ? group.presets : []
        readonly property string current: group ? (names[group.active] || "") : ""

        function startNaming(what) {
            mode = what;
            nameField.text = what === "rename" ? current : "";
            // After the menu has given the focus back
            Qt.callLater(() => {
                nameField.forceActiveFocus(Qt.TabFocusReason);
                nameField.readOnly = false;
                nameField.selectAll();
            });
        }

        function finishNaming(save) {
            const name = nameField.text.trim();
            if (save && name.length > 0 && group) {
                if (mode === "new")
                    group.addPreset(name);
                else
                    group.renamePreset(name);
            }
            mode = "";
            Qt.callLater(() => presetChoice.forceActiveFocus(Qt.TabFocusReason));
        }

        title: mode === "new" ? qsTr("New preset") : mode === "rename" ? qsTr("Rename the preset") : qsTr("Preset")
        subtitle: mode === "new" ? qsTr("Keeps everything as it is now, under a name of its own.")
                : mode === "" ? qsTr("Every change here goes into the preset chosen.") : ""
        textMinimum: 150

        C.Dropdown {
            id: presetChoice
            visible: presetRow.mode === ""
            Layout.maximumWidth: 240
            Accessible.name: qsTr("Preset")
            model: presetRow.names
            currentIndex: presetRow.group ? presetRow.group.active : -1
            onActivated: (index) => presetRow.group.active = index
        }

        C.IconButton {
            id: presetMore
            visible: presetRow.mode === ""
            small: true
            iconName: "more"
            tip: qsTr("New, rename, delete or start over")
            onClicked: presetMenu.popup(presetMore, 0, presetMore.height + 6)
        }

        C.Field {
            id: nameField
            visible: presetRow.mode !== ""
            Layout.preferredWidth: 200
            Layout.fillWidth: true
            Accessible.name: presetRow.title
            maximumLength: 40
            hint: presetRow.mode === "new" ? qsTr("Name of the new preset") : qsTr("New name")
            onAccepted: presetRow.finishNaming(true)
            Keys.onEscapePressed: presetRow.finishNaming(false)
        }

        C.PillButton {
            visible: presetRow.mode !== ""
            small: true
            kind: "primary"
            text: presetRow.mode === "new" ? qsTr("Save") : qsTr("Rename")
            enabled: nameField.text.trim().length > 0
            onClicked: presetRow.finishNaming(true)
        }

        C.PillButton {
            visible: presetRow.mode !== ""
            small: true
            text: qsTr("Cancel")
            onClicked: presetRow.finishNaming(false)
        }

        C.Menu {
            id: presetMenu
            padding: 6
            font.pixelSize: 14
            background: Rectangle {
                implicitWidth: 220
                radius: 16
                color: C.Theme.popup
                border.width: 1
                border.color: C.Theme.lineStrong
            }

            MenuEntry {
                iconName: "plus"
                text: qsTr("New preset…")
                onTriggered: presetRow.startNaming("new")
            }

            MenuEntry {
                iconName: "pencil"
                text: qsTr("Rename…")
                onTriggered: presetRow.startNaming("rename")
            }

            MenuEntry {
                iconName: "trash"
                danger: true
                text: qsTr("Delete…")
                enabled: presetRow.names.length > 1
                onTriggered: root.showConfirmDialog(qsTr("Delete the preset"),
                    qsTr("Delete the preset “%1”?").arg(presetRow.current),
                    () => presetRow.group.removePreset())
            }

            QQC.MenuSeparator {
                topPadding: 4
                bottomPadding: 4
                leftPadding: 6
                rightPadding: 6
                contentItem: Rectangle {
                    implicitHeight: 1
                    color: C.Theme.line
                }
            }

            MenuEntry {
                iconName: "refresh"
                text: qsTr("Start over with the built-in presets…")
                onTriggered: root.showConfirmDialog(qsTr("Start over"),
                    qsTr("Throw all presets away, your own ones too, and start over with the built-in ones?"),
                    () => presetRow.group.resetAll())
            }
        }
    }

    // ------------------------------------------------ the two cards

    Item {
        Layout.fillWidth: true
        implicitHeight: page.wide ? Math.max(hudGroup.implicitHeight, obsGroup.implicitHeight)
                                  : hudGroup.implicitHeight + 20 + obsGroup.implicitHeight

        // ............................................ on the game picture, for you

        C.SettingsGroup {
            id: hudGroup
            width: page.columnWidth
            title: qsTr("On your game picture")
            note: qsTr("only you see it")

            // A small look at it, with what is switched on, where it is placed
            Item {
                Layout.fillWidth: true
                implicitHeight: 124 + 14 + 6

                Item {
                    id: look
                    anchors {
                        fill: parent
                        leftMargin: 16
                        rightMargin: 16
                        topMargin: 14
                        bottomMargin: 6
                    }

                    readonly property bool plain: page.hud.style !== "glass" && page.hud.style !== "solid"
                    readonly property color box: page.hud.style === "solid" ? "#10101a"
                                               : page.hud.style === "glass" ? Qt.rgba(12 / 255, 10 / 255, 26 / 255, 0.72) : "transparent"
                    readonly property string ownName: (Chiaki.settings.coplayName || "").trim() || qsTr("Host")
                    readonly property var players: [
                        { name: ownName, colour: C.Theme.good, talking: true },
                        { name: "Nova", colour: "#a77bff", talking: false },
                        { name: "Mia", colour: "#f472b6", talking: false }
                    ]

                    /** Where an element sits: a fraction of the room that is left, as the game picture does. */
                    function place(fraction, room) {
                        const f = Math.max(0, Math.min(1, Number(fraction) || 0));
                        return Math.round(8 + f * Math.max(0, room - 16));
                    }

                    // The game: a sky and two hills, rounded with the card
                    Rectangle {
                        anchors.fill: parent
                        radius: 12
                        gradient: Gradient {
                            GradientStop { position: 0.0; color: "#24175a" }
                            GradientStop { position: 0.55; color: "#6e3080" }
                            GradientStop { position: 1.0; color: "#e07d68" }
                        }
                    }

                    Shape {
                        anchors.fill: parent
                        // Smooth edges
                        layer.enabled: true
                        layer.samples: 4

                        ShapePath {
                            strokeWidth: 0
                            strokeColor: "transparent"
                            fillColor: "#2b1650"
                            startX: 0
                            startY: look.height * 0.70
                            PathLine { x: look.width * 0.20; y: look.height * 0.52 }
                            PathLine { x: look.width * 0.38; y: look.height * 0.66 }
                            PathLine { x: look.width * 0.60; y: look.height * 0.44 }
                            PathLine { x: look.width * 0.80; y: look.height * 0.62 }
                            PathLine { x: look.width; y: look.height * 0.50 }
                            PathLine { x: look.width; y: look.height - 12 }
                            PathArc { x: look.width - 12; y: look.height; radiusX: 12; radiusY: 12 }
                            PathLine { x: 12; y: look.height }
                            PathArc { x: 0; y: look.height - 12; radiusX: 12; radiusY: 12 }
                            PathLine { x: 0; y: look.height * 0.70 }
                        }

                        ShapePath {
                            strokeWidth: 0
                            strokeColor: "transparent"
                            fillColor: "#160b2b"
                            startX: 0
                            startY: look.height * 0.86
                            PathLine { x: look.width * 0.30; y: look.height * 0.76 }
                            PathLine { x: look.width * 0.55; y: look.height * 0.84 }
                            PathLine { x: look.width * 0.80; y: look.height * 0.74 }
                            PathLine { x: look.width; y: look.height * 0.82 }
                            PathLine { x: look.width; y: look.height - 12 }
                            PathArc { x: look.width - 12; y: look.height; radiusX: 12; radiusY: 12 }
                            PathLine { x: 12; y: look.height }
                            PathArc { x: 0; y: look.height - 12; radiusX: 12; radiusY: 12 }
                            PathLine { x: 0; y: look.height * 0.86 }
                        }
                    }

                    // What is drawn on top, as see-through as chosen
                    Item {
                        id: drawn
                        anchors.fill: parent
                        opacity: (Number(page.hud.opacity) || 100) / 100

                        // The players
                        Grid {
                            id: miniPlayers
                            visible: !!page.hud.players_show
                            columns: page.hud.vertical ? 1 : 3
                            spacing: 3
                            x: look.place(page.hud.players_x, look.width - width)
                            y: look.place(page.hud.players_y, look.height - height)

                            Repeater {
                                model: look.players

                                Rectangle {
                                    id: miniPlayer
                                    required property var modelData
                                    required property int index
                                    width: miniRow.implicitWidth + 8
                                    height: 15
                                    radius: 7
                                    color: look.box

                                    Row {
                                        id: miniRow
                                        x: 2
                                        anchors.verticalCenter: parent.verticalCenter
                                        spacing: 4

                                        Rectangle {
                                            visible: page.hud.avatars !== false
                                            anchors.verticalCenter: parent.verticalCenter
                                            width: 11
                                            height: 11
                                            radius: 5.5
                                            color: miniPlayer.modelData.colour
                                            border.width: 1.5
                                            border.color: page.hud.talking !== false && miniPlayer.modelData.talking ? C.Theme.good : "transparent"
                                        }

                                        Text {
                                            visible: page.hud.names !== false
                                            anchors.verticalCenter: parent.verticalCenter
                                            text: miniPlayer.modelData.name
                                            color: "#ffffff"
                                            style: look.plain ? Text.Outline : Text.Normal
                                            styleColor: "#000000"
                                            font.pixelSize: 8
                                            font.weight: Font.ExtraBold
                                        }

                                        // The controller's battery
                                        Rectangle {
                                            visible: page.hud.battery !== false
                                            anchors.verticalCenter: parent.verticalCenter
                                            width: 9
                                            height: 5
                                            radius: 1
                                            color: "transparent"
                                            border.width: 1
                                            border.color: "#d9d5f2"

                                            Rectangle {
                                                x: 1.5
                                                y: 1.5
                                                width: (parent.width - 3) * (miniPlayer.index === 2 ? 0.3 : 0.8)
                                                height: parent.height - 3
                                                color: miniPlayer.index === 2 ? C.Theme.wait : C.Theme.good
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        // A webcam
                        Rectangle {
                            visible: !!page.hud.cams_show
                            width: 58
                            height: 33
                            radius: 6
                            x: look.place(page.hud.cams_x, look.width - width)
                            y: look.place(page.hud.cams_y, look.height - height)
                            border.width: 1.5
                            border.color: C.Theme.good
                            gradient: Gradient {
                                GradientStop { position: 0.0; color: "#3b2a73" }
                                GradientStop { position: 1.0; color: "#1a1538" }
                            }
                        }

                        // Everybody's controller
                        Row {
                            id: miniPads
                            visible: !!page.hud.pads_show
                            spacing: page.hud.pads_separate ? 10 : 4
                            x: look.place(page.hud.pads_x, look.width - width)
                            y: look.place(page.hud.pads_y, look.height - height)

                            Repeater {
                                model: 3

                                Rectangle {
                                    width: 22
                                    height: 13
                                    radius: 6.5
                                    color: Qt.rgba(243 / 255, 241 / 255, 1, 0.9)
                                }
                            }
                        }

                        // The controller the console gets
                        Rectangle {
                            visible: !!page.hud.combined_show
                            width: 30
                            height: 17
                            radius: 8.5
                            x: look.place(page.hud.combined_x, look.width - width)
                            y: look.place(page.hud.combined_y, look.height - height)
                            color: Qt.rgba(243 / 255, 241 / 255, 1, 0.9)
                            border.width: 2
                            border.color: C.Theme.brandFrom
                        }

                        // Somebody joined: a note on top, below the players when they are there too
                        Rectangle {
                            readonly property bool playersAbove: miniPlayers.visible && miniPlayers.y < look.height / 3
                                                                 && miniPlayers.x < x + width && miniPlayers.x + miniPlayers.width > x
                            visible: page.hud.notices !== false
                            x: Math.round((look.width - width) / 2)
                            y: playersAbove ? miniPlayers.y + miniPlayers.height + 4 : 6
                            width: noticeText.implicitWidth + 20
                            height: 15
                            radius: 7.5
                            color: Qt.rgba(12 / 255, 10 / 255, 26 / 255, 0.72)
                            border.width: 1
                            border.color: C.Theme.good

                            Rectangle {
                                x: 6
                                anchors.verticalCenter: parent.verticalCenter
                                width: 4
                                height: 4
                                radius: 2
                                color: C.Theme.good
                            }

                            Text {
                                id: noticeText
                                x: 13
                                anchors.verticalCenter: parent.verticalCenter
                                text: qsTr("%1 joined").arg("Mia")
                                color: "#ffffff"
                                font.pixelSize: 8
                                font.weight: Font.Bold
                            }
                        }

                        // A chat message, bottom left, above the controllers when they are there
                        Rectangle {
                            readonly property bool padsBelow: miniPads.visible && miniPads.x < look.width / 2 && miniPads.y > look.height / 2
                            visible: page.hud.chat !== false
                            x: 8
                            y: (padsBelow ? miniPads.y - 5 : look.height - 8) - height
                            width: chatText.implicitWidth + 14
                            height: 17
                            radius: 8
                            color: Qt.rgba(0, 0, 0, 0.55)

                            Text {
                                id: chatText
                                anchors.centerIn: parent
                                textFormat: Text.StyledText
                                text: "<b><font color=\"#cdb6ff\">Nova</font></b> " + qsTr("left path!")
                                color: "#ffffff"
                                font.pixelSize: 8
                                font.weight: Font.Bold
                            }
                        }
                    }
                }
            }

            PresetRow {
                group: Chiaki.hud
                keywords: qsTr("presets layout arrangement game picture hud display save")
            }

            ChipRow {
                title: qsTr("Show")

                Chip {
                    text: qsTr("Players")
                    keywords: qsTr("names pictures who is playing hud")
                    checked: !!page.hud.players_show
                    onToggled: (on) => page.setHud("players_show", on)
                }

                Chip {
                    text: qsTr("Webcams in a corner")
                    keywords: qsTr("camera video cams game picture hud")
                    checked: !!page.hud.cams_show
                    onToggled: (on) => page.setHud("cams_show", on)
                }

                Chip {
                    text: qsTr("Everybody's controller")
                    keywords: qsTr("pads inputs buttons what everybody presses hud")
                    checked: !!page.hud.pads_show
                    onToggled: (on) => page.setHud("pads_show", on)
                }

                Chip {
                    text: qsTr("The console's controller")
                    keywords: qsTr("combined pad inputs what the console gets hud")
                    checked: !!page.hud.combined_show
                    onToggled: (on) => page.setHud("combined_show", on)
                }

                Chip {
                    text: qsTr("Joins and leaves")
                    keywords: qsTr("notices notes somebody joined left hud")
                    checked: page.hud.notices !== false
                    onToggled: (on) => page.setHud("notices", on)
                }

                Chip {
                    text: qsTr("New chat messages")
                    keywords: qsTr("chat text messages for a few seconds game picture hud")
                    checked: page.hud.chat !== false
                    onToggled: (on) => page.setHud("chat", on)
                }

                Chip {
                    text: qsTr("Also when I play alone")
                    keywords: qsTr("solo alone without friends hud")
                    checked: !!page.hud.solo
                    onToggled: (on) => page.setHud("solo", on)
                }
            }

            ChipRow {
                title: qsTr("For each player")

                Chip {
                    text: qsTr("Picture")
                    keywords: qsTr("avatar player hud")
                    checked: page.hud.avatars !== false
                    onToggled: (on) => page.setHud("avatars", on)
                }

                Chip {
                    text: qsTr("Name")
                    keywords: qsTr("player names hud")
                    checked: page.hud.names !== false
                    onToggled: (on) => page.setHud("names", on)
                }

                Chip {
                    text: qsTr("Lights up when talking")
                    keywords: qsTr("voice speaking ring hud")
                    checked: page.hud.talking !== false
                    onToggled: (on) => page.setHud("talking", on)
                }

                Chip {
                    text: qsTr("Controller battery")
                    keywords: qsTr("battery charge pad hud")
                    checked: page.hud.battery !== false
                    onToggled: (on) => page.setHud("battery", on)
                }
            }

            C.SettingsRow {
                title: qsTr("Look")
                subtitle: qsTr("Behind names, pictures and notes.")
                keywords: qsTr("style boxes glass transparent dark background game picture hud")
                textMinimum: 120

                C.Segmented {
                    model: page.styleNames
                    currentIndex: Math.max(0, page.styles.indexOf(page.hud.style || "transparent"))
                    onActivated: (index) => page.setHud("style", page.styles[index])
                }
            }

            C.SettingsRow {
                title: qsTr("Visibility")
                subtitle: qsTr("Lower lets more of the game show through.")
                keywords: qsTr("opacity see-through transparency fade hud")
                textMinimum: 120

                C.ValueSlider {
                    Layout.preferredWidth: 220
                    Accessible.name: qsTr("Visibility")
                    from: 30
                    to: 100
                    stepSize: 5
                    value: Number(page.hud.opacity) || 100
                    valueText: qsTr("%1 %").arg(Math.round(value))
                    onMoved: page.setHud("opacity", Math.round(value))
                }
            }

            C.SettingsRow {
                title: qsTr("Players")
                subtitle: qsTr("How the players are lined up.")
                keywords: qsTr("vertical horizontal list row column below each other hud")
                textMinimum: 120

                C.Segmented {
                    // Chiaki.hud.values.vertical
                    model: [qsTr("Side by side"), qsTr("Below each other")]
                    currentIndex: page.hud.vertical === false ? 0 : 1
                    onActivated: (index) => page.setHud("vertical", index === 1)
                }
            }

            C.SettingsRow {
                enabled: !!page.hud.pads_show
                title: qsTr("Everybody's controller")
                subtitle: qsTr("All in one row, or each placed by itself.")
                keywords: qsTr("pads separate each controller by itself place hud")
                textMinimum: 120

                C.Segmented {
                    // Chiaki.hud.values.pads_separate
                    model: [qsTr("In one row"), qsTr("Each by itself")]
                    currentIndex: page.hud.pads_separate ? 1 : 0
                    onActivated: (index) => page.setHud("pads_separate", index === 1)
                }
            }

            C.SettingsRow {
                title: qsTr("Controller colour")
                subtitle: qsTr("For the controllers drawn on the game picture.")
                keywords: qsTr("skin color dualsense pad hud")
                textMinimum: 120

                C.Dropdown {
                    // The PS5 controllers of Chiaki.settings.overlaySkins; "auto" is the white one
                    readonly property var skins: (Chiaki.settings.overlaySkins || []).filter((s) => String(s).startsWith("PS5"))
                    Accessible.name: qsTr("Controller colour")
                    model: skins.map((s) => s === "PS5" ? qsTr("White") : page.capitalised(s.replace("PS5 (", "").replace(")", "")))
                    currentIndex: Math.max(0, skins.indexOf(!page.hud.skin || page.hud.skin === "auto" ? "PS5" : page.hud.skin))
                    onActivated: (index) => page.setHud("skin", skins[index])
                }
            }

            C.SettingsRow {
                title: qsTr("Places and sizes")
                subtitle: qsTr("Drag things into place while playing: stream menu, Overlays, Arrange. Reset puts everything of this preset back.")
                keywords: qsTr("arrange move position size reset placement drag hud")
                textMinimum: 150

                C.PillButton {
                    small: true
                    iconName: "refresh"
                    text: qsTr("Reset")
                    onClicked: if (Chiaki.hud) Chiaki.hud.resetPlacement()
                }
            }

            C.SettingsRow {
                // With OpenGL the display is a window of its own, which Windows does not always show
                visible: Qt.platform.os === "windows" && Chiaki.window.runtimeRendererBackend === 1
                title: qsTr("Nothing on the game picture?")
                subtitle: qsTr("On Windows this needs the Vulkan renderer.")
                keywords: qsTr("vulkan opengl renderer windows missing hud")
                textMinimum: 150

                C.TextLink {
                    id: rendererLink
                    text: qsTr("Renderer…")
                    onClicked: page.openPage("picture", qsTr("Renderer"), rendererLink.visualFocus)
                }
            }
        }

        // ............................................ in OBS, for your viewers

        C.SettingsGroup {
            id: obsGroup
            x: page.wide ? page.columnWidth + 20 : 0
            y: page.wide ? 0 : hudGroup.implicitHeight + 20
            width: page.columnWidth
            title: qsTr("For your viewers")
            note: qsTr("in OBS, as a Browser Source")

            C.SettingsRow {
                title: qsTr("OBS overlay")
                subtitle: qsTr("A page on this computer that OBS shows over your stream.")
                keywords: qsTr("obs stream overlay browser source streaming twitch on off")
                switchItem: obsSwitch
                textMinimum: 150

                C.Badge {
                    visible: page.obsOn && (Chiaki.overlayUrl.length > 0 || Chiaki.overlayError.length > 0)
                    dot: true
                    tone: Chiaki.overlayUrl.length > 0 ? "good" : "bad"
                    text: Chiaki.overlayUrl.length > 0 ? qsTr("Running") : qsTr("Not running")
                }

                C.Toggle {
                    id: obsSwitch
                    Accessible.name: qsTr("OBS overlay")
                    checked: page.obsOn
                    onToggled: Chiaki.settings.overlayEnabled = checked
                }
            }

            C.SettingsRow {
                visible: page.obsOn
                title: Chiaki.overlayError.length > 0 && Chiaki.overlayUrl.length === 0
                       ? qsTr("The overlay could not start") : qsTr("Address for OBS")
                subtitle: Chiaki.overlayError.length > 0 && Chiaki.overlayUrl.length === 0
                          ? qsTr("%1. Try another port.").arg(Chiaki.overlayError)
                          : qsTr("Add it to OBS as a Browser Source.")
                keywords: qsTr("url link localhost copy browser source")
                textMinimum: 150

                Rectangle {
                    visible: Chiaki.overlayUrl.length > 0
                    Layout.preferredWidth: 220
                    Layout.fillWidth: true
                    implicitHeight: 40
                    radius: C.Theme.radiusField
                    color: C.Theme.field
                    border.width: 1
                    border.color: Qt.rgba(1, 1, 1, 0.12)

                    TextInput {
                        anchors {
                            fill: parent
                            leftMargin: 12
                            rightMargin: 12
                        }
                        verticalAlignment: TextInput.AlignVCenter
                        clip: true
                        readOnly: true
                        selectByMouse: true
                        text: Chiaki.overlayUrl
                        color: "#e7e4fa"
                        selectionColor: C.Theme.button
                        selectedTextColor: "#ffffff"
                        font.family: Qt.platform.os === "osx" ? "Menlo" : Qt.platform.os === "windows" ? "Consolas" : "monospace"
                        font.pixelSize: 13
                    }
                }

                C.PillButton {
                    visible: Chiaki.overlayUrl.length > 0
                    small: true
                    kind: "primary"
                    text: copiedTimer.running ? qsTr("Copied") : qsTr("Copy")
                    onClicked: {
                        Chiaki.copyOverlayUrl();
                        copiedTimer.restart();
                    }

                    Timer {
                        id: copiedTimer
                        interval: 1500
                    }
                }
            }

            C.SettingsRow {
                title: qsTr("Port of the OBS overlay")
                subtitle: qsTr("Change it if the overlay cannot start, or another program uses it.")
                keywords: qsTr("port number tcp localhost")
                textMinimum: 150

                C.Field {
                    Layout.preferredWidth: 96
                    Accessible.name: qsTr("Port of the OBS overlay")
                    validator: IntValidator { bottom: 1; top: 65535 }
                    inputMethodHints: Qt.ImhDigitsOnly
                    text: Chiaki.settings.overlayPort
                    onEditingFinished: {
                        if (acceptableInput)
                            Chiaki.settings.overlayPort = parseInt(text);
                        else
                            text = Chiaki.settings.overlayPort;
                    }
                }
            }

            PresetRow {
                enabled: page.obsOn
                group: Chiaki.overlayLayout
                keywords: qsTr("presets layout obs overlay save")
            }

            ChipRow {
                enabled: page.obsOn
                title: qsTr("Show")

                Chip {
                    text: qsTr("Names and pictures")
                    keywords: qsTr("players avatars obs overlay")
                    checked: !!Chiaki.settings.overlayPlayers
                    onToggled: (on) => Chiaki.settings.overlayPlayers = on
                }

                Chip {
                    text: qsTr("Webcams")
                    keywords: qsTr("camera video cams obs overlay")
                    checked: !!Chiaki.settings.overlayWebcams
                    onToggled: (on) => Chiaki.settings.overlayWebcams = on
                }

                Chip {
                    text: qsTr("Everybody's controller")
                    keywords: qsTr("pads inputs buttons obs overlay")
                    checked: !!Chiaki.settings.overlayPads
                    onToggled: (on) => Chiaki.settings.overlayPads = on
                }

                Chip {
                    text: qsTr("The console's controller")
                    keywords: qsTr("combined pad obs overlay")
                    checked: !!Chiaki.settings.overlayCombined
                    onToggled: (on) => Chiaki.settings.overlayCombined = on
                }

                Chip {
                    text: qsTr("Controller battery")
                    keywords: qsTr("battery charge obs overlay")
                    checked: page.obs.battery !== false
                    onToggled: (on) => page.setObs("battery", on)
                }

                Chip {
                    text: qsTr("Joins and leaves")
                    keywords: qsTr("notices notes joined left obs overlay")
                    checked: page.obs.notices !== false
                    onToggled: (on) => page.setObs("notices", on)
                }

                Chip {
                    text: qsTr("The chat")
                    keywords: qsTr("chat messages viewers obs overlay")
                    checked: page.obs.chat !== false
                    onToggled: (on) => page.setObs("chat", on)
                }
            }

            C.SettingsRow {
                enabled: page.obsOn
                title: qsTr("Play the players' conversation")
                subtitle: qsTr("The voice chat, for your viewers to hear.")
                keywords: qsTr("voice chat sound audio obs overlay")
                switchItem: voiceSwitch
                textMinimum: 150

                C.Toggle {
                    id: voiceSwitch
                    Accessible.name: qsTr("Play the players' conversation")
                    checked: !!Chiaki.settings.overlayVoice
                    onToggled: Chiaki.settings.overlayVoice = checked
                }
            }

            C.SettingsRow {
                sub: true
                enabled: page.obsOn && voiceSwitch.checked
                title: qsTr("Include my own voice")
                subtitle: qsTr("Leave it off when OBS records your microphone already, or you are heard twice.")
                keywords: qsTr("microphone own voice twice echo obs overlay")
                switchItem: ownVoiceSwitch
                textMinimum: 150

                C.Toggle {
                    id: ownVoiceSwitch
                    Accessible.name: qsTr("Include my own voice")
                    checked: !!Chiaki.settings.overlayOwnVoice
                    onToggled: Chiaki.settings.overlayOwnVoice = checked
                }
            }

            C.SettingsRow {
                enabled: page.obsOn
                title: qsTr("Look")
                subtitle: qsTr("Behind names and pictures.")
                keywords: qsTr("style boxes glass transparent dark background obs overlay")
                textMinimum: 120

                C.Segmented {
                    model: page.styleNames
                    currentIndex: Math.max(0, page.styles.indexOf(page.obs.style || "transparent"))
                    onActivated: (index) => page.setObs("style", page.styles[index])
                }
            }

            C.SettingsRow {
                enabled: page.obsOn
                title: qsTr("Controllers")
                subtitle: qsTr("How the controllers look.")
                keywords: qsTr("skin color colour dualsense xbox ps4 pad obs overlay")
                textMinimum: 120

                C.Dropdown {
                    Layout.maximumWidth: 260
                    Accessible.name: qsTr("Controllers")
                    model: [qsTr("Like each player's controller")].concat(Chiaki.settings.overlaySkins || [])
                    currentIndex: Math.max(0, (Chiaki.settings.overlaySkins || []).indexOf(Chiaki.settings.overlaySkin) + 1)
                    onActivated: (index) => Chiaki.settings.overlaySkin = index === 0 ? "auto" : Chiaki.settings.overlaySkins[index - 1]
                }
            }

            C.SettingsRow {
                enabled: page.obsOn
                title: qsTr("Controller size")
                keywords: qsTr("scale size controllers obs overlay")
                textMinimum: 120

                C.ValueSlider {
                    Layout.preferredWidth: 220
                    Accessible.name: qsTr("Controller size")
                    from: 10
                    to: 100
                    stepSize: 5
                    value: Chiaki.settings.overlayScale
                    valueText: qsTr("%1 %").arg(Math.round(value))
                    onMoved: Chiaki.settings.overlayScale = Math.round(value)
                }
            }

            C.SettingsRow {
                enabled: page.obsOn
                title: qsTr("Players")
                subtitle: qsTr("How the players are lined up.")
                keywords: qsTr("layout row column below each other side by side obs overlay")
                textMinimum: 120

                C.Segmented {
                    // Chiaki.settings.overlayLayout: "row" or "column"
                    model: [qsTr("Side by side"), qsTr("Below each other")]
                    currentIndex: Chiaki.settings.overlayLayout === "column" ? 1 : 0
                    onActivated: (index) => Chiaki.settings.overlayLayout = index === 1 ? "column" : "row"
                }
            }

            C.SettingsRow {
                enabled: page.obsOn
                title: qsTr("Place freely")
                subtitle: qsTr("Every player and the console's controller where you drag them in the browser.")
                keywords: qsTr("free placement position drag obs overlay")
                switchItem: freeSwitch
                textMinimum: 150

                C.Toggle {
                    id: freeSwitch
                    Accessible.name: qsTr("Place freely")
                    checked: !!page.obs.free
                    onToggled: page.setObs("free", checked)
                }
            }

            C.SettingsRow {
                enabled: page.obsOn
                title: qsTr("Arrange in the browser")
                // A word joiner after each "?": the line never breaks inside "?show=…"
                subtitle: qsTr("OBS follows at once. For sources that show different things, add %1 or %2 to the address.")
                          .arg("?⁠show=players").arg("?⁠show=combined")
                keywords: qsTr("arrange edit drag places reset positions show players combined obs overlay")
                textMinimum: 150

                C.PillButton {
                    small: true
                    iconName: "external"
                    text: qsTr("Arrange")
                    enabled: Chiaki.overlayUrl.length > 0
                    onClicked: {
                        page.setObs("free", true);
                        Qt.openUrlExternally(Chiaki.overlayUrl + "?edit=1");
                    }
                }

                C.PillButton {
                    small: true
                    iconName: "refresh"
                    text: qsTr("Reset")
                    onClicked: if (Chiaki.overlayLayout) Chiaki.overlayLayout.resetPlacement()
                }
            }
        }
    }
}
