import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

import org.streetpea.chiaking

import "controls" as C

// The renderer's own options (libplacebo), in the look of the settings window: the
// sections in a list on the left, the one shown on the right. Saved as you go.
// Also shown over the game (Main.qml's placeboSettingsLoader, 1200 × 650, or a window
// of its own): every section scrolls, and the setting with the focus stays on screen.
//
// Keys and controller: the list is one stop in the focus chain. Up and Down there go
// through the sections, Right or Return goes into the section shown; Up from its first
// setting or Left comes back. Page Up / Page Down (L1 / R1) change the section from
// anywhere; from inside a section the focus goes into the new one.
DialogView {
    id: dialog
    property string options_url: ""
    title: qsTr("Renderer")
    header: qsTr("Advanced picture processing. Changes are saved at once")
    buttonVisible: false

    readonly property var sections: [qsTr("General"), qsTr("Scaling"), qsTr("Deinterlacing"), qsTr("Debanding"),
        qsTr("Sigmoidization"), qsTr("Colour"), qsTr("Peak detection"), qsTr("Colour mapping")]
    /** The section shown, by its place in `sections`. */
    property int section: 0

    readonly property bool narrow: width < 900
    readonly property int gutter: width < 600 ? 16 : 32
    /** Width of a section: the room there is, up to a width that still reads well. */
    readonly property real pageWidth: Math.max(0, Math.min(pages.width - gutter, 920))
    /** Width of the column the controls sit in: up to 380, less where there is little room. */
    readonly property int controlWidth: Math.round(Math.min(380, Math.max(180, (pageWidth - 36) * 0.45)))

    function showSection(index) {
        if (index < 0 || index >= sections.length || index === section)
            return;
        // From inside a section (L1 / R1) the focus goes along into the new one
        const fromPage = !side.activeFocus;
        section = index;
        if (fromPage)
            Qt.callLater(enterSection);
    }

    /** Into the section shown: its first setting gets the focus, or the list keeps it. */
    function enterSection() {
        const flick = pages.children[section];
        const first = flick ? flick.contentItem.nextItemInFocusChain(true) : null;
        if (first && inside(first, flick))
            first.forceActiveFocus(Qt.TabFocusReason);
        else
            side.forceActiveFocus(Qt.TabFocusReason);
    }

    function inside(item, ancestor) {
        for (let p = item; p; p = p.parent) {
            if (p === ancestor)
                return true;
        }
        return false;
    }

    /** The row (or the item) that should be on screen when `item` has the focus. */
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

    Keys.onPressed: (event) => {
        if (event.modifiers)
            return;
        switch (event.key) {
        case Qt.Key_PageUp:
            showSection(section - 1);
            event.accepted = true;
            break;
        case Qt.Key_PageDown:
            showSection(section + 1);
            event.accepted = true;
            break;
        case Qt.Key_Left: {
            // Left that no setting took: back to the list
            const item = dialog.Window.window ? dialog.Window.window.activeFocusItem : null;
            // Not while a dropdown's list is open: Left there means nothing yet
            if (item && inside(item, pages) && !(item.popup && item.popup.visible)) {
                side.forceActiveFocus(Qt.TabFocusReason);
                event.accepted = true;
            }
            break;
        }
        }
    }

    /** A section: scrolls, its groups one under the other. */
    component Page: Flickable {
        id: pageFlick
        default property alias content: pageColumn.data
        contentWidth: width
        contentHeight: pageColumn.implicitHeight + 18 + 32
        clip: true
        flickableDirection: Flickable.VerticalFlick
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {
            policy: pageFlick.contentHeight > pageFlick.height ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
            width: 10
        }

        ColumnLayout {
            id: pageColumn
            y: 18
            width: dialog.pageWidth
            spacing: 18
        }
    }

    component Setting: C.SettingRow {
        controlWidth: dialog.controlWidth
    }

    /** An amount; set `valueText` in the decimals the setting has. */
    component Amount: C.ValueSlider {
        Layout.fillWidth: true
        valueWidth: 72
    }

    Item {
        // Keeps the setting with the focus on screen, also over the game where there is little room
        Connections {
            target: dialog.Window.window
            function onActiveFocusItemChanged() {
                const item = dialog.Window.window ? dialog.Window.window.activeFocusItem : null;
                const flick = pages.children[dialog.section];
                if (item && flick && dialog.inside(item, flick.contentItem))
                    dialog.scrollTo(dialog.rowOf(item, flick), flick, 12);
            }
        }

        // ------------------------------------------------ the sections, down the left side

        Flickable {
            id: sideFlick
            anchors {
                top: parent.top
                left: parent.left
                bottom: parent.bottom
                leftMargin: dialog.gutter
            }
            width: dialog.narrow ? 190 : 220
            contentWidth: width
            contentHeight: side.implicitHeight + 18 + 24
            clip: true
            flickableDirection: Flickable.VerticalFlick
            boundsBehavior: Flickable.StopAtBounds

            Control {
                id: side
                y: 18
                width: sideFlick.width
                focusPolicy: Qt.StrongFocus
                padding: 0
                Accessible.role: Accessible.PageTabList
                Accessible.name: qsTr("Renderer sections")

                Keys.onPressed: (event) => {
                    switch (event.key) {
                    case Qt.Key_Up:
                        dialog.showSection(dialog.section - 1);
                        event.accepted = true;
                        break;
                    case Qt.Key_Down:
                        dialog.showSection(dialog.section + 1);
                        event.accepted = true;
                        break;
                    case Qt.Key_Right:
                    case Qt.Key_Return:
                    case Qt.Key_Enter:
                    case Qt.Key_Space:
                        dialog.enterSection();
                        event.accepted = true;
                        break;
                    }
                }

                contentItem: Column {
                    spacing: 2

                    Repeater {
                        model: dialog.sections

                        Rectangle {
                            id: entry
                            required property var modelData
                            required property int index
                            readonly property bool current: index === dialog.section

                            width: side.width
                            height: 36
                            radius: 10
                            color: current ? Qt.rgba(140 / 255, 82 / 255, 1, 0.20)
                                 : entryMouse.containsMouse ? Qt.rgba(1, 1, 1, 0.05) : "transparent"
                            border.width: side.visualFocus && current ? 2 : 0
                            border.color: C.Theme.focus

                            Text {
                                anchors {
                                    left: parent.left
                                    right: parent.right
                                    verticalCenter: parent.verticalCenter
                                    leftMargin: 12
                                    rightMargin: 10
                                }
                                elide: Text.ElideRight
                                text: entry.modelData
                                color: entry.current ? "#ffffff" : "#d9d5f2"
                                font.pixelSize: 14
                                font.weight: entry.current ? Font.ExtraBold : Font.DemiBold
                            }

                            MouseArea {
                                id: entryMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    side.forceActiveFocus(Qt.MouseFocusReason);
                                    dialog.showSection(entry.index);
                                }
                            }
                        }
                    }

                    Item { width: 1; height: 14 }

                    // L1 / R1 change the section, once a controller is there
                    Row {
                        visible: Chiaki.controllers.length > 0
                        spacing: 6
                        leftPadding: 12

                        Image {
                            width: 22
                            height: 22
                            sourceSize: Qt.size(width, height)
                            source: "qrc:/icons/l1.svg"
                        }

                        Image {
                            width: 22
                            height: 22
                            sourceSize: Qt.size(width, height)
                            source: "qrc:/icons/r1.svg"
                        }

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("change the section")
                            color: C.Theme.textDim
                            font.pixelSize: 12
                        }
                    }
                }
            }
        }

        // ------------------------------------------------ the section shown

        StackLayout {
            id: pages
            anchors {
                top: parent.top
                left: sideFlick.right
                right: parent.right
                bottom: parent.bottom
                leftMargin: dialog.narrow ? 18 : 28
            }
            currentIndex: dialog.section

            Page {
                // General
                C.SettingsSection {
                    title: qsTr("Renderer")
                    description: qsTr("The picture goes through libplacebo. This is for fine-tuning: for most screens the quality presets in Settings › Picture are right.")

                    Setting {
                        title: qsTr("Documentation")
                        description: dialog.options_url.length > 0
                                     ? qsTr("It opened in your web browser. If it did not, copy the address and paste it there.")
                                     : qsTr("What every option does, on the libplacebo website.")

                        // Only for copying the address; hidden in streamer mode as before
                        TextField {
                            id: openurl
                            echoMode: Chiaki.settings.streamerMode ? TextInput.Password : TextInput.Normal
                            text: dialog.options_url
                            visible: false
                        }
                        Item { Layout.fillWidth: true }
                        C.PillButton {
                            id: openwebpage
                            small: true
                            iconName: "external"
                            text: qsTr("Open documentation")
                            visible: dialog.options_url.length === 0
                            onClicked: {
                                const byKeys = visualFocus;
                                dialog.options_url = Chiaki.openPlaceboOptionsLink();
                                // This button goes; the keys go on with the one in its place
                                if (byKeys)
                                    Qt.callLater(() => copyUrl.forceActiveFocus(Qt.TabFocusReason));
                            }
                        }
                        C.PillButton {
                            id: copyUrl
                            small: true
                            iconName: "copy"
                            text: qsTr("Copy the address")
                            visible: dialog.options_url.length > 0
                            onClicked: {
                                openurl.selectAll();
                                openurl.copy();
                            }
                        }
                    }

                    Setting {
                        title: qsTr("Settings file")
                        description: qsTr("Save these settings to a file, or load them from one.")

                        Item { Layout.fillWidth: true }
                        C.PillButton {
                            id: exportButton
                            small: true
                            text: qsTr("Export…")
                            onClicked: Chiaki.settings.exportPlaceboSettings()
                            KeyNavigation.down: importButton
                            KeyNavigation.right: importButton
                        }
                        C.PillButton {
                            id: importButton
                            small: true
                            lastInFocusChain: true
                            text: qsTr("Import…")
                            onClicked: Chiaki.settings.importPlaceboSettings()
                            KeyNavigation.up: exportButton
                            KeyNavigation.left: exportButton
                        }
                    }
                }
            }

            Page {
                // Scaling
                C.SettingsSection {
                    title: qsTr("Scaling")
                    description: qsTr("How the picture is made larger or smaller for your screen.")

                    Setting {
                        title: qsTr("Upscaler")
                        description: qsTr("Makes the picture larger.")
                        hint: qsTr("EwaLanczosSharp")

                        C.Dropdown {
                            Layout.fillWidth: true
                            Accessible.name: qsTr("Upscaler")
                            model: [qsTr("Custom"), qsTr("Nearest"), qsTr("Bilinear"), qsTr("Oversample"), qsTr("Bicubic"), qsTr("Gaussian"), qsTr("Catmull-Rom"), qsTr("Lanczos"), qsTr("EwaLanczos"), qsTr("EwaLanczosSharp"), qsTr("EwaLanczosSharpest"), qsTr("FSR"), qsTr("FSRCNNX x2 8-0-4-1"), qsTr("FSRCNNX x2 16-0-4-1")]
                            currentIndex: Chiaki.settings.placeboUpscaler
                            onActivated: index => Chiaki.settings.placeboUpscaler = index
                        }
                    }

                    Setting {
                        title: qsTr("Plane upscaler")
                        description: qsTr("For the colour planes.")
                        hint: qsTr("Same as the upscaler")

                        C.Dropdown {
                            Layout.fillWidth: true
                            Accessible.name: qsTr("Plane upscaler")
                            model: [qsTr("Same as the upscaler"), qsTr("Nearest"), qsTr("Bilinear"), qsTr("Oversample"), qsTr("Bicubic"), qsTr("Gaussian"), qsTr("Catmull-Rom"), qsTr("Lanczos"), qsTr("EwaLanczos"), qsTr("EwaLanczosSharp"), qsTr("EwaLanczosSharpest")]
                            currentIndex: Chiaki.settings.placeboPlaneUpscaler
                            onActivated: index => Chiaki.settings.placeboPlaneUpscaler = index
                        }
                    }

                    Setting {
                        title: qsTr("Downscaler")
                        description: qsTr("Makes the picture smaller.")
                        hint: qsTr("Hermite")

                        C.Dropdown {
                            Layout.fillWidth: true
                            Accessible.name: qsTr("Downscaler")
                            model: [qsTr("Custom"), qsTr("Box"), qsTr("Hermite"), qsTr("Bilinear"), qsTr("Bicubic"), qsTr("Gaussian"), qsTr("Catmull-Rom"), qsTr("Mitchell"), qsTr("Lanczos")]
                            currentIndex: Chiaki.settings.placeboDownscaler
                            onActivated: index => Chiaki.settings.placeboDownscaler = index
                        }
                    }

                    Setting {
                        title: qsTr("Plane downscaler")
                        description: qsTr("For the colour planes.")
                        hint: qsTr("Same as the downscaler")

                        C.Dropdown {
                            Layout.fillWidth: true
                            Accessible.name: qsTr("Plane downscaler")
                            model: [qsTr("Same as the downscaler"), qsTr("Box"), qsTr("Hermite"), qsTr("Bilinear"), qsTr("Bicubic"), qsTr("Gaussian"), qsTr("Catmull-Rom"), qsTr("Mitchell"), qsTr("Lanczos")]
                            currentIndex: Chiaki.settings.placeboPlaneDownscaler
                            onActivated: index => Chiaki.settings.placeboPlaneDownscaler = index
                        }
                    }

                    Setting {
                        title: qsTr("Anti-ringing strength")
                        description: qsTr("Less of a halo around sharp edges.")
                        hint: "0.00"

                        Amount {
                            Accessible.name: qsTr("Anti-ringing strength")
                            lastInFocusChain: true
                            from: 0.00
                            to: 1.00
                            stepSize: 0.01
                            value: Chiaki.settings.placeboAntiringingStrength
                            valueText: value.toFixed(2)
                            onMoved: Chiaki.settings.placeboAntiringingStrength = value
                        }
                    }
                }
            }

            Page {
                // Deinterlacing
                C.SettingsSection {
                    title: qsTr("Deinterlacing")
                    description: qsTr("Only for interlaced video.")

                    Setting {
                        title: qsTr("Deinterlace")
                        hint: qsTr("Off")

                        Item { Layout.fillWidth: true }
                        C.Toggle {
                            Accessible.name: qsTr("Deinterlace")
                            lastInFocusChain: !Chiaki.settings.placeboDeinterlaceEnabled
                            checked: Chiaki.settings.placeboDeinterlaceEnabled
                            onToggled: Chiaki.settings.placeboDeinterlaceEnabled = checked
                        }
                    }

                    Setting {
                        visible: Chiaki.settings.placeboDeinterlaceEnabled
                        title: qsTr("Preset")
                        hint: qsTr("Default")

                        C.Dropdown {
                            Layout.fillWidth: true
                            Accessible.name: qsTr("Deinterlacing preset")
                            model: [qsTr("Default")]
                            currentIndex: Chiaki.settings.placeboDeinterlacePreset
                            enabled: Chiaki.settings.placeboDeinterlaceEnabled
                            onActivated: index => Chiaki.settings.placeboDeinterlacePreset = index
                        }
                    }

                    Setting {
                        visible: Chiaki.settings.placeboDeinterlaceEnabled
                        title: qsTr("Algorithm")
                        hint: qsTr("Yadif")

                        C.Dropdown {
                            Layout.fillWidth: true
                            Accessible.name: qsTr("Deinterlacing algorithm")
                            model: [qsTr("Weave"), qsTr("Bob"), qsTr("Yadif"), qsTr("Bwdif")]
                            currentIndex: Chiaki.settings.placeboDeinterlaceAlgorithm
                            enabled: Chiaki.settings.placeboDeinterlaceEnabled
                            onActivated: index => Chiaki.settings.placeboDeinterlaceAlgorithm = index
                        }
                    }

                    Setting {
                        visible: Chiaki.settings.placeboDeinterlaceEnabled
                        title: qsTr("Skip the spatial check")
                        hint: qsTr("Off")

                        Item { Layout.fillWidth: true }
                        C.Toggle {
                            Accessible.name: qsTr("Skip the spatial check")
                            lastInFocusChain: true
                            checked: Chiaki.settings.placeboDeinterlaceSkipSpatial
                            enabled: Chiaki.settings.placeboDeinterlaceEnabled
                            onToggled: Chiaki.settings.placeboDeinterlaceSkipSpatial = checked
                        }
                    }
                }
            }

            Page {
                // Debanding
                C.SettingsSection {
                    title: qsTr("Debanding")
                    description: qsTr("Smooths out the steps that compression leaves in colour gradients.")

                    Setting {
                        title: qsTr("Deband")
                        hint: qsTr("On")

                        Item { Layout.fillWidth: true }
                        C.Toggle {
                            Accessible.name: qsTr("Deband")
                            lastInFocusChain: !Chiaki.settings.placeboDebandEnabled
                            checked: Chiaki.settings.placeboDebandEnabled
                            onToggled: Chiaki.settings.placeboDebandEnabled = checked
                        }
                    }

                    Setting {
                        visible: Chiaki.settings.placeboDebandEnabled
                        title: qsTr("Preset")
                        hint: qsTr("Custom")

                        C.Dropdown {
                            Layout.fillWidth: true
                            Accessible.name: qsTr("Debanding preset")
                            lastInFocusChain: Chiaki.settings.placeboDebandPreset != 0
                            model: [qsTr("Custom"), qsTr("Default")]
                            currentIndex: Chiaki.settings.placeboDebandPreset
                            onActivated: index => Chiaki.settings.placeboDebandPreset = index
                        }
                    }

                    Setting {
                        visible: Chiaki.settings.placeboDebandEnabled && (Chiaki.settings.placeboDebandPreset == 0)
                        title: qsTr("Iterations")
                        hint: "1"

                        Amount {
                            Accessible.name: qsTr("Debanding iterations")
                            from: 0
                            to: 16
                            stepSize: 1
                            value: Chiaki.settings.placeboDebandIterations
                            valueText: qsTr("%1").arg(value)
                            onMoved: Chiaki.settings.placeboDebandIterations = value
                        }
                    }

                    Setting {
                        visible: Chiaki.settings.placeboDebandEnabled && (Chiaki.settings.placeboDebandPreset == 0)
                        title: qsTr("Threshold")
                        hint: "3.0"

                        Amount {
                            Accessible.name: qsTr("Debanding threshold")
                            from: 0.0
                            to: 1000.0
                            stepSize: 0.1
                            value: Chiaki.settings.placeboDebandThreshold
                            valueText: value.toFixed(1)
                            onMoved: Chiaki.settings.placeboDebandThreshold = value
                        }
                    }

                    Setting {
                        visible: Chiaki.settings.placeboDebandEnabled && (Chiaki.settings.placeboDebandPreset == 0)
                        title: qsTr("Radius")
                        hint: "16.0"

                        Amount {
                            Accessible.name: qsTr("Debanding radius")
                            from: 0.0
                            to: 1000.0
                            stepSize: 0.1
                            value: Chiaki.settings.placeboDebandRadius
                            valueText: value.toFixed(1)
                            onMoved: Chiaki.settings.placeboDebandRadius = value
                        }
                    }

                    Setting {
                        visible: Chiaki.settings.placeboDebandEnabled && (Chiaki.settings.placeboDebandPreset == 0)
                        title: qsTr("Grain")
                        hint: "4.0"

                        Amount {
                            Accessible.name: qsTr("Debanding grain")
                            lastInFocusChain: true
                            from: 0.0
                            to: 1000.0
                            stepSize: 0.1
                            value: Chiaki.settings.placeboDebandGrain
                            valueText: value.toFixed(1)
                            onMoved: Chiaki.settings.placeboDebandGrain = value
                        }
                    }
                }
            }

            Page {
                // Sigmoidization
                C.SettingsSection {
                    title: qsTr("Sigmoidization")
                    description: qsTr("Less ringing around edges when the picture is made larger.")

                    Setting {
                        title: qsTr("Sigmoidize")
                        hint: qsTr("On")

                        Item { Layout.fillWidth: true }
                        C.Toggle {
                            Accessible.name: qsTr("Sigmoidize")
                            lastInFocusChain: !Chiaki.settings.placeboSigmoidEnabled
                            checked: Chiaki.settings.placeboSigmoidEnabled
                            onToggled: Chiaki.settings.placeboSigmoidEnabled = checked
                        }
                    }

                    Setting {
                        visible: Chiaki.settings.placeboSigmoidEnabled
                        title: qsTr("Preset")
                        hint: qsTr("Custom")

                        C.Dropdown {
                            Layout.fillWidth: true
                            Accessible.name: qsTr("Sigmoidization preset")
                            lastInFocusChain: Chiaki.settings.placeboSigmoidPreset != 0
                            model: [qsTr("Custom"), qsTr("Default")]
                            currentIndex: Chiaki.settings.placeboSigmoidPreset
                            onActivated: index => Chiaki.settings.placeboSigmoidPreset = index
                        }
                    }

                    Setting {
                        visible: Chiaki.settings.placeboSigmoidEnabled && (Chiaki.settings.placeboSigmoidPreset == 0)
                        title: qsTr("Centre")
                        hint: "0.75"

                        Amount {
                            Accessible.name: qsTr("Sigmoid centre")
                            from: 0.00
                            to: 1.00
                            stepSize: 0.01
                            value: Chiaki.settings.placeboSigmoidCenter
                            valueText: value.toFixed(2)
                            onMoved: Chiaki.settings.placeboSigmoidCenter = value
                        }
                    }

                    Setting {
                        visible: Chiaki.settings.placeboSigmoidEnabled && (Chiaki.settings.placeboSigmoidPreset == 0)
                        title: qsTr("Slope")
                        hint: "6.5"

                        Amount {
                            Accessible.name: qsTr("Sigmoid slope")
                            lastInFocusChain: true
                            from: 1.0
                            to: 20.0
                            stepSize: 0.1
                            value: Chiaki.settings.placeboSigmoidSlope
                            valueText: value.toFixed(1)
                            onMoved: Chiaki.settings.placeboSigmoidSlope = value
                        }
                    }
                }
            }

            Page {
                // Colour
                C.SettingsSection {
                    title: qsTr("Colour")
                    description: qsTr("Brightness, contrast, saturation, hue, gamma and temperature of the picture.")

                    Setting {
                        title: qsTr("Adjust the colours")
                        hint: qsTr("On")

                        Item { Layout.fillWidth: true }
                        C.Toggle {
                            Accessible.name: qsTr("Adjust the colours")
                            lastInFocusChain: !Chiaki.settings.placeboColorAdjustmentEnabled
                            checked: Chiaki.settings.placeboColorAdjustmentEnabled
                            onToggled: Chiaki.settings.placeboColorAdjustmentEnabled = checked
                        }
                    }

                    Setting {
                        visible: Chiaki.settings.placeboColorAdjustmentEnabled
                        title: qsTr("Preset")
                        hint: qsTr("Custom")

                        C.Dropdown {
                            Layout.fillWidth: true
                            Accessible.name: qsTr("Colour preset")
                            lastInFocusChain: Chiaki.settings.placeboColorAdjustmentPreset != 0
                            model: [qsTr("Custom"), qsTr("Neutral")]
                            currentIndex: Chiaki.settings.placeboColorAdjustmentPreset
                            onActivated: index => Chiaki.settings.placeboColorAdjustmentPreset = index
                        }
                    }

                    Setting {
                        visible: Chiaki.settings.placeboColorAdjustmentEnabled && (Chiaki.settings.placeboColorAdjustmentPreset == 0)
                        title: qsTr("Brightness")
                        hint: "0.00"

                        Amount {
                            Accessible.name: qsTr("Brightness")
                            from: -1.00
                            to: 1.00
                            stepSize: 0.01
                            value: Chiaki.settings.placeboColorAdjustmentBrightness
                            valueText: value.toFixed(2)
                            onMoved: Chiaki.settings.placeboColorAdjustmentBrightness = value
                        }
                    }

                    Setting {
                        visible: Chiaki.settings.placeboColorAdjustmentEnabled && (Chiaki.settings.placeboColorAdjustmentPreset == 0)
                        title: qsTr("Contrast")
                        hint: "1.0"

                        Amount {
                            Accessible.name: qsTr("Contrast")
                            from: 0.0
                            to: 100.0
                            stepSize: 0.1
                            value: Chiaki.settings.placeboColorAdjustmentContrast
                            valueText: value.toFixed(1)
                            onMoved: Chiaki.settings.placeboColorAdjustmentContrast = value
                        }
                    }

                    Setting {
                        visible: Chiaki.settings.placeboColorAdjustmentEnabled && (Chiaki.settings.placeboColorAdjustmentPreset == 0)
                        title: qsTr("Saturation")
                        hint: "1.0"

                        Amount {
                            Accessible.name: qsTr("Saturation")
                            from: 0.0
                            to: 100.0
                            stepSize: 0.1
                            value: Chiaki.settings.placeboColorAdjustmentSaturation
                            valueText: value.toFixed(1)
                            onMoved: Chiaki.settings.placeboColorAdjustmentSaturation = value
                        }
                    }

                    Setting {
                        visible: Chiaki.settings.placeboColorAdjustmentEnabled && (Chiaki.settings.placeboColorAdjustmentPreset == 0)
                        title: qsTr("Hue")
                        hint: "0.00"

                        Amount {
                            Accessible.name: qsTr("Hue")
                            from: 0.00
                            to: 6.28
                            stepSize: 0.01
                            value: Chiaki.settings.placeboColorAdjustmentHue
                            valueText: value.toFixed(2)
                            onMoved: Chiaki.settings.placeboColorAdjustmentHue = value
                        }
                    }

                    Setting {
                        visible: Chiaki.settings.placeboColorAdjustmentEnabled && (Chiaki.settings.placeboColorAdjustmentPreset == 0)
                        title: qsTr("Gamma")
                        hint: "1.0"

                        Amount {
                            Accessible.name: qsTr("Gamma")
                            from: 0.0
                            to: 100.0
                            stepSize: 0.1
                            value: Chiaki.settings.placeboColorAdjustmentGamma
                            valueText: value.toFixed(1)
                            onMoved: Chiaki.settings.placeboColorAdjustmentGamma = value
                        }
                    }

                    Setting {
                        visible: Chiaki.settings.placeboColorAdjustmentEnabled && (Chiaki.settings.placeboColorAdjustmentPreset == 0)
                        title: qsTr("Temperature")
                        hint: "0.000"

                        Amount {
                            Accessible.name: qsTr("Temperature")
                            lastInFocusChain: true
                            from: -1.143
                            to: 5.286
                            stepSize: 0.001
                            value: Chiaki.settings.placeboColorAdjustmentTemperature
                            valueText: value.toFixed(3)
                            onMoved: Chiaki.settings.placeboColorAdjustmentTemperature = value
                        }
                    }
                }
            }

            Page {
                // Peak detection
                C.SettingsSection {
                    title: qsTr("Peak detection")
                    description: qsTr("Measures how bright each scene is, for HDR tone mapping.")

                    Setting {
                        title: qsTr("Detect the HDR peak")
                        hint: qsTr("On")

                        Item { Layout.fillWidth: true }
                        C.Toggle {
                            Accessible.name: qsTr("Detect the HDR peak")
                            lastInFocusChain: !Chiaki.settings.placeboPeakDetectionEnabled
                            checked: Chiaki.settings.placeboPeakDetectionEnabled
                            onToggled: Chiaki.settings.placeboPeakDetectionEnabled = checked
                        }
                    }

                    Setting {
                        visible: Chiaki.settings.placeboPeakDetectionEnabled
                        title: qsTr("Preset")
                        hint: qsTr("Custom")

                        C.Dropdown {
                            Layout.fillWidth: true
                            Accessible.name: qsTr("Peak detection preset")
                            lastInFocusChain: Chiaki.settings.placeboPeakDetectionPreset != 0
                            model: [qsTr("Custom"), qsTr("Default"), qsTr("High quality")]
                            currentIndex: Chiaki.settings.placeboPeakDetectionPreset
                            onActivated: index => Chiaki.settings.placeboPeakDetectionPreset = index
                        }
                    }

                    Setting {
                        visible: Chiaki.settings.placeboPeakDetectionEnabled && (Chiaki.settings.placeboPeakDetectionPreset == 0)
                        title: qsTr("Smoothing period")
                        hint: "20.0"

                        Amount {
                            Accessible.name: qsTr("Peak smoothing period")
                            from: 0.0
                            to: 1000.0
                            stepSize: 0.1
                            value: Chiaki.settings.placeboPeakDetectionPeakSmoothingPeriod
                            valueText: value.toFixed(1)
                            onMoved: Chiaki.settings.placeboPeakDetectionPeakSmoothingPeriod = value
                        }
                    }

                    Setting {
                        visible: Chiaki.settings.placeboPeakDetectionEnabled && (Chiaki.settings.placeboPeakDetectionPreset == 0)
                        title: qsTr("Scene threshold, low")
                        hint: "1.0"

                        Amount {
                            Accessible.name: qsTr("Scene threshold, low")
                            from: 0.0
                            to: 100.0
                            stepSize: 0.1
                            value: Chiaki.settings.placeboPeakDetectionSceneThresholdLow
                            valueText: value.toFixed(1)
                            onMoved: Chiaki.settings.placeboPeakDetectionSceneThresholdLow = value
                        }
                    }

                    Setting {
                        visible: Chiaki.settings.placeboPeakDetectionEnabled && (Chiaki.settings.placeboPeakDetectionPreset == 0)
                        title: qsTr("Scene threshold, high")
                        hint: "3.0"

                        Amount {
                            Accessible.name: qsTr("Scene threshold, high")
                            from: 0.0
                            to: 100.0
                            stepSize: 0.1
                            value: Chiaki.settings.placeboPeakDetectionSceneThresholdHigh
                            valueText: value.toFixed(1)
                            onMoved: Chiaki.settings.placeboPeakDetectionSceneThresholdHigh = value
                        }
                    }

                    Setting {
                        visible: Chiaki.settings.placeboPeakDetectionEnabled && (Chiaki.settings.placeboPeakDetectionPreset == 0)
                        title: qsTr("Peak percentile")
                        hint: "100.0"

                        Amount {
                            Accessible.name: qsTr("Peak percentile")
                            from: 0.0
                            to: 100.0
                            stepSize: 0.001
                            value: Chiaki.settings.placeboPeakDetectionPeakPercentile
                            valueText: value.toFixed(3)
                            onMoved: Chiaki.settings.placeboPeakDetectionPeakPercentile = value
                        }
                    }

                    Setting {
                        visible: Chiaki.settings.placeboPeakDetectionEnabled && (Chiaki.settings.placeboPeakDetectionPreset == 0)
                        title: qsTr("Black cutoff")
                        hint: "1.0"

                        Amount {
                            Accessible.name: qsTr("Black cutoff")
                            from: 0.0
                            to: 100.0
                            stepSize: 0.1
                            value: Chiaki.settings.placeboPeakDetectionBlackCutoff
                            valueText: value.toFixed(1)
                            onMoved: Chiaki.settings.placeboPeakDetectionBlackCutoff = value
                        }
                    }

                    Setting {
                        visible: Chiaki.settings.placeboPeakDetectionEnabled && (Chiaki.settings.placeboPeakDetectionPreset == 0)
                        title: qsTr("Allow a delayed peak")
                        description: qsTr("Uses the peak one frame late.")
                        hint: qsTr("Off")

                        Item { Layout.fillWidth: true }
                        C.Toggle {
                            Accessible.name: qsTr("Allow a delayed peak")
                            lastInFocusChain: true
                            checked: Chiaki.settings.placeboPeakDetectionAllowDelayedPeak
                            onToggled: Chiaki.settings.placeboPeakDetectionAllowDelayedPeak = checked
                        }
                    }
                }
            }

            Page {
                // Colour mapping
                C.SettingsSection {
                    title: qsTr("Colour mapping")
                    description: qsTr("Fits the colours and the brightness to your screen.")

                    Setting {
                        title: qsTr("Map the colours")
                        hint: qsTr("On")

                        Item { Layout.fillWidth: true }
                        C.Toggle {
                            Accessible.name: qsTr("Map the colours")
                            lastInFocusChain: !Chiaki.settings.placeboColorMappingEnabled
                            checked: Chiaki.settings.placeboColorMappingEnabled
                            onToggled: Chiaki.settings.placeboColorMappingEnabled = checked
                        }
                    }

                    Setting {
                        visible: Chiaki.settings.placeboColorMappingEnabled
                        title: qsTr("Preset")
                        hint: qsTr("Custom")

                        C.Dropdown {
                            Layout.fillWidth: true
                            Accessible.name: qsTr("Colour mapping preset")
                            lastInFocusChain: Chiaki.settings.placeboColorMappingPreset != 0
                            model: [qsTr("Custom"), qsTr("Default"), qsTr("High quality")]
                            currentIndex: Chiaki.settings.placeboColorMappingPreset
                            onActivated: index => Chiaki.settings.placeboColorMappingPreset = index
                        }
                    }

                    Setting {
                        visible: Chiaki.settings.placeboColorMappingEnabled && Chiaki.settings.placeboColorMappingPreset == 0
                        title: qsTr("Custom colour mapping")
                        description: qsTr("Gamut and tone mapping, in a page of their own.")

                        Item { Layout.fillWidth: true }
                        C.PillButton {
                            small: true
                            lastInFocusChain: true
                            text: qsTr("Open…")
                            onClicked: root.showPlaceboColorMappingDialog()
                        }
                    }
                }
            }
        }
    }
}
