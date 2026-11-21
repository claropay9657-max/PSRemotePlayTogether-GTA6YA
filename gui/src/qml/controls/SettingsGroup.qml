import QtQuick
import QtQuick.Layouts

// A group of settings on a page: a small capital heading, maybe a note beside it,
// and its rows (SettingsRow) on one card, a thin line between two rows.
//
// Put the rows straight into the group (a Repeater in it is fine): a row draws its
// line when another row is above it in the same column. To show or hide some rows
// together, bind each row's `visible` rather than wrapping them.
// `card: false` leaves out the card, for things that are cards of their own.
ColumnLayout {
    id: group
    property string title
    property string note
    property bool card: true
    default property alias content: rows.data
    readonly property bool isSettingsGroup: true

    Layout.fillWidth: true
    spacing: 8

    Flow {
        Layout.fillWidth: true
        visible: group.title.length > 0 || group.note.length > 0
        spacing: 8

        SectionLabel {
            visible: text.length > 0
            text: group.title
        }

        Text {
            visible: text.length > 0
            width: Math.min(implicitWidth, parent.width)
            text: group.note
            wrapMode: Text.WordWrap
            color: Theme.textDim
            font.pixelSize: 12
        }
    }

    Rectangle {
        Layout.fillWidth: true
        implicitHeight: rows.implicitHeight
        radius: 18
        color: group.card ? Theme.card : "transparent"
        border.width: group.card ? 1 : 0
        border.color: Theme.line

        ColumnLayout {
            id: rows
            width: parent.width
            spacing: group.card ? 0 : 10
        }
    }
}
