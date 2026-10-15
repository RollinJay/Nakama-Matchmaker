/* SPDX-License-Identifier: MIT */
#include "ServerManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QHostAddress>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>

namespace {

QString exe(const QString& name) {
#ifdef Q_OS_WIN
	return name + QStringLiteral(".exe");
#else
	return name;
#endif
}

bool isProgram(const QString& path) {
	QFileInfo info(path);
	return info.isFile() && info.isExecutable();
}

/* PostgreSQL's usual folders on each system, newest version first. */
QStringList postgresCandidates() {
	QStringList patterns;
#if defined(Q_OS_WIN)
	patterns << QStringLiteral("C:/Program Files/PostgreSQL/*/bin");
#elif defined(Q_OS_MACOS)
	patterns << QStringLiteral("/Applications/Postgres.app/Contents/Versions/*/bin") << QStringLiteral("/opt/homebrew/opt/postgresql@*/bin")
	         << QStringLiteral("/usr/local/opt/postgresql@*/bin") << QStringLiteral("/opt/homebrew/opt/postgresql/bin")
	         << QStringLiteral("/usr/local/opt/postgresql/bin");
#else
	patterns << QStringLiteral("/usr/lib/postgresql/*/bin") << QStringLiteral("/usr/pgsql-*/bin") << QStringLiteral("/usr/local/pgsql/bin");
#endif
	QStringList found;
	for (const QString& pattern : patterns) {
		int star = pattern.indexOf(QLatin1Char('*'));
		if (star < 0) {
			found.append(pattern);
			continue;
		}
		QString parent = pattern.left(pattern.lastIndexOf(QLatin1Char('/'), star));
		QString segment = pattern.mid(parent.size() + 1).section(QLatin1Char('/'), 0, 0);
		QString rest = pattern.mid(parent.size() + 1 + segment.size());
		for (const QString& entry : QDir(parent).entryList(QStringList { segment }, QDir::Dirs | QDir::NoDotAndDotDot)) {
			found.append(parent + QLatin1Char('/') + entry + rest);
		}
	}
	static const QRegularExpression number(QStringLiteral("(\\d+)(?:\\.(\\d+))?[^/]*/bin$"));
	auto version = [](const QString& path) {
		QRegularExpressionMatch m = number.match(path);
		return m.hasMatch() ? m.captured(1).toInt() * 1000 + m.captured(2).toInt() : 0;
	};
	std::stable_sort(found.begin(), found.end(), [&](const QString& a, const QString& b) { return version(a) > version(b); });
	return found;
}

}

ServerManager::ServerManager(const QString& folder, QObject* parent)
	: QObject(parent)
	, m_config(folder) {
}

ServerManager::~ServerManager() {
	stopAllAndWait(30000);
}

ServerHost::State ServerManager::state(const QString& id) const {
	ServerHost* h = m_hosts.value(id);
	return h ? h->state() : ServerHost::State::Stopped;
}

QStringList ServerManager::active() const {
	QStringList out;
	for (ServerHost* h : m_hosts) {
		if (h->active()) {
			out.append(h->id());
		}
	}
	out.sort();
	return out;
}

QString ServerManager::nakamaProgram() const {
	QDir folder(QFileInfo(m_config.path()).absolutePath());
	if (!m_config.nakamaPath.isEmpty()) {
		QString path = folder.absoluteFilePath(m_config.nakamaPath);
		return isProgram(path) ? path : QString();
	}
	QDir app(QCoreApplication::applicationDirPath());
	for (const QString& candidate : { app.filePath(exe(QStringLiteral("nakama"))), app.filePath(QStringLiteral("nakama/") + exe(QStringLiteral("nakama"))) }) {
		if (isProgram(candidate)) {
			return candidate;
		}
	}
	return QStandardPaths::findExecutable(QStringLiteral("nakama"));
}

QString ServerManager::postgresFolder(const ServerProfile* profile) const {
	QDir folder(QFileInfo(m_config.path()).absolutePath());
	if (!m_config.postgresPath.isEmpty()) {
		return folder.absoluteFilePath(m_config.postgresPath);
	}
	auto complete = [](const QString& dir) {
		return isProgram(QDir(dir).filePath(exe(QStringLiteral("initdb")))) && isProgram(QDir(dir).filePath(exe(QStringLiteral("postgres")))) &&
		       isProgram(QDir(dir).filePath(exe(QStringLiteral("pg_ctl"))));
	};
	QStringList found;
	for (const QString& candidate : postgresCandidates()) {
		if (complete(candidate)) {
			found.append(candidate);
		}
	}
	QString initdb = QStandardPaths::findExecutable(QStringLiteral("initdb"));
	if (!initdb.isEmpty() && complete(QFileInfo(initdb).absolutePath()) && !found.contains(QFileInfo(initdb).absolutePath())) {
		found.append(QFileInfo(initdb).absolutePath());
	}
	if (found.isEmpty()) {
		return QString();
	}
	// an existing database needs the PostgreSQL of its own major version
	if (profile && profile->managedDatabase()) {
		QFile file(QDir(m_config.dataDir(*profile)).filePath(QStringLiteral("postgres/PG_VERSION")));
		if (file.open(QIODevice::ReadOnly)) {
			int version = QString::fromLatin1(file.readAll()).trimmed().toInt();
			for (const QString& candidate : found) {
				if (ServerHost::postgresVersion(candidate) == version) {
					return candidate;
				}
			}
		}
	}
	return found.first();
}

QString ServerManager::modulesFolder() const {
	QDir folder(QFileInfo(m_config.path()).absolutePath());
	if (!m_config.modulesPath.isEmpty()) {
		return folder.absoluteFilePath(m_config.modulesPath);
	}
	QDir app(QCoreApplication::applicationDirPath());
	for (const QString& candidate : { app.filePath(QStringLiteral("server-modules")), app.filePath(QStringLiteral("../share/nakama-matchmaker/server-modules")),
	                                  app.filePath(QStringLiteral("../Resources/server-modules")) }) {
		if (QFileInfo::exists(QDir(candidate).filePath(QStringLiteral("community/community.lua")))) {
			return QDir::cleanPath(candidate);
		}
	}
	return QString();
}

QString ServerManager::runningConflict(const ServerProfile& profile) const {
	for (const QString& other : m_config.conflicts(profile)) {
		ServerHost* h = m_hosts.value(other);
		if (h && h->active()) {
			return tr("The server %1 is running and uses the same ports, folder or database.").arg(other);
		}
	}
	return QString();
}

bool ServerManager::start(const QString& id, QString* error) {
	const ServerProfile* profile = m_config.find(id);
	if (!profile) {
		*error = tr("There is no server %1.").arg(id);
		return false;
	}
	QString invalid = m_config.validate(*profile);
	if (!invalid.isEmpty()) {
		*error = invalid;
		return false;
	}
	ServerHost* h = m_hosts.value(id);
	if (h && h->active()) {
		*error = tr("The server %1 is already running.").arg(id);
		return false;
	}
	QString busy = runningConflict(*profile);
	if (!busy.isEmpty()) {
		*error = busy;
		return false;
	}
	ServerHost::Programs programs;
	programs.nakama = nakamaProgram();
	programs.postgres = profile->managedDatabase() ? postgresFolder(profile) : QString();
	programs.modules = modulesFolder();
	if (programs.modules.isEmpty()) {
		*error = tr("The server modules were not found next to this program; set their folder in the servers' settings.");
		return false;
	}
	if (!h) {
		h = new ServerHost(id, this);
		m_hosts.insert(id, h);
		connect(h, &ServerHost::stateChanged, this, [this, id](ServerHost::State state) { emit stateChanged(id, state); });
		connect(h, &ServerHost::logLine, this, [this, id](const QString& line) { emit logLine(id, line); });
	}
	return h->start(*profile, m_config.dataDir(*profile), programs, error);
}

void ServerManager::stop(const QString& id) {
	if (ServerHost* h = m_hosts.value(id)) {
		h->stop();
	}
}

void ServerManager::stopAll() {
	for (ServerHost* h : m_hosts) {
		h->stop();
	}
}

void ServerManager::stopAllAndWait(int msecs) {
	stopAll();
	for (ServerHost* h : m_hosts) {
		if (h->active()) {
			h->stopAndWait(msecs);
		}
	}
}

QStringList ServerManager::defaultStunServers() {
	// the launcher's own defaults (Settings::stunServers)
	return QStringList { QStringLiteral("stun.l.google.com:19302"), QStringLiteral("stun.cloudflare.com:3478") };
}

bool ServerManager::localOnly(const QString& host) {
	QHostAddress address(host);
	return host.isEmpty() || host == QLatin1String("localhost") || (!address.isNull() && address.isLoopback());
}

QByteArray ServerManager::ikemenProfile(const ServerProfile& profile, const IkemenProfileOptions& options) const {
	// IKEMEN GO finds its players' public addresses through the profile's STUN servers:
	// without them, only players on one network could reach each other
	QStringList stun = options.stun.isEmpty() ? defaultStunServers() : options.stun;
	QJsonObject server {
		{ QStringLiteral("id"), profile.id },
		{ QStringLiteral("name"), profile.title },
		{ QStringLiteral("host"), options.host.isEmpty() ? profile.clientHost() : options.host },
		{ QStringLiteral("port"), profile.clientPort() },
		{ QStringLiteral("server_key"), profile.serverKey },
		{ QStringLiteral("ssl"), profile.clientSsl() },
		{ QStringLiteral("status"), true },
		{ QStringLiteral("game"), options.game },
		{ QStringLiteral("build"), options.build },
		{ QStringLiteral("stun_servers"), QJsonArray::fromStringList(stun) },
	};
	if (!options.region.isEmpty()) {
		server.insert(QStringLiteral("region"), options.region);
	}
	QJsonObject root {
		{ QStringLiteral("version"), 1 },
		{ QStringLiteral("default"), profile.id },
		{ QStringLiteral("servers"), QJsonArray { server } },
	};
	return QJsonDocument(root).toJson();
}

QStringList ServerManager::connectionInfo(const ServerProfile& profile) const {
	QStringList lines;
	lines << tr("Server address: %1%2").arg(profile.clientHost(), localOnly(profile.clientHost()) ? tr(" (this computer: set the public address for players elsewhere)") : QString());
	lines << tr("Port: %1").arg(profile.clientPort());
	lines << tr("Server key: %1").arg(profile.serverKey);
	lines << tr("TLS: %1").arg(profile.clientSsl() ? tr("yes") : tr("no"));
	for (const QString& kind : profile.kinds) {
		if (kind == QLatin1String("ikemen")) {
			lines << tr("IKEMEN GO: a server profile in data/online/servers.json (the ikemen-profile command, or IKEMEN GO profile... in the window).");
		} else if (kind == QLatin1String("mgba")) {
			lines << tr("mGBA fork: Tools > Netplay..., or Nakama Matchmaker's settings.");
		} else if (kind == QLatin1String("melonds")) {
			lines << tr("melonDS fork: System > Multiplayer > Play online (rollback)... > Settings...");
		}
	}
	return lines;
}

bool ServerManager::runningProfile(const QString& id, ServerProfile* profile, QString* why) const {
	const ServerProfile* saved = m_config.find(id);
	if (!saved) {
		*why = tr("There is no server %1.").arg(id);
		return false;
	}
	ServerHost* h = m_hosts.value(id);
	if (h && h->active()) {
		if (h->state() != ServerHost::State::Running) {
			*why = tr("The server %1 is not running yet (%2).").arg(id, ServerHost::stateLabel(h->state()).toLower());
			return false;
		}
		*profile = h->profile();
		return true;
	}
	QString hostname;
	if (ServerHost::runningElsewhere(m_config.dataDir(*saved), nullptr, &hostname)) {
		if (!hostname.isEmpty() && hostname != QSysInfo::machineHostName()) {
			*why = tr("The server %1 is run by the computer %2 (its server.lock says so), not by this one.").arg(id, hostname);
			return false;
		}
		// another program of this computer: the settings it started the server with, which
		// servers.json may no longer hold (a change waits for the server's next start)
		*profile = *saved;
		if (!ServerConfig::readStarted(m_config.dataDir(*saved), profile)) {
			*why = tr("Cannot read the settings that the server %1 runs with (its nakama.yml in %2).")
			           .arg(id, QDir::toNativeSeparators(m_config.dataDir(*saved)));
			return false;
		}
		return true;
	}
	*why = tr("The server %1 is not running: start it first.").arg(id);
	return false;
}

void ServerManager::moderatorRpc(const QString& id, const QString& rpc, const QJsonObject& payload, QObject* context, RpcDone done) {
	QPointer<QObject> guard(context);
	auto finish = [guard, done](const QJsonObject& answer, const QString& error) {
		if (guard) {
			done(answer, error);
		}
	};
	ServerProfile s;
	QString why;
	const ServerProfile* saved = m_config.find(id);
	if (saved && !saved->serves(QStringLiteral("ikemen"))) {
		why = tr("The server %1 does not serve IKEMEN GO, so it has no ranked sets.").arg(id);
	} else if (runningProfile(id, &s, &why) && !s.serves(QStringLiteral("ikemen"))) {
		why = tr("The server %1 runs without IKEMEN GO's modules: restart it for its new games.").arg(id);
	}
	if (!why.isEmpty()) {
		QTimer::singleShot(0, this, [finish, why]() { finish(QJsonObject(), why); });
		return;
	}
	if (!m_http) {
		m_http = new QNetworkAccessManager(this);
		// straight to the server's own port on this computer, never through a proxy (such
		// as one that http_proxy names): the URL holds the server's HTTP key
		m_http->setProxy(QNetworkProxy::NoProxy);
	}
	bool tls = !s.tlsCertificate.isEmpty();
	QString host = s.bind;
	if (host.isEmpty() || host == QLatin1String("0.0.0.0") || host == QLatin1String("::")) {
		host = QStringLiteral("127.0.0.1");
	}
	QUrl url;
	url.setScheme(tls ? QStringLiteral("https") : QStringLiteral("http"));
	url.setHost(host);
	url.setPort(s.port);
	url.setPath(QStringLiteral("/v2/rpc/") + rpc);
	// Nakama takes the HTTP key as a query parameter; with unwrap, the body is the payload
	// itself and the answer is the RPC's own JSON
	QUrlQuery query;
	query.addQueryItem(QStringLiteral("http_key"), s.httpKey);
	query.addQueryItem(QStringLiteral("unwrap"), QString());
	url.setQuery(query);
	QNetworkRequest request(url);
	request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
	QNetworkReply* reply = m_http->post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
	if (tls) {
		reply->ignoreSslErrors();   // this computer's own server, whatever its certificate says
	}
	QTimer::singleShot(120000, reply, &QNetworkReply::abort);
	QString where = QStringLiteral("%1, port %2").arg(host).arg(s.port);
	connect(reply, &QNetworkReply::finished, this, [reply, finish, id, rpc, where]() {
		reply->deleteLater();
		QByteArray body = reply->readAll();
		int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
		QJsonDocument doc = QJsonDocument::fromJson(body);
		if (status == 200 && doc.isObject()) {
			finish(doc.object(), QString());
			return;
		}
		if (status == 0) {
			finish(QJsonObject(), tr("Cannot reach the server %1 at %2: %3").arg(id, where, reply->errorString()));
			return;
		}
		if (status == 200) {
			finish(QJsonObject(), tr("The server %1 answered %2 with something that is not a JSON object.").arg(id, rpc));
			return;
		}
		// Nakama's errors: {"code": <gRPC code>, "message": "..."}
		QJsonObject e = doc.object();
		int code = e.value(QStringLiteral("code")).toInt(-1);
		QString message = e.value(QStringLiteral("message")).toString();
		if (message.isEmpty()) {
			message = QString::fromUtf8(body.left(200)).trimmed();
		}
		if (status == 404 && message.contains(QLatin1String("RPC function not found"), Qt::CaseInsensitive)) {
			finish(QJsonObject(), tr("The server %1 has no %2 RPC: it runs IKEMEN GO modules without the ranked sets' moderation. "
			                         "Restart it with the modules of this version of Nakama Matchmaker (or newer ones of your own).").arg(id, rpc));
			return;
		}
		if (status == 401) {
			finish(QJsonObject(), tr("The server %1 refused its HTTP key: it runs with another one than servers.json holds (restart it).").arg(id));
			return;
		}
		finish(QJsonObject(), code >= 0 ? tr("The server %1 refused: %2 (code %3)").arg(id, message).arg(code)
		                                : tr("The server %1 refused: %2 (HTTP status %3)").arg(id, message).arg(status));
	});
}
