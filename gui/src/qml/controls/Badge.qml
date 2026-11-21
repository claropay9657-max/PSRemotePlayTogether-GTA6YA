import QtQuick

// A small label for what something is. `tone` keeps its meaning everywhere:
// "good" (on), "wait" (rest, waiting), "brand" (registered), "neutral"
// (language, counts), "play" (cyan: how a room is played), "door" (purple: how you
// get in), "web" (green: browsers can come), "bad" (muted, closed).
// With `dot` it has a coloured dot in front (states); `small` is for room cards.
Rectangle {
    id: badge
    property string text
    property string tone: "neutral"
    property bool dot: false
    property bool small: false
    /** A picture in front instead of the dot, for example a game's cover. */
    property alias imageSource: picture.source

    readonly property var tones: ({
        good: ["#1f34d399", "#9ff0cc", "#34d399"],
        wait: ["#1ff5b942", "#fbd98c", "#f5b942"],
        brand: ["#248c52ff", "#cdb6ff", "#a77bff"],
        neutral: ["#14ffffff", "#e7e4fa", "#e7e4fa"],
        play: ["#1c00f2ff", "#9ef7ff", "#00f2ff"],
        door: ["#338c52ff", "#d8c6ff", "#a77bff"],
        web: ["#2434d399", "#9ff0cc", "#34d399"],
        bad: ["#24f0466e", "#ffc2d1", "#f0466e"]
    })
    readonly property var colours: tones[tone] || tones.neutral

    implicitHeight: small ? 22 : 24
    implicitWidth: row.implicitWidth + (small ? 18 : 20) - (picture.visible ? 7 : 0)
    radius: height / 2
    color: colours[0]

    Row {
        id: row
        anchors.verticalCenter: parent.verticalCenter
        x: picture.visible ? 3 : (badge.small ? 9 : 10)
        spacing: 6

        Image {
            id: picture
            anchors.verticalCenter: parent.verticalCenter
            visible: status === Image.Ready
            width: badge.height - 6
            height: width
            sourceSize: Qt.size(width * 2, height * 2)
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
        }

        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            visible: badge.dot && !picture.visible
            width: 7
            height: 7
            radius: 3.5
            color: badge.colours[2]
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: badge.text
            font.pixelSize: badge.small ? 11.5 : 12
            font.weight: Font.Bold
            color: badge.colours[1]
        }
    }
}
