// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "keymouse.h"

#include "settings.h"

#include <QKeyEvent>
#include <QKeySequence>
#include <QMouseEvent>
#include <QWheelEvent>

#include <chiaki/controller.h>

#include <algorithm>
#include <cmath>

using keymouse::Function;

// The converter names the controller's buttons itself, to be tried without the stream library
static_assert(keymouse::kCross == CHIAKI_CONTROLLER_BUTTON_CROSS && keymouse::kCircle == CHIAKI_CONTROLLER_BUTTON_MOON
	&& keymouse::kSquare == CHIAKI_CONTROLLER_BUTTON_BOX && keymouse::kTriangle == CHIAKI_CONTROLLER_BUTTON_PYRAMID
	&& keymouse::kDpadLeft == CHIAKI_CONTROLLER_BUTTON_DPAD_LEFT && keymouse::kDpadRight == CHIAKI_CONTROLLER_BUTTON_DPAD_RIGHT
	&& keymouse::kDpadUp == CHIAKI_CONTROLLER_BUTTON_DPAD_UP && keymouse::kDpadDown == CHIAKI_CONTROLLER_BUTTON_DPAD_DOWN
	&& keymouse::kL1 == CHIAKI_CONTROLLER_BUTTON_L1 && keymouse::kR1 == CHIAKI_CONTROLLER_BUTTON_R1
	&& keymouse::kL3 == CHIAKI_CONTROLLER_BUTTON_L3 && keymouse::kR3 == CHIAKI_CONTROLLER_BUTTON_R3
	&& keymouse::kOptions == CHIAKI_CONTROLLER_BUTTON_OPTIONS && keymouse::kCreate == CHIAKI_CONTROLLER_BUTTON_SHARE
	&& keymouse::kTouchpad == CHIAKI_CONTROLLER_BUTTON_TOUCHPAD && keymouse::kPs == CHIAKI_CONTROLLER_BUTTON_PS,
	"keymousepad.h and chiaki/controller.h name the buttons differently");

namespace
{

/** Each function's name in the settings and for the page. */
const char *const kIds[keymouse::kFunctions] = {
	"cross", "circle", "square", "triangle",
	"dpad_up", "dpad_down", "dpad_left", "dpad_right",
	"l1", "r1", "l2", "r2", "l3", "r3",
	"options", "create", "touchpad", "ps",
	"left_up", "left_down", "left_left", "left_right",
	"right_up", "right_down", "right_left", "right_right",
	"walk", "mouse",
};

Function FunctionOf(const QString &id)
{
	for(size_t f = 0; f < keymouse::kFunctions; f++)
	{
		if(id == QLatin1String(kIds[f]))
			return static_cast<Function>(f);
	}
	return Function::Count;
}

/**
 * Where a key sits, as this system numbers it; 0 if the event does not say. A number from
 * one system means nothing on another, so it is kept with the system's letter (PlaceTag).
 */
uint32_t Place(const QKeyEvent *event)
{
#if defined(Q_OS_MACOS)
	// The Mac numbers its keys from 0 (the A key), and gives 0 as well when it does not know
	const quint32 key = event->nativeVirtualKey();
	if(!key && event->key() != Qt::Key_A)
		return 0;
	return key + 1;
#else
	return event->nativeScanCode();
#endif
}

#if defined(Q_OS_MACOS)
const QLatin1Char kPlaceTag('m');
#elif defined(Q_OS_WIN)
const QLatin1Char kPlaceTag('w');
#else
const QLatin1Char kPlaceTag('x');
#endif

uint32_t MouseCode(Qt::MouseButton button)
{
	uint32_t bits = static_cast<uint32_t>(button);
	if(!bits)
		return 0;
	uint32_t number = 1;
	while(!(bits & 1))
	{
		bits >>= 1;
		number++;
	}
	return keymouse::kMouseButton + number;
}

/** An input as the settings keep it: "k:<key>[:<system><place>]", "m:<button>", "w:up" or "w:down". */
QString Token(const keymouse::Input &input)
{
	if(input.code == keymouse::kWheelUp)
		return QStringLiteral("w:up");
	if(input.code == keymouse::kWheelDown)
		return QStringLiteral("w:down");
	if(keymouse::IsMouse(input.code))
		return QStringLiteral("m:%1").arg(input.code - keymouse::kMouseButton);
	if(input.native)
		return QStringLiteral("k:%1:%2%3").arg(input.code).arg(kPlaceTag).arg(input.native);
	return QStringLiteral("k:%1").arg(input.code);
}

keymouse::Input FromToken(const QString &token)
{
	keymouse::Input input;
	const QStringList parts = token.split(QLatin1Char(':'));
	if(parts.size() < 2)
		return input;
	if(parts[0] == QLatin1String("w"))
		input.code = parts[1] == QLatin1String("up") ? keymouse::kWheelUp : parts[1] == QLatin1String("down") ? keymouse::kWheelDown : 0;
	else if(parts[0] == QLatin1String("m"))
	{
		const uint number = parts[1].toUInt();
		if(number >= 1 && number <= 32)
			input.code = keymouse::kMouseButton + number;
	}
	else if(parts[0] == QLatin1String("k"))
	{
		input.code = parts[1].toUInt();
		if(keymouse::IsMouse(input.code))
			input.code = 0;
		// Its place only if this system wrote it
		if(parts.size() > 2 && parts[2].startsWith(kPlaceTag))
			input.native = parts[2].mid(1).toUInt();
	}
	return input;
}

QString KeyLabel(int key)
{
	switch(key)
	{
		case Qt::Key_Shift: return KeyMouse::tr("Shift");
		case Qt::Key_AltGr: return KeyMouse::tr("AltGr");
		case Qt::Key_CapsLock: return KeyMouse::tr("Caps Lock");
		case Qt::Key_Space: return KeyMouse::tr("Space");
		case Qt::Key_Up: return QStringLiteral("↑");
		case Qt::Key_Down: return QStringLiteral("↓");
		case Qt::Key_Left: return QStringLiteral("←");
		case Qt::Key_Right: return QStringLiteral("→");
		case Qt::Key_Return: return KeyMouse::tr("Return");
		case Qt::Key_Enter: return KeyMouse::tr("Enter");
		case Qt::Key_Escape: return KeyMouse::tr("Esc");
		case Qt::Key_Tab: return KeyMouse::tr("Tab");
		case Qt::Key_Backspace: return KeyMouse::tr("Backspace");
		case Qt::Key_Insert: return KeyMouse::tr("Insert");
		case Qt::Key_Delete: return KeyMouse::tr("Delete");
		case Qt::Key_PageUp: return KeyMouse::tr("Page Up");
		case Qt::Key_PageDown: return KeyMouse::tr("Page Down");
		case Qt::Key_Home: return KeyMouse::tr("Home");
		case Qt::Key_End: return KeyMouse::tr("End");
#if defined(Q_OS_MACOS)
		// On a Mac, Qt calls the ⌘ key Control and the Control key Meta
		case Qt::Key_Control: return KeyMouse::tr("Cmd");
		case Qt::Key_Meta: return KeyMouse::tr("Control");
		case Qt::Key_Alt: return KeyMouse::tr("Option");
#else
		case Qt::Key_Control: return KeyMouse::tr("Ctrl");
		case Qt::Key_Alt: return KeyMouse::tr("Alt");
#if defined(Q_OS_WIN)
		case Qt::Key_Meta: return KeyMouse::tr("Win");
#else
		case Qt::Key_Meta: return KeyMouse::tr("Meta");
#endif
#endif
		default: break;
	}
	const QString text = QKeySequence(key).toString(QKeySequence::NativeText);
	return text.isEmpty() ? KeyMouse::tr("Key %1").arg(key) : text;
}

QString Label(const keymouse::Input &input)
{
	if(input.code == keymouse::kWheelUp)
		return KeyMouse::tr("Wheel up");
	if(input.code == keymouse::kWheelDown)
		return KeyMouse::tr("Wheel down");
	if(keymouse::IsMouse(input.code))
	{
		switch(input.code - keymouse::kMouseButton)
		{
			case 1: return KeyMouse::tr("Left click");
			case 2: return KeyMouse::tr("Right click");
			case 3: return KeyMouse::tr("Middle click");
			default: return KeyMouse::tr("Mouse %1").arg(input.code - keymouse::kMouseButton);
		}
	}
	return KeyLabel(static_cast<int>(input.code));
}

void Bind(keymouse::Bindings &bindings, Function function, uint32_t code)
{
	bindings[static_cast<size_t>(function)].push_back({ code, 0 });
}

/** The Control key: Qt calls it Meta on a Mac, where its Control is the ⌘ key. */
#if defined(Q_OS_MACOS)
const int kControlKey = Qt::Key_Meta;
#else
const int kControlKey = Qt::Key_Control;
#endif

/** The keys the app came with (Settings::GetControllerMapping), and F8 for the mouse. */
keymouse::Bindings ClassicLayout()
{
	keymouse::Bindings b;
	Bind(b, Function::Cross, Qt::Key_Return);
	Bind(b, Function::Circle, Qt::Key_Backspace);
	Bind(b, Function::Square, Qt::Key_Backslash);
	Bind(b, Function::Triangle, Qt::Key_C);
	Bind(b, Function::DpadUp, Qt::Key_Up);
	Bind(b, Function::DpadDown, Qt::Key_Down);
	Bind(b, Function::DpadLeft, Qt::Key_Left);
	Bind(b, Function::DpadRight, Qt::Key_Right);
	Bind(b, Function::L1, Qt::Key_2);
	Bind(b, Function::R1, Qt::Key_3);
	Bind(b, Function::L2, Qt::Key_1);
	Bind(b, Function::R2, Qt::Key_4);
	Bind(b, Function::L3, Qt::Key_5);
	Bind(b, Function::R3, Qt::Key_6);
	Bind(b, Function::Options, Qt::Key_O);
	Bind(b, Function::Create, Qt::Key_F);
	Bind(b, Function::Touchpad, Qt::Key_T);
	Bind(b, Function::Ps, Qt::Key_Escape);
	Bind(b, Function::LeftUp, Qt::Key_Insert);
	Bind(b, Function::LeftDown, Qt::Key_Delete);
	Bind(b, Function::LeftLeft, Qt::Key_BracketLeft);
	Bind(b, Function::LeftRight, Qt::Key_BracketRight);
	Bind(b, Function::RightUp, Qt::Key_PageUp);
	Bind(b, Function::RightDown, Qt::Key_PageDown);
	Bind(b, Function::RightLeft, Qt::Key_Minus);
	Bind(b, Function::RightRight, Qt::Key_Equal);
	Bind(b, Function::MouseCatch, Qt::Key_F8);
	return b;
}

/** As games on a computer have it: WASD walks, the mouse aims, fires and aims down the sights. */
keymouse::Bindings ShooterLayout()
{
	keymouse::Bindings b;
	Bind(b, Function::LeftUp, Qt::Key_W);
	Bind(b, Function::LeftLeft, Qt::Key_A);
	Bind(b, Function::LeftDown, Qt::Key_S);
	Bind(b, Function::LeftRight, Qt::Key_D);
	Bind(b, Function::R2, keymouse::kMouseButton + 1);
	Bind(b, Function::L2, keymouse::kMouseButton + 2);
	Bind(b, Function::Cross, Qt::Key_Space);
	Bind(b, Function::Circle, Qt::Key_C);
	Bind(b, Function::Circle, kControlKey);
	Bind(b, Function::Square, Qt::Key_R);
	Bind(b, Function::Square, Qt::Key_E);
	Bind(b, Function::Triangle, Qt::Key_1);
	Bind(b, Function::Triangle, keymouse::kWheelUp);
	Bind(b, Function::Triangle, keymouse::kWheelDown);
	Bind(b, Function::L1, Qt::Key_Q);
	Bind(b, Function::L1, keymouse::kMouseButton + 4);
	Bind(b, Function::R1, Qt::Key_G);
	Bind(b, Function::R1, keymouse::kMouseButton + 5);
	Bind(b, Function::L3, Qt::Key_Shift);
	Bind(b, Function::R3, Qt::Key_F);
	Bind(b, Function::R3, keymouse::kMouseButton + 3);
	Bind(b, Function::DpadUp, Qt::Key_Up);
	Bind(b, Function::DpadDown, Qt::Key_Down);
	Bind(b, Function::DpadLeft, Qt::Key_Left);
	Bind(b, Function::DpadRight, Qt::Key_Right);
	Bind(b, Function::Options, Qt::Key_Escape);
	Bind(b, Function::Touchpad, Qt::Key_Tab);
	Bind(b, Function::Touchpad, Qt::Key_M);
	Bind(b, Function::Ps, Qt::Key_F1);
	Bind(b, Function::Create, Qt::Key_F2);
	Bind(b, Function::Walk, Qt::Key_Alt);
	Bind(b, Function::MouseCatch, Qt::Key_F8);
	return b;
}

/** The keys from before there were bindings of this kind: what Settings has for each button, one key each. */
keymouse::Bindings CarriedOver(Settings *settings)
{
	static const struct
	{
		int button;
		Function function;
	} kOld[] = {
		{ CHIAKI_CONTROLLER_BUTTON_CROSS, Function::Cross }, { CHIAKI_CONTROLLER_BUTTON_MOON, Function::Circle },
		{ CHIAKI_CONTROLLER_BUTTON_BOX, Function::Square }, { CHIAKI_CONTROLLER_BUTTON_PYRAMID, Function::Triangle },
		{ CHIAKI_CONTROLLER_BUTTON_DPAD_UP, Function::DpadUp }, { CHIAKI_CONTROLLER_BUTTON_DPAD_DOWN, Function::DpadDown },
		{ CHIAKI_CONTROLLER_BUTTON_DPAD_LEFT, Function::DpadLeft }, { CHIAKI_CONTROLLER_BUTTON_DPAD_RIGHT, Function::DpadRight },
		{ CHIAKI_CONTROLLER_BUTTON_L1, Function::L1 }, { CHIAKI_CONTROLLER_BUTTON_R1, Function::R1 },
		{ CHIAKI_CONTROLLER_ANALOG_BUTTON_L2, Function::L2 }, { CHIAKI_CONTROLLER_ANALOG_BUTTON_R2, Function::R2 },
		{ CHIAKI_CONTROLLER_BUTTON_L3, Function::L3 }, { CHIAKI_CONTROLLER_BUTTON_R3, Function::R3 },
		{ CHIAKI_CONTROLLER_BUTTON_OPTIONS, Function::Options }, { CHIAKI_CONTROLLER_BUTTON_SHARE, Function::Create },
		{ CHIAKI_CONTROLLER_BUTTON_TOUCHPAD, Function::Touchpad }, { CHIAKI_CONTROLLER_BUTTON_PS, Function::Ps },
		{ static_cast<int>(ControllerButtonExt::ANALOG_STICK_LEFT_Y_UP), Function::LeftUp },
		{ static_cast<int>(ControllerButtonExt::ANALOG_STICK_LEFT_Y_DOWN), Function::LeftDown },
		{ static_cast<int>(ControllerButtonExt::ANALOG_STICK_LEFT_X_DOWN), Function::LeftLeft },
		{ static_cast<int>(ControllerButtonExt::ANALOG_STICK_LEFT_X_UP), Function::LeftRight },
		{ static_cast<int>(ControllerButtonExt::ANALOG_STICK_RIGHT_Y_UP), Function::RightUp },
		{ static_cast<int>(ControllerButtonExt::ANALOG_STICK_RIGHT_Y_DOWN), Function::RightDown },
		{ static_cast<int>(ControllerButtonExt::ANALOG_STICK_RIGHT_X_DOWN), Function::RightLeft },
		{ static_cast<int>(ControllerButtonExt::ANALOG_STICK_RIGHT_X_UP), Function::RightRight },
	};
	keymouse::Bindings b;
	const QMap<int, Qt::Key> old = settings->GetControllerMapping();
	bool f8_taken = false;
	for(const auto &entry : kOld)
	{
		const int key = old.value(entry.button, Qt::Key(0));
		if(key <= 0 || key == Qt::Key_unknown)
			continue;
		// One key, one button: the map could hold a key twice, the last one counted
		for(auto &inputs : b)
			inputs.erase(std::remove(inputs.begin(), inputs.end(), keymouse::Input{ static_cast<uint32_t>(key), 0 }), inputs.end());
		Bind(b, entry.function, static_cast<uint32_t>(key));
		f8_taken = f8_taken || key == Qt::Key_F8;
	}
	if(!f8_taken)
		Bind(b, Function::MouseCatch, Qt::Key_F8);
	return b;
}

const double kCurves[3][4] = {
	{ 0.2, 0.4, 0.6, 0.8 },     // even
	{ 0.08, 0.23, 0.44, 0.70 }, // fine at slow speed: speed to the power of 1.6
	{ 0.38, 0.58, 0.74, 0.87 }, // quick at slow speed: speed to the power of 0.6
};
const char *const kCurveNames[3] = { "even", "fine", "quick" };

} // namespace

KeyMouse::KeyMouse(Settings *settings, QObject *parent)
	: QObject(parent), settings(settings)
{
	clock.start();
	timer.setInterval(8);
	timer.setTimerType(Qt::PreciseTimer);
	connect(&timer, &QTimer::timeout, this, &KeyMouse::Update);
	Load();
}

void KeyMouse::SetSettings(Settings *new_settings)
{
	settings = new_settings;
	stopListening();
	Load();
	emit bindingsChanged();
	emit optionsChanged();
	Update();
}

void KeyMouse::Load()
{
	keymouse::Bindings b;
	if(!settings->HasKeyMouse(QStringLiteral("version")))
	{
		// The first time: the keys of before
		b = CarriedOver(settings);
		converter.SetBindings(b);
		SaveBindings();
		settings->SetKeyMouse(QStringLiteral("version"), 1);
	}
	else
	{
		for(size_t f = 0; f < keymouse::kFunctions; f++)
		{
			const QStringList tokens = settings->GetKeyMouse(QStringLiteral("bind/") + QLatin1String(kIds[f])).toStringList();
			for(const QString &token : tokens)
			{
				const keymouse::Input input = FromToken(token);
				if(input.code)
					b[f].push_back(input);
			}
		}
		converter.SetBindings(b);
	}

	keymouse::Options o;
	o.aim = settings->GetKeyMouse(QStringLiteral("aim"), false).toBool();
	o.right_stick = settings->GetKeyMouse(QStringLiteral("stick"), QStringLiteral("right")).toString() != QLatin1String("left");
	o.sens_x = settings->GetKeyMouse(QStringLiteral("sens_x"), 1.0).toDouble();
	o.sens_y = settings->GetKeyMouse(QStringLiteral("sens_y"), 1.0).toDouble();
	const QStringList points = settings->GetKeyMouse(QStringLiteral("curve")).toString().split(QLatin1Char(','), Qt::SkipEmptyParts);
	if(points.size() == 4)
	{
		for(int i = 0; i < 4; i++)
			o.curve[static_cast<size_t>(i)] = points[i].toDouble();
	}
	o.dead_zone = settings->GetKeyMouse(QStringLiteral("dead_zone"), 0.10).toDouble();
	o.smoothing_ms = settings->GetKeyMouse(QStringLiteral("smoothing"), 24).toInt();
	o.aim_factor = settings->GetKeyMouse(QStringLiteral("aim_factor"), 0.6).toDouble();
	const QString aim_button = settings->GetKeyMouse(QStringLiteral("aim_button"), QStringLiteral("l2")).toString();
	o.aim_button = aim_button == QLatin1String("l2") ? Function::L2 : aim_button == QLatin1String("l1") ? Function::L1 : Function::Count;
	o.invert_y = settings->GetKeyMouse(QStringLiteral("invert_y"), false).toBool();
	o.walk = settings->GetKeyMouse(QStringLiteral("walk"), 0.5).toDouble();
	converter.SetOptions(o);
	options = converter.GetOptions();
}

void KeyMouse::SaveBindings()
{
	const keymouse::Bindings &b = converter.GetBindings();
	for(size_t f = 0; f < keymouse::kFunctions; f++)
	{
		QStringList tokens;
		for(const keymouse::Input &input : b[f])
			tokens.append(Token(input));
		settings->SetKeyMouse(QStringLiteral("bind/") + QLatin1String(kIds[f]), tokens);
	}
}

void KeyMouse::SaveOptions()
{
	settings->SetKeyMouse(QStringLiteral("aim"), options.aim);
	settings->SetKeyMouse(QStringLiteral("stick"), options.right_stick ? QStringLiteral("right") : QStringLiteral("left"));
	settings->SetKeyMouse(QStringLiteral("sens_x"), options.sens_x);
	settings->SetKeyMouse(QStringLiteral("sens_y"), options.sens_y);
	QStringList points;
	for(double point : options.curve)
		points.append(QString::number(point, 'f', 3));
	settings->SetKeyMouse(QStringLiteral("curve"), points.join(QLatin1Char(',')));
	settings->SetKeyMouse(QStringLiteral("dead_zone"), options.dead_zone);
	settings->SetKeyMouse(QStringLiteral("smoothing"), options.smoothing_ms);
	settings->SetKeyMouse(QStringLiteral("aim_factor"), options.aim_factor);
	settings->SetKeyMouse(QStringLiteral("aim_button"), aimButton());
	settings->SetKeyMouse(QStringLiteral("invert_y"), options.invert_y);
	settings->SetKeyMouse(QStringLiteral("walk"), options.walk);
}

void KeyMouse::ApplyOptions()
{
	converter.SetOptions(options);
	options = converter.GetOptions();
	SaveOptions();
	emit optionsChanged();
	Update();
}

QVariantMap KeyMouse::bindings() const
{
	QVariantMap out;
	const keymouse::Bindings &b = converter.GetBindings();
	for(size_t f = 0; f < keymouse::kFunctions; f++)
	{
		QVariantList inputs;
		for(const keymouse::Input &input : b[f])
		{
			QVariantMap m;
			m[QStringLiteral("label")] = Label(input);
			m[QStringLiteral("kind")] = !keymouse::IsMouse(input.code) ? QStringLiteral("key")
				: (input.code == keymouse::kWheelUp || input.code == keymouse::kWheelDown) ? QStringLiteral("wheel") : QStringLiteral("mouse");
			inputs.append(m);
		}
		out[QLatin1String(kIds[f])] = inputs;
	}
	return out;
}

void KeyMouse::setAim(bool on)
{
	if(options.aim == on)
		return;
	options.aim = on;
	ApplyOptions();
}

void KeyMouse::setRightStick(bool right)
{
	if(options.right_stick == right)
		return;
	options.right_stick = right;
	ApplyOptions();
}

void KeyMouse::setSensX(double v)
{
	options.sens_x = v;
	ApplyOptions();
}

void KeyMouse::setSensY(double v)
{
	options.sens_y = v;
	ApplyOptions();
}

QVariantList KeyMouse::curve() const
{
	QVariantList out;
	for(double point : options.curve)
		out.append(point);
	return out;
}

void KeyMouse::setCurve(const QVariantList &points)
{
	if(points.size() != 4)
		return;
	for(int i = 0; i < 4; i++)
		options.curve[static_cast<size_t>(i)] = points[i].toDouble();
	ApplyOptions();
}

QString KeyMouse::curveName() const
{
	for(int c = 0; c < 3; c++)
	{
		bool same = true;
		for(size_t i = 0; i < 4; i++)
			same = same && std::abs(options.curve[i] - kCurves[c][i]) < 0.005;
		if(same)
			return QLatin1String(kCurveNames[c]);
	}
	return QStringLiteral("custom");
}

QVariantList KeyMouse::namedCurve(const QString &name) const
{
	QVariantList out;
	for(int c = 0; c < 3; c++)
	{
		if(name != QLatin1String(kCurveNames[c]))
			continue;
		for(double point : kCurves[c])
			out.append(point);
	}
	return out;
}

void KeyMouse::setCurveName(const QString &name)
{
	const QVariantList points = namedCurve(name);
	if(!points.isEmpty())
		setCurve(points);
}

void KeyMouse::setDeadZone(double v)
{
	options.dead_zone = v;
	ApplyOptions();
}

void KeyMouse::setSmoothing(int ms)
{
	// What the page offers; the converter itself takes more
	options.smoothing_ms = qBound(8, ms, 80);
	ApplyOptions();
}

void KeyMouse::setAimFactor(double v)
{
	options.aim_factor = v;
	ApplyOptions();
}

QString KeyMouse::aimButton() const
{
	return options.aim_button == Function::L2 ? QStringLiteral("l2") : options.aim_button == Function::L1 ? QStringLiteral("l1") : QString();
}

void KeyMouse::setAimButton(const QString &button)
{
	options.aim_button = button == QLatin1String("l2") ? Function::L2 : button == QLatin1String("l1") ? Function::L1 : Function::Count;
	ApplyOptions();
}

void KeyMouse::setInvertY(bool on)
{
	if(options.invert_y == on)
		return;
	options.invert_y = on;
	ApplyOptions();
}

void KeyMouse::setWalk(double v)
{
	options.walk = v;
	ApplyOptions();
}

void KeyMouse::setTrying(bool on)
{
	if(is_trying == on)
		return;
	stopListening();
	ReleaseAll();
	is_trying = on;
	emit tryingChanged();
}

QVariantMap KeyMouse::pad() const
{
	QVariantMap m;
	m[QStringLiteral("b")] = static_cast<uint>(state.buttons);
	m[QStringLiteral("l2")] = state.l2;
	m[QStringLiteral("r2")] = state.r2;
	m[QStringLiteral("lx")] = state.left_x;
	m[QStringLiteral("ly")] = state.left_y;
	m[QStringLiteral("rx")] = state.right_x;
	m[QStringLiteral("ry")] = state.right_y;
	return m;
}

QString KeyMouse::catchKey() const
{
	const auto &inputs = converter.GetBindings()[static_cast<size_t>(Function::MouseCatch)];
	return inputs.empty() ? QString() : Label(inputs.front());
}

void KeyMouse::listen(const QString &function, int slot)
{
	if(FunctionOf(function) == Function::Count || slot < 0 || slot >= maxInputs())
		return;
	if(listening_function == function && listening_slot == slot)
		return;
	ReleaseAll();
	listening_function = function;
	listening_slot = slot;
	emit listeningChanged();
}

void KeyMouse::stopListening()
{
	if(listening_function.isEmpty())
		return;
	listening_function.clear();
	listening_slot = -1;
	emit listeningChanged();
}

void KeyMouse::SetBindings(const keymouse::Bindings &b, bool save)
{
	converter.SetBindings(b);
	if(save)
		SaveBindings();
	emit bindingsChanged();
	Update();
}

void KeyMouse::Assign(Function function, int slot, const keymouse::Input &input)
{
	keymouse::Bindings b = converter.GetBindings();
	auto &list = b[static_cast<size_t>(function)];
	// The place it had, before the lists change
	const bool replaces = slot >= 0 && slot < static_cast<int>(list.size());
	const keymouse::Input replaced = replaces ? list[static_cast<size_t>(slot)] : keymouse::Input();
	// One input, one function: it is taken away wherever else it was
	for(auto &inputs : b)
	{
		inputs.erase(std::remove_if(inputs.begin(), inputs.end(), [&input](const keymouse::Input &other) {
			return other.code == input.code || (!keymouse::IsMouse(input.code) && input.native && other.native == input.native);
		}), inputs.end());
	}
	if(replaces)
	{
		auto at = std::find(list.begin(), list.end(), replaced);
		if(at != list.end())
			*at = input;
		else
			list.insert(static_cast<size_t>(slot) < list.size() ? list.begin() + slot : list.end(), input);
	}
	else if(list.size() < keymouse::kSlots)
		list.push_back(input);
	else
		list.back() = input;
	SetBindings(b, true);
}

void KeyMouse::clear(const QString &function, int slot)
{
	const Function f = FunctionOf(function);
	if(f == Function::Count)
		return;
	keymouse::Bindings b = converter.GetBindings();
	auto &list = b[static_cast<size_t>(f)];
	if(slot < 0 || slot >= static_cast<int>(list.size()))
		return;
	stopListening();
	list.erase(list.begin() + slot);
	SetBindings(b, true);
}

void KeyMouse::applyLayout(const QString &name)
{
	const bool shooter = name == QLatin1String("shooter");
	if(!shooter && name != QLatin1String("classic"))
		return;
	stopListening();
	SetBindings(shooter ? ShooterLayout() : ClassicLayout(), true);
	setAim(shooter);
}

bool KeyMouse::ListenEvent(QEvent *event)
{
	// The button that chose an input comes up again: that is not a click on anything
	if(chosen_button && event->type() == QEvent::MouseButtonRelease
		&& MouseCode(static_cast<QMouseEvent *>(event)->button()) == chosen_button)
	{
		chosen_button = 0;
		return true;
	}
	if(listening_function.isEmpty())
		return false;
	const Function function = FunctionOf(listening_function);
	switch(event->type())
	{
		case QEvent::ShortcutOverride:
			// Shortcuts wait as well: the key comes as a key
			event->accept();
			return true;
		case QEvent::KeyPress:
		{
			auto *key = static_cast<QKeyEvent *>(event);
			if(key->isAutoRepeat())
				return true;
			if(key->key() == Qt::Key_Escape)
			{
				stopListening();
				return true;
			}
			if(key->key() <= 0 || key->key() == Qt::Key_unknown)
				return true;
			const int slot = listening_slot;
			stopListening();
			Assign(function, slot, { static_cast<uint32_t>(key->key()), Place(key) });
			return true;
		}
		case QEvent::KeyRelease:
			return true;
		case QEvent::MouseButtonPress:
		{
			const uint32_t code = MouseCode(static_cast<QMouseEvent *>(event)->button());
			if(!code)
				return true;
			const int slot = listening_slot;
			stopListening();
			chosen_button = code;
			Assign(function, slot, { code, 0 });
			return true;
		}
		case QEvent::MouseButtonRelease:
		case QEvent::MouseButtonDblClick:
			return true;
		case QEvent::Wheel:
		{
			auto *wheel = static_cast<QWheelEvent *>(event);
			int turned = wheel->angleDelta().y();
			if(wheel->inverted())
				turned = -turned;
			// A whole notch, or a clear push on a trackpad
			wheel_angle = (wheel_angle > 0) == (turned > 0) ? wheel_angle + turned : turned;
			if(std::abs(wheel_angle) < 120)
				return true;
			const uint32_t code = wheel_angle > 0 ? keymouse::kWheelUp : keymouse::kWheelDown;
			wheel_angle = 0;
			const int slot = listening_slot;
			stopListening();
			Assign(function, slot, { code, 0 });
			return true;
		}
		default:
			return false;
	}
}

void KeyMouse::KeyEvent(QKeyEvent *event)
{
	if(event->isAutoRepeat())
		return;
	const bool is_down = event->type() == QEvent::KeyPress;
	const Function function = converter.Press(static_cast<uint32_t>(event->key()), Place(event), is_down);
	if(function == Function::Count)
		return;
	if(function == Function::MouseCatch)
	{
		// Catches the mouse, or lets it go: remembered, like the switch in the settings
		if(is_down)
			setAim(!options.aim);
		return;
	}
	used = true;
	Update();
}

void KeyMouse::MouseButton(Qt::MouseButton button, bool is_down)
{
	const uint32_t code = MouseCode(button);
	if(!code || converter.Press(code, 0, is_down) == Function::Count)
		return;
	used = true;
	Update();
}

void KeyMouse::WheelEvent(QWheelEvent *event)
{
	int turned = event->angleDelta().y();
	// As the wheel turns, not as the page would move
	if(event->inverted())
		turned = -turned;
	if(!turned)
		return;
	const qint64 now = clock.elapsed();
	if(now - wheel_at > 400 || (wheel_angle > 0) != (turned > 0))
		wheel_angle = 0;
	wheel_at = now;
	wheel_angle += turned;
	const int notches = wheel_angle / 120;
	if(!notches)
		return;
	wheel_angle -= notches * 120;
	converter.Wheel(notches, static_cast<uint64_t>(now));
	used = true;
	Update();
}

void KeyMouse::MouseMoved(double dx, double dy)
{
	converter.Move(dx, dy, static_cast<uint64_t>(clock.elapsed()));
	Update();
}

bool KeyMouse::Binds(int key) const
{
	for(const auto &inputs : converter.GetBindings())
	{
		for(const keymouse::Input &input : inputs)
		{
			if(input.code == static_cast<uint32_t>(key))
				return true;
		}
	}
	return false;
}

void KeyMouse::ReleaseAll()
{
	converter.ReleaseAll();
	used = false;
	wheel_angle = 0;
	Update();
}

void KeyMouse::SetCaught(bool caught_now)
{
	if(is_caught == caught_now)
		return;
	is_caught = caught_now;
	emit caughtChanged();
}

void KeyMouse::Update()
{
	const uint64_t now = static_cast<uint64_t>(clock.elapsed());
	const keymouse::Pad next = converter.State(now);
	if(next != state)
	{
		state = next;
		emit stateChanged();
		emit padChanged();
	}
	// The mouse's speed falls off by itself, and a notch of the wheel ends by itself
	if(converter.Busy(now))
	{
		if(!timer.isActive())
			timer.start();
	}
	else
		timer.stop();
	// Keys that were found by their name know their place now: kept, so that they do next time too
	if(converter.TakeLearned())
		SaveBindings();
}
