import QtQuick
import QtQuick.Layouts

import org.streetpea.chiaking

import "../controls" as C

// Stream quality: what the console sends, for a PS5 or a PS4, at home and away. A table
// with At home and Away side by side; where a column is too narrow for its choices, each
// setting shows At home and Away one under the other.
C.SettingsPage {
    id: page
    title: qsTr("Stream quality")
    description: qsTr("What the console sends you. Each kind of console keeps its own values, for at home and away.")

    /** The values shown are the PS5's, else the PS4's. */
    property bool ps5: true
    /** As in the names of the settings: resolutionLocalPS5, bitrateRemotePS4 … */
    readonly property string kind: ps5 ? "PS5" : "PS4"
    /** The column with the names of the settings, when At home and Away are side by side. */
    readonly property int labelWidth: 170
    readonly property bool wide: (width - labelWidth) / 2 - 36
                                 >= Math.max(resolutionHome.implicitWidth, codecHome.implicitWidth, bitrateHome.implicitWidth)

    // Starts on the PS4 when all consoles you have are PS4s
    Component.onCompleted: {
        const hosts = Chiaki.settings.registeredHosts || [];
        if (hosts.length > 0 && hosts.every((host) => !host.ps5))
            ps5 = false;
    }

    /** Another page of the settings window, scrolled to a row (SettingsView.openPage). */
    function openPage(key, rowTitle, byKeys) {
        for (let p = page.parent; p; p = p.parent) {
            if (typeof p.openPage === "function") {
                p.openPage(key, rowTitle, byKeys);
                return;
            }
        }
    }

    /** All values of the console kind shown back to what a new install has. */
    function resetDefaults() {
        const kind = page.kind;
        // Resolution: 1 360p, 2 540p, 3 720p, 4 1080p. Bitrate 0: what the resolution uses by itself.
        Chiaki.settings["resolutionLocal" + kind] = kind === "PS5" ? 4 : 3;
        Chiaki.settings["resolutionRemote" + kind] = 3;
        Chiaki.settings["fpsLocal" + kind] = 60;
        Chiaki.settings["fpsRemote" + kind] = 60;
        Chiaki.settings["bitrateLocal" + kind] = 0;
        Chiaki.settings["bitrateRemote" + kind] = 0;
        if (kind === "PS5") {
            // Codec: 0 H264, 1 H265, 2 H265 HDR
            Chiaki.settings.codecLocalPS5 = 1;
            Chiaki.settings.codecRemotePS5 = 1;
        }
    }

    // ------------------------------------------------------------------ the choices

    component ResolutionChoice: C.Segmented {
        property bool home
        property string kind
        readonly property string setting: "resolution" + (home ? "Local" : "Remote") + kind
        Accessible.name: home ? qsTr("Resolution at home") : qsTr("Resolution away")
        // Chiaki.settings.resolution…: 1 360p, 2 540p, 3 720p, 4 1080p
        model: ["360p", "540p", "720p", "1080p"]
        currentIndex: Chiaki.settings[setting] - 1
        onActivated: (index) => {
            if (index + 1 === Chiaki.settings[setting])
                return;
            Chiaki.settings[setting] = index + 1;
            // The bitrate goes back to what the new resolution uses by itself
            Chiaki.settings["bitrate" + (home ? "Local" : "Remote") + kind] = 0;
        }
    }

    component FrameRateChoice: C.Segmented {
        property bool home
        property string kind
        readonly property string setting: "fps" + (home ? "Local" : "Remote") + kind
        Accessible.name: home ? qsTr("Frame rate at home") : qsTr("Frame rate away")
        model: [qsTr("30 fps"), qsTr("60 fps")]
        currentIndex: Chiaki.settings[setting] / 30 - 1
        onActivated: (index) => Chiaki.settings[setting] = (index + 1) * 30
    }

    component BitrateChoice: RowLayout {
        id: bitrate
        property bool home
        property string kind
        readonly property string setting: "bitrate" + (home ? "Local" : "Remote") + kind
        readonly property int resolution: Chiaki.settings["resolution" + (home ? "Local" : "Remote") + kind]
        /** Mbit/s the console uses at this resolution when no bitrate is set (0). */
        readonly property int standard: [2, 6, 10, 15][resolution - 1] || 0

        Layout.fillWidth: true
        spacing: 12

        C.ValueSlider {
            id: slider
            Layout.fillWidth: true
            Layout.minimumWidth: 100
            Layout.maximumWidth: 220
            implicitWidth: 150
            Accessible.name: bitrate.home ? qsTr("Bitrate at home") : qsTr("Bitrate away")
            // In Mbit/s; the setting is in kbit/s
            from: 2
            to: 100
            stepSize: 1
            value: Chiaki.settings[bitrate.setting] > 0 ? Chiaki.settings[bitrate.setting] / 1000 : bitrate.standard
            onMoved: Chiaki.settings[bitrate.setting] = Math.round(value) * 1000
        }

        ColumnLayout {
            Layout.preferredWidth: Math.max(widest.advanceWidth, standardText.implicitWidth)
            spacing: 0

            TextMetrics {
                id: widest
                font: valueText.font
                text: qsTr("%1 Mbit/s").arg(100)
            }

            Text {
                id: valueText
                text: qsTr("%1 Mbit/s").arg(Math.round(slider.value))
                color: C.Theme.text
                font.pixelSize: 14
                font.weight: Font.ExtraBold
                font.features: { "tnum": 1 }
            }

            Text {
                id: standardText
                visible: bitrate.standard > 0
                text: qsTr("%1 alone: %2").arg(["360p", "540p", "720p", "1080p"][bitrate.resolution - 1]).arg(bitrate.standard)
                color: C.Theme.textDim
                font.pixelSize: 12
            }
        }
    }

    component CodecChoice: C.Segmented {
        property bool home
        readonly property string setting: home ? "codecLocalPS5" : "codecRemotePS5"
        Accessible.name: home ? qsTr("Video codec at home") : qsTr("Video codec away")
        // Chiaki.settings.codec…PS5: 0 H264, 1 H265, 2 H265 HDR; no HDR with the OpenGL renderer
        model: Chiaki.settings.rendererBackend === 1 ? ["H264", "H265"] : ["H264", "H265", qsTr("H265 HDR")]
        currentIndex: Chiaki.settings[setting]
        onActivated: (index) => Chiaki.settings[setting] = index
    }

    // ------------------------------------------------------------------ the table

    /** The tile of a column: a house for at home, the globe for away. */
    component ColumnTile: Rectangle {
        property bool home
        property int size: 32
        implicitWidth: size
        implicitHeight: size
        radius: Math.round(size * 0.31)
        color: home ? Qt.rgba(0, 242 / 255, 1, 0.12) : Qt.rgba(140 / 255, 82 / 255, 1, 0.18)

        C.Icon {
            anchors.centerIn: parent
            iconName: parent.home ? "home" : "globe"
            size: Math.round(parent.size * 0.53)
            color: parent.home ? "#9ef7ff" : "#cdb6ff"
        }
    }

    /** The head of a column: its tile, its name, and where it applies. */
    component ColumnHead: RowLayout {
        id: head
        property bool home
        spacing: 10

        ColumnTile {
            home: head.home
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 1

            Text {
                Layout.fillWidth: true
                text: head.home ? qsTr("At home") : qsTr("Away")
                elide: Text.ElideRight
                color: C.Theme.text
                font.pixelSize: 15
                font.weight: Font.ExtraBold
            }

            Text {
                Layout.fillWidth: true
                text: head.home ? qsTr("Same network as the console") : qsTr("Over the internet, also through PSN")
                wrapMode: Text.WordWrap
                color: C.Theme.textDim
                font.pixelSize: 12
            }
        }
    }

    /** Before a choice when At home and Away are one under the other. */
    component ColumnCaption: Row {
        id: caption
        property bool home
        spacing: 8

        ColumnTile {
            anchors.verticalCenter: parent.verticalCenter
            home: caption.home
            size: 24
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: caption.home ? qsTr("At home") : qsTr("Away")
            color: C.Theme.textSoft
            font.pixelSize: 13
            font.weight: Font.Bold
        }
    }

    /** One setting of the table: its name, then its choice at home and its choice away. */
    component QualityLine: Item {
        id: line
        property string title
        property string subtitle
        property string keywords
        readonly property bool isSettingRow: true
        /** Side by side under the column heads; else one under the other, each with its caption. */
        property bool wide: true
        property int labelWidth: 170
        property alias home: homeBox.data
        property alias away: awayBox.data
        /** A link under the subtitle, when not empty. */
        property string linkText
        signal linkClicked(bool byKeys)

        readonly property real columnWidth: (width - labelWidth) / 2
        readonly property int captionWidth: 112
        /** Narrow and no room beside the caption either: the choice goes under it. */
        readonly property bool captionsOver: width - 36 - captionWidth < Math.max(homeBox.implicitWidth, awayBox.implicitWidth)
        readonly property real captionHeight: homeCaption.implicitHeight
        readonly property real homeSlotHeight: captionsOver ? captionHeight + 6 + homeBox.implicitHeight
                                                            : Math.max(captionHeight, homeBox.implicitHeight)
        readonly property real awaySlotHeight: captionsOver ? captionHeight + 6 + awayBox.implicitHeight
                                                            : Math.max(captionHeight, awayBox.implicitHeight)
        readonly property real homeSlotY: 12 + texts.implicitHeight + 12
        readonly property real awaySlotY: homeSlotY + homeSlotHeight + 10

        function inside(item, ancestor) {
            for (let p = item; p; p = p.parent) {
                if (p === ancestor)
                    return true;
            }
            return false;
        }

        Layout.fillWidth: true
        implicitHeight: wide ? Math.max(texts.implicitHeight, homeBox.implicitHeight, awayBox.implicitHeight) + 24
                             : awaySlotY + awaySlotHeight + 14

        // The lines between the rows and the columns
        Rectangle {
            width: parent.width
            height: 1
            color: Qt.rgba(1, 1, 1, 0.06)
        }

        Rectangle {
            visible: line.wide
            x: line.labelWidth
            width: 1
            height: parent.height
            color: Qt.rgba(1, 1, 1, 0.06)
        }

        Rectangle {
            visible: line.wide
            x: line.labelWidth + Math.round(line.columnWidth)
            width: 1
            height: parent.height
            color: Qt.rgba(1, 1, 1, 0.06)
        }

        ColumnLayout {
            id: texts
            x: 18
            y: line.wide ? Math.round((line.height - implicitHeight) / 2) : 12
            width: (line.wide ? line.labelWidth : line.width) - 36
            spacing: 2

            Text {
                Layout.fillWidth: true
                text: line.title
                wrapMode: Text.WordWrap
                color: C.Theme.text
                font.pixelSize: 15
                font.weight: Font.ExtraBold
            }

            Text {
                Layout.fillWidth: true
                visible: text.length > 0
                text: line.subtitle
                wrapMode: Text.WordWrap
                color: C.Theme.textDim
                font.pixelSize: 12
            }

            C.TextLink {
                id: link
                visible: line.linkText.length > 0
                Layout.topMargin: 2
                text: line.linkText
                font.pixelSize: 12
                onClicked: line.linkClicked(link.visualFocus)
            }
        }

        ColumnCaption {
            id: homeCaption
            visible: !line.wide
            home: true
            x: 18
            y: line.captionsOver ? line.homeSlotY : line.homeSlotY + Math.round((line.homeSlotHeight - height) / 2)
        }

        ColumnCaption {
            visible: !line.wide
            home: false
            x: 18
            y: line.captionsOver ? line.awaySlotY : line.awaySlotY + Math.round((line.awaySlotHeight - height) / 2)
        }

        RowLayout {
            id: homeBox
            x: line.wide ? line.labelWidth + 18 : line.captionsOver ? 18 : 18 + line.captionWidth
            y: line.wide ? Math.round((line.height - implicitHeight) / 2)
             : line.captionsOver ? line.homeSlotY + line.captionHeight + 6
             : line.homeSlotY + Math.round((line.homeSlotHeight - implicitHeight) / 2)
            width: line.wide ? line.columnWidth - 36 : line.width - 18 - x
            spacing: 8

            // Right from the last choice at home goes on to Away
            Keys.onPressed: (event) => {
                if (!line.wide || event.key !== Qt.Key_Right)
                    return;
                const next = awayBox.nextItemInFocusChain(true);
                if (next && line.inside(next, awayBox)) {
                    next.forceActiveFocus(Qt.TabFocusReason);
                    event.accepted = true;
                }
            }
        }

        RowLayout {
            id: awayBox
            x: line.wide ? line.labelWidth + Math.round(line.columnWidth) + 18 : homeBox.x
            y: line.wide ? Math.round((line.height - implicitHeight) / 2)
             : line.captionsOver ? line.awaySlotY + line.captionHeight + 6
             : line.awaySlotY + Math.round((line.awaySlotHeight - implicitHeight) / 2)
            width: line.wide ? line.columnWidth - 36 : line.width - 18 - x
            spacing: 8

            // Left from the first choice away goes back to At home
            Keys.onPressed: (event) => {
                if (!line.wide || event.key !== Qt.Key_Left)
                    return;
                const previous = awayBox.nextItemInFocusChain(false);
                if (previous && line.inside(previous, homeBox)) {
                    previous.forceActiveFocus(Qt.TabFocusReason);
                    event.accepted = true;
                }
            }
        }
    }

    // ------------------------------------------------------------------ the page

    Item {
        readonly property bool isSettingRow: true
        readonly property string title: qsTr("Values for")
        readonly property string subtitle: qsTr("The kind of console the values below are for.")
        readonly property string keywords: qsTr("PS5 PS4 console kind")

        Layout.fillWidth: true
        implicitHeight: consoleRow.implicitHeight

        RowLayout {
            id: consoleRow
            spacing: 10

            Text {
                text: qsTr("Values for")
                color: C.Theme.textSoft
                font.pixelSize: 13
                font.weight: Font.Bold
            }

            C.Segmented {
                Accessible.name: qsTr("Values for")
                model: ["PS5", "PS4"]
                currentIndex: page.ps5 ? 0 : 1
                onActivated: (index) => page.ps5 = index === 0
            }
        }
    }

    C.SettingsGroup {
        // The heads of the two columns, then one line for each setting
        Item {
            id: heads
            readonly property real columnWidth: page.wide ? (width - page.labelWidth) / 2 : width / 2

            Layout.fillWidth: true
            implicitHeight: Math.max(homeHead.implicitHeight, awayHead.implicitHeight) + 28

            Rectangle {
                visible: page.wide
                x: page.labelWidth
                width: 1
                height: parent.height
                color: Qt.rgba(1, 1, 1, 0.06)
            }

            Rectangle {
                visible: page.wide
                x: page.labelWidth + Math.round(heads.columnWidth)
                width: 1
                height: parent.height
                color: Qt.rgba(1, 1, 1, 0.06)
            }

            ColumnHead {
                id: homeHead
                home: true
                x: (page.wide ? page.labelWidth : 0) + 18
                y: Math.round((heads.height - height) / 2)
                width: heads.columnWidth - 36
            }

            ColumnHead {
                id: awayHead
                home: false
                x: (page.wide ? page.labelWidth : 0) + Math.round(heads.columnWidth) + 18
                y: Math.round((heads.height - height) / 2)
                width: heads.columnWidth - 36
            }
        }

        QualityLine {
            title: qsTr("Resolution")
            subtitle: page.ps5 ? qsTr("1080p: PS5 and PS4 Pro") : qsTr("1080p only on a PS4 Pro")
            keywords: qsTr("360p 540p 720p 1080p picture size")
            wide: page.wide
            labelWidth: page.labelWidth
            home: ResolutionChoice {
                id: resolutionHome
                home: true
                kind: page.kind
            }
            away: ResolutionChoice {
                home: false
                kind: page.kind
            }
        }

        QualityLine {
            title: qsTr("Frame rate")
            subtitle: qsTr("Pictures per second")
            keywords: qsTr("fps 30 60 smooth")
            wide: page.wide
            labelWidth: page.labelWidth
            home: FrameRateChoice {
                home: true
                kind: page.kind
            }
            away: FrameRateChoice {
                home: false
                kind: page.kind
            }
        }

        QualityLine {
            title: qsTr("Bitrate")
            subtitle: qsTr("More is sharper, needs a steadier line")
            keywords: qsTr("mbit mbps kbps bandwidth data rate shaky")
            wide: page.wide
            labelWidth: page.labelWidth
            home: BitrateChoice {
                id: bitrateHome
                home: true
                kind: page.kind
            }
            away: BitrateChoice {
                home: false
                kind: page.kind
            }
        }

        QualityLine {
            // The PS4 always sends H264
            visible: page.ps5
            title: qsTr("Video codec")
            subtitle: qsTr("HDR needs the Vulkan renderer")
            keywords: qsTr("h264 h265 hevc avc hdr codec")
            wide: page.wide
            labelWidth: page.labelWidth
            // With OpenGL, HDR is not offered: the way to the renderer
            linkText: Chiaki.settings.rendererBackend === 1 ? qsTr("Renderer…") : ""
            onLinkClicked: (byKeys) => page.openPage("picture", qsTr("Renderer"), byKeys)
            home: CodecChoice {
                id: codecHome
                home: true
            }
            away: CodecChoice {
                home: false
            }
        }
    }

    Item {
        readonly property bool isSettingRow: true
        readonly property string title: qsTr("Back to the defaults")
        readonly property string subtitle: qsTr("For the kind of console shown, at home and away.")
        readonly property string keywords: qsTr("reset default standard")

        Layout.fillWidth: true
        implicitHeight: footer.implicitHeight

        RowLayout {
            id: footer
            width: parent.width
            spacing: 12

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                C.Icon {
                    Layout.alignment: Qt.AlignTop
                    Layout.topMargin: 1
                    iconName: "info"
                    size: 16
                    color: C.Theme.headingSoft
                }

                Text {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    text: qsTr("Friends who join get what the console sends you. A shaky picture for everybody? Lower the bitrate for Away first. A browser's connection slows down when packets get lost: if the picture stutters for a friend in a web browser, try 720p and 6 to 8 Mbit/s.")
                    wrapMode: Text.WordWrap
                    lineHeight: 1.1
                    color: C.Theme.textSoft
                    font.pixelSize: 13
                }
            }

            C.PillButton {
                small: true
                text: qsTr("Back to the defaults")
                onClicked: root.showConfirmDialog(qsTr("Back to the defaults"),
                    page.ps5 ? qsTr("Set the PS5 values for at home and away back to the defaults?")
                             : qsTr("Set the PS4 values for at home and away back to the defaults?"),
                    () => page.resetDefaults())
            }
        }
    }
}
