import QtQuick
import QtQuick.Controls

// A menu that is never cut off: as wide as its longest line, and moved back into the
// window where it would stick out. Qt's own menu is 200 wide whatever it holds and
// shortens longer lines with "…".
//
// Opened from the keys or a controller, it gives the focus back the same way when it
// closes, focus ring and all: Qt hands it back as if by mouse, and the new look's
// buttons only press on Return while the ring shows.
Menu {
    id: menu

    // Keeps it inside the window
    margins: 8

    property Item restoreTo: null
    property bool restoreRing: false

    function fit() {
        let widest = 200;
        for (let i = 0; i < count; ++i) {
            const item = itemAt(i);
            if (item && item.visible)
                widest = Math.max(widest, Math.ceil(item.implicitWidth));
        }
        width = widest;
    }

    function window() {
        return menu.parent ? menu.parent.Window.window : null;
    }

    onAboutToShow: {
        fit();
        const w = window();
        restoreTo = w ? w.activeFocusItem : null;
        restoreRing = !!restoreTo && restoreTo.visualFocus === true;
    }

    onClosed: {
        if (!restoreRing || !restoreTo)
            return;
        const item = restoreTo;
        Qt.callLater(() => {
            const w = window();
            if (w && w.activeFocusItem === item && item.focusReason !== undefined)
                item.focusReason = Qt.TabFocusReason;
        });
    }
}
