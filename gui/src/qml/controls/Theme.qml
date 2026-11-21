pragma Singleton
import QtQuick

// The few colours and sizes every screen shares. A colour always means the same
// thing: the gradient starts something, purple goes on, green is on or lets in,
// amber rests or waits, red leaves or removes, cyan is focus and how a room plays.
QtObject {
    // Grounds, back to front
    readonly property color ground: "#0c0a1d"
    readonly property color card: "#15122b"
    readonly property color cardHigh: "#1a1535"
    readonly property color raised: "#1d1938"
    readonly property color field: "#0f0c22"
    readonly property color popup: "#1b1735"
    readonly property color line: Qt.rgba(1, 1, 1, 0.08)
    readonly property color lineStrong: Qt.rgba(1, 1, 1, 0.14)

    // The brand: the one main action per screen, and the name
    readonly property color brandFrom: "#8c52ff"
    readonly property color brandTo: "#f0469b"
    readonly property color buttonFrom: "#7a3cf5"
    readonly property color buttonTo: "#c92d7b"
    readonly property color button: "#7a3cf5"
    readonly property color heading: "#7c86ff"
    readonly property color headingSoft: "#a9b0ff"
    readonly property color focus: "#00f2ff"

    // What things are
    readonly property color good: "#34d399"
    readonly property color goodButton: "#1f9d6f"
    readonly property color wait: "#f5b942"
    readonly property color bad: "#f0466e"

    // Text
    readonly property color text: "#f3f1ff"
    readonly property color textSoft: "#b9b4d6"
    readonly property color textDim: "#8f8aad"
    readonly property color textOnLight: "#2a1366"

    /** Where the line icons are; a preview outside the app points it at gui/res/ui/. */
    property string iconBase: "qrc:/icons/ui/"

    // Sizes
    readonly property int radiusCard: 20
    readonly property int radiusField: 12
    readonly property int touch: 44
}
