import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

import org.streetpea.chiaking

import "controls" as C

// What the stream is shown as: colour space and brightness of this screen. The same
// settings as "Your screen" in Settings › Picture, with the same names.
//
// Also shown over the game (Main.qml's displaySettingsLoader, 1200 × 500, or a window
// of its own as high as `gridHeight` needs): the page scrolls, and the setting with the
// focus is kept on screen.
DialogView {
    id: dialog
    property alias gridHeight: displayGrid.implicitHeight
    title: qsTr("Display")
    header: qsTr("Changes are saved at once")
    buttonVisible: false

    readonly property int gutter: width < 600 ? 16 : 32
    /** Width of the column the controls sit in: half a row at most, less where there is little room. */
    readonly property int controlWidth: Math.round(Math.min(400, Math.max(190, (displayGrid.width - 36) * 0.46)))

    function number(value) {
        return Number(value).toLocaleString(Qt.locale(), "f", 0);
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

    component Setting: C.SettingRow {
        controlWidth: dialog.controlWidth
    }

    Item {
        // Keeps the setting with the focus on screen, also over the game where there is little room
        Connections {
            target: dialog.Window.window
            function onActiveFocusItemChanged() {
                const item = dialog.Window.window ? dialog.Window.window.activeFocusItem : null;
                if (item && dialog.inside(item, displayFlick.contentItem))
                    dialog.scrollTo(dialog.rowOf(item, displayFlick), displayFlick, 12);
            }
        }

        Flickable {
            id: displayFlick
            anchors.fill: parent
            clip: true
            contentWidth: width
            contentHeight: displayGrid.implicitHeight + 48
            flickableDirection: Flickable.VerticalFlick
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {
                policy: displayFlick.contentHeight > displayFlick.height ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
                width: 10
            }

            ColumnLayout {
                id: displayGrid
                x: Math.max(dialog.gutter, Math.round((displayFlick.width - width) / 2))
                y: 24
                width: Math.min(displayFlick.width - 2 * dialog.gutter, 920)
                spacing: 18

                C.SettingsSection {
                    title: qsTr("Your screen")
                    description: qsTr("Leave all on Auto unless colours or brightness look wrong. These describe this screen, not the console.")

                    Setting {
                        title: qsTr("Colour space")
                        description: qsTr("The colours the screen can show.")
                        hint: qsTr("Auto")

                        // Chiaki.settings.displayTargetPrim: libplacebo's pl_color_primaries, 0 is auto
                        C.Dropdown {
                            firstInFocusChain: true
                            Layout.fillWidth: true
                            Accessible.name: qsTr("Colour space")
                            model: [qsTr("Auto"), "BT.601 NTSC", "BT.601 PAL", "BT.709 (sRGB)", "BT.470 M",
                                "EBU 3213-E", qsTr("BT.2020 (wide)"), qsTr("Apple RGB (wide)"), qsTr("Adobe RGB (wide)"),
                                qsTr("ProPhoto RGB (wide)"), qsTr("CIE 1931 RGB (wide)"), qsTr("DCI-P3 (wide)"),
                                qsTr("Display P3 (wide)"), qsTr("V-Gamut (wide)"), qsTr("S-Gamut (wide)"),
                                qsTr("Film C (wide)"), qsTr("ACES AP0 (wide)"), qsTr("ACES AP1 (wide)")]
                            currentIndex: Chiaki.settings.displayTargetPrim
                            onActivated: index => Chiaki.settings.displayTargetPrim = index
                        }
                    }

                    Setting {
                        title: qsTr("Brightness curve")
                        description: qsTr("SDR gamma, or PQ / HLG for HDR screens.")
                        hint: qsTr("Auto")

                        // Chiaki.settings.displayTargetTrc: libplacebo's pl_color_transfer, 0 is auto
                        C.Dropdown {
                            Layout.fillWidth: true
                            Accessible.name: qsTr("Brightness curve")
                            model: [qsTr("Auto"), "BT.1886 (SDR)", "sRGB (SDR)", qsTr("Linear (SDR)"),
                                qsTr("Gamma 1.8 (SDR)"), qsTr("Gamma 2.0 (SDR)"), qsTr("Gamma 2.2 (SDR)"),
                                qsTr("Gamma 2.4 (SDR)"), qsTr("Gamma 2.6 (SDR)"), qsTr("Gamma 2.8 (SDR)"),
                                "ProPhoto RGB (SDR)", qsTr("Digital cinema (SDR)"), "PQ (HDR)", "HLG (HDR)",
                                "V-Log (HDR)", "S-Log1 (HDR)", "S-Log2 (HDR)"]
                            currentIndex: Chiaki.settings.displayTargetTrc
                            onActivated: index => Chiaki.settings.displayTargetTrc = index
                        }
                    }
                }

                C.SettingsSection {
                    title: qsTr("Brightness and contrast")

                    Setting {
                        title: qsTr("Peak brightness")
                        description: qsTr("How bright the screen can get, in nits.")
                        hint: qsTr("Auto")

                        // Chiaki.settings.displayTargetPeak: 0 auto, else nits
                        C.Dropdown {
                            id: targetPeakChoice
                            Layout.fillWidth: true
                            Accessible.name: qsTr("Peak brightness")
                            model: [qsTr("Auto"), qsTr("Set by hand")]
                            currentIndex: Chiaki.settings.displayTargetPeak == 0 ? 0 : 1
                            onActivated: index => {
                                if (index == 0)
                                    Chiaki.settings.displayTargetPeak = 0;
                                else if (index == 1)
                                    Chiaki.settings.displayTargetPeak = 1000;
                            }
                        }
                    }

                    Setting {
                        visible: targetPeakChoice.currentIndex == 1
                        title: qsTr("Peak brightness value")
                        hint: qsTr("%1 nits").arg(dialog.number(1000))

                        C.ValueSlider {
                            id: targetPeakValue
                            Layout.fillWidth: true
                            Accessible.name: qsTr("Peak brightness value")
                            valueWidth: 84
                            from: 10
                            to: 10000
                            stepSize: 10
                            value: Chiaki.settings.displayTargetPeak
                            valueText: qsTr("%1 nits").arg(dialog.number(value))
                            onMoved: Chiaki.settings.displayTargetPeak = value
                        }
                    }

                    Setting {
                        title: qsTr("Contrast")
                        description: qsTr("Infinite for OLED and other screens with true black.")
                        hint: qsTr("Auto")

                        // Chiaki.settings.displayTargetContrast: 0 auto, -1 infinite, else n:1
                        C.Dropdown {
                            id: targetContrastChoice
                            Layout.fillWidth: true
                            Accessible.name: qsTr("Contrast")
                            lastInFocusChain: currentIndex != 2
                            model: [qsTr("Auto"), qsTr("Infinite (OLED)"), qsTr("Set by hand")]
                            currentIndex: {
                                if (Chiaki.settings.displayTargetContrast == -1)
                                    1;
                                else if (Chiaki.settings.displayTargetContrast == 0)
                                    0;
                                else
                                    2;
                            }
                            onActivated: index => {
                                if (index == 0)
                                    Chiaki.settings.displayTargetContrast = 0;
                                else if (index == 1)
                                    Chiaki.settings.displayTargetContrast = -1;
                                else if (index == 2)
                                    Chiaki.settings.displayTargetContrast = 1000;
                            }
                        }
                    }

                    Setting {
                        visible: targetContrastChoice.currentIndex == 2
                        title: qsTr("Contrast value")
                        hint: qsTr("%1:1").arg(dialog.number(1000))

                        C.ValueSlider {
                            id: targetContrastValue
                            Layout.fillWidth: true
                            Accessible.name: qsTr("Contrast value")
                            lastInFocusChain: true
                            valueWidth: 96
                            from: 10
                            to: 1000000
                            stepSize: 1000
                            value: Chiaki.settings.displayTargetContrast
                            valueText: qsTr("%1:1").arg(dialog.number(value))
                            onMoved: Chiaki.settings.displayTargetContrast = value
                        }
                    }
                }
            }
        }
    }
}
