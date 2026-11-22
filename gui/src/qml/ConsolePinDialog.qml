import QtQuick
import QtQuick.Layouts

import org.streetpea.chiaking

import "controls" as C

// The passcode of the user on a console, saved so that the app types it in on connecting.
DialogView {
    property var consoleIndex
    /** A registered console by its ID: works while it is away from this network too. */
    property string consoleMac: ""
    title: qsTr("Console login PIN")
    buttonText: qsTr("Save")
    buttonEnabled: pin.acceptableInput
    onAccepted: {
        if (consoleMac !== "")
            Chiaki.setConsolePinForMac(consoleMac, pin.text.trim());
        else
            Chiaki.setConsolePin(consoleIndex, pin.text.trim());
        stack.pop();
    }

    Item {
        id: page

        C.FormCard {
            title: qsTr("Log in without typing")
            text: qsTr("If your user on the console is protected with a 4-digit passcode, the console asks for it every time you connect. Save it here and the app enters it for you.")
            footnote: qsTr("This is the passcode of your PlayStation user, not the 8-digit number used to register the console.")

            ColumnLayout {
                width: Math.min(512, page.width - 80)
                spacing: 6

                Text {
                    text: qsTr("Login PIN")
                    color: C.Theme.textSoft
                    font.pixelSize: 13
                    font.weight: Font.Bold
                }

                C.Field {
                    id: pin
                    Layout.fillWidth: true
                    Accessible.name: qsTr("Login PIN")
                    hint: qsTr("4 digits")
                    validator: RegularExpressionValidator { regularExpression: /[0-9]{4}/ }
                    inputMethodHints: Qt.ImhDigitsOnly
                    echoMode: Chiaki.settings.streamerMode ? TextInput.Password : TextInput.Normal
                    firstInFocusChain: true
                    lastInFocusChain: true
                }
            }
        }
    }
}
