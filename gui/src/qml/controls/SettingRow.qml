import QtQuick
import QtQuick.Layouts

// One setting in the older dialogs (in a SettingsSection): what it is on the left
// (name, a line about it, its default), the control on the right in a column of the
// same width on every page. In the look of the settings window's SettingsRow.
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
    /** No line above the first row of a section. */
    readonly property bool firstInColumn: {
        const kids = parent ? parent.visibleChildren : [];
        for (let i = 0; i < kids.length; ++i) {
            if (kids[i].isSettingRow === true || kids[i].height > 0)
                return kids[i] === row;
        }
        return true;
    }

    Layout.fillWidth: true
    implicitHeight: Math.max(texts.implicitHeight, controlArea.implicitHeight) + 24
    opacity: enabled ? 1 : 0.5

    Rectangle {
        visible: !row.firstInColumn
        anchors {
            left: parent.left
            right: parent.right
            top: parent.top
            leftMargin: -18
            rightMargin: -18
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

            Text {
                Layout.fillWidth: true
                visible: text.length > 0
                text: row.title
                wrapMode: Text.WordWrap
                color: Theme.text
                font.pixelSize: 15
                font.weight: Font.ExtraBold
            }

            Text {
                Layout.fillWidth: true
                visible: text.length > 0
                text: row.description
                wrapMode: Text.WordWrap
                color: Theme.textDim
                font.pixelSize: 13
            }

            Text {
                visible: row.hint.length > 0
                text: qsTr("Default: %1").arg(row.hint.replace(/^\((.*)\)$/, "$1"))
                color: Theme.textDim
                opacity: 0.8
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
