import QtQuick
import QtQuick.Controls

// A menu that is never cut off: as wide as its longest line, and moved back into the
// window where it would stick out. Qt's own menu is 200 wide whatever it holds and
// shortens longer lines with "…".
Menu {
    id: menu

    // Keeps it inside the window
    margins: 8

    function fit() {
        let widest = 200;
        for (let i = 0; i < count; ++i) {
            const item = itemAt(i);
            if (item && item.visible)
                widest = Math.max(widest, Math.ceil(item.implicitWidth));
        }
        width = widest;
    }

    onAboutToShow: fit()
}
