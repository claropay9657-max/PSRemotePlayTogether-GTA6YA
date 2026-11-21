import QtQuick
import QtQuick.Layouts
import QtQuick.Effects

import org.streetpea.chiaking

import "../controls" as C

// A stream scene, 1920 x 1080, drawn into the app's own live stream in place of the
// game picture (StreamView's live overlay). The same scenes exist as pages of the
// overlay server for OBS (res/overlay/scene.js); both follow the designs and are
// filled from Chiaki.scenes (livescenes.h).
Item {
    id: root

    /** "starting", "lobby", "brb", "play", "difficulties" or "ending". */
    property string scene: ""
    readonly property var d: Chiaki.scenes ? Chiaki.scenes.data : ({})
    readonly property var texts: d.texts || ({})
    readonly property var players: d.players || []

    width: 1920
    height: 1080

    FontLoader {
        id: jakarta
        source: "qrc:/fonts/PlusJakartaSans.ttf"
    }
    readonly property string family: jakarta.status === FontLoader.Ready ? jakarta.font.family : ""

    readonly property color ink: "#f3f1ff"
    readonly property color soft: "#b9b4d6"
    readonly property color dim: "#8f8aad"
    readonly property color label: "#a9b0ff"
    readonly property color good: "#34d399"
    readonly property color wait: "#f5b942"
    readonly property color bad: "#f0466e"

    /** "A", "A and B", "A, B and C" */
    function names(list) {
        if (list.length <= 1)
            return list.join("");
        return qsTr("%1 and %2").arg(list.slice(0, -1).join(", ")).arg(list[list.length - 1]);
    }

    function joinWhat() {
        if (d.joinMode === "web")
            return qsTr("Join from your browser, no download: ");
        if (d.joinMode === "app")
            return qsTr("Get the app and find the room: ");
        return qsTr("Ask in the chat for an invite.");
    }
    function joinWhere() {
        return d.joinMode === "invite" ? "" : (d.joinShown || "");
    }

    // ------------------------------------------------------------ building blocks

    component T: Text {
        color: root.ink
        font.family: root.family
        font.weight: Font.Bold
        textFormat: Text.PlainText
        elide: Text.ElideRight
    }

    /** A heading in the brand gradient; with `wrapWidth` it breaks into lines. */
    component Grad: Item {
        id: grad
        property alias text: gradLabel.text
        property alias font: gradLabel.font
        property real wrapWidth: 0
        implicitWidth: gradLabel.width
        implicitHeight: gradLabel.height

        Text {
            id: gradLabel
            width: grad.wrapWidth > 0 ? Math.min(grad.wrapWidth, implicitWidth) : implicitWidth
            wrapMode: grad.wrapWidth > 0 ? Text.WordWrap : Text.NoWrap
            textFormat: Text.PlainText
            lineHeight: 0.98
            color: "white"
            font.family: root.family
            font.weight: Font.ExtraBold
            visible: false
            layer.enabled: true
        }

        Rectangle {
            id: gradFill
            anchors.fill: gradLabel
            visible: false
            layer.enabled: true
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: "#a77bff" }
                GradientStop { position: 1.0; color: "#f0469b" }
            }
        }

        MultiEffect {
            anchors.fill: gradLabel
            source: gradFill
            maskEnabled: true
            maskSource: gradLabel
        }
    }

    function players_text(n) {
        return n === 1 ? qsTr("1 player") : qsTr("%1 players").arg(n);
    }

    component Brand: Row {
        property int size: 56
        spacing: 20

        Image {
            anchors.verticalCenter: parent.verticalCenter
            width: parent.size
            height: parent.size
            sourceSize: Qt.size(parent.size * 2, parent.size * 2)
            source: "qrc:/icons/ui/app-icon.svg"
        }

        T {
            anchors.verticalCenter: parent.verticalCenter
            text: "PSRemotePlayTogether"
            color: "#cdb6ff"
            font.pixelSize: parent.size >= 64 ? 30 : 26
            font.weight: Font.ExtraBold
        }
    }

    component Pill: Rectangle {
        property string tone: "neutral"
        property alias text: pillText.text
        property string iconName: ""
        implicitWidth: pillRow.implicitWidth + 48
        implicitHeight: 52
        radius: height / 2
        color: tone === "good" ? Qt.rgba(52 / 255, 211 / 255, 153 / 255, 0.12)
             : tone === "wait" ? Qt.rgba(245 / 255, 185 / 255, 66 / 255, 0.14) : Qt.rgba(1, 1, 1, 0.06)
        border.width: 1
        border.color: tone === "good" ? Qt.rgba(52 / 255, 211 / 255, 153 / 255, 0.45)
                    : tone === "wait" ? Qt.rgba(245 / 255, 185 / 255, 66 / 255, 0.55) : Qt.rgba(1, 1, 1, 0.14)

        Row {
            id: pillRow
            anchors.centerIn: parent
            spacing: 12

            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                visible: !parent.parent.iconName
                width: 12
                height: 12
                radius: 6
                color: parent.parent.tone === "good" ? root.good : root.wait
            }

            C.Icon {
                anchors.verticalCenter: parent.verticalCenter
                visible: !!parent.parent.iconName
                iconName: parent.parent.iconName
                size: 24
                color: parent.parent.tone === "good" ? root.good : root.wait
            }

            T {
                id: pillText
                anchors.verticalCenter: parent.verticalCenter
                font.pixelSize: 22
                color: parent.parent.tone === "good" ? "#d7fbe9" : parent.parent.tone === "wait" ? "#fde4a8" : "#e7e4fa"
            }
        }
    }

    component Card: Rectangle {
        property bool brand: false
        radius: 28
        color: brand ? Qt.rgba(122 / 255, 60 / 255, 245 / 255, 0.16) : Qt.rgba(21 / 255, 18 / 255, 43 / 255, 0.88)
        border.width: 1
        border.color: brand ? Qt.rgba(140 / 255, 82 / 255, 255 / 255, 0.55) : Qt.rgba(1, 1, 1, 0.10)
    }

    component Tag: Rectangle {
        property alias text: tagText.text
        property bool live: false
        implicitWidth: tagText.implicitWidth + 28
        implicitHeight: 34
        radius: 14
        color: live ? "#e91916" : Qt.rgba(122 / 255, 60 / 255, 245 / 255, 0.9)

        T {
            id: tagText
            anchors.centerIn: parent
            font.pixelSize: 18
            font.weight: Font.ExtraBold
            font.letterSpacing: 1.4
        }
    }

    /** Overlapping pictures of the players. */
    component Stack: Row {
        property int size: 56
        property int max: 5
        property color edge: "#15122b"
        spacing: -Math.round(size / 4)

        Repeater {
            model: root.players.slice(0, parent.max)

            Rectangle {
                required property var modelData
                width: parent.size + 6
                height: width
                radius: width / 2
                color: parent.edge

                C.Avatar {
                    anchors.centerIn: parent
                    width: parent.width - 6
                    height: width
                    source: modelData.avatarUrl || ""
                    name: modelData.name || ""
                }
            }
        }
    }

    component Gradient2: Rectangle {
        // The scene's ground with the two coloured glows of its design
        property real ax: 0
        property real ay: 0
        property real bx: 1
        property real by: 1
        property color ca: Qt.rgba(140 / 255, 82 / 255, 255 / 255, 0.30)
        property color cb: Qt.rgba(240 / 255, 70 / 255, 155 / 255, 0.20)
        anchors.fill: parent
        color: "#0c0a1d"

        Canvas {
            anchors.fill: parent
            onPaint: {
                const ctx = getContext("2d");
                ctx.clearRect(0, 0, width, height);
                const glow = (x, y, rx, c) => {
                    const g = ctx.createRadialGradient(x, y, 0, x, y, rx);
                    g.addColorStop(0, c);
                    g.addColorStop(0.7, "rgba(0,0,0,0)");
                    ctx.fillStyle = g;
                    ctx.fillRect(0, 0, width, height);
                };
                glow(parent.ax * width, parent.ay * height, 1100, parent.ca);
                glow(parent.bx * width, parent.by * height, 950, parent.cb);
            }
        }
    }

    // ------------------------------------------------------------ the scenes

    Loader {
        anchors.fill: parent
        sourceComponent: root.scene === "starting" ? starting
                       : root.scene === "lobby" ? lobby
                       : root.scene === "brb" ? brb
                       : root.scene === "play" ? play
                       : root.scene === "difficulties" ? difficulties
                       : root.scene === "ending" ? ending : null
    }

    Component {
        id: starting

        Item {
            Gradient2 {}

            Brand {
                x: 104
                y: 88
                size: 64
            }

            Pill {
                x: parent.width - 104 - width
                y: 94
                text: qsTr("Stream starts soon")
            }

            ColumnLayout {
                x: 104
                width: 1920 - 104 - 104 - 600 - 96
                anchors.bottom: progress.top
                anchors.bottomMargin: 90
                spacing: 20

                T {
                    text: qsTr("Live in")
                    color: root.label
                    font.pixelSize: 28
                    font.letterSpacing: 6
                    font.capitalization: Font.AllUppercase
                }

                T {
                    text: root.d.countdown || ""
                    font.pixelSize: 260
                    font.weight: Font.ExtraBold
                    font.letterSpacing: -10
                    font.features: { "tnum": 1 }
                    Layout.preferredHeight: 240
                    verticalAlignment: Text.AlignBottom
                }

                Grad {
                    Layout.topMargin: 24
                    text: root.texts.starting_title || ""
                    font.pixelSize: 88
                }

                T {
                    visible: text.length > 0
                    Layout.fillWidth: true
                    text: root.texts.starting_line || ""
                    color: root.soft
                    font.pixelSize: 34
                    font.weight: Font.DemiBold
                    wrapMode: Text.WordWrap
                    maximumLineCount: 2
                }
            }

            ColumnLayout {
                x: 1920 - 104 - 600
                width: 600
                anchors.bottom: progress.top
                anchors.bottomMargin: 90
                spacing: 20

                Card {
                    Layout.fillWidth: true
                    Layout.preferredHeight: roomColumn.implicitHeight + 64

                    ColumnLayout {
                        id: roomColumn
                        x: 32
                        y: 32
                        width: parent.width - 64
                        spacing: 20

                        RowLayout {
                            Layout.fillWidth: true

                            T {
                                Layout.fillWidth: true
                                text: qsTr("In the room")
                                color: root.label
                                font.pixelSize: 22
                                font.weight: Font.ExtraBold
                                font.letterSpacing: 3.5
                                font.capitalization: Font.AllUppercase
                            }

                            T {
                                text: root.d.maxPlayers > 0 ? qsTr("%1 / %2 players").arg(root.d.playerCount).arg(root.d.maxPlayers)
                                                            : root.players_text(root.d.playerCount || 0)
                                color: root.soft
                                font.pixelSize: 24
                            }
                        }

                        Repeater {
                            model: root.players.slice(0, 6)

                            RowLayout {
                                required property var modelData
                                Layout.fillWidth: true
                                Layout.preferredHeight: 64
                                spacing: 18

                                C.Avatar {
                                    Layout.preferredWidth: 60
                                    Layout.preferredHeight: 60
                                    source: modelData.avatarUrl || ""
                                    name: modelData.name || ""
                                    ringWidth: 2
                                    ringColor: modelData.talking ? root.good : Qt.rgba(1, 1, 1, 0.18)
                                }

                                T {
                                    Layout.fillWidth: true
                                    text: modelData.name
                                    font.pixelSize: 28
                                }

                                Tag {
                                    visible: !!modelData.live || !!modelData.host
                                    live: !!modelData.live
                                    text: modelData.live ? qsTr("LIVE") : qsTr("HOST")
                                }
                            }
                        }
                    }
                }

                Card {
                    brand: true
                    Layout.fillWidth: true
                    Layout.preferredHeight: alongColumn.implicitHeight + 56

                    ColumnLayout {
                        id: alongColumn
                        x: 32
                        y: 28
                        width: parent.width - 64
                        spacing: 8

                        T {
                            text: qsTr("Want to play along?")
                            font.pixelSize: 24
                            font.weight: Font.ExtraBold
                        }

                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            textFormat: Text.StyledText
                            font.family: root.family
                            font.pixelSize: 22
                            font.weight: Font.DemiBold
                            color: "#cfc9ec"
                            // The address is ours, never from a player: the only markup is the colour
                            text: root.joinWhat() + (root.joinWhere() ? "<font color=\"#ffffff\">" + root.joinWhere() + "</font>" : "")
                        }
                    }
                }
            }

            RowLayout {
                id: progress
                x: 104
                width: 1920 - 208
                y: 1080 - 88 - height
                spacing: 24

                Rectangle {
                    Layout.fillWidth: true
                    height: 8
                    radius: 4
                    color: Qt.rgba(1, 1, 1, 0.08)

                    Rectangle {
                        width: parent.width * (root.d.progress || 0)
                        height: 8
                        radius: 4
                        gradient: Gradient {
                            orientation: Gradient.Horizontal
                            GradientStop { position: 0.0; color: "#8c52ff" }
                            GradientStop { position: 1.0; color: "#f0469b" }
                        }
                    }
                }

                T {
                    visible: text.length > 0
                    text: root.d.channelLine || ""
                    color: root.dim
                    font.pixelSize: 22
                }
            }
        }
    }

    Component {
        id: lobby

        Item {
            Gradient2 {
                ca: Qt.rgba(140 / 255, 82 / 255, 255 / 255, 0.26)
                cb: Qt.rgba(240 / 255, 70 / 255, 155 / 255, 0.16)
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.leftMargin: 88
                anchors.rightMargin: 88
                anchors.topMargin: 64
                anchors.bottomMargin: 64
                spacing: 40

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 20

                    Image {
                        Layout.preferredWidth: 56
                        Layout.preferredHeight: 56
                        sourceSize: Qt.size(112, 112)
                        source: "qrc:/icons/ui/app-icon.svg"
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2

                        T {
                            Layout.fillWidth: true
                            text: root.texts.lobby_title || ""
                            font.pixelSize: 40
                            font.weight: Font.ExtraBold
                        }

                        T {
                            Layout.fillWidth: true
                            visible: text.length > 0
                            text: [root.d.roomTitle || "", root.texts.lobby_line || ""].filter((x) => x).join(" · ")
                            color: root.soft
                            font.pixelSize: 22
                            font.weight: Font.DemiBold
                        }
                    }

                    Pill {
                        visible: (root.d.inVoice || 0) > 0
                        tone: "good"
                        text: qsTr("%1 in voice chat").arg(root.d.inVoice || 0)
                    }
                }

                GridLayout {
                    id: tiles
                    readonly property int count: Math.max(1, Math.min(9, root.players.length))
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    columns: count === 1 ? 1 : (count <= 4 ? 2 : 3)
                    columnSpacing: 28
                    rowSpacing: 28

                    Repeater {
                        model: root.players.slice(0, 9)

                        Rectangle {
                            id: tile
                            required property var modelData
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.preferredWidth: 100
                            Layout.preferredHeight: 100
                            radius: 28
                            color: "#15122b"
                            clip: true
                            border.width: modelData.talking ? 4 : 1
                            border.color: modelData.talking ? root.good : Qt.rgba(1, 1, 1, 0.10)

                            Image {
                                anchors.fill: parent
                                anchors.margins: tile.border.width
                                visible: !!tile.modelData.camUrl
                                cache: false
                                fillMode: Image.PreserveAspectCrop
                                source: tile.modelData.camUrl || ""
                            }

                            C.Avatar {
                                anchors.centerIn: parent
                                visible: !tile.modelData.camUrl
                                width: Math.min(200, parent.height * 0.5)
                                height: width
                                source: tile.modelData.avatarUrl || ""
                                name: tile.modelData.name || ""
                                ringWidth: 4
                                ringColor: Qt.rgba(1, 1, 1, 0.14)
                            }

                            Rectangle {
                                x: 24
                                y: parent.height - 22 - height
                                height: 52
                                width: plateRow.implicitWidth + 36
                                radius: 26
                                color: Qt.rgba(8 / 255, 6 / 255, 20 / 255, 0.72)

                                Row {
                                    id: plateRow
                                    anchors.verticalCenter: parent.verticalCenter
                                    x: 16
                                    spacing: 12

                                    C.Icon {
                                        anchors.verticalCenter: parent.verticalCenter
                                        iconName: tile.modelData.muted ? "mic-off" : "mic"
                                        size: 22
                                        color: tile.modelData.talking ? root.good : (tile.modelData.muted ? root.bad : "#e7e4fa")
                                    }

                                    T {
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: tile.modelData.name
                                        font.pixelSize: 26
                                    }

                                    T {
                                        anchors.verticalCenter: parent.verticalCenter
                                        visible: !!tile.modelData.host || !!tile.modelData.live
                                        text: tile.modelData.live ? qsTr("LIVE") : qsTr("HOST")
                                        color: tile.modelData.live ? "#ff8a8a" : "#cdb6ff"
                                        font.pixelSize: 16
                                        font.weight: Font.ExtraBold
                                        font.letterSpacing: 1.6
                                    }
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 72
                    radius: 22
                    color: Qt.rgba(21 / 255, 18 / 255, 43 / 255, 0.88)
                    border.width: 1
                    border.color: Qt.rgba(1, 1, 1, 0.10)

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 28
                        anchors.rightMargin: 28
                        spacing: 20

                        Image {
                            visible: !!root.d.gamePicture
                            Layout.preferredWidth: 96
                            Layout.preferredHeight: 54
                            fillMode: Image.PreserveAspectCrop
                            source: root.d.gamePicture || ""
                        }

                        Text {
                            visible: !!root.d.game
                            textFormat: Text.StyledText
                            font.family: root.family
                            font.pixelSize: 24
                            font.weight: Font.DemiBold
                            color: root.soft
                            text: qsTr("Next up: %1").arg("<font color=\"#f3f1ff\"><b>" + root.escaped(root.d.game || "") + "</b></font>")
                        }

                        Item { Layout.fillWidth: true }

                        Text {
                            textFormat: Text.StyledText
                            font.family: root.family
                            font.pixelSize: 22
                            font.weight: Font.DemiBold
                            color: root.soft
                            text: root.joinWhat() + (root.joinWhere() ? "<font color=\"#ffffff\">" + root.joinWhere() + "</font>" : "")
                        }
                    }
                }
            }
        }
    }

    /** For the few places that colour part of a line: a game's name as text, never markup. */
    function escaped(text) {
        return String(text).replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;").replace(/"/g, "&quot;");
    }

    Component {
        id: brb

        Item {
            Rectangle {
                anchors.fill: parent
                color: "#0c0a1d"
            }

            Image {
                id: backdrop
                x: -80
                y: -80
                width: 2080
                height: 1240
                fillMode: Image.PreserveAspectCrop
                source: root.d.gamePicture || ""
                visible: false
            }

            MultiEffect {
                anchors.fill: backdrop
                source: backdrop
                visible: !!root.d.gamePicture
                blurEnabled: true
                blur: 1.0
                blurMax: 64
                brightness: -0.45
                saturation: 0.2
            }

            Canvas {
                anchors.fill: parent
                onPaint: {
                    const ctx = getContext("2d");
                    const g = ctx.createRadialGradient(width / 2, height * 0.46, 0, width / 2, height * 0.46, 1000);
                    g.addColorStop(0, "rgba(12,10,29,0.15)");
                    g.addColorStop(1, "rgba(12,10,29,0.78)");
                    ctx.fillStyle = g;
                    ctx.fillRect(0, 0, width, height);
                }
            }

            Brand {
                x: 104
                y: 88
            }

            ColumnLayout {
                anchors.centerIn: parent
                width: 1400
                spacing: 28

                Pill {
                    Layout.alignment: Qt.AlignHCenter
                    tone: "wait"
                    implicitHeight: 56
                    iconName: "clock"
                    text: qsTr("Back in about %1 min").arg(root.d.breakMinutes || 1)
                }

                Grad {
                    Layout.alignment: Qt.AlignHCenter
                    text: root.texts.brb_title || ""
                    font.pixelSize: 150
                }

                T {
                    visible: text.length > 0
                    Layout.alignment: Qt.AlignHCenter
                    Layout.maximumWidth: 1100
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    text: root.texts.brb_line || ""
                    color: "#cfc9ec"
                    font.pixelSize: 36
                    font.weight: Font.DemiBold
                }
            }

            Rectangle {
                visible: root.players.length > 0
                anchors.horizontalCenter: parent.horizontalCenter
                y: 1080 - 88 - height
                height: 88
                width: whoRow.implicitWidth + 52
                radius: 44
                color: Qt.rgba(21 / 255, 18 / 255, 43 / 255, 0.82)
                border.width: 1
                border.color: Qt.rgba(1, 1, 1, 0.12)

                Row {
                    id: whoRow
                    anchors.verticalCenter: parent.verticalCenter
                    x: 20
                    spacing: 22

                    Stack {
                        anchors.verticalCenter: parent.verticalCenter
                    }

                    T {
                        anchors.verticalCenter: parent.verticalCenter
                        text: root.names(root.players.map((p) => p.name))
                        font.pixelSize: 26
                    }

                    Rectangle {
                        visible: !!root.d.game
                        anchors.verticalCenter: parent.verticalCenter
                        width: 1
                        height: 36
                        color: Qt.rgba(1, 1, 1, 0.16)
                    }

                    T {
                        visible: !!root.d.game
                        anchors.verticalCenter: parent.verticalCenter
                        text: root.d.game || ""
                        color: root.soft
                        font.pixelSize: 24
                        font.weight: Font.DemiBold
                    }
                }
            }
        }
    }

    Component {
        id: play

        Item {
            Gradient2 {
                ax: 1
                ay: 0
                bx: 0
                by: 1
                cb: Qt.rgba(240 / 255, 70 / 255, 155 / 255, 0.18)
            }

            ColumnLayout {
                x: 104
                width: 1920 - 208 - 640 - 96
                anchors.verticalCenter: parent.verticalCenter
                spacing: 36

                Brand {}

                Grad {
                    wrapWidth: 1920 - 208 - 640 - 96
                    text: root.texts.play_title || ""
                    font.pixelSize: 112
                }

                T {
                    visible: text.length > 0
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: root.texts.play_line || ""
                    color: root.soft
                    font.pixelSize: 32
                    font.weight: Font.DemiBold
                }

                ColumnLayout {
                    Layout.topMargin: 12
                    spacing: 22

                    Repeater {
                        model: root.d.steps || []

                        RowLayout {
                            required property string modelData
                            required property int index
                            spacing: 24

                            Rectangle {
                                Layout.preferredWidth: 60
                                Layout.preferredHeight: 60
                                radius: 30
                                color: Qt.rgba(122 / 255, 60 / 255, 245 / 255, 0.28)
                                border.width: 1
                                border.color: Qt.rgba(140 / 255, 82 / 255, 255 / 255, 0.7)

                                T {
                                    anchors.centerIn: parent
                                    text: index + 1
                                    font.pixelSize: 28
                                    font.weight: Font.ExtraBold
                                }
                            }

                            T {
                                Layout.maximumWidth: 1920 - 208 - 640 - 96 - 84
                                wrapMode: Text.WordWrap
                                text: modelData
                                color: "#e7e4fa"
                                font.pixelSize: 32
                                font.weight: Font.DemiBold
                            }
                        }
                    }
                }
            }

            ColumnLayout {
                x: 1920 - 104 - 640
                width: 640
                anchors.verticalCenter: parent.verticalCenter
                spacing: 28

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 360 + cardBody.implicitHeight + 56
                    radius: 30
                    color: "#15122b"
                    border.width: 1
                    border.color: Qt.rgba(1, 1, 1, 0.12)
                    clip: true

                    Rectangle {
                        width: 640
                        height: 360
                        color: "#0f0c22"

                        Image {
                            anchors.fill: parent
                            fillMode: Image.PreserveAspectCrop
                            source: root.d.gamePicture || ""
                        }

                        Rectangle {
                            x: 20
                            y: 20
                            height: 40
                            width: liveRow.implicitWidth + 32
                            radius: 20
                            color: Qt.rgba(8 / 255, 6 / 255, 20 / 255, 0.78)

                            Row {
                                id: liveRow
                                anchors.centerIn: parent
                                spacing: 10

                                Rectangle {
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 10
                                    height: 10
                                    radius: 5
                                    color: root.bad
                                }

                                T {
                                    text: qsTr("Live")
                                    font.pixelSize: 20
                                    font.weight: Font.ExtraBold
                                }
                            }
                        }
                    }

                    ColumnLayout {
                        id: cardBody
                        x: 30
                        y: 360 + 26
                        width: 640 - 60
                        spacing: 16

                        T {
                            Layout.fillWidth: true
                            text: root.d.roomTitle || (root.d.hostName ? qsTr("%1's session").arg(root.d.hostName) : qsTr("Our session"))
                            font.pixelSize: 32
                            font.weight: Font.ExtraBold
                        }

                        T {
                            visible: !!root.d.game
                            Layout.fillWidth: true
                            text: root.d.hostName ? qsTr("%1 plays %2").arg(root.d.hostName).arg(root.d.game) : (root.d.game || "")
                            color: root.soft
                            font.pixelSize: 22
                            font.weight: Font.DemiBold
                        }

                        Flow {
                            Layout.fillWidth: true
                            spacing: 10

                            Repeater {
                                model: [
                                    { text: root.d.mixText || "", fill: Qt.rgba(0, 242 / 255, 1, 0.12), ink: "#b8f9ff", on: true },
                                    { text: root.d.policyText || "", fill: Qt.rgba(140 / 255, 82 / 255, 1, 0.2), ink: "#ddd0ff", on: !!root.d["public"] }, // "public" is a reserved word for Qt before 6.10: .public breaks the whole file there
                                    { text: qsTr("Browsers welcome"), fill: Qt.rgba(52 / 255, 211 / 255, 153 / 255, 0.14), ink: "#c6f6e2", on: !!root.d.browsers }
                                ].filter((c) => c.on && c.text)

                                Rectangle {
                                    required property var modelData
                                    height: 38
                                    width: chipText.implicitWidth + 32
                                    radius: 19
                                    color: modelData.fill

                                    T {
                                        id: chipText
                                        anchors.centerIn: parent
                                        text: parent.modelData.text
                                        color: parent.modelData.ink
                                        font.pixelSize: 19
                                    }
                                }
                            }
                        }

                        RowLayout {
                            spacing: 16

                            Stack {
                                size: 44
                            }

                            Text {
                                textFormat: Text.StyledText
                                font.family: root.family
                                font.pixelSize: 24
                                font.weight: Font.Bold
                                color: root.soft
                                text: (root.d.freePlaces >= 0
                                       ? "<font color=\"#34d399\">" + (root.d.freePlaces === 1 ? qsTr("1 place free") : qsTr("%1 places free").arg(root.d.freePlaces))
                                         + "</font> · " + qsTr("%1 / %2 players").arg(root.d.playerCount).arg(root.d.maxPlayers)
                                       : qsTr("%1 playing").arg(root.d.playerCount || 0))
                            }
                        }
                    }
                }

                Card {
                    brand: true
                    radius: 26
                    Layout.fillWidth: true
                    Layout.preferredHeight: 176

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 22
                        spacing: 24

                        Rectangle {
                            visible: (root.d.qrRows || []).length > 0
                            Layout.preferredWidth: 132
                            Layout.preferredHeight: 132
                            radius: 14
                            color: "#f3f1ff"

                            Canvas {
                                id: qr
                                anchors.fill: parent
                                anchors.margins: 8
                                property var rows: root.d.qrRows || []
                                onRowsChanged: requestPaint()
                                onPaint: {
                                    const ctx = getContext("2d");
                                    ctx.clearRect(0, 0, width, height);
                                    const n = rows.length;
                                    if (!n)
                                        return;
                                    const cell = width / n;
                                    ctx.fillStyle = "#15122b";
                                    for (let y = 0; y < n; ++y) {
                                        const row = rows[y];
                                        for (let x = 0; x < row.length; ++x) {
                                            if (row[x] === "1")
                                                ctx.fillRect(Math.floor(x * cell), Math.floor(y * cell), Math.ceil(cell), Math.ceil(cell));
                                        }
                                    }
                                }
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 6

                            T {
                                text: (root.d.qrRows || []).length > 0 ? qsTr("Scan, or open") : qsTr("Want in?")
                                color: "#cfc9ec"
                                font.pixelSize: 22
                            }

                            T {
                                Layout.fillWidth: true
                                wrapMode: Text.WrapAnywhere
                                maximumLineCount: 2
                                text: (root.d.qrRows || []).length > 0 ? (root.d.joinShown || "") : qsTr("Ask in the chat for an invite")
                                color: "#ffffff"
                                font.pixelSize: 26
                                font.weight: Font.ExtraBold
                            }
                        }
                    }
                }
            }
        }
    }

    Component {
        id: difficulties

        Item {
            Rectangle {
                anchors.fill: parent
                color: "#0a0918"
            }

            Gradient2 {
                color: "transparent"
                ax: 0.5
                ay: 0
                bx: 0.5
                by: 1
                ca: Qt.rgba(245 / 255, 185 / 255, 66 / 255, 0.10)
                cb: Qt.rgba(140 / 255, 82 / 255, 255 / 255, 0.18)
            }

            Brand {
                x: 104
                y: 88
            }

            ColumnLayout {
                anchors.centerIn: parent
                spacing: 56

                ColumnLayout {
                    Layout.alignment: Qt.AlignHCenter
                    spacing: 24

                    C.Icon {
                        Layout.alignment: Qt.AlignHCenter
                        iconName: "wifi"
                        size: 96
                        color: root.wait
                    }

                    T {
                        Layout.alignment: Qt.AlignHCenter
                        text: root.texts.difficulties_title || ""
                        font.pixelSize: 120
                        font.weight: Font.ExtraBold
                    }

                    T {
                        visible: text.length > 0
                        Layout.alignment: Qt.AlignHCenter
                        Layout.maximumWidth: 1100
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        text: root.texts.difficulties_line || ""
                        color: "#cfc9ec"
                        font.pixelSize: 36
                        font.weight: Font.DemiBold
                    }
                }

                Card {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: 920
                    Layout.preferredHeight: rowsColumn.implicitHeight + 24

                    Column {
                        id: rowsColumn
                        x: 36
                        y: 12
                        width: parent.width - 72

                        Repeater {
                            model: root.d.connection || []

                            Item {
                                required property var modelData
                                required property int index
                                width: rowsColumn.width
                                height: 84

                                Rectangle {
                                    visible: index > 0
                                    width: parent.width
                                    height: 1
                                    color: Qt.rgba(1, 1, 1, 0.08)
                                }

                                RowLayout {
                                    anchors.fill: parent
                                    spacing: 22

                                    Rectangle {
                                        Layout.preferredWidth: 16
                                        Layout.preferredHeight: 16
                                        radius: 8
                                        color: modelData.tone === "good" ? root.good : modelData.tone === "wait" ? root.wait
                                             : modelData.tone === "bad" ? root.bad : root.dim
                                    }

                                    T {
                                        Layout.fillWidth: true
                                        text: modelData.what
                                        font.pixelSize: 28
                                    }

                                    T {
                                        text: modelData.state
                                        font.pixelSize: 26
                                        color: modelData.tone === "good" ? "#c6f6e2" : modelData.tone === "wait" ? "#fde4a8"
                                             : modelData.tone === "bad" ? "#ffc2d1" : root.soft
                                    }
                                }
                            }
                        }
                    }
                }

                T {
                    visible: text.length > 0
                    Layout.alignment: Qt.AlignHCenter
                    text: root.texts.difficulties_footer || ""
                    color: root.dim
                    font.pixelSize: 24
                    font.weight: Font.DemiBold
                }
            }
        }
    }

    Component {
        id: ending

        Item {
            Gradient2 {
                ax: 0.5
                ay: -0.2
                bx: 0.5
                by: 1.2
                ca: Qt.rgba(140 / 255, 82 / 255, 255 / 255, 0.26)
                cb: Qt.rgba(240 / 255, 70 / 255, 155 / 255, 0.24)
            }

            Brand {
                x: 104
                y: 88
            }

            ColumnLayout {
                anchors.centerIn: parent
                spacing: 44

                Grad {
                    Layout.alignment: Qt.AlignHCenter
                    text: root.texts.ending_title || ""
                    font.pixelSize: 150
                }

                RowLayout {
                    id: stats
                    readonly property var list: {
                        let l = [];
                        if (root.d.game)
                            l.push({ label: qsTr("Played"), value: root.d.game });
                        l.push({ label: qsTr("Together for"), value: root.d.togetherText || "" });
                        l.push({ label: qsTr("Players"), value: root.players_text(root.d.seenCount || 0) });
                        return l;
                    }
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: 1360
                    spacing: 24

                    Repeater {
                        model: stats.list

                        Card {
                            required property var modelData
                            Layout.fillWidth: true
                            Layout.preferredWidth: 100
                            Layout.preferredHeight: 150
                            radius: 26

                            ColumnLayout {
                                x: 32
                                y: 30
                                width: parent.width - 64
                                spacing: 8

                                T {
                                    text: modelData.label
                                    color: root.label
                                    font.pixelSize: 20
                                    font.weight: Font.ExtraBold
                                    font.letterSpacing: 3.2
                                    font.capitalization: Font.AllUppercase
                                }

                                T {
                                    Layout.fillWidth: true
                                    text: modelData.value
                                    font.pixelSize: 46
                                    font.weight: Font.ExtraBold
                                }
                            }
                        }
                    }
                }

                RowLayout {
                    visible: (root.d.seenNames || []).length > 0
                    Layout.alignment: Qt.AlignHCenter
                    spacing: 18

                    Stack {
                        size: 60
                        max: 6
                        edge: "#0c0a1d"
                    }

                    T {
                        text: qsTr("Played by %1").arg(root.names(root.d.seenNames || []))
                        color: "#cfc9ec"
                        font.pixelSize: 30
                        font.weight: Font.DemiBold
                    }
                }
            }

            RowLayout {
                x: 104
                width: 1920 - 208
                y: 1080 - 88 - height
                spacing: 24

                Rectangle {
                    visible: !!root.d.nextLine
                    Layout.preferredHeight: 64
                    Layout.preferredWidth: nextRow.implicitWidth + 56
                    radius: 32
                    color: Qt.rgba(122 / 255, 60 / 255, 245 / 255, 0.18)
                    border.width: 1
                    border.color: Qt.rgba(140 / 255, 82 / 255, 255 / 255, 0.6)

                    Row {
                        id: nextRow
                        anchors.centerIn: parent
                        spacing: 14

                        C.Icon {
                            anchors.verticalCenter: parent.verticalCenter
                            iconName: "calendar"
                            size: 26
                            color: "#cdb6ff"
                        }

                        T {
                            anchors.verticalCenter: parent.verticalCenter
                            text: root.d.nextLine || ""
                            font.pixelSize: 26
                        }
                    }
                }

                Rectangle {
                    visible: !!root.d.channelLine
                    Layout.preferredHeight: 64
                    Layout.preferredWidth: followText.implicitWidth + 56
                    radius: 32
                    color: Qt.rgba(1, 1, 1, 0.06)
                    border.width: 1
                    border.color: Qt.rgba(1, 1, 1, 0.14)

                    T {
                        id: followText
                        anchors.centerIn: parent
                        text: qsTr("Follow %1").arg(root.d.channelLine || "")
                        font.pixelSize: 26
                    }
                }

                Item { Layout.fillWidth: true }

                T {
                    visible: root.d.joinMode === "web"
                    text: qsTr("Play together: %1").arg(root.d.joinShown || "")
                    color: root.dim
                    font.pixelSize: 22
                    font.weight: Font.DemiBold
                }
            }
        }
    }
}
