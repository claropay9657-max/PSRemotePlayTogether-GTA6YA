import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.Material

import org.streetpea.chiaking

import "controls" as C

DialogView {
    property var consoleIndex
    title: qsTr("Console login PIN")
    buttonText: qsTr("Save")
    buttonEnabled: pin.acceptableInput
    onAccepted: {
        Chiaki.setConsolePin(consoleIndex, pin.text.trim());
        stack.pop();
    }
    Item {
        C.FormCard {
            title: qsTr("Log in without typing")
            text: qsTr("If your user on the console is protected with a 4-digit passcode, the console asks for it every time you connect. Save it here and the app enters it for you.")
            footnote: qsTr("This is the passcode of your PlayStation user, not the 8-digit number used to register the console.")

            GridLayout {
                columns: 2
                rowSpacing: 10
                columnSpacing: 20

                Label {
                    Layout.alignment: Qt.AlignRight
                    text: qsTr("Login PIN (4 digits):")
                }

                C.TextField {
                    id: pin
                    validator: RegularExpressionValidator { regularExpression: /[0-9]{4}/ }
                    echoMode: Chiaki.settings.streamerMode ? TextInput.Password : TextInput.Normal
                    Layout.preferredWidth: 400
                    firstInFocusChain: true
                    lastInFocusChain: true
                }
            }
        }
    }
}
