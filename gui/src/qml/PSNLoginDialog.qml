import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC

import org.streetpea.chiaking

import "controls" as C

// The PSN account ID for registering a console (RegistDialog), handed to `callback`.
// With `login`: by logging in to PSN, on Sony's page inside the app where the app has
// a web view (Windows); otherwise, and always on macOS and Linux, in the browser, with
// the address the browser lands on pasted back here. Without: looked up by the online ID.
DialogView {
    id: dialog
    property var callback: null
    property bool login
    property bool submitting: false
    property bool closing: false
    property var psnurl: ""
    /** "web": Sony's page in the app; "browser": in the web browser; "lookup": by the
     *  online ID. Empty until shown. */
    property string mode
    /** Why getting the account ID failed, on Sony's page or in the browser. */
    property string webError
    property string browserError
    /** Looking up an online ID; what went wrong with the last one. */
    property bool looking: false
    property string lookupError
    title: login ? qsTr("Log in to PSN") : qsTr("Look up your account ID")
    buttonVisible: false
    buttonText: qsTr("Get account ID")
    buttonEnabled: !submitting && url.text.trim()
    onAccepted: {
        submitting = true;
        Chiaki.handlePsnLoginRedirect(url.text.trim());
    }
    QQC.StackView.onActivated: {
        if(login)
        {
            if (!mode) {
                mode = "web";
                nativeLoginForm.forceActiveFocus(Qt.TabFocusReason);
            }
            if(Qt.platform.os == "linux" || Qt.platform.os == "osx")
                useBrowser();
        }
        else
        {
            mode = "lookup";
            usernameField.forceActiveFocus(Qt.TabFocusReason)
            usernameField.readOnly = false;
            Qt.inputMethod.show();
        }
    }
    function close() {
        if(webView.web)
        {
            dialog.closing = true;
            if(Chiaki.settings.remotePlayAsk)
                reloadTimer.start();
            else
                cacheClearTimer.start();
        }
        else
            root.closeDialog();
    }
    /** Opens Sony's page in the browser, once, and waits for the address pasted back. */
    function useBrowser() {
        if (mode !== "browser") {
            mode = "browser";
            webError = "";
            dialog.buttonVisible = true;
            psnurl = Chiaki.openPsnLink();
            if(psnurl)
            {
                openurl.selectAll();
                openurl.copy();
            }
        }
        pasteUrl.forceActiveFocus(Qt.TabFocusReason);
    }

    component FieldLabel: Text {
        color: C.Theme.textSoft
        font.pixelSize: 13
        font.weight: Font.Bold
    }

    /** What went wrong, in red. */
    component ErrorNote: Rectangle {
        id: note
        property alias text: noteText.text
        Layout.fillWidth: true
        implicitHeight: noteText.implicitHeight + 20
        radius: 12
        color: Qt.rgba(240 / 255, 70 / 255, 110 / 255, 0.12)
        border.width: 1
        border.color: Qt.rgba(240 / 255, 70 / 255, 110 / 255, 0.45)

        Text {
            id: noteText
            anchors {
                left: parent.left
                right: parent.right
                verticalCenter: parent.verticalCenter
                leftMargin: 12
                rightMargin: 12
            }
            wrapMode: Text.WordWrap
            color: "#ffd3de"
            font.pixelSize: 13
        }
    }

    Item {
        id: page
        readonly property int gutter: width < 600 ? 16 : 32

        // ------------------------------------------------ Sony's page in the app
        Item {
            id: nativeLoginForm
            Keys.onPressed: (event) => {
                if (event.modifiers)
                    return;
                switch (event.key) {
                case Qt.Key_PageUp:
                    reloadButton.clicked();
                    event.accepted = true;
                    break;
                case Qt.Key_PageDown:
                    extBrowserButton.clicked();
                    event.accepted = true;
                    break;
                }
            }
            anchors.fill: parent
            visible: dialog.mode === "web"

            // L1 and R1 (Page Up, Page Down) press these
            Item {
                id: psnLoginToolbar
                anchors {
                    top: parent.top
                    left: parent.left
                    right: parent.right
                }
                height: 64

                RowLayout {
                    anchors {
                        fill: parent
                        leftMargin: page.gutter
                        rightMargin: page.gutter
                    }
                    spacing: 16

                    C.GlyphButton {
                        id: reloadButton
                        glyph: Chiaki.controllers.length > 0 ? "qrc:/icons/l1.svg" : ""
                        text: qsTr("Clear and reload")
                        onClicked: reloadTimer.start()
                    }

                    // How far the page has loaded
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 4
                        radius: 2
                        color: Qt.rgba(1, 1, 1, 0.08)
                        opacity: webView.web && webView.web.loadProgress < 100 ? 1 : 0.4

                        Rectangle {
                            width: parent.width * (webView.web ? webView.web.loadProgress : 0) / 100
                            height: parent.height
                            radius: 2
                            gradient: Gradient {
                                orientation: Gradient.Horizontal
                                GradientStop { position: 0.0; color: C.Theme.brandFrom }
                                GradientStop { position: 1.0; color: C.Theme.brandTo }
                            }
                        }
                    }

                    C.GlyphButton {
                        id: extBrowserButton
                        glyph: Chiaki.controllers.length > 0 ? "qrc:/icons/r1.svg" : ""
                        text: qsTr("Use my browser instead")
                        onClicked: dialog.useBrowser()
                    }
                }
            }
            Timer {
                id: reloadTimer
                interval: 0
                running: false
                onTriggered: {
                    Chiaki.clearCookies(webView.web.profile);
                    webView.web.profile.clearHttpCache();
                }
            }

            Timer {
                id: cacheClearTimer
                interval: 0
                running: false
                onTriggered: {
                    webView.web.profile.clearHttpCache();
                }
            }

            // When Sony's page did not give the account ID
            Item {
                anchors {
                    top: psnLoginToolbar.bottom
                    bottom: parent.bottom
                    left: parent.left
                    right: parent.right
                }
                visible: dialog.webError.length > 0

                C.FormCard {
                    title: qsTr("Getting your account ID did not work")
                    text: dialog.webError

                    C.PillButton {
                        kind: "primary"
                        iconName: "refresh"
                        text: qsTr("Try again")
                        firstInFocusChain: true
                        lastInFocusChain: true
                        onClicked: {
                            dialog.webError = "";
                            webView.web.url = Chiaki.psnLoginUrl();
                        }
                    }
                }
            }

            Item {
                id: webView
                property Item web: null
                visible: dialog.webError.length === 0
                anchors {
                    top: psnLoginToolbar.bottom
                    bottom: parent.bottom
                    left: parent.left
                    right: parent.right
                    leftMargin: page.gutter
                    rightMargin: page.gutter
                    bottomMargin: 16
                }
                Component.onCompleted: {
                    try {
                        web = Qt.createQmlObject("
                        import QtWebEngine
                        import org.streetpea.chiaking
                        WebEngineView {
                            profile {
                                offTheRecord: false
                                storageName: 'psn-token'
                                onClearHttpCacheCompleted: {
                                    if(dialog.closing)
                                        root.closeDialog();
                                    else
                                        webView.web.reload();
                                }
                            }
                            settings {
                                // Load larger touch icons
                                touchIconsEnabled: true
                            }

                            onContextMenuRequested: (request) => request.accepted = true;
                            onNavigationRequested: (request) => {
                                if (Chiaki.checkPsnRedirectURL(request.url)) {
                                    Chiaki.handlePsnLoginRedirect(request.url)
                                    request.reject();
                                }
                                else
                                    request.accept();
                            }
                            onCertificateError: console.error(error.description);
                        }", webView, "webView");
                        Chiaki.setWebEngineHints(web.profile);
                        webView.web.url = Chiaki.psnLoginUrl();
                        web.anchors.fill = webView;
                    } catch (error) {
                        console.error('Create webengine view failed with error:' + error);
                        // Looking up by the online ID needs no log-in
                        if (dialog.login)
                            dialog.useBrowser();
                    }
                }
            }
        }

        // ------------------------------------------------ in the browser
        Item {
            id: loginForm
            anchors.fill: parent
            visible: dialog.mode === "browser"

            C.FormCard {
                title: qsTr("Log in with your browser")
                text: qsTr("Log in on Sony's page in your browser. Afterwards the browser lands on a page whose address starts with remoteplay.dl.playstation.net. Copy that whole address and paste it here.")
                footnote: qsTr("Then press “%1” at the top.").arg(dialog.buttonText)

                ColumnLayout {
                    width: Math.min(512, loginForm.width - 80)
                    spacing: 6

                    ErrorNote {
                        Layout.bottomMargin: 10
                        visible: dialog.browserError.length > 0
                        text: qsTr("Getting your account ID did not work: %1").arg(dialog.browserError)
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.bottomMargin: 10
                        visible: !!dialog.psnurl
                        spacing: 12

                        C.Icon {
                            Layout.alignment: Qt.AlignVCenter
                            iconName: "browser"
                            size: 18
                            color: C.Theme.headingSoft
                        }

                        Text {
                            Layout.fillWidth: true
                            text: qsTr("Sony's page opened in your browser. If it did not, paste its address there: it is copied already.")
                            wrapMode: Text.WordWrap
                            lineHeight: 1.15
                            color: C.Theme.textSoft
                            font.pixelSize: 13
                        }

                        C.PillButton {
                            id: copyUrl
                            small: true
                            iconName: "copy"
                            text: qsTr("Copy again")
                            Accessible.name: qsTr("Copy the address again")
                            firstInFocusChain: true
                            onClicked: {
                                openurl.selectAll()
                                openurl.copy()
                            }
                        }
                    }

                    FieldLabel {
                        text: qsTr("The address you land on after logging in")
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        C.Field {
                            id: url
                            Layout.fillWidth: true
                            Accessible.name: qsTr("The address you land on after logging in")
                            hint: "https://remoteplay.dl.playstation.net/…"
                            echoMode: Chiaki.settings.streamerMode ? TextInput.Password : TextInput.Normal
                            firstInFocusChain: !dialog.psnurl
                            // Right goes to Paste while not typing
                            KeyNavigation.priority: readOnly ? KeyNavigation.BeforeItem : KeyNavigation.AfterItem
                            KeyNavigation.right: pasteUrl
                        }

                        C.PillButton {
                            id: pasteUrl
                            small: true
                            text: qsTr("Paste")
                            lastInFocusChain: true
                            KeyNavigation.left: url
                            // The field takes text only while it is being typed in
                            onClicked: {
                                const typing = !url.readOnly;
                                url.readOnly = false;
                                url.paste();
                                url.readOnly = !typing;
                            }
                        }
                    }
                }
            }

            // Holds the login page's address for copying
            TextInput {
                id: openurl
                visible: false
                text: dialog.psnurl ? dialog.psnurl : ""
                echoMode: Chiaki.settings.streamerMode ? TextInput.Password : TextInput.Normal
            }
        }

        // ------------------------------------------------ by the online ID
        Item {
            id: accountForm
            anchors.fill: parent
            visible: dialog.mode === "lookup"

            C.FormCard {
                title: qsTr("By your PSN online ID")
                text: qsTr("Looks up the account ID that belongs to a PSN online ID, without logging in. It works only if your PSN privacy settings let anyone find you when they search.")

                ColumnLayout {
                    width: Math.min(512, accountForm.width - 80)
                    spacing: 6

                    FieldLabel {
                        text: qsTr("PSN online ID")
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        C.Field {
                            id: usernameField
                            Layout.fillWidth: true
                            Accessible.name: qsTr("PSN online ID")
                            hint: qsTr("Your PSN name")
                            echoMode: Chiaki.settings.streamerMode ? TextInput.Password : TextInput.Normal
                            firstInFocusChain: true
                            onAccepted: submitButton.clicked()
                        }

                        C.PillButton {
                            id: submitButton
                            small: true
                            kind: "primary"
                            iconName: "search"
                            text: qsTr("Look up")
                            enabled: !dialog.looking
                            lastInFocusChain: true
                            onClicked: {
                                const username = usernameField.text.trim();
                                if (!username.length || dialog.looking)
                                    return;
                                const request = new XMLHttpRequest();
                                request.onreadystatechange = function() {
                                    if (request.readyState === XMLHttpRequest.DONE) {
                                        const response = JSON.parse(request.response);
                                        const accountId = response["encoded_id"];
                                        if (accountId) {
                                            dialog.callback(accountId);
                                            dialog.close();
                                        } else {
                                            dialog.looking = false;
                                            dialog.lookupError = String(response["error"]);
                                        }
                                    }
                                }
                                request.open("GET", "https://psn.flipscreen.games/search.php?username=%1".arg(encodeURIComponent(username)));
                                request.send();
                                dialog.looking = true;
                            }
                        }
                    }

                    ErrorNote {
                        Layout.topMargin: 6
                        visible: dialog.lookupError.length > 0
                        text: qsTr("That did not work: %1").arg(dialog.lookupError)
                    }
                }
            }
        }

        Connections {
            target: Chiaki

            function onPsnLoginAccountIdDone(accountId) {
                dialog.callback(accountId);
                submitting = false;
                dialog.close();
            }

            function onPsnLoginAccountIdError(error) {
                if(dialog.mode === "web")
                    dialog.webError = error;
                else
                {
                    dialog.browserError = error;
                    submitting = false;
                }
            }
        }
    }
}
