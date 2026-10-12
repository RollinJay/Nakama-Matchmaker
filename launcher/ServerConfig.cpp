/* SPDX-License-Identifier: MIT */
#include "ServerConfig.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QTcpServer>
#include <QUrl>

namespace {

const char* const KINDS[] = { "ikemen", "mgba", "melonds" };

/* An external database as host:port/database, to tell when two servers share one. */
QString databaseKey(const ServerProfile& s) {
	if (s.managedDatabase()) {
		return QString();
	}
	QString text = s.database;
	if (!text.startsWith(QLatin1String("postgres://")) && !text.startsWith(QLatin1String("postgresql://"))) {
		text = QStringLiteral("postgres://") + text;
	}
	QUrl url(text);
	QString name = url.path().mid(1);
	return QStringLiteral("%1:%2/%3").arg(url.host().toLower()).arg(url.port(5432)).arg(name.isEmpty() ? QStringLiteral("nakama") : name);
}

bool portFree(const QHostAddress& address, int port) {
	QTcpServer probe;
	return probe.listen(address, quint16(port));
}

QStringList toList(const QJsonValue& v) {
	QStringList out;
	for (const QJsonValue& item : v.toArray()) {
		out.append(item.toString());
	}
	return out;
}

/* A YAML double-quoted scalar. */
QByteArray yamlString(const QString& text) {
	QByteArray out = "\"";
	for (char c : text.toUtf8()) {
		unsigned char u = static_cast<unsigned char>(c);
		if (c == '"' || c == '\\') {
			out += '\\';
			out += c;
		} else if (c == '\n') {
			out += "\\n";
		} else if (c == '\t') {
			out += "\\t";
		} else if (u < 0x20 || u == 0x7F) {
			out += QByteArray("\\x") + QByteArray::number(u, 16).rightJustified(2, '0');
		} else {
			out += c;
		}
	}
	out += '"';
	return out;
}

}

/* The temporary file gets the permissions before anything is written to it, and replaces
 * the old file in one step. */
bool ServerConfig::writePrivate(const QString& path, const QByteArray& data, QString* error) {
	QSaveFile file(path);
	if (!file.open(QIODevice::WriteOnly)) {
		*error = QStringLiteral("%1: %2").arg(path, file.errorString());
		return false;
	}
	file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
	file.write(data);
	if (!file.commit()) {
		*error = QStringLiteral("%1: %2").arg(path, file.errorString());
		return false;
	}
	return true;
}

QList<QPair<int, QString>> ServerProfile::ports() const {
	QList<QPair<int, QString>> out {
		{ port - 1, QStringLiteral("API (gRPC)") },
		{ port, QStringLiteral("API") },
		{ consolePort - 3, QStringLiteral("console (gRPC)") },
		{ consolePort, QStringLiteral("console") },
	};
	if (managedDatabase()) {
		out.append({ databasePort, QStringLiteral("database") });
	}
	return out;
}

QString ServerProfile::kindsLabel() const {
	QStringList labels;
	for (const QString& kind : kinds) {
		labels.append(ServerConfig::kindLabel(kind));
	}
	return labels.join(QStringLiteral(", "));
}

QString ServerProfile::clientHost() const {
	if (!publicHost.isEmpty()) {
		return publicHost;
	}
	if (!bind.isEmpty() && bind != QLatin1String("0.0.0.0") && bind != QLatin1String("::")) {
		return bind;
	}
	return QStringLiteral("127.0.0.1");
}

ServerConfig::ServerConfig(const QString& folder)
	: m_folder(folder) {
}

QString ServerConfig::path() const {
	return QDir(m_folder).filePath(QStringLiteral("servers.json"));
}

bool ServerConfig::load(QString* error) {
	m_servers.clear();
	m_filled = false;
	QFile file(path());
	if (!file.exists()) {
		return true;
	}
	if (!file.open(QIODevice::ReadOnly)) {
		*error = QStringLiteral("%1: %2").arg(path(), file.errorString());
		return false;
	}
	QJsonParseError parseError;
	QJsonObject root = QJsonDocument::fromJson(file.readAll(), &parseError).object();
	if (parseError.error != QJsonParseError::NoError) {
		*error = QStringLiteral("%1: %2").arg(path(), parseError.errorString());
		return false;
	}
	nakamaPath = root.value(QStringLiteral("nakama")).toString();
	postgresPath = root.value(QStringLiteral("postgres")).toString();
	modulesPath = root.value(QStringLiteral("modules")).toString();
	for (const QJsonValue& v : root.value(QStringLiteral("servers")).toArray()) {
		QJsonObject o = v.toObject();
		ServerProfile s;
		s.id = o.value(QStringLiteral("id")).toString();
		s.title = o.value(QStringLiteral("title")).toString(s.id);
		s.message = o.value(QStringLiteral("message")).toString();
		s.kinds = toList(o.value(QStringLiteral("kinds")));
		s.port = o.value(QStringLiteral("port")).toInt(7350);
		s.consolePort = o.value(QStringLiteral("console_port")).toInt(s.port + 1);
		s.consolePublic = o.value(QStringLiteral("console_public")).toBool(false);
		s.bind = o.value(QStringLiteral("bind")).toString();
		s.publicHost = o.value(QStringLiteral("public_host")).toString();
		s.publicPort = o.value(QStringLiteral("public_port")).toInt(0);
		s.clientTls = o.value(QStringLiteral("client_tls")).toBool(false);
		s.database = o.value(QStringLiteral("database")).toString();
		s.databasePort = o.value(QStringLiteral("database_port")).toInt(54330);
		s.serverKey = o.value(QStringLiteral("server_key")).toString();
		s.httpKey = o.value(QStringLiteral("http_key")).toString();
		s.sessionKey = o.value(QStringLiteral("session_key")).toString();
		s.refreshKey = o.value(QStringLiteral("refresh_key")).toString();
		s.consoleUser = o.value(QStringLiteral("console_user")).toString();
		s.consolePassword = o.value(QStringLiteral("console_password")).toString();
		s.consoleSigningKey = o.value(QStringLiteral("console_signing_key")).toString();
		s.mfaKey = o.value(QStringLiteral("mfa_key")).toString();
		s.databasePassword = o.value(QStringLiteral("database_password")).toString();
		s.tlsCertificate = o.value(QStringLiteral("tls_certificate")).toString();
		s.tlsPrivateKey = o.value(QStringLiteral("tls_private_key")).toString();
		s.extraModules = o.value(QStringLiteral("extra_modules")).toString();
		s.extraArgs = toList(o.value(QStringLiteral("extra_args")));
		s.dataDir = o.value(QStringLiteral("data_dir")).toString();
		if (s.id.isEmpty()) {
			continue;
		}
		// a file written by hand, or by an earlier version: the missing secrets are made now
		for (QString* secret : { &s.serverKey, &s.httpKey, &s.sessionKey, &s.refreshKey, &s.consolePassword, &s.consoleSigningKey, &s.mfaKey }) {
			if (secret->isEmpty()) {
				*secret = ServerConfig::secret();
				m_filled = true;
			}
		}
		if (s.consoleUser.isEmpty()) {
			s.consoleUser = QStringLiteral("operator");
			m_filled = true;
		}
		// a server's own database gets its password when it is created, so a missing one
		// can be made only while there is no database yet
		if (s.managedDatabase() && s.databasePassword.isEmpty() && !QFileInfo::exists(QDir(dataDir(s)).filePath(QStringLiteral("postgres")))) {
			s.databasePassword = ServerConfig::secret();
			m_filled = true;
		}
		m_servers.append(s);
	}
	return true;
}

bool ServerConfig::save(QString* error) const {
	QJsonArray servers;
	for (const ServerProfile& s : m_servers) {
		QJsonObject o {
			{ QStringLiteral("id"), s.id },
			{ QStringLiteral("title"), s.title },
			{ QStringLiteral("message"), s.message },
			{ QStringLiteral("kinds"), QJsonArray::fromStringList(s.kinds) },
			{ QStringLiteral("port"), s.port },
			{ QStringLiteral("console_port"), s.consolePort },
			{ QStringLiteral("console_public"), s.consolePublic },
			{ QStringLiteral("bind"), s.bind },
			{ QStringLiteral("public_host"), s.publicHost },
			{ QStringLiteral("public_port"), s.publicPort },
			{ QStringLiteral("client_tls"), s.clientTls },
			{ QStringLiteral("database"), s.database },
			{ QStringLiteral("database_port"), s.databasePort },
			{ QStringLiteral("server_key"), s.serverKey },
			{ QStringLiteral("http_key"), s.httpKey },
			{ QStringLiteral("session_key"), s.sessionKey },
			{ QStringLiteral("refresh_key"), s.refreshKey },
			{ QStringLiteral("console_user"), s.consoleUser },
			{ QStringLiteral("console_password"), s.consolePassword },
			{ QStringLiteral("console_signing_key"), s.consoleSigningKey },
			{ QStringLiteral("mfa_key"), s.mfaKey },
			{ QStringLiteral("database_password"), s.databasePassword },
			{ QStringLiteral("tls_certificate"), s.tlsCertificate },
			{ QStringLiteral("tls_private_key"), s.tlsPrivateKey },
			{ QStringLiteral("extra_modules"), s.extraModules },
			{ QStringLiteral("extra_args"), QJsonArray::fromStringList(s.extraArgs) },
			{ QStringLiteral("data_dir"), s.dataDir },
		};
		servers.append(o);
	}
	QJsonObject root {
		{ QStringLiteral("version"), 1 },
		{ QStringLiteral("nakama"), nakamaPath },
		{ QStringLiteral("postgres"), postgresPath },
		{ QStringLiteral("modules"), modulesPath },
		{ QStringLiteral("servers"), servers },
	};
	QDir().mkpath(m_folder);
	return writePrivate(path(), QJsonDocument(root).toJson(), error);
}

const ServerProfile* ServerConfig::find(const QString& id) const {
	for (const ServerProfile& s : m_servers) {
		if (s.id == id) {
			return &s;
		}
	}
	return nullptr;
}

void ServerConfig::put(const ServerProfile& server) {
	for (ServerProfile& s : m_servers) {
		if (s.id == server.id) {
			s = server;
			return;
		}
	}
	m_servers.append(server);
}

bool ServerConfig::remove(const QString& id) {
	for (int i = 0; i < m_servers.size(); ++i) {
		if (m_servers[i].id == id) {
			m_servers.removeAt(i);
			return true;
		}
	}
	return false;
}

QString ServerConfig::secret() {
	QByteArray raw(16, Qt::Uninitialized);
	for (int i = 0; i < raw.size(); ++i) {
		raw[i] = char(QRandomGenerator::system()->bounded(256));
	}
	return QString::fromLatin1(raw.toHex());
}

ServerProfile ServerConfig::create(const QString& id, const QStringList& kinds) const {
	ServerProfile s;
	s.id = id;
	s.title = id;
	s.kinds = kinds;
	s.serverKey = secret();
	s.httpKey = secret();
	s.sessionKey = secret();
	s.refreshKey = secret();
	s.consoleUser = QStringLiteral("operator");
	s.consolePassword = secret();
	s.consoleSigningKey = secret();
	s.mfaKey = secret();
	s.databasePassword = secret();
	// the first block of ports that no other server uses and no program holds now: 7350,
	// 7360, ... and, for the database, 54330, 54331, ... (5432 and the ports after it are
	// where PostgreSQL installations put their own clusters)
	QSet<int> used;
	for (const ServerProfile& other : m_servers) {
		for (const auto& p : other.ports()) {
			used.insert(p.first);
		}
	}
	for (int block = 0; block < 100; ++block) {
		s.port = 7350 + 10 * block;
		s.consolePort = s.port + 1;
		s.databasePort = 54330 + block;
		bool free = true;
		for (const auto& p : s.ports()) {
			QHostAddress address = p.second == QLatin1String("database") || p.second.startsWith(QLatin1String("console"))
			                           ? QHostAddress(QHostAddress::LocalHost)
			                           : QHostAddress(QHostAddress::Any);
			free = free && !used.contains(p.first) && portFree(address, p.first);
		}
		if (free) {
			break;
		}
	}
	return s;
}

bool ServerConfig::validId(const QString& id) {
	static const QRegularExpression pattern(QStringLiteral("^[a-z0-9]([a-z0-9-]{0,15})$"));
	return id.size() <= 16 && pattern.match(id).hasMatch();
}

QString ServerConfig::validate(const ServerProfile& s) const {
	if (!validId(s.id)) {
		return QStringLiteral("The id \"%1\" is not valid: use 1 to 16 lowercase letters, digits and dashes, starting with a letter or digit.").arg(s.id);
	}
	if (s.kinds.isEmpty()) {
		return QStringLiteral("Choose at least one kind of game for the server.");
	}
	for (const QString& kind : s.kinds) {
		if (kindFromName(kind) != kind) {
			return QStringLiteral("Unknown kind of game: %1.").arg(kind);
		}
	}
	if (s.port < 2 || s.port > 65535) {
		return QStringLiteral("The port must be between 2 and 65535.");
	}
	if (s.consolePort < 4 || s.consolePort > 65535) {
		return QStringLiteral("The console port must be between 4 and 65535.");
	}
	if (s.publicPort < 0 || s.publicPort > 65535) {
		return QStringLiteral("The public port must be between 1 and 65535 (or 0: the server's port).");
	}
	if (s.managedDatabase() && (s.databasePort < 1 || s.databasePort > 65535)) {
		return QStringLiteral("The database port must be between 1 and 65535.");
	}
	QList<QPair<int, QString>> ports = s.ports();
	for (int i = 0; i < ports.size(); ++i) {
		for (int j = i + 1; j < ports.size(); ++j) {
			if (ports[i].first == ports[j].first) {
				return QStringLiteral("The %1 and the %2 would both use port %3 (the API also takes the port before its own, and the console the port 3 below its own).")
				    .arg(ports[i].second, ports[j].second)
				    .arg(ports[i].first);
			}
		}
	}
	QStringList keys { s.serverKey, s.httpKey, s.sessionKey, s.refreshKey, s.consoleSigningKey, s.mfaKey };
	for (const QString& key : keys) {
		if (key.isEmpty()) {
			return QStringLiteral("A key of the server is empty.");
		}
	}
	QSet<QString> distinct;
	for (const QString& key : keys) {
		distinct.insert(key);
	}
	if (distinct.size() != keys.size()) {
		return QStringLiteral("The server's keys must all differ.");
	}
	if (s.mfaKey.size() != 32) {
		return QStringLiteral("The MFA key must have 32 characters.");
	}
	if (s.consoleUser.isEmpty() || s.consolePassword.isEmpty()) {
		return QStringLiteral("The console needs a user name and a password.");
	}
	if (s.managedDatabase() && s.databasePassword.isEmpty()) {
		return QStringLiteral("The database needs a password.");
	}
	if (!s.managedDatabase()) {
		QUrl url(QStringLiteral("postgres://") + s.database);
		if (!url.isValid() || url.host().isEmpty()) {
			return QStringLiteral("The database address is not valid; it looks like user:password@host:port/database.");
		}
	}
	QString folderProblem = checkDataDir(dataDir(s));
	if (!folderProblem.isEmpty()) {
		return folderProblem;
	}
	if (!s.extraModules.isEmpty()) {
		QString modules = QDir::cleanPath(QDir(m_folder).absoluteFilePath(s.extraModules));
		QString data = QDir::cleanPath(dataDir(s));
		if (modules == data || modules.startsWith(data + QLatin1Char('/'))) {
			return QStringLiteral("Keep your own modules outside the server's folder: its modules folder is rebuilt at every start.");
		}
	}
	if (s.tlsCertificate.isEmpty() != s.tlsPrivateKey.isEmpty()) {
		return QStringLiteral("TLS needs both a certificate and its private key.");
	}
	if (s.title.contains(QLatin1Char('\n')) || s.title.size() > 64) {
		return QStringLiteral("The title must be one line of at most 64 characters.");
	}
	if (s.message.size() > 500) {
		return QStringLiteral("The message must be at most 500 characters.");
	}
	return QString();
}

QStringList ServerConfig::conflicts(const ServerProfile& s) const {
	QStringList out;
	QSet<int> mine;
	for (const auto& p : s.ports()) {
		mine.insert(p.first);
	}
	QString myData = QDir::cleanPath(dataDir(s));
	QString myDatabase = databaseKey(s);
	for (const ServerProfile& other : m_servers) {
		if (other.id == s.id) {
			continue;
		}
		// two Nakama servers on one database share its accounts and records, but not their
		// matches, so the records of one name matches the other cannot find
		bool shared = QDir::cleanPath(dataDir(other)) == myData || (!myDatabase.isEmpty() && databaseKey(other) == myDatabase);
		for (const auto& p : other.ports()) {
			shared = shared || mine.contains(p.first);
		}
		if (shared) {
			out.append(other.id);
		}
	}
	return out;
}

QString ServerConfig::dataDir(const ServerProfile& s) const {
	if (s.dataDir.isEmpty()) {
		return QDir(m_folder).absoluteFilePath(QStringLiteral("servers/") + s.id);
	}
	return QDir(m_folder).absoluteFilePath(s.dataDir);
}

QString ServerConfig::checkDataDir(const QString& folder) const {
	QString path = QDir::cleanPath(QDir(m_folder).absoluteFilePath(folder));
	QString home = QDir::cleanPath(QDir::homePath());
	if (QDir(path).isRoot() || path == home || path == QDir::cleanPath(QDir(m_folder).absolutePath()) ||
	    path == QDir::cleanPath(QDir(m_folder).absoluteFilePath(QStringLiteral("servers")))) {
		return QStringLiteral("%1 cannot be a server's folder: give each server a folder of its own.").arg(QDir::toNativeSeparators(path));
	}
	for (QStandardPaths::StandardLocation location : { QStandardPaths::DesktopLocation, QStandardPaths::DocumentsLocation, QStandardPaths::DownloadLocation }) {
		for (const QString& p : QStandardPaths::standardLocations(location)) {
			if (QDir::cleanPath(p) == path) {
				return QStringLiteral("%1 cannot be a server's folder: give each server a folder of its own.").arg(QDir::toNativeSeparators(path));
			}
		}
	}
	return QString();
}

QString ServerConfig::kindFromName(const QString& name) {
	QString n = name.trimmed().toLower();
	if (n == QLatin1String("ikemen")) {
		return n;
	}
	if (n == QLatin1String("gba") || n == QLatin1String("mgba")) {
		return QStringLiteral("mgba");
	}
	if (n == QLatin1String("ds") || n == QLatin1String("melonds")) {
		return QStringLiteral("melonds");
	}
	return QString();
}

QStringList ServerConfig::parseKinds(const QString& list, QString* error) {
	QStringList out;
	for (const QString& part : list.split(QLatin1Char(','))) {
		if (part.trimmed().isEmpty()) {
			continue;
		}
		if (part.trimmed().toLower() == QLatin1String("all")) {
			for (const char* kind : KINDS) {
				if (!out.contains(QLatin1String(kind))) {
					out.append(QLatin1String(kind));
				}
			}
			continue;
		}
		QString kind = kindFromName(part);
		if (kind.isEmpty()) {
			*error = QStringLiteral("unknown kind \"%1\" (ikemen, gba, ds or all)").arg(part.trimmed());
			return QStringList();
		}
		if (!out.contains(kind)) {
			out.append(kind);
		}
	}
	if (out.isEmpty()) {
		*error = QStringLiteral("no kind of game given (ikemen, gba, ds or all)");
	}
	return out;
}

QString ServerConfig::kindLabel(const QString& kind) {
	if (kind == QLatin1String("mgba")) {
		return QStringLiteral("GBA");
	}
	if (kind == QLatin1String("melonds")) {
		return QStringLiteral("DS");
	}
	if (kind == QLatin1String("ikemen")) {
		return QStringLiteral("IKEMEN");
	}
	return kind;
}

QByteArray ServerConfig::nakamaYaml(const ServerProfile& s, const QString& dataDir) {
	QDir dir(dataDir);
	QByteArray y;
	y += "# Nakama's configuration for the community server \"" + s.id.toUtf8() + "\", written by\n";
	y += "# Nakama Matchmaker every time it starts the server: change the server there, not here.\n";
	y += "name: " + yamlString(s.id) + "\n";
	y += "data_dir: " + yamlString(dir.filePath(QStringLiteral("nakama"))) + "\n";
	y += "logger:\n";
	y += "  level: \"INFO\"\n";
	y += "  stdout: true\n";
	y += "runtime:\n";
	y += "  path: " + yamlString(dir.filePath(QStringLiteral("modules"))) + "\n";
	y += "  http_key: " + yamlString(s.httpKey) + "\n";
	y += "  env:\n";
	y += "    - " + yamlString(QStringLiteral("NMM_TITLE=") + s.title) + "\n";
	y += "    - " + yamlString(QStringLiteral("NMM_MESSAGE=") + s.message) + "\n";
	y += "    - " + yamlString(QStringLiteral("NMM_KINDS=") + s.kinds.join(QLatin1Char(','))) + "\n";
	y += "socket:\n";
	y += "  server_key: " + yamlString(s.serverKey) + "\n";
	y += "  port: " + QByteArray::number(s.port) + "\n";
	if (!s.bind.isEmpty()) {
		y += "  address: " + yamlString(s.bind) + "\n";
	}
	// IKEMEN GO's replay stream headers and melonDS's signals exceed Nakama's 4096-byte default
	y += "  max_message_size_bytes: 65536\n";
	if (!s.tlsCertificate.isEmpty()) {
		y += "  ssl_certificate: " + yamlString(s.tlsCertificate) + "\n";
		y += "  ssl_private_key: " + yamlString(s.tlsPrivateKey) + "\n";
	}
	y += "session:\n";
	y += "  encryption_key: " + yamlString(s.sessionKey) + "\n";
	y += "  refresh_encryption_key: " + yamlString(s.refreshKey) + "\n";
	y += "mfa:\n";
	y += "  storage_encryption_key: " + yamlString(s.mfaKey) + "\n";
	y += "console:\n";
	y += "  port: " + QByteArray::number(s.consolePort) + "\n";
	if (!s.consolePublic) {
		y += "  address: \"127.0.0.1\"\n";
	}
	y += "  username: " + yamlString(s.consoleUser) + "\n";
	y += "  password: " + yamlString(s.consolePassword) + "\n";
	y += "  signing_key: " + yamlString(s.consoleSigningKey) + "\n";
	y += "  mfa:\n";
	y += "    storage_encryption_key: " + yamlString(s.mfaKey) + "\n";
	if (s.serves(QStringLiteral("ikemen"))) {
		// both tickets' rating windows apply in IKEMEN GO's ranked matchmaking
		y += "matchmaker:\n";
		y += "  rev_precision: true\n";
	}
	return y;
}
