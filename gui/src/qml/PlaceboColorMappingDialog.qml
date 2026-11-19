import QtCore
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Dialogs

import org.streetpea.chiaking

import "controls" as C

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
    Keys.onPressed: (event) => {
        if (event.modifiers)
            return;
        switch (event.key) {
        case Qt.Key_PageUp:
            bar.decrementCurrentIndex();
            event.accepted = true;
            break;
        case Qt.Key_PageDown:
            bar.incrementCurrentIndex();
            event.accepted = true;
            break;
        }
    }

    Item {
        // The sections, down the left side; L1 / R1 on a controller go up and down
        Rectangle {
            id: sidebar
            anchors {
                top: parent.top
                left: parent.left
                bottom: parent.bottom
                margins: 14
                topMargin: 10
            }
            width: 210
            radius: 18
            color: Qt.rgba(16 / 255, 14 / 255, 32 / 255, 0.72)
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, 0.07)

            Column {
                anchors {
                    fill: parent
                    margins: 10
                }
                spacing: 4

                Repeater {
                    model: [
                        { label: qsTr("Gamut mapping"), detail: qsTr("Colours outside the screen's range") },
                        { label: qsTr("Gamut fine-tuning"), detail: qsTr("Perceptual strength and more") },
                        { label: qsTr("Tone mapping"), detail: qsTr("HDR brightness on this screen") },
                        { label: qsTr("Tone curve"), detail: qsTr("Knee, slope and contrast") },
                        { label: qsTr("Tone fine-tuning"), detail: qsTr("Metadata and inverse mapping") }
                    ]
                    delegate: Rectangle {
                        id: navItem
                        required property var modelData
                        required property int index
                        readonly property bool current: bar.currentIndex === index
                        width: parent.width
                        height: 52
                        radius: 12
                        color: current ? Qt.rgba(140 / 255, 82 / 255, 1, 0.28)
                             : navArea.containsMouse ? Qt.rgba(1, 1, 1, 0.06) : "transparent"
                        border.width: current ? 1 : 0
                        border.color: Qt.rgba(140 / 255, 82 / 255, 1, 0.7)

                        Rectangle {
                            visible: navItem.current
                            anchors {
                                left: parent.left
                                verticalCenter: parent.verticalCenter
                                leftMargin: 4
                            }
                            width: 3
                            height: 24
                            radius: 1.5
                            color: "#00f2ff"
                        }

                        Column {
                            anchors {
                                left: parent.left
                                right: parent.right
                                verticalCenter: parent.verticalCenter
                                leftMargin: 16
                                rightMargin: 8
                            }
                            spacing: 1

                            Label {
                                width: parent.width
                                elide: Text.ElideRight
                                font.pixelSize: 15
                                font.bold: true
                                text: navItem.modelData.label
                            }
                            Label {
                                width: parent.width
                                elide: Text.ElideRight
                                font.pixelSize: 11
                                opacity: 0.5
                                text: navItem.modelData.detail
                            }
                        }

                        MouseArea {
                            id: navArea
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: bar.currentIndex = navItem.index
                        }
                    }
                }
            }
        }

        // The pages sit on a glass panel
        Rectangle {
            anchors {
                top: parent.top
                left: sidebar.right
                right: parent.right
                bottom: parent.bottom
                margins: 14
                topMargin: 10
            }
            radius: 18
            color: Qt.rgba(16 / 255, 16 / 255, 24 / 255, 0.5)
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, 0.07)
        }

        TabBar {
            id: bar
            // Keeps the current page; the sidebar above is what is shown
            visible: false
            height: 0


            TabButton {
                text: qsTr("Gamut Mapping 1")
                focusPolicy: Qt.NoFocus
            }

            TabButton {
                text: qsTr("Gamut Mapping 2")
                focusPolicy: Qt.NoFocus
            }

            TabButton {
                text: qsTr("Tone-mapping 1")
                focusPolicy: Qt.NoFocus
            }

            TabButton {
                text: qsTr("Tone-mapping 2")
                focusPolicy: Qt.NoFocus
            }

           TabButton {
                text: qsTr("Tone-mapping 3")
                focusPolicy: Qt.NoFocus
            }
        }

        StackLayout {
            anchors {
                top: parent.top
                left: sidebar.right
                right: parent.right
                bottom: parent.bottom
                topMargin: 10
                leftMargin: 14
                rightMargin: 14
            }
            currentIndex: bar.currentIndex
            onCurrentIndexChanged: nextItemInFocusChain().forceActiveFocus(Qt.TabFocusReason)

            Item {
                // Gamut Mapping 1
                Flickable {
                    id: page0Flick
                    anchors.fill: parent
                    clip: true
                    contentWidth: width
                    contentHeight: page0Column.implicitHeight + 48
                    flickableDirection: Flickable.VerticalFlick
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar {
                        visible: page0Flick.contentHeight > page0Flick.height
                    }
                    ColumnLayout {
                        id: page0Column
                        x: Math.max(28, Math.round((page0Flick.width - width) / 2))
                        y: 24
                        width: Math.min(page0Flick.width - 56, 980)
                        spacing: 18

                        C.SettingsSection {
                            title: qsTr("Gamut mapping")

                            C.SettingRow {
                                title: qsTr("Gamut Mapping Function")
                                hint: qsTr("(Perceptual)")

                                C.ComboBox {
                                    Layout.fillWidth: true
                                    firstInFocusChain: true
                                    model: [qsTr("Clip"), qsTr("Perceptual"), qsTr("Soft Clip"), qsTr("Relative"), qsTr("Saturation"), qsTr("Absolute"), qsTr("Desaturate"), qsTr("Darken"), qsTr("Highlight"), qsTr("Linear")]
                                    currentIndex: Chiaki.settings.placeboGamutMappingFunction
                                    onActivated: index => Chiaki.settings.placeboGamutMappingFunction = index
                                }
                            }

                            C.SettingRow {
                                visible: Chiaki.settings.placeboGamutMappingFunction == PlaceboColorMappingDialog.GamutMappingFunction.Perceptual
                                title: qsTr("Perceptual Deadzone")
                                hint: qsTr("(0.30)")

                                C.Slider {
                                    id: valueSlider1
                                    Layout.fillWidth: true
                                    from: 0.00
                                    to: 1.00
                                    stepSize: 0.01
                                    value: Chiaki.settings.placeboGamutMappingPerceptualDeadzone
                                    onMoved: Chiaki.settings.placeboGamutMappingPerceptualDeadzone = value
                                    visible: Chiaki.settings.placeboGamutMappingFunction == PlaceboColorMappingDialog.GamutMappingFunction.Perceptual
                                }
                                Label {
                                    Layout.preferredWidth: 110
                                    horizontalAlignment: Text.AlignRight
                                    text: qsTr(valueSlider1.value.toFixed(2))
                                }
                            }

                            C.SettingRow {
                                visible: Chiaki.settings.placeboGamutMappingFunction == PlaceboColorMappingDialog.GamutMappingFunction.Perceptual
                                title: qsTr("Perceptual Strength")
                                hint: qsTr("(0.80)")

                                C.Slider {
                                    id: valueSlider2
                                    Layout.fillWidth: true
                                    from: 0.00
                                    to: 1.00
                                    stepSize: 0.01
                                    value: Chiaki.settings.placeboGamutMappingPerceptualStrength
                                    onMoved: Chiaki.settings.placeboGamutMappingPerceptualStrength = value
                                    visible: Chiaki.settings.placeboGamutMappingFunction == PlaceboColorMappingDialog.GamutMappingFunction.Perceptual

                                }
                                Label {
                                    Layout.preferredWidth: 110
                                    horizontalAlignment: Text.AlignRight
                                    text: qsTr(valueSlider2.value.toFixed(2))
                                }
                            }

                            C.SettingRow {
                                visible: Chiaki.settings.placeboGamutMappingFunction == PlaceboColorMappingDialog.GamutMappingFunction.Relative || Chiaki.settings.placeboGamutMappingFunction == PlaceboColorMappingDialog.GamutMappingFunction.Absolute || Chiaki.settings.placeboGamutMappingFunction == PlaceboColorMappingDialog.GamutMappingFunction.Darken
                                title: qsTr("Colorimetric Gamma")
                                hint: qsTr("(1.80)")

                                C.Slider {
                                    id: valueSlider3
                                    Layout.fillWidth: true
                                    from: 0.00
                                    to: 10.00
                                    stepSize: 0.01
                                    value: Chiaki.settings.placeboGamutMappingColorimetricGamma
                                    onMoved: Chiaki.settings.placeboGamutMappingColorimetricGamma = value
                                    visible: Chiaki.settings.placeboGamutMappingFunction == PlaceboColorMappingDialog.GamutMappingFunction.Relative || Chiaki.settings.placeboGamutMappingFunction == PlaceboColorMappingDialog.GamutMappingFunction.Absolute || Chiaki.settings.placeboGamutMappingFunction == PlaceboColorMappingDialog.GamutMappingFunction.Darken
                                }
                                Label {
                                    Layout.preferredWidth: 110
                                    horizontalAlignment: Text.AlignRight
                                    text: qsTr(valueSlider3.value.toFixed(2))
                                }
                            }

                            C.SettingRow {
                                visible: Chiaki.settings.placeboGamutMappingFunction == PlaceboColorMappingDialog.GamutMappingFunction.Perceptual || Chiaki.settings.placeboGamutMappingFunction == PlaceboColorMappingDialog.GamutMappingFunction.SoftClip
                                title: qsTr("Soft Clip Knee")
                                hint: qsTr("(0.70)")

                                C.Slider {
                                    id: valueSlider4
                                    Layout.fillWidth: true
                                    from: 0.00
                                    to: 1.00
                                    stepSize: 0.01
                                    value: Chiaki.settings.placeboGamutMappingSoftClipKnee
                                    onMoved: Chiaki.settings.placeboGamutMappingSoftClipKnee = value
                                    visible: Chiaki.settings.placeboGamutMappingFunction == PlaceboColorMappingDialog.GamutMappingFunction.Perceptual || Chiaki.settings.placeboGamutMappingFunction == PlaceboColorMappingDialog.GamutMappingFunction.SoftClip
                                }
                                Label {
                                    Layout.preferredWidth: 110
                                    horizontalAlignment: Text.AlignRight
                                    text: qsTr(valueSlider4.value.toFixed(2))
                                }
                            }

                            C.SettingRow {
                                visible: Chiaki.settings.placeboGamutMappingFunction == PlaceboColorMappingDialog.GamutMappingFunction.SoftClip
                                title: qsTr("Soft Clip Desaturation")
                                hint: qsTr("(0.35)")

                                C.Slider {
                                    id: valueSlider5
                                    Layout.fillWidth: true
                                    from: 0.00
                                    to: 1.00
                                    stepSize: 0.01
                                    value: Chiaki.settings.placeboGamutMappingSoftClipDesat
                                    onMoved: Chiaki.settings.placeboGamutMappingSoftClipDesat = value
                                    visible: Chiaki.settings.placeboGamutMappingFunction == PlaceboColorMappingDialog.GamutMappingFunction.SoftClip
                                }
                                Label {
                                    Layout.preferredWidth: 110
                                    horizontalAlignment: Text.AlignRight
                                    text: qsTr(valueSlider5.value.toFixed(2))
                                }
                            }
                        }
                    }
                }
            }

            Item {
                // Gamut Mapping 2
                Flickable {
                    id: page1Flick
                    anchors.fill: parent
                    clip: true
                    contentWidth: width
                    contentHeight: page1Column.implicitHeight + 48
                    flickableDirection: Flickable.VerticalFlick
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar {
                        visible: page1Flick.contentHeight > page1Flick.height
                    }
                    ColumnLayout {
                        id: page1Column
                        x: Math.max(28, Math.round((page1Flick.width - width) / 2))
                        y: 24
                        width: Math.min(page1Flick.width - 56, 980)
                        spacing: 18

                        C.SettingsSection {
                            title: qsTr("Gamut mapping: fine-tuning")

                            C.SettingRow {
                                title: qsTr("LUT 3D Size I")
                                hint: qsTr("(48)")

                                C.Slider {
                                    id: valueSlider6
                                    Layout.fillWidth: true
                                    from: 0
                                    to: 1024
                                    stepSize: 1
                                    value: Chiaki.settings.placeboGamutMappingLut3dSizeI
                                    onMoved: Chiaki.settings.placeboGamutMappingLut3dSizeI = value
                                }
                                Label {
                                    Layout.preferredWidth: 110
                                    horizontalAlignment: Text.AlignRight
                                    text: qsTr("%1").arg(valueSlider6.value)
                                }
                            }

                            C.SettingRow {
                                title: qsTr("LUT 3D Size C")
                                hint: qsTr("(32)")

                                C.Slider {
                                    id: valueSlider7
                                    Layout.fillWidth: true
                                    from: 0
                                    to: 1024
                                    stepSize: 1
                                    value: Chiaki.settings.placeboGamutMappingLut3dSizeC
                                    onMoved: Chiaki.settings.placeboGamutMappingLut3dSizeC = value
                                }
                                Label {
                                    Layout.preferredWidth: 110
                                    horizontalAlignment: Text.AlignRight
                                    text: qsTr("%1").arg(valueSlider7.value)
                                }
                            }

                            C.SettingRow {
                                title: qsTr("LUT 3D Size h")
                                hint: qsTr("(256)")

                                C.Slider {
                                    id: valueSlider8
                                    Layout.fillWidth: true
                                    from: 0
                                    to: 1024
                                    stepSize: 1
                                    value: Chiaki.settings.placeboGamutMappingLut3dSizeH
                                    onMoved: Chiaki.settings.placeboGamutMappingLut3dSizeH = value
                                }
                                Label {
                                    Layout.preferredWidth: 110
                                    horizontalAlignment: Text.AlignRight
                                    text: qsTr("%1").arg(valueSlider8.value)
                                }
                            }

                            C.SettingRow {
                                title: qsTr("LUT 3D Tricubic Enabled")
                                description: qsTr("Enable Tricubic Interpolation for 3DLUTs")
                                hint: qsTr("(Unchecked)")

                                Item { Layout.fillWidth: true }
                                C.CheckBox {
                                    checked: Chiaki.settings.placeboGamutMappingLut3dTricubicEnabled
                                    onToggled: Chiaki.settings.placeboGamutMappingLut3dTricubicEnabled = checked
                                }
                            }

                            C.SettingRow {
                                title: qsTr("Gamut Expansion Enabled")
                                description: qsTr("Enable Gamut Expansion")
                                hint: qsTr("(Unchecked)")

                                Item { Layout.fillWidth: true }
                                C.CheckBox {
                                    checked: Chiaki.settings.placeboGamutMappingGamutExpansionEnabled
                                    onToggled: Chiaki.settings.placeboGamutMappingGamutExpansionEnabled = checked
                                }
                            }
                        }
                    }
                }
            }

            Item {
                // Tone-mapping 1
                Flickable {
                    id: page2Flick
                    anchors.fill: parent
                    clip: true
                    contentWidth: width
                    contentHeight: page2Column.implicitHeight + 48
                    flickableDirection: Flickable.VerticalFlick
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar {
                        visible: page2Flick.contentHeight > page2Flick.height
                    }
                    ColumnLayout {
                        id: page2Column
                        x: Math.max(28, Math.round((page2Flick.width - width) / 2))
                        y: 24
                        width: Math.min(page2Flick.width - 56, 980)
                        spacing: 18

                        C.SettingsSection {
                            title: qsTr("Tone mapping")

                            C.SettingRow {
                                title: qsTr("Tone-mapping Function")
                                hint: qsTr("(Spline)")

                                C.ComboBox {
                                    Layout.fillWidth: true
                                    firstInFocusChain: true
                                    model: [qsTr("Clip"), qsTr("Spline"), qsTr("St2094-40"), qsTr("St-2094-10"), qsTr("Bt2390"), qsTr("Bt2446a"), qsTr("Reinhard"), qsTr("Mobius"), qsTr("Hable"), qsTr("Gamma"), qsTr("Linear"), qsTr("Linear Light")]
                                    currentIndex: Chiaki.settings.placeboToneMappingFunction
                                    onActivated: index => Chiaki.settings.placeboToneMappingFunction = index
                                }
                            }

                            C.SettingRow {
                                visible: Chiaki.settings.placeboToneMappingFunction == PlaceboColorMappingDialog.ToneMappingFunction.Spline || Chiaki.settings.placeboToneMappingFunction == PlaceboColorMappingDialog.ToneMappingFunction.St209440 || Chiaki.settings.placeboToneMappingFunction == PlaceboColorMappingDialog.ToneMappingFunction.St209410
                                title: qsTr("Knee Adaptation")
                                hint: qsTr("(0.40)")

                                C.Slider {
                                    id: kneeadaptation
                                    Layout.fillWidth: true
                                    from: 0.00
                                    to: 1.00
                                    stepSize: 0.01
                                    value: Chiaki.settings.placeboToneMappingKneeAdaptation
                                    onMoved: Chiaki.settings.placeboToneMappingKneeAdaptation = value
                                    visible: Chiaki.settings.placeboToneMappingFunction == PlaceboColorMappingDialog.ToneMappingFunction.Spline || Chiaki.settings.placeboToneMappingFunction == PlaceboColorMappingDialog.ToneMappingFunction.St209440 || Chiaki.settings.placeboToneMappingFunction == PlaceboColorMappingDialog.ToneMappingFunction.St209410
                                }
                                Label {
                                    Layout.preferredWidth: 110
                                    horizontalAlignment: Text.AlignRight
                                    text: qsTr(kneeadaptation.value.toFixed(2))
                                }
                            }

                            C.SettingRow {
                                visible: kneeadaptation.visible
                                title: qsTr("Knee Minimum")
                                hint: qsTr("(0.10)")

                                C.Slider {
                                    id: valueSlider9
                                    Layout.fillWidth: true
                                    from: 0.00
                                    to: 0.50
                                    stepSize: 0.01
                                    value: Chiaki.settings.placeboToneMappingKneeMinimum
                                    onMoved: Chiaki.settings.placeboToneMappingKneeMinimum = value
                                    visible: kneeadaptation.visible
                                }
                                Label {
                                    Layout.preferredWidth: 110
                                    horizontalAlignment: Text.AlignRight
                                    text: qsTr(valueSlider9.value.toFixed(2))
                                }
                            }

                            C.SettingRow {
                                visible: kneeadaptation.visible
                                title: qsTr("Knee Maximum")
                                hint: qsTr("(0.80)")

                                C.Slider {
                                    id: valueSlider10
                                    Layout.fillWidth: true
                                    from: 0.50
                                    to: 1.00
                                    stepSize: 0.01
                                    value: Chiaki.settings.placeboToneMappingKneeMaximum
                                    onMoved: Chiaki.settings.placeboToneMappingKneeMaximum = value
                                    visible: kneeadaptation.visible
                                }
                                Label {
                                    Layout.preferredWidth: 110
                                    horizontalAlignment: Text.AlignRight
                                    text: qsTr(valueSlider10.value.toFixed(2))
                                }
                            }

                            C.SettingRow {
                                title: qsTr("Knee Default")
                                hint: qsTr("(0.40)")

                                C.Slider {
                                    id: valueSlider11
                                    Layout.fillWidth: true
                                    from: 0.00
                                    to: 1.00
                                    stepSize: 0.01
                                    value: Chiaki.settings.placeboToneMappingKneeDefault
                                    onMoved: Chiaki.settings.placeboToneMappingKneeDefault = value
                                }
                                Label {
                                    Layout.preferredWidth: 110
                                    horizontalAlignment: Text.AlignRight
                                    text: qsTr(valueSlider11.value.toFixed(2))
                                }
                            }

                            C.SettingRow {
                                visible: Chiaki.settings.placeboToneMappingFunction == PlaceboColorMappingDialog.ToneMappingFunction.Bt2390
                                title: qsTr("Knee Offset")
                                hint: qsTr("(1.00)")

                                C.Slider {
                                    id: valueSlider12
                                    Layout.fillWidth: true
                                    from: 0.50
                                    to: 2.00
                                    stepSize: 0.01
                                    value: Chiaki.settings.placeboToneMappingKneeOffset
                                    onMoved: Chiaki.settings.placeboToneMappingKneeOffset = value
                                    visible: Chiaki.settings.placeboToneMappingFunction == PlaceboColorMappingDialog.ToneMappingFunction.Bt2390
                                }
                                Label {
                                    Layout.preferredWidth: 110
                                    horizontalAlignment: Text.AlignRight
                                    text: qsTr(valueSlider12.value.toFixed(2))
                                }
                            }
                        }
                    }
                }
            }

            Item {
                // Tone-mapping 2
                Flickable {
                    id: page3Flick
                    anchors.fill: parent
                    clip: true
                    contentWidth: width
                    contentHeight: page3Column.implicitHeight + 48
                    flickableDirection: Flickable.VerticalFlick
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar {
                        visible: page3Flick.contentHeight > page3Flick.height
                    }
                    ColumnLayout {
                        id: page3Column
                        x: Math.max(28, Math.round((page3Flick.width - width) / 2))
                        y: 24
                        width: Math.min(page3Flick.width - 56, 980)
                        spacing: 18

                        C.SettingsSection {
                            title: qsTr("Tone mapping: curve")

                            C.SettingRow {
                                visible: Chiaki.settings.placeboToneMappingFunction == PlaceboColorMappingDialog.ToneMappingFunction.Spline
                                title: qsTr("Slope Tuning")
                                hint: qsTr("(1.5)")

                                C.Slider {
                                    Layout.fillWidth: true
                                    from: 0.0
                                    to: 10.0
                                    stepSize: 0.1
                                    value: Chiaki.settings.placeboToneMappingSlopeTuning
                                    onMoved: Chiaki.settings.placeboToneMappingSlopeTuning = value
                                    visible: Chiaki.settings.placeboToneMappingFunction == PlaceboColorMappingDialog.ToneMappingFunction.Spline
                                }
                                Label {
                                    Layout.preferredWidth: 110
                                    horizontalAlignment: Text.AlignRight
                                    text: qsTr(slopeTuningLabel.value.toFixed(1))
                                }
                            }

                            C.SettingRow {
                                visible: Chiaki.settings.placeboToneMappingFunction == PlaceboColorMappingDialog.ToneMappingFunction.Spline
                                title: qsTr("Slope Offset")
                                hint: qsTr("(0.20)")

                                C.Slider {
                                    id: valueSlider13
                                    Layout.fillWidth: true
                                    from: 0.00
                                    to: 1.00
                                    stepSize: 0.01
                                    value: Chiaki.settings.placeboToneMappingSlopeOffset
                                    onMoved: Chiaki.settings.placeboToneMappingSlopeOffset = value
                                    visible: Chiaki.settings.placeboToneMappingFunction == PlaceboColorMappingDialog.ToneMappingFunction.Spline
                                }
                                Label {
                                    Layout.preferredWidth: 110
                                    horizontalAlignment: Text.AlignRight
                                    text: qsTr(valueSlider13.value.toFixed(2))
                                }
                            }

                            C.SettingRow {
                                visible: Chiaki.settings.placeboToneMappingFunction == PlaceboColorMappingDialog.ToneMappingFunction.Spline
                                title: qsTr("Spline Contrast")
                                hint: qsTr("(0.50)")

                                C.Slider {
                                    id: valueSlider14
                                    Layout.fillWidth: true
                                    from: 0.00
                                    to: 1.50
                                    stepSize: 0.01
                                    value: Chiaki.settings.placeboToneMappingSplineContrast
                                    onMoved: Chiaki.settings.placeboToneMappingSplineContrast = value
                                    visible: Chiaki.settings.placeboToneMappingFunction == PlaceboColorMappingDialog.ToneMappingFunction.Spline
                                }
                                Label {
                                    Layout.preferredWidth: 110
                                    horizontalAlignment: Text.AlignRight
                                    text: qsTr(valueSlider14.value.toFixed(2))
                                }
                            }

                            C.SettingRow {
                                visible: Chiaki.settings.placeboToneMappingFunction == PlaceboColorMappingDialog.ToneMappingFunction.Reinhard
                                title: qsTr("Reinhard Contrast")
                                hint: qsTr("(0.50)")

                                C.Slider {
                                    id: valueSlider15
                                    Layout.fillWidth: true
                                    from: 0.00
                                    to: 1.00
                                    stepSize: 0.01
                                    value: Chiaki.settings.placeboToneMappingReinhardContrast
                                    onMoved: Chiaki.settings.placeboToneMappingReinhardContrast = value
                                    visible: Chiaki.settings.placeboToneMappingFunction == PlaceboColorMappingDialog.ToneMappingFunction.Reinhard
                                }
                                Label {
                                    Layout.preferredWidth: 110
                                    horizontalAlignment: Text.AlignRight
                                    text: qsTr(valueSlider15.value.toFixed(2))
                                }
                            }

                            C.SettingRow {
                                visible: Chiaki.settings.placeboToneMappingFunction == PlaceboColorMappingDialog.ToneMappingFunction.Mobius || Chiaki.settings.placeboToneMappingFunction == PlaceboColorMappingDialog.ToneMappingFunction.Gamma
                                title: qsTr("Linear Knee")
                                hint: qsTr("(0.3)")

                                C.Slider {
                                    id: valueSlider16
                                    Layout.fillWidth: true
                                    from: 0.00
                                    to: 1.00
                                    stepSize: 0.01
                                    value: Chiaki.settings.placeboToneMappingLinearKnee
                                    onMoved: Chiaki.settings.placeboToneMappingLinearKnee = value
                                    visible: Chiaki.settings.placeboToneMappingFunction == PlaceboColorMappingDialog.ToneMappingFunction.Mobius || Chiaki.settings.placeboToneMappingFunction == PlaceboColorMappingDialog.ToneMappingFunction.Gamma
                                }
                                Label {
                                    Layout.preferredWidth: 110
                                    horizontalAlignment: Text.AlignRight
                                    text: qsTr(valueSlider16.value.toFixed(2))
                                }
                            }

                            C.SettingRow {
                                visible: Chiaki.settings.placeboToneMappingFunction == PlaceboColorMappingDialog.ToneMappingFunction.Linear || Chiaki.settings.placeboToneMappingFunction == PlaceboColorMappingDialog.ToneMappingFunction.LinearLight
                                title: qsTr("Exposure")
                                hint: qsTr("(1.0)")

                                C.Slider {
                                    id: valueSlider17
                                    Layout.fillWidth: true
                                    from: 0.0
                                    to: 10.0
                                    stepSize: 0.1
                                    value: Chiaki.settings.placeboToneMappingExposure
                                    onMoved: Chiaki.settings.placeboToneMappingExposure = value
                                    visible: Chiaki.settings.placeboToneMappingFunction == PlaceboColorMappingDialog.ToneMappingFunction.Linear || Chiaki.settings.placeboToneMappingFunction == PlaceboColorMappingDialog.ToneMappingFunction.LinearLight
                                }
                                Label {
                                    Layout.preferredWidth: 110
                                    horizontalAlignment: Text.AlignRight
                                    text: qsTr(valueSlider17.value.toFixed(1))
                                }
                            }
                        }
                    }
                }
            }
            
            Item {
                // Tone-mapping 3
                Flickable {
                    id: page4Flick
                    anchors.fill: parent
                    clip: true
                    contentWidth: width
                    contentHeight: page4Column.implicitHeight + 48
                    flickableDirection: Flickable.VerticalFlick
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar {
                        visible: page4Flick.contentHeight > page4Flick.height
                    }
                    ColumnLayout {
                        id: page4Column
                        x: Math.max(28, Math.round((page4Flick.width - width) / 2))
                        y: 24
                        width: Math.min(page4Flick.width - 56, 980)
                        spacing: 18

                        C.SettingsSection {
                            title: qsTr("Tone mapping: fine-tuning")

                            C.SettingRow {
                                title: qsTr("Inverse Tone-mapping Enabled")
                                description: qsTr("Enable Inverse Tone-mapping")
                                hint: qsTr("(Unchecked)")

                                Item { Layout.fillWidth: true }
                                C.CheckBox {
                                    checked: Chiaki.settings.placeboToneMappingInverseToneMappingEnabled
                                    onToggled: Chiaki.settings.placeboToneMappingInverseToneMappingEnabled = checked
                                }
                            }

                            C.SettingRow {
                                title: qsTr("Tone-mapping Metadata")
                                hint: qsTr("(Any)")

                                C.ComboBox {
                                    Layout.fillWidth: true
                                    firstInFocusChain: true
                                    model: [qsTr("Any"), qsTr("None"), qsTr("HDR10"), qsTr("HDR10 Plus"), qsTr("Cie_y")]
                                    currentIndex: Chiaki.settings.placeboToneMappingMetadata
                                    onActivated: index => Chiaki.settings.placeboToneMappingMetadata = index
                                }
                            }

                            C.SettingRow {
                                title: qsTr("Tone LUT Size")
                                hint: qsTr("(256)")

                                C.Slider {
                                    id: valueSlider18
                                    Layout.fillWidth: true
                                    from: 0
                                    to: 4096
                                    stepSize: 1
                                    value: Chiaki.settings.placeboToneMappingToneLutSize
                                    onMoved: Chiaki.settings.placeboToneMappingToneLutSize = value
                                }
                                Label {
                                    Layout.preferredWidth: 110
                                    horizontalAlignment: Text.AlignRight
                                    text: qsTr("%1").arg(valueSlider18.value)
                                }
                            }

                            C.SettingRow {
                                title: qsTr("Contrast Recovery")
                                hint: qsTr("(0.30)")

                                C.Slider {
                                    id: valueSlider19
                                    Layout.fillWidth: true
                                    from: 0.00
                                    to: 2.00
                                    stepSize: 0.01
                                    value: Chiaki.settings.placeboToneMappingContrastRecovery
                                    onMoved: Chiaki.settings.placeboToneMappingContrastRecovery = value
                                }
                                Label {
                                    Layout.preferredWidth: 110
                                    horizontalAlignment: Text.AlignRight
                                    text: qsTr(valueSlider19.value.toFixed(2))
                                }
                            }

                            C.SettingRow {
                                title: qsTr("Contrast Smoothness")
                                hint: qsTr("(3.5)")

                                C.Slider {
                                    id: valueSlider20
                                    Layout.fillWidth: true
                                    from: 1.0
                                    to: 32.0
                                    stepSize: 0.1
                                    value: Chiaki.settings.placeboToneMappingContrastSmoothness
                                    onMoved: Chiaki.settings.placeboToneMappingContrastSmoothness = value
                                }
                                Label {
                                    Layout.preferredWidth: 110
                                    horizontalAlignment: Text.AlignRight
                                    text: qsTr(valueSlider20.value.toFixed(1))
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}