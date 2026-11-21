import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC

import org.streetpea.chiaking

import "controls" as C

// Logging in to PSN for playing via PSN. On Sony's page inside the app where the app
// has a web view (Windows); otherwise, and always on macOS and Linux, in the browser,
// with the address the browser lands on pasted back here. What happens then shows in
// a note over the page.
DialogView {
    id: dialog
    property var psnurl
    property var expired
    property bool closing: false
    /** "web": Sony's page in the app; "browser": in the web browser. Empty until shown. */
    property string mode
    title: expired ? qsTr("Log in to PSN again") : qsTr("Log in to PSN")
    header: expired ? qsTr("Your PSN log-in has run out.") : ""
    buttonText: qsTr("Finish")
    buttonEnabled: url.text.trim()
    buttonVisible: false
    onAccepted: {
        logDialog.open()
        Chiaki.initPsnAuth(url.text.trim(), function(msg, ok, done) {
            if(ok)
                Chiaki.settings.remotePlayAsk = false;
            if (!done)
                logArea.text += msg + "\n";
            else
            {
                logArea.text += msg + "\n";
                logDialog.allDone = true;
            }
        });
    }
    QQC.StackView.onActivated: {
        Chiaki.settings.remotePlayAsk = true;
        if (!mode) {
            mode = "web";
            nativeTokenForm.forceActiveFocus(Qt.TabFocusReason);
        }
        if(Qt.platform.os == "linux" || Qt.platform.os == "osx")
            useBrowser();
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

    Item {
        id: page
        readonly property int gutter: width < 600 ? 16 : 32

        // ------------------------------------------------ Sony's page in the app
        Item {
            id: nativeTokenForm
            visible: dialog.mode === "web"
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

            // L1 and R1 (Page Up, Page Down) press these
            Item {
                id: psnTokenToolbar
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

            Item {
                id: webView
                property Item web: null
                anchors {
                    top: psnTokenToolbar.bottom
                    bottom: parent.bottom
                    left: parent.left
                    right: parent.right
                    leftMargin: page.gutter
                    rightMargin: page.gutter
                    bottomMargin: 16
                }
                property bool started: false
                Component.onCompleted: {
                    try {
                        web = Qt.createQmlObject("
                        import QtWebEngine
                        import QtQuick.Controls
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
                                localContentCanAccessRemoteUrls: true
                            }

                            onContextMenuRequested: (request) => request.accepted = true;
                            onNavigationRequested: (request) => {
                                if (Chiaki.checkPsnRedirectURL(request.url)) {
                                    logDialog.open()
                                    Chiaki.initPsnAuth(request.url, function(msg, ok, done) {
                                        if (!done)
                                            logArea.text += msg + '\n';
                                        else
                                        {
                                            logArea.text += msg + '\n';
                                            logDialog.allDone = true;
                                        }
                                    });
                                    request.reject();
                                }
                                else
                                    request.accept();
                            }
                            onCertificateError: console.error(error.description);
                        }", webView, "webView");
                        Chiaki.setWebEngineHints(web.profile);
                        web.url = Chiaki.psnLoginUrl();
                        web.anchors.fill = webView;
                    } catch (error) {
                        console.error('Create webengine view failed with error:' + error);
                        dialog.useBrowser();
                    }
                }
            }
        }

        // ------------------------------------------------ in the browser
        Item {
            id: linkForm
            anchors.fill: parent
            visible: dialog.mode === "browser"

            C.FormCard {
                title: qsTr("Log in with your browser")
                text: dialog.expired
                      ? qsTr("Your PSN log-in has run out: log in once more on Sony's page in your browser. Afterwards the browser lands on a page whose address starts with remoteplay.dl.playstation.net. Copy that whole address and paste it here.")
                      : qsTr("Log in on Sony's page in your browser. Afterwards the browser lands on a page whose address starts with remoteplay.dl.playstation.net. Copy that whole address and paste it here.")
                footnote: qsTr("Then press “%1” at the top.").arg(dialog.buttonText)

                ColumnLayout {
                    width: Math.min(512, linkForm.width - 80)
                    spacing: 6

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

        // What happens while logging in; Close (or Cancel) goes to the start page
        C.PopupCard {
            id: logDialog
            property bool allDone: false
            title: qsTr("Logging in to PSN")
            closePolicy: QQC.Popup.NoAutoClose
            onOpened: logArea.forceActiveFocus(Qt.TabFocusReason)
            onClosed: root.showMainView();

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
    }
}
