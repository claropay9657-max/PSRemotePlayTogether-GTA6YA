import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

// One setting: what it is on the left (name, a line about it, its default), the
// control on the right in a column of the same width on every page.
Item {
    id: row
    property string title
    property string description
    /** The default as the settings note it, "(Ask)" or "Ask"; shown as "Default: Ask". */
    property string hint
    /** Width of the control column. */
    property int controlWidth: 380
    default property alias content: controlArea.data
    /** Marks this as the thing to scroll into view when one of its controls gets the focus. */
    readonly property bool isSettingRow: true

    Layout.fillWidth: true
    implicitHeight: Math.max(texts.implicitHeight, controlArea.implicitHeight) + 26

    Rectangle {
        anchors {
            left: parent.left
            right: parent.right
            top: parent.top
        }
        height: 1
        color: Qt.rgba(1, 1, 1, 0.06)
    }

    RowLayout {
        anchors {
            left: parent.left
            right: parent.right
            verticalCenter: parent.verticalCenter
        }
        spacing: 24

        ColumnLayout {
            id: texts
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            spacing: 2

            Label {
                Layout.fillWidth: true
                visible: text.length > 0
                text: row.title
                wrapMode: Text.WordWrap
                font.pixelSize: 15
                font.weight: Font.DemiBold
            }

            Label {
                Layout.fillWidth: true
                visible: text.length > 0
                text: row.description
                wrapMode: Text.WordWrap
                opacity: 0.55
                font.pixelSize: 13
            }

            Label {
                visible: row.hint.length > 0
                text: qsTr("Default: %1").arg(row.hint.replace(/^\((.*)\)$/, "$1"))
                opacity: 0.38
                font.pixelSize: 12
            }
        }

        RowLayout {
            id: controlArea
            Layout.preferredWidth: row.controlWidth
            Layout.maximumWidth: row.controlWidth
            Layout.alignment: Qt.AlignVCenter | Qt.AlignRight
            spacing: 10
        }
    }
}
