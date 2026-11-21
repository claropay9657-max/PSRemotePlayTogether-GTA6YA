import QtQuick
import QtQuick.Layouts

import org.streetpea.chiaking

import "../controls" as C

// Picture: how the stream window opens, how the picture is decoded and drawn, and what
// this screen can show (what was the Display dialog). Two columns where there is room.
C.SettingsPage {
    id: page
    title: qsTr("Picture")
    description: qsTr("How the stream window opens, how the picture is made, and what your screen can show.")

    /** Chiaki.settings.windowType: SelectedResolution, CustomResolution, AdjustableResolution, Fullscreen, Zoom, Stretch */
    readonly property var windowTypes: [qsTr("Stream size"), qsTr("Custom size"), qsTr("As I left it"),
        qsTr("Full screen"), qsTr("Full screen, zoomed"), qsTr("Full screen, stretched")]
    /** Chiaki.settings.videoPreset and Chiaki.window.videoPreset: Fast, Default, HighQuality,
     *  HighQualitySpatial, HighQualityAdvancedSpatial, Custom. Named as in the stream menu. */
    readonly property var presets: [qsTr("Fast"), qsTr("Default"), qsTr("High quality"), qsTr("HQ + spatial"),
        qsTr("HQ + advanced spatial"), qsTr("Custom")]
    readonly property int presetCustom: 5

    /** The decoders as they are offered: Auto first, the graphics chips, then none (the processor). */
    readonly property var decoders: {
        const all = Chiaki.settings.availableDecoders || [];
        let list = all.indexOf("auto") >= 0 ? ["auto"] : [];
        list = list.concat(all.filter((d) => d !== "auto" && d !== "none"));
        if (all.indexOf("none") >= 0)
            list.push("none");
        // One saved that this computer does not have is shown as it is
        const current = Chiaki.settings.decoder || "none";
        if (list.indexOf(current) < 0)
            list.push(current);
        return list;
    }
    readonly property var decoderNames: ({ auto: qsTr("Auto"), none: qsTr("Off: the processor"), vulkan: "Vulkan",
        vaapi: "VA-API", cuda: "CUDA", videotoolbox: "VideoToolbox", d3d11va: "Direct3D 11" })

    /** Chiaki.settings.displayTargetPrim: libplacebo's pl_color_primaries, 0 is auto. */
    readonly property var colourSpaces: [qsTr("Auto"), "BT.601 NTSC", "BT.601 PAL", "BT.709 (sRGB)", "BT.470 M",
        "EBU 3213-E", qsTr("BT.2020 (wide)"), qsTr("Apple RGB (wide)"), qsTr("Adobe RGB (wide)"),
        qsTr("ProPhoto RGB (wide)"), qsTr("CIE 1931 RGB (wide)"), qsTr("DCI-P3 (wide)"), qsTr("Display P3 (wide)"),
        qsTr("V-Gamut (wide)"), qsTr("S-Gamut (wide)"), qsTr("Film C (wide)"), qsTr("ACES AP0 (wide)"),
        qsTr("ACES AP1 (wide)")]
    /** Chiaki.settings.displayTargetTrc: libplacebo's pl_color_transfer, 0 is auto. */
    readonly property var brightnessCurves: [qsTr("Auto"), "BT.1886 (SDR)", "sRGB (SDR)", qsTr("Linear (SDR)"),
        qsTr("Gamma 1.8 (SDR)"), qsTr("Gamma 2.0 (SDR)"), qsTr("Gamma 2.2 (SDR)"), qsTr("Gamma 2.4 (SDR)"),
        qsTr("Gamma 2.6 (SDR)"), qsTr("Gamma 2.8 (SDR)"), "ProPhoto RGB (SDR)", qsTr("Digital cinema (SDR)"),
        "PQ (HDR)", "HLG (HDR)", "V-Log (HDR)", "S-Log1 (HDR)", "S-Log2 (HDR)"]
    /** What the sliders for a set peak brightness (nits) and contrast (:1) stop at; the old range. */
    readonly property var peakSteps: [10, 50, 80, 100, 120, 150, 200, 250, 300, 350, 400, 450, 500, 600, 700, 800,
        900, 1000, 1100, 1200, 1400, 1500, 1600, 1800, 2000, 2500, 3000, 4000, 5000, 10000]
    readonly property var contrastSteps: [10, 20, 50, 100, 200, 300, 500, 700, 800, 1000, 1200, 1500, 2000, 2500,
        3000, 4000, 5000, 6000, 7000, 10000, 20000, 50000, 100000, 1000000]

    function nearestStep(steps, value) {
        let best = 0;
        for (let i = 1; i < steps.length; ++i) {
            if (Math.abs(steps[i] - value) < Math.abs(steps[best] - value))
                best = i;
        }
        return best;
    }

    function number(value) {
        return Number(value).toLocaleString(Qt.locale(), "f", 0);
    }

    /** A change that needs a new start: asks first, then changes it and restarts the app. */
    function changeAndRestart(change) {
        root.showConfirmDialog(qsTr("Restart"),
            qsTr("This change needs a new start of PSRemotePlayTogether. Restart now?"),
            () => {
                change();
                if (Chiaki.settings.restartApplication())
                    Qt.quit();
            });
    }

    /** One choice of "Your screen": its name over its list. */
    component ScreenChoice: ColumnLayout {
        id: choice
        property string title
        property string subtitle
        property string keywords
        readonly property bool isSettingRow: true

        Layout.fillWidth: true
        Layout.preferredWidth: 1
        Layout.alignment: Qt.AlignTop
        spacing: 6

        Text {
            Layout.fillWidth: true
            text: choice.title
            elide: Text.ElideRight
            color: C.Theme.textSoft
            font.pixelSize: 12
            font.weight: Font.Bold
        }
    }

    /** A set value of "Your screen", on a slider under the lists. */
    component ScreenValue: RowLayout {
        id: screenValue
        property string title
        property string subtitle
        property string keywords
        readonly property bool isSettingRow: true

        Layout.fillWidth: true
        spacing: 12

        Text {
            Layout.preferredWidth: 150
            text: screenValue.title
            wrapMode: Text.WordWrap
            color: C.Theme.textSoft
            font.pixelSize: 12
            font.weight: Font.Bold
        }
    }

    Item {
        id: columns
        /** Two columns as on the board where they fit, else one under the other. */
        readonly property bool two: page.width >= 860
        readonly property int gap: 20

        Layout.fillWidth: true
        implicitHeight: two ? Math.max(left.implicitHeight, right.implicitHeight)
                            : left.implicitHeight + 18 + right.implicitHeight

        ColumnLayout {
            id: left
            width: columns.two ? Math.floor((columns.width - columns.gap) / 2) : columns.width
            spacing: 18

            C.SettingsGroup {
                title: qsTr("Window")

                C.SettingsRow {
                    title: qsTr("Window size")
                    subtitle: Chiaki.settings.windowType === 2 ? qsTr("Keeps the size and place you last gave it.")
                            : Chiaki.settings.windowType === 4 ? qsTr("Set how far it zooms with the slider in the stream menu.")
                            : qsTr("How the stream window opens.")
                    keywords: qsTr("resolution fullscreen zoom stretch custom adjust manually")
                    textMinimum: 140

                    C.Dropdown {
                        Accessible.name: qsTr("Window size")
                        model: page.windowTypes
                        currentIndex: Chiaki.settings.windowType
                        onActivated: (index) => Chiaki.settings.windowType = index
                    }
                }

                C.SettingsRow {
                    visible: Chiaki.settings.windowType === 1
                    sub: true
                    title: qsTr("Custom size")
                    subtitle: qsTr("Width × height, in pixels.")
                    keywords: qsTr("custom resolution width height pixels")
                    textMinimum: 120

                    C.Field {
                        id: customWidth
                        function valid() {
                            const number = parseInt(text);
                            return number >= 0 && number <= 9999;
                        }
                        Layout.preferredWidth: 84
                        Accessible.name: qsTr("Width")
                        hint: qsTr("Width")
                        inputMethodHints: Qt.ImhDigitsOnly
                        color: text.length > 0 && !valid() ? C.Theme.bad : C.Theme.text
                        text: Chiaki.settings.customResolutionWidth
                        onEditingFinished: {
                            if (valid()) {
                                Chiaki.settings.customResolutionWidth = parseInt(text);
                            } else {
                                Chiaki.settings.customResolutionWidth = 0;
                                text = "";
                            }
                        }
                    }

                    Text {
                        text: "×"
                        color: C.Theme.textDim
                        font.pixelSize: 15
                    }

                    C.Field {
                        id: customHeight
                        function valid() {
                            const number = parseInt(text);
                            return number >= 0 && number <= 9999;
                        }
                        Layout.preferredWidth: 84
                        Accessible.name: qsTr("Height")
                        hint: qsTr("Height")
                        inputMethodHints: Qt.ImhDigitsOnly
                        color: text.length > 0 && !valid() ? C.Theme.bad : C.Theme.text
                        text: Chiaki.settings.customResolutionHeight
                        onEditingFinished: {
                            if (valid()) {
                                Chiaki.settings.customResolutionHeight = parseInt(text);
                            } else {
                                Chiaki.settings.customResolutionHeight = 0;
                                text = "";
                            }
                        }
                    }
                }

                C.SettingsRow {
                    title: qsTr("Full screen on double-click")
                    keywords: qsTr("fullscreen mouse window")
                    switchItem: doubleClick

                    C.Toggle {
                        id: doubleClick
                        Accessible.name: qsTr("Full screen on double-click")
                        checked: Chiaki.settings.fullscreenDoubleClick
                        onToggled: Chiaki.settings.fullscreenDoubleClick = checked
                    }
                }

                C.SettingsRow {
                    title: qsTr("Hide the mouse pointer while streaming")
                    keywords: qsTr("cursor mouse hide")
                    switchItem: hideCursor

                    C.Toggle {
                        id: hideCursor
                        Accessible.name: qsTr("Hide the mouse pointer while streaming")
                        checked: Chiaki.settings.hideCursor
                        onToggled: Chiaki.settings.hideCursor = checked
                    }
                }
            }

            C.SettingsGroup {
                title: qsTr("Decoding")

                C.SettingsRow {
                    title: qsTr("Hardware decoder")
                    subtitle: qsTr("Auto picks the graphics chip when it can.")
                    keywords: qsTr("hw decode gpu graphics card videotoolbox vaapi cuda vulkan d3d11va software")
                    textMinimum: 140

                    C.Dropdown {
                        Accessible.name: qsTr("Hardware decoder")
                        model: page.decoders.map((d) => page.decoderNames[d] || d)
                        currentIndex: page.decoders.indexOf(Chiaki.settings.decoder || "none")
                        // None is saved as no decoder at all, as before
                        onActivated: (index) => Chiaki.settings.decoder = page.decoders[index] === "none" ? "" : page.decoders[index]
                    }
                }

                C.SettingsRow {
                    title: qsTr("Zero-copy")
                    subtitle: qsTr("Hands the decoded picture straight to the screen.")
                    keywords: qsTr("zero copy hardware decoder")
                    switchItem: zeroCopy

                    C.Toggle {
                        id: zeroCopy
                        Accessible.name: qsTr("Zero-copy")
                        checked: Chiaki.settings.useZeroCopy
                        onToggled: Chiaki.settings.useZeroCopy = checked
                    }
                }
            }
        }

        ColumnLayout {
            id: right
            x: columns.two ? left.width + columns.gap : 0
            y: columns.two ? 0 : left.implicitHeight + 18
            width: left.width
            spacing: 18

            C.SettingsGroup {
                title: qsTr("Rendering")

                C.SettingsRow {
                    title: qsTr("Quality preset")
                    subtitle: qsTr("Upscaling and filters. Also in the stream menu.")
                    keywords: qsTr("video preset placebo spatial upscaling fast high quality")
                    textMinimum: 140

                    C.Dropdown {
                        Accessible.name: qsTr("Quality preset")
                        model: page.presets
                        currentIndex: Chiaki.settings.videoPreset
                        onActivated: (index) => {
                            Chiaki.settings.videoPreset = index;
                            // ChiakiWindow.VideoPreset has the same order
                            Chiaki.window.videoPreset = index;
                        }
                    }
                }

                C.SettingsRow {
                    visible: Chiaki.window.videoPreset === page.presetCustom
                    sub: true
                    title: qsTr("Custom renderer settings")
                    subtitle: qsTr("Scaling, debanding, tone mapping and more.")
                    keywords: qsTr("placebo libplacebo custom renderer")
                    textMinimum: 140

                    C.PillButton {
                        small: true
                        text: qsTr("Open…")
                        onClicked: root.showPlaceboSettingsDialog()
                    }
                }

                C.SettingsRow {
                    title: qsTr("Vertical sync")
                    subtitle: qsTr("Avoids tearing; can add a little delay.")
                    keywords: qsTr("vsync tearing")
                    // Only the OpenGL renderer needs a new start for it
                    restart: Chiaki.window.runtimeRendererBackend === 1
                    switchItem: vsync

                    C.Toggle {
                        id: vsync
                        Accessible.name: qsTr("Vertical sync")
                        checked: Chiaki.settings.vSyncEnabled
                        onToggled: {
                            const enable = checked;
                            // Shows the setting until it has changed
                            checked = Qt.binding(() => Chiaki.settings.vSyncEnabled);
                            if (Chiaki.window.runtimeRendererBackend === 1)
                                page.changeAndRestart(() => Chiaki.settings.vSyncEnabled = enable);
                            else
                                Chiaki.settings.vSyncEnabled = enable;
                        }
                    }
                }

                C.SettingsRow {
                    title: qsTr("Renderer")
                    keywords: qsTr("vulkan opengl backend graphics")
                    restart: true
                    textMinimum: 120

                    C.Segmented {
                        Accessible.name: qsTr("Renderer")
                        // Chiaki.settings.rendererBackend: 0 Vulkan, 1 OpenGL
                        model: ["Vulkan", "OpenGL"]
                        currentIndex: Chiaki.settings.rendererBackend
                        onActivated: (index) => {
                            if (index !== Chiaki.settings.rendererBackend)
                                page.changeAndRestart(() => Chiaki.settings.rendererBackend = index);
                        }
                    }
                }

                C.SettingsRow {
                    visible: Chiaki.settings.rendererBackend === 0
                    sub: true
                    title: qsTr("Vulkan deferred swap")
                    subtitle: qsTr("Hands each finished picture to the screen from a thread of its own.")
                    keywords: qsTr("present swapchain thread vulkan")
                    switchItem: deferredSwap

                    C.Toggle {
                        id: deferredSwap
                        Accessible.name: qsTr("Vulkan deferred swap")
                        checked: Chiaki.settings.vulkanDeferredSwap
                        onToggled: Chiaki.settings.vulkanDeferredSwap = checked
                    }
                }

                C.SettingsRow {
                    title: qsTr("Frame delivery")
                    keywords: qsTr("direct frame mapping queue buffer")
                    restart: true
                    textMinimum: 120

                    C.Segmented {
                        Accessible.name: qsTr("Frame delivery")
                        model: [qsTr("Direct"), qsTr("Frame queue")]
                        currentIndex: Chiaki.settings.directFrameMapping ? 0 : 1
                        onActivated: (index) => {
                            const direct = index === 0;
                            if (direct !== Chiaki.settings.directFrameMapping)
                                page.changeAndRestart(() => Chiaki.settings.directFrameMapping = direct);
                        }
                    }
                }

                C.SettingsRow {
                    enabled: !Chiaki.settings.directFrameMapping
                    sub: true
                    title: qsTr("Frame mixer")
                    subtitle: qsTr("Only with the frame queue.")
                    keywords: qsTr("blend oversample hermite linear cubic interpolation")
                    textMinimum: 120

                    C.Dropdown {
                        Accessible.name: qsTr("Frame mixer")
                        // Chiaki.settings.placeboFrameMixer: None, Oversample, Hermite, Linear, Cubic
                        model: [qsTr("None"), qsTr("Oversample"), qsTr("Hermite"), qsTr("Linear"), qsTr("Cubic")]
                        currentIndex: Chiaki.settings.directFrameMapping ? 0 : Chiaki.settings.placeboFrameMixer
                        onActivated: (index) => Chiaki.settings.placeboFrameMixer = index
                    }
                }
            }

            C.SettingsGroup {
                title: qsTr("Your screen")

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.margins: 16
                    Layout.topMargin: 14
                    spacing: 12

                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Leave all on Auto unless colours or brightness look wrong. These describe this screen, not the console.")
                        wrapMode: Text.WordWrap
                        lineHeight: 1.1
                        color: C.Theme.textSoft
                        font.pixelSize: 13
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: width >= 360 ? 2 : 1
                        columnSpacing: 10
                        rowSpacing: 10

                        ScreenChoice {
                            title: qsTr("Colour space")
                            keywords: qsTr("color primaries gamut display target bt.709 bt.2020 p3 hdr")

                            C.Dropdown {
                                Layout.fillWidth: true
                                Accessible.name: qsTr("Colour space")
                                model: page.colourSpaces
                                currentIndex: Chiaki.settings.displayTargetPrim
                                onActivated: (index) => Chiaki.settings.displayTargetPrim = index
                            }
                        }

                        ScreenChoice {
                            title: qsTr("Brightness curve")
                            keywords: qsTr("transfer characteristics gamma trc pq hlg hdr sdr display target")

                            C.Dropdown {
                                Layout.fillWidth: true
                                Accessible.name: qsTr("Brightness curve")
                                model: page.brightnessCurves
                                currentIndex: Chiaki.settings.displayTargetTrc
                                onActivated: (index) => Chiaki.settings.displayTargetTrc = index
                            }
                        }

                        ScreenChoice {
                            title: qsTr("Peak brightness")
                            keywords: qsTr("nits hdr luminance display target")

                            C.Dropdown {
                                Layout.fillWidth: true
                                Accessible.name: qsTr("Peak brightness")
                                // Chiaki.settings.displayTargetPeak: 0 auto, else nits
                                model: [qsTr("Auto"), qsTr("Set by hand")]
                                currentIndex: Chiaki.settings.displayTargetPeak === 0 ? 0 : 1
                                onActivated: (index) => {
                                    if (index === 0)
                                        Chiaki.settings.displayTargetPeak = 0;
                                    else if (Chiaki.settings.displayTargetPeak === 0)
                                        Chiaki.settings.displayTargetPeak = 1000;
                                }
                            }
                        }

                        ScreenChoice {
                            title: qsTr("Contrast")
                            keywords: qsTr("oled black infinity display target")

                            C.Dropdown {
                                Layout.fillWidth: true
                                Accessible.name: qsTr("Contrast")
                                // Chiaki.settings.displayTargetContrast: 0 auto, -1 infinite, else n:1
                                model: [qsTr("Auto"), qsTr("Infinite (OLED)"), qsTr("Set by hand")]
                                currentIndex: Chiaki.settings.displayTargetContrast === 0 ? 0
                                            : Chiaki.settings.displayTargetContrast === -1 ? 1 : 2
                                onActivated: (index) => {
                                    if (index === 0)
                                        Chiaki.settings.displayTargetContrast = 0;
                                    else if (index === 1)
                                        Chiaki.settings.displayTargetContrast = -1;
                                    else if (Chiaki.settings.displayTargetContrast <= 0)
                                        Chiaki.settings.displayTargetContrast = 1000;
                                }
                            }
                        }
                    }

                    ScreenValue {
                        visible: Chiaki.settings.displayTargetPeak !== 0
                        title: qsTr("Peak brightness value")
                        keywords: qsTr("nits hdr luminance")

                        C.ValueSlider {
                            Layout.fillWidth: true
                            Accessible.name: qsTr("Peak brightness value")
                            from: 0
                            to: page.peakSteps.length - 1
                            stepSize: 1
                            valueWidth: 92
                            value: page.nearestStep(page.peakSteps, Chiaki.settings.displayTargetPeak)
                            valueText: qsTr("%1 nits").arg(page.number(Chiaki.settings.displayTargetPeak))
                            onMoved: Chiaki.settings.displayTargetPeak = page.peakSteps[Math.round(value)]
                        }
                    }

                    ScreenValue {
                        visible: Chiaki.settings.displayTargetContrast > 0
                        title: qsTr("Contrast value")
                        keywords: qsTr("contrast ratio")

                        C.ValueSlider {
                            Layout.fillWidth: true
                            Accessible.name: qsTr("Contrast value")
                            from: 0
                            to: page.contrastSteps.length - 1
                            stepSize: 1
                            valueWidth: 92
                            value: page.nearestStep(page.contrastSteps, Chiaki.settings.displayTargetContrast)
                            valueText: qsTr("%1:1").arg(page.number(Chiaki.settings.displayTargetContrast))
                            onMoved: Chiaki.settings.displayTargetContrast = page.contrastSteps[Math.round(value)]
                        }
                    }
                }
            }
        }
    }
}
