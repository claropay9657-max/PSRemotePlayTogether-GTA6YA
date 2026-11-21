import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

import org.streetpea.chiaking

import "controls" as C

// The renderer's colour mapping (libplacebo's gamut and tone mapping), opened from the
// renderer's "Colour mapping" section with the preset on Custom. In the look of the
// settings window: the sections in a list on the left, the one shown on the right.
// Also shown over the game (Main.qml's colorMappingSettingsLoader, 1200 × 600, or a
// window of its own): every section scrolls, and the setting with the focus stays on screen.
//
// Keys and controller as in the renderer's dialog: the list is one stop in the focus
// chain, Up and Down there go through the sections, Right or Return goes into one, Up
// from its first setting or Left comes back; Page Up / Page Down (L1 / R1) change the
// section from anywhere.
DialogView {
    enum GamutMappingFunction {
        Clip,
        Perceptual,
        SoftClip,
        Relative,
        Saturation,
        Absolute,
        Desaturate,
        Darken,
        Highlight,
        Linear
    }
    enum ToneMappingFunction {
        Clip,
        Spline,
        St209440,
        St209410,
        Bt2390,
        Bt2446a,
        Reinhard,
        Mobius,
        Hable,
        Gamma,
        Linear,
        LinearLight
    }
    id: dialog
    title: qsTr("Colour mapping")
    header: qsTr("For HDR and wide-colour screens. Changes are saved at once")
    buttonVisible: false

    readonly property var sections: [qsTr("Gamut mapping"), qsTr("Gamut fine-tuning"), qsTr("Tone mapping"),
        qsTr("Tone curve"), qsTr("Tone fine-tuning")]
    /** The section shown, by its place in `sections`. */
    property int section: 0

    readonly property int gamutFunction: Chiaki.settings.placeboGamutMappingFunction
    readonly property int toneFunction: Chiaki.settings.placeboToneMappingFunction

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
                Accessible.name: qsTr("Colour mapping sections")

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
                // Gamut mapping
                C.SettingsSection {
                    title: qsTr("Gamut mapping")
                    description: qsTr("What happens to colours outside the range your screen can show.")

                    Setting {
                        title: qsTr("Method")
                        hint: qsTr("Perceptual")

                        C.Dropdown {
                            Layout.fillWidth: true
                            Accessible.name: qsTr("Gamut mapping method")
                            lastInFocusChain: !(dialog.gamutFunction == PlaceboColorMappingDialog.GamutMappingFunction.Perceptual || dialog.gamutFunction == PlaceboColorMappingDialog.GamutMappingFunction.SoftClip || dialog.gamutFunction == PlaceboColorMappingDialog.GamutMappingFunction.Relative || dialog.gamutFunction == PlaceboColorMappingDialog.GamutMappingFunction.Absolute || dialog.gamutFunction == PlaceboColorMappingDialog.GamutMappingFunction.Darken)
                            model: [qsTr("Clip"), qsTr("Perceptual"), qsTr("Soft clip"), qsTr("Relative"), qsTr("Saturation"), qsTr("Absolute"), qsTr("Desaturate"), qsTr("Darken"), qsTr("Highlight"), qsTr("Linear")]
                            currentIndex: Chiaki.settings.placeboGamutMappingFunction
                            onActivated: index => Chiaki.settings.placeboGamutMappingFunction = index
                        }
                    }

                    Setting {
                        visible: dialog.gamutFunction == PlaceboColorMappingDialog.GamutMappingFunction.Perceptual
                        title: qsTr("Perceptual deadzone")
                        hint: "0.30"

                        Amount {
                            Accessible.name: qsTr("Perceptual deadzone")
                            from: 0.00
                            to: 1.00
                            stepSize: 0.01
                            value: Chiaki.settings.placeboGamutMappingPerceptualDeadzone
                            valueText: value.toFixed(2)
                            onMoved: Chiaki.settings.placeboGamutMappingPerceptualDeadzone = value
                        }
                    }

                    Setting {
                        visible: dialog.gamutFunction == PlaceboColorMappingDialog.GamutMappingFunction.Perceptual
                        title: qsTr("Perceptual strength")
                        hint: "0.80"

                        Amount {
                            Accessible.name: qsTr("Perceptual strength")
                            from: 0.00
                            to: 1.00
                            stepSize: 0.01
                            value: Chiaki.settings.placeboGamutMappingPerceptualStrength
                            valueText: value.toFixed(2)
                            onMoved: Chiaki.settings.placeboGamutMappingPerceptualStrength = value
                        }
                    }

                    Setting {
                        visible: dialog.gamutFunction == PlaceboColorMappingDialog.GamutMappingFunction.Relative || dialog.gamutFunction == PlaceboColorMappingDialog.GamutMappingFunction.Absolute || dialog.gamutFunction == PlaceboColorMappingDialog.GamutMappingFunction.Darken
                        title: qsTr("Colorimetric gamma")
                        hint: "1.80"

                        Amount {
                            Accessible.name: qsTr("Colorimetric gamma")
                            lastInFocusChain: true
                            from: 0.00
                            to: 10.00
                            stepSize: 0.01
                            value: Chiaki.settings.placeboGamutMappingColorimetricGamma
                            valueText: value.toFixed(2)
                            onMoved: Chiaki.settings.placeboGamutMappingColorimetricGamma = value
                        }
                    }

                    Setting {
                        visible: dialog.gamutFunction == PlaceboColorMappingDialog.GamutMappingFunction.Perceptual || dialog.gamutFunction == PlaceboColorMappingDialog.GamutMappingFunction.SoftClip
                        title: qsTr("Soft clip knee")
                        hint: "0.70"

                        Amount {
                            Accessible.name: qsTr("Soft clip knee")
                            lastInFocusChain: dialog.gamutFunction == PlaceboColorMappingDialog.GamutMappingFunction.Perceptual
                            from: 0.00
                            to: 1.00
                            stepSize: 0.01
                            value: Chiaki.settings.placeboGamutMappingSoftClipKnee
                            valueText: value.toFixed(2)
                            onMoved: Chiaki.settings.placeboGamutMappingSoftClipKnee = value
                        }
                    }

                    Setting {
                        visible: dialog.gamutFunction == PlaceboColorMappingDialog.GamutMappingFunction.SoftClip
                        title: qsTr("Soft clip desaturation")
                        hint: "0.35"

                        Amount {
                            Accessible.name: qsTr("Soft clip desaturation")
                            lastInFocusChain: true
                            from: 0.00
                            to: 1.00
                            stepSize: 0.01
                            value: Chiaki.settings.placeboGamutMappingSoftClipDesat
                            valueText: value.toFixed(2)
                            onMoved: Chiaki.settings.placeboGamutMappingSoftClipDesat = value
                        }
                    }
                }
            }

            Page {
                // Gamut fine-tuning
                C.SettingsSection {
                    title: qsTr("Gamut fine-tuning")
                    description: qsTr("The steps of the 3D lookup tables, and how they are read.")

                    Setting {
                        title: qsTr("3D LUT size, intensity")
                        hint: "48"

                        Amount {
                            Accessible.name: qsTr("3D LUT size, intensity")
                            from: 0
                            to: 1024
                            stepSize: 1
                            value: Chiaki.settings.placeboGamutMappingLut3dSizeI
                            valueText: qsTr("%1").arg(value)
                            onMoved: Chiaki.settings.placeboGamutMappingLut3dSizeI = value
                        }
                    }

                    Setting {
                        title: qsTr("3D LUT size, chroma")
                        hint: "32"

                        Amount {
                            Accessible.name: qsTr("3D LUT size, chroma")
                            from: 0
                            to: 1024
                            stepSize: 1
                            value: Chiaki.settings.placeboGamutMappingLut3dSizeC
                            valueText: qsTr("%1").arg(value)
                            onMoved: Chiaki.settings.placeboGamutMappingLut3dSizeC = value
                        }
                    }

                    Setting {
                        title: qsTr("3D LUT size, hue")
                        hint: "256"

                        Amount {
                            Accessible.name: qsTr("3D LUT size, hue")
                            from: 0
                            to: 1024
                            stepSize: 1
                            value: Chiaki.settings.placeboGamutMappingLut3dSizeH
                            valueText: qsTr("%1").arg(value)
                            onMoved: Chiaki.settings.placeboGamutMappingLut3dSizeH = value
                        }
                    }

                    Setting {
                        title: qsTr("Tricubic 3D LUTs")
                        description: qsTr("Smoother lookups in the 3D LUTs.")
                        hint: qsTr("Off")

                        Item { Layout.fillWidth: true }
                        C.Toggle {
                            Accessible.name: qsTr("Tricubic 3D LUTs")
                            checked: Chiaki.settings.placeboGamutMappingLut3dTricubicEnabled
                            onToggled: Chiaki.settings.placeboGamutMappingLut3dTricubicEnabled = checked
                        }
                    }

                    Setting {
                        title: qsTr("Gamut expansion")
                        description: qsTr("Stretches the colours to a wider gamut where the screen has one.")
                        hint: qsTr("Off")

                        Item { Layout.fillWidth: true }
                        C.Toggle {
                            Accessible.name: qsTr("Gamut expansion")
                            lastInFocusChain: true
                            checked: Chiaki.settings.placeboGamutMappingGamutExpansionEnabled
                            onToggled: Chiaki.settings.placeboGamutMappingGamutExpansionEnabled = checked
                        }
                    }
                }
            }

            Page {
                // Tone mapping
                C.SettingsSection {
                    title: qsTr("Tone mapping")
                    description: qsTr("How HDR brightness is fitted to this screen.")

                    Setting {
                        title: qsTr("Method")
                        hint: qsTr("Spline")

                        C.Dropdown {
                            Layout.fillWidth: true
                            Accessible.name: qsTr("Tone mapping method")
                            model: [qsTr("Clip"), qsTr("Spline"), qsTr("ST 2094-40"), qsTr("ST 2094-10"), qsTr("BT.2390"), qsTr("BT.2446a"), qsTr("Reinhard"), qsTr("Mobius"), qsTr("Hable"), qsTr("Gamma"), qsTr("Linear"), qsTr("Linear light")]
                            currentIndex: Chiaki.settings.placeboToneMappingFunction
                            onActivated: index => Chiaki.settings.placeboToneMappingFunction = index
                        }
                    }

                    Setting {
                        id: kneeAdaptationRow
                        visible: dialog.toneFunction == PlaceboColorMappingDialog.ToneMappingFunction.Spline || dialog.toneFunction == PlaceboColorMappingDialog.ToneMappingFunction.St209440 || dialog.toneFunction == PlaceboColorMappingDialog.ToneMappingFunction.St209410
                        title: qsTr("Knee adaptation")
                        hint: "0.40"

                        Amount {
                            Accessible.name: qsTr("Knee adaptation")
                            from: 0.00
                            to: 1.00
                            stepSize: 0.01
                            value: Chiaki.settings.placeboToneMappingKneeAdaptation
                            valueText: value.toFixed(2)
                            onMoved: Chiaki.settings.placeboToneMappingKneeAdaptation = value
                        }
                    }

                    Setting {
                        visible: kneeAdaptationRow.visible
                        title: qsTr("Knee minimum")
                        hint: "0.10"

                        Amount {
                            Accessible.name: qsTr("Knee minimum")
                            from: 0.00
                            to: 0.50
                            stepSize: 0.01
                            value: Chiaki.settings.placeboToneMappingKneeMinimum
                            valueText: value.toFixed(2)
                            onMoved: Chiaki.settings.placeboToneMappingKneeMinimum = value
                        }
                    }

                    Setting {
                        visible: kneeAdaptationRow.visible
                        title: qsTr("Knee maximum")
                        hint: "0.80"

                        Amount {
                            Accessible.name: qsTr("Knee maximum")
                            from: 0.50
                            to: 1.00
                            stepSize: 0.01
                            value: Chiaki.settings.placeboToneMappingKneeMaximum
                            valueText: value.toFixed(2)
                            onMoved: Chiaki.settings.placeboToneMappingKneeMaximum = value
                        }
                    }

                    Setting {
                        title: qsTr("Knee default")
                        hint: "0.40"

                        Amount {
                            Accessible.name: qsTr("Knee default")
                            lastInFocusChain: dialog.toneFunction != PlaceboColorMappingDialog.ToneMappingFunction.Bt2390
                            from: 0.00
                            to: 1.00
                            stepSize: 0.01
                            value: Chiaki.settings.placeboToneMappingKneeDefault
                            valueText: value.toFixed(2)
                            onMoved: Chiaki.settings.placeboToneMappingKneeDefault = value
                        }
                    }

                    Setting {
                        visible: dialog.toneFunction == PlaceboColorMappingDialog.ToneMappingFunction.Bt2390
                        title: qsTr("Knee offset")
                        hint: "1.00"

                        Amount {
                            Accessible.name: qsTr("Knee offset")
                            lastInFocusChain: true
                            from: 0.50
                            to: 2.00
                            stepSize: 0.01
                            value: Chiaki.settings.placeboToneMappingKneeOffset
                            valueText: value.toFixed(2)
                            onMoved: Chiaki.settings.placeboToneMappingKneeOffset = value
                        }
                    }
                }
            }

            Page {
                // Tone curve
                C.SettingsSection {
                    title: qsTr("Tone curve")
                    description: qsTr("The shape of the curve, for the tone mapping method chosen.")

                    Setting {
                        visible: dialog.toneFunction == PlaceboColorMappingDialog.ToneMappingFunction.Spline
                        title: qsTr("Slope tuning")
                        hint: "1.5"

                        Amount {
                            Accessible.name: qsTr("Slope tuning")
                            from: 0.0
                            to: 10.0
                            stepSize: 0.1
                            value: Chiaki.settings.placeboToneMappingSlopeTuning
                            valueText: value.toFixed(1)
                            onMoved: Chiaki.settings.placeboToneMappingSlopeTuning = value
                        }
                    }

                    Setting {
                        visible: dialog.toneFunction == PlaceboColorMappingDialog.ToneMappingFunction.Spline
                        title: qsTr("Slope offset")
                        hint: "0.20"

                        Amount {
                            Accessible.name: qsTr("Slope offset")
                            from: 0.00
                            to: 1.00
                            stepSize: 0.01
                            value: Chiaki.settings.placeboToneMappingSlopeOffset
                            valueText: value.toFixed(2)
                            onMoved: Chiaki.settings.placeboToneMappingSlopeOffset = value
                        }
                    }

                    Setting {
                        visible: dialog.toneFunction == PlaceboColorMappingDialog.ToneMappingFunction.Spline
                        title: qsTr("Spline contrast")
                        hint: "0.50"

                        Amount {
                            Accessible.name: qsTr("Spline contrast")
                            lastInFocusChain: true
                            from: 0.00
                            to: 1.50
                            stepSize: 0.01
                            value: Chiaki.settings.placeboToneMappingSplineContrast
                            valueText: value.toFixed(2)
                            onMoved: Chiaki.settings.placeboToneMappingSplineContrast = value
                        }
                    }

                    Setting {
                        visible: dialog.toneFunction == PlaceboColorMappingDialog.ToneMappingFunction.Reinhard
                        title: qsTr("Reinhard contrast")
                        hint: "0.50"

                        Amount {
                            Accessible.name: qsTr("Reinhard contrast")
                            lastInFocusChain: true
                            from: 0.00
                            to: 1.00
                            stepSize: 0.01
                            value: Chiaki.settings.placeboToneMappingReinhardContrast
                            valueText: value.toFixed(2)
                            onMoved: Chiaki.settings.placeboToneMappingReinhardContrast = value
                        }
                    }

                    Setting {
                        visible: dialog.toneFunction == PlaceboColorMappingDialog.ToneMappingFunction.Mobius || dialog.toneFunction == PlaceboColorMappingDialog.ToneMappingFunction.Gamma
                        title: qsTr("Linear knee")
                        hint: "0.3"

                        Amount {
                            Accessible.name: qsTr("Linear knee")
                            lastInFocusChain: true
                            from: 0.00
                            to: 1.00
                            stepSize: 0.01
                            value: Chiaki.settings.placeboToneMappingLinearKnee
                            valueText: value.toFixed(2)
                            onMoved: Chiaki.settings.placeboToneMappingLinearKnee = value
                        }
                    }

                    Setting {
                        visible: dialog.toneFunction == PlaceboColorMappingDialog.ToneMappingFunction.Linear || dialog.toneFunction == PlaceboColorMappingDialog.ToneMappingFunction.LinearLight
                        title: qsTr("Exposure")
                        hint: "1.0"

                        Amount {
                            Accessible.name: qsTr("Exposure")
                            lastInFocusChain: true
                            from: 0.0
                            to: 10.0
                            stepSize: 0.1
                            value: Chiaki.settings.placeboToneMappingExposure
                            valueText: value.toFixed(1)
                            onMoved: Chiaki.settings.placeboToneMappingExposure = value
                        }
                    }

                    // Clip, the ST 2094 ones, BT.2390, BT.2446a and Hable have no curve to set here
                    Text {
                        visible: !(dialog.toneFunction == PlaceboColorMappingDialog.ToneMappingFunction.Spline || dialog.toneFunction == PlaceboColorMappingDialog.ToneMappingFunction.Reinhard || dialog.toneFunction == PlaceboColorMappingDialog.ToneMappingFunction.Mobius || dialog.toneFunction == PlaceboColorMappingDialog.ToneMappingFunction.Gamma || dialog.toneFunction == PlaceboColorMappingDialog.ToneMappingFunction.Linear || dialog.toneFunction == PlaceboColorMappingDialog.ToneMappingFunction.LinearLight)
                        Layout.fillWidth: true
                        Layout.topMargin: 16
                        Layout.bottomMargin: 14
                        text: qsTr("Nothing to set here for this tone mapping method. Spline, Reinhard, Mobius, Gamma and the linear ones have a curve to tune.")
                        wrapMode: Text.WordWrap
                        color: C.Theme.textDim
                        font.pixelSize: 13
                    }
                }
            }

            Page {
                // Tone fine-tuning
                C.SettingsSection {
                    title: qsTr("Tone fine-tuning")
                    description: qsTr("Metadata, inverse mapping and contrast.")

                    Setting {
                        title: qsTr("Inverse tone mapping")
                        description: qsTr("Stretches SDR pictures towards HDR, where the method can.")
                        hint: qsTr("Off")

                        Item { Layout.fillWidth: true }
                        C.Toggle {
                            Accessible.name: qsTr("Inverse tone mapping")
                            checked: Chiaki.settings.placeboToneMappingInverseToneMappingEnabled
                            onToggled: Chiaki.settings.placeboToneMappingInverseToneMappingEnabled = checked
                        }
                    }

                    Setting {
                        title: qsTr("Metadata")
                        description: qsTr("Which HDR metadata to go by.")
                        hint: qsTr("Any")

                        C.Dropdown {
                            Layout.fillWidth: true
                            Accessible.name: qsTr("Tone mapping metadata")
                            model: [qsTr("Any"), qsTr("None"), qsTr("HDR10"), qsTr("HDR10+"), qsTr("CIE Y")]
                            currentIndex: Chiaki.settings.placeboToneMappingMetadata
                            onActivated: index => Chiaki.settings.placeboToneMappingMetadata = index
                        }
                    }

                    Setting {
                        title: qsTr("Tone LUT size")
                        hint: "256"

                        Amount {
                            Accessible.name: qsTr("Tone LUT size")
                            from: 0
                            to: 4096
                            stepSize: 1
                            value: Chiaki.settings.placeboToneMappingToneLutSize
                            valueText: qsTr("%1").arg(value)
                            onMoved: Chiaki.settings.placeboToneMappingToneLutSize = value
                        }
                    }

                    Setting {
                        title: qsTr("Contrast recovery")
                        hint: "0.30"

                        Amount {
                            Accessible.name: qsTr("Contrast recovery")
                            from: 0.00
                            to: 2.00
                            stepSize: 0.01
                            value: Chiaki.settings.placeboToneMappingContrastRecovery
                            valueText: value.toFixed(2)
                            onMoved: Chiaki.settings.placeboToneMappingContrastRecovery = value
                        }
                    }

                    Setting {
                        title: qsTr("Contrast smoothness")
                        hint: "3.5"

                        Amount {
                            Accessible.name: qsTr("Contrast smoothness")
                            lastInFocusChain: true
                            from: 1.0
                            to: 32.0
                            stepSize: 0.1
                            value: Chiaki.settings.placeboToneMappingContrastSmoothness
                            valueText: value.toFixed(1)
                            onMoved: Chiaki.settings.placeboToneMappingContrastSmoothness = value
                        }
                    }
                }
            }
        }
    }
}
