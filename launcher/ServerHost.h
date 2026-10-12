/* SPDX-License-Identifier: MIT */
#pragma once

#include "ServerConfig.h"

#include <QElapsedTimer>
#include <QFile>
#include <QObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStringList>
#include <QTimer>

#include <functional>
#include <memory>

class QLockFile;
class QNetworkAccessManager;

/* One running community server: Nakama, and the PostgreSQL it uses unless its profile
 * names another database. Starting goes through these steps, each after the one before
 * has worked:
 *
 * 1. the data folder, which only one program may use at a time (server.lock): Nakama's
 *    configuration (nakama.yml), the server modules for the profile's kinds (modules/),
 *    and the log (server.log);
 * 2. what an earlier run left running when it ended without stopping the server (a crash,
 *    a forced quit): its Nakama and its database, recognized by their process ids, ports
 *    and programs, are stopped;
 * 3. the ports, which must be free;
 * 4. the database, when the server has its own: created on the first start (initdb, and a
 *    nakama role that owns the nakama database and is not a superuser), then started on
 *    127.0.0.1 and waited for;
 * 5. Nakama's database migrations (`nakama migrate up`);
 * 6. Nakama, waited for until its health check answers.
 *
 * Stopping stops Nakama, then the database. When either ends by itself, the other is
 * stopped too and the server is Failed. Passwords reach the programs through the
 * configuration file, PGPASSWORD and standard input, never through a command line. */
class ServerHost : public QObject {
	Q_OBJECT

public:
	enum class State { Stopped, Starting, Running, Stopping, Failed };

	struct Programs {
		QString nakama;     // Nakama's executable
		QString postgres;   // the folder of PostgreSQL's programs (for a server's own database)
		QString modules;    // the bundled server modules (ikemen/, melonds/, community/)
	};

	explicit ServerHost(const QString& id, QObject* parent = nullptr);
	~ServerHost() override;

	QString id() const { return m_id; }
	State state() const { return m_state; }
	bool active() const { return m_state == State::Starting || m_state == State::Running || m_state == State::Stopping; }
	QString error() const { return m_error; }
	/* The profile it was started with. */
	const ServerProfile& profile() const { return m_profile; }
	QStringList recentLog() const { return m_recent; }

	/* Prepares the data folder and starts the first step; false (with the reason) when the
	 * server cannot start at all. */
	bool start(const ServerProfile&, const QString& dataDir, const Programs&, QString* error);
	void stop();
	/* Blocks until the server has stopped, at most the given time; for the end of the
	 * program, when there is no event loop left to wait in. */
	void stopAndWait(int msecs);

	static QString stateLabel(State);
	/* A line of output made readable: Nakama's JSON log lines become
	 * "level message key=value ...". */
	static QString formatLine(const QString& source, const QString& line);
	/* The bundled module sets a profile needs. */
	static QStringList moduleSets(const ServerProfile&);
	/* Builds the modules folder of a server: the bundled sets, then the operator's own
	 * modules, which replace bundled ones of the same name. Notes go to the log lines. */
	static bool installModules(const ServerProfile&, const QString& bundled, const QString& target, QStringList* notes, QString* error);
	/* Whether another program runs the server of this data folder now (its server.lock),
	 * with its process and computer. */
	static bool runningElsewhere(const QString& dataDir, qint64* pid = nullptr, QString* hostname = nullptr);
	/* A port the server needs that a program holds, with what the port is for; empty when
	 * every port is free. */
	static QString busyPort(const ServerProfile&);
	/* The major version of PostgreSQL's programs in a folder (`postgres --version`); 0 when
	 * unknown. */
	static int postgresVersion(const QString& folder);
	/* The marker that says a folder is a server's data folder. */
	static QString markerName();

signals:
	void stateChanged(ServerHost::State);
	void logLine(const QString& line);

private:
	using Done = std::function<void(int exitCode, bool crashed)>;

	void setState(State);
	void log(const QString& source, const QString& line);
	void fail(const QString& error);
	QProcess* spawn(const QString& source, const QString& program, const QStringList& args);
	void runTool(const QString& source, const QString& program, const QStringList& args, Done, const QByteArray& input = QByteArray());
	QString pgTool(const QString& name) const;
	QString path(const QString& name) const;
	QString databaseAddress() const;   // without the password
	QString databasePassword() const;

	void stopLeftoverNakama();
	void stopLeftoverDatabase();
	void checkPorts();
	void initDatabase();
	void createRole();
	void launchPostgres();
	void waitForDatabase();
	void migrate();
	void launchNakama();
	void waitForNakama(int attempt);
	void stopNakama();
	void stopPostgres();
	void finishStop();

	QString m_id;
	State m_state = State::Stopped;
	QString m_error;
	ServerProfile m_profile;
	QString m_dataDir;
	Programs m_programs;
	QProcessEnvironment m_env;
	QFile m_logFile;
	std::unique_ptr<QLockFile> m_lock;   // one program runs a server at a time
	QStringList m_recent;
	QProcess* m_tool = nullptr;       // a short step: initdb, pg_ctl, pg_isready, migrate
	QProcess* m_postgres = nullptr;
	QProcess* m_nakama = nullptr;
	QNetworkAccessManager* m_http = nullptr;
	QTimer m_deadline;
	QElapsedTimer m_waitClock;
	bool m_stopping = false;
	int m_generation = 0;              // invalidates callbacks of an earlier start
};

Q_DECLARE_METATYPE(ServerHost::State)
