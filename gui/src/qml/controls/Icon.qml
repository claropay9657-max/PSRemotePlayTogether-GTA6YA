import QtQuick
import QtQuick.Controls.impl as Impl

// One of the line icons (qrc:/icons/ui/<name>.svg), in any colour.
Impl.IconImage {
    property string iconName
    property int size: 20

    source: iconName ? Theme.iconBase + iconName + ".svg" : ""
    sourceSize: Qt.size(size, size)
    width: size
    height: size
    color: Theme.text
    fillMode: Image.PreserveAspectFit
}
