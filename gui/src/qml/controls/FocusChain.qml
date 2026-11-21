pragma Singleton
import QtQuick

// What every control of the new look does with keys and a controller: Up and Down
// move along the focus chain, Return presses. Attach it with
// `Keys.onPressed: (event) => FocusChain.handle(control, event)`.
QtObject {
    function move(item, forward) {
        const next = item.nextItemInFocusChain(forward);
        if (next)
            next.forceActiveFocus(Qt.TabFocusReason);
    }

    function handle(control, event) {
        switch (event.key) {
        case Qt.Key_Up:
            if (!control.firstInFocusChain) {
                move(control, false);
                event.accepted = true;
            }
            break;
        case Qt.Key_Down:
            if (!control.lastInFocusChain) {
                move(control, true);
                event.accepted = true;
            }
            break;
        case Qt.Key_Return:
        case Qt.Key_Enter:
            if (control.visualFocus) {
                if (control.checkable) {
                    control.toggle();
                    control.toggled();
                } else {
                    control.clicked();
                }
            }
            event.accepted = true;
            break;
        }
    }
}
