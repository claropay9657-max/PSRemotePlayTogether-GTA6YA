import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC

import org.streetpea.chiaking

import "controls" as C

// Adds the app to Steam as a non-Steam game; what happens shows in a note over the page.
DialogView {
    id: dialog
    property bool opening: false
    property bool succeeded: false
    property bool fromReminder
    /** Where Steam is, when chosen; empty: found by itself. */
    property var steamBasePath: ""
    title: qsTr("Steam shortcut")
    buttonText: qsTr("Create")
    buttonEnabled: name.text.trim() && !opening
    function close() {
        root.closeDialog();
        if(dialog.fromReminder && Chiaki.settings.remotePlayAsk)
        {
            if(!Chiaki.settings.psnRefreshToken || !Chiaki.settings.psnAuthToken || !Chiaki.settings.psnAuthTokenExpiry || !Chiaki.settings.psnAccountId)
                root.showRemindDialog(qsTr("Remote Play via PSN"), qsTr("Would you like to connect to PSN?\nThis enables:\n- Automatic registration\n- Playing outside of your home network without port forwarding?") + "\n\n" + qsTr("(If you say no now and want it later: Settings › Away from home (PSN).)"), true, () => root.showPSNTokenDialog(false));
            else
                Chiaki.settings.remotePlayAsk = false;
        }
    }
    onAccepted: {
        opening = true;
        logDialog.open();
        Chiaki.createSteamShortcut(name.text.trim(), options.text.trim(), function(msg, ok, done) {
            if(ok)
            {
                succeeded = true;
                Chiaki.settings.addSteamShortcutAsk = false;
            }
            if (!done)
                logArea.text += msg + "\n";
            else
            {
                logArea.text += msg + "\n";
                logDialog.allDone = true;
                opening = false;
            }
        }, dialog.steamBasePath);
    }

    Item {
        id: page

        C.FormCard {
            title: qsTr("Start from Steam")
            text: qsTr("Adds PSRemotePlayTogether to Steam as a non-Steam game, with artwork and a controller layout. Steam has to be restarted afterwards.")

            ColumnLayout {
                width: Math.min(512, page.width - 80)
                spacing: 6

                Text {
                    text: qsTr("Name in Steam")
                    color: C.Theme.textSoft
                    font.pixelSize: 13
                    font.weight: Font.Bold
                }

                C.Field {
                    id: name
                    Layout.fillWidth: true
                    Accessible.name: qsTr("Name in Steam")
                    text: Chiaki.settings.currentProfile ? qsTr("PSRemotePlayTogether ") + Chiaki.settings.currentProfile: qsTr("PSRemotePlayTogether")
                    firstInFocusChain: true
                }

                Text {
                    Layout.topMargin: 10
                    text: qsTr("Launch options (optional)")
                    color: C.Theme.textSoft
                    font.pixelSize: 13
                    font.weight: Font.Bold
                }

                C.Field {
                    id: options
                    Layout.fillWidth: true
                    Accessible.name: qsTr("Launch options")
                    hint: qsTr("None")
                    text: Chiaki.settings.currentProfile ? qsTr("--profile=") + Chiaki.settings.currentProfile : ""
                }

                Text {
                    Layout.topMargin: 10
                    text: qsTr("Steam folder")
                    color: C.Theme.textSoft
                    font.pixelSize: 13
                    font.weight: Font.Bold
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    // Where Steam is: shown, not typed
                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: 40
                        radius: C.Theme.radiusField
                        color: Qt.rgba(1, 1, 1, 0.03)
                        border.width: 1
                        border.color: C.Theme.line

                        C.Icon {
                            id: folderIcon
                            x: 12
                            anchors.verticalCenter: parent.verticalCenter
                            iconName: "folder"
                            size: 16
                            color: C.Theme.textSoft
                        }

                        Text {
                            anchors {
                                left: folderIcon.right
                                right: parent.right
                                verticalCenter: parent.verticalCenter
                                leftMargin: 8
                                rightMargin: 12
                            }
                            elide: Text.ElideMiddle
                            text: dialog.steamBasePath ? dialog.steamBasePath : qsTr("Found by itself")
                            color: dialog.steamBasePath ? C.Theme.text : C.Theme.textSoft
                            font.pixelSize: 14
                            font.weight: Font.DemiBold
                        }
                    }

                    C.PillButton {
                        id: choosePathButton
                        small: true
                        text: qsTr("Choose another…")
                        Accessible.name: qsTr("Choose another Steam folder")
                        lastInFocusChain: true
                        onClicked: {
                            dialog.steamBasePath = Chiaki.settings.chooseSteamBasePath();
                        }
                    }
                }
            }
        }

        // What happens while the game is added; Close (or Cancel) leaves it
        C.PopupCard {
            id: logDialog
            property bool allDone: false
            title: qsTr("Adding to Steam")
            closePolicy: QQC.Popup.NoAutoClose
            onOpened: logArea.forceActiveFocus(Qt.TabFocusReason)
            onClosed: if(succeeded) { restartDialog.open(); }

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

        // Steam shows the game only after a restart
        C.PopupCard {
            id: restartDialog
            title: qsTr("Restart Steam")
            closePolicy: QQC.Popup.NoAutoClose
            onOpened: restartArea.forceActiveFocus(Qt.TabFocusReason)
            onClosed: dialog.close()

            ColumnLayout {
                spacing: 20

                Text {
                    id: restartArea
                    Layout.preferredWidth: Math.min(implicitWidth, 460)
                    text: qsTr("%1 shows up in Steam once Steam has been restarted.").arg(name.text.trim())
                    wrapMode: Text.Wrap
                    lineHeight: 1.2
                    color: C.Theme.textSoft
                    font.pixelSize: 14
                    Keys.onReturnPressed: restartDialog.close()
                    Keys.onEscapePressed: restartDialog.close()
                }

                C.GlyphButton {
                    Layout.alignment: Qt.AlignRight
                    text: qsTr("Close")
                    primary: true
                    glyph: root.controllerButton("cross")
                    onClicked: restartDialog.close()
                }
            }
        }
    }
}
