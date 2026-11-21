#pragma once

#include "streamsession.h"
#include "discoverymanager.h"
#include "qmlmainwindow.h"
#include "qmlcontroller.h"
#include "qmlsettings.h"

#include <QObject>
#include <QMutex>
#include <QThread>
#include <QJSValue>
#include <QSet>
#include <QUrl>
#include <QFutureWatcher>
#include <QFuture>
#include <QAtomicInteger>
#ifdef CHIAKI_HAVE_WEBENGINE
#include <QQuickWebEngineProfile>
#include <QWebEngineUrlRequestInterceptor>
#endif

class SystemdInhibit;
#ifdef Q_OS_MACOS
    class MacWakeSleep;
#elif defined(Q_OS_WINDOWS)
    class WindowsWakeSleep;
#endif

class QmlRegist : public QObject
{
    Q_OBJECT

public:
    QmlRegist(const ChiakiRegistInfo &regist_info, uint32_t log_mask, QObject *parent = nullptr);

signals:
    void log(ChiakiLogLevel level, const QString &msg);
    void failed();
    void success(const RegisteredHost& host);

private:
    static void log_cb(ChiakiLogLevel level, const char *msg, void *user);
    static void regist_cb(ChiakiRegistEvent *event, void *user);

    ChiakiLog chiaki_log;
    ChiakiRegist chiaki_regist;
};

class PsnConnectionWorker : public QObject
{
    Q_OBJECT

public:
    void ConnectPsnConnection(StreamSession *session, const QString &duid, const bool &ps5);

signals:
    void resultReady(const ChiakiErrorCode &err);
};

#ifdef CHIAKI_HAVE_WEBENGINE
class SecUaRequestInterceptor : public QWebEngineUrlRequestInterceptor {
    Q_OBJECT

public:
    SecUaRequestInterceptor(QString version, QObject *p = Q_NULLPTR) : QWebEngineUrlRequestInterceptor(p) { chrome_version = version; };
    void interceptRequest(QWebEngineUrlRequestInfo &info) override {
#ifdef Q_OS_WINDOWS
        info.setHttpHeader("Sec-Ch-Ua", QString("\"Not(A:Brand\";v=\"99\", \"Microsoft Edge\";v=\"%1\", \"Chromium\";v=\"%1\"").arg(chrome_version).toUtf8());
#else
        info.setHttpHeader("Sec-Ch-Ua", QString("\"Not(A:Brand\";v=\"99\", \"Google Chrome\";v=\"%1\", \"Chromium\";v=\"%1\"").arg(chrome_version).toUtf8());
#endif
    }
private:
    QString chrome_version;
};
#endif
class QmlBackend : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QmlMainWindow* window READ qmlWindow CONSTANT)
    Q_PROPERTY(QmlSettings* settings READ qmlSettings CONSTANT)
    // What is drawn on top of the game picture, and the OBS overlay: live values and presets
    Q_PROPERTY(QObject* hud READ hudSettings CONSTANT)
    Q_PROPERTY(QObject* overlayLayout READ overlayLayoutSettings CONSTANT)
    // Going live to Twitch or another RTMP(S) server: settings, key, Go live / Stop, numbers
    Q_PROPERTY(QObject* live READ liveController CONSTANT)
    // Stream scenes (Starting soon, Be right back, ...) for the live stream and for OBS
    Q_PROPERTY(QObject* scenes READ liveScenes CONSTANT)
    // The player's Twitch account and its events, and the alerts made of them
    Q_PROPERTY(QObject* twitch READ twitchClient CONSTANT)
    Q_PROPERTY(QObject* alerts READ liveAlerts CONSTANT)
    // What the Twitch channel shows on the game picture: latest follower, goals, poll, chat, ...
    Q_PROPERTY(QObject* twitchElements READ twitchElementsObject CONSTANT)
    Q_PROPERTY(QObject* viewerActions READ viewerActionsObject CONSTANT)
    // Whether the display over the game is drawn into the stream scene (true) or sits in a window of its own
    Q_PROPERTY(bool hudInline READ hudInline CONSTANT)
    // A release newer than this build, found by the check at start; empty if there is none
    Q_PROPERTY(QString updateVersion READ updateVersion NOTIFY updateChanged)
    Q_PROPERTY(StreamSession* session READ qmlSession NOTIFY sessionChanged)
    Q_PROPERTY(QList<QmlController*> controllers READ qmlControllers NOTIFY controllersChanged)
    Q_PROPERTY(bool discoveryEnabled READ discoveryEnabled WRITE setDiscoveryEnabled NOTIFY discoveryEnabledChanged)
    /** macOS: the system (Local Network privacy) or a firewall keeps the app from the consoles in its network. */
    Q_PROPERTY(bool localNetworkBlocked READ localNetworkBlocked NOTIFY localNetworkBlockedChanged)
    Q_PROPERTY(QVariantList hosts READ hosts NOTIFY hostsChanged)
    Q_PROPERTY(QVariantList hiddenHosts READ hiddenHosts NOTIFY hiddenHostsChanged)
    Q_PROPERTY(bool autoConnect READ autoConnect NOTIFY autoConnectChanged)
    Q_PROPERTY(bool coplayJoining READ coplayJoining NOTIFY coplayJoiningChanged)
    /** Joining a public room whose host lets each newcomer in: the host has to answer. */
    Q_PROPERTY(bool coplayJoinWaiting READ coplayJoinWaiting NOTIFY coplayJoiningChanged)
    Q_PROPERTY(bool reconnecting READ reconnecting NOTIFY reconnectingChanged)
    Q_PROPERTY(QString reconnectStatus READ reconnectStatus NOTIFY reconnectingChanged)
    Q_PROPERTY(QString chiakiVersion READ chiakiVersion CONSTANT)
    Q_PROPERTY(QString overlayUrl READ overlayUrl NOTIFY overlayStateChanged)
    Q_PROPERTY(QString overlayError READ overlayError NOTIFY overlayStateChanged)
    Q_PROPERTY(PsnConnectState connectState READ connectState WRITE setConnectState NOTIFY connectStateChanged)
    Q_PROPERTY(QVariantList currentControllerMapping READ currentControllerMapping NOTIFY currentControllerMappingChanged)
    Q_PROPERTY(QString currentControllerType READ currentControllerType NOTIFY currentControllerTypeChanged)
    Q_PROPERTY(bool controllerMappingDefaultMapping READ controllerMappingDefaultMapping NOTIFY controllerMappingDefaultMappingChanged)
    Q_PROPERTY(bool controllerMappingInProgress READ controllerMappingInProgress NOTIFY controllerMappingInProgressChanged)
    Q_PROPERTY(bool controllerMappingAltered READ controllerMappingAltered NOTIFY controllerMappingAlteredChanged)
    Q_PROPERTY(bool enableAnalogStickMapping READ enableAnalogStickMapping WRITE setEnableAnalogStickMapping NOTIFY enableAnalogStickMappingChanged)

public:

    enum class PsnConnectState
    {
        NotStarted,
        WaitingForInternet,
        InitiatingConnection,
        LinkingConsole,
        RegisteringConsole,
        RegistrationFinished,
        DataConnectionStart,
        DataConnectionFinished,
        ConnectFailed,
        ConnectFailedStart,
        ConnectFailedConsoleUnreachable,
    };
    Q_ENUM(PsnConnectState);
    QmlBackend(Settings *settings, QmlMainWindow *window);
    ~QmlBackend();

    QmlMainWindow *qmlWindow() const;
    QmlSettings *qmlSettings() const;
    StreamSession *qmlSession() const;
    QList<QmlController*> qmlControllers() const;

    bool discoveryEnabled() const;
    bool localNetworkBlocked() const { return discovery_manager.GetLocalNetworkBlocked(); }
    void setDiscoveryEnabled(bool enabled);
    void refreshAuth();

    PsnConnectState connectState() const;
    void setConnectState(PsnConnectState connect_state);
    QVariantList hosts() const;

    QVariantList hiddenHosts() const;

    QVariantList currentControllerMapping() const;

    QString currentControllerType() const { return controller_mapping_controller_type; }

    void controllerMappingChangeButton(QString button);

    void controllerMappingUpdate(Controller *controller);

    bool controllerMappingDefaultMapping() const { return controller_mapping_default_mapping; }
    void setControllerMappingDefaultMapping(bool is_default_mapping);

    bool controllerMappingAltered() const { return controller_mapping_altered; }
    void setControllerMappingAltered(bool altered);

    bool controllerMappingInProgress() const  {return controller_mapping_in_progress; }
    void setControllerMappingInProgress(bool is_in_progress);

    bool enableAnalogStickMapping() const { return enable_analog_stick_mapping; }
    void setEnableAnalogStickMapping(bool enabled);

    void finishAutoRegister(const ChiakiRegisteredHost &host);

    bool autoConnect() const;

    void psnConnector();

    void createSession(const StreamSessionConnectInfo &connect_info);

    void psnSessionStart();
    void startSession(bool emit_session_changed = true);

    void checkPsnConnection(const ChiakiErrorCode &err);

    void checkNickname(QString nickname);

    bool closeRequested();

    void setAllowJoystickBackgroundEvents();

    void setIsAppActive();

    void profileChanged();
    bool prepareFrameForPresentation(ChiakiFfmpegFrame &frame, bool use_opengl_renderer);
    void prepareStartupPresentation(bool present_warmup);
    void applyStartupWindowSizing();

    void goToSleep();

    bool zeroCopy()        { return !disable_zero_copy; };
    void disableZeroCopy() { disable_zero_copy = true; };
    void enableZeroCopy()  { disable_zero_copy = false; };

    Q_INVOKABLE void deleteHost(int index);
    Q_INVOKABLE void wakeUpHost(int index, QString nickname = QString());
    Q_INVOKABLE void addManualHost(int index, const QString &address);
    Q_INVOKABLE void hideHost(const QString &mac_string, const QString &host_nickname);
    Q_INVOKABLE void unhideHost(const QString &mac_string);
    Q_INVOKABLE bool registerHost(const QString &host, const QString &psn_id, const QString &pin, const QString &cpin, bool broadcast, int target, const QJSValue &callback);
    /** Play: a session of this computer's own, nobody joins. */
    Q_INVOKABLE void connectToHost(int index, QString nickname = QString());
    /** Host: a session friends join, as set up on the Host page. */
    Q_INVOKABLE void hostConsole(int index, QString nickname = QString());
    Q_INVOKABLE void stopSession(bool sleep);
    /** Version of the chiaki-ng this app is built on; the app's own is the application version. */
    QString chiakiVersion() const { return QStringLiteral(CHIAKI_VERSION); }
    // Play Together, see qmlbackend_coplay.cpp
    bool coplayJoining() const { return coplay_joining; }
    bool coplayJoinWaiting() const { return coplay_join_waiting; }
    /** Stops joining: no more waiting for the host. */
    Q_INVOKABLE void coplayCancelJoin();
    /** Address of the stream overlay for OBS; empty while it is switched off. */
    QString overlayUrl() const;
    QString overlayError() const;
    Q_INVOKABLE void copyOverlayUrl();
    void initOverlay();
    void initDisplays();
    QObject *hudSettings() const;
    QObject *liveController() const;
    QObject *liveScenes() const;
    QObject *twitchClient() const;
    QObject *liveAlerts() const;
    QObject *twitchElementsObject() const;
    QObject *viewerActionsObject() const;
    QObject *overlayLayoutSettings() const;
    bool hudInline() const;
    QString updateVersion() const { return update_version; }
    /** Asks GitHub for the latest release; see qmlbackend_update.cpp. */
    Q_INVOKABLE void checkForUpdate();
    Q_INVOKABLE void openUpdatePage();
    /** macOS: System Settings at Privacy & Security › Local Network. */
    Q_INVOKABLE void openLocalNetworkSettings();
    /** "Later": the start does not ask about this version again. */
    Q_INVOKABLE void skipUpdate();
    /** Joins a session somebody else shares. Answers with coplayJoinFinished. */
    Q_INVOKABLE void joinCoPlay(const QString &invite, const QString &name, bool controller_only);
    /**
     * The connection (to the console, or as a guest to the host of a shared session) was
     * lost and the app is getting it back by itself. The stream view stays on screen.
     */
    bool reconnecting() const;
    /** A short text for the user while that goes on. */
    QString reconnectStatus() const;
    /** Gives up on getting the connection back: the session ends. */
    Q_INVOKABLE void cancelReconnect();
    Q_INVOKABLE void sessionGoHome();
    Q_INVOKABLE void enterPin(const QString &pin);
    Q_INVOKABLE QUrl psnLoginUrl() const;
    Q_INVOKABLE bool checkPsnRedirectURL(const QUrl &url) const;
    Q_INVOKABLE bool handlePsnLoginRedirect(const QUrl &url);
    Q_INVOKABLE void stopAutoConnect();
    /** As stopAutoConnect(), and the console is not looked for over PSN either. */
    void cancelAutoConnect();
    Q_INVOKABLE void setConsolePin(int index, QString console_pin);
    Q_INVOKABLE QString openPsnLink();
    Q_INVOKABLE QString openPlaceboOptionsLink();
    Q_INVOKABLE void initPsnAuth(const QUrl &url, const QJSValue &callback);
    Q_INVOKABLE void psnCancel(bool stop_thread);
    Q_INVOKABLE void refreshPsnToken();
    Q_INVOKABLE void beginControllerMapping(bool reset_mapping);
    Q_INVOKABLE void creatingControllerMapping(bool creating_controller_mapping);
    Q_INVOKABLE void updateButton(int chiaki_button, QString physical_button, int new_index);
    Q_INVOKABLE void controllerMappingSelectButton();
    Q_INVOKABLE void controllerMappingReset();
    Q_INVOKABLE void controllerMappingQuit();
    Q_INVOKABLE void controllerMappingButtonQuit();
    Q_INVOKABLE void controllerMappingApply();
    Q_INVOKABLE void autoRegister();
#if CHIAKI_GUI_ENABLE_STEAM_SHORTCUT
    Q_INVOKABLE void createSteamShortcut(QString shortcutName, QString launchOptions, const QJSValue &callback, QString steamDir);
#endif
#ifdef CHIAKI_HAVE_WEBENGINE
    Q_INVOKABLE void setWebEngineHints(QQuickWebEngineProfile *profile);
    Q_INVOKABLE void clearCookies(QQuickWebEngineProfile *profile);
#endif

signals:
    void sessionChanged(StreamSession *session);
    /** A session that was running is over for good, as opposed to lost and being reconnected. */
    void sessionEnded();
    void reconnectingChanged();
    void psnConnect(StreamSession *session, const QString &duid, const bool &ps5);
    void showPsnView();
    void connectStateChanged();
    void controllersChanged();
    void currentControllerMappingChanged();
    void currentControllerTypeChanged();
    void controllerMappingInProgressChanged();
    void controllerMappingDefaultMappingChanged();
    void controllerMappingButtonSelected(QString button);
    void controllerMappingAlteredChanged();
    void controllerMappingSteamControllerSelected();
    void enableAnalogStickMappingChanged();
    void discoveryEnabledChanged();
    void localNetworkBlockedChanged();
    void hostsChanged();
    void hiddenHostsChanged();
    void psnTokenChanged();
    void psnCredsExpired();
    void autoConnectChanged();
    void wakeupStartInitiated();
    void wakeupStartFailed();
    void windowTypeUpdated(WindowType type);

    void error(const QString &title, const QString &text);
    void sessionError(const QString &title, const QString &text);
    void coplayJoiningChanged();
    void overlayStateChanged();
    void updateChanged();
    /** A newer release exists, and the user has not said "later" to it yet. */
    void updateFound(const QString &version);
    void coplayJoinFinished(bool ok, const QString &message);
    void sessionPinDialogRequested();
    void sessionStopDialogRequested();
    void registDialogRequested(const QString &host, bool ps5, const QString &duid);
    void psnLoginAccountIdDone(const QString &accountId);
    void psnLoginAccountIdError(const QString &error);

private:
    /** Starts a session with this console; shared if the last start was Host (coplay/host_enabled). */
    void startConsole(int index, QString nickname = QString());
    struct DisplayServer {
        bool valid = false;

        DiscoveryHost discovery_host;
        ManualHost manual_host;
        bool discovered;

        PsnHost psn_host;
        QString duid;
        RegisteredHost registered_host;
        bool registered;

        QString GetHostAddr() const { return discovered ? discovery_host.host_addr : manual_host.GetHost(); }
        bool IsPS5() const { return discovered ? discovery_host.ps5 :
            (registered ? chiaki_target_is_ps5(registered_host.GetTarget()) : true); }
    };

    DisplayServer displayServerAt(int index) const;
    bool sendWakeup(const DisplayServer &server);
    bool sendWakeup(const QString &host, const QByteArray &regist_key, bool ps5);
    void updateControllers();
    void updateControllerMappings();
    void updateDiscoveryHosts();
    void updatePsnHosts();
    void updatePsnHostsThread();
    void updateAudioVolume();
    void resumeFromSleep();
    uint32_t getStreamShortcut() const;
    void updateStreamShortcut();
    QString getExecutable();
    void teardownSession();
    void finishSession();
    // Getting a lost console session back, see qmlbackend.cpp
    bool scheduleConsoleReconnect();
    void consoleReconnectAttempt();
    void stopConsoleReconnect();

    Settings *settings = {};
    QmlSettings *settings_qml = {};
    QmlMainWindow *window = {};
    StreamSession *session = {};
    QThread *frame_thread = {};
    QTimer *psn_reconnect_timer = {};
    QTimer *psn_auto_connect_timer = {};
    QTimer *wakeup_start_timer = {};
    QTimer *wakeup_resend_timer = {};
    int psn_reconnect_tries = 0;
    QThread psn_connection_thread;
    PsnConnectState psn_connect_state;
    DiscoveryManager discovery_manager;
    QList<QString> waking_sleeping_nicknames;
    QHash<int, QmlController*> controllers;
    DisplayServer regist_dialog_server;
    StreamSessionConnectInfo session_info = {};
    SystemdInhibit *sleep_inhibit = {};
#ifdef Q_OS_MACOS
    MacWakeSleep *mac_wake_sleep = {};
#elif defined(Q_OS_WINDOWS)
    WindowsWakeSleep *windows_wake_sleep = {};
#endif
    bool controller_mapping_default_mapping = false;
    bool controller_mapping_altered = false;
    bool updating_psn_hosts = false;
    QFutureWatcher<void> psn_hosts_watcher;
    QFuture<void> psn_hosts_future;
    bool disable_zero_copy = false;
    QMutex hw_transfer_state_mutex;
    QSet<int> logged_hw_transfer_formats;
    QSet<int> logged_hw_transfer_failures;
    QAtomicInteger<int> pending_recovered_frame = 0;
    Controller *controller_mapping_controller = {};
    QMap<QString, QStringList> controller_guids_to_update = {};
    int controller_mapping_id = -1;
    QString controller_mapping_controller_guid = "";
    QString controller_mapping_controller_vid_pid = "";
    QString controller_mapping_controller_type = "";
    QMap<QString, QStringList> controller_mapping_controller_mappings = {};
    QMap<QString, QStringList> controller_mapping_applied_controller_mappings = {};
    QMap<QString, QString> controller_mapping_physical_button_mappings = {};
    QMap<QString, QString> controller_mapping_original_controller_mappings = {};
    bool controller_mapping_reset_requested = false;
    bool controller_mapping_in_progress = false;
    bool enable_analog_stick_mapping = false;
    bool resume_session = false;
    // The console session was lost and is being replaced by a new one. In between attempts
    // `session` is the lost one: it stays until its successor takes its place, so that the
    // stream view never sees a time without a session.
    bool console_reconnecting = false;
    bool console_reconnect_waiting = false;
    int console_reconnect_attempt = 0;
    QElapsedTimer console_reconnect_clock;
    QElapsedTimer console_reconnect_stable;
    QTimer *console_reconnect_timer = {};
    bool coplay_joining = false;
    bool coplay_join_waiting = false;
    std::weak_ptr<coplay::Client> coplay_join_client;
    /** Names this player to a host for as long as the app runs, see coplay::ClientConfig. */
    quint64 coplay_player_id = 0;
    class OverlayServer *overlay = {};
    QString update_version;
    QString update_url;
    class PresetSettings *hud_settings = {};
    class LiveController *live_controller = {};
    class LiveScenes *live_scenes = {};
    class TwitchClient *twitch = {};
    class LiveAlerts *live_alerts = {};
    class TwitchElements *twitch_elements = {};
    class ViewerActions *viewer_actions = {};
    class PresetSettings *overlay_settings = {};
    bool settings_allocd = false;
    HostMAC auto_connect_mac = {};
    QString auto_connect_nickname = "";
    QString wakeup_nickname = "";
    bool wakeup_start = false;
    QMap<QString, PsnHost> psn_hosts = {};
    QMap<QString, PsnHost> psn_nickname_hosts = {};
#ifdef CHIAKI_HAVE_WEBENGINE
    SecUaRequestInterceptor * request_interceptor = {};
#endif
};
