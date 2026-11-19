import QtCore
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Dialogs

import org.streetpea.chiaking

import "controls" as C

DialogView {
    id: dialog
    property string options_url: ""
    title: qsTr("Renderer")
    header: qsTr("Advanced picture processing. Changes are saved at once")
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
                        { label: qsTr("General"), detail: qsTr("Documentation, import, export") },
                        { label: qsTr("Scaling"), detail: qsTr("Up- and downscalers") },
                        { label: qsTr("Deinterlacing"), detail: qsTr("For interlaced video") },
                        { label: qsTr("Debanding"), detail: qsTr("Smooth colour steps") },
                        { label: qsTr("Sigmoidization"), detail: qsTr("Less ringing when upscaling") },
                        { label: qsTr("Colour"), detail: qsTr("Brightness, contrast, gamma") },
                        { label: qsTr("Peak detection"), detail: qsTr("For HDR tone mapping") },
                        { label: qsTr("Colour mapping"), detail: qsTr("Gamut and tone mapping") }
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
                text: qsTr("Config")
                focusPolicy: Qt.NoFocus
            }

            TabButton {
                text: qsTr("Scaling")
                focusPolicy: Qt.NoFocus
            }

            TabButton {
                text: qsTr("Deinterlace")
                focusPolicy: Qt.NoFocus
            }

            TabButton {
                text: qsTr("Debanding")
                focusPolicy: Qt.NoFocus
            }

            TabButton {
                text: qsTr("Sigmoidization")
                focusPolicy: Qt.NoFocus
            }

            TabButton {
                text: qsTr("Color")
                focusPolicy: Qt.NoFocus
            }

            TabButton {
                text: qsTr("Peak Detection")
                focusPolicy: Qt.NoFocus
            }

            TabButton {
                text: qsTr("Color Mapping")
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
                // Config
                Flickable {
                    id: configFlick
                    anchors.fill: parent
                    clip: true
                    contentWidth: width
                    contentHeight: configColumn.implicitHeight + 48
                    flickableDirection: Flickable.VerticalFlick
                    boundsBehavior: Flickable.StopAtBounds
                    ColumnLayout {
                        id: configColumn
                        x: Math.max(28, Math.round((configFlick.width - width) / 2))
                        y: 24
                        width: Math.min(configFlick.width - 56, 980)
                        spacing: 18

                        C.SettingsSection {
                            title: qsTr("Renderer")
                            description: qsTr("The picture goes through libplacebo. These settings are for fine-tuning; the presets in Settings > Video are right for most screens.")

                            C.SettingRow {
                                title: qsTr("Documentation")
                                description: options_url ? qsTr("The address was copied. Paste it into your web browser.")
                                                         : qsTr("What every option does, on the libplacebo website.")

                                TextField {
                                    id: openurl
                                    echoMode: Chiaki.settings.streamerMode ? TextInput.Password : TextInput.Normal
                                    text: options_url
                                    visible: false
                                }
                                Item { Layout.fillWidth: true }
                                C.Button {
                                    id: openwebpage
                                    text: qsTr("Open documentation")
                                    onClicked: {
                                        options_url = Chiaki.openPlaceboOptionsLink()
                                    }
                                    visible: !options_url
                                }
                                C.Button {
                                    id: copyUrl
                                    text: qsTr("Copy the address again")
                                    onClicked: {
                                        openurl.selectAll()
                                        openurl.copy()
                                    }
                                    visible: options_url
                                }
                            }

                            C.SettingRow {
                                title: qsTr("Renderer settings file")
                                description: qsTr("Save these settings to a file, or load them from one.")

                                Item { Layout.fillWidth: true }
                                C.Button {
                                    id: exportButton
                                    text: qsTr("Export")
                                    onClicked: {
                                        Chiaki.settings.exportPlaceboSettings();
                                    }
                                    Material.roundedScale: Material.SmallScale
                                    KeyNavigation.down: importButton
                                    KeyNavigation.right: importButton
                                }
                                C.Button {
                                    id: importButton
                                    lastInFocusChain: true
                                    text: qsTr("Import")
                                    onClicked: {
                                        Chiaki.settings.importPlaceboSettings();
                                    }
                                    Material.roundedScale: Material.SmallScale
                                    KeyNavigation.up: exportButton
                                    KeyNavigation.left: exportButton
                                }
                            }
                        }
                    }
                }
            }

            Item {
                // Scaling
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
                            title: qsTr("Scaling")

                            C.SettingRow {
                                title: qsTr("Upscaler")
                                hint: qsTr("(EwaLanczos)")

                                C.ComboBox {
                                    Layout.fillWidth: true
                                    firstInFocusChain: true
                                    model: [qsTr("Custom"), qsTr("Nearest"), qsTr("Bilinear"), qsTr("Oversample"), qsTr("Bicubic"), qsTr("Gaussian"), qsTr("Catmull Rom"), qsTr("Lanczos"), qsTr("EwaLanczos"), qsTr("EwaLanczosSharp"), qsTr("EwaLanczosSharpest"), qsTr("FSR"), qsTr("FSRCNNX x2 8-0-4-1"), qsTr("FSRCNNX x2 16-0-4-1")]
                                    currentIndex: Chiaki.settings.placeboUpscaler
                                    onActivated: index => Chiaki.settings.placeboUpscaler = index
                                }
                            }

                            C.SettingRow {
                                title: qsTr("Plane Upscaler")
                                hint: qsTr("(Chosen Upscaler)")

                                C.ComboBox {
                                    Layout.fillWidth: true
                                    firstInFocusChain: true
                                    model: [qsTr("Chosen Upscaler"), qsTr("Nearest"), qsTr("Bilinear"), qsTr("Oversample"), qsTr("Bicubic"), qsTr("Gaussian"), qsTr("Catmull Rom"), qsTr("Lanczos"), qsTr("EwaLanczos"), qsTr("EwaLanczosSharp"), qsTr("EwaLanczosSharpest")]
                                    currentIndex: Chiaki.settings.placeboPlaneUpscaler
                                    onActivated: index => Chiaki.settings.placeboPlaneUpscaler = index
                                }
                            }

                            C.SettingRow {
                                title: qsTr("Downscaler")
                                hint: qsTr("(Hermite)")

                                C.ComboBox {
                                    Layout.fillWidth: true
                                    firstInFocusChain: true
                                    model: [qsTr("Custom"), qsTr("Box"), qsTr("Hermite"), qsTr("Bilinear"), qsTr("Bicubic"), qsTr("Gaussian"), qsTr("Catmull Rom"), qsTr("Mitchell"), qsTr("Lanczos")]
                                    currentIndex: Chiaki.settings.placeboDownscaler
                                    onActivated: index => Chiaki.settings.placeboDownscaler = index
                                }
                            }

                            C.SettingRow {
                                title: qsTr("Plane Downscaler")
                                hint: qsTr("(Chosen Downscaler)")

                                C.ComboBox {
                                    Layout.fillWidth: true
                                    firstInFocusChain: true
                                    model: [qsTr("Chosen Downscaler"), qsTr("Box"), qsTr("Hermite"), qsTr("Bilinear"), qsTr("Bicubic"), qsTr("Gaussian"), qsTr("Catmull Rom"), qsTr("Mitchell"), qsTr("Lanczos")]
                                    currentIndex: Chiaki.settings.placeboPlaneDownscaler
                                    onActivated: index => Chiaki.settings.placeboPlaneDownscaler = index
                                }
                            }

                            C.SettingRow {
                                title: qsTr("Anti-ringing Strength")
                                hint: qsTr("(0.00)")

                                C.Slider {
                                    id: valueSlider1
                                    Layout.fillWidth: true
                                    from: 0.00
                                    to: 1.00
                                    stepSize: 0.01
                                    value: Chiaki.settings.placeboAntiringingStrength
                                    onMoved: Chiaki.settings.placeboAntiringingStrength = value
                                }
                                Label {
                                    Layout.preferredWidth: 110
                                    horizontalAlignment: Text.AlignRight
                                    text: qsTr(valueSlider1.value.toFixed(2))
                                }
                            }
                        }
                    }
                }
            }

            Item {
                // Deinterlace
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
                            title: qsTr("Deinterlacing")

                            C.SettingRow {
                                title: qsTr("Enable Deinterlace")
                                hint: qsTr("(no)")

                                Item { Layout.fillWidth: true }
                                C.CheckBox {
                                    checked: Chiaki.settings.placeboDeinterlaceEnabled
                                    onToggled: Chiaki.settings.placeboDeinterlaceEnabled = checked
                                }
                            }

                            C.SettingRow {
                                visible: Chiaki.settings.placeboDeinterlaceEnabled
                                title: qsTr("Preset")
                                hint: qsTr("(Default)")

                                C.ComboBox {
                                    Layout.fillWidth: true
                                    model: [qsTr("Default")]
                                    currentIndex: Chiaki.settings.placeboDeinterlacePreset
                                    enabled: Chiaki.settings.placeboDeinterlaceEnabled
                                    visible: Chiaki.settings.placeboDeinterlaceEnabled
                                    onActivated: index => Chiaki.settings.placeboDeinterlacePreset = index
                                }
                            }

                            C.SettingRow {
                                visible: Chiaki.settings.placeboDeinterlaceEnabled
                                title: qsTr("Algorithm")
                                hint: qsTr("(Yadif)")

                                C.ComboBox {
                                    Layout.fillWidth: true
                                    model: [qsTr("Weave"), qsTr("Bob"), qsTr("Yadif"), qsTr("Bwdif")]
                                    currentIndex: Chiaki.settings.placeboDeinterlaceAlgorithm
                                    enabled: Chiaki.settings.placeboDeinterlaceEnabled
                                    visible: Chiaki.settings.placeboDeinterlaceEnabled
                                    onActivated: index => Chiaki.settings.placeboDeinterlaceAlgorithm = index
                                }
                            }

                            C.SettingRow {
                                visible: Chiaki.settings.placeboDeinterlaceEnabled
                                title: qsTr("Skip spatial check")
                                hint: qsTr("(no)")

                                Item { Layout.fillWidth: true }
                                C.CheckBox {
                                    checked: Chiaki.settings.placeboDeinterlaceSkipSpatial
                                    enabled: Chiaki.settings.placeboDeinterlaceEnabled
                                    visible: Chiaki.settings.placeboDeinterlaceEnabled
                                    onToggled: Chiaki.settings.placeboDeinterlaceSkipSpatial = checked
                                    Material.roundedScale: Material.SmallScale
                                }
                            }
                        }
                    }
                }
            }

            Item {
                // Debanding
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
                            title: qsTr("Debanding")

                            C.SettingRow {
                                title: qsTr("Deband Enabled")
                                description: qsTr("Enable Debanding")
                                hint: qsTr("(Checked)")

                                Item { Layout.fillWidth: true }
                                C.CheckBox {
                                    checked: Chiaki.settings.placeboDebandEnabled
                                    onToggled: Chiaki.settings.placeboDebandEnabled = checked
                                }
                            }

                            C.SettingRow {
                                visible: Chiaki.settings.placeboDebandEnabled
                                title: qsTr("Deband Preset")
                                hint: qsTr("(Custom)")

                                C.ComboBox {
                                    Layout.fillWidth: true
                                    model: [qsTr("Custom"), qsTr("Default")]
                                    currentIndex: Chiaki.settings.placeboDebandPreset
                                    onActivated: index => Chiaki.settings.placeboDebandPreset = index
                                    visible: Chiaki.settings.placeboDebandEnabled
                                }
                            }

                            C.SettingRow {
                                visible: Chiaki.settings.placeboDebandEnabled && (Chiaki.settings.placeboDebandPreset == 0)
                                title: qsTr("Deband Iterations")
                                hint: qsTr("(1)")

                                C.Slider {
                                    id: valueSlider2
                                    Layout.fillWidth: true
                                    from: 0
                                    to: 16
                                    stepSize: 1
                                    value: Chiaki.settings.placeboDebandIterations
                                    onMoved: Chiaki.settings.placeboDebandIterations = value
                                    visible: Chiaki.settings.placeboDebandEnabled && (Chiaki.settings.placeboDebandPreset == 0)
                                }
                                Label {
                                    Layout.preferredWidth: 110
                                    horizontalAlignment: Text.AlignRight
                                    text: qsTr("%1").arg(valueSlider2.value)
                                }
                            }

                            C.SettingRow {
                                visible: Chiaki.settings.placeboDebandEnabled && (Chiaki.settings.placeboDebandPreset == 0)
                                title: qsTr("Deband Threshold")
                                hint: qsTr("(3.0)")

                                C.Slider {
                                    id: valueSlider3
                                    Layout.fillWidth: true
                                    from: 0.0
                                    to: 1000.0
                                    stepSize: 0.1
                                    value: Chiaki.settings.placeboDebandThreshold
                                    onMoved: Chiaki.settings.placeboDebandThreshold = value
                                    visible: Chiaki.settings.placeboDebandEnabled && (Chiaki.settings.placeboDebandPreset == 0)
                                }
                                Label {
                                    Layout.preferredWidth: 110
                                    horizontalAlignment: Text.AlignRight
                                    text: qsTr(valueSlider3.value.toFixed(1))
                                }
                            }

                            C.SettingRow {
                                visible: Chiaki.settings.placeboDebandEnabled && (Chiaki.settings.placeboDebandPreset == 0)
                                title: qsTr("Deband Radius")
                                hint: qsTr("(16.0)")

                                C.Slider {
                                    id: valueSlider4
                                    Layout.fillWidth: true
                                    from: 0.0
                                    to: 1000.0
                                    stepSize: 0.1
                                    value: Chiaki.settings.placeboDebandRadius
                                    onMoved: Chiaki.settings.placeboDebandRadius = value
                                    visible: Chiaki.settings.placeboDebandEnabled && (Chiaki.settings.placeboDebandPreset == 0)
                                }
                                Label {
                                    Layout.preferredWidth: 110
                                    horizontalAlignment: Text.AlignRight
                                    text: qsTr(valueSlider4.value.toFixed(1))
                                }
                            }

                            C.SettingRow {
                                visible: Chiaki.settings.placeboDebandEnabled && (Chiaki.settings.placeboDebandPreset == 0)
                                title: qsTr("Deband Grain")
                                hint: qsTr("(4.0)")

                                C.Slider {
                                    id: valueSlider5
                                    Layout.fillWidth: true
                                    from: 0.0
                                    to: 1000.0
                                    stepSize: 0.1
                                    value: Chiaki.settings.placeboDebandGrain
                                    onMoved: Chiaki.settings.placeboDebandGrain = value
                                    visible: Chiaki.settings.placeboDebandEnabled && (Chiaki.settings.placeboDebandPreset == 0)
                                }
                                Label {
                                    Layout.preferredWidth: 110
                                    horizontalAlignment: Text.AlignRight
                                    text: qsTr(valueSlider5.value.toFixed(1))
                                }
                            }
                        }
                    }
                }
            }

            Item {
                // Sigmoidization
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
                            title: qsTr("Sigmoidization")

                            C.SettingRow {
                                title: qsTr("Sigmoidization Enabled")
                                description: qsTr("Enable Sigmoidization")
                                hint: qsTr("(Checked)")

                                Item { Layout.fillWidth: true }
                                C.CheckBox {
                                    checked: Chiaki.settings.placeboSigmoidEnabled
                                    onToggled: Chiaki.settings.placeboSigmoidEnabled = checked
                                }
                            }

                            C.SettingRow {
                                visible: Chiaki.settings.placeboSigmoidEnabled
                                title: qsTr("Sigmoid Preset")
                                hint: qsTr("(Custom)")

                                C.ComboBox {
                                    Layout.fillWidth: true
                                    model: [qsTr("Custom"), qsTr("Default")]
                                    currentIndex: Chiaki.settings.placeboSigmoidPreset
                                    onActivated: index => Chiaki.settings.placeboSigmoidPreset = index
                                    visible: Chiaki.settings.placeboSigmoidEnabled
                                }
                            }

                            C.SettingRow {
                                visible: Chiaki.settings.placeboSigmoidEnabled && (Chiaki.settings.placeboSigmoidPreset == 0)
                                title: qsTr("Sigmoid Center")
                                hint: qsTr("(0.75)")

                                C.Slider {
                                    id: valueSlider6
                                    Layout.fillWidth: true
                                    from: 0.00
                                    to: 1.00
                                    stepSize: 0.01
                                    value: Chiaki.settings.placeboSigmoidCenter
                                    onMoved: Chiaki.settings.placeboSigmoidCenter = value
                                    visible: Chiaki.settings.placeboSigmoidEnabled && (Chiaki.settings.placeboSigmoidPreset == 0)
                                }
                                Label {
                                    Layout.preferredWidth: 110
                                    horizontalAlignment: Text.AlignRight
                                    text: qsTr(valueSlider6.value.toFixed(2))
                                }
                            }

                            C.SettingRow {
                                visible: Chiaki.settings.placeboSigmoidEnabled && (Chiaki.settings.placeboSigmoidPreset == 0)
                                title: qsTr("Sigmoid Slope")
                                hint: qsTr("(6.5)")

                                C.Slider {
                                    id: valueSlider7
                                    Layout.fillWidth: true
                                    from: 1.0
                                    to: 20.0
                                    stepSize: 0.1
                                    value: Chiaki.settings.placeboSigmoidSlope
                                    onMoved: Chiaki.settings.placeboSigmoidSlope = value
                                    visible: Chiaki.settings.placeboSigmoidEnabled && (Chiaki.settings.placeboSigmoidPreset == 0)
                                }
                                Label {
                                    Layout.preferredWidth: 110
                                    horizontalAlignment: Text.AlignRight
                                    text: qsTr(valueSlider7.value.toFixed(1))
                                }
                            }
                        }
                    }
                }
            }

            Item {
                // Color
                Flickable {
                    id: page5Flick
                    anchors.fill: parent
                    clip: true
                    contentWidth: width
                    contentHeight: page5Column.implicitHeight + 48
                    flickableDirection: Flickable.VerticalFlick
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar {
                        visible: page5Flick.contentHeight > page5Flick.height
                    }
                    ColumnLayout {
                        id: page5Column
                        x: Math.max(28, Math.round((page5Flick.width - width) / 2))
                        y: 24
                        width: Math.min(page5Flick.width - 56, 980)
                        spacing: 18

                        C.SettingsSection {
                            title: qsTr("Colour")

                            C.SettingRow {
                                title: qsTr("Color Adjustment Enabled")
                                description: qsTr("Enable Color Adjustment")
                                hint: qsTr("(Checked)")

                                Item { Layout.fillWidth: true }
                                C.CheckBox {
                                    checked: Chiaki.settings.placeboColorAdjustmentEnabled
                                    onToggled: Chiaki.settings.placeboColorAdjustmentEnabled = checked
                                }
                            }

                            C.SettingRow {
                                visible: Chiaki.settings.placeboColorAdjustmentEnabled
                                title: qsTr("Color Adjustment Preset")
                                hint: qsTr("(Custom)")

                                C.ComboBox {
                                    Layout.fillWidth: true
                                    model: [qsTr("Custom"), qsTr("Neutral")]
                                    currentIndex: Chiaki.settings.placeboColorAdjustmentPreset
                                    onActivated: index => Chiaki.settings.placeboColorAdjustmentPreset = index
                                    visible: Chiaki.settings.placeboColorAdjustmentEnabled
                                }
                            }

                            C.SettingRow {
                                visible: Chiaki.settings.placeboColorAdjustmentEnabled && (Chiaki.settings.placeboColorAdjustmentPreset == 0)
                                title: qsTr("Brightness")
                                hint: qsTr("(0.00)")

                                C.Slider {
                                    id: valueSlider8
                                    Layout.fillWidth: true
                                    from: -1.00
                                    to: 1.00
                                    stepSize: 0.01
                                    value: Chiaki.settings.placeboColorAdjustmentBrightness
                                    onMoved: Chiaki.settings.placeboColorAdjustmentBrightness = value
                                    visible: Chiaki.settings.placeboColorAdjustmentEnabled && (Chiaki.settings.placeboColorAdjustmentPreset == 0)
                                }
                                Label {
                                    Layout.preferredWidth: 110
                                    horizontalAlignment: Text.AlignRight
                                    text: qsTr(valueSlider8.value.toFixed(2))
                                }
                            }

                            C.SettingRow {
                                visible: Chiaki.settings.placeboColorAdjustmentEnabled && (Chiaki.settings.placeboColorAdjustmentPreset == 0)
                                title: qsTr("Contrast")
                                hint: qsTr("(1.0)")

                                C.Slider {
                                    id: valueSlider9
                                    Layout.fillWidth: true
                                    from: 0.0
                                    to: 100.0
                                    stepSize: 0.1
                                    value: Chiaki.settings.placeboColorAdjustmentContrast
                                    onMoved: Chiaki.settings.placeboColorAdjustmentContrast = value
                                    visible: Chiaki.settings.placeboColorAdjustmentEnabled && (Chiaki.settings.placeboColorAdjustmentPreset == 0)
                                }
                                Label {
                                    Layout.preferredWidth: 110
                                    horizontalAlignment: Text.AlignRight
                                    text: qsTr(valueSlider9.value.toFixed(1))
                                }
                            }

                            C.SettingRow {
                                visible: Chiaki.settings.placeboColorAdjustmentEnabled && (Chiaki.settings.placeboColorAdjustmentPreset == 0)
                                title: qsTr("Saturation")
                                hint: qsTr("(1.0)")

                                C.Slider {
                                    id: valueSlider10
                                    Layout.fillWidth: true
                                    from: 0.0
                                    to: 100.0
                                    stepSize: 0.1
                                    value: Chiaki.settings.placeboColorAdjustmentSaturation
                                    onMoved: Chiaki.settings.placeboColorAdjustmentSaturation = value
                                    visible: Chiaki.settings.placeboColorAdjustmentEnabled && (Chiaki.settings.placeboColorAdjustmentPreset == 0)
                                }
                                Label {
                                    Layout.preferredWidth: 110
                                    horizontalAlignment: Text.AlignRight
                                    text: qsTr(valueSlider10.value.toFixed(1))
                                }
                            }

                            C.SettingRow {
                                visible: Chiaki.settings.placeboColorAdjustmentEnabled && (Chiaki.settings.placeboColorAdjustmentPreset == 0)
                                title: qsTr("Hue")
                                hint: qsTr("(0.00)")

                                C.Slider {
                                    id: valueSlider11
                                    Layout.fillWidth: true
                                    from: 0.00
                                    to: 6.28
                                    stepSize: 0.01
                                    value: Chiaki.settings.placeboColorAdjustmentHue
                                    onMoved: Chiaki.settings.placeboColorAdjustmentHue = value
                                    visible: Chiaki.settings.placeboColorAdjustmentEnabled && (Chiaki.settings.placeboColorAdjustmentPreset == 0)
                                }
                                Label {
                                    Layout.preferredWidth: 110
                                    horizontalAlignment: Text.AlignRight
                                    text: qsTr(valueSlider11.value.toFixed(2))
                                }
                            }

                            C.SettingRow {
                                visible: Chiaki.settings.placeboColorAdjustmentEnabled && (Chiaki.settings.placeboColorAdjustmentPreset == 0)
                                title: qsTr("Gamma")
                                hint: qsTr("(1.0)")

                                C.Slider {
                                    id: valueSlider12
                                    Layout.fillWidth: true
                                    from: 0.0
                                    to: 100.0
                                    stepSize: 0.1
                                    value: Chiaki.settings.placeboColorAdjustmentGamma
                                    onMoved: Chiaki.settings.placeboColorAdjustmentGamma = value
                                    visible: Chiaki.settings.placeboColorAdjustmentEnabled && (Chiaki.settings.placeboColorAdjustmentPreset == 0)
                                }
                                Label {
                                    Layout.preferredWidth: 110
                                    horizontalAlignment: Text.AlignRight
                                    text: qsTr(valueSlider12.value.toFixed(1))
                                }
                            }

                            C.SettingRow {
                                visible: Chiaki.settings.placeboColorAdjustmentEnabled && (Chiaki.settings.placeboColorAdjustmentPreset == 0)
                                title: qsTr("Temperature")
                                hint: qsTr("(0.000)")

                                C.Slider {
                                    id: valueSlider13
                                    Layout.fillWidth: true
                                    from: -1.143
                                    to: 5.286
                                    stepSize: 0.001
                                    value: Chiaki.settings.placeboColorAdjustmentTemperature
                                    onMoved: Chiaki.settings.placeboColorAdjustmentTemperature = value
                                    visible: Chiaki.settings.placeboColorAdjustmentEnabled && (Chiaki.settings.placeboColorAdjustmentPreset == 0)
                                }
                                Label {
                                    Layout.preferredWidth: 110
                                    horizontalAlignment: Text.AlignRight
                                    text: qsTr(valueSlider13.value.toFixed(3))
                                }
                            }
                        }
                    }
                }
            }

            Item {
                // Peak Detection
                Flickable {
                    id: page6Flick
                    anchors.fill: parent
                    clip: true
                    contentWidth: width
                    contentHeight: page6Column.implicitHeight + 48
                    flickableDirection: Flickable.VerticalFlick
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar {
                        visible: page6Flick.contentHeight > page6Flick.height
                    }
                    ColumnLayout {
                        id: page6Column
                        x: Math.max(28, Math.round((page6Flick.width - width) / 2))
                        y: 24
                        width: Math.min(page6Flick.width - 56, 980)
                        spacing: 18

                        C.SettingsSection {
                            title: qsTr("Peak detection")

                            C.SettingRow {
                                title: qsTr("HDR Peak Detection Enabled")
                                description: qsTr("Enable HDR Peak Detection")
                                hint: qsTr("(Checked)")

                                Item { Layout.fillWidth: true }
                                C.CheckBox {
                                    checked: Chiaki.settings.placeboPeakDetectionEnabled
                                    onToggled: Chiaki.settings.placeboPeakDetectionEnabled = checked
                                }
                            }

                            C.SettingRow {
                                visible: Chiaki.settings.placeboPeakDetectionEnabled
                                title: qsTr("Peak Detection Preset")
                                hint: qsTr("(High Quality)")

                                C.ComboBox {
                                    Layout.fillWidth: true
                                    model: [qsTr("Custom"), qsTr("Default"), qsTr("High Quality")]
                                    currentIndex: Chiaki.settings.placeboPeakDetectionPreset
                                    onActivated: index => Chiaki.settings.placeboPeakDetectionPreset = index
                                    visible: Chiaki.settings.placeboPeakDetectionEnabled
                                }
                            }

                            C.SettingRow {
                                visible: Chiaki.settings.placeboPeakDetectionEnabled && (Chiaki.settings.placeboPeakDetectionPreset == 0)
                                title: qsTr("Peak Smoothing Period")
                                hint: qsTr("(20.0)")

                                C.Slider {
                                    id: valueSlider14
                                    Layout.fillWidth: true
                                    from: 0.0
                                    to: 1000.0
                                    stepSize: 0.1
                                    value: Chiaki.settings.placeboPeakDetectionPeakSmoothingPeriod
                                    onMoved: Chiaki.settings.placeboPeakDetectionPeakSmoothingPeriod = value
                                    visible: Chiaki.settings.placeboPeakDetectionEnabled && (Chiaki.settings.placeboPeakDetectionPreset == 0)
                                }
                                Label {
                                    Layout.preferredWidth: 110
                                    horizontalAlignment: Text.AlignRight
                                    text: qsTr(valueSlider14.value.toFixed(1))
                                }
                            }

                            C.SettingRow {
                                visible: Chiaki.settings.placeboPeakDetectionEnabled && (Chiaki.settings.placeboPeakDetectionPreset == 0)
                                title: qsTr("Scene Threshold Low")
                                hint: qsTr("(1.0)")

                                C.Slider {
                                    id: valueSlider15
                                    Layout.fillWidth: true
                                    from: 0.0
                                    to: 100.0
                                    stepSize: 0.1
                                    value: Chiaki.settings.placeboPeakDetectionSceneThresholdLow
                                    onMoved: Chiaki.settings.placeboPeakDetectionSceneThresholdLow = value
                                    visible: Chiaki.settings.placeboPeakDetectionEnabled && (Chiaki.settings.placeboPeakDetectionPreset == 0)
                                }
                                Label {
                                    Layout.preferredWidth: 110
                                    horizontalAlignment: Text.AlignRight
                                    text: qsTr(valueSlider15.value.toFixed(1))
                                }
                            }

                            C.SettingRow {
                                visible: Chiaki.settings.placeboPeakDetectionEnabled && (Chiaki.settings.placeboPeakDetectionPreset == 0)
                                title: qsTr("Scene Threshold High")
                                hint: qsTr("(3.0)")

                                C.Slider {
                                    id: valueSlider16
                                    Layout.fillWidth: true
                                    from: 0.0
                                    to: 100.0
                                    stepSize: 0.1
                                    value: Chiaki.settings.placeboPeakDetectionSceneThresholdHigh
                                    onMoved: Chiaki.settings.placeboPeakDetectionSceneThresholdHigh = value
                                    visible: Chiaki.settings.placeboPeakDetectionEnabled && (Chiaki.settings.placeboPeakDetectionPreset == 0)
                                }
                                Label {
                                    Layout.preferredWidth: 110
                                    horizontalAlignment: Text.AlignRight
                                    text: qsTr(valueSlider16.value.toFixed(1))
                                }
                            }

                            C.SettingRow {
                                visible: Chiaki.settings.placeboPeakDetectionEnabled && (Chiaki.settings.placeboPeakDetectionPreset == 0)
                                title: qsTr("Peak Percentile")
                                hint: qsTr("(99.995)")

                                C.Slider {
                                    id: valueSlider17
                                    Layout.fillWidth: true
                                    from: 0.0
                                    to: 100.0
                                    stepSize: 0.001
                                    value: Chiaki.settings.placeboPeakDetectionPeakPercentile
                                    onMoved: Chiaki.settings.placeboPeakDetectionPeakPercentile = value
                                    visible: Chiaki.settings.placeboPeakDetectionEnabled && (Chiaki.settings.placeboPeakDetectionPreset == 0)
                                }
                                Label {
                                    Layout.preferredWidth: 110
                                    horizontalAlignment: Text.AlignRight
                                    text: qsTr(valueSlider17.value.toFixed(3))
                                }
                            }

                            C.SettingRow {
                                visible: Chiaki.settings.placeboPeakDetectionEnabled && (Chiaki.settings.placeboPeakDetectionPreset == 0)
                                title: qsTr("Black Cutoff")
                                hint: qsTr("(1.0)")

                                C.Slider {
                                    id: valueSlider18
                                    Layout.fillWidth: true
                                    from: 0.0
                                    to: 100.0
                                    stepSize: 0.1
                                    value: Chiaki.settings.placeboPeakDetectionBlackCutoff
                                    onMoved: Chiaki.settings.placeboPeakDetectionBlackCutoff = value
                                    visible: Chiaki.settings.placeboPeakDetectionEnabled && (Chiaki.settings.placeboPeakDetectionPreset == 0)
                                }
                                Label {
                                    Layout.preferredWidth: 110
                                    horizontalAlignment: Text.AlignRight
                                    text: qsTr(valueSlider18.value.toFixed(1))
                                }
                            }

                            C.SettingRow {
                                visible: Chiaki.settings.placeboPeakDetectionEnabled && (Chiaki.settings.placeboPeakDetectionPreset == 0)
                                title: qsTr("Allow Delayed Peak")
                                description: qsTr("Enable 1-Frame Delayed Peak")
                                hint: qsTr("(Unchecked)")

                                Item { Layout.fillWidth: true }
                                C.CheckBox {
                                    checked: Chiaki.settings.placeboPeakDetectionAllowDelayedPeak
                                    onToggled: Chiaki.settings.placeboPeakDetectionAllowDelayedPeak = checked
                                    visible: Chiaki.settings.placeboPeakDetectionEnabled && (Chiaki.settings.placeboPeakDetectionPreset == 0)
                                }
                            }
                        }
                    }
                }
            }

            Item {
                // Color Mapping
                Flickable {
                    id: page7Flick
                    anchors.fill: parent
                    clip: true
                    contentWidth: width
                    contentHeight: page7Column.implicitHeight + 48
                    flickableDirection: Flickable.VerticalFlick
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar {
                        visible: page7Flick.contentHeight > page7Flick.height
                    }
                    ColumnLayout {
                        id: page7Column
                        x: Math.max(28, Math.round((page7Flick.width - width) / 2))
                        y: 24
                        width: Math.min(page7Flick.width - 56, 980)
                        spacing: 18

                        C.SettingsSection {
                            title: qsTr("Colour mapping")

                            C.SettingRow {
                                title: qsTr("Color Mapping Enabled")
                                description: qsTr("Use Color Mapping Settings")
                                hint: qsTr("(Checked)")

                                Item { Layout.fillWidth: true }
                                C.CheckBox {
                                    checked: Chiaki.settings.placeboColorMappingEnabled
                                    onToggled: Chiaki.settings.placeboColorMappingEnabled = checked
                                }
                            }

                            C.SettingRow {
                                visible: Chiaki.settings.placeboColorMappingEnabled
                                title: qsTr("Color Mapping Preset")
                                hint: qsTr("(High Quality)")

                                C.ComboBox {
                                    Layout.fillWidth: true
                                    model: [qsTr("Custom"), qsTr("Default"), qsTr("High Quality")]
                                    currentIndex: Chiaki.settings.placeboColorMappingPreset
                                    onActivated: index => Chiaki.settings.placeboColorMappingPreset = index
                                    visible: Chiaki.settings.placeboColorMappingEnabled
                                }
                            }

                            C.SettingRow {
                                visible: Chiaki.settings.placeboColorMappingEnabled && Chiaki.settings.placeboColorMappingPreset == 0
                                title: qsTr("Color Mapping Settings")

                                Item { Layout.fillWidth: true }
                                C.Button {
                                    text: qsTr("Open")
                                    onClicked: root.showPlaceboColorMappingDialog()
                                    visible: Chiaki.settings.placeboColorMappingEnabled && Chiaki.settings.placeboColorMappingPreset == 0
                                    Material.roundedScale: Material.SmallScale
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
