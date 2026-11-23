#include "qmlbackend.h"
#include "livecontroller.h"
#include "livescenes.h"
#include "livealerts.h"
#include "devicecheck.h"
#include "keymouse.h"
#include "appupdater.h"
#include "macpermissions.h"
#include "introsound.h"
#include "twitch.h"
#include "twitchelements.h"
#include "vieweractions.h"

#include <coplay/invite.h>
#include "qmlsettings.h"
#include "qmlmainwindow.h"
#include "streamsession.h"
#include "controllermanager.h"
#include "psnaccountid.h"
#include "psntoken.h"
#include "systemdinhibit.h"
#include "chiaki/remote/holepunch.h"
#ifdef Q_OS_MACOS
#include "macWakeSleep.h"
#elif defined(Q_OS_WINDOWS)
#include "windowsWakeSleep.h"
#endif
#if CHIAKI_GUI_ENABLE_STEAM_SHORTCUT
#include "steamtools.h"
#endif

#ifdef CHIAKI_HAVE_WEBENGINE
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
#include <QWebEngineClientHints>
#endif
#include <QWebEngineCookieStore>
#endif
#include <QUrlQuery>
#include <QMetaObject>
#include <QtGlobal>
#include <QGuiApplication>
#include <QClipboard>
#include <QPixmap>
#include <QImageReader>
#include <QProcessEnvironment>
#include <QDesktopServices>
#include <QtConcurrent>
#include <QTimer>
#include <QBuffer>
#include <QImage>
#include <QThreadPool>
#include "pcsource.h"
#include "virtualpads.h"
#include "pcinput.h"

#ifndef _WIN32
#include <unistd.h>
#endif

extern "C" {
#include <chiaki/time.h>
#include <libavcodec/avcodec.h>
#include <libavutil/pixdesc.h>
#include <libavutil/pixfmt.h>
}

static bool detailedVerboseTelemetryEnabled()
{
    return qEnvironmentVariableIntValue("CHIAKI_VERBOSE_LOG_NOISE") == 1;
}

#define CHIAKI_NOISY_DEBUG() if (!detailedVerboseTelemetryEnabled()) {} else qCDebug(chiakiGui)

static void logDecoderDeliveryStats(qint64 now_us, int32_t frames_lost, bool recovered)
{
    static qint64 last_sample_us = 0;
    static qint64 last_log_us = 0;
    static qint64 min_interval_us = 0;
    static qint64 max_interval_us = 0;
    static qint64 sum_interval_us = 0;
    static int samples = 0;
    static int lost_samples = 0;
    static int recovered_samples = 0;

    if (frames_lost > 0)
        lost_samples += frames_lost;
    if (recovered)
        recovered_samples++;

    if (last_sample_us != 0) {
        const qint64 interval_us = now_us - last_sample_us;
        if (min_interval_us == 0 || interval_us < min_interval_us)
            min_interval_us = interval_us;
        if (interval_us > max_interval_us)
            max_interval_us = interval_us;
        sum_interval_us += interval_us;
        samples++;
    }
    last_sample_us = now_us;

    if (last_log_us == 0)
        last_log_us = now_us;
    else if (now_us - last_log_us >= 1000000 && samples > 0) {
        CHIAKI_NOISY_DEBUG().nospace()
            << "[decode] arrival interval_us_min=" << min_interval_us
            << " interval_us_max=" << max_interval_us
            << " interval_us_avg=" << (sum_interval_us / samples)
            << " frames_lost=" << lost_samples
            << " recovered=" << recovered_samples
            << " samples=" << samples;
        last_log_us = now_us;
        min_interval_us = 0;
        max_interval_us = 0;
        sum_interval_us = 0;
        samples = 0;
        lost_samples = 0;
        recovered_samples = 0;
    }
}

static void logDecoderHandoffStats(qint64 now_us, qint64 handoff_us, double pts)
{
    static qint64 last_log_us = 0;
    static qint64 min_handoff_us = 0;
    static qint64 max_handoff_us = 0;
    static qint64 sum_handoff_us = 0;
    static int samples = 0;

    if (handoff_us < 0)
        handoff_us = 0;
    if (handoff_us >= 12000) {
        CHIAKI_NOISY_DEBUG().nospace()
            << "[decode] handoff_outlier_us=" << handoff_us
            << " pts=" << pts;
    }
    if (min_handoff_us == 0 || handoff_us < min_handoff_us)
        min_handoff_us = handoff_us;
    if (handoff_us > max_handoff_us)
        max_handoff_us = handoff_us;
    sum_handoff_us += handoff_us;
    samples++;

    if (last_log_us == 0)
        last_log_us = now_us;
    else if (now_us - last_log_us >= 1000000 && samples > 0) {
        CHIAKI_NOISY_DEBUG().nospace()
            << "[decode] handoff_us_min=" << min_handoff_us
            << " handoff_us_max=" << max_handoff_us
            << " handoff_us_avg=" << (sum_handoff_us / samples)
            << " pts=" << pts
            << " samples=" << samples;
        last_log_us = now_us;
        min_handoff_us = 0;
        max_handoff_us = 0;
        sum_handoff_us = 0;
        samples = 0;
    }
}

static void logDecoderFramePtsStats(qint64 now_us, double pts, float duration)
{
    static qint64 last_log_us = 0;
    static double last_pts = -1.0;
    static qint64 min_interval_us = 0;
    static qint64 max_interval_us = 0;
    static qint64 sum_interval_us = 0;
    static qint64 min_duration_us = 0;
    static qint64 max_duration_us = 0;
    static qint64 sum_duration_us = 0;
    static int samples = 0;

    if (last_pts >= 0.0) {
        const qint64 interval_us = static_cast<qint64>((pts - last_pts) * 1000000.0 + 0.5);
        if (min_interval_us == 0 || interval_us < min_interval_us)
            min_interval_us = interval_us;
        if (interval_us > max_interval_us)
            max_interval_us = interval_us;
        sum_interval_us += interval_us;
        samples++;
    }
    last_pts = pts;

    const qint64 duration_us = static_cast<qint64>(duration * 1000000.0 + 0.5);
    if (min_duration_us == 0 || duration_us < min_duration_us)
        min_duration_us = duration_us;
    if (duration_us > max_duration_us)
        max_duration_us = duration_us;
    sum_duration_us += duration_us;

    if (last_log_us == 0)
        last_log_us = now_us;
    else if (now_us - last_log_us >= 1000000 && samples > 0) {
        CHIAKI_NOISY_DEBUG().nospace()
            << "[decode] pts interval_us_min=" << min_interval_us
            << " interval_us_max=" << max_interval_us
            << " interval_us_avg=" << (sum_interval_us / samples)
            << " duration_us_min=" << min_duration_us
            << " duration_us_max=" << max_duration_us
            << " duration_us_avg=" << (sum_duration_us / samples)
            << " samples=" << samples;
        last_log_us = now_us;
        min_interval_us = 0;
        max_interval_us = 0;
        sum_interval_us = 0;
        min_duration_us = 0;
        max_duration_us = 0;
        sum_duration_us = 0;
        samples = 0;
    }
}

// ------------------------------------------------------------------ a picture for the room list
//
// A public room shows a small picture of its game in the list (StreamSession, CoPlayRoomAnnouncer).
// One decoded frame now and then becomes a JPEG of at most 320x180, off the frame thread. The
// formats the decoders give (NV12, P010, YUV 4:2:0 in 8 or 10 bits) are turned into RGB here, so
// that no scaler library is needed; another format simply gives no picture.

static inline uint8_t clamp_byte(int value)
{
    return static_cast<uint8_t>(value < 0 ? 0 : value > 255 ? 255 : value);
}

/** The frame (a hardware one is copied first) as an RGB picture of at most 640 pixels across. */
static QImage room_picture_rgb(const AVFrame *source)
{
    AVFrame *sw = nullptr;
    const AVFrame *frame = source;
    if (source->hw_frames_ctx) {
        sw = av_frame_alloc();
        if (!sw || av_hwframe_transfer_data(sw, source, 0) < 0) {
            av_frame_free(&sw);
            return QImage();
        }
        frame = sw;
    }
    const AVPixelFormat format = static_cast<AVPixelFormat>(frame->format);
    const bool semi_planar = format == AV_PIX_FMT_NV12 || format == AV_PIX_FMT_P010LE;
    const bool planar = format == AV_PIX_FMT_YUV420P || format == AV_PIX_FMT_YUVJ420P || format == AV_PIX_FMT_YUV420P10LE;
    // How many bits to drop to get 8: P010 keeps its 10 bits at the top of 16, YUV420P10 at the bottom
    const int shift = format == AV_PIX_FMT_P010LE ? 8 : format == AV_PIX_FMT_YUV420P10LE ? 2 : 0;
    const bool wide = format == AV_PIX_FMT_P010LE || format == AV_PIX_FMT_YUV420P10LE;
    if ((!semi_planar && !planar) || frame->width < 16 || frame->height < 16 || !frame->data[0] || !frame->data[1]
            || (planar && !frame->data[2])) {
        av_frame_free(&sw);
        return QImage();
    }

    const int out_w = qMin(640, frame->width);
    const int out_h = qMax(1, static_cast<int>(static_cast<qint64>(frame->height) * out_w / frame->width));
    QImage image(out_w, out_h, QImage::Format_RGB888);
    const bool full_range = frame->color_range == AVCOL_RANGE_JPEG || format == AV_PIX_FMT_YUVJ420P;
    const bool bt601 = frame->colorspace == AVCOL_SPC_BT470BG || frame->colorspace == AVCOL_SPC_SMPTE170M;
    // Y'CbCr to R'G'B' (BT.709, or BT.601 where the frame says so), times 1024
    const int kr = bt601 ? 1436 : 1613, kgu = bt601 ? 352 : 192, kgv = bt601 ? 731 : 479, kb = bt601 ? 1815 : 1901;
    auto sample = [&](int plane, int x, int y) -> int {
        const uint8_t *row = frame->data[plane] + static_cast<qint64>(y) * frame->linesize[plane];
        if (!wide)
            return row[x];
        const uint16_t value = reinterpret_cast<const uint16_t *>(row)[x];
        return value >> shift;
    };
    for (int oy = 0; oy < out_h; ++oy) {
        const int y = static_cast<int>(static_cast<qint64>(oy) * frame->height / out_h);
        uchar *line = image.scanLine(oy);
        for (int ox = 0; ox < out_w; ++ox) {
            const int x = static_cast<int>(static_cast<qint64>(ox) * frame->width / out_w);
            int luma = sample(0, x, y);
            int u, v;
            if (semi_planar) {
                u = sample(1, (x / 2) * 2, y / 2);
                v = sample(1, (x / 2) * 2 + 1, y / 2);
            } else {
                u = sample(1, x / 2, y / 2);
                v = sample(2, x / 2, y / 2);
            }
            u -= 128;
            v -= 128;
            if (!full_range) {
                luma = (luma - 16) * 1192 / 1024;
                u = u * 1167 / 1024;
                v = v * 1167 / 1024;
            }
            line[ox * 3 + 0] = clamp_byte(luma + (kr * v) / 1024);
            line[ox * 3 + 1] = clamp_byte(luma - (kgu * u + kgv * v) / 1024);
            line[ox * 3 + 2] = clamp_byte(luma + (kb * u) / 1024);
        }
    }
    av_frame_free(&sw);
    return image;
}

/** A JPEG of at most 320x180 (and the list's 48 KB) of the frame; empty if that cannot be made. */
static QByteArray room_picture_jpeg(const AVFrame *frame)
{
    const QImage rgb = room_picture_rgb(frame);
    if (rgb.isNull())
        return QByteArray();
    const QImage small = rgb.scaled(320, 180, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    for (int quality : { 72, 55, 40 }) {
        QByteArray jpeg;
        QBuffer buffer(&jpeg);
        buffer.open(QIODevice::WriteOnly);
        if (!small.save(&buffer, "JPG", quality))
            return QByteArray();
        if (jpeg.size() <= 46 * 1024)
            return jpeg;
    }
    return QByteArray();
}

static inline bool frame_has_planes(const AVFrame *frame)
{
    if (!frame)
        return false;
    if (frame->hw_frames_ctx)
        return true; // zero copy frames still map via hw context
    const int planes = av_pix_fmt_count_planes(static_cast<AVPixelFormat>(frame->format));
    return frame->data[0] && planes > 0;
}

static bool frame_can_use_direct_render(const AVFrame *frame, bool use_opengl_renderer)
{
    if (!frame || !frame->hw_frames_ctx)
        return true;

    switch (frame->format) {
    case AV_PIX_FMT_VULKAN:
        return true;
#ifdef Q_OS_LINUX
    case AV_PIX_FMT_VAAPI:
        return !use_opengl_renderer;
#endif
    default:
        return false;
    }
}

#define PSN_DEVICES_TRIES 2
#define MAX_PSN_RECONNECT_TRIES 6
#define PSN_INTERNET_WAIT_SECONDS 5
#define WAKEUP_PSN_IGNORE_SECONDS 10
#define WAKEUP_WAIT_SECONDS 25
// A wakeup is one UDP packet, so it is resent this often while a session waits for the console to wake
#define WAKEUP_RESEND_MS 1000
static QMutex chiaki_log_mutex;
static ChiakiLog *chiaki_log_ctx = nullptr;
static QtMessageHandler qt_msg_handler = nullptr;

static void msg_handler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    QMutexLocker lock(&chiaki_log_mutex);
    if (!chiaki_log_ctx) {
        qt_msg_handler(type, context, msg);
        return;
    }
    ChiakiLogLevel chiaki_level;
    switch (type) {
    case QtDebugMsg:
        chiaki_level = CHIAKI_LOG_DEBUG;
        break;
    case QtInfoMsg:
        chiaki_level = CHIAKI_LOG_INFO;
        break;
    case QtWarningMsg:
        chiaki_level = CHIAKI_LOG_WARNING;
        break;
    case QtCriticalMsg:
        chiaki_level = CHIAKI_LOG_ERROR;
        break;
    case QtFatalMsg:
        chiaki_level = CHIAKI_LOG_ERROR;
        break;
    }
    chiaki_log(chiaki_log_ctx, chiaki_level, "%s", qPrintable(msg));
}

QmlRegist::QmlRegist(const ChiakiRegistInfo &regist_info, uint32_t log_mask, QObject *parent)
    : QObject(parent)
{
    chiaki_log_init(&chiaki_log, log_mask, &QmlRegist::log_cb, this);
    chiaki_regist_start(&chiaki_regist, &chiaki_log, &regist_info, &QmlRegist::regist_cb, this);
}

void QmlRegist::log_cb(ChiakiLogLevel level, const char *msg, void *user)
{
    chiaki_log_cb_print(level, msg, nullptr);
    auto r = static_cast<QmlRegist*>(user);
    QMetaObject::invokeMethod(r, std::bind(&QmlRegist::log, r, level, QString::fromUtf8(msg)), Qt::QueuedConnection);
}

void QmlRegist::regist_cb(ChiakiRegistEvent *event, void *user)
{
    auto r = static_cast<QmlRegist*>(user);
    switch (event->type) {
    case CHIAKI_REGIST_EVENT_TYPE_FINISHED_SUCCESS:
        QMetaObject::invokeMethod(r, std::bind(&QmlRegist::success, r, *event->registered_host), Qt::QueuedConnection);
        QMetaObject::invokeMethod(r, &QObject::deleteLater, Qt::QueuedConnection);
        break;
    case CHIAKI_REGIST_EVENT_TYPE_FINISHED_FAILED:
        QMetaObject::invokeMethod(r, &QmlRegist::failed, Qt::QueuedConnection);
        QMetaObject::invokeMethod(r, &QObject::deleteLater, Qt::QueuedConnection);
        break;
    default:
        break;
    }
}

QmlBackend::QmlBackend(Settings *settings, QmlMainWindow *window)
    : QObject(window)
    , settings(settings)
    , settings_qml(new QmlSettings(settings, this))
    , window(window)
{
    qt_msg_handler = qInstallMessageHandler(msg_handler);

    const char *uri = "org.streetpea.chiaking";
    qmlRegisterSingletonInstance(uri, 1, 0, "Chiaki", this);
    qmlRegisterUncreatableType<QmlMainWindow>(uri, 1, 0, "ChiakiWindow", {});
    qmlRegisterUncreatableType<QmlSettings>(uri, 1, 0, "ChiakiSettings", {});
    qmlRegisterUncreatableType<StreamSession>(uri, 1, 0, "ChiakiSession", {});

    live_controller = new LiveController(settings, this);
    connect(this, &QmlBackend::sessionChanged, live_controller, &LiveController::SetSession);
    live_scenes = new LiveScenes(settings, this);
    connect(this, &QmlBackend::sessionChanged, live_scenes, &LiveScenes::SetSession);
    connect(this, &QmlBackend::reconnectingChanged, live_scenes, [this]() { live_scenes->SetReconnecting(reconnecting()); });
    twitch = new TwitchClient(settings, this);
    live_alerts = new LiveAlerts(settings, live_scenes, this);
    connect(twitch, &TwitchClient::Event, live_alerts, &LiveAlerts::OnTwitchEvent);
    // The chat sound is for the room's chat too
    connect(this, &QmlBackend::sessionChanged, live_alerts, [this](StreamSession *s) {
        if (s)
            connect(s, &StreamSession::coplayChatMessage, live_alerts, &LiveAlerts::OnRoomChat, Qt::UniqueConnection);
    });
    // Public rooms say when their host is live on Twitch, if the host wants that
    auto room_stream = [this]() {
        if (session)
            session->SetCoPlayRoomStream(twitch->RoomStream());
    };
    connect(twitch, &TwitchClient::changed, this, room_stream);
    connect(this, &QmlBackend::sessionChanged, this, room_stream);
    twitch_elements = new TwitchElements(settings, twitch, live_controller, live_alerts, this);
    connect(twitch, &TwitchClient::Event, twitch_elements, &TwitchElements::OnTwitchEvent);
    viewer_actions = new ViewerActions(settings, twitch, this);
    connect(twitch, &TwitchClient::Event, viewer_actions, &ViewerActions::OnTwitchEvent);
    connect(this, &QmlBackend::sessionChanged, viewer_actions, &ViewerActions::SetSession);
    device_check = new DeviceCheck(settings, this);
    permissions = new MacPermissions(&discovery_manager, this);
    intro_sound = new IntroSound(settings, this);
    // While playing, the session has the microphone, the speakers and the webcam
    connect(this, &QmlBackend::sessionChanged, device_check, [this]() {
        if (session)
            device_check->stopAll();
    });
    app_updater = new AppUpdater(this);
    // The new version is in place and starts once this app has gone
    connect(app_updater, &AppUpdater::restarting, this, []() {
        QTimer::singleShot(600, qApp, &QCoreApplication::quit);
    });
    key_mouse = new KeyMouse(settings, this);
    // A game has the keyboard and the mouse now: trying them in the settings is over, and nothing is still held
    connect(this, &QmlBackend::sessionChanged, key_mouse, [this](StreamSession *s) {
        key_mouse->setTrying(false);
        key_mouse->ReleaseAll();
        if (s)
            s->SetKeyMouse(key_mouse);
    });
    // With the saved login, a moment after start (it reads the password store)
    QTimer::singleShot(3000, twitch, &TwitchClient::Start);

    QObject *frame_obj = new QObject();
    frame_thread = new QThread(frame_obj);
    frame_thread->setObjectName("frame");
    frame_thread->start();
    frame_obj->moveToThread(frame_thread);

    PsnConnectionWorker *worker = new PsnConnectionWorker;
    worker->moveToThread(&psn_connection_thread);
    connect(&psn_connection_thread, &QThread::finished, worker, &QObject::deleteLater);
    connect(this, &QmlBackend::psnConnect, worker, &PsnConnectionWorker::ConnectPsnConnection);
    connect(worker, &PsnConnectionWorker::resultReady, this, &QmlBackend::checkPsnConnection);
    connect(&psn_hosts_watcher, &QFutureWatcher<void>::finished, [this]{ this->updating_psn_hosts = false; });
    psn_connection_thread.start();

    setConnectState(PsnConnectState::NotStarted);
    connect(settings_qml, &QmlSettings::audioVolumeChanged, this, &QmlBackend::updateAudioVolume);
    initOverlay();
    connect(settings_qml, &QmlSettings::placeboChanged, window, &QmlMainWindow::updatePlacebo);
        connect(settings_qml,
            &QmlSettings::vSyncEnabledChanged,
            window,
            &QmlMainWindow::updateVSync,
            Qt::QueuedConnection);
    connect(settings_qml,
            &QmlSettings::vulkanDeferredSwapChanged,
            window,
            &QmlMainWindow::updateVulkanDeferredSwap,
            Qt::QueuedConnection);
    connect(settings_qml, &QmlSettings::streamMenuEnabledChanged, this, &QmlBackend::updateStreamShortcut);
    connect(settings_qml, &QmlSettings::streamMenuShortcut1Changed, this, &QmlBackend::updateStreamShortcut);
    connect(settings_qml, &QmlSettings::streamMenuShortcut2Changed, this, &QmlBackend::updateStreamShortcut);
    connect(settings_qml, &QmlSettings::streamMenuShortcut3Changed, this, &QmlBackend::updateStreamShortcut);
    connect(settings_qml, &QmlSettings::streamMenuShortcut4Changed, this, &QmlBackend::updateStreamShortcut);
    connect(settings, &Settings::RegisteredHostsUpdated, this, &QmlBackend::hostsChanged);
    connect(settings, &Settings::HiddenHostsUpdated, this, &QmlBackend::hiddenHostsChanged);
    connect(settings, &Settings::ManualHostsUpdated, this, &QmlBackend::hostsChanged);
    connect(settings, &Settings::CurrentProfileChanged, this, &QmlBackend::profileChanged);
    connect(&discovery_manager, &DiscoveryManager::HostsUpdated, this, &QmlBackend::updateDiscoveryHosts);
    connect(&discovery_manager, &DiscoveryManager::LocalNetworkBlockedChanged, this, &QmlBackend::localNetworkBlockedChanged);
    discovery_manager.SetSettings(settings);
    setDiscoveryEnabled(true);
    connect(ControllerManager::GetInstance(), &ControllerManager::AvailableControllersUpdated, this, &QmlBackend::updateControllers);
    connect(settings_qml, &QmlSettings::allowJoystickBackgroundEventsChanged, this, &QmlBackend::setAllowJoystickBackgroundEvents);
    connect(window, &QmlMainWindow::activeChanged, this, &QmlBackend::setIsAppActive);
    connect(this, &QmlBackend::sessionChanged, this, &QmlBackend::setIsAppActive);
    setAllowJoystickBackgroundEvents();
    setIsAppActive();
    ControllerManager::GetInstance()->SetIsAppActive(window->isActive());
    updateControllers();
    updateControllerMappings();
    connect(settings, &Settings::ControllerMappingsUpdated, this, &QmlBackend::updateControllerMappings);
    connect(this, &QmlBackend::controllersChanged, this, &QmlBackend::updateControllerMappings);
    auto_connect_mac = settings->GetAutoConnectHost().GetServerMAC();
    auto_connect_nickname = settings->GetAutoConnectHost().GetServerNickname();
    psn_auto_connect_timer = new QTimer(this);
    psn_auto_connect_timer->setSingleShot(true);
    psn_reconnect_tries = 0;
    psn_reconnect_timer = new QTimer(this);
    wakeup_start_timer = new QTimer(this);
    wakeup_start_timer->setSingleShot(true);
    wakeup_resend_timer = new QTimer(this);
    wakeup_resend_timer->setInterval(WAKEUP_RESEND_MS);
    console_reconnect_timer = new QTimer(this);
    console_reconnect_timer->setSingleShot(true);
    connect(console_reconnect_timer, &QTimer::timeout, this, &QmlBackend::consoleReconnectAttempt);
    if(autoConnect() && !auto_connect_nickname.isEmpty())
    {
        connect(psn_auto_connect_timer, &QTimer::timeout, this, [this]
        {
            int i = 0;
            for (const auto &host : std::as_const(psn_hosts))
            {
                if(host.GetName() == auto_connect_nickname)
                {
                    int index = discovery_manager.GetHosts().size() + this->settings->GetManualHosts().size() + i;
                    startConsole(index);
                    return;
                }
                i++;
            }
            qCWarning(chiakiGui) << "Couldn't find PSN host with the requested nickname: " << auto_connect_nickname;
        });
        psn_auto_connect_timer->start(PSN_INTERNET_WAIT_SECONDS * 1000);
    }
    connect(psn_reconnect_timer, &QTimer::timeout, this, [this]{
        QString refresh = this->settings->GetPsnRefreshToken();
        if(refresh.isEmpty())
        {
            qCWarning(chiakiGui) << "No refresh token found, can't refresh PSN token to use PSN remote connection";
            psn_reconnect_tries = 0;
            resume_session = false;
            psn_reconnect_timer->stop();
            setConnectState(PsnConnectState::ConnectFailed);
            return;
        }
        PSNToken *psnToken = new PSNToken(this->settings, this);
        connect(psnToken, &PSNToken::PSNTokenError, this, [this](const QString &error) {
            qCWarning(chiakiGui) << "Internet is currently down...waiting 5 seconds" << error;
            psn_reconnect_tries++;
            if(psn_reconnect_tries < MAX_PSN_RECONNECT_TRIES)
                return;
            else
            {
                resume_session = false;
                psn_reconnect_tries = 0;
                psn_reconnect_timer->stop();
                setConnectState(PsnConnectState::ConnectFailed);
            }
        });
        connect(psnToken, &PSNToken::UnauthorizedError, this, &QmlBackend::psnCredsExpired);
        connect(psnToken, &PSNToken::PSNTokenSuccess, this, [this]() {
            qCWarning(chiakiGui) << "PSN Remote Connection Tokens Refreshed. Internet is back up";
            resume_session = false;
            psn_reconnect_tries = 0;
            psn_reconnect_timer->stop();
            createSession(session_info);
        });
        connect(psnToken, &PSNToken::Finished, psnToken, &QObject::deleteLater);
        QString refresh_token = this->settings->GetPsnRefreshToken();
        psnToken->RefreshPsnToken(std::move(refresh_token));
    });
    connect(wakeup_start_timer, &QTimer::timeout, this, [this]
    {
        // Woken for the friends in its room, and it did not come: the room stays
        if (asleep_console && session && !session->IsAsleepRoom()) {
            qCWarning(chiakiGui) << "Play Together: the console did not wake up, the room stays";
            wakeup_start = false;
            wakeup_nickname.clear();
            session->SetKeepGuests(true);
            if (enterAsleepRoom())
                emit error(tr("Play Together"), tr("The console did not wake up. You and your friends can try again."));
            return;
        }
        emit wakeupStartFailed();
    });
    connect(wakeup_resend_timer, &QTimer::timeout, this, [this]
    {
        // stop once the console woke up, the wait was cancelled or it timed out
        if(!wakeup_start || !wakeup_start_timer->isActive())
        {
            wakeup_resend_timer->stop();
            return;
        }
        // the console this session is waiting for, not whichever was woken last
        if(session_info.host.isEmpty())
            return;
        try {
            discovery_manager.SendWakeup(session_info.host, session_info.regist_key, chiaki_target_is_ps5(session_info.target));
        } catch (const Exception &e) {
            qCWarning(chiakiGui) << "Failed to resend Wakeup packet:" << e.what();
        }
    });
    psn_auto_connect_timer->start(PSN_INTERNET_WAIT_SECONDS * 1000);
    sleep_inhibit = new SystemdInhibit(QGuiApplication::applicationName(), tr("Remote Play session"), "sleep", "delay", this);
    connect(sleep_inhibit, &SystemdInhibit::sleep, this, &QmlBackend::goToSleep);
    connect(sleep_inhibit, &SystemdInhibit::resume, this, &QmlBackend::resumeFromSleep);
    connect(ControllerManager::GetInstance(), &ControllerManager::ControllerMoved, sleep_inhibit, &SystemdInhibit::simulateUserActivity);
#ifdef Q_OS_MACOS
    mac_wake_sleep = new MacWakeSleep(this);
    connect(mac_wake_sleep, &MacWakeSleep::wokeUp, this, &QmlBackend::resumeFromSleep);
    connect(ControllerManager::GetInstance(), &ControllerManager::ControllerMoved, mac_wake_sleep, &MacWakeSleep::simulateUserActivity);
#elif defined(Q_OS_WINDOWS)
    windows_wake_sleep = new WindowsWakeSleep(this);
    connect(windows_wake_sleep, &WindowsWakeSleep::wokeUp, this, &QmlBackend::resumeFromSleep);
    connect(windows_wake_sleep, &WindowsWakeSleep::sleeping, this, &QmlBackend::goToSleep);
#endif
    refreshPsnToken();

    // A moment after the start, so that it never slows it down: is there a newer release?
    QTimer::singleShot(4000, this, &QmlBackend::checkForUpdate);
}

bool QmlBackend::prepareFrameForPresentation(ChiakiFfmpegFrame &frame, bool use_opengl_renderer)
{
    if (!frame.frame)
        return false;

    if (frame.frame->format == AV_PIX_FMT_NONE)
        return false;

    if (!frame.frame->hw_frames_ctx)
        return frame_has_planes(frame.frame);

    const bool direct_render = !disable_zero_copy && frame_can_use_direct_render(frame.frame, use_opengl_renderer);
    if (direct_render)
        return frame_has_planes(frame.frame);

    const int format = frame.frame->format;
    {
        QMutexLocker locker(&hw_transfer_state_mutex);
        if (!logged_hw_transfer_formats.contains(format)) {
            logged_hw_transfer_formats.insert(format);
            const char *format_name = av_get_pix_fmt_name(static_cast<AVPixelFormat>(format));
            qCInfo(chiakiGui) << "Transferring hardware-decoded frames to software for presentation:"
                              << (format_name ? format_name : "unknown");
        }
    }

    AVFrame *sw_frame = av_frame_alloc();
    if (!sw_frame)
        return false;

    if (av_hwframe_transfer_data(sw_frame, frame.frame, 0) < 0) {
        bool should_log_failure = false;
        {
            QMutexLocker locker(&hw_transfer_state_mutex);
            if (!logged_hw_transfer_failures.contains(format)) {
                logged_hw_transfer_failures.insert(format);
                should_log_failure = true;
            }
        }
        if (should_log_failure) {
            const char *format_name = av_get_pix_fmt_name(static_cast<AVPixelFormat>(format));
            qCWarning(chiakiGui) << "Failed to transfer hardware frame for presentation:"
                                 << (format_name ? format_name : "unknown");
        }
        av_frame_free(&sw_frame);
        return false;
    }

    av_frame_copy_props(sw_frame, frame.frame);
    av_frame_free(&frame.frame);
    frame.frame = sw_frame;
    return frame_has_planes(frame.frame);
}

QmlBackend::~QmlBackend()
{
    live_controller->Shutdown();
    teardownSession();
#ifdef CHIAKI_HAVE_WEBENGINE
    if(request_interceptor)
        request_interceptor->deleteLater();
#endif
    frame_thread->quit();
    frame_thread->wait();
    frame_thread->parent()->deleteLater();
    psn_connection_thread.quit();
    psn_connection_thread.wait();
}

void QmlBackend::teardownSession()
{
    if (!session)
        return;

    chiaki_log_mutex.lock();
    chiaki_log_ctx = nullptr;
    chiaki_log_mutex.unlock();

    // 1) Stop any new signal deliveries from the session, most importantly the
    //    frame-thread FfmpegFrameAvailable handler.
    session->disconnect();

    // 2) Drain the frame thread's already-queued lambdas. QMetaObject::invoke
    //    queues this barrier *after* any in-flight FfmpegFrameAvailable call, so
    //    blocking until it runs guarantees no handler still touches `session`
    //    before we schedule its deletion below. Without this, deleteLater's
    //    asynchronous free races the frame thread reading `session`, which is the
    //    use-after-free crash in session->GetFfmpegDecoder().
    if (QObject *frame_ctx = frame_thread ? frame_thread->parent() : nullptr) {
        QMetaObject::invokeMethod(frame_ctx, []() {}, Qt::BlockingQueuedConnection);
    }

    // 3) Safe now: schedule the deferred delete and drop the member.
    session->deleteLater();
    session = nullptr;
}

QmlMainWindow *QmlBackend::qmlWindow() const
{
    return window;
}

QmlSettings *QmlBackend::qmlSettings() const
{
    return settings_qml;
}

StreamSession *QmlBackend::qmlSession() const
{
    return session;
}

void QmlBackend::updateAudioVolume()
{
    if(session)
        session->SetAudioVolume(settings->GetAudioVolume());
}

void QmlBackend::goToSleep()
{
    qCInfo(chiakiGui) << "About to sleep";
    if (session && console_reconnecting) {
        // The console is gone already; getting it back is for after waking up
        const bool waiting = console_reconnect_waiting;
        stopConsoleReconnect();
        resume_session = true;
        if (waiting)
            finishSession();
        else
            session->Stop();
        return;
    }
    if (session) {
        if (this->settings->GetSuspendAction() == SuspendAction::Sleep)
            session->GoToBed();
        session->Stop();
        if(!session_info.duid.isEmpty())
            psnCancel(true);
        resume_session = true;
    }
}
void QmlBackend::resumeFromSleep()
{
#ifdef Q_OS_WINDOWS
    if(windows_wake_sleep->getWakeState() == WindowsWakeState::AboutToSleep)
    {
        windows_wake_sleep->setWakeState(WindowsWakeState::Awake);
        return;
    }
    windows_wake_sleep->setWakeState(WindowsWakeState::Awake);
#endif
    if (resume_session) {
        qCInfo(chiakiGui) << "Resuming session...";
        resume_session = false;
        if(session_info.duid.isEmpty())
        {
            createSession({
                session_info.settings,
                session_info.target,
                session_info.host,
                session_info.nickname,
                session_info.regist_key,
                session_info.morning,
                session_info.initial_login_pin,
                session_info.duid,
                session_info.auto_regist,
                session_info.fullscreen,
                session_info.zoom,
                session_info.stretch,
            });
        }
        else
        {
            emit showPsnView();
            setConnectState(PsnConnectState::WaitingForInternet);
            psn_reconnect_timer->start(PSN_INTERNET_WAIT_SECONDS * 1000);
        }
    }
}

QList<QmlController*> QmlBackend::qmlControllers() const
{
    return controllers.values();
}

void QmlBackend::profileChanged()
{
    QString profile = settings->GetCurrentProfile();
    Settings *settings_copy = new Settings(profile);
    if(settings_allocd)
        settings->deleteLater();
    settings_allocd = true;
    settings = settings_copy;
    emit hostsChanged();
    updateControllerMappings();
    connect(settings, &Settings::RegisteredHostsUpdated, this, &QmlBackend::hostsChanged);
    connect(settings, &Settings::HiddenHostsUpdated, this, &QmlBackend::hiddenHostsChanged);
    connect(settings, &Settings::ManualHostsUpdated, this, &QmlBackend::hostsChanged);
    connect(settings, &Settings::CurrentProfileChanged, this, &QmlBackend::profileChanged);
    connect(settings, &Settings::ControllerMappingsUpdated, this, &QmlBackend::updateControllerMappings);
    settings_qml->setSettings(settings);
    discovery_manager.SetSettings(settings);
    window->setSettings(settings);
    if (device_check)
        device_check->SetSettings(settings);
    if (key_mouse)
        key_mouse->SetSettings(settings);
    if (intro_sound)
        intro_sound->SetSettings(settings);
    setDiscoveryEnabled(true);

    auto_connect_mac = settings->GetAutoConnectHost().GetServerMAC();
    auto_connect_nickname = settings->GetAutoConnectHost().GetServerNickname();
    psn_reconnect_timer->deleteLater();
    psn_auto_connect_timer->deleteLater();
    psn_auto_connect_timer = new QTimer(this);
    psn_auto_connect_timer->setSingleShot(true);
    psn_reconnect_tries = 0;
    psn_reconnect_timer = new QTimer(this);
    if(autoConnect() && !auto_connect_nickname.isEmpty())
    {
        connect(psn_auto_connect_timer, &QTimer::timeout, this, [this]
        {
            int i = 0;
            for (const auto &host : std::as_const(psn_hosts))
            {
                if(host.GetName() == auto_connect_nickname)
                {
                    int index = discovery_manager.GetHosts().size() + this->settings->GetManualHosts().size() + i;
                    startConsole(index);
                    return;
                }
                i++;
            }
            qCWarning(chiakiGui) << "Couldn't find PSN host with the requested nickname: " << auto_connect_nickname;
        });
        psn_auto_connect_timer->start(PSN_INTERNET_WAIT_SECONDS * 1000);
    }
    connect(psn_reconnect_timer, &QTimer::timeout, this, [this]{
        QString refresh = this->settings->GetPsnRefreshToken();
        if(refresh.isEmpty())
        {
            qCWarning(chiakiGui) << "No refresh token found, can't refresh PSN token to use PSN remote connection";
            psn_reconnect_tries = 0;
            resume_session = false;
            psn_reconnect_timer->stop();
            setConnectState(PsnConnectState::ConnectFailed);
            return;
        }
        PSNToken *psnToken = new PSNToken(this->settings, this);
        connect(psnToken, &PSNToken::PSNTokenError, this, [this](const QString &error) {
            qCWarning(chiakiGui) << "Internet is currently down...waiting 5 seconds" << error;
            psn_reconnect_tries++;
            if(psn_reconnect_tries < MAX_PSN_RECONNECT_TRIES)
                return;
            else
            {
                resume_session = false;
                psn_reconnect_tries = 0;
                psn_reconnect_timer->stop();
                setConnectState(PsnConnectState::ConnectFailed);
            }
        });
        connect(psnToken, &PSNToken::UnauthorizedError, this, &QmlBackend::psnCredsExpired);
        connect(psnToken, &PSNToken::PSNTokenSuccess, this, []() {
            qCWarning(chiakiGui) << "PSN Remote Connection Tokens Refreshed. Internet is back up";
        });
        connect(psnToken, &PSNToken::PSNTokenSuccess, this, [this]() {
            resume_session = false;
            psn_reconnect_tries = 0;
            psn_reconnect_timer->stop();
            createSession(session_info);
        });
        connect(psnToken, &PSNToken::Finished, psnToken, &QObject::deleteLater);
        QString refresh_token = this->settings->GetPsnRefreshToken();
        psnToken->RefreshPsnToken(std::move(refresh_token));
    });
    sleep_inhibit->deleteLater();
    sleep_inhibit = new SystemdInhibit(QGuiApplication::applicationName(), tr("Remote Play session"), "sleep", "delay", this);
    connect(sleep_inhibit, &SystemdInhibit::sleep, this, &QmlBackend::goToSleep);
    connect(sleep_inhibit, &SystemdInhibit::resume, this, &QmlBackend::resumeFromSleep);
    connect(ControllerManager::GetInstance(), &ControllerManager::ControllerMoved, sleep_inhibit, &SystemdInhibit::simulateUserActivity);
#ifdef Q_OS_MACOS
    mac_wake_sleep->deleteLater();
    mac_wake_sleep = new MacWakeSleep(this);
    connect(mac_wake_sleep, &MacWakeSleep::wokeUp, this, &QmlBackend::resumeFromSleep);
    connect(ControllerManager::GetInstance(), &ControllerManager::ControllerMoved, mac_wake_sleep, &MacWakeSleep::simulateUserActivity);
#elif defined(Q_OS_WINDOWS)
    windows_wake_sleep->deleteLater();
    windows_wake_sleep = new WindowsWakeSleep(this);
    connect(windows_wake_sleep, &WindowsWakeSleep::wokeUp, this, &QmlBackend::resumeFromSleep);
    connect(windows_wake_sleep, &WindowsWakeSleep::sleeping, this, &QmlBackend::goToSleep);
#endif
    refreshPsnToken();
    emit hostsChanged();
    emit hiddenHostsChanged();
}

bool QmlBackend::discoveryEnabled() const
{
    return discovery_manager.GetActive();
}

void QmlBackend::setDiscoveryEnabled(bool enabled)
{
    discovery_manager.SetActive(enabled);
    emit discoveryEnabledChanged();
}

QmlBackend::PsnConnectState QmlBackend::connectState() const
{
    return psn_connect_state;
}

void QmlBackend::checkNickname(QString nickname)
{
    if(!session)
        return;
    if(!settings->GetNicknameRegisteredHostRegistered(nickname))
    {
        emit error(tr("PS4 Console Unregistered"), tr("Can't proceed...please register your PS4 console locally"));
        session->Stop();
    }
}

void QmlBackend::setConnectState(PsnConnectState connect_state)
{
    psn_connect_state = connect_state;
    if(connect_state == PsnConnectState::ConnectFailed)
    {
        if(!this->settings->GetGeometry().isEmpty())
        {
            window->setGeometry(this->settings->GetGeometry());
            window->normalTime();
        }
        else
            window->showMaximized();
    }
    emit connectStateChanged();
}

QVariantList QmlBackend::hosts() const
{
    QVariantList out;
    QList<QString> discovered_nicknames;
    QList<ManualHost> discovered_manual_hosts;
    size_t registered_discovered_ps4s = 0;
    auto manual_hosts = settings->GetManualHosts();
    for (const auto &host : discovery_manager.GetHosts()) {
        QVariantMap m;
        HostMAC host_mac = host.GetHostMAC();
        bool registered = settings->GetRegisteredHostRegistered(host_mac);
        bool hidden = settings->GetHiddenHostHidden(host_mac);
        if(registered && hidden)
        {
            settings->RemoveHiddenHost(host_mac);
            bool hidden = false;
        }
        // Update hidden host nickname if it's changed
        if(hidden)
        {
            auto hidden_host = settings->GetHiddenHost(host_mac);
            if(hidden_host.GetNickname() != host.host_name)
            {
                hidden_host.SetNickname(host.host_name);
                settings->RemoveHiddenHost(host_mac);
                settings->AddHiddenHost(hidden_host);
            }
        }
        m["discovered"] = true;
        bool manual = false;
        for(int i = 0; i < manual_hosts.length(); i++)
        {
            const auto &manual_host = manual_hosts.at(i);
            if(manual_host.GetRegistered() && manual_host.GetMAC() == host_mac && manual_host.GetHost() == host.host_addr)
            {
                manual = true;
                discovered_manual_hosts.append(manual_host);
            }
        }
        m["manual"] = manual;
        m["name"] = host.host_name;
        QString duid = "";
        if(!registered)
        {
            if(psn_nickname_hosts.contains(host.host_name))
                duid = psn_nickname_hosts.value(host.host_name).GetDuid();
            else if(!host.ps5)
                duid =  psn_nickname_hosts.value(QString("Main PS4 Console")).GetDuid();
        }
        m["duid"] = duid;
        m["address"] = host.host_addr;
        m["ps5"] = host.ps5;
        m["mac"] = host_mac.ToString();
        m["state"] = chiaki_discovery_host_state_string(host.state);
        m["app"] = host.running_app_name;
        m["titleId"] = host.running_app_titleid;
        m["registered"] = registered;
        m["display"] = hidden ? false : true;
        discovered_nicknames.append(host.host_name);
        out.append(m);
        if(!host.ps5 && registered)
            registered_discovered_ps4s++;
    }
    for (const auto &host : settings->GetManualHosts()) {
        QVariantMap m;
        m["discovered"] = false;
        m["manual"] = true;
        m["name"] = host.GetHost();
        m["duid"] = "";
        m["address"] = host.GetHost();
        m["state"] = "unknown";
        m["registered"] = false;
        m["display"] = discovered_manual_hosts.contains(host) ? false : true;
        if (host.GetRegistered() && settings->GetRegisteredHostRegistered(host.GetMAC())) {
            auto registered = settings->GetRegisteredHost(host.GetMAC());
            m["registered"] = true;
            m["name"] = registered.GetServerNickname();
            m["ps5"] = chiaki_target_is_ps5(registered.GetTarget());
            m["mac"] = registered.GetServerMAC().ToString();
        }
        out.append(m);
    }
    if(registered_discovered_ps4s >= settings->GetPS4RegisteredHostsRegistered())
        discovered_nicknames.append(QString("Main PS4 Console"));
    for (const auto &host : psn_hosts) {
        QVariantMap m;
        // Only list PSN remote hosts that aren't discovered locally
        bool discovered = false;
        for (int i = 0; i < discovered_nicknames.size(); ++i)
        {
            if (discovered_nicknames.at(i) == host.GetName())
                discovered = true;
        }
        for (int i = 0; i < waking_sleeping_nicknames.size(); ++i)
        {
            if (waking_sleeping_nicknames.at(i) == host.GetName())
                discovered = true;
        }
        if(discovered)
            continue;
        m["discovered"] = false;
        m["manual"] = false;
        m["display"] = true;
        m["name"] = host.GetName();
        m["duid"] = host.GetDuid();
        m["address"] = "";
        m["registered"] = true;
        m["ps5"] = host.IsPS5();
        out.append(m);
    }
    return out;
}

bool QmlBackend::autoConnect() const
{
    return auto_connect_mac.GetValue();
}

void QmlBackend::psnCancel(bool stop_thread)
{
    session->CancelPsnConnection(stop_thread);
}

void QmlBackend::checkPsnConnection(const ChiakiErrorCode &err)
{
    switch(err)
    {
        case CHIAKI_ERR_SUCCESS:
            setConnectState(PsnConnectState::LinkingConsole);
            psnSessionStart();
            break;
        case CHIAKI_ERR_HOST_DOWN:
            setConnectState(PsnConnectState::ConnectFailedStart);
            teardownSession();
            setDiscoveryEnabled(true);
            break;
        case CHIAKI_ERR_HOST_UNREACH:
            setConnectState(PsnConnectState::ConnectFailedConsoleUnreachable);
            teardownSession();
            setDiscoveryEnabled(true);
            break;
        default:
            setConnectState(PsnConnectState::ConnectFailed);
            teardownSession();
            setDiscoveryEnabled(true);
            break;
    }
}

void QmlBackend::psnSessionStart()
{
    startSession(true);
}

void QmlBackend::startSession(bool emit_session_changed)
{
    if (window)
        window->armVerbosePlaceboQuietWindow();

    // A session that takes the place of a lost one finds the window as the user left it
    if (!console_reconnecting)
        applyStartupWindowSizing();
    if (emit_session_changed)
        prepareStartupPresentation(true);

    try {
        session->Start();
    } catch (const Exception &e) {
        if (window) {
            window->resetPlaceboQueue();
            window->requestOverlayUpdate();
        }
        teardownSession();
        emit sessionChanged(nullptr);
        emit error(tr("Stream failed"), tr("Failed to start Stream Session: %1").arg(e.what()));
        return;
    }

    if (emit_session_changed)
        emit sessionChanged(session);

    // Still held from the session that was lost
    if (!console_reconnecting)
        sleep_inhibit->inhibit();
}

// ------------------------------------------------------------------ reconnecting
//
// A console session that was lost (an error after it had been connected, or no picture for a
// while, see StreamSession::ReconnectWanted()) is replaced by a new one made from the same
// session_info. The lost session object stays in place until its successor is created, so
// that the window and the stream view go from one session straight to the next. A guest of
// a shared session joins again inside its session object, see streamsession_coplay.cpp.

/** Everything from losing the console to having it back must fit into this. */
static const int CONSOLE_RECONNECT_BUDGET_MS = 60 * 1000;
/** Wait before each attempt. The console can say "Remote Play already in use" for some seconds. */
static const int CONSOLE_RECONNECT_DELAYS_MS[] = { 1500, 2500, 4000, 6000, 8000 };
/** A session lost again this soon after it came back goes on with the time that was left. */
static const int CONSOLE_RECONNECT_STABLE_MS = 20 * 1000;

bool QmlBackend::reconnecting() const
{
    return console_reconnecting || (session && session->IsCoPlayReconnecting());
}

QString QmlBackend::reconnectStatus() const
{
    if (console_reconnecting)
        return tr("Connection lost — reconnecting…");
    if (session && session->IsCoPlayReconnecting())
        return session->CoPlayReconnectStatus();
    return QString();
}

void QmlBackend::cancelReconnect()
{
    if (!session)
        return;
    if (session->IsCoPlayReconnecting()) {
        // A guest: its session ends the usual way
        session->Stop();
        return;
    }
    if (!console_reconnecting)
        return;
    qCInfo(chiakiGui) << "Reconnecting to the console cancelled";
    const bool waiting = console_reconnect_waiting;
    stopConsoleReconnect();
    if (waiting)
        finishSession(); // nothing runs that could still report a quit
    else
        session->Stop();
}

/** The session was lost: schedules the next attempt, or says that the time is up. */
bool QmlBackend::scheduleConsoleReconnect()
{
    if (!console_reconnecting) {
        const bool lost_again = console_reconnect_clock.isValid() && console_reconnect_stable.isValid()
                && console_reconnect_stable.elapsed() < CONSOLE_RECONNECT_STABLE_MS;
        if (!lost_again) {
            console_reconnect_clock.start();
            console_reconnect_attempt = 0;
        }
    }
    const int delay_count = sizeof(CONSOLE_RECONNECT_DELAYS_MS) / sizeof(CONSOLE_RECONNECT_DELAYS_MS[0]);
    const int delay = CONSOLE_RECONNECT_DELAYS_MS[qMin(console_reconnect_attempt, delay_count - 1)];
    if (console_reconnect_clock.elapsed() + delay > CONSOLE_RECONNECT_BUDGET_MS) {
        qCWarning(chiakiGui) << "Giving up on getting the console back after" << console_reconnect_attempt << "attempts";
        return false;
    }
    console_reconnect_attempt++;
    console_reconnect_stable.invalidate();
    console_reconnect_waiting = true;
    console_reconnect_timer->start(delay);
    qCInfo(chiakiGui) << "Connection to the console lost, attempt" << console_reconnect_attempt << "to get it back in" << delay << "ms";
    if (!console_reconnecting) {
        console_reconnecting = true;
        emit reconnectingChanged();
    }
    return true;
}

void QmlBackend::consoleReconnectAttempt()
{
    if (!console_reconnecting || !console_reconnect_waiting)
        return;
    console_reconnect_waiting = false;

    // The lost session goes, and its successor takes its place before anybody looks
    teardownSession();
    // Straight to the console: it was awake a moment ago
    wakeup_nickname.clear();
    // A log of its own, the lost session's stays as it is
    session_info.log_file = CreateLogFilename();
    bool announced = false;
    auto connection = connect(this, &QmlBackend::sessionChanged, this, [&announced]() { announced = true; });
    createSession(session_info);
    disconnect(connection);
    if (session)
        return;

    // Could not even be set up, and createSession() has said why
    stopConsoleReconnect();
    if (!announced)
        emit sessionChanged(nullptr);
    sleep_inhibit->release();
    setDiscoveryEnabled(true);
    emit sessionEnded();
}

bool QmlBackend::keepRoomAsleepPossible() const
{
    // Only a console that this computer can wake: one on its own network
    return settings->GetCoPlayKeepRoomAsleep() && session_info.coplay_host_enabled && !session_info.pc_source
            && !session_info.coplay_client && session_info.duid.isEmpty() && !session_info.host.isEmpty()
            && !session_info.regist_key.isEmpty();
}

bool QmlBackend::enterAsleepRoom()
{
    const StreamSessionConnectInfo console = session_info;
    StreamSessionConnectInfo info = console;
    info.coplay_asleep = true;
    info.coplay_room_game = QString();
    asleep_console = std::make_unique<StreamSessionConnectInfo>(console);
    qCInfo(chiakiGui) << "Play Together: the console sleeps, the room stays";
    teardownSession();
    wakeup_nickname.clear();
    wakeup_start = false;
    wakeup_start_timer->stop();
    createSession(info);
    // The console, to wake later
    session_info = console;
    if (!session) {
        asleep_console.reset();
        emit sessionChanged(nullptr);
        sleep_inhibit->release();
        setDiscoveryEnabled(true);
        emit sessionEnded();
        return true;
    }
    // To see the console wake up
    setDiscoveryEnabled(true);
    return true;
}

void QmlBackend::wakeAsleepConsole(const QString &who)
{
    if (!session || !session->IsAsleepRoom() || !asleep_console)
        return;
    const StreamSessionConnectInfo console = *asleep_console;
    if (!sendWakeup(console.host, console.regist_key, chiaki_target_is_ps5(console.target))) {
        emit error(tr("Play Together"), tr("The console could not be woken from this computer."));
        return;
    }
    qCInfo(chiakiGui) << "Play Together:" << (who.isEmpty() ? QStringLiteral("the host") : who) << "wakes the console";
    session->SetKeepGuests(true);
    teardownSession();
    // createSession() waits for the console to come up (wakeup_start), then connects
    wakeup_nickname = console.nickname;
    createSession(console);
    if (!session) {
        asleep_console.reset();
        emit sessionChanged(nullptr);
        sleep_inhibit->release();
        setDiscoveryEnabled(true);
        emit sessionEnded();
        return;
    }
    // Until the console plays: the friends wait for it, and a failure brings the room back
    session->SetKeepGuests(true);
}

void QmlBackend::stopConsoleReconnect()
{
    console_reconnect_timer->stop();
    console_reconnect_waiting = false;
    console_reconnect_clock.invalidate();
    console_reconnect_stable.invalidate();
    if (!console_reconnecting)
        return;
    console_reconnecting = false;
    emit reconnectingChanged();
}

/** The current session is over for good: takes it apart and tells everybody. */
void QmlBackend::finishSession()
{
    asleep_console.reset();
    asleep_room_pending = false;
    // Who had the key of a public room cannot come back to the next one
    if (session && session->GetCoPlayPublic())
        settings->SetCoPlayPublicSecret(QString::fromStdString(coplay::SecretToString(coplay::GenerateSecret())));
    pending_recovered_frame.storeRelaxed(0);
    {
        QMutexLocker locker(&hw_transfer_state_mutex);
        logged_hw_transfer_formats.clear();
        logged_hw_transfer_failures.clear();
    }

    teardownSession();
    emit sessionChanged(session);
    emit reconnectingChanged();

    sleep_inhibit->release();
    setDiscoveryEnabled(true);
    emit sessionEnded();
}

void QmlBackend::createSession(const StreamSessionConnectInfo &connect_info)
{
    pending_recovered_frame.storeRelaxed(0);

    if (autoConnect()) {
        auto_connect_mac = {};
        emit autoConnectChanged();
    }

    if (session) {
        qCWarning(chiakiGui) << "Another session is already active";
        return;
    }

    session_info = connect_info;
    window->setStreamMaxFPS(connect_info.video_profile.max_fps);
    disable_zero_copy = !settings->GetUseZeroCopy();
    {
        QMutexLocker locker(&hw_transfer_state_mutex);
        logged_hw_transfer_formats.clear();
        logged_hw_transfer_failures.clear();
    }
    QStringList availableDecoders = settings_qml->availableDecoders();
    if (!session_info.hw_decoder.isEmpty()
        && session_info.hw_decoder != "auto"
        && session_info.hw_decoder != "none"
        && !availableDecoders.contains(session_info.hw_decoder)) {
        qCInfo(chiakiGui) << "Configured hw decoder" << session_info.hw_decoder
                          << "is not currently available, falling back to auto";
        session_info.hw_decoder = "auto";
    }
    bool use_opengl_renderer = window && window->runtimeRendererBackend() == static_cast<int>(RenderBackend::OpenGL);
    bool prefer_cuda = window && window->nvidiaCard() && availableDecoders.contains("cuda");
    auto fallbackVulkanDecoderForOpenGL = [&]() {
        if (!use_opengl_renderer || session_info.hw_decoder != "vulkan")
            return;

        bool fallbackApplied = false;
#if defined(Q_OS_LINUX)
        if (prefer_cuda)
        {
            qCInfo(chiakiGui) << "Renderer backend is OpenGL, falling back from vulkan decoder to cuda";
            session_info.hw_decoder = "cuda";
            fallbackApplied = true;
        }
        else
        if (availableDecoders.contains("vaapi"))
        {
            qCInfo(chiakiGui) << "Renderer backend is OpenGL, falling back from vulkan decoder to vaapi";
            session_info.hw_decoder = "vaapi";
            fallbackApplied = true;
        }
#elif defined(Q_OS_WIN)
        if (prefer_cuda)
        {
            qCInfo(chiakiGui) << "Renderer backend is OpenGL, falling back from vulkan decoder to cuda";
            session_info.hw_decoder = "cuda";
            fallbackApplied = true;
        }
        else
        if (availableDecoders.contains("d3d11va"))
        {
            qCInfo(chiakiGui) << "Renderer backend is OpenGL, falling back from vulkan decoder to d3d11va";
            session_info.hw_decoder = "d3d11va";
            fallbackApplied = true;
        }
#elif defined(Q_OS_MACOS)
        if (availableDecoders.contains("videotoolbox"))
        {
            qCInfo(chiakiGui) << "Renderer backend is OpenGL, falling back from vulkan decoder to videotoolbox";
            session_info.hw_decoder = "videotoolbox";
            fallbackApplied = true;
        }
#endif
        if (!fallbackApplied)
        {
            qCInfo(chiakiGui) << "Renderer backend is OpenGL, falling back from vulkan decoder to software decoder";
            session_info.hw_decoder.clear();
        }
    };
    if(session_info.hw_decoder == "auto")
    {
        session_info.hw_decoder = QString();
#if defined(Q_OS_LINUX)
        if(!use_opengl_renderer && availableDecoders.contains("vulkan"))
        {
            qCInfo(chiakiGui) << "Auto hw decoder selecting vulkan";
            session_info.hw_decoder = "vulkan";
        }
        else if(prefer_cuda)
        {
            qCInfo(chiakiGui) << "Auto hw decoder selecting cuda";
            session_info.hw_decoder = "cuda";
        }
        else if(availableDecoders.contains("vaapi"))
        {
            qCInfo(chiakiGui) << "Auto hw decoder selecting vaapi";
            session_info.hw_decoder = "vaapi";
        }
#elif defined(Q_OS_WIN)
        if(!use_opengl_renderer && availableDecoders.contains("vulkan"))
        {
            qCInfo(chiakiGui) << "Auto hw decoder selecting vulkan";
            session_info.hw_decoder = "vulkan";
        }
        else if(prefer_cuda)
        {
            qCInfo(chiakiGui) << "Auto hw decoder selecting cuda";
            session_info.hw_decoder = "cuda";
        }
        else if(availableDecoders.contains("d3d11va"))
        {
            qCInfo(chiakiGui) << "Auto hw decoder selecting d3d11va";
            session_info.hw_decoder = "d3d11va";
        }
#elif defined(Q_OS_MACOS)
        if(availableDecoders.contains("videotoolbox"))
        {
            qCInfo(chiakiGui) << "Auto hw decoder selecting videotoolbox";
            session_info.hw_decoder = "videotoolbox";
        }
#endif
    }
    fallbackVulkanDecoderForOpenGL();
    if (session_info.hw_decoder == "vulkan") {
        session_info.hw_device_ctx = window->vulkanHwDeviceCtx();
        if (!session_info.hw_device_ctx)
        {
            session_info.hw_decoder.clear();
            qCInfo(chiakiGui) << "vulkan video decoding not supported by your gpu driver, retrying other hw video decoders";
#if defined(Q_OS_LINUX)
            if(prefer_cuda)
            {
                qCInfo(chiakiGui) << "Falling back to cuda";
                session_info.hw_decoder = "cuda";
            }
            else if(availableDecoders.contains("vaapi"))
            {
                qCInfo(chiakiGui) << "Falling back to vaapi";
                session_info.hw_decoder = "vaapi";
            }
#elif defined(Q_OS_WIN)
            if(prefer_cuda)
            {
                qCInfo(chiakiGui) << "Falling back to cuda";
                session_info.hw_decoder = "cuda";
            }
            else if(availableDecoders.contains("d3d11va"))
            {
                qCInfo(chiakiGui) << "Falling back to d3d11va";
                session_info.hw_decoder = "d3d11va";
            }
#endif
        }
        if(session_info.hw_decoder.isEmpty())
            qCInfo(chiakiGui) << "Falling back to software decoder";
    }

    try {
        session = new StreamSession(session_info, this);
    } catch (const Exception &e) {
        emit error(tr("Stream failed"), tr("Failed to initialize Stream Session: %1").arg(e.what()));
        return;
    }

    connect(session, &StreamSession::FfmpegFrameAvailable, frame_thread->parent(), [this, use_opengl_renderer]() {
        ChiakiFfmpegDecoder *decoder = session->GetFfmpegDecoder();
        if (!decoder) {
            qCCritical(chiakiGui) << "Session has no FFmpeg decoder";
            return;
        }
        int32_t frames_lost;
        ChiakiFfmpegFrame frame = chiaki_ffmpeg_decoder_pull_frame(decoder, &frames_lost);
        if (!frame.frame)
            return;
        logDecoderDeliveryStats(static_cast<qint64>(chiaki_time_now_monotonic_us()), frames_lost, frame.recovered);
        logDecoderFramePtsStats(static_cast<qint64>(chiaki_time_now_monotonic_us()), frame.pts, frame.duration);
        if (frame.recovered)
            pending_recovered_frame.storeRelaxed(1);

        const qint64 prepare_begin_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
        if (!prepareFrameForPresentation(frame, use_opengl_renderer))
        {
            av_frame_free(&frame.frame);
            return;
        }
        const qint64 prepare_end_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
        if (prepare_end_us >= prepare_begin_us && prepare_end_us - prepare_begin_us >= 5000) {
            CHIAKI_NOISY_DEBUG().nospace()
                << "[decode] prepare_frame_us=" << (prepare_end_us - prepare_begin_us)
                << " pts=" << frame.pts;
        }

        if (pending_recovered_frame.fetchAndStoreRelaxed(0) != 0) {
            frame.recovered = true;
        }

        // The list of public rooms wants a picture of the game: this frame, made into a
        // small JPEG off this thread, while the frame itself goes on to the screen
        if (session->TakeRoomPictureRequest()) {
            AVFrame *copy = av_frame_clone(frame.frame);
            StreamSession *picture_session = session;
            if (copy) {
                QThreadPool::globalInstance()->start([this, copy, picture_session]() mutable {
                    const QByteArray jpeg = room_picture_jpeg(copy);
                    av_frame_free(&copy);
                    if (jpeg.isEmpty())
                        return;
                    QMetaObject::invokeMethod(this, [this, picture_session, jpeg]() {
                        // Only the session that asked, and only while it is still there
                        if (session == picture_session)
                            session->CoPlayRoomPicture(jpeg);
                    });
                });
            }
        }

        // A stream scene shows a picture of the game: the same, a little larger
        if (live_scenes->TakeGamePictureRequest()) {
            AVFrame *copy = av_frame_clone(frame.frame);
            if (copy) {
                QThreadPool::globalInstance()->start([this, copy]() mutable {
                    const QImage rgb = room_picture_rgb(copy);
                    av_frame_free(&copy);
                    if (rgb.isNull())
                        return;
                    QByteArray jpeg;
                    QBuffer buffer(&jpeg);
                    buffer.open(QIODevice::WriteOnly);
                    if (!rgb.scaled(640, 360, Qt::KeepAspectRatio, Qt::SmoothTransformation).save(&buffer, "JPG", 80))
                        return;
                    QMetaObject::invokeMethod(this, [this, jpeg]() { live_scenes->SetGamePicture(jpeg); });
                });
            }
        }

        const qint64 delivery_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
        QmlMainWindow *target_window = window;
        QMetaObject::invokeMethod(target_window, [target_window, frame, frames_lost, delivery_us]() mutable {
            target_window->presentFrame(frame, frames_lost, delivery_us);
        });
    });

    // An attempt to get a lost session back: this side decides about the next one
    if (console_reconnecting)
        session->SetReconnectAttempt(true);

    StreamSession *session_for_connections = session;
    connect(session, &StreamSession::SessionQuit, this, [this, session_for_connections](ChiakiQuitReason reason, const QString &reason_str) {
        if (session != session_for_connections)
            return;

        // Lost rather than ended: the session object and the stream view stay while a
        // new session is made, see consoleReconnectAttempt()
        const bool lost = session_for_connections->ReconnectWanted() && !session_for_connections->StopRequested();
        if (lost && scheduleConsoleReconnect())
            return;
        stopConsoleReconnect();

        // The console sleeps, did not come back, or did not wake: where the host wants it, the
        // room stays and the friends may wake it
        if (!session_for_connections->IsAsleepRoom()) {
            const bool woken_failed = asleep_console && !session_for_connections->StopRequested();
            const bool keep = asleep_room_pending || woken_failed || (lost && keepRoomAsleepPossible());
            asleep_room_pending = false;
            if (keep && enterAsleepRoom())
                return;
        }
        asleep_room_pending = false;

        if (chiaki_quit_reason_is_error(reason)) {
            QString m = tr("Chiaki Session has quit") + ":\n" + chiaki_quit_reason_string(reason);
            if (!reason_str.isEmpty())
                m += "\n" + tr("Reason") + ": \"" + reason_str + "\"";
            emit sessionError(tr("Session has quit"), m);
        }

        finishSession();
#ifdef Q_OS_WINDOWS
        qCInfo(chiakiGui) << "Checking sleep state: ";
        if(windows_wake_sleep->getWakeState() == WindowsWakeState::Awake)
            QTimer::singleShot(2000, this, &QmlBackend::resumeFromSleep);
        else
            windows_wake_sleep->setWakeState(WindowsWakeState::Sleeping);
#endif
    });

    connect(session, &StreamSession::LoginPINRequested, this, [this, connect_info](bool incorrect) {
        if (!connect_info.initial_login_pin.isEmpty() && incorrect == false)
            session->SetLoginPIN(connect_info.initial_login_pin);
        else
            emit sessionPinDialogRequested();
    });

    connect(session, &StreamSession::DataHolepunchProgress, this, [this](bool finished) {
        if(finished)
        {
            setConnectState(PsnConnectState::DataConnectionFinished);
        }
        else
            setConnectState(PsnConnectState::DataConnectionStart);
    });

    connect(session, &StreamSession::NicknameReceived, this, &QmlBackend::checkNickname);

    connect(session, &StreamSession::CoPlayNotice, this, [this](const QString &text) {
        emit error(tr("Play Together"), text);
    });

    // A guest left and asked for the console to sleep, which the host has allowed: the same
    // as the host choosing "Sleep" when leaving
    connect(session, &StreamSession::CoPlaySleepRequested, this, [this, session_for_connections](const QString &name) {
        if (session != session_for_connections)
            return;
        qCInfo(chiakiGui) << "Play Together:" << name << "put the console to sleep";
        stopSession(true);
        emit error(tr("Play Together"), tr("%1 left and put the console to sleep.").arg(name));
    });

    // Somebody in the room of the sleeping console wants it woken; not from inside the session's signal
    connect(session, &StreamSession::CoPlayWakeRequested, this, [this, session_for_connections](const QString &who) {
        if (session != session_for_connections)
            return;
        QTimer::singleShot(0, this, [this, who]() { wakeAsleepConsole(who); });
    });

    connect(session, &StreamSession::AutoRegistSucceeded, this, &QmlBackend::finishAutoRegister);

    connect(session, &StreamSession::ConnectedChanged, this, [this, session_for_connections]() {
        if (session != session_for_connections)
            return;

        if (!session_for_connections->IsConnected())
            return;
        // A public room tells the list which game runs, which the console's discovery answers say.
        // A game on this computer leaves the consoles alone.
        if (!session_for_connections->GetCoPlayPublic() && !session_for_connections->IsPcSource()
                && !session_for_connections->IsAsleepRoom())
            setDiscoveryEnabled(false);
        // The console of the room woke up and plays: what ends it from now on ends it as always
        if (!session_for_connections->IsAsleepRoom() && asleep_console) {
            asleep_console.reset();
            session_for_connections->SetKeepGuests(false);
        }
        if (console_reconnecting) {
            qCInfo(chiakiGui) << "Connected to the console again after" << console_reconnect_clock.elapsed() << "ms";
            // The clock runs on: a session that is lost again at once does not get all the time anew
            console_reconnecting = false;
            console_reconnect_stable.start();
            emit reconnectingChanged();
        }
    });

    // A guest that lost the host joins again inside its session
    connect(session, &StreamSession::CoPlayReconnectingChanged, this, &QmlBackend::reconnectingChanged);
    // A game on this computer names itself for the stream scenes, as a console's discovery does
    connect(session, &StreamSession::PcGameChanged, this, [this](const QString &game) {
        live_scenes->SetGame(game);
    });
    chiaki_log_mutex.lock();
    chiaki_log_ctx = session->GetChiakiLog();
    chiaki_log_mutex.unlock();

    if(connect_info.duid.isEmpty())
    {
        if(!wakeup_nickname.isEmpty())
        {
            wakeup_start = true;
            wakeup_start_timer->start(WAKEUP_WAIT_SECONDS * 1000);
            wakeup_resend_timer->start();
            emit wakeupStartInitiated();
            QTimer::singleShot(0, this, &QmlBackend::updateDiscoveryHosts);
        }
        else
        {
            startSession(true);
        }
    }
    else
    {
        setDiscoveryEnabled(false);
        emit showPsnView();
        if(session_info.auto_regist)
            setConnectState(PsnConnectState::RegisteringConsole);
        else
            setConnectState(PsnConnectState::InitiatingConnection);
            emit psnConnect(session, session_info.duid, chiaki_target_is_ps5(session_info.target));
    }
}

bool QmlBackend::closeRequested()
{
    if (!session)
        return true;

    if (reconnecting()) {
        // Nothing is connected that could be asked to stop or sent to sleep
        cancelReconnect();
        return false;
    }

    bool stop = true;
    if (session->IsCoPlayGuest()) {
        // It is the host's console. A guest is only asked about its sleep where the host lets
        // guests decide that, and never sends it to sleep without being asked: "always" in the
        // settings is about one's own console.
        if (session->IsConnected() && session->CoPlayGuestMaySleep()
                && settings->GetDisconnectAction() != DisconnectAction::AlwaysNothing) {
            stop = false;
            emit sessionStopDialogRequested();
        }
    } else if (session->IsAsleepRoom()) {
        // The console sleeps already: nothing to ask, the room just ends
    } else if (session->IsConnected()) {
        switch (settings->GetDisconnectAction()) {
        case DisconnectAction::Ask:
            stop = false;
            emit sessionStopDialogRequested();
            break;
        case DisconnectAction::AlwaysSleep:
            stopSession(true);
            return false;
        default:
            break;
        }
    }

    if (stop)
        session->Stop();

    return false;
}

void QmlBackend::deleteHost(int index)
{
    auto server = displayServerAt(index);
    auto id = server.manual_host.GetID();
    if (!server.valid || (id < 0))
        return;
    settings->RemoveManualHost(id);
}

void QmlBackend::wakeUpHost(int index, QString nickname)
{
    auto server = displayServerAt(index);
    if (!server.valid)
        return;
    if (!server.registered)
        return;
    if (!nickname.isEmpty())
    {
        waking_sleeping_nicknames.append(nickname);
        QTimer::singleShot(WAKEUP_PSN_IGNORE_SECONDS * 1000, [this, nickname]{
            waking_sleeping_nicknames.removeOne(nickname);
            emit hostsChanged();
        });
    }
    const QString wake_host = server.GetHostAddr();
    const QByteArray wake_regist_key = server.registered_host.GetRPRegistKey();
    const bool wake_ps5 = server.IsPS5();
    QTimer::singleShot(0, this, [this, wake_host, wake_regist_key, wake_ps5]() {
        if(!sendWakeup(wake_host, wake_regist_key, wake_ps5))
            qCWarning(chiakiGui) << "Couldn't wakeup server";
    });
}

void QmlBackend::setConsolePin(int index, QString console_pin)
{
    auto server = displayServerAt(index);
    if (!server.valid)
        return;
    server.registered_host.SetConsolePin(server.registered_host, std::move(console_pin));
    settings->AddRegisteredHost(server.registered_host);
}

void QmlBackend::setConsolePinForMac(const QString &mac, QString console_pin)
{
    const QList<RegisteredHost> hosts = settings->GetRegisteredHosts();
    for (RegisteredHost host : hosts) {
        if (host.GetServerMAC().ToString() != mac)
            continue;
        host.SetConsolePin(host, std::move(console_pin));
        settings->AddRegisteredHost(host);
        return;
    }
}

void QmlBackend::addManualHost(int index, const QString &address)
{
    HostMAC hmac;
    QList<RegisteredHost> registered_hosts = settings->GetRegisteredHosts();
    bool registered = (index >= 0 && (index < registered_hosts.length()));
    if (registered)
        hmac = registered_hosts.at(index).GetServerMAC();
    ManualHost host(-1, address, registered, hmac);
    settings->SetManualHost(host);
}

void QmlBackend::hideHost(const QString &mac_string, const QString &host_nickname)
{
    QByteArray mac_array = QByteArray::fromHex(mac_string.toUtf8());
    if (mac_array.size() != 6)
    {
        qCCritical(chiakiGui) << "Invalid host mac:" << mac_string.toUtf8();
        qCCritical(chiakiGui) << "Aborting hidden host creation because mac string couldn't be converted to a valid host mac!";
        qCCritical(chiakiGui) << "Received an array of unexpected size. Expected: 6 bytes, Received:" << mac_array.size() << "bytes";
        return;
    }
    HostMAC mac((const uint8_t *)mac_array.constData());
    HiddenHost hidden_host(mac, host_nickname);
    settings->AddHiddenHost(hidden_host);
    emit hostsChanged();
}

void QmlBackend::unhideHost(const QString &mac_string)
{
    QByteArray mac_array = QByteArray::fromHex(mac_string.toUtf8());
    const char *mac_ptr = mac_array.constData();
    if (strlen(mac_ptr) != 6)
    {
        qCCritical(chiakiGui) << " Aborting hidden host creation because mac string couldn't be converted to a valid host mac!";
        return;
    }
    HostMAC mac((const uint8_t *)mac_ptr);
    settings->RemoveHiddenHost(mac);
    emit hostsChanged();
}

QVariantList QmlBackend::hiddenHosts() const
{
    QVariantList out;
    for (const auto &host : settings->GetHiddenHosts()) {
        QVariantMap m;
        m["name"] = host.GetNickname();
        m["mac"] = host.GetMAC().ToString();
        out.append(m);
    }
    return out;
}


bool QmlBackend::registerHost(const QString &host, const QString &psn_id, const QString &pin, const QString &cpin, bool broadcast, int target, const QJSValue &callback)
{
    ChiakiRegistInfo info = {};
    QByteArray hostb = host.toUtf8();
    info.host = hostb.constData();
    info.target = static_cast<ChiakiTarget>(target);
    info.broadcast = broadcast;
    info.pin = (uint32_t)pin.toULong();
    info.console_pin = (uint32_t)cpin.toULong();
    info.holepunch_info = nullptr;
    info.rudp = nullptr;
    QByteArray psn_idb;
    if (target == CHIAKI_TARGET_PS4_8) {
        psn_idb = psn_id.toUtf8();
        info.psn_online_id = psn_idb.constData();
    } else {
        QByteArray account_id = QByteArray::fromBase64(psn_id.toUtf8());
        if (account_id.size() != CHIAKI_PSN_ACCOUNT_ID_SIZE) {
            emit error(tr("Invalid Account-ID"), tr("The PSN Account-ID must be exactly %1 bytes encoded as base64.").arg(CHIAKI_PSN_ACCOUNT_ID_SIZE));
            return false;
        }
        info.psn_online_id = nullptr;
        memcpy(info.psn_account_id, account_id.constData(), CHIAKI_PSN_ACCOUNT_ID_SIZE);
    }
    auto regist = new QmlRegist(info, settings->GetLogLevelMask(), this);
    connect(regist, &QmlRegist::log, this, [callback](ChiakiLogLevel level, QString msg) {
        QJSValue cb = callback;
        if (cb.isCallable())
            cb.call({QString("[%1] %2").arg(chiaki_log_level_char(level)).arg(msg), true, false});
    });
    connect(regist, &QmlRegist::failed, this, [this, callback]() {
        QJSValue cb = callback;
        if (cb.isCallable())
            cb.call({QString(), false, true});

        regist_dialog_server = {};
    });
    connect(regist, &QmlRegist::success, this, [this, host, callback](const RegisteredHost &rhost) {
        QJSValue cb = callback;
        if (cb.isCallable())
            cb.call({QString(), true, true});

        settings->AddRegisteredHost(rhost);
        // The console clicked on before may be another one (a registration from Settings by its
        // address): only the console found on the network at this very address needs no entry
        const DisplayServer chosen = regist_dialog_server;
        regist_dialog_server = {};
        const bool found_on_network = chosen.discovered && chosen.GetHostAddr() == host;
        if(!found_on_network && host != QLatin1String("255.255.255.255"))
        {
            ManualHost manual_host = !chosen.discovered && chosen.manual_host.GetHost() == host ? chosen.manual_host : ManualHost();
            if(manual_host.GetHost().isEmpty())
                manual_host.SetHost(host);
            manual_host.Register(rhost);
            settings->SetManualHost(manual_host);
        }
    });
    return true;
}

void QmlBackend::autoRegister()
{
    const auto &server = regist_dialog_server;
    resume_session = false;
    StreamSessionConnectInfo info(
            settings,
            server.discovery_host.target,
            QString(),
            QString(),
            QByteArray(),
            QByteArray(),
            0,
            server.duid,
            true,
            false,
            false,
            false);

    QString expiry_s = settings->GetPsnAuthTokenExpiry();
    QString refresh = settings->GetPsnRefreshToken();
    if(expiry_s.isEmpty() || refresh.isEmpty())
        return;
    QDateTime expiry = QDateTime::fromString(expiry_s, settings->GetTimeFormat());
    // give 1 minute buffer
    QDateTime now = QDateTime::currentDateTime().addSecs(60);
    if(now.secsTo(expiry) < 1)
    {
        PSNToken *psnToken = new PSNToken(settings, this);
        connect(psnToken, &PSNToken::PSNTokenError, this, [this](const QString &error) {
            qCWarning(chiakiGui) << "Could not refresh token. Automatic PSN Connection Unavailable!" << error;
        });
        connect(psnToken, &PSNToken::UnauthorizedError, this, &QmlBackend::psnCredsExpired);
        connect(psnToken, &PSNToken::PSNTokenSuccess, this, []() {
            qCWarning(chiakiGui) << "PSN Remote Connection Tokens Refreshed.";
        });
        connect(psnToken, &PSNToken::PSNTokenSuccess, this, [this, info]() {
            createSession(info);
        });
        connect(psnToken, &PSNToken::Finished, psnToken, &QObject::deleteLater);
        QString refresh_token = settings->GetPsnRefreshToken();
        psnToken->RefreshPsnToken(std::move(refresh_token));
    }
    else
        createSession(info);
}

void QmlBackend::finishAutoRegister(const ChiakiRegisteredHost &host)
{
    QString nickname(host.server_nickname);
    if(!regist_dialog_server.discovery_host.ps5 && regist_dialog_server.discovery_host.host_name != nickname)
    {
        emit error(tr("PS4 Console Not Main"), tr("Can't proceed...%1 is not your main PS4 console in PSN").arg(regist_dialog_server.discovery_host.host_name));
        return;
    }
    settings->AddRegisteredHost(host);
    setConnectState(PsnConnectState::RegistrationFinished);
    updatePsnHosts();
}

#ifdef CHIAKI_HAVE_WEBENGINE
void QmlBackend::clearCookies(QQuickWebEngineProfile *profile)
{
    auto cookieStore = profile->cookieStore();
    cookieStore->deleteAllCookies();
}

void QmlBackend::setWebEngineHints(QQuickWebEngineProfile *profile)
{
    QDate starting_release_date(2025, 02, 18);
    QDate today = QDate::currentDate();
    auto daysSinceStart = starting_release_date.daysTo(today);
    qint64 versionsSinceStart = daysSinceStart / 28;
    qint64 release = 133 + versionsSinceStart;
    QString chrome_version = QString::number(release);
    auto userAgent = profile->httpUserAgent();
    userAgent = userAgent.replace(QRegularExpression(" \\bQtWebEngine[^ ]*\\b"), "");
    userAgent = userAgent.replace(QRegularExpression("\\bChrome[^ ]*\\b"), QString("Chrome/%1.0.0.0").arg(chrome_version));
#ifdef Q_OS_WINDOWS
    userAgent = userAgent.replace("Windows NT 6.2", "Windows NT 10.0");
    userAgent += QString(" Edg/%1.0.0.0").arg(chrome_version);
#endif
    profile->setHttpUserAgent(userAgent);
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    auto hints = profile->clientHints();
    hints->setFullVersion(QString("%1.0.0.0").arg(chrome_version));
    QMap<QString, QVariant> fullVersionList;
    fullVersionList.insert("Not A(Brand", "99.0.0.0");
#ifdef Q_OS_WINDOWS
    fullVersionList.insert("Microsoft Edge", QString("%1.0.0.0").arg(chrome_version));
#else
    fullVersionList.insert("Google Chrome", QString("%1.0.0.0").arg(chrome_version));
#endif
    fullVersionList.insert("Chromium", QString("%1.0.0.0").arg(chrome_version));
    hints->setFullVersionList(fullVersionList);
#endif
    request_interceptor = new SecUaRequestInterceptor(chrome_version);
    profile->setUrlRequestInterceptor(request_interceptor);
}
#endif

void QmlBackend::connectToHost(int index, QString nickname)
{
    // Play: this computer's own session. Remembered, so that connecting at start does the same.
    settings->SetCoPlayHostEnabled(false);
    startConsole(index, nickname);
}

void QmlBackend::hostConsole(int index, QString nickname)
{
    settings->SetCoPlayHostEnabled(true);
    // A public room has a key of its own, renewed when the room closes (finishSession)
    if (settings->GetCoPlayPublic()) {
        coplay::Secret secret;
        if (!coplay::SecretFromString(settings->GetCoPlayPublicSecret().toStdString(), secret))
            settings->SetCoPlayPublicSecret(QString::fromStdString(coplay::SecretToString(coplay::GenerateSecret())));
    }
    startConsole(index, nickname);
}

QVariantList QmlBackend::pcTargets() const
{
    QVariantList list;
    // Linux: what the system shared last time comes first, shared again without asking
    const PcSource::Target last = PcSource::Target::FromMap(settings->GetCoPlayPcTarget());
    for (const PcSource::Target &t : PcSource::Targets()) {
        if (t.kind == QLatin1String("portal") && last.kind == t.kind && !last.token.isEmpty())
            list.append(last.ToMap());
        list.append(t.ToMap());
    }
    return list;
}

QVariantList QmlBackend::cardDevices() const
{
    QVariantList list;
    for (const PcSource::Target &t : PcSource::Devices())
        list.append(t.ToMap());
    return list;
}

QString QmlBackend::pcUnsupported() const
{
    return PcSource::Unsupported();
}

QString QmlBackend::pcPadsProblem() const
{
    return VirtualPads::Problem();
}

QString QmlBackend::pcDriverUrl() const
{
    return VirtualPads::DriverUrl();
}

QString QmlBackend::pcFixCommand() const
{
    return VirtualPads::FixCommand();
}

void QmlBackend::pcCopyFixCommand() const
{
    QGuiApplication::clipboard()->setText(VirtualPads::FixCommand());
}

QString QmlBackend::pcPermission() const
{
    return PcSource::Permission();
}

void QmlBackend::pcAskPermission() const
{
    PcSource::AskPermission();
}

QString QmlBackend::pcNote() const
{
    return PcSource::Note();
}

QString QmlBackend::pcKeyMouseProblem() const
{
    return PcInput::Problem();
}

void QmlBackend::pcAskKeyMouse() const
{
    PcInput::AskPermission();
}

QVariantMap QmlBackend::pcPicture(const QVariantMap &target, int height) const
{
    int width = 1920, h = 1080;
    PcSource::PictureSize(PcSource::Target::FromMap(target), height, &width, &h);
    const int kbps = settings->GetCoPlayPcBitrate();
    QVariantMap m;
    m["width"] = width;
    m["height"] = h;
    m["mbps"] = qRound((kbps > 0 ? kbps : PcSource::BitrateFor(width, h, 60)) / 1000.0);
    return m;
}

QVariantMap QmlBackend::pcLastTarget() const
{
    return settings->GetCoPlayPcTarget();
}

QString QmlBackend::pcGameIn(const QVariantMap &target) const
{
    return PcSource::DetectGame(PcSource::Target::FromMap(target)).title;
}

void QmlBackend::hostThisComputer(const QVariantMap &target_map)
{
    if (session) {
        qCWarning(chiakiGui) << "Another session is already active";
        return;
    }
    const QString unsupported = PcSource::Unsupported();
    if (!unsupported.isEmpty()) {
        emit error(tr("This computer"), unsupported);
        return;
    }
    const QString permission = PcSource::Permission();
    if (!permission.isEmpty()) {
        emit error(tr("This computer"), permission);
        return;
    }
    PcSource::Target target = PcSource::Find(PcSource::Target::FromMap(target_map));
    if (!target.Valid()) {
        emit error(tr("This computer"), tr("That window is not open any more. Choose again."));
        return;
    }
    if (target.kind == QLatin1String("portal")) {
        // Linux: the system asks which window or screen (or shares the one it shared last time)
        if (pc_portal_asking)
            return;
        auto portal = std::make_shared<PcPortal>();
        pc_portal_asking = portal;
        portal->Open(target.token, [this, portal, target](const PcPortal::Stream &stream, const QString &why) {
            pc_portal_asking.reset();
            if (stream.fd < 0) {
                // Cancelled in the system's dialog needs no words
                if (!why.isEmpty())
                    emit error(tr("This computer"), why);
                return;
            }
            if (session) {
#ifndef _WIN32
                ::close(stream.fd);
#endif
                return;
            }
            PcSource::Target chosen = target;
            chosen.handle = 1;
            chosen.token = stream.token;
            chosen.width = stream.width;
            chosen.height = stream.height;
            chosen.title = stream.window ? tr("The window you chose") : tr("The screen you chose");
            startThisComputer(chosen, portal, stream.fd, stream.node);
        });
        return;
    }
    startThisComputer(target, nullptr, -1, 0);
}

void QmlBackend::startThisComputer(const PcSource::Target &target, std::shared_ptr<PcPortal> portal, int portal_fd, quint32 portal_node)
{
    settings->SetCoPlayHostEnabled(true);
    settings->SetCoPlayPcTarget(target.ToMap());
    if (settings->GetCoPlayPublic()) {
        coplay::Secret secret;
        if (!coplay::SecretFromString(settings->GetCoPlayPublicSecret().toStdString(), secret))
            settings->SetCoPlayPublicSecret(QString::fromStdString(coplay::SecretToString(coplay::GenerateSecret())));
    }
    window->setWindowAdjustable(false);
    resume_session = false;
    // The game plays in its own window on this computer: the app's window never covers it,
    // it only shows what the guests get
    StreamSessionConnectInfo info(
            settings,
            CHIAKI_TARGET_PS5_1,
            QStringLiteral("127.0.0.1"),
            QString(),
            QByteArray(),
            QByteArray(),
            QString(),
            QString(),
            false,
            false,
            false,
            false);
    info.pc_source = true;
    info.pc_target = target.ToMap();
    info.pc_portal = std::move(portal);
    info.pc_portal_fd = portal_fd;
    info.pc_portal_node = portal_node;
    // The window's own shape, ultrawide too, as high as chosen on the Host page
    int width = 1920, height = 1080;
    PcSource::PictureSize(target, settings->GetCoPlayPcHeight(), &width, &height);
    info.video_profile.width = static_cast<unsigned int>(width);
    info.video_profile.height = static_cast<unsigned int>(height);
    if (info.video_profile.max_fps < 30)
        info.video_profile.max_fps = 60;
    const int kbps = settings->GetCoPlayPcBitrate();
    info.pc_video_kbps = kbps > 0 ? kbps : PcSource::BitrateFor(width, height, static_cast<int>(info.video_profile.max_fps));
    info.video_profile.codec = CHIAKI_CODEC_H264;
    info.enable_dualsense = false;
    info.coplay_room_game = PcSource::DetectGame(target).title;
    info.coplay_room_title_id = QString();
    createSession(info);
}

void QmlBackend::startConsole(int index, QString nickname)
{
    window->setWindowAdjustable(false);
    auto server = displayServerAt(index);
    if (!server.valid)
        return;

    if (!server.registered) {
        regist_dialog_server = server;
        emit registDialogRequested(server.GetHostAddr(), server.IsPS5(), server.duid);
        return;
    }

    if (server.discovered && server.discovery_host.state == CHIAKI_DISCOVERY_HOST_STATE_STANDBY)
    {
        const QString wake_host = server.GetHostAddr();
        const QByteArray wake_regist_key = server.registered_host.GetRPRegistKey();
        const bool wake_ps5 = server.IsPS5();
        if(!sendWakeup(wake_host, wake_regist_key, wake_ps5))
        {
            qCWarning(chiakiGui) << "Couldn't wakeup server";
            window->setWindowAdjustable(true);
            return;
        }
        if(nickname.isEmpty())
        {
            qCWarning(chiakiGui) << "No nickname given for registered connection, not connecting...";
            return;
        }
        waking_sleeping_nicknames.append(nickname);
        QTimer::singleShot(WAKEUP_PSN_IGNORE_SECONDS * 1000, [this, nickname]{
            waking_sleeping_nicknames.removeOne(nickname);
            emit hostsChanged();
        });
        wakeup_nickname = nickname;
    }

    // Controller only: there is no picture to fill the screen with, and whoever plays on the TV
    // needs this computer's screen for other things (OBS, the chat), so the window stays as it is
    const bool picture_off = (settings->GetAudioVideoDisabled() & CHIAKI_VIDEO_DISABLED) != 0;
    bool fullscreen = false, zoom = false, stretch = false;
    if (!picture_off) {
        switch (settings->GetWindowType()) {
        case WindowType::SelectedResolution:
            break;
        case WindowType::CustomResolution:
            break;
        case WindowType::AdjustableResolution:
            break;
        case WindowType::Fullscreen:
            fullscreen = true;
            break;
        case WindowType::Zoom:
            zoom = true;
            break;
        case WindowType::Stretch:
            stretch = true;
            break;
        default:
            break;
        }
        emit windowTypeUpdated(settings->GetWindowType());
    }

    resume_session = false;
    if(server.duid.isEmpty())
    {
        QString host = server.GetHostAddr();
        StreamSessionConnectInfo info(
                settings,
                server.registered_host.GetTarget(),
                std::move(host),
                std::move(nickname),
                server.registered_host.GetRPRegistKey(),
                server.registered_host.GetRPKey(),
                server.registered_host.GetConsolePin(),
                server.duid,
                false,
                fullscreen,
                zoom,
                stretch);
        info.coplay_room_game = server.discovered ? server.discovery_host.running_app_name : QString();
        info.coplay_room_title_id = server.discovered ? server.discovery_host.running_app_titleid : QString();
        createSession(info);
    }
    else
    {
        StreamSessionConnectInfo info(
                settings,
                server.psn_host.GetTarget(),
                QString(),
                QString(),
                QByteArray(),
                QByteArray(),
                server.registered_host.GetConsolePin(),
                server.duid,
                false,
                fullscreen,
                zoom,
                stretch);
        info.coplay_room_game = server.discovered ? server.discovery_host.running_app_name : QString();
        info.coplay_room_title_id = server.discovered ? server.discovery_host.running_app_titleid : QString();

        QString expiry_s = settings->GetPsnAuthTokenExpiry();
        QString refresh = settings->GetPsnRefreshToken();
        if(expiry_s.isEmpty() || refresh.isEmpty())
            return;
        QDateTime expiry = QDateTime::fromString(expiry_s, settings->GetTimeFormat());
        // give 1 minute buffer
        QDateTime now = QDateTime::currentDateTime().addSecs(60);
        if(now.secsTo(expiry) < 1)
        {
            PSNToken *psnToken = new PSNToken(settings, this);
            connect(psnToken, &PSNToken::PSNTokenError, this, [this](const QString &error) {
                qCWarning(chiakiGui) << "Could not refresh token. Automatic PSN Connection Unavailable!" << error;
            });
            connect(psnToken, &PSNToken::UnauthorizedError, this, &QmlBackend::psnCredsExpired);
            connect(psnToken, &PSNToken::PSNTokenSuccess, this, []() {
                qCWarning(chiakiGui) << "PSN Remote Connection Tokens Refreshed.";
            });
            connect(psnToken, &PSNToken::PSNTokenSuccess, this, [this, info]() {
                createSession(info);
            });
                connect(psnToken, &PSNToken::Finished, psnToken, &QObject::deleteLater);
            QString refresh_token = settings->GetPsnRefreshToken();
            psnToken->RefreshPsnToken(std::move(refresh_token));
        }
        else
            createSession(info);
    }
}

void QmlBackend::stopSession(bool sleep)
{
    if (!session)
        return;

    if (!session_info.nickname.isEmpty())
    {
        waking_sleeping_nicknames.append(session_info.nickname);
        QTimer::singleShot(WAKEUP_PSN_IGNORE_SECONDS * 1000, [this]{
            waking_sleeping_nicknames.removeOne(session_info.nickname);
            emit hostsChanged();
        });
    }

    if (console_reconnecting) {
        // There is no console to tell; this ends what is left of the session
        cancelReconnect();
        return;
    }

    // Sharing: where the host wants it, the room stays while the console sleeps
    if (sleep && !session->IsAsleepRoom() && session->IsCoPlayHosting() && keepRoomAsleepPossible()) {
        asleep_room_pending = true;
        session->SetKeepGuests(true);
    }

    if (sleep)
        session->GoToBed();

    session->Stop();
}

void QmlBackend::sessionGoHome()
{
    if (!session)
        return;

    session->GoHome();
}

void QmlBackend::enterPin(const QString &pin)
{
    qCInfo(chiakiGui) << "Set login pin";
    if (session)
        session->SetLoginPIN(pin);
}

QUrl QmlBackend::psnLoginUrl() const
{
    size_t duid_size = CHIAKI_DUID_STR_SIZE;
    char duid[duid_size];
    chiaki_holepunch_generate_client_device_uid(duid, &duid_size);
    return QUrl(PSNAuth::LOGIN_URL + "duid=" + QString(duid) + "&");
}

bool QmlBackend::handlePsnLoginRedirect(const QUrl &url)
{
    if (!url.toString().startsWith(QString::fromStdString(PSNAuth::REDIRECT_PAGE)))
    {
        emit psnLoginAccountIdError(QString("Redirect URL invalid does not start with:\n") + QString::fromStdString(PSNAuth::REDIRECT_PAGE));
        return false;
    }

    const QString code = QUrlQuery(url).queryItemValue("code");
    if (code.isEmpty()) {
        qCWarning(chiakiGui) << "Invalid code from redirect url";
        emit psnLoginAccountIdError("Redirect URL invalid");
        return false;
    }
    PSNAccountID *psnId = new PSNAccountID(settings, this);
    connect(psnId, &PSNAccountID::AccountIDResponse, this, [this, psnId](const QString &accountId) {
        emit psnLoginAccountIdDone(accountId);
    });
    connect(psnId, &PSNAccountID::AccountIDResponse, this, &QmlBackend::updatePsnHosts);
    connect(psnId, &PSNAccountID::AccountIDError, [this](const QString &error) {
        qCWarning(chiakiGui) << "Could not retrieve psn token or account Id!" << error;
        emit psnLoginAccountIdError(error);
    });
    connect(psnId, &PSNAccountID::Finished, psnId, &QObject::deleteLater);
    psnId->GetPsnAccountId(code);
    emit psnTokenChanged();
    return true;
}

void QmlBackend::stopAutoConnect()
{
    auto_connect_mac = {};
    if(!wakeup_nickname.isEmpty())
    {
        wakeup_start_timer->stop();
        wakeup_nickname.clear();
        if(wakeup_start)
        {
            wakeup_start = false;
            teardownSession();
        }
    }
    emit autoConnectChanged();
}

void QmlBackend::cancelAutoConnect()
{
    stopAutoConnect();
    psn_auto_connect_timer->stop();
}

QmlBackend::DisplayServer QmlBackend::displayServerAt(int index) const
{
    if (index < 0)
        return {};
    auto discovered = discovery_manager.GetHosts();
    auto manual = settings->GetManualHosts();
    if (index < discovered.size()) {
        DisplayServer server;
        server.valid = true;
        server.discovered = true;
        server.discovery_host = discovered.at(index);
        auto host_mac = server.discovery_host.GetHostMAC();
        server.registered = settings->GetRegisteredHostRegistered(host_mac);
        QString duid = "";
        if(!server.registered)
        {
            if(psn_nickname_hosts.contains(server.discovery_host.host_name))
                duid = psn_nickname_hosts.value(server.discovery_host.host_name).GetDuid();
            else if(!server.discovery_host.ps5)
                duid =  psn_nickname_hosts.value(QString("Main PS4 Console")).GetDuid();
        }
        server.duid = std::move(duid);
        if (server.registered)
            server.registered_host = settings->GetRegisteredHost(host_mac);
        for (int i = 0; i < manual.size(); i++)
        {
            const auto &manual_host = manual.at(i);
            if(manual_host.GetRegistered() && manual_host.GetMAC() == host_mac && manual_host.GetHost() == server.discovery_host.host_addr)
            {
                server.manual_host = std::move(manual_host);
                break;
            }
        }
        return server;
    }
    index -= discovered.size();
    if (index < manual.size()) {
        DisplayServer server;
        server.valid = true;
        server.discovered = false;
        server.manual_host = manual.at(index);
        server.registered = false;
        server.duid = QString();
        if (server.manual_host.GetRegistered() && settings->GetRegisteredHostRegistered(server.manual_host.GetMAC())) {
            server.registered = true;
            server.registered_host = settings->GetRegisteredHost(server.manual_host.GetMAC());
        }
        return server;
    }
    index -= manual.size();
    if (index < psn_hosts.count())
    {
        DisplayServer server;

        QMapIterator<QString, PsnHost> i(psn_hosts);
        size_t j = 0;
        while (i.hasNext())
        {
            i.next();
            PsnHost psn_host = i.value();
            bool hidden = false;
            for (const auto &host : discovery_manager.GetHosts())
            {
                if(host.host_name == psn_host.GetName())
                    hidden = true;
            }
            if(hidden)
                continue;
            if(j == index)
            {
                server.valid = true;
                server.discovered = false;
                server.psn_host = std::move(psn_host);
                server.duid = i.key();
                server.registered = true;
                server.registered_host = settings->GetNicknameRegisteredHost(server.psn_host.GetName());
                return server;
            }
            j++;
        }
        return {};
    }
    return {};
}

bool QmlBackend::sendWakeup(const DisplayServer &server)
{
    if (!server.registered)
        return false;
    return sendWakeup(server.GetHostAddr(), server.registered_host.GetRPRegistKey(), server.IsPS5());
}

bool QmlBackend::sendWakeup(const QString &host, const QByteArray &regist_key, bool ps5)
{
    try {
        discovery_manager.SendWakeup(host, regist_key, ps5);
        return true;
    } catch (const Exception &e) {
        // Blocked by macOS or a firewall: the text says what to do, nothing to put in front of it
        if (discovery_manager.GetLocalNetworkBlocked())
            emit error(tr("Wakeup failed"), QString::fromUtf8(e.what()));
        else
            emit error(tr("Wakeup failed"), tr("Failed to send Wakeup packet:\n%1").arg(e.what()));
        return false;
    }
}

void QmlBackend::setAllowJoystickBackgroundEvents()
{
    ControllerManager::GetInstance()->SetAllowJoystickBackgroundEvents(settings->GetAllowJoystickBackgroundEvents());
}

void QmlBackend::setIsAppActive()
{
    ControllerManager::GetInstance()->SetIsAppActive(window->isActive());
}

uint32_t QmlBackend::getStreamShortcut() const
{
    if(!settings->GetStreamMenuEnabled())
        return 0;
	uint32_t stream_menu_shortcut1 = settings->GetStreamMenuShortcut1();
	if(stream_menu_shortcut1 > 0)
		stream_menu_shortcut1 = 1 << (stream_menu_shortcut1 - 1);
	uint32_t stream_menu_shortcut2 = settings->GetStreamMenuShortcut2();
	if(stream_menu_shortcut2 > 0)
		stream_menu_shortcut2 = 1 << (stream_menu_shortcut2 - 1);
	uint32_t stream_menu_shortcut3 = settings->GetStreamMenuShortcut3();
	if(stream_menu_shortcut3 > 0)
		stream_menu_shortcut3 = 1 << (stream_menu_shortcut3 - 1);
	uint32_t stream_menu_shortcut4 = settings->GetStreamMenuShortcut4();
	if(stream_menu_shortcut4 > 0)
		stream_menu_shortcut4 = 1 << (stream_menu_shortcut4 - 1);
    uint32_t shortcut = stream_menu_shortcut1 | stream_menu_shortcut2 | stream_menu_shortcut3 | stream_menu_shortcut4;
    return shortcut;
}

void QmlBackend::updateStreamShortcut()
{
    uint32_t shortcut = getStreamShortcut();
    for (const auto &controller : std::as_const(controllers)) {
       controller->setEscapeShortcut(shortcut);
    }
}

void QmlBackend::updateControllers()
{
    bool changed = false;
    QMap<QString,QString> controller_mappings = settings->GetControllerMappings();
    for (auto it = controllers.begin(); it != controllers.end();) {
        if (ControllerManager::GetInstance()->GetAvailableControllers().contains(it.key())) {
            it++;
            continue;
        }
        if(it.key() == controller_mapping_id)
        {
            controller_mapping_controller = nullptr;
            controllerMappingQuit();
        }
        QString vidpid = it.value()->GetVIDPID();
        QString guid = it.value()->GetGUID();
        it.value()->deleteLater();
        it = controllers.erase(it);
        changed = true;
    }
    uint32_t stream_shortcut = getStreamShortcut();
    for (auto id : ControllerManager::GetInstance()->GetAvailableControllers()) {
        if (controllers.contains(id))
            continue;
        auto controller = ControllerManager::GetInstance()->OpenController(id);
        if (!controller)
            continue;
        controllers[id] = new QmlController(controller, stream_shortcut ,window, this);
        QString vidpid = controller->GetVIDPIDString();
        QString guid = controller->GetGUIDString();
        QStringList existing_vidpid;
        if(controller_guids_to_update.contains(vidpid))
            existing_vidpid.append(controller_guids_to_update.value(vidpid));
        existing_vidpid.append(guid);
        controller_guids_to_update.insert(vidpid, existing_vidpid);
        // replace old guid with new vid/pid
        if(controller_mappings.contains(guid))
        {
            QString old_mapping = controller_mappings.value(guid);
            qsizetype guid_string_length = old_mapping.indexOf(",") + 1;
            old_mapping.remove(0, guid_string_length);
            settings->RemoveControllerMapping(guid);
            settings->SetControllerMapping(vidpid, old_mapping);
            qCInfo(chiakiGui) << "Migrated controller mapping from platform-specific GUID: " << guid << " to platform-agnostic VID/PID: " << vidpid;
        }
        controller_guids_to_update.insert(vidpid, existing_vidpid);
        connect(controller, &Controller::UpdatingControllerMapping, this, &QmlBackend::controllerMappingUpdate);
        connect(controller, &Controller::NewButtonMapping, this, &QmlBackend::controllerMappingChangeButton);
        changed = true;
    }
    if (changed)
        emit controllersChanged();
}

void QmlBackend::setControllerMappingDefaultMapping(bool is_default_mapping)
{
    if(controller_mapping_default_mapping != is_default_mapping)
    {
        controller_mapping_default_mapping = is_default_mapping;
        emit controllerMappingDefaultMappingChanged();
    }
}

void QmlBackend::setControllerMappingAltered(bool altered)
{
    if(controller_mapping_altered != altered)
    {
        controller_mapping_altered = altered;
        emit controllerMappingAlteredChanged();
    }
}

void QmlBackend::setControllerMappingInProgress(bool is_in_progress)
{
    if(controller_mapping_in_progress != is_in_progress)
    {
        controller_mapping_in_progress = is_in_progress;
        emit controllerMappingInProgressChanged();
    }
}

void QmlBackend::setEnableAnalogStickMapping(bool enabled)
{
    if(enable_analog_stick_mapping != enabled)
    {
        if(controller_mapping_in_progress && controller_mapping_controller)
            controller_mapping_controller->EnableAnalogStickMapping(enabled);
        enable_analog_stick_mapping = enabled;
        emit enableAnalogStickMappingChanged();
    }
}

QVariantList QmlBackend::currentControllerMapping() const
{
    QVariantList out;
    if(!controller_mapping_in_progress)
        return out;
	QMap<int, QStringList> result =
	{
		{CHIAKI_CONTROLLER_BUTTON_CROSS     , controller_mapping_controller_mappings.contains("a") ? controller_mapping_controller_mappings.value("a") : QStringList()},
		{CHIAKI_CONTROLLER_BUTTON_MOON      , controller_mapping_controller_mappings.contains("b") ? controller_mapping_controller_mappings.value("b") : QStringList()},
		{CHIAKI_CONTROLLER_BUTTON_BOX       , controller_mapping_controller_mappings.contains("x") ? controller_mapping_controller_mappings.value("x") : QStringList()},
		{CHIAKI_CONTROLLER_BUTTON_PYRAMID   , controller_mapping_controller_mappings.contains("y") ? controller_mapping_controller_mappings.value("y") : QStringList()},
		{CHIAKI_CONTROLLER_BUTTON_DPAD_LEFT , controller_mapping_controller_mappings.contains("dpleft") ? controller_mapping_controller_mappings.value("dpleft") : QStringList()},
		{CHIAKI_CONTROLLER_BUTTON_DPAD_RIGHT, controller_mapping_controller_mappings.contains("dpright") ? controller_mapping_controller_mappings.value("dpright") : QStringList()},
		{CHIAKI_CONTROLLER_BUTTON_DPAD_UP   , controller_mapping_controller_mappings.contains("dpup") ? controller_mapping_controller_mappings.value("dpup") : QStringList()},
		{CHIAKI_CONTROLLER_BUTTON_DPAD_DOWN , controller_mapping_controller_mappings.contains("dpdown") ? controller_mapping_controller_mappings.value("dpdown") : QStringList()},
		{CHIAKI_CONTROLLER_BUTTON_L1        , controller_mapping_controller_mappings.contains("leftshoulder") ? controller_mapping_controller_mappings.value("leftshoulder") : QStringList()},
		{CHIAKI_CONTROLLER_BUTTON_R1        , controller_mapping_controller_mappings.contains("rightshoulder") ? controller_mapping_controller_mappings.value("rightshoulder") : QStringList()},
		{CHIAKI_CONTROLLER_BUTTON_L3        , controller_mapping_controller_mappings.contains("leftstick") ? controller_mapping_controller_mappings.value("leftstick") : QStringList()},
		{CHIAKI_CONTROLLER_BUTTON_R3        , controller_mapping_controller_mappings.contains("rightstick") ? controller_mapping_controller_mappings.value("rightstick") : QStringList()},
		{CHIAKI_CONTROLLER_BUTTON_OPTIONS   , controller_mapping_controller_mappings.contains("start") ? controller_mapping_controller_mappings.value("start") : QStringList()},
		{CHIAKI_CONTROLLER_BUTTON_SHARE     , controller_mapping_controller_mappings.contains("back") ? controller_mapping_controller_mappings.value("back") : QStringList()},
		{CHIAKI_CONTROLLER_BUTTON_TOUCHPAD  , controller_mapping_controller_mappings.contains("touchpad") ? controller_mapping_controller_mappings.value("touchpad") : QStringList()},
		{CHIAKI_CONTROLLER_BUTTON_PS        , controller_mapping_controller_mappings.contains("guide") ? controller_mapping_controller_mappings.value("guide") : QStringList()},
		{CHIAKI_CONTROLLER_ANALOG_BUTTON_L2 , controller_mapping_controller_mappings.contains("lefttrigger") ? controller_mapping_controller_mappings.value("lefttrigger") : QStringList()},
		{CHIAKI_CONTROLLER_ANALOG_BUTTON_R2 , controller_mapping_controller_mappings.contains("righttrigger") ? controller_mapping_controller_mappings.value("righttrigger") : QStringList()},
		{static_cast<int>(ControllerButtonExt::ANALOG_STICK_LEFT_X)   , controller_mapping_controller_mappings.contains("leftx") ? controller_mapping_controller_mappings.value("leftx") : QStringList()},
		{static_cast<int>(ControllerButtonExt::ANALOG_STICK_LEFT_Y)   , controller_mapping_controller_mappings.contains("lefty") ? controller_mapping_controller_mappings.value("lefty") : QStringList()},
		{static_cast<int>(ControllerButtonExt::ANALOG_STICK_RIGHT_X)  , controller_mapping_controller_mappings.contains("rightx") ? controller_mapping_controller_mappings.value("rightx") : QStringList()},
		{static_cast<int>(ControllerButtonExt::ANALOG_STICK_RIGHT_Y)  , controller_mapping_controller_mappings.contains("righty") ? controller_mapping_controller_mappings.value("righty") : QStringList()},
        {static_cast<int>(ControllerButtonExt::MISC1)   ,      controller_mapping_controller_mappings.contains("misc1") ? controller_mapping_controller_mappings.value("misc1") : QStringList()},
	};

    for (auto it = result.cbegin(); it != result.cend(); ++it) {
        QVariantMap m;
        m["buttonName"] = Settings::GetChiakiControllerButtonName(it.key());
        m["buttonValue"] = it.key();
        m["physicalButton"] = it.value();
        out.append(m);
    }
    return out;
}

void QmlBackend::updateControllerMappings()
{
    if(SDL_WasInit(SDL_INIT_GAMECONTROLLER)==0)
        return;
    QMap<QString,QString> controller_mappings = settings->GetControllerMappings();
    QStringList mapping_vidpids = controller_mappings.keys();
    for(int i=0; i<mapping_vidpids.length(); i++)
    {
        QString vidpid = mapping_vidpids.at(i);
        if(!controller_guids_to_update.contains(vidpid))
            continue;
        QStringList guids_to_update = controller_guids_to_update.value(vidpid);
        for(int j=0; j<guids_to_update.length(); j++)
        {
            QString guid = guids_to_update.at(j);
            if(!controller_mapping_original_controller_mappings.contains(guid))
            {
                const SDL_JoystickGUID real_guid = SDL_JoystickGetGUIDFromString(guid.toUtf8().constData());
                const char *mapping = SDL_GameControllerMappingForGUID(real_guid);
                QString original_controller_mapping(mapping);
                SDL_free((char *)mapping);
                if(original_controller_mapping.isEmpty())
                {
                    qCWarning(chiakiGui) << "Error retrieving controller mapping of GUID " << guid << "with error: " << SDL_GetError();
                    return;
                }
                controller_mapping_original_controller_mappings.insert(guid, original_controller_mapping);
            }
            QString controller_mapping_to_add = controller_mappings.value(vidpid);
            controller_mapping_to_add.prepend(QString("%1,").arg(guid));
            int result = SDL_GameControllerAddMapping(controller_mapping_to_add.toUtf8().constData());
            switch(result)
            {
                case -1:
                    qCWarning(chiakiGui) << "Error setting controller mapping for guid: " << guid << " with error: " << SDL_GetError();
                    break;
                case 0:
                    qCInfo(chiakiGui) << "Updated controller mapping for guid: " << guid;
                    break;
                case 1:
                    qCInfo(chiakiGui) << "Added controller mapping for guid: " << guid;
                    break;
                default:
                    qCInfo(chiakiGui) << "Unidentified problem mapping for guid: " << guid;
                    break;
            }
        }
        controller_guids_to_update.remove(vidpid);
    }
}

void QmlBackend::creatingControllerMapping(bool creating_controller_mapping)
{
    ControllerManager::GetInstance()->creatingControllerMapping(creating_controller_mapping);
}

void QmlBackend::beginControllerMapping(bool reset_mapping)
{
    controller_mapping_reset_requested = reset_mapping;
    creatingControllerMapping(true);
}

void QmlBackend::controllerMappingChangeButton(QString button)
{
    if(!controller_mapping_in_progress || !controller_mapping_controller)
        return;
    emit controllerMappingButtonSelected(std::move(button));
}

void QmlBackend::updateButton(int chiaki_button, QString physical_button, int new_index)
{
	QMap<int, QString> button_map =
	{
		{CHIAKI_CONTROLLER_BUTTON_CROSS     , "a"},
		{CHIAKI_CONTROLLER_BUTTON_MOON      , "b"},
		{CHIAKI_CONTROLLER_BUTTON_BOX       , "x"},
		{CHIAKI_CONTROLLER_BUTTON_PYRAMID   , "y"},
		{CHIAKI_CONTROLLER_BUTTON_DPAD_LEFT , "dpleft"},
		{CHIAKI_CONTROLLER_BUTTON_DPAD_RIGHT, "dpright"},
		{CHIAKI_CONTROLLER_BUTTON_DPAD_UP   , "dpup"},
		{CHIAKI_CONTROLLER_BUTTON_DPAD_DOWN , "dpdown"},
		{CHIAKI_CONTROLLER_BUTTON_L1        , "leftshoulder"},
		{CHIAKI_CONTROLLER_BUTTON_R1        , "rightshoulder"},
		{CHIAKI_CONTROLLER_BUTTON_L3        , "leftstick"},
		{CHIAKI_CONTROLLER_BUTTON_R3        , "rightstick"},
		{CHIAKI_CONTROLLER_BUTTON_OPTIONS   , "start"},
		{CHIAKI_CONTROLLER_BUTTON_SHARE     , "back"},
		{CHIAKI_CONTROLLER_BUTTON_TOUCHPAD  , "touchpad"},
		{CHIAKI_CONTROLLER_BUTTON_PS        , "guide"},
		{CHIAKI_CONTROLLER_ANALOG_BUTTON_L2 , "lefttrigger"},
		{CHIAKI_CONTROLLER_ANALOG_BUTTON_R2 , "righttrigger"},
		{static_cast<int>(ControllerButtonExt::ANALOG_STICK_LEFT_X)   , "leftx"},
		{static_cast<int>(ControllerButtonExt::ANALOG_STICK_LEFT_Y)   , "lefty"},
		{static_cast<int>(ControllerButtonExt::ANALOG_STICK_RIGHT_X)  , "rightx"},
		{static_cast<int>(ControllerButtonExt::ANALOG_STICK_RIGHT_Y)  , "righty"},
        {static_cast<int>(ControllerButtonExt::MISC1)   , "misc1"},
	};
    QString button = button_map.value(chiaki_button);
    QString old_mapping = controller_mapping_physical_button_mappings.contains(physical_button) ? controller_mapping_physical_button_mappings.value(physical_button) : QString();
    if(button == old_mapping)
        return;
    if(!old_mapping.isEmpty())
    {
        QStringList old_mapping_buttons = controller_mapping_controller_mappings.value(old_mapping);
        if(old_mapping_buttons.length() > 1)
        {
            old_mapping_buttons.removeOne(physical_button);
            controller_mapping_controller_mappings.insert(old_mapping, old_mapping_buttons);
        }
        else
            controller_mapping_controller_mappings.remove(old_mapping);
    }
    QStringList new_mapping_buttons = controller_mapping_controller_mappings.value(button);
    if(new_mapping_buttons.length() > new_index)
    {
        controller_mapping_physical_button_mappings.insert(new_mapping_buttons.at(new_index), QString());
        new_mapping_buttons.remove(new_index);
    }
    if(new_index == 0)
        new_mapping_buttons.prepend(physical_button);
    else
        new_mapping_buttons.append(physical_button);
    controller_mapping_controller_mappings.insert(button, new_mapping_buttons);
    controller_mapping_physical_button_mappings.insert(physical_button, button);
    if(controller_mapping_controller_mappings == controller_mapping_applied_controller_mappings)
        setControllerMappingAltered(false);
    else
        setControllerMappingAltered(true);
    emit currentControllerMappingChanged();
}

void QmlBackend::controllerMappingUpdate(Controller *controller)
{
    if(SDL_WasInit(SDL_INIT_GAMECONTROLLER)==0)
        return;
    if(controller_mapping_in_progress)
        return;
    controller_mapping_controller = controller;
    if(controller->IsSteamVirtual() && !controller_mapping_reset_requested)
    {
        controllerMappingQuit();
        emit controllerMappingSteamControllerSelected();
        return;
    }
    controller_mapping_id = controller->GetDeviceID();
    const char *mapping = SDL_GameControllerMapping(controller->GetController());
    QString original_controller_mapping(mapping);
    qCInfo(chiakiGui) << "Original controller mapping: " << original_controller_mapping;
    SDL_free((char *)mapping);
    if(original_controller_mapping.isEmpty())
    {
        qCWarning(chiakiGui) << "Error retrieving controller mapping " << SDL_GetError();
        controller_mapping_id = -1;
        controller_mapping_controller = nullptr;
        return;
    }
	QStringList mapping_results = original_controller_mapping.split(u',');
    if(mapping_results.length() < 2)
    {
        qCWarning(chiakiGui) << "Received invalid Mapping List";
        controller_mapping_id = -1;
        controller_mapping_controller = nullptr;
        return;
    }
	controller_mapping_controller_guid = mapping_results.takeFirst();
    controller_mapping_controller_vid_pid = controller_mapping_controller->GetVIDPIDString();
    if(!controller_mapping_original_controller_mappings.contains(controller_mapping_controller_guid))
    {
        setControllerMappingDefaultMapping(true);
        controller_mapping_original_controller_mappings.insert(controller_mapping_controller_guid, original_controller_mapping);
    }
    controller_mapping_controller->EnableAnalogStickMapping(enable_analog_stick_mapping);
	controller_mapping_controller_type = mapping_results.takeFirst();
    if(controller_mapping_controller_type == "*")
    {
        controller_mapping_controller_type = controller_mapping_controller->GetType();
        if(controller_mapping_controller_type.isEmpty())
            controller_mapping_controller_type = QString("Unidentified Controller");
    }
    for(int i=0; i<mapping_results.length(); i++)
    {
        QString individual_mapping = mapping_results.at(i);
        QStringList individual_mapping_list = individual_mapping.split(u':');
        if(individual_mapping_list.length() < 2)
            continue;
        QString key = individual_mapping_list.takeFirst();
        if(controller_mapping_controller_mappings.contains(key))
        {
            auto update_list = controller_mapping_controller_mappings.value(key);
            individual_mapping_list = update_list + individual_mapping_list;
        }
        controller_mapping_controller_mappings.insert(key, individual_mapping_list);
        // only add buttons to physical mapping list
        if(key != "crc" && key != "platform" && key != "type" && key != "hint" && !key.startsWith("sdk"))
        {
            for(int j = 0; j < individual_mapping_list.length(); j++)
                controller_mapping_physical_button_mappings.insert(individual_mapping_list[j], key);
        }
    }

    controller_mapping_applied_controller_mappings = controller_mapping_controller_mappings;
    emit currentControllerTypeChanged();
    emit currentControllerMappingChanged();
    setControllerMappingInProgress(true);
}

void QmlBackend::controllerMappingSelectButton()
{
    if(controller_mapping_in_progress && controller_mapping_controller)
        controller_mapping_controller->IsUpdatingMappingButton(true);
}

void QmlBackend::controllerMappingReset()
{
    if(!controller_mapping_original_controller_mappings.contains(controller_mapping_controller_guid) || SDL_WasInit(SDL_INIT_GAMECONTROLLER)==0 || !settings->GetControllerMappings().keys().contains(controller_mapping_controller_vid_pid))
    {
        controllerMappingQuit();
        return;
    }
    settings->RemoveControllerMapping(controller_mapping_controller_vid_pid);
    int result = SDL_GameControllerAddMapping(controller_mapping_original_controller_mappings.value(controller_mapping_controller_guid).toUtf8().constData());
    switch(result)
    {
        case -1:
            qCWarning(chiakiGui) << "Error setting controller mapping for guid: " << controller_mapping_controller_guid << " with error: " << SDL_GetError();
            break;
        case 0:
            qCInfo(chiakiGui) << "Updated controller mapping for guid: " << controller_mapping_controller_guid;
            break;
        case 1:
            qCInfo(chiakiGui) << "Added controller mapping for guid: " << controller_mapping_controller_guid;
            break;
        default:
            qCInfo(chiakiGui) << "Unidentified problem mapping for guid: " << controller_mapping_controller_guid;
            break;
    }
    controllerMappingQuit();
}

void QmlBackend::controllerMappingQuit()
{
    if(controller_mapping_in_progress && controller_mapping_controller)
        controller_mapping_controller->IsUpdatingMappingButton(false);
    else
        creatingControllerMapping(false);
    controller_mapping_reset_requested = false;
    setEnableAnalogStickMapping(false);
    controller_mapping_controller = nullptr;
    setControllerMappingDefaultMapping(false);
    setControllerMappingAltered(false);
    controller_mapping_id = -1;
    controller_mapping_controller_guid.clear();
    controller_mapping_controller_vid_pid.clear();
    controller_mapping_controller_type.clear();
    controller_mapping_controller_mappings.clear();
    controller_mapping_applied_controller_mappings.clear();
    controller_mapping_physical_button_mappings.clear();
    setControllerMappingInProgress(false);
    emit currentControllerTypeChanged();
    emit currentControllerMappingChanged();
}

void QmlBackend::controllerMappingButtonQuit()
{
    if(controller_mapping_in_progress && controller_mapping_controller)
        controller_mapping_controller->IsUpdatingMappingButton(false);
}

void QmlBackend::controllerMappingApply()
{
    QString new_controller_mapping = controller_mapping_controller_type;
    QMapIterator<QString, QStringList> i(controller_mapping_controller_mappings);
    while (i.hasNext()) {
        i.next();
        const auto &physical_buttons = i.value();
        for(int j = 0; j < physical_buttons.length(); j++)
            new_controller_mapping += "," + (i.key() + ":" + physical_buttons.at(j));
    }
    // if user actually reset to default manually, reset mapping, else add
    if(new_controller_mapping == controller_mapping_original_controller_mappings.value(controller_mapping_controller_guid))
    {
        settings->RemoveControllerMapping(controller_mapping_controller_vid_pid);
        int result = SDL_GameControllerAddMapping(controller_mapping_original_controller_mappings.value(controller_mapping_controller_guid).toUtf8().constData());
        switch(result)
        {
            case -1:
                qCWarning(chiakiGui) << "Error setting controller mapping for guid: " << controller_mapping_controller_guid << " with error: " << SDL_GetError();
                break;
            case 0:
                qCInfo(chiakiGui) << "Updated controller mapping for guid: " << controller_mapping_controller_guid;
                break;
            case 1:
                qCInfo(chiakiGui) << "Added controller mapping for guid: " << controller_mapping_controller_guid;
                break;
            default:
                qCInfo(chiakiGui) << "Unidentified problem mapping for guid: " << controller_mapping_controller_guid;
                break;
        }
    }
    else
    {
        QStringList existing_vidpid;
        if(controller_guids_to_update.contains(controller_mapping_controller_vid_pid))
            existing_vidpid.append(controller_guids_to_update.value(controller_mapping_controller_vid_pid));
        existing_vidpid.append(controller_mapping_controller_guid);
        controller_guids_to_update.insert(controller_mapping_controller_vid_pid, existing_vidpid);
        settings->SetControllerMapping(controller_mapping_controller_vid_pid, new_controller_mapping);
    }
}

void QmlBackend::updateDiscoveryHosts()
{
    // The game on the console of the session, for the stream scenes
    if (session) {
        for (const auto &host : discovery_manager.GetHosts()) {
            if (host.host_addr == session_info.host && host.state == CHIAKI_DISCOVERY_HOST_STATE_READY) {
                live_scenes->SetGame(host.running_app_name);
                break;
            }
        }
    }
    // The game of a public room: what the console of the session says it runs
    if (session && session->GetCoPlayPublic()) {
        for (const auto &host : discovery_manager.GetHosts()) {
            if (host.host_addr == session_info.host && host.state == CHIAKI_DISCOVERY_HOST_STATE_READY) {
                session->SetCoPlayRoomGame(host.running_app_name, host.running_app_titleid);
                break;
            }
        }
    }
    // Wakeup console that we are currently connecting to
    for (const auto &host : discovery_manager.GetHosts()) {
        if (host.host_addr != session_info.host)
            continue;
        if (host.ps5 != chiaki_target_is_ps5(session_info.target))
            continue;
        if (!settings->GetRegisteredHostRegistered(host.GetHostMAC()))
            continue;
        auto registered = settings->GetRegisteredHost(host.GetHostMAC());
        if (registered.GetRPRegistKey() == session_info.regist_key) {
            if(wakeup_start && session && host.host_name == wakeup_nickname && host.state == CHIAKI_DISCOVERY_HOST_STATE_READY)
            {
                wakeup_nickname.clear();
                wakeup_start = false;
                wakeup_start_timer->stop();
                startSession(true);
            }
            else if(host.state == CHIAKI_DISCOVERY_HOST_STATE_STANDBY && session && session->IsConnecting())
            {
                sendWakeup(host.host_addr, registered.GetRPRegistKey(), host.ps5);
                QString nickname = host.host_name;
                wakeup_nickname = nickname;
                waking_sleeping_nicknames.append(nickname);
                QTimer::singleShot(WAKEUP_PSN_IGNORE_SECONDS * 1000, [this, nickname]{
                    waking_sleeping_nicknames.removeOne(nickname);
                    emit hostsChanged();
                });
                break;
            }
        }
    }
    if (autoConnect()) {
        const int hosts_count = discovery_manager.GetHosts().count();
        for (int i = 0; i < hosts_count; ++i) {
            if (discovery_manager.GetHosts().at(i).GetHostMAC() != auto_connect_mac)
                continue;
            psn_auto_connect_timer->stop();
            startConsole(i, discovery_manager.GetHosts().at(i).host_name);
            break;
        }
    }
    emit hostsChanged();
}

void QmlBackend::prepareStartupPresentation(bool present_warmup)
{
    if (!window)
        return;

    const auto &connect_info = session_info;
    if (present_warmup && connect_info.video_profile.width > 0 && connect_info.video_profile.height > 0)
        window->presentStartupWarmupFrame(connect_info.video_profile.width,
                                          connect_info.video_profile.height,
                                          connect_info.video_profile.codec == CHIAKI_CODEC_H265_HDR);
}

void QmlBackend::applyStartupWindowSizing()
{
    if (!window)
        return;

    const auto &connect_info = session_info;
    if (window->windowState() == Qt::WindowFullScreen)
        return;

    if(settings->GetWindowType() == WindowType::CustomResolution)
    {
        window->resize(settings->GetCustomResolutionWidth(), settings->GetCustomResolutionHeight());
        window->setMaximumSize(QSize(settings->GetCustomResolutionWidth(), settings->GetCustomResolutionHeight()));
    }
    else if(settings->GetWindowType() == WindowType::AdjustableResolution)
    {
        window->normalTime();
        if(!settings->GetStreamGeometry().isEmpty())
            window->setGeometry(settings->GetStreamGeometry());
    }
    else if (connect_info.video_profile.width > 0 && connect_info.video_profile.height > 0)
    {
        window->resize(connect_info.video_profile.width, connect_info.video_profile.height);
    }
}

#if CHIAKI_GUI_ENABLE_STEAM_SHORTCUT
QString QmlBackend::getExecutable() {
#if defined(Q_OS_LINUX)
    //Check for flatpak
    const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    QString flatpakId = env.value("FLATPAK_ID");
    QString appImagePath = env.value("APPIMAGE");
    if (!flatpakId.isEmpty()) {
        return QString("flatpak");
    }
    if (!appImagePath.isEmpty())
        return appImagePath;
#endif
    return QCoreApplication::applicationFilePath();
}

void QmlBackend::createSteamShortcut(QString shortcutName, QString launchOptions, const QJSValue &callback, QString steamDir)
{
    QJSValue cb = callback;
    QString controller_layout_workshop_id = "3049833406";
    QMap<QString, const QPixmap*> artwork;
    auto landscape = QPixmap(":/icons/steam_landscape.png");
    auto portrait = QPixmap(":/icons/steam_portrait.png");
    QImageReader reader;
    reader.setAllocationLimit(512);
    reader.setFileName(":/icons/steam_hero.png");
    auto hero = QPixmap::fromImageReader(&reader);
    auto icon = QPixmap(":/icons/steam_icon.png");
    auto logo = QPixmap(":/icons/steam_logo.png");
    artwork.insert("landscape", &landscape);
    artwork.insert("portrait", &portrait);
    artwork.insert("hero", &hero);
    artwork.insert("icon", &icon);
    artwork.insert("logo", &logo);
    
    auto infoLambda = [callback](const QString &infoMessage) {
        QJSValue icb = callback;
        if (icb.isCallable())
            icb.call({infoMessage, true, false});
    };

    auto errorLambda = [callback](const QString &errorMessage) {
        QJSValue icb = callback;
        if (icb.isCallable())
            icb.call({errorMessage, false, true});
    };
    SteamTools* steam_tools = new SteamTools(infoLambda, errorLambda, steamDir);
    bool steamExists = steam_tools->steamExists();
    if(!steamExists)
    {
        if (cb.isCallable())
            cb.call({QString("[E] Steam does not exist, cannot create Steam Shortcut"), false, true});
        delete steam_tools;
        return;
    }

    QString executable = getExecutable();
    if(executable == "flatpak")
    {
        const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        QString flatpakId = env.value("FLATPAK_ID");
        launchOptions.prepend(QString("run %1 ").arg(flatpakId));
    }
    SteamShortcutEntry newShortcut = steam_tools->buildShortcutEntry(std::move(shortcutName), std::move(executable), std::move(launchOptions), std::move(artwork));

    QVector<SteamShortcutEntry> shortcuts = steam_tools->parseShortcuts();
    bool found = false;
    for (auto& map : shortcuts) {
        if (map.getExe() == newShortcut.getExe() && map.getLaunchOptions() == newShortcut.getLaunchOptions()) {
            // Replace the entire map with the new one
            
            if (cb.isCallable())
                cb.call({QString("[I] Updating Steam entry"), true, false});
            map = newShortcut;
            found = true;
            break;  // Stop iterating once a match is found
        }
    }

   //If we didn't find it to update, let's add it to the end
    if (!found) {
        if (cb.isCallable())
            cb.call({QString("[I] Adding Steam entry ") + QString(newShortcut.getAppName().toStdString().c_str()), false, true});
        shortcuts.append(newShortcut);
    }
    steam_tools->updateShortcuts(std::move(shortcuts));
    steam_tools->updateControllerConfig(newShortcut.getAppName(), std::move(controller_layout_workshop_id));
    if (!found)
    {
        if (cb.isCallable())
            cb.call({QString("[I] Added Steam entry: ") + QString(newShortcut.getAppName().toStdString().c_str()), true, true});
    }
    else
    {
        if (cb.isCallable())
            cb.call({QString("[I] Updated Steam entry: ") + QString(newShortcut.getAppName().toStdString().c_str()), true, true});
    }
    delete steam_tools;
}
#endif

QString QmlBackend::openPsnLink()
{
    QUrl url = psnLoginUrl();
    QDesktopServices::openUrl(url);
    url = psnLoginUrl();
    return QString(url.toEncoded());
}

QString QmlBackend::openPlaceboOptionsLink()
{
    // Opened in the browser, and given back for "Copy the address" where that did not work
    const QUrl url = QUrl("https://libplacebo.org/options/");
    QDesktopServices::openUrl(url);
    return QString(url.toEncoded());
}

bool QmlBackend::checkPsnRedirectURL(const QUrl &url) const
{
    return url.toString().startsWith(QString::fromStdString(PSNAuth::REDIRECT_PAGE));
}

void QmlBackend::initPsnAuth(const QUrl &url, const QJSValue &callback)
{
    const QJSValue cb = callback;
    if (!url.toString().startsWith(QString::fromStdString(PSNAuth::REDIRECT_PAGE)))
    {
        if (cb.isCallable())
            cb.call({QString("[E] Invalid URL: Please make sure you have copy and pasted the URL correctly."), false, true});
        return;
    }
    const QString code = QUrlQuery(url).queryItemValue("code");
    if (code.isEmpty())
    {
        if (cb.isCallable())
            cb.call({QString("[E] Invalid code from redirect url."), false, true});
        return;
    }
    PSNAccountID *psnId = new PSNAccountID(settings, this);
    connect(psnId, &PSNAccountID::AccountIDResponse, this, &QmlBackend::updatePsnHosts);
    connect(psnId, &PSNAccountID::AccountIDError, this, [cb](const QString &error) {
        if (cb.isCallable())
            cb.call({error, false, true});
    });
    connect(psnId, &PSNAccountID::AccountIDResponse, this, [cb]() {
        if (cb.isCallable())
            cb.call({QString("[I] PSN Remote Connection Tokens Generated."), true, true});
    });
    connect(psnId, &PSNAccountID::Finished, psnId, &QObject::deleteLater);
    psnId->GetPsnAccountId(code);
    emit psnTokenChanged();
}

void QmlBackend::refreshAuth()
{
    PSNToken *psnToken = new PSNToken(settings, this);
    connect(psnToken, &PSNToken::PSNTokenError, this, [this](const QString &error) {
        qCWarning(chiakiGui) << "Could not refresh token. Automatic PSN Connection Unavailable!" << error;
    });
    connect(psnToken, &PSNToken::UnauthorizedError, this, &QmlBackend::psnCredsExpired);
    connect(psnToken, &PSNToken::PSNTokenSuccess, this, []() {
        qCWarning(chiakiGui) << "PSN Remote Connection Tokens Refreshed.";
    });
    connect(psnToken, &PSNToken::PSNTokenSuccess, this, &QmlBackend::updatePsnHosts);
    connect(psnToken, &PSNToken::Finished, psnToken, &QObject::deleteLater);
    QString refresh_token = settings->GetPsnRefreshToken();
    psnToken->RefreshPsnToken(std::move(refresh_token));
}

void QmlBackend::updatePsnHosts()
{
    if(updating_psn_hosts)
    {
        qCInfo(chiakiGui) << "Already updating psn hosts, skipping...";
        return;
    }
    psn_hosts_future = QtConcurrent::run(&QmlBackend::updatePsnHostsThread, this);
    psn_hosts_watcher.setFuture(psn_hosts_future);
    updating_psn_hosts = true;
}

void QmlBackend::updatePsnHostsThread()
{
    QString psn_token = settings->GetPsnAuthToken();
    if(psn_token.isEmpty())
        return;

    ChiakiHolepunchDeviceInfo *device_info_ps5 = nullptr;
    size_t num_devices_ps5 = 0;
    ChiakiLog backend_log;
    chiaki_log_init(&backend_log, settings->GetLogLevelMask(), chiaki_log_cb_print, nullptr);
    for(int i = 0; i < PSN_DEVICES_TRIES; i++)
    {
        ChiakiErrorCode err = chiaki_holepunch_list_devices(psn_token.toUtf8().constData(), CHIAKI_HOLEPUNCH_CONSOLE_TYPE_PS5, &device_info_ps5, &num_devices_ps5, &backend_log);
        if (err != CHIAKI_ERR_SUCCESS)
        {
            if(PSN_DEVICES_TRIES - i > 1)
            {
                qCWarning(chiakiGui) << "Failed to get PS5 devices trying again";
                continue;
            }
            else
            {
                qCWarning(chiakiGui) << "Failed to get PS5 devices after max tries: " << PSN_DEVICES_TRIES;
                num_devices_ps5 = 0;
            }
        }
        break;
    }
    for (size_t i = 0; i < num_devices_ps5; i++)
    {
        ChiakiHolepunchDeviceInfo dev = device_info_ps5[i];
        // skip devices that don't have remote play enabled
        if(!dev.remoteplay_enabled)
            continue;
        QByteArray duid_bytes(reinterpret_cast<char*>(dev.device_uid), sizeof(dev.device_uid));
        QString duid = QString(duid_bytes.toHex());
        QString name = QString(dev.device_name);
        bool ps5 = true;
        PsnHost psn_host(duid, name, ps5);
        if(!psn_nickname_hosts.contains(name))
            psn_nickname_hosts.insert(name, psn_host);
	    if(!psn_hosts.contains(duid) && settings->GetNicknameRegisteredHostRegistered(name))
		    psn_hosts.insert(duid, psn_host);
    }
    QByteArray duid_bytes(32, 'A');
    QString duid = QString(duid_bytes.toHex());
    QString name = QString("Main PS4 Console");
    bool ps5 = false;
    PsnHost psn_host(duid, name, ps5);
    if(!psn_nickname_hosts.contains(name))
        psn_nickname_hosts.insert(name, psn_host);
    if(!psn_hosts.contains(duid) && (settings->GetPS4RegisteredHostsRegistered() > 0))
        psn_hosts.insert(duid, psn_host);

    emit hostsChanged();
    qCInfo(chiakiGui) << "Updated PSN hosts";
    if(device_info_ps5)
        chiaki_holepunch_free_device_list(&device_info_ps5);
}

void QmlBackend::refreshPsnToken()
{
    QString expiry_s = settings->GetPsnAuthTokenExpiry();
    QString refresh = settings->GetPsnRefreshToken();
    if(expiry_s.isEmpty() || refresh.isEmpty())
        return;
    QDateTime expiry = QDateTime::fromString(expiry_s, settings->GetTimeFormat());
    // give 1 minute buffer
    QDateTime now = QDateTime::currentDateTime().addSecs(60);
    if (now >= expiry)
        refreshAuth();
    else
        updatePsnHosts();
}

void PsnConnectionWorker::ConnectPsnConnection(StreamSession *session, const QString &duid, const bool &ps5)
{
    ChiakiErrorCode result = session->ConnectPsnConnection(duid, ps5);
    emit resultReady(result);
}
