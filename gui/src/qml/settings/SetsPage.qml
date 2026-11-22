import QtQuick
import QtQuick.Layouts
import QtQuick.Shapes

import org.streetpea.chiaking

import "../controls" as C

// Setting sets (the app calls them profiles): the ones on this computer with what is in each,
// switching between them, a new one, renaming and deleting one; and taking the settings to
// another computer.
C.SettingsPage {
    id: page
    title: qsTr("Setting sets")
    description: qsTr("Complete sets of settings, each with its own consoles and PSN login: for example one for each person who uses this computer.")

    /** The sets by name, "" for the default one. Chiaki.settings.profiles has "default" first and "create new profile" last. */
    readonly property var sets: {
        const all = Chiaki.settings.profiles || [];
        let names = [];
        for (let i = 0; i < all.length; ++i) {
            if ((i === 0 && all[i] === "default") || (i === all.length - 1 && all[i] === "create new profile"))
                continue;
            names.push(all[i]);
        }
        return [""].concat(names);
    }
    readonly property string current: Chiaki.settings.currentProfile || ""
    readonly property int consoleCount: (Chiaki.settings.registeredHosts || []).length
    readonly property bool psnLoggedIn: !!(Chiaki.settings.psnRefreshToken && Chiaki.settings.psnAuthToken
                                           && Chiaki.settings.psnAuthTokenExpiry && Chiaki.settings.psnAccountId)
    property bool adding: false
    /** The set being renamed, "" for none (the default set keeps its name). */
    property string renaming: ""
    /** What is wrong with the name typed for a new set; "" when it can be made. */
    readonly property string newNameProblem: nameProblem(newName.text, "")

    /** What is wrong with `typed` as the name of a set (other than `own`, its present name); " " for nothing typed. */
    function nameProblem(typed, own) {
        const name = typed.trim();
        if (name.length === 0)
            return " ";
        const lower = name.toLowerCase();
        if (lower === "default" || lower === "create new profile"
                || sets.some((s) => s !== own && s.toLowerCase() === lower))
            return qsTr("There is a set with this name already.");
        return "";
    }

    function displayName(name) {
        return name === "" ? qsTr("Default") : name;
    }

    /** What is in a set, in words: its consoles, its PSN login, streamer mode. */
    function describe(consoles, psn, streamer) {
        return (consoles === 0 ? qsTr("No consoles") : consoles === 1 ? qsTr("1 console")
                : qsTr("%1 consoles").arg(consoles)) + " · "
            + (psn ? qsTr("PSN logged in") : qsTr("PSN not logged in"))
            + (streamer ? " · " + qsTr("streamer mode on") : "");
    }

    function startRenaming(name) {
        stopAdding(false);
        renaming = name;
    }

    function stopRenaming(row, byKeys) {
        renaming = "";
        if (byKeys && row)
            Qt.callLater(() => row.focusRename());
    }

    /** Renames the set `from` to what was typed; the set in use stays in use. */
    function rename(row, from, typed, byKeys) {
        if (nameProblem(typed, from) !== "")
            return;
        const to = typed.trim();
        if (to === from) {
            stopRenaming(row, byKeys);
            return;
        }
        const index = row.index;
        renaming = "";
        if (!Chiaki.settings.renameProfile(from, to))
            root.showConfirmDialog(qsTr("Rename set"), qsTr("The set “%1” could not be renamed.").arg(from), () => {});
        if (byKeys)
            refocusRow(index, newButton);
    }

    /** After the list was made anew: the focus back to the row at `index`, or to `fallback`. */
    function refocusRow(index, fallback) {
        Qt.callLater(() => {
            const row = setRepeater.count > 0 ? setRepeater.itemAt(Math.min(index, setRepeater.count - 1)) : null;
            let target = row ? row.nextItemInFocusChain(true) : null;
            if (!target || !page.owns(target))
                target = fallback;
            if (target)
                target.forceActiveFocus(Qt.TabFocusReason);
        });
    }

    function owns(item) {
        for (let p = item; p; p = p.parent) {
            if (p === page)
                return true;
        }
        return false;
    }

    function startAdding() {
        renaming = "";
        newName.text = "";
        adding = true;
        Qt.callLater(() => {
            newName.forceActiveFocus(Qt.TabFocusReason);
            newName.readOnly = false;
        });
    }

    function stopAdding(byKeys) {
        adding = false;
        if (byKeys)
            Qt.callLater(() => newButton.forceActiveFocus(Qt.TabFocusReason));
    }

    function create(byKeys) {
        if (newNameProblem !== "")
            return;
        // Makes the set and switches to it
        Chiaki.settings.currentProfile = newName.text.trim();
        stopAdding(byKeys);
    }

    // ------------------------------------------------ the sets

    C.SettingsGroup {
        title: qsTr("On this computer")

        Repeater {
            id: setRepeater
            model: page.sets

            // One set: its icon, its name and what is in it, switch and delete on the right
            Item {
                id: setRow
                required property string modelData
                required property int index
                readonly property bool inUse: modelData === page.current
                readonly property bool renamingIt: page.renaming !== "" && page.renaming === modelData
                readonly property string setName: page.displayName(modelData)
                /** What is in it: the set in use as it is now, the others as they were saved. */
                readonly property var summary: {
                    Chiaki.settings.profiles; // read again when the sets change
                    return inUse ? null : Chiaki.settings.profileSummary(modelData);
                }

                function focusRename() {
                    renameButton.forceActiveFocus(Qt.TabFocusReason);
                }

                // For the search and the scrolling, as a SettingsRow
                readonly property bool isSettingRow: true
                readonly property string title: setName
                readonly property string subtitle: details.text
                readonly property string keywords: qsTr("profile profiles manage switch delete rename name")

                readonly property bool stacked: !renamingIt && width - 66 - 16 < 180 + 20 + controls.implicitWidth
                readonly property bool firstInColumn: {
                    const kids = parent ? parent.visibleChildren : [];
                    for (let i = 0; i < kids.length; ++i) {
                        if (kids[i].isSettingRow === true || kids[i].height > 0)
                            return kids[i] === setRow;
                    }
                    return true;
                }
                readonly property real topHeight: Math.max(36, texts.implicitHeight)

                Layout.fillWidth: true
                implicitHeight: (stacked ? topHeight + 10 + controls.implicitHeight
                                         : Math.max(topHeight, controls.implicitHeight)) + 24

                // The set in use, tinted; rounded where the card is
                Item {
                    visible: setRow.inUse
                    x: 1
                    y: setRow.firstInColumn ? 1 : 0
                    width: parent.width - 2
                    height: parent.height - y
                    clip: true

                    Rectangle {
                        y: 0
                        width: parent.width
                        height: parent.height + 20
                        radius: setRow.firstInColumn ? 17 : 0
                        color: Qt.rgba(140 / 255, 82 / 255, 1, 0.08)
                    }
                }

                Rectangle {
                    visible: !setRow.firstInColumn
                    width: parent.width
                    height: 1
                    color: Qt.rgba(1, 1, 1, 0.06)
                }

                Rectangle {
                    x: 16
                    y: setRow.stacked ? 12 + Math.round((setRow.topHeight - height) / 2) : Math.round((setRow.height - height) / 2)
                    width: 36
                    height: 36
                    radius: 10
                    color: setRow.inUse ? Qt.rgba(140 / 255, 82 / 255, 1, 0.2) : Qt.rgba(1, 1, 1, 0.06)

                    C.Icon {
                        anchors.centerIn: parent
                        iconName: "copies"
                        size: 18
                        color: setRow.inUse ? "#cdb6ff" : C.Theme.textSoft
                    }
                }

                ColumnLayout {
                    id: texts
                    x: 66
                    y: setRow.stacked ? 12 + Math.round((setRow.topHeight - implicitHeight) / 2) : Math.round((setRow.height - height) / 2)
                    width: setRow.stacked || setRow.renamingIt ? setRow.width - 66 - 16
                                                               : setRow.width - 66 - 16 - 20 - controls.implicitWidth
                    spacing: 2

                    Text {
                        Layout.fillWidth: true
                        visible: !setRow.renamingIt
                        text: setRow.setName
                        wrapMode: Text.WrapAnywhere
                        color: C.Theme.text
                        font.pixelSize: 15
                        font.weight: Font.ExtraBold
                    }

                    // The new name, typed where the name was
                    Flow {
                        Layout.fillWidth: true
                        visible: setRow.renamingIt
                        spacing: 8

                        C.Field {
                            id: renameField
                            width: Math.min(280, parent.width)
                            hint: qsTr("New name of the set")
                            Accessible.name: qsTr("New name of the set %1").arg(setRow.setName)
                            maximumLength: 40
                            // Becomes part of a file name
                            validator: RegularExpressionValidator { regularExpression: /[^\/\\:*?"<>|]*/ }
                            onAccepted: page.rename(setRow, setRow.modelData, text, true)
                            Keys.onEscapePressed: (event) => {
                                if (readOnly)
                                    page.stopRenaming(setRow, true);
                                else
                                    event.accepted = false;
                            }
                        }

                        C.PillButton {
                            kind: "primary"
                            implicitHeight: 40
                            text: qsTr("Rename")
                            enabled: page.nameProblem(renameField.text, setRow.modelData) === ""
                            onClicked: page.rename(setRow, setRow.modelData, renameField.text, visualFocus)
                        }

                        C.PillButton {
                            implicitHeight: 40
                            text: qsTr("Cancel")
                            onClicked: page.stopRenaming(setRow, visualFocus)
                        }
                    }

                    // What is in it
                    Text {
                        id: details
                        Layout.fillWidth: true
                        readonly property string problem: setRow.renamingIt
                            ? page.nameProblem(renameField.text, setRow.modelData).trim() : ""
                        visible: text.length > 0
                        text: problem.length > 0 ? problem
                              : setRow.inUse ? page.describe(page.consoleCount, page.psnLoggedIn, Chiaki.settings.streamerMode)
                              : setRow.summary && setRow.summary.consoles !== undefined
                                ? page.describe(setRow.summary.consoles, setRow.summary.psn, setRow.summary.streamerMode) : ""
                        wrapMode: Text.WordWrap
                        color: problem.length > 0 ? C.Theme.wait : C.Theme.textDim
                        font.pixelSize: 12
                    }
                }

                onRenamingItChanged: {
                    if (renamingIt) {
                        renameField.text = modelData;
                        Qt.callLater(() => {
                            renameField.forceActiveFocus(Qt.TabFocusReason);
                            renameField.readOnly = false;
                            renameField.selectAll();
                        });
                    }
                }

                RowLayout {
                    id: controls
                    visible: !setRow.renamingIt
                    x: setRow.stacked ? 66 : setRow.width - 16 - width
                    y: setRow.stacked ? 12 + setRow.topHeight + 10 : Math.round((setRow.height - height) / 2)
                    spacing: 8

                    C.Badge {
                        visible: setRow.inUse
                        tone: "good"
                        dot: true
                        text: qsTr("In use")
                    }

                    C.PillButton {
                        visible: !setRow.inUse
                        kind: "primary"
                        small: true
                        text: qsTr("Switch to it")
                        Accessible.name: qsTr("Switch to the set %1").arg(setRow.setName)
                        onClicked: {
                            const byKeys = visualFocus;
                            const index = setRow.index;
                            Chiaki.settings.currentProfile = setRow.modelData;
                            if (byKeys)
                                page.refocusRow(index, newButton);
                        }
                    }

                    // The default set keeps its name
                    C.PillButton {
                        id: renameButton
                        visible: setRow.modelData !== ""
                        small: true
                        iconName: "pencil"
                        implicitWidth: implicitHeight
                        Accessible.name: qsTr("Rename the set %1").arg(setRow.setName)
                        onClicked: page.startRenaming(setRow.modelData)
                    }

                    // The default set and the one in use cannot be deleted
                    C.PillButton {
                        visible: !setRow.inUse && setRow.modelData !== ""
                        kind: "danger"
                        small: true
                        iconName: "trash"
                        implicitWidth: implicitHeight
                        Accessible.name: qsTr("Delete the set %1").arg(setRow.setName)
                        onClicked: {
                            const byKeys = visualFocus;
                            const index = setRow.index;
                            const name = setRow.modelData;
                            root.showConfirmDialog(qsTr("Delete set"),
                                qsTr("Delete the set “%1”? Its settings, consoles and PSN login go with it.").arg(name),
                                () => {
                                    Chiaki.settings.deleteProfile(name);
                                    if (byKeys)
                                        page.refocusRow(index, newButton);
                                });
                        }
                    }
                }
            }
        }

        // A new set: the button, or the name for it
        Item {
            id: newRow
            readonly property bool isSettingRow: true
            readonly property string title: qsTr("New set")
            readonly property string subtitle: qsTr("Starts with the usual settings, and the app switches to it.")
            readonly property string keywords: qsTr("create new profile add")

            Layout.fillWidth: true
            implicitHeight: page.adding ? newForm.implicitHeight + 26 : newButton.implicitHeight + 24

            Rectangle {
                width: parent.width
                height: 1
                color: Qt.rgba(1, 1, 1, 0.06)
            }

            C.PillButton {
                id: newButton
                visible: !page.adding
                x: 16
                y: 10
                implicitHeight: 38
                small: true
                iconName: "plus"
                text: qsTr("New set")
                onClicked: page.startAdding()

                // A dashed purple outline over the button's own, as for something still to be made
                Shape {
                    anchors.fill: parent
                    layer.enabled: true
                    layer.samples: 4

                    ShapePath {
                        readonly property real r: newButton.height / 2
                        fillColor: C.Theme.card
                        strokeColor: Qt.rgba(167 / 255, 123 / 255, 1, 0.7)
                        strokeWidth: 1
                        strokeStyle: ShapePath.DashLine
                        dashPattern: [4, 3]
                        startX: r
                        startY: 0.5
                        PathLine { x: newButton.width - newButton.height / 2; y: 0.5 }
                        PathArc { x: newButton.width - newButton.height / 2; y: newButton.height - 0.5; radiusX: newButton.height / 2 - 0.5; radiusY: newButton.height / 2 - 0.5 }
                        PathLine { x: newButton.height / 2; y: newButton.height - 0.5 }
                        PathArc { x: newButton.height / 2; y: 0.5; radiusX: newButton.height / 2 - 0.5; radiusY: newButton.height / 2 - 0.5 }
                    }
                }

                Row {
                    anchors.centerIn: parent
                    spacing: 8

                    C.Icon {
                        anchors.verticalCenter: parent.verticalCenter
                        iconName: "plus"
                        size: 15
                        color: "#d8c6ff"
                    }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: newButton.text
                        color: "#d8c6ff"
                        font.pixelSize: 13
                        font.weight: Font.ExtraBold
                    }
                }
            }

            ColumnLayout {
                id: newForm
                visible: page.adding
                x: 16
                y: 13
                width: parent.width - 32
                spacing: 6

                Flow {
                    Layout.fillWidth: true
                    spacing: 8

                    C.Field {
                        id: newName
                        width: Math.min(280, parent.width)
                        hint: qsTr("Name of the new set")
                        Accessible.name: qsTr("Name of the new set")
                        maximumLength: 40
                        // Becomes part of a file name
                        validator: RegularExpressionValidator { regularExpression: /[^\/\\:*?"<>|]*/ }
                        onAccepted: page.create(true)
                        // Escape while typing stops typing (TextField.qml), then gives up the new set
                        Keys.onEscapePressed: (event) => {
                            if (readOnly)
                                page.stopAdding(true);
                            else
                                event.accepted = false;
                        }
                    }

                    C.PillButton {
                        kind: "primary"
                        implicitHeight: 40
                        text: qsTr("Make it")
                        enabled: page.newNameProblem === ""
                        onClicked: page.create(visualFocus)
                    }

                    C.PillButton {
                        implicitHeight: 40
                        text: qsTr("Cancel")
                        onClicked: page.stopAdding(visualFocus)
                    }
                }

                Text {
                    Layout.fillWidth: true
                    text: page.newNameProblem.trim().length > 0 ? page.newNameProblem : newRow.subtitle
                    wrapMode: Text.WordWrap
                    color: page.newNameProblem.trim().length > 0 ? C.Theme.wait : C.Theme.textDim
                    font.pixelSize: 12
                }
            }
        }
    }

    // ------------------------------------------------ another computer

    C.SettingsGroup {
        title: qsTr("Another computer")

        C.SettingsRow {
            title: qsTr("Take your settings along")
            subtitle: qsTr("Saves the set in use in a file you open on the other computer.")
            keywords: qsTr("export import backup settings file ini")

            C.PillButton {
                small: true
                text: qsTr("Save to a file…")
                onClicked: Chiaki.settings.exportSettings()
            }

            C.PillButton {
                small: true
                text: qsTr("Open a file…")
                onClicked: Chiaki.settings.importSettings()
            }
        }
    }
}
