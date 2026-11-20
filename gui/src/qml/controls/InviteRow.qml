import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.Material

// One invite to hand out: who it is for, the invite itself, and a button that
// copies it. While the invite is not known yet, the note says why.
Rectangle {
    id: row
    property string title
    property string note
    property string invite
    /** Something is being looked up for this invite right now. */
    property bool busy: false
    /** Hide the invite itself (streamer mode): it can still be copied. */
    property bool secret: false
    /** What stands there instead while it is hidden. */
    property string secretText: "psrpt://••••••••••••••••"
    /** A second button, for example "Try again"; hidden while empty. */
    property string actionText
    property color dot: "#8c52ff"
    property alias copyButton: copyButton
    signal action()

    Layout.fillWidth: true
    implicitHeight: content.implicitHeight + 24
    radius: 12
    color: Qt.rgba(1, 1, 1, 0.045)
    border.width: 1
    border.color: Qt.rgba(1, 1, 1, 0.09)

    // Copies any text: a text item is the one thing in QML that reaches the clipboard
    TextEdit {
        id: clipboard
        visible: false
    }

    RowLayout {
        id: content
        anchors {
            left: parent.left
            right: parent.right
            verticalCenter: parent.verticalCenter
            leftMargin: 16
            rightMargin: 12
        }
        spacing: 14

        Rectangle {
            Layout.alignment: Qt.AlignTop
            Layout.topMargin: 7
            width: 10
            height: 10
            radius: 5
            color: row.invite.length > 0 ? row.dot : Qt.rgba(1, 1, 1, 0.25)
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 3

            RowLayout {
                Layout.fillWidth: true
                spacing: 12

                Label {
                    text: row.title
                    font.pixelSize: 16
                    font.bold: true
                }

                Label {
                    Layout.fillWidth: true
                    visible: row.invite.length > 0
                    elide: Text.ElideMiddle
                    opacity: 0.85
                    font.family: "Menlo, Consolas, DejaVu Sans Mono, monospace"
                    font.pixelSize: 13
                    text: row.secret ? row.secretText : row.invite
                }

                BusyIndicator {
                    visible: row.busy
                    running: row.busy
                    implicitWidth: 22
                    implicitHeight: 22
                    padding: 0
                }

                Item {
                    Layout.fillWidth: true
                    visible: row.invite.length === 0
                }
            }

            Label {
                Layout.fillWidth: true
                visible: text.length > 0
                wrapMode: Text.WordWrap
                opacity: 0.62
                font.pixelSize: 13
                text: row.note
            }
        }

        Button {
            id: actionButton
            visible: row.actionText.length > 0
            flat: true
            text: row.actionText
            Material.roundedScale: Material.SmallScale
            onClicked: row.action()
            Keys.onReturnPressed: clicked()
        }

        Button {
            id: copyButton
            enabled: row.invite.length > 0
            highlighted: enabled
            text: copied.running ? qsTr("Copied") : qsTr("Copy")
            Material.roundedScale: Material.SmallScale
            Keys.onReturnPressed: clicked()
            onClicked: {
                clipboard.text = row.invite;
                clipboard.selectAll();
                clipboard.copy();
                clipboard.text = "";
                copied.restart();
            }
            Timer {
                id: copied
                interval: 1500
            }
        }
    }
}
