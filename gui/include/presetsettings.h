// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_PRESETSETTINGS_H
#define CHIAKI_PRESETSETTINGS_H

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QQmlPropertyMap>
#include <QString>
#include <QStringList>
#include <QVariant>

class Settings;

/**
 * A group of settings that can be changed live and kept as named presets.
 *
 * Used for what is drawn on top of the game picture ("hud") and for the OBS
 * overlay ("overlay"). The settings themselves are ordinary values in the
 * configuration file, "<group>/<key>", and take effect the moment they change.
 * A preset is a named copy of all of them; the selected preset follows every
 * change, so arranging things means arranging the selected preset.
 *
 * The values are offered to QML as a property map: read and bind with
 * `values.cams_show`, change with `values.cams_show = false` or `set()`.
 */
class PresetSettings : public QObject
{
	Q_OBJECT
	Q_PROPERTY(QObject *values READ valuesObject CONSTANT)
	Q_PROPERTY(QStringList presets READ presets NOTIFY presetsChanged)
	Q_PROPERTY(int active READ active WRITE setActive NOTIFY presetsChanged)

	public:
		struct Key
		{
			QString name;
			QVariant default_value;
			/** Where something sits and how large it is: what "reset positions" puts back. */
			bool placement = false;
		};

		/**
		 * @param builtin presets to start with: objects with "name" and the keys that differ from the defaults
		 */
		PresetSettings(Settings *settings, const QString &group, const QList<Key> &keys,
				const QJsonArray &builtin, QObject *parent = nullptr);

		QObject *valuesObject() { return &map; }
		QStringList presets() const;
		int active() const { return active_index; }
		void setActive(int index);

		QVariant get(const QString &key) const;
		bool getBool(const QString &key) const { return get(key).toBool(); }
		int getInt(const QString &key) const { return get(key).toInt(); }
		double getDouble(const QString &key) const { return get(key).toDouble(); }
		QString getString(const QString &key) const { return get(key).toString(); }
		bool has(const QString &key) const;

		Q_INVOKABLE void set(const QString &key, const QVariant &value);
		/** Saves the current values as a new preset and selects it. */
		Q_INVOKABLE void addPreset(const QString &name);
		Q_INVOKABLE void renamePreset(const QString &name);
		/** Removes the selected preset; the last one cannot be removed. */
		Q_INVOKABLE void removePreset();
		/** Puts everything of the selected preset back where it is by default. */
		Q_INVOKABLE void resetPlacement();
		/** Throws all presets away and starts over with the built-in ones. */
		Q_INVOKABLE void resetAll();

		/** Values in the configuration file changed behind our back (another part of the app wrote them). */
		void Reload();
		/** All current values, for handing to something that is not QML. */
		QJsonObject Snapshot() const;

	signals:
		/** One or more values changed, by whatever means. */
		void changed();
		void presetsChanged();

	private:
		QVariant Coerce(const Key &key, const QVariant &value) const;
		const Key *FindKey(const QString &name) const;
		void Store(const QString &key, const QVariant &value);
		void SavePresets();
		void LoadPresets();
		void ApplyPreset(const QJsonObject &preset);
		void RememberActive();

		Settings *settings;
		QString group;
		QList<Key> keys;
		QJsonArray builtin;
		QQmlPropertyMap map;
		QJsonArray preset_list;
		int active_index = 0;
		bool applying = false;
};

#endif // CHIAKI_PRESETSETTINGS_H
