import QtQuick
import QtQuick.Controls as QQC

// A link in a line of text: "Use my own…", "About, credits and licenses…".
QQC.AbstractButton {
    id: link
    /** A small icon after the text, such as "external" for a page in the browser. */
    property string iconName
    property bool firstInFocusChain: false
    property bool lastInFocusChain: false

    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    implicitWidth: content.implicitWidth + 4
    implicitHeight: Math.max(24, content.implicitHeight + 4)
    font.pixelSize: 13
    font.weight: Font.Bold
    // Greyed out on its own; inside something greyed out already (a row), not a second time
    opacity: enabled || (parent && !parent.enabled) ? 1 : 0.5

    Keys.onPressed: (event) => FocusChain.handle(link, event)

    background: Rectangle {
        anchors.fill: parent
        anchors.margins: -3
        radius: 6
        color: "transparent"
        border.width: 2
        border.color: Theme.focus
        visible: link.visualFocus
    }

    contentItem: Item {
        implicitWidth: content.implicitWidth
        implicitHeight: content.implicitHeight

        Row {
            id: content
            anchors.verticalCenter: parent.verticalCenter
            x: 2
            spacing: 5

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: link.text
                font.pixelSize: link.font.pixelSize
                font.weight: link.font.weight
                font.underline: link.hovered
                color: link.hovered ? "#ffffff" : Theme.headingSoft
            }

            Icon {
                visible: link.iconName.length > 0
                anchors.verticalCenter: parent.verticalCenter
                iconName: link.iconName
                size: 13
                color: link.hovered ? "#ffffff" : Theme.headingSoft
            }
        }
    }
}
