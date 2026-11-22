#pragma once

#include "controllermanager.h"

class QTimer;

class QmlController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool dualSense READ isDualSense CONSTANT)
    Q_PROPERTY(bool handheld READ isHandheld CONSTANT)
    Q_PROPERTY(bool steamVirtual READ isSteamVirtual CONSTANT)
    Q_PROPERTY(bool dualSenseEdge READ isDualSenseEdge CONSTANT)
    Q_PROPERTY(bool playStation READ isPS CONSTANT)
    /** What the controller calls itself ("Xbox Series X Controller"). */
    Q_PROPERTY(QString name READ name CONSTANT)
    /** Its charge in percent (SDL's coarse levels: 5, 20, 55, 100); 254 on a cable, 255 unknown. */
    Q_PROPERTY(int battery READ battery NOTIFY batteryChanged)

public:
    QmlController(Controller *controller, uint32_t shortcut, QObject *target, QObject *parent = nullptr);
    ~QmlController();

    bool isDualSense() const;
    bool isHandheld() const;
    bool isSteamVirtual() const;
    bool isDualSenseEdge() const;
    bool isPS() const;
    QString name() const;
    int battery() const { return battery_level; }
    void setEscapeShortcut(uint32_t shortcut) { escape_shortcut = shortcut; };
    QString GetGUID() const;
    QString GetVIDPID() const;

signals:
    void batteryChanged();

private:
    void sendKey(Qt::Key key, Qt::KeyboardModifiers modifiers = Qt::NoModifier);

    QObject *target = {};
    uint32_t escape_shortcut = 0;
    uint32_t old_buttons = 0;
    Controller *controller = {};
    QTimer *repeat_timer = {};
    int repeat_running = 0;
    Qt::Key pressed_key = Qt::Key_unknown;
    QTimer *battery_timer = {};
    int battery_level = 255;
};
