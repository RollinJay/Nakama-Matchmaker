/* SPDX-License-Identifier: MIT */
#include "Settings.h"

#include <QDir>
#include <QRandomGenerator>

Settings::Settings(const QString& folder)
	: m_folder(folder) {
	QDir().mkpath(folder);
	m_settings.reset(new QSettings(QDir(folder).filePath(QStringLiteral("settings.ini")), QSettings::IniFormat));
}

NetplayOnline::ServerConfig Settings::server() const {
	NetplayOnline::ServerConfig s;
	s.host = m_settings->value(QStringLiteral("server/host"), QStringLiteral("127.0.0.1")).toString();
	s.port = m_settings->value(QStringLiteral("server/port"), 7350).toInt();
	s.serverKey = m_settings->value(QStringLiteral("server/key"), QStringLiteral("defaultkey")).toString();
	s.ssl = m_settings->value(QStringLiteral("server/ssl"), false).toBool();
	return s;
}

void Settings::setServer(const NetplayOnline::ServerConfig& s) {
	m_settings->setValue(QStringLiteral("server/host"), s.host);
	m_settings->setValue(QStringLiteral("server/port"), s.port);
	m_settings->setValue(QStringLiteral("server/key"), s.serverKey);
	m_settings->setValue(QStringLiteral("server/ssl"), s.ssl);
}

QString Settings::name() const {
	return m_settings->value(QStringLiteral("player/name")).toString();
}

void Settings::setName(const QString& name) {
	m_settings->setValue(QStringLiteral("player/name"), name);
}

QString Settings::deviceId() {
	QString id = m_settings->value(QStringLiteral("player/device")).toString();
	if (id.size() < 10) {
		// the account's only credential: from the system's secure generator (std::random_device
		// is not random with some MinGW compilers)
		QByteArray raw(16, Qt::Uninitialized);
		for (int i = 0; i < raw.size(); ++i) {
			raw[i] = char(QRandomGenerator::system()->bounded(256));
		}
		id = QString::fromLatin1(raw.toHex());
		m_settings->setValue(QStringLiteral("player/device"), id);
	}
	return id;
}

void Settings::setDeviceId(const QString& id) {
	m_settings->setValue(QStringLiteral("player/device"), id);
}

QStringList Settings::stunServers() const {
	return m_settings->value(QStringLiteral("network/stun"), QStringList { QStringLiteral("stun.l.google.com:19302"), QStringLiteral("stun.cloudflare.com:3478") }).toStringList();
}

void Settings::setStunServers(const QStringList& servers) {
	m_settings->setValue(QStringLiteral("network/stun"), servers);
}

QString Settings::mgbaPath() const {
	return m_settings->value(QStringLiteral("emulators/mgba")).toString();
}

void Settings::setMgbaPath(const QString& path) {
	m_settings->setValue(QStringLiteral("emulators/mgba"), path);
}

QString Settings::melondsPath() const {
	return m_settings->value(QStringLiteral("emulators/melonds")).toString();
}

void Settings::setMelondsPath(const QString& path) {
	m_settings->setValue(QStringLiteral("emulators/melonds"), path);
}

QString Settings::ikemenPath() const {
	return m_settings->value(QStringLiteral("emulators/ikemen")).toString();
}

void Settings::setIkemenPath(const QString& path) {
	m_settings->setValue(QStringLiteral("emulators/ikemen"), path);
}

QString Settings::value(const QString& key) const {
	return m_settings->value(key).toString();
}

void Settings::setValue(const QString& key, const QString& value) {
	m_settings->setValue(key, value);
}
