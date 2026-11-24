import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC

import org.streetpea.chiaking

import "../controls" as C

// Friends & chats: switched on or off on this computer, what friends see of you, the
// friend link, the backup file, the friends server and who is blocked. The friends
// themselves and the chats are in the friends panel (FriendsPanel.qml).
C.SettingsPage {
    id: page
    title: qsTr("Friends & chats")
    description: qsTr("Friends see whether you are online and what you play, and chat with you outside of sessions. End-to-end encrypted, without an account.")

    readonly property var fr: Chiaki.friends
    readonly property bool on: !!fr && fr.enabled
    readonly property bool twoColumns: width >= 760

    /** What the passphrase card is asked for: "save" or "restore". */
    property string backupMode: ""
    property string backupResult: ""

    function askBackup(mode) {
        backupMode = mode;
        backupResult = "";
        passOne.text = "";
        passTwo.text = "";
        passCard.open();
    }

    function runBackup() {
        const pass = passOne.text;
        if (pass.length < 8) {
            backupResult = qsTr("The passphrase needs at least 8 characters.");
            return;
        }
        if (backupMode === "save" && pass !== passTwo.text) {
            backupResult = qsTr("The two passphrases are not the same.");
            return;
        }
        passCard.close();
        const result = backupMode === "save" ? Chiaki.friendsSaveBackup(pass) : Chiaki.friendsRestoreBackup(pass);
        if (result === "cancel")
            return;
        if (result.length > 0)
            root.showInfoDialog(backupMode === "save" ? qsTr("Backup not saved") : qsTr("Backup not restored"), result);
        else
            root.showInfoDialog(backupMode === "save" ? qsTr("Backup saved") : qsTr("Friends restored"),
                backupMode === "save"
                    ? qsTr("Keep the file and the passphrase apart. With both, anybody can be you to your friends.")
                    : qsTr("This computer is now one of your devices. Your friends get to know it the next time they write."));
    }

    GridLayout {
        id: columns
        Layout.fillWidth: true
        columns: page.twoColumns ? 2 : 1
        columnSpacing: 20
        rowSpacing: 18

        // ------------------------------------------------ left
        ColumnLayout {
            Layout.alignment: Qt.AlignTop
            Layout.fillWidth: true
            Layout.preferredWidth: page.twoColumns ? (columns.width - columns.columnSpacing) / 2 : columns.width
            spacing: 18

            C.SettingsGroup {
                title: qsTr("Friends")

                C.SettingsRow {
                    title: qsTr("Friends and chats on this computer")
                    subtitle: page.on ? qsTr("Your keys and chats are kept on this computer, sealed.")
                                      : qsTr("Switched off: no friends server is asked.")
                    keywords: qsTr("friends chat status online enable switch")
                    switchItem: onToggle

                    C.Toggle {
                        id: onToggle
                        Accessible.name: qsTr("Friends and chats on this computer")
                        checked: page.on
                        onToggled: {
                            if (checked) {
                                page.fr.enable();
                                return;
                            }
                            checked = true;
                            root.showConfirmDialog(qsTr("Switch off friends on this computer?"),
                                qsTr("Your friends, chats and keys on this computer are deleted. Your friends keep you in their list, but this computer no longer gets messages. Save a backup first to keep them."),
                                () => page.fr.disable());
                        }
                    }
                }

                C.SettingsRow {
                    visible: !page.on
                    title: qsTr("Restore from a backup")
                    subtitle: qsTr("Your friends and keys from a backup file, with its passphrase.")
                    keywords: qsTr("backup restore import")

                    C.PillButton {
                        small: true
                        iconName: "upload"
                        text: qsTr("Restore…")
                        onClicked: page.askBackup("restore")
                    }
                }

                C.SettingsRow {
                    visible: !page.on
                    title: qsTr("Link to my other device")
                    subtitle: qsTr("Your friends from a computer or browser that has them: there, choose Link another device, and paste its link here.")
                    keywords: qsTr("link device second computer browser")

                    C.Field {
                        id: joinField
                        Layout.preferredWidth: 220
                        Layout.fillWidth: true
                        hint: "psrpt://link/…"
                        Accessible.name: qsTr("The link from the other device")
                    }

                    C.PillButton {
                        small: true
                        kind: "primary"
                        enabled: joinField.text.trim().length > 0
                        text: qsTr("Link")
                        onClicked: {
                            page.fr.joinLink(joinField.text.trim());
                            linkCard.open();
                        }
                    }
                }
            }

            C.SettingsGroup {
                title: qsTr("What friends see")
                enabled: page.on

                C.SettingsRow {
                    title: qsTr("Show my status")
                    subtitle: qsTr("Online, away or busy, and when you are free. Off: you look offline to everybody and still see your friends.")
                    keywords: qsTr("status online invisible privacy")
                    switchItem: statusToggle

                    C.Toggle {
                        id: statusToggle
                        Accessible.name: qsTr("Show my status")
                        checked: !page.fr || page.fr.showStatus !== "nobody"
                        onToggled: page.fr.showStatus = checked ? "all" : "nobody"
                    }
                }

                C.SettingsRow {
                    title: qsTr("Show what I play")
                    subtitle: qsTr("The game and the console, e.g. “Playing Astro Bot on PS5”.")
                    keywords: qsTr("game activity playing privacy")
                    switchItem: gameToggle

                    C.Toggle {
                        id: gameToggle
                        Accessible.name: qsTr("Show what I play")
                        checked: !page.fr || page.fr.showGame
                        onToggled: page.fr.showGame = checked
                    }
                }

                C.SettingsRow {
                    title: qsTr("Read receipts")
                    subtitle: qsTr("Friends see when you have read their messages, and you see when they have read yours. Off: neither.")
                    keywords: qsTr("read receipts seen privacy")
                    switchItem: readToggle

                    C.Toggle {
                        id: readToggle
                        Accessible.name: qsTr("Read receipts")
                        checked: !page.fr || page.fr.readReceipts
                        onToggled: page.fr.readReceipts = checked
                    }
                }

                C.SettingsRow {
                    title: qsTr("Friends can join my sessions")
                    subtitle: qsTr("While you host, friends see your session with Join. They come in as the session allows: asking first, as viewers, or to play.")
                    keywords: qsTr("join session room invite friends")
                    switchItem: joinToggle

                    C.Toggle {
                        id: joinToggle
                        Accessible.name: qsTr("Friends can join my sessions")
                        checked: !page.fr || page.fr.friendsJoin !== "never"
                        onToggled: page.fr.friendsJoin = checked ? "always" : "never"
                    }
                }
            }

            C.SettingsGroup {
                title: qsTr("Your friend link")
                enabled: page.on

                C.SettingsRow {
                    title: qsTr("New friend link")
                    subtitle: qsTr("The link you gave out stops working, so nobody can send you requests with it any more. Your friends stay.")
                    keywords: qsTr("link new reset spam requests")

                    C.PillButton {
                        small: true
                        iconName: "refresh"
                        text: qsTr("New link…")
                        onClicked: root.showConfirmDialog(qsTr("Make a new friend link?"),
                            qsTr("Whoever has your old link can no longer send you a request. Your friends stay your friends."),
                            () => page.fr.newLink())
                    }
                }
            }
        }

        // ------------------------------------------------ right
        ColumnLayout {
            Layout.alignment: Qt.AlignTop
            Layout.fillWidth: true
            Layout.preferredWidth: page.twoColumns ? (columns.width - columns.columnSpacing) / 2 : columns.width
            spacing: 18

            C.SettingsGroup {
                title: qsTr("Your devices")
                enabled: page.on
                visible: page.on
                note: qsTr("Every device with your friends on it gets your chats. Removing one stops new messages going there.")

                Repeater {
                    model: page.fr ? page.fr.devices : []

                    delegate: C.SettingsRow {
                        required property var modelData
                        title: modelData.me ? qsTr("This computer") : modelData.kind === 2 ? qsTr("A web browser") : qsTr("The app on another computer")
                        subtitle: modelData.me ? qsTr("The app, here") : qsTr("Device %1").arg(modelData.id.substring(0, 6))
                        textMinimum: 140

                        C.TextLink {
                            visible: !modelData.me
                            text: qsTr("Remove…")
                            onClicked: root.showConfirmDialog(qsTr("Remove this device?"),
                                qsTr("Your friends no longer send to it. A device that still has your keys could put itself back: for a lost one, a backup on a new device and a new friend link are safer."),
                                () => page.fr.removeDevice(modelData.id))
                        }
                    }
                }

                C.SettingsRow {
                    title: qsTr("Link another device")
                    subtitle: qsTr("Your friends and keys on one more computer or browser, without a backup file.")
                    keywords: qsTr("link device second computer browser phone")

                    C.PillButton {
                        small: true
                        iconName: "link"
                        text: qsTr("Link…")
                        onClicked: {
                            page.fr.startLinking();
                            linkCard.open();
                        }
                    }
                }
            }

            C.SettingsGroup {
                title: qsTr("Backup")
                enabled: page.on
                note: qsTr("Your friends are tied to keys on your devices, not to an account. Without a backup they are gone with this computer.")

                C.SettingsRow {
                    title: qsTr("Save a backup")
                    subtitle: qsTr("A file with your keys and friends, sealed with a passphrase. Restoring it makes another computer one of your devices.")
                    keywords: qsTr("backup export save keys")

                    C.PillButton {
                        small: true
                        iconName: "download"
                        text: qsTr("Save…")
                        onClicked: page.askBackup("save")
                    }
                }
            }

            C.SettingsGroup {
                title: qsTr("Blocked")
                enabled: page.on
                visible: page.on

                QQC.Label {
                    textFormat: Text.PlainText
                    readonly property bool isSettingRow: true
                    Layout.fillWidth: true
                    Layout.margins: 18
                    visible: !page.fr || page.fr.blocked.length === 0
                    wrapMode: Text.WordWrap
                    font.pixelSize: 13
                    color: C.Theme.textDim
                    text: qsTr("Nobody. Blocked players cannot send you messages or requests.")
                }

                Repeater {
                    model: page.fr ? page.fr.blocked : []

                    delegate: C.SettingsRow {
                        required property var modelData
                        title: modelData.name || qsTr("A player without a name")
                        textMinimum: 120

                        C.TextLink {
                            text: qsTr("Unblock")
                            onClicked: page.fr.unblock(modelData.fp)
                        }
                    }
                }
            }

            C.SettingsGroup {
                title: qsTr("Friends server")
                enabled: page.on

                C.SettingsRow {
                    id: serverRow
                    property bool editing: false
                    readonly property bool own: !!page.fr && page.fr.server.length > 0
                    title: own || editing ? qsTr("Your own server") : qsTr("The default server")
                    subtitle: own || editing ? qsTr("You and your friends need the same one. Changing it starts anew there: send your friends your link again.")
                                             : page.fr ? page.fr.defaultServer : ""
                    keywords: qsTr("server address self hosted php")
                    textMinimum: 150

                    C.TextLink {
                        visible: !serverRow.own && !serverRow.editing
                        text: qsTr("Use my own…")
                        onClicked: {
                            serverRow.editing = true;
                            serverField.forceActiveFocus(Qt.TabFocusReason);
                            serverField.readOnly = false;
                        }
                    }

                    C.Field {
                        id: serverField
                        visible: serverRow.own || serverRow.editing
                        Layout.preferredWidth: 300
                        Layout.fillWidth: true
                        Accessible.name: qsTr("Friends server address")
                        hint: "https://example.org/psrpt_friends.php"
                        text: page.fr ? page.fr.server : ""
                        onEditingFinished: {
                            const address = text.trim();
                            if (address !== page.fr.server)
                                page.fr.server = address;
                        }
                    }

                    C.TextLink {
                        visible: serverRow.own || serverRow.editing
                        text: qsTr("Default")
                        onClicked: {
                            serverRow.editing = false;
                            serverField.text = "";
                            page.fr.server = "";
                        }
                    }
                }
            }
        }
    }

    // Linking a device, on either side: the link, the code to compare, done
    C.PopupCard {
        id: linkCard
        readonly property string linkState: page.fr ? page.fr.linkState : ""
        /** This side has the friends (it shows the link and sends). */
        readonly property bool giving: !!page.fr && page.fr.linkText.length > 0 || linkState === "sending"
        title: linkState === "done" ? qsTr("Linked") : linkState === "failed" ? qsTr("Not linked") : qsTr("Link a device")
        width: Math.min(480, page.width)
        onClosed: {
            if (linkState !== "done" && linkState !== "failed" && page.fr)
                page.fr.cancelLinking();
        }

        ColumnLayout {
            width: parent ? parent.width : 420
            spacing: 12

            QQC.Label {
                textFormat: Text.PlainText
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                font.pixelSize: 13
                color: C.Theme.textSoft
                text: {
                    switch (linkCard.linkState) {
                    case "waiting":
                        return linkCard.giving
                            ? qsTr("On the other device: Settings → Friends & chats → Link to my other device, and paste this link. In a browser, the friends panel offers it too. The link works for two minutes.")
                            : qsTr("Waiting for the other device…");
                    case "code":
                        return linkCard.giving
                            ? qsTr("Does the other device show the same six digits? Only then send: whoever has these digits gets your friends and keys.")
                            : qsTr("The other device shows six digits too. If they are the same, confirm there.");
                    case "sending":
                        return qsTr("Sending, sealed for the other device only…");
                    case "receiving":
                        return qsTr("Your friends are on their way…");
                    case "done":
                        return linkCard.giving ? qsTr("The other device has your friends now. It gets your chats from here on.")
                                               : qsTr("This computer has your friends now.");
                    case "failed":
                        return page.fr ? page.fr.problem : "";
                    }
                    return "";
                }
            }

            RowLayout {
                Layout.fillWidth: true
                visible: linkCard.linkState === "waiting" && linkCard.giving
                spacing: 8

                C.Field {
                    Layout.fillWidth: true
                    readOnly: true
                    text: page.fr ? page.fr.linkText : ""
                    Accessible.name: qsTr("The link for the other device")
                }

                C.PillButton {
                    small: true
                    iconName: "copy"
                    text: linkCopied.running ? qsTr("Copied") : qsTr("Copy")
                    onClicked: {
                        linkClipboard.text = page.fr.linkText;
                        linkClipboard.selectAll();
                        linkClipboard.copy();
                        linkClipboard.text = "";
                        linkCopied.restart();
                    }
                }

                TextEdit {
                    id: linkClipboard
                    visible: false
                }

                Timer {
                    id: linkCopied
                    interval: 1800
                }
            }

            // The digits, large: compared by eye
            QQC.Label {
                textFormat: Text.PlainText
                Layout.alignment: Qt.AlignHCenter
                visible: linkCard.linkState === "code"
                font.pixelSize: 40
                font.weight: Font.ExtraBold
                font.letterSpacing: 4
                color: C.Theme.focus
                text: page.fr ? page.fr.linkCode : ""
            }

            QQC.BusyIndicator {
                Layout.alignment: Qt.AlignHCenter
                visible: linkCard.linkState === "sending" || linkCard.linkState === "receiving" || (linkCard.linkState === "waiting" && !linkCard.giving)
                running: visible
            }

            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: 8

                C.PillButton {
                    text: linkCard.linkState === "done" || linkCard.linkState === "failed" ? qsTr("Close") : qsTr("Cancel")
                    onClicked: linkCard.close()
                }

                C.PillButton {
                    visible: linkCard.linkState === "code" && linkCard.giving
                    kind: "primary"
                    text: qsTr("Same digits: send")
                    onClicked: page.fr.confirmLinking()
                }
            }
        }
    }

    // The passphrase for saving or opening a backup
    C.PopupCard {
        id: passCard
        title: page.backupMode === "save" ? qsTr("Passphrase for the backup") : qsTr("Passphrase of the backup")
        width: Math.min(460, page.width)

        ColumnLayout {
            width: parent ? parent.width : 400
            spacing: 10

            QQC.Label {
                textFormat: Text.PlainText
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                font.pixelSize: 13
                color: C.Theme.textSoft
                text: page.backupMode === "save"
                      ? qsTr("At least 8 characters. Nobody can open the backup without it, not even you: write it down somewhere safe.")
                      : qsTr("The passphrase the backup was saved with. Then choose the file.")
            }

            C.Field {
                id: passOne
                Layout.fillWidth: true
                echoMode: TextInput.Password
                hint: qsTr("Passphrase")
                Accessible.name: qsTr("Passphrase")
            }

            C.Field {
                id: passTwo
                Layout.fillWidth: true
                visible: page.backupMode === "save"
                echoMode: TextInput.Password
                hint: qsTr("The same once more")
                Accessible.name: qsTr("The passphrase once more")
            }

            QQC.Label {
                textFormat: Text.PlainText
                Layout.fillWidth: true
                visible: page.backupResult.length > 0
                wrapMode: Text.WordWrap
                font.pixelSize: 12
                color: C.Theme.bad
                text: page.backupResult
            }

            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: 8

                C.PillButton {
                    text: qsTr("Cancel")
                    onClicked: passCard.close()
                }

                C.PillButton {
                    kind: "primary"
                    text: page.backupMode === "save" ? qsTr("Choose where to save…") : qsTr("Choose the file…")
                    onClicked: page.runBackup()
                }
            }
        }
    }
}
