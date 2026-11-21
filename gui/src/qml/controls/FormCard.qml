import QtQuick
import QtQuick.Layouts

// A card near the top of a page, for a form with a handful of fields: a heading, a
// sentence that says what the form is for, then the fields, and maybe a note under
// them (where to find the value asked for). In the look of the settings window.
Rectangle {
    id: card
    default property alias content: body.data
    property string title
    property string text
    /** Shown below the fields, for example where to find the value asked for. */
    property string footnote

    anchors {
        top: parent ? parent.top : undefined
        horizontalCenter: parent ? parent.horizontalCenter : undefined
        topMargin: 28
    }
    width: Math.min(parent ? parent.width - 32 : 640, Math.max(body.implicitWidth + 48, 560))
    height: column.implicitHeight + 44
    radius: 20
    color: Theme.card
    border.width: 1
    border.color: Theme.line

    ColumnLayout {
        id: column
        anchors {
            left: parent.left
            right: parent.right
            top: parent.top
            margins: 22
            leftMargin: 24
            rightMargin: 24
        }
        spacing: 12

        Text {
            Layout.fillWidth: true
            visible: card.title.length > 0
            text: card.title
            wrapMode: Text.WordWrap
            color: Theme.text
            font.pixelSize: 20
            font.weight: Font.ExtraBold
        }

        Text {
            Layout.fillWidth: true
            Layout.topMargin: card.title.length > 0 ? -6 : 0
            Layout.bottomMargin: 4
            visible: card.text.length > 0
            wrapMode: Text.WordWrap
            lineHeight: 1.15
            color: Theme.textSoft
            font.pixelSize: 14
            text: card.text
        }

        Item {
            id: body
            Layout.fillWidth: true
            implicitWidth: childrenRect.width
            implicitHeight: childrenRect.height
        }

        Text {
            Layout.fillWidth: true
            Layout.topMargin: 4
            visible: card.footnote.length > 0
            wrapMode: Text.WordWrap
            lineHeight: 1.15
            color: Theme.textDim
            font.pixelSize: 13
            text: card.footnote
        }
    }
}
