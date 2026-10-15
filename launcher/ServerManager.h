/* SPDX-License-Identifier: MIT */
#pragma once

#include "ServerConfig.h"
#include "ServerHost.h"

#include <QHash>
#include <QJsonObject>
#include <QObject>

#include <functional>

class QNetworkAccessManager;

/* The community servers of this configuration folder: their profiles (servers.json) and
 * the ones running. The window and the command line share it. */
class ServerManager : public QObject {
	Q_OBJECT

public:
	explicit ServerManager(const QString& folder, QObject* parent = nullptr);
	~ServerManager() override;

	ServerConfig& config() { return m_config; }
	const ServerConfig& config() const { return m_config; }
	bool load(QString* error) { return m_config.load(error); }
	bool save(QString* error) const { return m_config.save(error); }

	ServerHost* host(const QString& id) const { return m_hosts.value(id); }
	ServerHost::State state(const QString& id) const;
	QStringList active() const;   // the servers starting, running or stopping

	/* Starts a server with its saved profile. */
	bool start(const QString& id, QString* error);
	void stop(const QString& id);
	void stopAll();
	/* For the end of the program: stops every server and waits for them. */
	void stopAllAndWait(int msecs);

	/* The programs, as configured or found: empty when not found. For a server whose
	 * database exists, the PostgreSQL of the database's version is preferred. */
	QString nakamaProgram() const;
	QString postgresFolder(const ServerProfile* = nullptr) const;
	QString modulesFolder() const;

	/* Another running server of this program that shares a port, the data folder or the
	 * database with this one. */
	QString runningConflict(const ServerProfile&) const;

	struct IkemenProfileOptions {
		QString game;
		QString build;
		QString host;          // empty: the server's public address
		QString region;
		QStringList stun;      // empty: the launcher's default STUN servers
	};
	/* An IKEMEN GO client profile (data/online/servers.json) for a server. */
	QByteArray ikemenProfile(const ServerProfile&, const IkemenProfileOptions&) const;
	static QStringList defaultStunServers();
	/* Whether players elsewhere could not reach this address (none set, or this computer). */
	static bool localOnly(const QString& host);
	/* What players enter to connect, as lines of text. */
	QStringList connectionInfo(const ServerProfile&) const;

	/* The profile a server runs with: the one this program started it with, or when another
	 * program of this computer runs it (its server.lock), the saved one with the port,
	 * address, HTTP key, certificate and games it was started with (its nakama.yml). False,
	 * with the reason, when it is not running here. */
	bool runningProfile(const QString& id, ServerProfile* profile, QString* why) const;

	using RpcDone = std::function<void(const QJsonObject& answer, const QString& error)>;
	/* Calls one of the IKEMEN GO modules' moderator RPCs (ikemen_ranked_list,
	 * ikemen_ranked_replay, ikemen_ranked_settle; RankedSets.h) on a running server that
	 * serves IKEMEN GO: over HTTP on this computer, at the server's listen address (or
	 * 127.0.0.1) and its port, never through a proxy, with its runtime HTTP key, which goes
	 * to that server only.
	 * done gets the RPC's answer, or why there is none (error not empty); it is not called
	 * when context is destroyed first. */
	void moderatorRpc(const QString& id, const QString& rpc, const QJsonObject& payload, QObject* context, RpcDone done);

signals:
	void stateChanged(const QString& id, ServerHost::State);
	void logLine(const QString& id, const QString& line);

private:
	ServerConfig m_config;
	QHash<QString, ServerHost*> m_hosts;
	QNetworkAccessManager* m_http = nullptr;
};
