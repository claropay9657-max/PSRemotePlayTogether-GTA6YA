import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC

import org.streetpea.chiaking

import "controls" as C

// Links the app to a console once: its address, the PSN account, the number the
// console shows. What happens then shows in a note over the page.
DialogView {
    id: dialog
    property bool ps5: true
    property alias host: hostField.text
    /** Which console it is, as the registration knows it: PS5 1000100; PS4 by its
     *  system software 1000 (8.0 or newer), 900 (7.x) or 800 (older than 7.0). */
    property int consoleTarget: ps5 ? 1000100 : ps4Target
    /** The PS4 choice, kept while PS5 is chosen for a moment. */
    property int ps4Target: 1000
    readonly property bool isPs4: consoleTarget !== 1000100
    /** A PS4 older than 7.0 registers with the online ID, all others with the account ID. */
    readonly property bool usesOnlineId: consoleTarget === 800

    title: qsTr("Register a console")
    buttonText: qsTr("Register")
    buttonEnabled: hostField.text.trim() && pin.acceptableInput && cpin.acceptableInput && (!usesOnlineId || onlineId.text.trim()) && (usesOnlineId || accountId.text.trim())
    // The account ID of the PSN login, at once and again on coming back (from the login)
    function fillAccountId() {
        if (Chiaki.settings.psnAccountId && !accountId.text)
            accountId.text = Chiaki.settings.psnAccountId;
    }
    Component.onCompleted: fillAccountId()
    QQC.StackView.onActivated: fillAccountId()
    onAccepted: {
        let psnId = usesOnlineId ? onlineId.text.trim() : accountId.text.trim();
        let registerOk = Chiaki.registerHost(hostField.text.trim(), psnId, pin.text.trim(), cpin.text.trim(), hostField.text.trim() == "255.255.255.255", consoleTarget, function(msg, ok, done) {
            if (!done)
                logArea.text += msg + "\n";
            else
                logDialog.allDone = true;
        });
        if (registerOk) {
            logArea.text = "";
            logDialog.open();
        }
    }

    component FieldLabel: Text {
        color: C.Theme.textSoft
        font.pixelSize: 13
        font.weight: Font.Bold
    }

    Item {
        // Keyboard and controller: whatever gets the focus is scrolled into view
        Connections {
            target: dialog.Window.window
            function onActiveFocusItemChanged() {
                const item = dialog.Window.window ? dialog.Window.window.activeFocusItem : null;
                if (!item)
                    return;
                let inside = false;
                for (let p = item; p; p = p.parent) {
                    if (p === flick.contentItem) {
                        inside = true;
                        break;
                    }
                }
                if (!inside)
                    return;
                const top = item.mapToItem(flick.contentItem, 0, 0).y - 24;
                const bottom = top + item.height + 48;
                const max = Math.max(0, flick.contentHeight - flick.height);
                // Near the top: all the way up, with the card's heading
                if (top < flick.contentY)
                    flick.contentY = top < 200 ? 0 : top;
                else if (bottom > flick.contentY + flick.height)
                    flick.contentY = Math.min(max, bottom - flick.height);
            }
        }

        Flickable {
            id: flick
            anchors.fill: parent
            contentWidth: width
            contentHeight: card.height + 56
            boundsBehavior: Flickable.StopAtBounds
            clip: true
            QQC.ScrollIndicator.vertical: QQC.ScrollIndicator { }

            C.FormCard {
                id: card
                title: qsTr("Link this app to your console")
                text: qsTr("Needed once per console. The console shows an 8-digit number that proves you are sitting in front of it; together with your PSN account it lets this app connect from then on.")
                footnote: dialog.isPs4
                    ? qsTr("Where the number is: on the PS4 open Settings → Remote Play Connection Settings → Add Device. It is valid for a few minutes.")
                    : qsTr("Where the number is: on the PS5 open Settings → System → Remote Play → Link Device (switch on Enable Remote Play first). It is valid for a few minutes.")

                ColumnLayout {
                    width: Math.min(512, flick.width - 80)
                    spacing: 6

                    FieldLabel {
                        text: qsTr("Console address")
                    }

                    C.Field {
                        id: hostField
                        Layout.fillWidth: true
                        Accessible.name: qsTr("Console address")
                        hint: qsTr("IP address or host name")
                        echoMode: Chiaki.settings.streamerMode ? TextInput.Password : TextInput.Normal
                        firstInFocusChain: true
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: hostField.text.trim() == "255.255.255.255"
                        text: qsTr("This address looks for the console on this network.")
                        wrapMode: Text.WordWrap
                        color: C.Theme.textDim
                        font.pixelSize: 12
                    }

                    FieldLabel {
                        Layout.topMargin: 10
                        text: qsTr("Console")
                    }

                    C.Segmented {
                        id: consoleChoice
                        Layout.fillWidth: true
                        Accessible.name: qsTr("Console")
                        model: [qsTr("PS5"), qsTr("PS4")]
                        currentIndex: dialog.isPs4 ? 1 : 0
                        onActivated: (index) => dialog.consoleTarget = index === 0 ? 1000100 : dialog.ps4Target
                    }

                    // The PS4 registers differently by its system software
                    FieldLabel {
                        Layout.topMargin: 10
                        visible: dialog.isPs4
                        text: qsTr("PS4 system software")
                    }

                    C.Segmented {
                        id: firmwareChoice
                        readonly property var targets: [1000, 900, 800]
                        Layout.fillWidth: true
                        visible: dialog.isPs4
                        Accessible.name: qsTr("PS4 system software")
                        model: [qsTr("8.0 or newer"), qsTr("7.0 to 7.55"), qsTr("Older than 7.0")]
                        currentIndex: Math.max(0, targets.indexOf(dialog.consoleTarget))
                        onActivated: (index) => {
                            dialog.ps4Target = targets[index];
                            dialog.consoleTarget = targets[index];
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: dialog.isPs4
                        text: qsTr("The version is in Settings → System → System Information on the PS4.")
                        wrapMode: Text.WordWrap
                        color: C.Theme.textDim
                        font.pixelSize: 12
                    }

                    FieldLabel {
                        Layout.topMargin: 10
                        visible: dialog.usesOnlineId
                        text: qsTr("PSN online ID")
                    }

                    C.Field {
                        id: onlineId
                        Layout.fillWidth: true
                        visible: dialog.usesOnlineId
                        Accessible.name: qsTr("PSN online ID")
                        hint: qsTr("Your PSN name; capitals count")
                        echoMode: Chiaki.settings.streamerMode ? TextInput.Password : TextInput.Normal
                    }

                    FieldLabel {
                        Layout.topMargin: 10
                        visible: !dialog.usesOnlineId
                        text: qsTr("PSN account ID")
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        visible: !dialog.usesOnlineId
                        spacing: 8

                        C.Field {
                            id: accountId
                            Layout.fillWidth: true
                            Accessible.name: qsTr("PSN account ID")
                            hint: loginButton.visible ? qsTr("Log in or look it up") : qsTr("Your PSN account ID")
                            echoMode: Chiaki.settings.streamerMode ? TextInput.Password : TextInput.Normal
                            // Right goes to the buttons while not typing
                            KeyNavigation.priority: readOnly ? KeyNavigation.BeforeItem : KeyNavigation.AfterItem
                            KeyNavigation.right: loginButton.visible ? loginButton : null
                        }

                        C.PillButton {
                            id: loginButton
                            visible: !Chiaki.settings.psnAccountId
                            small: true
                            kind: "primary"
                            text: qsTr("Log in to PSN")
                            // On macOS and Linux the login opens in the browser
                            iconName: Qt.platform.os === "osx" || Qt.platform.os === "linux" ? "external" : ""
                            iconAfter: true
                            onClicked: stack.push(psnLoginDialogComponent, {login: true, callback: (id) => accountId.text = id})
                            KeyNavigation.left: accountId
                            KeyNavigation.right: lookupButton
                        }

                        C.PillButton {
                            id: lookupButton
                            visible: !Chiaki.settings.psnAccountId
                            small: true
                            text: qsTr("Look it up")
                            onClicked: stack.push(psnLoginDialogComponent, {login: false, callback: (id) => accountId.text = id})
                            KeyNavigation.left: loginButton
                        }
                    }

                    FieldLabel {
                        Layout.topMargin: 10
                        text: qsTr("Number from the console")
                    }

                    C.Field {
                        id: pin
                        Layout.fillWidth: true
                        Accessible.name: qsTr("Number from the console")
                        hint: qsTr("8 digits")
                        validator: RegularExpressionValidator { regularExpression: /[0-9]{8}/ }
                        inputMethodHints: Qt.ImhDigitsOnly
                        echoMode: Chiaki.settings.streamerMode ? TextInput.Password : TextInput.Normal
                    }

                    FieldLabel {
                        Layout.topMargin: 10
                        text: qsTr("Login PIN, if you use one")
                    }

                    C.Field {
                        id: cpin
                        Layout.fillWidth: true
                        Accessible.name: qsTr("Login PIN")
                        hint: qsTr("4 digits, or empty")
                        validator: RegularExpressionValidator { regularExpression: /^$|[0-9]{4}/ }
                        inputMethodHints: Qt.ImhDigitsOnly
                        echoMode: Chiaki.settings.streamerMode ? TextInput.Password : TextInput.Normal
                        lastInFocusChain: true
                    }
                }
            }
        }

        // What happens while registering; Close (or Cancel) goes back
        C.PopupCard {
            id: logDialog
            property bool allDone: false
            title: qsTr("Registering the console")
            closePolicy: QQC.Popup.NoAutoClose
            onOpened: logArea.forceActiveFocus(Qt.TabFocusReason)
            onClosed: stack.pop();

            ColumnLayout {
                spacing: 18

                Rectangle {
                    Layout.preferredWidth: Math.min(600, (logDialog.parent ? logDialog.parent.width : 800) - 112)
                    Layout.preferredHeight: Math.min(340, (logDialog.parent ? logDialog.parent.height : 600) - 200)
                    radius: 12
                    color: C.Theme.field
                    border.width: 1
                    border.color: C.Theme.line

                    Flickable {
                        id: logFlick
                        anchors {
                            fill: parent
                            margins: 12
                        }
                        clip: true
                        contentWidth: width
                        contentHeight: logArea.implicitHeight
                        flickableDirection: Flickable.AutoFlickIfNeeded
                        QQC.ScrollBar.vertical: QQC.ScrollBar {
                            id: logScrollbar
                            policy: QQC.ScrollBar.AlwaysOn
                            visible: logFlick.contentHeight > logFlick.height
                            contentItem: Rectangle {
                                implicitWidth: 4
                                radius: 2
                                color: Qt.rgba(1, 1, 1, 0.28)
                            }
                        }

                        Text {
                            id: logArea
                            width: logFlick.width - 12
                            wrapMode: Text.Wrap
                            lineHeight: 1.2
                            color: C.Theme.textSoft
                            font.pixelSize: 13
                            Keys.onReturnPressed: if (logDialog.allDone) logDialog.close()
                            Keys.onEscapePressed: logDialog.close()
                            Keys.onPressed: (event) => {
                                switch (event.key) {
                                case Qt.Key_Up:
                                    if(logScrollbar.position > 0.001)
                                        logFlick.flick(0, 500);
                                    event.accepted = true;
                                    break;
                                case Qt.Key_Down:
                                    if(logScrollbar.position < 1.0 - logScrollbar.size - 0.001)
                                        logFlick.flick(0, -500);
                                    event.accepted = true;
                                    break;
                                }
                            }
                        }
                    }
                }

                C.GlyphButton {
                    Layout.alignment: Qt.AlignRight
                    text: logDialog.allDone ? qsTr("Close") : qsTr("Cancel")
                    primary: logDialog.allDone
                    glyph: root.controllerButton(logDialog.allDone ? "cross" : "moon")
                    onClicked: logDialog.close()
                }
            }
        }

        Component {
            id: psnLoginDialogComponent
            PSNLoginDialog { }
        }
    }
}
