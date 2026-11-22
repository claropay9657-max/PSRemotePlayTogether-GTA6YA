// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_KEYMOUSE_H
#define CHIAKI_KEYMOUSE_H

#include "keymousepad.h"

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariant>

class QEvent;
class QKeyEvent;
class QWheelEvent;
class Settings;

/**
 * Mouse and keyboard as a controller, in the app: which key, mouse button or wheel direction
 * is which button of the controller (up to three for each, kept in the settings), how the
 * mouse aims, and what all of that makes of what is pressed right now. The working out is
 * keymouse::Converter's (keymousepad.h); this feeds it from the window's events, keeps its
 * settings, and tells the page in the settings what to show.
 *
 * The window (QmlMainWindow) hands over the keys while a game has the keyboard, and the
 * mouse while it is caught for aiming; the session sends the controller on whenever it
 * changes (stateChanged). In the settings the same can be tried without a game (`trying`).
 */
class KeyMouse : public QObject
{
	Q_OBJECT
	/** Function → what it is bound to: [{ label, kind: "key" | "mouse" | "wheel" }, …]. */
	Q_PROPERTY(QVariantMap bindings READ bindings NOTIFY bindingsChanged)
	/** How many inputs one function takes. */
	Q_PROPERTY(int maxInputs READ maxInputs CONSTANT)
	/** The function whose input is being chosen: the next key, mouse button or wheel notch is it. "" if none. */
	Q_PROPERTY(QString listening READ listening NOTIFY listeningChanged)
	/** Which of the function's inputs, counted from 0. */
	Q_PROPERTY(int listeningSlot READ listeningSlot NOTIFY listeningChanged)

	/** The mouse aims: caught while playing, its movement a stick, its buttons and wheel bound like keys. */
	Q_PROPERTY(bool aim READ aim WRITE setAim NOTIFY optionsChanged)
	Q_PROPERTY(bool rightStick READ rightStick WRITE setRightStick NOTIFY optionsChanged)
	Q_PROPERTY(double sensX READ sensX WRITE setSensX NOTIFY optionsChanged)
	Q_PROPERTY(double sensY READ sensY WRITE setSensY NOTIFY optionsChanged)
	/** Acceleration: how far the stick goes at 20, 40, 60 and 80 % of full speed, each 0 to 1. */
	Q_PROPERTY(QVariantList curve READ curve WRITE setCurve NOTIFY optionsChanged)
	/** "even", "fine", "quick", or "custom" for a curve drawn by hand; setting one of the first three puts its curve in place. */
	Q_PROPERTY(QString curveName READ curveName WRITE setCurveName NOTIFY optionsChanged)
	Q_PROPERTY(double deadZone READ deadZone WRITE setDeadZone NOTIFY optionsChanged)
	/** In milliseconds. */
	Q_PROPERTY(int smoothing READ smoothing WRITE setSmoothing NOTIFY optionsChanged)
	Q_PROPERTY(double aimFactor READ aimFactor WRITE setAimFactor NOTIFY optionsChanged)
	/** "l2", "l1" or "" for none. */
	Q_PROPERTY(QString aimButton READ aimButton WRITE setAimButton NOTIFY optionsChanged)
	Q_PROPERTY(bool invertY READ invertY WRITE setInvertY NOTIFY optionsChanged)
	Q_PROPERTY(double walk READ walk WRITE setWalk NOTIFY optionsChanged)

	/** Trying it in the settings: keys and mouse go here instead of to the window, until Escape. */
	Q_PROPERTY(bool trying READ trying WRITE setTrying NOTIFY tryingChanged)
	/** The window has the mouse caught for aiming right now. */
	Q_PROPERTY(bool caught READ caught NOTIFY caughtChanged)
	/** The controller as it is, the way the controller picture wants it: { b, l2, r2, lx, ly, rx, ry }. */
	Q_PROPERTY(QVariantMap pad READ pad NOTIFY padChanged)
	/** What catches the mouse and lets it go, as it is written on its key; "" if nothing does. */
	Q_PROPERTY(QString catchKey READ catchKey NOTIFY bindingsChanged)

	public:
		explicit KeyMouse(Settings *settings, QObject *parent = nullptr);

		/** Another set of settings: its bindings and its way of aiming. */
		void SetSettings(Settings *settings);

		QVariantMap bindings() const;
		int maxInputs() const { return static_cast<int>(keymouse::kSlots); }
		QString listening() const { return listening_function; }
		int listeningSlot() const { return listening_slot; }

		bool aim() const { return options.aim; }
		void setAim(bool on);
		bool rightStick() const { return options.right_stick; }
		void setRightStick(bool right);
		double sensX() const { return options.sens_x; }
		void setSensX(double v);
		double sensY() const { return options.sens_y; }
		void setSensY(double v);
		QVariantList curve() const;
		void setCurve(const QVariantList &points);
		QString curveName() const;
		void setCurveName(const QString &name);
		double deadZone() const { return options.dead_zone; }
		void setDeadZone(double v);
		int smoothing() const { return options.smoothing_ms; }
		void setSmoothing(int ms);
		double aimFactor() const { return options.aim_factor; }
		void setAimFactor(double v);
		QString aimButton() const;
		void setAimButton(const QString &button);
		bool invertY() const { return options.invert_y; }
		void setInvertY(bool on);
		double walk() const { return options.walk; }
		void setWalk(double v);

		bool trying() const { return is_trying; }
		void setTrying(bool on);
		bool caught() const { return is_caught; }
		QVariantMap pad() const;
		QString catchKey() const;

		/** Waits for the next key, mouse button or wheel notch: it becomes this function's input number `slot`. */
		Q_INVOKABLE void listen(const QString &function, int slot);
		Q_INVOKABLE void stopListening();
		/** Takes an input away from a function. */
		Q_INVOKABLE void clear(const QString &function, int slot);
		/** Every binding as a layout has it: "classic" (the keys the app came with) or "shooter" (WASD and the mouse, which then aims). */
		Q_INVOKABLE void applyLayout(const QString &name);
		/** The points of one of the curves that have a name. */
		Q_INVOKABLE QVariantList namedCurve(const QString &name) const;

		// ---- for the window

		/** While an input is being chosen: takes the event that chooses it (or Escape, which keeps the old one). */
		bool ListenEvent(QEvent *event);
		/** A key went down or came up. */
		void KeyEvent(QKeyEvent *event);
		/** A mouse button, while the mouse is caught. */
		void MouseButton(Qt::MouseButton button, bool down);
		/** The wheel, while the mouse is caught. */
		void WheelEvent(QWheelEvent *event);
		/** The caught mouse moved by this much. */
		void MouseMoved(double dx, double dy);
		/** Nothing is held any more: the window lost the keyboard, a menu opened, the mouse was let go. */
		void ReleaseAll();
		/** Told by the window when it catches the mouse for aiming and when it lets it go. */
		void SetCaught(bool caught);

		// ---- for the session

		const keymouse::Pad &State() const { return state; }
		/** Whether this key (Qt::Key) is bound to anything. */
		bool Binds(int key) const;
		/** Whether anything bound was pressed since the last ReleaseAll(): the keyboard counts as a controller then. */
		bool Used() const { return used; }

	signals:
		void bindingsChanged();
		void listeningChanged();
		void optionsChanged();
		void tryingChanged();
		void caughtChanged();
		void padChanged();
		/** The controller looks different. */
		void stateChanged();

	private:
		void Load();
		void SaveBindings();
		void SaveOptions();
		void ApplyOptions();
		void Update();
		void Assign(keymouse::Function function, int slot, const keymouse::Input &input);
		void SetBindings(const keymouse::Bindings &bindings, bool save);

		Settings *settings;
		keymouse::Converter converter;
		keymouse::Options options;
		keymouse::Pad state;
		QElapsedTimer clock;
		QTimer timer;
		QString listening_function;
		int listening_slot = -1;
		bool is_trying = false;
		bool is_caught = false;
		bool used = false;
		/** What the wheel has turned short of a whole notch, and when it last turned. */
		int wheel_angle = 0;
		qint64 wheel_at = 0;
		/** The mouse button that was just chosen as an input, until it comes up. */
		uint32_t chosen_button = 0;
};

#endif // CHIAKI_KEYMOUSE_H
