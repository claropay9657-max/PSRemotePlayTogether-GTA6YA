import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.Material

import org.streetpea.chiaking

import "controls" as C

// Everything about what is shown around the game: the display on top of the
// game picture, and the OBS overlay. Each has its switches and its presets.
// Used on the Play Together page and, while playing, from the stream menu:
// whatever is changed here takes effect at once.
Item {
    id: options

    /** True while a game is running: only then can things be arranged on its picture. */
    property bool inSession: !!Chiaki.session
    /** Which half to show: "both", "hud" or "overlay". */
    property string part: "both"
    /** The user wants to drag the elements on the game picture into place. */
    signal arrangeRequested()

    readonly property var hud: Chiaki.hud
    readonly property var obs: Chiaki.overlayLayout
    readonly property bool wide: width >= 880 && part === "both"

    implicitHeight: grid.implicitHeight
    implicitWidth: 880

    readonly property color accent: "#8c52ff"
    readonly property color cyan: "#00f2ff"

    component Card: Rectangle {
        default property alias content: cardColumn.data
        property string title
        property string subtitle
        Layout.fillWidth: true
        Layout.alignment: Qt.AlignTop
        implicitHeight: cardColumn.implicitHeight + 36
        radius: 16
        color: Qt.rgba(16 / 255, 16 / 255, 24 / 255, 0.72)
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, 0.09)

        ColumnLayout {
            id: cardColumn
            anchors {
                left: parent.left
                right: parent.right
                top: parent.top
                margins: 18
            }
            spacing: 8

            C.GradientText {
                text: title
                font.pixelSize: 21
                font.bold: true
                to: "#7c86ff"
            }

            Label {
                Layout.fillWidth: true
                visible: subtitle.length > 0
                wrapMode: Text.WordWrap
                opacity: 0.68
                font.pixelSize: 14
                text: subtitle
            }
        }
    }

    component Toggle: Switch {
        Layout.fillWidth: true
        font.pixelSize: 15
        topPadding: 9
        bottomPadding: 9
    }

    component Caption: Label {
        Layout.topMargin: 8
        font.pixelSize: 12
        font.bold: true
        font.capitalization: Font.AllUppercase
        font.letterSpacing: 0.8
        opacity: 0.55
    }

    /** Choose, add, rename and remove presets of one group of settings. */
    component PresetRow: ColumnLayout {
        id: presetRow
        property var group
        /** "", "new" or "rename" */
        property string mode: ""
        Layout.fillWidth: true
        spacing: 6

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            ComboBox {
                Layout.fillWidth: true
                font.pixelSize: 15
                model: presetRow.group.presets
                currentIndex: presetRow.group.active
                onActivated: (index) => presetRow.group.active = index
            }

            Button {
                flat: true
                font.pixelSize: 14
                text: qsTr("New")
                onClicked: {
                    presetRow.mode = "new";
                    nameField.text = "";
                    nameField.forceActiveFocus();
                }
            }

            Button {
                flat: true
                font.pixelSize: 14
                text: qsTr("Rename")
                onClicked: {
                    presetRow.mode = "rename";
                    nameField.text = presetRow.group.presets[presetRow.group.active] || "";
                    nameField.forceActiveFocus();
                    nameField.selectAll();
                }
            }

            Button {
                flat: true
                font.pixelSize: 14
                text: qsTr("Delete")
                enabled: presetRow.group.presets.length > 1
                onClicked: presetRow.group.removePreset()
            }

            Button {
                flat: true
                font.pixelSize: 14
                text: restoreTimer.running ? qsTr("Sure?") : qsTr("Defaults")
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Throw all presets away and start over with the built-in ones")
                onClicked: {
                    if (restoreTimer.running) {
                        restoreTimer.stop();
                        presetRow.group.resetAll();
                    } else {
                        restoreTimer.restart();
                    }
                }
                Timer {
                    id: restoreTimer
                    interval: 3000
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            visible: presetRow.mode !== ""
            spacing: 8

            TextField {
                id: nameField
                Layout.fillWidth: true
                font.pixelSize: 15
                maximumLength: 40
                placeholderText: presetRow.mode === "new" ? qsTr("Name of the new preset") : qsTr("New name")
                onAccepted: saveButton.clicked()
                Keys.onEscapePressed: presetRow.mode = ""
            }

            Button {
                id: saveButton
                highlighted: true
                font.pixelSize: 14
                text: presetRow.mode === "new" ? qsTr("Save as new") : qsTr("Rename")
                enabled: nameField.text.trim().length > 0
                onClicked: {
                    if (presetRow.mode === "new")
                        presetRow.group.addPreset(nameField.text);
                    else
                        presetRow.group.renamePreset(nameField.text);
                    presetRow.mode = "";
                }
            }

            Button {
                flat: true
                font.pixelSize: 14
                text: qsTr("Cancel")
                onClicked: presetRow.mode = ""
            }
        }
    }

    GridLayout {
        id: grid
        anchors {
            left: parent.left
            right: parent.right
        }
        columns: options.wide ? 2 : 1
        columnSpacing: 18
        rowSpacing: 18

        // ---------------------------------------------------- on the game picture

        Card {
            visible: options.part !== "overlay"
            title: qsTr("On the game picture")
            subtitle: qsTr("What you see on top of the game while playing. Only you see this; for your viewers there is the OBS overlay.")

            Caption { text: qsTr("Preset") }
            PresetRow { group: options.hud }

            Caption { text: qsTr("Show") }
            Toggle {
                text: qsTr("Players")
                checked: options.hud.values.players_show
                onToggled: options.hud.values.players_show = checked
            }
            Toggle {
                text: qsTr("Webcams")
                checked: options.hud.values.cams_show
                onToggled: options.hud.values.cams_show = checked
            }
            Toggle {
                text: qsTr("Everybody's controller")
                checked: options.hud.values.pads_show
                onToggled: options.hud.values.pads_show = checked
            }
            Toggle {
                text: qsTr("Place each controller by itself")
                enabled: options.hud.values.pads_show
                checked: options.hud.values.pads_separate
                onToggled: options.hud.values.pads_separate = checked
            }
            Toggle {
                text: qsTr("The controller the console gets")
                checked: options.hud.values.combined_show
                onToggled: options.hud.values.combined_show = checked
            }
            Toggle {
                text: qsTr("A note when somebody joins or leaves")
                checked: options.hud.values.notices
                onToggled: options.hud.values.notices = checked
            }
            Toggle {
                text: qsTr("New chat messages, for a few seconds")
                checked: options.hud.values.chat
                onToggled: options.hud.values.chat = checked
            }

            Caption { text: qsTr("For each player") }
            GridLayout {
                Layout.fillWidth: true
                columns: width >= 620 ? 2 : 1
                columnSpacing: 10
                rowSpacing: 0

                Toggle {
                    text: qsTr("Picture")
                    checked: options.hud.values.avatars
                    onToggled: options.hud.values.avatars = checked
                }
                Toggle {
                    text: qsTr("Name")
                    checked: options.hud.values.names
                    onToggled: options.hud.values.names = checked
                }
                Toggle {
                    text: qsTr("Lights up when talking")
                    checked: options.hud.values.talking
                    onToggled: options.hud.values.talking = checked
                }
                Toggle {
                    text: qsTr("Controller battery")
                    checked: options.hud.values.battery
                    onToggled: options.hud.values.battery = checked
                }
            }
            Toggle {
                text: qsTr("List the players below each other")
                checked: options.hud.values.vertical
                onToggled: options.hud.values.vertical = checked
            }
            Toggle {
                text: qsTr("Also when I play alone")
                checked: options.hud.values.solo
                onToggled: options.hud.values.solo = checked
            }

            Caption { text: qsTr("Look") }
            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                Label {
                    font.pixelSize: 15
                    text: qsTr("Style")
                }
                ComboBox {
                    Layout.fillWidth: true
                    font.pixelSize: 15
                    readonly property var styles: ["transparent", "glass", "solid"]
                    model: [qsTr("Transparent, no boxes"), qsTr("Glass boxes"), qsTr("Dark boxes")]
                    currentIndex: Math.max(0, styles.indexOf(options.hud.values.style))
                    onActivated: (index) => options.hud.values.style = styles[index]
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                Label {
                    font.pixelSize: 15
                    text: qsTr("See-through")
                }
                Slider {
                    Layout.fillWidth: true
                    from: 30
                    to: 100
                    stepSize: 5
                    value: options.hud.values.opacity
                    onMoved: options.hud.values.opacity = value
                }
                Label {
                    Layout.preferredWidth: 46
                    horizontalAlignment: Text.AlignRight
                    font.pixelSize: 15
                    text: qsTr("%1 %").arg(options.hud.values.opacity)
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                Label {
                    font.pixelSize: 15
                    text: qsTr("Controller colour")
                }
                ComboBox {
                    id: hudSkinCombo
                    Layout.fillWidth: true
                    font.pixelSize: 15
                    readonly property var skins: Chiaki.settings.overlaySkins.filter((s) => s.startsWith("PS5"))
                    model: skins.map((s) => s === "PS5" ? qsTr("White") : s.replace("PS5 (", "").replace(")", ""))
                    currentIndex: Math.max(0, skins.indexOf(options.hud.values.skin === "auto" ? "PS5" : options.hud.values.skin))
                    onActivated: (index) => options.hud.values.skin = skins[index]
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 10
                spacing: 10

                Button {
                    highlighted: true
                    font.pixelSize: 15
                    text: qsTr("Arrange on the game picture")
                    enabled: options.inSession
                    onClicked: options.arrangeRequested()
                }

                Button {
                    flat: true
                    font.pixelSize: 15
                    text: qsTr("Reset places and sizes")
                    onClicked: options.hud.resetPlacement()
                }
            }

            Label {
                Layout.fillWidth: true
                visible: !options.inSession
                wrapMode: Text.WordWrap
                opacity: 0.6
                font.pixelSize: 13
                text: qsTr("Arranging works while a game is running: open the stream menu and choose Overlays.")
            }
        }

        // ---------------------------------------------------- OBS overlay

        Card {
            visible: options.part !== "hud"
            title: qsTr("OBS overlay")
            subtitle: qsTr("For your viewers: add the address below to OBS as a Browser Source. Every player can run their own.")

            Toggle {
                id: overlaySwitch
                text: qsTr("Overlay on")
                checked: Chiaki.settings.overlayEnabled
                onToggled: Chiaki.settings.overlayEnabled = checked
            }

            RowLayout {
                Layout.fillWidth: true
                visible: overlaySwitch.checked
                spacing: 8

                TextField {
                    Layout.fillWidth: true
                    readOnly: true
                    selectByMouse: true
                    font.pixelSize: 15
                    text: Chiaki.overlayUrl
                }

                Button {
                    font.pixelSize: 14
                    text: copiedTimer.running ? qsTr("Copied") : qsTr("Copy")
                    enabled: Chiaki.overlayUrl.length > 0
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

            Label {
                Layout.fillWidth: true
                visible: overlaySwitch.checked && Chiaki.overlayError.length > 0
                wrapMode: Text.WordWrap
                color: Material.color(Material.Red, Material.Shade300)
                font.pixelSize: 14
                text: qsTr("The overlay could not start: %1. Try another port.").arg(Chiaki.overlayError)
            }

            ColumnLayout {
                Layout.fillWidth: true
                enabled: overlaySwitch.checked
                opacity: enabled ? 1.0 : 0.4
                spacing: 8

                Caption { text: qsTr("Preset") }
                PresetRow { group: options.obs }

                Caption { text: qsTr("Show") }
                GridLayout {
                    Layout.fillWidth: true
                    columns: width >= 620 ? 2 : 1
                    columnSpacing: 10
                    rowSpacing: 0

                    Toggle {
                        text: qsTr("Names and pictures")
                        checked: Chiaki.settings.overlayPlayers
                        onToggled: Chiaki.settings.overlayPlayers = checked
                    }
                    Toggle {
                        text: qsTr("Webcams")
                        checked: Chiaki.settings.overlayWebcams
                        onToggled: Chiaki.settings.overlayWebcams = checked
                    }
                    Toggle {
                        text: qsTr("Everybody's controller")
                        checked: Chiaki.settings.overlayPads
                        onToggled: Chiaki.settings.overlayPads = checked
                    }
                    Toggle {
                        text: qsTr("Console's controller")
                        checked: Chiaki.settings.overlayCombined
                        onToggled: Chiaki.settings.overlayCombined = checked
                    }
                    Toggle {
                        text: qsTr("Controller battery")
                        checked: options.obs.values.battery
                        onToggled: options.obs.values.battery = checked
                    }
                    Toggle {
                        text: qsTr("A note when somebody joins or leaves")
                        checked: options.obs.values.notices
                        onToggled: options.obs.values.notices = checked
                    }
                    Toggle {
                        text: qsTr("The chat, for the viewers")
                        checked: options.obs.values.chat
                        onToggled: options.obs.values.chat = checked
                    }
                }

                Caption { text: qsTr("Sound") }
                Toggle {
                    text: qsTr("Play the players' conversation")
                    checked: Chiaki.settings.overlayVoice
                    onToggled: Chiaki.settings.overlayVoice = checked
                }
                Toggle {
                    text: qsTr("Include my own voice")
                    enabled: Chiaki.settings.overlayVoice
                    checked: Chiaki.settings.overlayOwnVoice
                    onToggled: Chiaki.settings.overlayOwnVoice = checked
                }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    opacity: 0.6
                    font.pixelSize: 13
                    text: qsTr("Leave your own voice off if OBS already records your microphone, or you would be heard twice.")
                }

                Caption { text: qsTr("Look") }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    Label {
                        font.pixelSize: 15
                        text: qsTr("Style")
                    }
                    ComboBox {
                        Layout.fillWidth: true
                        font.pixelSize: 15
                        readonly property var styles: ["transparent", "glass", "solid"]
                        model: [qsTr("Transparent, no boxes"), qsTr("Glass boxes"), qsTr("Dark boxes")]
                        currentIndex: Math.max(0, styles.indexOf(options.obs.values.style))
                        onActivated: (index) => options.obs.values.style = styles[index]
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    Label {
                        font.pixelSize: 15
                        text: qsTr("Controllers")
                    }
                    ComboBox {
                        Layout.fillWidth: true
                        font.pixelSize: 15
                        model: [qsTr("Match each player's controller")].concat(Chiaki.settings.overlaySkins)
                        currentIndex: Math.max(0, Chiaki.settings.overlaySkins.indexOf(Chiaki.settings.overlaySkin) + 1)
                        onActivated: (index) => Chiaki.settings.overlaySkin = index === 0 ? "auto" : Chiaki.settings.overlaySkins[index - 1]
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    Label {
                        font.pixelSize: 15
                        text: qsTr("Controller size")
                    }
                    Slider {
                        Layout.fillWidth: true
                        from: 10
                        to: 100
                        stepSize: 5
                        value: Chiaki.settings.overlayScale
                        onMoved: Chiaki.settings.overlayScale = value
                    }
                    Label {
                        Layout.preferredWidth: 46
                        horizontalAlignment: Text.AlignRight
                        font.pixelSize: 15
                        text: qsTr("%1 %").arg(Chiaki.settings.overlayScale)
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    Label {
                        font.pixelSize: 15
                        text: qsTr("Players")
                    }
                    ComboBox {
                        Layout.fillWidth: true
                        font.pixelSize: 15
                        model: [qsTr("Side by side"), qsTr("Below each other")]
                        currentIndex: Chiaki.settings.overlayLayout === "column" ? 1 : 0
                        onActivated: (index) => Chiaki.settings.overlayLayout = index === 1 ? "column" : "row"
                    }
                }

                Caption { text: qsTr("Placement") }
                Toggle {
                    text: qsTr("Place every player and the console's controller freely")
                    checked: options.obs.values.free
                    onToggled: options.obs.values.free = checked
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    Button {
                        highlighted: true
                        font.pixelSize: 15
                        text: qsTr("Arrange in the browser")
                        enabled: Chiaki.overlayUrl.length > 0
                        onClicked: {
                            options.obs.values.free = true;
                            Qt.openUrlExternally(Chiaki.overlayUrl + "?edit=1");
                        }
                    }

                    Button {
                        flat: true
                        font.pixelSize: 15
                        text: qsTr("Reset places")
                        onClicked: options.obs.resetPlacement()
                    }
                }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    opacity: 0.6
                    font.pixelSize: 13
                    text: qsTr("Drag every player and the console's controller into place in the browser; OBS follows at once. For several sources that show different things, add for example ?show=players or ?show=combined to the address.")
                }
            }
        }
    }
}
