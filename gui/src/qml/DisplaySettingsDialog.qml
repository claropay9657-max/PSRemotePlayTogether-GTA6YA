import QtCore
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Dialogs

import org.streetpea.chiaking

import "controls" as C

// What the stream is shown as: colour space and brightness of this screen.
DialogView {
    id: dialog
    property alias gridHeight: displayGrid.implicitHeight
    title: qsTr("Display")
    header: qsTr("Changes are saved at once")
    buttonVisible: false

    Item {
        Flickable {
            id: displayFlick
            anchors.fill: parent
            clip: true
            contentWidth: width
            contentHeight: displayGrid.implicitHeight + 48
            flickableDirection: Flickable.VerticalFlick
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {
                visible: displayFlick.contentHeight > displayFlick.height
            }

            ColumnLayout {
                id: displayGrid
                x: Math.max(28, Math.round((displayFlick.width - width) / 2))
                y: 24
                width: Math.min(displayFlick.width - 56, 980)
                spacing: 18

                C.SettingsSection {
                    title: qsTr("Your screen")
                    description: qsTr("Leave everything on Auto unless colours or brightness look wrong. These describe this screen, not the console.")

                    C.SettingRow {
                        title: qsTr("Colour primaries")
                        description: qsTr("The colour space the screen shows.")
                        hint: qsTr("(Auto)")
                        controlWidth: 480

                        C.ComboBox {
                            firstInFocusChain: true
                            Layout.fillWidth: true
                            font.pixelSize: 14
                            model: [qsTr("Auto"), qsTr("ITU-R Rec. BT.601 NTSC (Standard Gamut)"), qsTr("ITU-R Rec. BT.601 PAL (Standard Gamut)"), qsTr("ITU-R Rec. BT.709 (Standard Gamut)"), qsTr("ITU-R Rec. BT.470 M (Standard Gamut)"), qsTr("EBU Tech. 3213-E (Standard Gamut)"), qsTr("ITU-R Rec. BT.2020 (Wide Gamut)"), qsTr("Apple RGB (Wide Gamut)"), qsTr("Adobe RGB (Wide Gamut)"), qsTr("ProPhoto RGB (Wide Gamut)"), qsTr("CIE 1931 RGB primaries (Wide Gamut)"), qsTr("DCI-P3 (Wide Gamut)"), qsTr("DCI-P3 with D65 white point (Wide Gamut)"), qsTr("Panasonic V-Gamut (Wide Gamut)"), qsTr("Sony S-Gamut (Wide Gamut)"), qsTr("Traditional film primaries with Illuminant C (Wide Gamut)"), qsTr("ACES Primaries #0 (Wide Gamut)"), qsTr("ACES Primaries #1 (Wide Gamut)")]
                            currentIndex: Chiaki.settings.displayTargetPrim
                            onActivated: index => Chiaki.settings.displayTargetPrim = index
                        }
                    }

                    C.SettingRow {
                        title: qsTr("Transfer characteristics")
                        description: qsTr("The brightness curve: SDR gamma, or PQ / HLG for HDR screens.")
                        hint: qsTr("(Auto)")
                        controlWidth: 480

                        C.ComboBox {
                            Layout.fillWidth: true
                            font.pixelSize: 14
                            model: [qsTr("Auto"), qsTr("ITU-R Rec. BT.1886 (SDR)"), qsTr("IEC 61966-2-4 sRGB (SDR)"), qsTr("Linear light content (SDR)"), qsTr("IPure power gamma 1.8 (SDR)"), qsTr("Pure power gamma 2.0 (SDR)"), qsTr("Pure power gamma 2.2 (SDR)"), qsTr("Pure power gamma 2.4 (SDR)"), qsTr("Pure power gamma 2.6 (SDR)"), qsTr("Pure power gamma 2.8 (SDR)"), qsTr("ProPhoto RGB (SDR)"), qsTr("Digital Cinema Distribution Master (SDR)"), qsTr("ITU-R BT.2100 PQ / SMPTE ST2048 (HDR)"), qsTr("ITU-R BT.2100 HLG / ARIB STD-B67 (HDR)"), qsTr("Panasonic V-Log (HDR)"), qsTr("Sony S-Log1 (HDR)"), qsTr("Sony S-Log2 (HDR)")]
                            currentIndex: Chiaki.settings.displayTargetTrc
                            onActivated: index => Chiaki.settings.displayTargetTrc = index
                        }
                    }
                }

                C.SettingsSection {
                    title: qsTr("Brightness and contrast")

                    C.SettingRow {
                        controlWidth: 480
                        title: qsTr("Peak brightness")
                        description: qsTr("How bright the screen can get, in nits.")
                        hint: qsTr("(Auto)")

                        C.ComboBox {
                            id: targetPeakChoice
                            font.pixelSize: 14
                            Layout.fillWidth: true
                            model: [qsTr("Auto"), qsTr("Numeric Value")]
                            currentIndex: {
                                if(Chiaki.settings.displayTargetPeak == 0)
                                    0;
                                else
                                    1;
                            }
                            onActivated: index => {
                                if(index == 0)
                                    Chiaki.settings.displayTargetPeak = 0;
                                else if(index == 1)
                                    Chiaki.settings.displayTargetPeak = 1000;
                            }
                        }
                    }

                    C.SettingRow {
                        controlWidth: 480
                        visible: targetPeakChoice.currentIndex == 1
                        title: qsTr("Peak brightness value")
                        hint: qsTr("(1000 nits)")

                        C.Slider {
                            id: targetPeakValue
                            Layout.fillWidth: true
                            from: 10
                            to: 10000
                            stepSize: 10
                            value: Chiaki.settings.displayTargetPeak
                            onMoved: Chiaki.settings.displayTargetPeak = value
                        }
                        Label {
                            Layout.preferredWidth: 100
                            horizontalAlignment: Text.AlignRight
                            text: targetPeakValue.value + qsTr(" nits")
                        }
                    }

                    C.SettingRow {
                        controlWidth: 480
                        title: qsTr("Contrast")
                        description: qsTr("Infinity for OLED and other screens with true black.")
                        hint: qsTr("(Auto)")

                        C.ComboBox {
                            id: targetContrastChoice
                            font.pixelSize: 14
                            Layout.fillWidth: true
                            lastInFocusChain: currentIndex != 2
                            model: [qsTr("Auto"), qsTr("Infinity"), qsTr("Numeric Value")]
                            currentIndex: {
                                if(Chiaki.settings.displayTargetContrast == -1)
                                    1;
                                else if(Chiaki.settings.displayTargetContrast == 0)
                                    0;
                                else
                                    2;
                            }
                            onActivated: index => {
                                if(index == 0)
                                    Chiaki.settings.displayTargetContrast = 0;
                                else if(index == 1)
                                    Chiaki.settings.displayTargetContrast = -1;
                                else if(index == 2)
                                    Chiaki.settings.displayTargetContrast = 1000;
                            }
                        }
                    }

                    C.SettingRow {
                        controlWidth: 480
                        visible: targetContrastChoice.currentIndex == 2
                        title: qsTr("Contrast value")
                        hint: qsTr("(1000)")

                        C.Slider {
                            id: targetContrastValue
                            Layout.fillWidth: true
                            from: 10
                            to: 1000000
                            stepSize: 1000
                            value: Chiaki.settings.displayTargetContrast
                            onMoved: Chiaki.settings.displayTargetContrast = value
                            lastInFocusChain: true
                        }
                        Label {
                            Layout.preferredWidth: 100
                            horizontalAlignment: Text.AlignRight
                            text: targetContrastValue.value + ":1"
                        }
                    }
                }
            }
        }
    }
}
