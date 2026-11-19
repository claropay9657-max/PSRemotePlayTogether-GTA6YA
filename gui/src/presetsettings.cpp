// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "presetsettings.h"
#include "settings.h"

#include <QJsonDocument>

PresetSettings::PresetSettings(Settings *settings, const QString &group, const QList<Key> &keys,
		const QJsonArray &builtin, QObject *parent)
	: QObject(parent), settings(settings), group(group), keys(keys), builtin(builtin)
{
	for(const Key &key : keys)
		map.insert(key.name, Coerce(key, settings->GetValue(group + "/" + key.name, key.default_value)));
	LoadPresets();

	// A value written from QML
	connect(&map, &QQmlPropertyMap::valueChanged, this, [this](const QString &key, const QVariant &value) {
		const Key *k = FindKey(key);
		if(!k)
			return;
		QVariant clean = Coerce(*k, value);
		if(clean != value)
			map.insert(key, clean);
		Store(key, clean);
		RememberActive();
		emit changed();
	});
}

const PresetSettings::Key *PresetSettings::FindKey(const QString &name) const
{
	for(const Key &key : keys)
	{
		if(key.name == name)
			return &key;
	}
	return nullptr;
}

bool PresetSettings::has(const QString &key) const
{
	return FindKey(key) != nullptr;
}

/** Whatever arrives (from QML, a file edited by hand, a web page) ends up with the type of the default. */
QVariant PresetSettings::Coerce(const Key &key, const QVariant &value) const
{
	switch(key.default_value.typeId())
	{
		case QMetaType::Bool:
			if(value.typeId() == QMetaType::QString)
				return value.toString() == "true" || value.toString() == "1";
			return value.toBool();
		case QMetaType::Int: {
			bool ok = false;
			int v = value.toInt(&ok);
			return ok ? v : key.default_value;
		}
		case QMetaType::Double: {
			bool ok = false;
			double v = value.toDouble(&ok);
			if(!ok || v != v)
				return key.default_value;
			// Placements are fractions of the free space; negative means "not placed yet"
			if(!key.placement)
				return v;
			return v < 0.0 ? -1.0 : qBound(0.0, v, 1.0);
		}
		default:
			return value.toString();
	}
}

void PresetSettings::Store(const QString &key, const QVariant &value)
{
	settings->SetValue(group + "/" + key, value);
}

QVariant PresetSettings::get(const QString &key) const
{
	return map.value(key);
}

void PresetSettings::set(const QString &key, const QVariant &value)
{
	const Key *k = FindKey(key);
	if(!k)
		return;
	QVariant clean = Coerce(*k, value);
	if(map.value(key) == clean)
		return;
	map.insert(key, clean);
	Store(key, clean);
	RememberActive();
	emit changed();
}

void PresetSettings::Reload()
{
	if(applying)
		return;
	bool any = false;
	for(const Key &key : std::as_const(keys))
	{
		QVariant value = Coerce(key, settings->GetValue(group + "/" + key.name, key.default_value));
		if(map.value(key.name) != value)
		{
			map.insert(key.name, value);
			any = true;
		}
	}
	if(any)
	{
		RememberActive();
		emit changed();
	}
}

QJsonObject PresetSettings::Snapshot() const
{
	QJsonObject o;
	for(const Key &key : keys)
		o[key.name] = QJsonValue::fromVariant(map.value(key.name));
	return o;
}

QStringList PresetSettings::presets() const
{
	QStringList names;
	for(const QJsonValue &v : preset_list)
		names.append(v.toObject().value("name").toString());
	return names;
}

void PresetSettings::LoadPresets()
{
	QJsonDocument doc = QJsonDocument::fromJson(settings->GetValue(group + "/presets").toString().toUtf8());
	preset_list = doc.isArray() ? doc.array() : QJsonArray();
	if(preset_list.isEmpty())
	{
		// First start: the built-in ones. What is set right now becomes the first of them, so
		// that nothing changes for somebody who had already arranged things before presets existed.
		preset_list = builtin;
		if(!preset_list.isEmpty())
		{
			QJsonObject first = Snapshot();
			first["name"] = preset_list[0].toObject().value("name");
			preset_list[0] = first;
		}
		active_index = 0;
		SavePresets();
		return;
	}
	active_index = qBound(0, settings->GetValue(group + "/preset_active", 0).toInt(), static_cast<int>(preset_list.size()) - 1);
}

void PresetSettings::SavePresets()
{
	settings->SetValue(group + "/presets", QString::fromUtf8(QJsonDocument(preset_list).toJson(QJsonDocument::Compact)));
	settings->SetValue(group + "/preset_active", active_index);
}

/** The selected preset follows what is set. */
void PresetSettings::RememberActive()
{
	if(applying || active_index < 0 || active_index >= preset_list.size())
		return;
	QJsonObject preset = Snapshot();
	preset["name"] = preset_list[active_index].toObject().value("name");
	preset_list[active_index] = preset;
	SavePresets();
}

void PresetSettings::ApplyPreset(const QJsonObject &preset)
{
	applying = true;
	for(const Key &key : std::as_const(keys))
	{
		// A key the preset does not mention (it was added to the app later) gets its default
		QVariant value = preset.contains(key.name) ? Coerce(key, preset.value(key.name).toVariant()) : key.default_value;
		map.insert(key.name, value);
		Store(key.name, value);
	}
	applying = false;
	emit changed();
}

void PresetSettings::setActive(int index)
{
	if(index < 0 || index >= preset_list.size() || index == active_index)
		return;
	active_index = index;
	settings->SetValue(group + "/preset_active", active_index);
	ApplyPreset(preset_list[index].toObject());
	emit presetsChanged();
}

void PresetSettings::addPreset(const QString &name)
{
	QString clean = name.trimmed().left(40);
	if(clean.isEmpty())
		clean = tr("Preset %1").arg(preset_list.size() + 1);
	QJsonObject preset = Snapshot();
	preset["name"] = clean;
	preset_list.append(preset);
	active_index = preset_list.size() - 1;
	SavePresets();
	emit presetsChanged();
}

void PresetSettings::renamePreset(const QString &name)
{
	QString clean = name.trimmed().left(40);
	if(clean.isEmpty() || active_index < 0 || active_index >= preset_list.size())
		return;
	QJsonObject preset = preset_list[active_index].toObject();
	preset["name"] = clean;
	preset_list[active_index] = preset;
	SavePresets();
	emit presetsChanged();
}

void PresetSettings::removePreset()
{
	if(preset_list.size() <= 1 || active_index < 0 || active_index >= preset_list.size())
		return;
	preset_list.removeAt(active_index);
	active_index = qMin(active_index, static_cast<int>(preset_list.size()) - 1);
	SavePresets();
	ApplyPreset(preset_list[active_index].toObject());
	emit presetsChanged();
}

void PresetSettings::resetPlacement()
{
	applying = true;
	for(const Key &key : std::as_const(keys))
	{
		if(!key.placement)
			continue;
		map.insert(key.name, key.default_value);
		Store(key.name, key.default_value);
	}
	applying = false;
	RememberActive();
	emit changed();
}

void PresetSettings::resetAll()
{
	preset_list = builtin;
	active_index = 0;
	SavePresets();
	if(!preset_list.isEmpty())
		ApplyPreset(preset_list[0].toObject());
	emit presetsChanged();
}
