import QtQuick
import QtQuick.Controls as QQC

import "../controls" as C
import "Film.js" as F

// Plays the intro film (IntroFilm.qml) in whatever space it gets: the film's stage scaled to
// fit, its backdrop out to the edges, and around it what a film needs: chapters to jump to,
// pause, sound on and off, and a way out. Mouse, keys and controller (whose buttons arrive as keys):
//   Space or Return   pause and play; at the end, Return takes what has the ring
//   Left and Right    the chapter before and after; at the end, from one choice to the next
//   M                 sound off and on
//   Escape            leaves
FocusScope {
    id: player

    /** Runs by itself. False while paused, and for whoever sets `t` themselves. */
    property bool playing: true
    property alias t: film.t
    readonly property alias atEnd: film.atEnd
    /** The chapter the film is in. */
    readonly property int chapter: {
        const list = film.chapters;
        let now = 0;
        for (let i = 0; i < list.length; ++i) {
            if (film.t >= list[i].start)
                now = i;
        }
        return now;
    }
    /** On its way out: nothing can be pressed any more. */
    property bool leaving: false
    /**
     * What plays the film's sound in step with it (the app's IntroSound), or null for a film
     * without sound: it is told where the film is whenever that changes other than by running.
     */
    property var sound: null
    readonly property bool muted: !sound || sound.muted

    /** Left by Skip, Close or Escape. */
    signal closed()
    /** Left through one of the two doors at the end. */
    signal hostChosen()
    signal joinChosen()

    function run() {
        clock.stop();
        if (film.t >= film.duration)
            return;
        clock.from = film.t;
        clock.to = film.duration;
        clock.duration = Math.max(1, (film.duration - film.t) * 1000);
        // The sound first: the first time it has to be read, and the film waits for that
        if (sound)
            sound.play(film.t);
        clock.start();
    }

    function seek(to) {
        clock.stop();
        film.t = Math.max(0, Math.min(film.duration, to));
        film.endFocus = -1;
        if (playing)
            run();
        else if (sound)
            sound.stop();
    }

    function toggleSound() {
        if (!sound)
            return;
        sound.muted = !sound.muted;
        // Back on in the middle of the film: from where the film is. At the end only its last chord is left
        if (!sound.muted && (clock.running || film.t >= film.duration))
            sound.play(film.t);
    }

    function toggle() {
        if (film.t >= film.duration)
            return;
        playing = !playing;
    }

    /** The chapter after this one, or the one before (the start of this one first, as players do). */
    function step(forward) {
        const list = film.chapters;
        if (forward) {
            if (chapter < list.length - 1)
                seek(list[chapter + 1].start);
            return;
        }
        seek(film.t - list[chapter].start > 1.5 || chapter === 0 ? list[chapter].start : list[chapter - 1].start);
    }

    function again() {
        playing = true;
        seek(0);
    }

    /** Leaves, softly: `how` is "close", "host" or "join". */
    function leave(how) {
        if (leaving)
            return;
        leaving = true;
        clock.stop();
        if (sound)
            sound.stop();
        fade.how = how;
        fade.start();
    }

    onPlayingChanged: {
        if (playing) {
            run();
        } else {
            clock.stop();
            if (sound)
                sound.stop();
        }
    }
    Component.onCompleted: {
        if (playing)
            run();
    }
    // The end's first choice has the ring once the end is there
    onAtEndChanged: film.endFocus = atEnd ? 0 : -1

    Keys.onPressed: (event) => {
        if (leaving || event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier))
            return;
        switch (event.key) {
        case Qt.Key_Escape:
            leave("close");
            break;
        case Qt.Key_Space:
            if (!atEnd)
                toggle();
            break;
        case Qt.Key_M:
            toggleSound();
            break;
        case Qt.Key_Return:
        case Qt.Key_Enter:
            if (!atEnd)
                toggle();
            else if (film.endFocus === 1)
                leave("join");
            else if (film.endFocus === 2)
                again();
            else
                leave("host");
            break;
        case Qt.Key_Left:
        case Qt.Key_PageUp:
            if (atEnd && event.key === Qt.Key_Left && film.endFocus > 0)
                film.endFocus = film.endFocus === 2 ? 0 : film.endFocus - 1;
            else if (!atEnd || event.key === Qt.Key_PageUp)
                step(false);
            break;
        case Qt.Key_Right:
        case Qt.Key_PageDown:
            if (atEnd && event.key === Qt.Key_Right)
                film.endFocus = Math.min(1, Math.max(0, film.endFocus) + 1);
            else if (!atEnd)
                step(true);
            break;
        case Qt.Key_Down:
            if (atEnd)
                film.endFocus = 2;
            break;
        case Qt.Key_Up:
            if (atEnd && film.endFocus === 2)
                film.endFocus = 0;
            break;
        default:
            return;
        }
        event.accepted = true;
    }

    NumberAnimation {
        id: clock
        target: film
        property: "t"
    }

    NumberAnimation {
        id: fade
        property string how: "close"
        target: player
        property: "opacity"
        to: 0
        duration: 200
        onFinished: {
            if (how === "host")
                player.hostChosen();
            else if (how === "join")
                player.joinChosen();
            else
                player.closed();
        }
    }

    IntroBackdrop {
        anchors.fill: parent
        t: film.t
        // Brighter for the title and the end
        glow: F.track(film.t, [[0, 1], [film.tPlay - 0.5, 1], [film.tPlay + 0.5, 0.45], [film.tEnd - 0.3, 0.45], [film.tEnd + 0.9, 1]])
        focusY: F.track(film.t, [[film.tPlay - 0.5, 0.42], [film.tPlay + 0.5, 0.38], [film.tEnd - 0.3, 0.38], [film.tEnd + 0.9, 0.42]])
    }

    // A click anywhere on the picture pauses and plays, as in every player
    MouseArea {
        anchors.fill: parent
        enabled: !player.leaving && !player.atEnd
        onClicked: player.toggle()
    }

    IntroFilm {
        id: film
        scale: Math.min(player.width / 1920, player.height / 1080)
        transformOrigin: Item.TopLeft
        x: (player.width - 1920 * scale) / 2
        y: (player.height - 1080 * scale) / 2
        onHostChosen: player.leave("host")
        onJoinChosen: player.leave("join")
        onAgainChosen: player.again()
    }

    // Paused: says so in the middle, and how to go on
    Rectangle {
        anchors.centerIn: parent
        width: pausedRow.width + 44
        height: 56
        radius: 28
        color: Qt.rgba(0.05, 0.04, 0.12, 0.82)
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, 0.16)
        visible: opacity > 0
        opacity: !player.playing && !player.atEnd && !player.leaving ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 140 } }

        Row {
            id: pausedRow
            anchors.centerIn: parent
            spacing: 12

            Row {
                anchors.verticalCenter: parent.verticalCenter
                spacing: 5

                Rectangle { width: 5; height: 18; radius: 2; color: C.Theme.text }
                Rectangle { width: 5; height: 18; radius: 2; color: C.Theme.text }
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Paused")
                color: C.Theme.text
                font.pixelSize: 16
                font.weight: Font.ExtraBold
            }
        }
    }

    // The way out
    C.PillButton {
        id: skipButton
        anchors {
            top: parent.top
            right: parent.right
            topMargin: 22
            rightMargin: 24
        }
        small: true
        focusPolicy: Qt.NoFocus
        iconName: player.atEnd ? "close" : "forward"
        iconAfter: true
        text: player.atEnd ? qsTr("Close") : qsTr("Skip")
        Accessible.name: player.atEnd ? qsTr("Close the intro") : qsTr("Skip the intro")
        enabled: !player.leaving
        onClicked: player.leave("close")
    }

    // Sound off and on: remembered for the next time
    C.IconButton {
        anchors {
            verticalCenter: skipButton.verticalCenter
            right: skipButton.left
            rightMargin: 10
        }
        visible: !!player.sound
        small: true
        round: true
        focusPolicy: Qt.NoFocus
        iconName: player.muted ? "speaker-off" : "speaker"
        tip: player.muted ? qsTr("Sound on") : qsTr("Sound off")
        enabled: !player.leaving
        onClicked: player.toggleSound()
    }

    // The chapters: where the film is, and a click on one goes there
    Item {
        id: bar
        readonly property real gap: 8
        readonly property int count: film.chapters.length
        anchors {
            horizontalCenter: parent.horizontalCenter
            bottom: parent.bottom
            bottomMargin: 20
        }
        width: Math.min(parent.width - 150, 860)
        height: 34
        visible: width > 200

        // Pause and play
        QQC.AbstractButton {
            id: pauseButton
            x: -52
            y: 10
            width: 34
            height: 34
            focusPolicy: Qt.NoFocus
            hoverEnabled: true
            enabled: !player.leaving && !player.atEnd
            opacity: enabled ? 1 : 0.35
            Accessible.name: player.playing ? qsTr("Pause") : qsTr("Play")
            onClicked: player.toggle()

            background: Rectangle {
                radius: 17
                color: Qt.rgba(1, 1, 1, pauseButton.down ? 0.16 : pauseButton.hovered ? 0.11 : 0.06)
                border.width: 1
                border.color: Qt.rgba(1, 1, 1, 0.1)
            }

            contentItem: Item {
                Row {
                    anchors.centerIn: parent
                    visible: player.playing
                    spacing: 4

                    Rectangle { width: 4; height: 12; radius: 1.5; color: "#d9d5f2" }
                    Rectangle { width: 4; height: 12; radius: 1.5; color: "#d9d5f2" }
                }

                C.Icon {
                    anchors.centerIn: parent
                    visible: !player.playing
                    iconName: "play"
                    size: 15
                    color: "#d9d5f2"
                }
            }
        }

        Text {
            x: 0
            y: 0
            text: film.chapters[hovered >= 0 ? hovered : player.chapter].title
            readonly property int hovered: {
                for (let i = 0; i < segments.count; ++i) {
                    const item = segments.itemAt(i);
                    if (item && item.hovered)
                        return i;
                }
                return -1;
            }
            color: C.Theme.textDim
            font.pixelSize: 12
            font.weight: Font.Bold
        }

        Repeater {
            id: segments
            model: bar.count

            Item {
                id: segment
                required property int index
                readonly property real from: film.chapters[index].start
                readonly property real to: index < bar.count - 1 ? film.chapters[index + 1].start : film.duration
                readonly property real done: F.seg(film.t, from, to)
                readonly property alias hovered: hover.hovered
                x: index * (width + bar.gap)
                y: 12
                width: (bar.width - (bar.count - 1) * bar.gap) / bar.count
                height: 22

                Rectangle {
                    y: 11 - height / 2
                    width: parent.width
                    height: hover.hovered ? 7 : 4
                    radius: height / 2
                    color: Qt.rgba(1, 1, 1, 0.16)
                    Behavior on height { NumberAnimation { duration: 100 } }

                    Rectangle {
                        width: Math.max(segment.done > 0 ? parent.height : 0, parent.width * segment.done)
                        height: parent.height
                        radius: height / 2
                        color: F.lerpColor(Qt.rgba(167 / 255, 123 / 255, 1, 1), C.Theme.brandTo, bar.count > 1 ? segment.index / (bar.count - 1) : 0)
                    }
                }

                HoverHandler {
                    id: hover
                    cursorShape: Qt.PointingHandCursor
                }

                TapHandler {
                    enabled: !player.leaving
                    onTapped: player.seek(segment.from)
                }
            }
        }
    }
}
