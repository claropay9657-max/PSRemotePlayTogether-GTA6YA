import QtQuick
import QtQuick.Layouts

// One setting: what it is and what it does on the left, its control on the right.
// Where the row is too narrow for both, the control goes under the text.
//
// The settings window finds rows by their title, subtitle and keywords when you
// search, and scrolls to the row of the control that has the focus. Something that is
// not a row can take part too: give it `readonly property bool isSettingRow: true`
// and a `title`.
Item {
    id: row
    property string title
    property string subtitle
    /** More words the search should find this by. */
    property string keywords
    /** Changing it restarts the app: said on the row. */
    property bool restart: false
    /** Belongs to the row above: set in, and greyed out with it (bind `enabled`). */
    property bool sub: false
    /** A Toggle in the row: a click on the text switches it as well. */
    property var switchItem: null
    /** The text keeps at least this much room before the control moves under it. */
    property int textMinimum: 240
    default property alias content: controls.data
    readonly property bool isSettingRow: true

    readonly property int leftPad: sub ? 34 : 18
    readonly property int rightPad: 18
    readonly property bool stacked: width - leftPad - rightPad < textMinimum + 20 + controls.implicitWidth
    /** No line above the first row of a group. */
    readonly property bool firstInColumn: {
        const kids = parent ? parent.visibleChildren : [];
        for (let i = 0; i < kids.length; ++i) {
            if (kids[i].isSettingRow === true || kids[i].height > 0)
                return kids[i] === row;
        }
        return true;
    }

    Layout.fillWidth: true
    implicitHeight: (stacked ? texts.implicitHeight + 10 + controls.implicitHeight
                             : Math.max(texts.implicitHeight, controls.implicitHeight)) + 24
    opacity: enabled ? 1 : 0.5

    Rectangle {
        visible: !row.firstInColumn
        width: parent.width
        height: 1
        color: Qt.rgba(1, 1, 1, 0.06)
    }

    ColumnLayout {
        id: texts
        x: row.leftPad
        y: row.stacked ? 12 : Math.round((row.height - height) / 2)
        width: row.stacked ? row.width - row.leftPad - row.rightPad
                           : row.width - row.leftPad - row.rightPad - 20 - controls.implicitWidth
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
            text: row.subtitle
            wrapMode: Text.WordWrap
            textFormat: Text.PlainText
            color: Theme.textDim
            font.pixelSize: 13
        }

        Row {
            visible: row.restart
            spacing: 5

            Icon {
                anchors.verticalCenter: parent.verticalCenter
                iconName: "refresh"
                size: 12
                color: Theme.wait
            }

            Text {
                text: qsTr("Restarts the app")
                color: Theme.wait
                font.pixelSize: 12
                font.weight: Font.Bold
            }
        }

    }

    // Over the text, outside its layout
    MouseArea {
        x: texts.x
        y: texts.y
        width: texts.width
        height: texts.height
        enabled: !!row.switchItem && row.switchItem.enabled
        cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
        onClicked: {
            row.switchItem.toggle();
            row.switchItem.toggled();
        }
    }

    RowLayout {
        id: controls
        x: row.stacked ? row.leftPad : row.width - row.rightPad - width
        y: row.stacked ? texts.y + texts.height + 10 : Math.round((row.height - height) / 2)
        width: row.stacked ? Math.min(implicitWidth, row.width - row.leftPad - row.rightPad) : implicitWidth
        spacing: 8
    }
}
