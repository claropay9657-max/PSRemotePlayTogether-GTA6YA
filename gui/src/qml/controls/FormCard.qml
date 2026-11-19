import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

// A glass card near the top of a page, for a form with a handful of fields:
// a heading, a sentence that says what the form is for, then the fields.
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
    width: Math.min(parent ? parent.width - 40 : 640, Math.max(body.implicitWidth + 64, 600))
    height: column.implicitHeight + 56
    radius: 18
    color: Qt.rgba(16 / 255, 16 / 255, 24 / 255, 0.72)
    border.width: 1
    border.color: Qt.rgba(1, 1, 1, 0.09)

    ColumnLayout {
        id: column
        anchors {
            left: parent.left
            right: parent.right
            top: parent.top
            margins: 28
            leftMargin: 32
            rightMargin: 32
        }
        spacing: 14

        GradientText {
            visible: card.title.length > 0
            text: card.title
            font.pixelSize: 22
            font.bold: true
            to: "#7c86ff"
        }

        Label {
            Layout.fillWidth: true
            Layout.bottomMargin: 6
            visible: card.text.length > 0
            wrapMode: Text.WordWrap
            opacity: 0.7
            font.pixelSize: 15
            lineHeight: 1.15
            text: card.text
        }

        Item {
            id: body
            Layout.alignment: Qt.AlignHCenter
            implicitWidth: childrenRect.width
            implicitHeight: childrenRect.height
        }

        Label {
            Layout.fillWidth: true
            Layout.topMargin: 6
            visible: card.footnote.length > 0
            wrapMode: Text.WordWrap
            opacity: 0.55
            font.pixelSize: 13
            lineHeight: 1.15
            text: card.footnote
        }
    }
}
