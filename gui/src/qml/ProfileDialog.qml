import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.Material

import org.streetpea.chiaking

import "controls" as C

DialogView {
    title: qsTr("Profiles")

    buttonText: {
        if(deleteBox.visible && deleteBox.checked)
            qsTr("Delete Profile")
        else if(profileName.visible)
            qsTr("Create Profile")
        else
            qsTr("Switch Profile")
    }
    buttonEnabled: {
        if(deleteBox.visible && deleteBox.checked)
            true
        else if(profileName.visible)
            profileName.text.trim();
        else
            !(profileComboBox.model[profileComboBox.currentIndex] == "default" && Chiaki.settings.currentProfile == "") && profileComboBox.model[profileComboBox.currentIndex] != Chiaki.settings.currentProfile
    }
    onAccepted: {
        if(deleteBox.visible && deleteBox.checked)
            Chiaki.settings.deleteProfile(profileComboBox.model[profileComboBox.currentIndex])
        else if(profileName.visible)
            Chiaki.settings.currentProfile = profileName.text.trim()
        else
            Chiaki.settings.currentProfile = profileComboBox.model[profileComboBox.currentIndex] == "default" ? "" : profileComboBox.model[profileComboBox.currentIndex]
        stack.pop()
        
    }

    Item {
        C.FormCard {
            title: qsTr("Profiles")
            text: qsTr("A profile is a complete set of settings with its own consoles and PSN login, for example one per person using this computer.")

        GridLayout {
            columns: 2
            rowSpacing: 10
            columnSpacing: 20

            Label {
                Layout.alignment: Qt.AlignRight
                text: qsTr("Profile:")
            }

            C.ComboBox {
                id: profileComboBox
                Layout.preferredWidth: 400
                firstInFocusChain: true
                model: Chiaki.settings.profiles
                currentIndex: Math.max(0, model.indexOf(Chiaki.settings.currentProfile))
                lastInFocusChain: model[currentIndex] == "default" || model[currentIndex] == Chiaki.settings.currentProfile
            }

            Label {
                Layout.alignment: Qt.AlignRight
                text: qsTr("Name of the new profile:")
                visible: profileComboBox.currentIndex == profileComboBox.model.indexOf("create new profile")
            }

            C.TextField {
                id: profileName
                visible: profileComboBox.currentIndex == profileComboBox.model.indexOf("create new profile")
                Layout.preferredWidth: 400
                lastInFocusChain: true
            }

            Label {
                Layout.alignment: Qt.AlignRight
                text: qsTr("Delete this profile:")
                visible: profileComboBox.model[profileComboBox.currentIndex] != "default" && profileComboBox.model[profileComboBox.currentIndex] != "create new profile" && profileComboBox.model[profileComboBox.currentIndex] != Chiaki.settings.currentProfile
            }

            C.CheckBox {
                id: deleteBox
                visible: profileComboBox.model[profileComboBox.currentIndex] != "default" && profileComboBox.model[profileComboBox.currentIndex] != "create new profile" && profileComboBox.model[profileComboBox.currentIndex] != Chiaki.settings.currentProfile
                lastInFocusChain: true
            }
        }
        }
    }
}
