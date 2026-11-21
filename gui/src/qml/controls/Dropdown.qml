import QtQuick
import QtQuick.Controls as QQC
import QtQuick.Templates as T

// A choice of many, of the new look: the chosen one on a field with a chevron, the
// list below it when opened. As ComboBox.qml underneath: as wide as its longest entry,
// never cut off, and Up / Down move on while it is closed. Bind `currentIndex` and
// change the setting in onActivated.
ComboBox {
    id: control

    implicitHeight: 40
    leftPadding: 0
    rightPadding: 36
    topPadding: 0
    bottomPadding: 0
    hoverEnabled: true
    font.pixelSize: 14
    font.weight: Font.DemiBold
    // Greyed out on its own; inside something greyed out already (a row), not a second time
    opacity: enabled || (parent && !parent.enabled) ? 1 : 0.5

    // A text input, so that WidestText can measure the entries; it is not drawn, the
    // text over it is, shortened with "…" where the dropdown is narrower than the entry
    contentItem: T.TextField {
        id: measure
        leftPadding: 12
        rightPadding: 0
        topPadding: 0
        bottomPadding: 0
        text: control.displayText
        enabled: false
        readOnly: true
        autoScroll: false
        color: "transparent"
        selectionColor: "transparent"
        font: control.font
        verticalAlignment: TextInput.AlignVCenter
        background: null

        Text {
            anchors.fill: parent
            leftPadding: measure.leftPadding
            text: control.displayText
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
            color: Theme.text
            font: control.font
        }
    }

    background: Rectangle {
        implicitWidth: 140
        implicitHeight: 40
        radius: Theme.radiusField
        color: Theme.field
        border.width: control.visualFocus ? 2 : 1
        border.color: control.visualFocus ? Theme.focus
                    : Qt.rgba(1, 1, 1, control.hovered || control.popup.visible ? 0.2 : 0.12)
    }

    indicator: Icon {
        x: control.width - width - 12
        y: (control.height - height) / 2
        iconName: "chevron-down"
        size: 16
        color: Theme.textSoft
        rotation: control.popup.visible ? 180 : 0
    }

    delegate: QQC.ItemDelegate {
        id: entry
        required property int index
        required property var model
        readonly property bool chosen: index === control.currentIndex

        width: ListView.view ? ListView.view.width : implicitWidth
        implicitHeight: 38
        leftPadding: 12
        rightPadding: 12
        hoverEnabled: true
        highlighted: control.highlightedIndex === index
        text: {
            const data = model.modelData;
            if (!control.textRole)
                return data !== undefined ? data : "";
            if (data !== undefined && data !== null && typeof data === "object")
                return data[control.textRole];
            return model[control.textRole];
        }

        contentItem: Text {
            text: entry.text
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
            font.pixelSize: 14
            font.weight: entry.chosen ? Font.ExtraBold : Font.DemiBold
            color: entry.chosen ? Theme.text : "#d9d5f2"
        }

        background: Rectangle {
            radius: 8
            color: entry.highlighted || entry.hovered ? Qt.rgba(140 / 255, 82 / 255, 1, 0.22) : "transparent"
        }
    }

    popup.y: control.height + 4
    popup.padding: 4
    popup.background: Rectangle {
        radius: 12
        color: Theme.popup
        border.width: 1
        border.color: Theme.lineStrong
    }
}
