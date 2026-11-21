import QtQuick
import QtQuick.Layouts

import org.streetpea.chiaking

import "controls" as C

// A console that is not found by itself, added by its address.
DialogView {
    title: qsTr("Add a console by address")
    buttonText: qsTr("Add")
    buttonEnabled: hostField.text.trim() && consoleCombo.currentIndex >= 0
    onAccepted: {
        Chiaki.addManualHost(consoleCombo.model[consoleCombo.currentIndex].index, hostField.text.trim());
        close();
    }

    Item {
        id: page

        C.FormCard {
            title: qsTr("A console somewhere else")
            text: qsTr("For a console that is not found by itself: one in another network, behind a VPN, or reached over the internet. Enter its IP address or host name.")
            footnote: Chiaki.settings.registeredHosts.length > 0
                      ? qsTr("Pick it from your registered consoles if it is one of them. One that is not registered yet is registered the first time you connect.")
                      : qsTr("It is registered the first time you connect.")

            ColumnLayout {
                width: Math.min(512, page.width - 80)
                spacing: 6

                Text {
                    text: qsTr("Address")
                    color: C.Theme.textSoft
                    font.pixelSize: 13
                    font.weight: Font.Bold
                }

                C.Field {
                    id: hostField
                    Layout.fillWidth: true
                    Accessible.name: qsTr("Address")
                    hint: qsTr("IP address or host name")
                    echoMode: Chiaki.settings.streamerMode ? TextInput.Password : TextInput.Normal
                    firstInFocusChain: true
                }

                Text {
                    Layout.topMargin: 10
                    text: qsTr("Which console it is")
                    color: C.Theme.textSoft
                    font.pixelSize: 13
                    font.weight: Font.Bold
                }

                C.Dropdown {
                    id: consoleCombo
                    Layout.fillWidth: true
                    Accessible.name: qsTr("Which console it is")
                    lastInFocusChain: true
                    textRole: "name"
                    // With registered consoles nothing is chosen at first: the hint asks for one
                    currentIndex: Chiaki.settings.registeredHosts.length > 0 ? -1 : 0
                    displayText: currentIndex < 0 ? "" : currentText
                    // Also when chosen with the keys, which only report the choice
                    onActivated: (index) => currentIndex = index
                    model: {
                        let m = [];
                        let i = 0;
                        for (; i < Chiaki.settings.registeredHosts.length; ++i) {
                            let host = Chiaki.settings.registeredHosts[i];
                            m.push({
                                name: Chiaki.settings.streamerMode ? host.name : "%1 (%2)".arg(host.name).arg(host.mac),
                                index: i,
                            });
                        }
                        m.push({
                            name: qsTr("Not registered yet: register it on connecting"),
                            index: i,
                        });
                        return m;
                    }

                    Text {
                        anchors {
                            fill: parent
                            leftMargin: 12
                            rightMargin: 36
                        }
                        visible: consoleCombo.currentIndex < 0
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                        text: qsTr("Choose a console")
                        color: C.Theme.textDim
                        font: consoleCombo.font
                    }
                }
            }
        }
    }
}
