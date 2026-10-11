/* SPDX-License-Identifier: MIT */
#pragma once

#include <NakamaClient.h>

#include <QSettings>
#include <QString>
#include <QStringList>

#include <memory>

/* The launcher's settings: the server, this player's account and name, STUN servers, and
 * where the emulators and IKEMEN GO are. They live in an INI file in the configuration
 * folder, next to the game list. */
class Settings {
public:
	explicit Settings(const QString& folder);

	QString folder() const { return m_folder; }

	NetplayOnline::ServerConfig server() const;
	void setServer(const NetplayOnline::ServerConfig&);

	QString name() const;
	void setName(const QString&);
	/* A random id made on first use: the account this computer signs in as. */
	QString deviceId();
	void setDeviceId(const QString&);

	QStringList stunServers() const;
	void setStunServers(const QStringList&);

	QString mgbaPath() const;
	void setMgbaPath(const QString&);
	QString melondsPath() const;
	void setMelondsPath(const QString&);
	QString ikemenPath() const;
	void setIkemenPath(const QString&);

	QString value(const QString& key) const;
	void setValue(const QString& key, const QString& value);

private:
	QString m_folder;
	std::unique_ptr<QSettings> m_settings;
};
