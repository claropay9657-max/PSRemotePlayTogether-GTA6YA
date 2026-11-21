import QtQuick
import QtQuick.Layouts

import org.streetpea.chiaking

import "../controls" as C

// Away from home (PSN): logging in to PSN, and how a PSN connection gets through routers.
C.SettingsPage {
    id: page
    title: qsTr("Away from home (PSN)")
    description: qsTr("With PSN your consoles register by themselves, and you play your own console from outside your home without opening a port. Your friends do not need this to join you.")

    readonly property bool loggedIn: !!(Chiaki.settings.psnRefreshToken && Chiaki.settings.psnAuthToken
                                        && Chiaki.settings.psnAuthTokenExpiry && Chiaki.settings.psnAccountId)

    // A number in a small field, for the hole punching; saved when typed in, put back when not valid
    component NumberField: C.Field {
        id: field
        property int value
        property int maximum
        signal committed(int value)

        Layout.preferredWidth: 72
        horizontalAlignment: TextInput.AlignHCenter
        leftPadding: 8
        rightPadding: 8
        text: String(value)
        validator: IntValidator { bottom: 0; top: field.maximum }
        onEditingFinished: {
            if (acceptableInput && text.length > 0 && parseInt(text) !== value)
                committed(parseInt(text));
            text = Qt.binding(() => String(field.value));
        }
        onActiveFocusChanged: {
            if (!activeFocus)
                text = Qt.binding(() => String(field.value));
        }
        // Typing replaces the number
        onReadOnlyChanged: {
            if (!readOnly)
                selectAll();
        }
    }

    // ------------------------------------------------ the account

    Rectangle {
        id: account
        // Found by the search and kept on screen as a row
        readonly property bool isSettingRow: true
        readonly property string title: page.loggedIn ? qsTr("Log out of PSN") : qsTr("Log in to PSN")
        readonly property string subtitle: page.loggedIn ? qsTr("Logged in.") : qsTr("Not logged in.")
        readonly property string keywords: qsTr("psn playstation network account login sign in log out reset tokens remote play automatic registration")
        readonly property bool narrow: width < 560

        Layout.fillWidth: true
        implicitHeight: accountGrid.implicitHeight + 40
        radius: 20
        border.width: 1
        border.color: Qt.rgba(124 / 255, 134 / 255, 1, 0.35)
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: Qt.rgba(124 / 255, 134 / 255, 1, 0.14) }
            GradientStop { position: 1.0; color: Qt.rgba(140 / 255, 82 / 255, 1, 0.08) }
        }

        GridLayout {
            id: accountGrid
            x: 22
            y: 20
            width: parent.width - 44
            columns: account.narrow ? 2 : 3
            columnSpacing: 18
            rowSpacing: 14

            Rectangle {
                Layout.preferredWidth: 56
                Layout.preferredHeight: 56
                Layout.alignment: Qt.AlignVCenter
                radius: 16
                color: page.loggedIn ? Qt.rgba(52 / 255, 211 / 255, 153 / 255, 0.16) : Qt.rgba(124 / 255, 134 / 255, 1, 0.2)

                C.Icon {
                    anchors.centerIn: parent
                    iconName: "cloud"
                    size: 30
                    color: page.loggedIn ? "#9ff0cc" : "#c3c8ff"
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
                spacing: 3

                Text {
                    Layout.fillWidth: true
                    text: page.loggedIn ? qsTr("Logged in") : qsTr("Not logged in")
                    wrapMode: Text.WordWrap
                    color: C.Theme.text
                    font.pixelSize: 17
                    font.weight: Font.ExtraBold
                }

                Text {
                    Layout.fillWidth: true
                    text: page.loggedIn ? qsTr("Your consoles register by themselves, and “Play via PSN” on the start page works outside your home.")
                                        : qsTr("Log in once. The app keeps a token, never your password.")
                    wrapMode: Text.WordWrap
                    lineHeight: 1.15
                    color: "#c9c5e6"
                    font.pixelSize: 13
                }
            }

            C.PillButton {
                id: accountButton
                Layout.columnSpan: account.narrow ? 2 : 1
                // Under the words where it is narrow, not under the icon
                Layout.leftMargin: account.narrow ? 74 : 0
                Layout.alignment: Qt.AlignVCenter
                kind: page.loggedIn ? "ghost" : "primary"
                text: page.loggedIn ? qsTr("Log out") : qsTr("Log in to PSN")
                // On macOS and Linux the login opens in the browser
                iconName: !page.loggedIn && (Qt.platform.os === "osx" || Qt.platform.os === "linux") ? "external" : ""
                iconAfter: true
                onClicked: {
                    if (page.loggedIn) {
                        Chiaki.settings.psnRefreshToken = "";
                        Chiaki.settings.psnAuthToken = "";
                        Chiaki.settings.psnAuthTokenExpiry = "";
                        Chiaki.settings.psnAccountId = "";
                    } else {
                        root.showPSNTokenDialog(false);
                    }
                }
            }
        }
    }

    // ------------------------------------------------ how it goes

    GridLayout {
        Layout.fillWidth: true
        visible: !page.loggedIn
        columns: page.width < 520 ? 1 : 3
        columnSpacing: 12
        rowSpacing: 12

        Repeater {
            model: [
                { title: qsTr("Log in"), text: qsTr("With your PSN account, on Sony's own page.") },
                { title: qsTr("Consoles appear"), text: qsTr("Yours show up on the start page by themselves.") },
                { title: qsTr("Play from anywhere"), text: qsTr("“Play via PSN” works outside your home.") }
            ]

            Rectangle {
                id: stepCard
                required property var modelData
                required property int index

                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 100
                implicitHeight: step.implicitHeight + 28
                radius: 16
                color: C.Theme.card
                border.width: 1
                border.color: C.Theme.line

                ColumnLayout {
                    id: step
                    x: 14
                    y: 14
                    width: parent.width - 28
                    spacing: 6

                    Text {
                        text: stepCard.index + 1
                        color: C.Theme.headingSoft
                        font.pixelSize: 12
                        font.weight: Font.ExtraBold
                    }

                    Text {
                        Layout.fillWidth: true
                        text: stepCard.modelData.title
                        wrapMode: Text.WordWrap
                        color: C.Theme.text
                        font.pixelSize: 14
                        font.weight: Font.ExtraBold
                    }

                    Text {
                        Layout.fillWidth: true
                        text: stepCard.modelData.text
                        wrapMode: Text.WordWrap
                        lineHeight: 1.15
                        color: C.Theme.textDim
                        font.pixelSize: 12
                    }
                }
            }
        }
    }

    // ------------------------------------------------ hole punching

    C.SettingsGroup {
        title: qsTr("Getting through routers")
        note: qsTr("change only if connecting from outside fails")

        C.SettingsRow {
            title: qsTr("Guess the router's port")
            subtitle: qsTr("When asking for the address alone does not work.")
            keywords: qsTr("force port guessing hole punching stun nat")
            switchItem: guessing

            C.Toggle {
                id: guessing
                Accessible.name: qsTr("Guess the router's port")
                checked: Chiaki.settings.portGuessingEnabled
                onToggled: Chiaki.settings.portGuessingEnabled = checked
            }
        }

        C.SettingsRow {
            title: qsTr("Guesses and sockets")
            // Not greyed out with the switch: the app also guesses by itself behind some routers
            subtitle: qsTr("How many ports to try, and sockets to open for it. Also used when the app guesses by itself.")
            keywords: qsTr("port guesses port guess count sockets for guessing socket count hole punching 75 250")
            textMinimum: 200

            Text {
                text: qsTr("Guesses")
                color: C.Theme.text
                font.pixelSize: 14
                font.weight: Font.Bold
            }

            NumberField {
                Accessible.name: qsTr("Port guesses")
                value: Chiaki.settings.portGuessCount
                maximum: 75
                onCommitted: (value) => Chiaki.settings.portGuessCount = value
            }

            Text {
                Layout.leftMargin: 8
                text: qsTr("Sockets")
                color: C.Theme.text
                font.pixelSize: 14
                font.weight: Font.Bold
            }

            NumberField {
                Accessible.name: qsTr("Sockets for guessing")
                value: Chiaki.settings.portGuessSocketCount
                maximum: 500
                onCommitted: (value) => Chiaki.settings.portGuessSocketCount = value
            }
        }
    }
}
