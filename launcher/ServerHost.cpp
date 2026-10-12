/* SPDX-License-Identifier: MIT */
#include "ServerHost.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTcpServer>
#include <QUrl>

#include <memory>

#ifdef Q_OS_UNIX
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#endif
#ifdef Q_OS_LINUX
#include <sys/prctl.h>
#endif
#ifdef Q_OS_MACOS
#include <libproc.h>
#endif
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {

const qint64 LOG_ROTATE_BYTES = 10 * 1024 * 1024;
const int RECENT_LINES = 1000;
const char* const MATCHMAKER = "matchmaker";

QString exe(const QString& name) {
#ifdef Q_OS_WIN
	return name + QStringLiteral(".exe");
#else
	return name;
#endif
}

bool processAlive(qint64 pid) {
	if (pid <= 0) {
		return false;
	}
#ifdef Q_OS_WIN
	HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, DWORD(pid));
	if (!h) {
		return false;
	}
	DWORD code = 0;
	bool alive = GetExitCodeProcess(h, &code) && code == STILL_ACTIVE;
	CloseHandle(h);
	return alive;
#else
	return ::kill(pid_t(pid), 0) == 0 || errno == EPERM;
#endif
}

/* The program a process runs, as a canonical path; empty when it cannot be known. */
QString processImage(qint64 pid) {
#if defined(Q_OS_LINUX)
	return QFileInfo(QStringLiteral("/proc/%1/exe").arg(pid)).canonicalFilePath();
#elif defined(Q_OS_MACOS)
	char path[PROC_PIDPATHINFO_MAXSIZE];
	if (proc_pidpath(int(pid), path, sizeof(path)) <= 0) {
		return QString();
	}
	return QFileInfo(QString::fromLocal8Bit(path)).canonicalFilePath();
#elif defined(Q_OS_WIN)
	HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, DWORD(pid));
	if (!h) {
		return QString();
	}
	wchar_t buffer[4096];
	DWORD size = sizeof(buffer) / sizeof(buffer[0]);
	QString image;
	if (QueryFullProcessImageNameW(h, 0, buffer, &size)) {
		image = QFileInfo(QString::fromWCharArray(buffer, int(size))).canonicalFilePath();
	}
	CloseHandle(h);
	return image;
#else
	Q_UNUSED(pid);
	return QString();
#endif
}

void signalProcess(qint64 pid, bool force) {
#ifdef Q_OS_WIN
	Q_UNUSED(force);
	HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, DWORD(pid));
	if (h) {
		TerminateProcess(h, 1);
		CloseHandle(h);
	}
#else
	::kill(pid_t(pid), force ? SIGKILL : SIGTERM);
#endif
}

#ifdef Q_OS_WIN
/* PostgreSQL refuses to run with administrative rights (only initdb and pg_ctl drop them). */
bool elevated() {
	HANDLE token = nullptr;
	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
		return false;
	}
	TOKEN_ELEVATION elevation = {};
	DWORD size = 0;
	bool result = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size) && elevation.TokenIsElevated;
	CloseHandle(token);
	return result;
}

/* A job that ends the servers' programs when this program ends, however it ends. */
HANDLE childJob() {
	static HANDLE job = []() {
		HANDLE j = CreateJobObjectW(nullptr, nullptr);
		if (j) {
			JOBOBJECT_EXTENDED_LIMIT_INFORMATION info = {};
			info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
			SetInformationJobObject(j, JobObjectExtendedLimitInformation, &info, sizeof(info));
		}
		return j;
	}();
	return job;
}
#endif

#ifdef Q_OS_UNIX
/* In the child, before the program starts: a process group of its own, so that a Ctrl+C in
 * the terminal reaches only the matchmaker, which then stops the server in order; and, on
 * Linux, the end of the program when the matchmaker ends, even when it is killed. */
void prepareChild() {
	::setpgid(0, 0);
#ifdef Q_OS_LINUX
	::prctl(PR_SET_PDEATHSIG, SIGTERM);
	if (::getppid() == 1) {
		::_exit(1);   // the matchmaker already ended
	}
#endif
}
#endif

class ServerProcess : public QProcess {
public:
	explicit ServerProcess(QObject* parent)
		: QProcess(parent) {
#if defined(Q_OS_UNIX) && QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
		setChildProcessModifier(prepareChild);
#endif
#ifdef Q_OS_WIN
		setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args) { args->flags |= CREATE_NEW_PROCESS_GROUP; });
		connect(this, &QProcess::started, this, [this]() {
			HANDLE h = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE, DWORD(processId()));
			if (h) {
				AssignProcessToJobObject(childJob(), h);
				CloseHandle(h);
			}
		});
#endif
	}

#if defined(Q_OS_UNIX) && QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
protected:
	void setupChildProcess() override {
		prepareChild();
	}
#endif
};

QUrl databaseUrl(const QString& address) {
	QString text = address;
	if (!text.startsWith(QLatin1String("postgres://")) && !text.startsWith(QLatin1String("postgresql://"))) {
		text = QStringLiteral("postgres://") + text;
	}
	return QUrl(text);
}

/* What a Lua file registers with Nakama: "rpc:<id>", "rt_before:<message>",
 * "matchmaker_matched", ... Nakama keeps one handler for each, the last one loaded. */
QStringList registrations(const QString& file) {
	QFile f(file);
	if (!f.open(QIODevice::ReadOnly)) {
		return QStringList();
	}
	static const QRegularExpression call(QStringLiteral("register_(rpc|rt_before|rt_after|req_before|req_after|matchmaker_matched|matchmaker_override|"
	                                                    "tournament_end|tournament_reset|leaderboard_reset|shutdown)\\s*\\(([^\\n]*)"));
	static const QRegularExpression literal(QStringLiteral("[\"']([^\"']+)[\"']"));
	QStringList out;
	QString text = QString::fromUtf8(f.readAll());
	auto it = call.globalMatch(text);
	while (it.hasNext()) {
		QRegularExpressionMatch m = it.next();
		QString kind = m.captured(1);
		// the id or message name is the call's last argument
		QString name;
		auto names = literal.globalMatch(m.captured(2));
		while (names.hasNext()) {
			name = names.next().captured(1);
		}
		bool named = kind != QLatin1String("matchmaker_matched") && kind != QLatin1String("matchmaker_override") && kind != QLatin1String("shutdown");
		out.append(named && !name.isEmpty() ? kind + QLatin1Char(':') + name : kind);
	}
	return out;
}

bool inside(const QString& path, const QString& folder) {
	QString p = QFileInfo(path).canonicalFilePath();
	QString f = QFileInfo(folder).canonicalFilePath();
	return !p.isEmpty() && !f.isEmpty() && (p == f || p.startsWith(f + QLatin1Char('/')));
}

}

ServerHost::ServerHost(const QString& id, QObject* parent)
	: QObject(parent)
	, m_id(id) {
	qRegisterMetaType<ServerHost::State>();
	m_deadline.setSingleShot(true);
	connect(&m_deadline, &QTimer::timeout, this, [this]() {
		// a program that did not stop when asked
		if (m_nakama && m_nakama->state() != QProcess::NotRunning) {
			log(QLatin1String(MATCHMAKER), tr("Nakama did not stop in time; ending it."));
			m_nakama->kill();
		} else if (m_postgres && m_postgres->state() != QProcess::NotRunning) {
			log(QLatin1String(MATCHMAKER), tr("The database did not stop in time; ending it."));
			m_postgres->kill();
		}
	});
	m_http = new QNetworkAccessManager(this);
}

ServerHost::~ServerHost() {
	if (active()) {
		stopAndWait(30000);
	}
}

QString ServerHost::markerName() {
	return QStringLiteral(".nakama-matchmaker-server");
}

QString ServerHost::stateLabel(State state) {
	switch (state) {
	case State::Stopped:
		return tr("Stopped");
	case State::Starting:
		return tr("Starting");
	case State::Running:
		return tr("Running");
	case State::Stopping:
		return tr("Stopping");
	case State::Failed:
		return tr("Failed");
	}
	return QString();
}

void ServerHost::setState(State state) {
	if (m_state != state) {
		m_state = state;
		emit stateChanged(state);
	}
}

QString ServerHost::formatLine(const QString& source, const QString& line) {
	if (source == QLatin1String("nakama") && line.startsWith(QLatin1Char('{'))) {
		QJsonParseError error;
		QJsonObject o = QJsonDocument::fromJson(line.toUtf8(), &error).object();
		if (error.error == QJsonParseError::NoError && o.contains(QStringLiteral("msg"))) {
			QStringList parts { source, o.value(QStringLiteral("level")).toString(), o.value(QStringLiteral("msg")).toString() };
			for (auto it = o.constBegin(); it != o.constEnd(); ++it) {
				if (it.key() == QLatin1String("level") || it.key() == QLatin1String("ts") || it.key() == QLatin1String("caller") ||
				    it.key() == QLatin1String("msg")) {
					continue;
				}
				QString value;
				if (it.value().isString()) {
					value = it.value().toString();
				} else if (it.value().isObject()) {
					value = QString::fromUtf8(QJsonDocument(it.value().toObject()).toJson(QJsonDocument::Compact));
				} else if (it.value().isArray()) {
					value = QString::fromUtf8(QJsonDocument(it.value().toArray()).toJson(QJsonDocument::Compact));
				} else if (it.value().isBool()) {
					value = it.value().toBool() ? QStringLiteral("true") : QStringLiteral("false");
				} else {
					value = QString::number(it.value().toDouble(), 'g', 15);
				}
				parts.append(it.key() + QLatin1Char('=') + value);
			}
			return parts.join(QLatin1Char(' '));
		}
	}
	return source + QLatin1Char(' ') + line;
}

void ServerHost::log(const QString& source, const QString& line) {
	QString text = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss ")) + formatLine(source, line);
	if (m_logFile.isOpen()) {
		if (m_logFile.size() > LOG_ROTATE_BYTES) {
			QString name = m_logFile.fileName();
			m_logFile.close();
			QFile::remove(name + QStringLiteral(".1"));
			QFile::rename(name, name + QStringLiteral(".1"));
			m_logFile.open(QIODevice::WriteOnly | QIODevice::Append);
			m_logFile.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
		}
		m_logFile.write(text.toUtf8() + '\n');
		m_logFile.flush();
	}
	m_recent.append(text);
	if (m_recent.size() > RECENT_LINES) {
		m_recent.removeFirst();
	}
	emit logLine(text);
}

QStringList ServerHost::moduleSets(const ServerProfile& profile) {
	QStringList sets { QStringLiteral("community") };
	// The GBA games' two-player matches are the IKEMEN GO modules' sessions (docs/server.md)
	if (profile.serves(QStringLiteral("ikemen")) || profile.serves(QStringLiteral("mgba"))) {
		sets.append(QStringLiteral("ikemen"));
	}
	if (profile.serves(QStringLiteral("melonds"))) {
		sets.append(QStringLiteral("melonds"));
	}
	return sets;
}

bool ServerHost::installModules(const ServerProfile& profile, const QString& bundled, const QString& target, QStringList* notes, QString* error) {
	// built beside the old folder, which it replaces only once every file is there
	QString building = target + QStringLiteral(".new");
	QDir(building).removeRecursively();
	if (!QDir().mkpath(building)) {
		*error = tr("Cannot create %1.").arg(QDir::toNativeSeparators(building));
		return false;
	}
	QDir out(building);
	// Nakama loads every module of the folder and names a module after its file, so the
	// sets go side by side in one folder
	QHash<QString, QString> from;
	QHash<QString, QString> registered;   // registration -> bundled file
	for (const QString& set : moduleSets(profile)) {
		QDir dir(QDir(bundled).filePath(set));
		QStringList files = dir.entryList(QStringList { QStringLiteral("*.lua") }, QDir::Files, QDir::Name);
		if (!dir.exists() || files.isEmpty()) {
			*error = tr("The server modules are missing: %1 holds no Lua modules.").arg(QDir::toNativeSeparators(dir.path()));
			return false;
		}
		for (const QString& file : files) {
			if (from.contains(file)) {
				*error = tr("Two bundled server modules are named %1.").arg(file);
				return false;
			}
			from.insert(file, set);
			for (const QString& r : registrations(dir.filePath(file))) {
				registered.insert(r, file);
			}
			if (!QFile::copy(dir.filePath(file), out.filePath(file))) {
				*error = tr("Cannot copy %1 into %2.").arg(dir.filePath(file), building);
				return false;
			}
		}
	}
	if (!profile.extraModules.isEmpty()) {
		QDir dir(profile.extraModules);
		if (!dir.exists()) {
			*error = tr("The folder of more modules, %1, does not exist.").arg(QDir::toNativeSeparators(profile.extraModules));
			return false;
		}
		QStringList ignored = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
		for (const QString& file : dir.entryList(QDir::Files)) {
			if (!file.endsWith(QLatin1String(".lua"))) {
				ignored.append(file);
			}
		}
		if (!ignored.isEmpty()) {
			notes->append(tr("Only the .lua files directly in %1 are used; left out: %2").arg(QDir::toNativeSeparators(dir.path()), ignored.join(QStringLiteral(", "))));
		}
		for (const QString& file : dir.entryList(QStringList { QStringLiteral("*.lua") }, QDir::Files, QDir::Name)) {
			if (from.contains(file)) {
				// how a community tunes a bundled module, such as IKEMEN GO's rating settings
				notes->append(tr("Your %1 is used instead of the bundled one.").arg(file));
				QFile::remove(out.filePath(file));
			}
			for (const QString& r : registrations(dir.filePath(file))) {
				QString other = registered.value(r);
				if (!other.isEmpty() && other != file) {
					notes->append(tr("Your %1 registers %2, as the bundled %3 does: Nakama keeps only the one it loads last, in the order of the file names.")
					                  .arg(file, r, other));
				}
			}
			if (!QFile::copy(dir.filePath(file), out.filePath(file))) {
				*error = tr("Cannot copy %1 into %2.").arg(dir.filePath(file), building);
				return false;
			}
			from.insert(file, profile.extraModules);
		}
	}
	for (const QString& file : out.entryList(QDir::Files)) {
		QFile::setPermissions(out.filePath(file), QFileDevice::ReadOwner | QFileDevice::WriteOwner);
	}
	QDir old(target);
	if (old.exists() && !old.removeRecursively()) {
		*error = tr("Cannot replace %1.").arg(QDir::toNativeSeparators(target));
		return false;
	}
	if (!QDir().rename(building, target)) {
		*error = tr("Cannot rename %1.").arg(QDir::toNativeSeparators(building));
		return false;
	}
	return true;
}

bool ServerHost::runningElsewhere(const QString& dataDir, qint64* pid) {
	if (!QFileInfo::exists(dataDir)) {
		return false;
	}
	QLockFile lock(QDir(dataDir).filePath(QStringLiteral("server.lock")));
	lock.setStaleLockTime(0);
	if (lock.tryLock(0)) {
		lock.unlock();
		return false;
	}
	if (pid) {
		QString hostname;
		QString app;
		lock.getLockInfo(pid, &hostname, &app);
	}
	return true;
}

QString ServerHost::busyPort(const ServerProfile& profile) {
	QHostAddress any = profile.bind.isEmpty() ? QHostAddress(QHostAddress::Any) : QHostAddress(profile.bind);
	if (any.isNull()) {
		return tr("The listen address %1 is not an IP address.").arg(profile.bind);
	}
	for (const auto& p : profile.ports()) {
		QHostAddress address = any;
		if ((p.second.startsWith(QLatin1String("console")) && !profile.consolePublic) || p.second == QLatin1String("database")) {
			address = QHostAddress(QHostAddress::LocalHost);
		}
		QTcpServer probe;
		if (!probe.listen(address, quint16(p.first))) {
			return tr("Port %1, which the server's %2 needs, is in use by another program.").arg(p.first).arg(p.second);
		}
	}
	return QString();
}

int ServerHost::postgresVersion(const QString& folder) {
	QProcess process;
	process.start(QDir(folder).filePath(exe(QStringLiteral("postgres"))), { QStringLiteral("--version") });
	if (!process.waitForFinished(10000) || process.exitCode() != 0) {
		process.kill();
		return 0;
	}
	// "postgres (PostgreSQL) 16.10 (Ubuntu 16.10-0ubuntu0.24.04.1)"
	QRegularExpressionMatch m = QRegularExpression(QStringLiteral("\\)\\s+(\\d+)")).match(QString::fromLocal8Bit(process.readAllStandardOutput()));
	return m.hasMatch() ? m.captured(1).toInt() : 0;
}

QString ServerHost::pgTool(const QString& name) const {
	return QDir(m_programs.postgres).filePath(exe(name));
}

QString ServerHost::path(const QString& name) const {
	return QDir(m_dataDir).filePath(name);
}

QString ServerHost::databaseAddress() const {
	if (m_profile.managedDatabase()) {
		return QStringLiteral("nakama@127.0.0.1:%1/nakama").arg(m_profile.databasePort);
	}
	QUrl url = databaseUrl(m_profile.database);
	url.setPassword(QString());
	return url.toString();
}

QString ServerHost::databasePassword() const {
	if (m_profile.managedDatabase()) {
		return m_profile.databasePassword;
	}
	return databaseUrl(m_profile.database).password(QUrl::FullyDecoded);
}

bool ServerHost::start(const ServerProfile& profile, const QString& dataDir, const Programs& programs, QString* error) {
	if (active()) {
		*error = tr("The server %1 is already running.").arg(profile.id);
		return false;
	}
	m_profile = profile;
	m_dataDir = QDir::cleanPath(dataDir);
	m_programs = programs;
	m_error.clear();
	m_recent.clear();
	m_stopping = false;
	++m_generation;
	QDir dir(m_dataDir);

	// a folder of its own, which the matchmaker may empty in part: never one it did not make
	QString marker = dir.filePath(markerName());
	if (dir.exists() && !QFileInfo::exists(marker) && !dir.entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden).isEmpty()) {
		*error = tr("%1 holds files of something else: give the server an empty folder, or none (it makes servers/%2 in the configuration folder).")
		             .arg(QDir::toNativeSeparators(m_dataDir), profile.id);
		return false;
	}
	if (!QDir().mkpath(m_dataDir)) {
		*error = tr("Cannot create %1.").arg(QDir::toNativeSeparators(m_dataDir));
		return false;
	}
	if (!QFileInfo::exists(marker)) {
		QFile file(marker);
		if (!file.open(QIODevice::WriteOnly)) {
			*error = tr("Cannot write in %1.").arg(QDir::toNativeSeparators(m_dataDir));
			return false;
		}
		file.write("A community server's folder, made by Nakama Matchmaker (docs/hosting.md): " + profile.id.toUtf8() + "\n");
	}
	QFile::setPermissions(m_dataDir, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);

	m_lock.reset(new QLockFile(dir.filePath(QStringLiteral("server.lock"))));
	m_lock->setStaleLockTime(0);
	if (!m_lock->tryLock(0)) {
		qint64 pid = 0;
		QString hostname;
		QString app;
		m_lock->getLockInfo(&pid, &hostname, &app);
		m_lock.reset();
		*error = tr("The server %1 is already running in another program (process %2).").arg(profile.id).arg(pid);
		return false;
	}

	QString logPath = dir.filePath(QStringLiteral("server.log"));
	m_logFile.setFileName(logPath);
	if (!m_logFile.open(QIODevice::WriteOnly | QIODevice::Append)) {
		*error = tr("Cannot write %1: %2").arg(QDir::toNativeSeparators(logPath), m_logFile.errorString());
		m_lock.reset();
		return false;
	}
	m_logFile.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);

	auto refuse = [&](const QString& reason) {
		*error = reason;
		log(QLatin1String(MATCHMAKER), reason);
		m_logFile.close();
		m_lock.reset();
		return false;
	};
	QFileInfo nakama(programs.nakama);
	if (programs.nakama.isEmpty() || !nakama.isFile() || !nakama.isExecutable()) {
		return refuse(tr("Nakama was not found. Download it from https://github.com/heroiclabs/nakama/releases and set its path in the servers' settings."));
	}
	if (profile.managedDatabase()) {
#ifdef Q_OS_UNIX
		if (::geteuid() == 0) {
			return refuse(tr("PostgreSQL does not run as root. Run the server as an ordinary user, or give it a database of its own."));
		}
#endif
#ifdef Q_OS_WIN
		if (elevated()) {
			return refuse(tr("PostgreSQL does not run with administrator rights. Start Nakama Matchmaker without them, or give the server a database of its own."));
		}
#endif
		QStringList missing;
		for (const char* tool : { "initdb", "postgres", "pg_ctl", "pg_isready" }) {
			if (!QFileInfo(pgTool(QLatin1String(tool))).isExecutable()) {
				missing.append(QLatin1String(tool));
			}
		}
		if (m_programs.postgres.isEmpty() || !missing.isEmpty()) {
			return refuse(tr("PostgreSQL's programs (%1) were not found. Install PostgreSQL and set the folder of its programs in the servers' settings, or give the server a database.")
			                  .arg(missing.isEmpty() ? QStringLiteral("initdb, postgres, pg_ctl, pg_isready") : missing.join(QStringLiteral(", "))));
		}
		QFile version(dir.filePath(QStringLiteral("postgres/PG_VERSION")));
		if (version.open(QIODevice::ReadOnly)) {
			int made = QString::fromLatin1(version.readAll()).trimmed().toInt();
			int found = postgresVersion(m_programs.postgres);
			if (made && found && made != found) {
				return refuse(tr("The server's database was made by PostgreSQL %1, and the PostgreSQL programs in %2 are version %3. Set the folder of PostgreSQL %1's programs, or upgrade the database with pg_upgrade.")
				                  .arg(made)
				                  .arg(QDir::toNativeSeparators(m_programs.postgres))
				                  .arg(found));
			}
		}
	}
	if (!profile.extraModules.isEmpty() && (inside(profile.extraModules, m_dataDir) || inside(profile.extraModules, programs.modules))) {
		return refuse(tr("Keep your own modules in a folder outside the server's folder and the bundled modules: the server's modules folder is rebuilt at every start."));
	}
	QString why;
	QStringList notes;
	if (!installModules(profile, programs.modules, dir.filePath(QStringLiteral("modules")), &notes, &why)) {
		return refuse(why);
	}
	if (!ServerConfig::writePrivate(dir.filePath(QStringLiteral("nakama.yml")), ServerConfig::nakamaYaml(profile, m_dataDir), &why)) {
		return refuse(why);
	}

	m_env = QProcessEnvironment::systemEnvironment();
	m_env.remove(QStringLiteral("NETPLAY_CONNECT"));
	if (profile.managedDatabase()) {
		// libpq's variables (PGSSLMODE, PGHOST, ...) would point the tools elsewhere
		for (const QString& key : m_env.keys()) {
			if (key.startsWith(QLatin1String("PG"))) {
				m_env.remove(key);
			}
		}
	}
	m_env.remove(QStringLiteral("PGPASSWORD"));
	if (!databasePassword().isEmpty()) {
		m_env.insert(QStringLiteral("PGPASSWORD"), databasePassword());
	}

	setState(State::Starting);
	log(QLatin1String(MATCHMAKER), tr("Starting the server %1 (%2) on port %3.").arg(profile.id, profile.kindsLabel()).arg(profile.port));
	for (const QString& note : notes) {
		log(QLatin1String(MATCHMAKER), note);
	}
	if (profile.consolePublic) {
		log(QLatin1String(MATCHMAKER), tr("Warning: Nakama's console is open to other computers, over plain HTTP. Reach it through an SSH tunnel or a VPN instead (docs/hosting.md)."));
	}
	stopLeftoverNakama();
	return true;
}

QProcess* ServerHost::spawn(const QString& source, const QString& program, const QStringList& args) {
	QProcess* process = new ServerProcess(this);
	process->setProcessEnvironment(m_env);
	process->setWorkingDirectory(m_dataDir);
	process->setProcessChannelMode(QProcess::MergedChannels);
	std::shared_ptr<QByteArray> pending(new QByteArray);
	auto drain = [this, process, source, pending](bool all) {
		pending->append(process->readAll());
		int end;
		while ((end = pending->indexOf('\n')) >= 0) {
			QByteArray line = pending->left(end);
			pending->remove(0, end + 1);
			if (line.endsWith('\r')) {
				line.chop(1);
			}
			if (!line.isEmpty()) {
				log(source, QString::fromLocal8Bit(line));
			}
		}
		if (all && !pending->isEmpty()) {
			log(source, QString::fromLocal8Bit(*pending));
			pending->clear();
		}
	};
	connect(process, &QProcess::readyRead, this, [drain]() { drain(false); });
	connect(process, static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished), this,
	        [drain](int, QProcess::ExitStatus) { drain(true); });
	process->start(program, args);
	return process;
}

void ServerHost::runTool(const QString& source, const QString& program, const QStringList& args, Done done, const QByteArray& input) {
	int generation = m_generation;
	QProcess* process = spawn(source, program, args);
	m_tool = process;
	if (!input.isNull()) {
		process->write(input);
		process->closeWriteChannel();
	}
	auto finish = [this, process, generation, done](int code, bool crashed) {
		if (m_tool == process) {
			m_tool = nullptr;
		}
		process->deleteLater();
		if (generation == m_generation) {
			done(code, crashed);
		}
	};
	connect(process, &QProcess::errorOccurred, this, [this, process, program, finish](QProcess::ProcessError e) {
		if (e == QProcess::FailedToStart) {
			log(QLatin1String(MATCHMAKER), tr("Cannot start %1: %2").arg(program, process->errorString()));
			finish(-1, true);
		}
	});
	connect(process, static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished), this,
	        [finish](int code, QProcess::ExitStatus status) { finish(code, status == QProcess::CrashExit); });
}

/* A Nakama of an earlier run of this server that is still running, after the program that
 * ran it ended without stopping it: its process id is in nakama.pid, and it is stopped when
 * that process still runs the same program. */
void ServerHost::stopLeftoverNakama() {
	QFile file(path(QStringLiteral("nakama.pid")));
	qint64 pid = 0;
	QString image;
	if (file.open(QIODevice::ReadOnly)) {
		QStringList lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
		pid = lines.value(0).trimmed().toLongLong();
		image = lines.value(1).trimmed();
		file.close();
	}
	if (pid <= 0 || !processAlive(pid) || image.isEmpty() || processImage(pid) != image) {
		QFile::remove(path(QStringLiteral("nakama.pid")));
		stopLeftoverDatabase();
		return;
	}
	log(QLatin1String(MATCHMAKER), tr("The Nakama of an earlier run is still running (process %1); stopping it.").arg(pid));
	signalProcess(pid, false);
	int generation = m_generation;
	std::shared_ptr<int> waited(new int(0));
	std::shared_ptr<std::function<void()>> poll(new std::function<void()>);
	*poll = [this, pid, generation, waited, poll]() {
		if (generation != m_generation) {
			*poll = nullptr;   // ends the cycle of the function holding itself
			return;
		}
		if (!processAlive(pid)) {
			QFile::remove(path(QStringLiteral("nakama.pid")));
			*poll = nullptr;
			stopLeftoverDatabase();
			return;
		}
		if (++*waited == 75) {
			signalProcess(pid, true);   // after 15 seconds
		} else if (*waited > 100) {
			*poll = nullptr;
			fail(tr("Cannot stop the Nakama left running by an earlier run (process %1).").arg(pid));
			return;
		}
		std::function<void()> next = *poll;
		QTimer::singleShot(200, this, next);
	};
	std::function<void()> first = *poll;
	QTimer::singleShot(200, this, first);
}

/* The database of an earlier run that is still running. postmaster.pid alone does not say
 * so (its process id may belong to another program by now), so the process must run this
 * PostgreSQL's postgres where the system can tell, and answer on this server's port. */
void ServerHost::stopLeftoverDatabase() {
	QFile::remove(path(QStringLiteral("initdb-password")));
	QDir(path(QStringLiteral("postgres.new"))).removeRecursively();   // a creation that did not finish
	QFile file(path(QStringLiteral("postgres/postmaster.pid")));
	if (!m_profile.managedDatabase() || !file.open(QIODevice::ReadOnly)) {
		checkPorts();
		return;
	}
	QStringList lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
	file.close();
	qint64 pid = lines.value(0).trimmed().toLongLong();
	int port = lines.value(3).trimmed().toInt();
	QString image = processImage(pid);
	bool ours = processAlive(pid) && port == m_profile.databasePort &&
	            (image.isEmpty() || image == QFileInfo(pgTool(QStringLiteral("postgres"))).canonicalFilePath());
	if (!ours) {
		checkPorts();   // a stale lock file: PostgreSQL replaces it
		return;
	}
	runTool(QStringLiteral("pg_isready"), pgTool(QStringLiteral("pg_isready")),
	        { QStringLiteral("-q"), QStringLiteral("-h"), QStringLiteral("127.0.0.1"), QStringLiteral("-p"), QString::number(port) }, [this, pid](int code, bool) {
		if (code != 0 && code != 1) {
			checkPorts();
			return;
		}
		log(QLatin1String(MATCHMAKER), tr("The database of an earlier run is still running (process %1); stopping it.").arg(pid));
		runTool(QStringLiteral("postgres"), pgTool(QStringLiteral("pg_ctl")),
		        { QStringLiteral("stop"), QStringLiteral("-D"), path(QStringLiteral("postgres")), QStringLiteral("-m"), QStringLiteral("fast"), QStringLiteral("-w"),
		          QStringLiteral("-t"), QStringLiteral("30") },
		        [this](int code, bool) {
			if (code != 0) {
				fail(tr("Cannot stop the database left running by an earlier run (see the log)."));
				return;
			}
			checkPorts();
		});
	});
}

void ServerHost::checkPorts() {
	QString busy = busyPort(m_profile);
	if (!busy.isEmpty()) {
		fail(busy);
		return;
	}
	if (!m_profile.managedDatabase()) {
		migrate();
	} else if (!QFileInfo::exists(path(QStringLiteral("postgres/PG_VERSION")))) {
		initDatabase();
	} else {
		launchPostgres();
	}
}

/* The database is made in postgres.new, which becomes postgres once it is complete: a
 * creation cut short leaves nothing that looks like a database. The superuser gets a
 * password nobody keeps (the system user can always reach the database in single-user
 * mode); Nakama connects as nakama, which owns its database and nothing else. */
void ServerHost::initDatabase() {
	QString building = path(QStringLiteral("postgres.new"));
	QString passwordFile = path(QStringLiteral("initdb-password"));
	QString error;
	if (!ServerConfig::writePrivate(passwordFile, ServerConfig::secret().toUtf8() + '\n', &error)) {
		fail(error);
		return;
	}
	log(QLatin1String(MATCHMAKER), tr("Creating the server's database (first start)."));
	runTool(QStringLiteral("initdb"), pgTool(QStringLiteral("initdb")),
	        { QStringLiteral("-D"), building, QStringLiteral("-U"), QStringLiteral("postgres"), QStringLiteral("--auth=scram-sha-256"),
	          QStringLiteral("--pwfile=") + passwordFile, QStringLiteral("-E"), QStringLiteral("UTF8"), QStringLiteral("--no-locale") },
	        [this](int code, bool) {
		if (code != 0) {
			fail(tr("Cannot create the database (initdb failed, see the log)."));
			return;
		}
		createRole();
	});
	// whatever happens to initdb, the password file goes, and so does half a database
	connect(m_tool, static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished), this,
	        [building, passwordFile](int code, QProcess::ExitStatus status) {
		QFile::remove(passwordFile);
		if (code != 0 || status == QProcess::CrashExit) {
			QDir(building).removeRecursively();
		}
	});
}

void ServerHost::createRole() {
	QString building = path(QStringLiteral("postgres.new"));
	// single-user mode reads one statement per line; the password goes in on standard input
	QByteArray sql = "CREATE ROLE nakama LOGIN PASSWORD '" + m_profile.databasePassword.toUtf8() + "';\n"
	                 "CREATE DATABASE nakama OWNER nakama;\n";
	runTool(QStringLiteral("postgres"), pgTool(QStringLiteral("postgres")), { QStringLiteral("--single"), QStringLiteral("-D"), building, QStringLiteral("postgres") },
	        [this, building](int code, bool) {
		if (code != 0) {
			QDir(building).removeRecursively();
			fail(tr("Cannot create the database's nakama role (see the log)."));
			return;
		}
		if (!QDir().rename(building, path(QStringLiteral("postgres")))) {
			fail(tr("Cannot rename %1.").arg(QDir::toNativeSeparators(building)));
			return;
		}
		launchPostgres();
	}, sql);
	connect(m_tool, static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished), this,
	        [building](int, QProcess::ExitStatus status) {
		if (status == QProcess::CrashExit) {
			QDir(building).removeRecursively();
		}
	});
}

void ServerHost::launchPostgres() {
	QString pgdata = path(QStringLiteral("postgres"));
	log(QLatin1String(MATCHMAKER), tr("Starting the database on 127.0.0.1, port %1.").arg(m_profile.databasePort));
	// TCP on 127.0.0.1 only, no Unix socket: nothing else on the computer needs it
	QProcess* process = spawn(QStringLiteral("postgres"), pgTool(QStringLiteral("postgres")),
	                          { QStringLiteral("-D"), pgdata, QStringLiteral("-c"), QStringLiteral("listen_addresses=127.0.0.1"), QStringLiteral("-c"),
	                            QStringLiteral("port=%1").arg(m_profile.databasePort), QStringLiteral("-c"), QStringLiteral("unix_socket_directories=") });
	m_postgres = process;
	connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError e) {
		if (e == QProcess::FailedToStart) {
			if (m_postgres == process) {
				m_postgres = nullptr;
			}
			process->deleteLater();
			fail(tr("Cannot start the database: %1").arg(process->errorString()));
		}
	});
	connect(process, static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished), this,
	        [this, process](int code, QProcess::ExitStatus) {
		if (m_postgres == process) {
			m_postgres = nullptr;
		}
		process->deleteLater();
		if (m_stopping) {
			if (!m_nakama) {
				m_deadline.stop();
				finishStop();
			}
			return;
		}
		bool locked = false;
		for (const QString& line : m_recent.mid(qMax(0, m_recent.size() - 20))) {
			locked = locked || (line.contains(QLatin1String("postmaster.pid")) && line.contains(QLatin1String("already exists")));
		}
		if (locked) {
			fail(tr("Another database server says it runs in this server's folder (postgres/postmaster.pid). If none does, delete that file and start again."));
		} else {
			fail(tr("The database stopped (exit code %1); see the log.").arg(code));
		}
	});
	m_waitClock.start();
	waitForDatabase();
}

void ServerHost::waitForDatabase() {
	if (m_stopping) {
		return;
	}
	runTool(QStringLiteral("pg_isready"), pgTool(QStringLiteral("pg_isready")),
	        { QStringLiteral("-q"), QStringLiteral("-h"), QStringLiteral("127.0.0.1"), QStringLiteral("-p"), QString::number(m_profile.databasePort) },
	        [this](int code, bool) {
		if (code == 0) {
			migrate();
			return;
		}
		// 1: starting or recovering after a crash, which may take long; 2: no answer yet
		qint64 limit = code == 1 ? 10 * 60 * 1000 : 30 * 1000;
		if (code > 2 || m_waitClock.elapsed() > limit) {
			fail(tr("The database did not start (see the log)."));
			return;
		}
		int generation = m_generation;
		QTimer::singleShot(250, this, [this, generation]() {
			if (generation == m_generation) {
				waitForDatabase();
			}
		});
	});
}

void ServerHost::migrate() {
	runTool(QStringLiteral("nakama"), m_programs.nakama,
	        { QStringLiteral("migrate"), QStringLiteral("up"), QStringLiteral("--database.address"), databaseAddress() }, [this](int code, bool) {
		if (code != 0) {
			fail(tr("Nakama could not prepare its database (nakama migrate up failed, see the log)."));
			return;
		}
		launchNakama();
	});
}

void ServerHost::launchNakama() {
	QStringList args { QStringLiteral("--config"), path(QStringLiteral("nakama.yml")), QStringLiteral("--database.address"), databaseAddress() };
	args += m_profile.extraArgs;
	QProcess* process = spawn(QStringLiteral("nakama"), m_programs.nakama, args);
	m_nakama = process;
	connect(process, &QProcess::started, this, [this, process]() {
		// how the next start recognizes this Nakama if this program ends without stopping it
		QString error;
		ServerConfig::writePrivate(path(QStringLiteral("nakama.pid")),
		                           QByteArray::number(process->processId()) + '\n' + QFileInfo(m_programs.nakama).canonicalFilePath().toUtf8() + '\n', &error);
	});
	connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError e) {
		if (e == QProcess::FailedToStart) {
			if (m_nakama == process) {
				m_nakama = nullptr;
			}
			process->deleteLater();
			fail(tr("Cannot start Nakama: %1").arg(process->errorString()));
		}
	});
	connect(process, static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished), this,
	        [this, process](int code, QProcess::ExitStatus) {
		if (m_nakama == process) {
			m_nakama = nullptr;
		}
		process->deleteLater();
		QFile::remove(path(QStringLiteral("nakama.pid")));
		if (m_stopping) {
			m_deadline.stop();
			stopPostgres();
		} else {
			fail(tr("Nakama stopped (exit code %1); see the log.").arg(code));
		}
	});
	waitForNakama(0);
}

void ServerHost::waitForNakama(int attempt) {
	if (m_stopping) {
		return;
	}
	if (attempt >= 120) {
		fail(tr("Nakama did not answer within 60 seconds."));
		return;
	}
	QUrl url;
	url.setScheme(m_profile.tlsCertificate.isEmpty() ? QStringLiteral("http") : QStringLiteral("https"));
	QString host = m_profile.bind;
	if (host.isEmpty() || host == QLatin1String("0.0.0.0") || host == QLatin1String("::")) {
		host = QStringLiteral("127.0.0.1");
	}
	url.setHost(host);
	url.setPort(m_profile.port);
	url.setPath(QStringLiteral("/healthcheck"));
	QNetworkReply* reply = m_http->get(QNetworkRequest(url));
	if (!m_profile.tlsCertificate.isEmpty()) {
		reply->ignoreSslErrors();   // this computer's own server, whatever its certificate says
	}
	QTimer::singleShot(2000, reply, &QNetworkReply::abort);
	int generation = m_generation;
	connect(reply, &QNetworkReply::finished, this, [this, reply, attempt, generation]() {
		reply->deleteLater();
		if (generation != m_generation || m_stopping) {
			return;
		}
		int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
		if (reply->error() == QNetworkReply::NoError && status == 200) {
			setState(State::Running);
			log(QLatin1String(MATCHMAKER), tr("The server %1 is running. Players connect to %2, port %3, with the server key.")
			                                   .arg(m_profile.id, m_profile.clientHost())
			                                   .arg(m_profile.clientPort()));
			return;
		}
		QTimer::singleShot(500, this, [this, attempt, generation]() {
			if (generation == m_generation) {
				waitForNakama(attempt + 1);
			}
		});
	});
}

void ServerHost::fail(const QString& error) {
	if (m_error.isEmpty()) {
		m_error = error;
	}
	log(QLatin1String(MATCHMAKER), error);
	if (m_stopping) {
		return;   // already stopping: the end of the stop reports the failure
	}
	m_stopping = true;
	++m_generation;
	if (m_tool) {
		m_tool->kill();
	}
	setState(State::Stopping);
	stopNakama();
}

void ServerHost::stop() {
	if (!active() || m_stopping) {
		return;
	}
	m_stopping = true;
	++m_generation;
	if (m_tool) {
		m_tool->kill();   // a step in progress; its continuation no longer runs
	}
	setState(State::Stopping);
	log(QLatin1String(MATCHMAKER), tr("Stopping the server %1.").arg(m_id));
	stopNakama();
}

void ServerHost::stopNakama() {
	if (m_nakama && m_nakama->state() != QProcess::NotRunning) {
#ifdef Q_OS_WIN
		m_nakama->kill();   // a console program on Windows has no polite way to be asked
#else
		m_nakama->terminate();
#endif
		m_deadline.start(15000);
		return;   // its end stops the database
	}
	stopPostgres();
}

void ServerHost::stopPostgres() {
	if (m_postgres && m_postgres->state() != QProcess::NotRunning) {
#ifdef Q_OS_WIN
		runTool(QStringLiteral("postgres"), pgTool(QStringLiteral("pg_ctl")),
		        { QStringLiteral("stop"), QStringLiteral("-D"), path(QStringLiteral("postgres")), QStringLiteral("-m"), QStringLiteral("fast"), QStringLiteral("-w") },
		        [](int, bool) {});
#else
		::kill(static_cast<pid_t>(m_postgres->processId()), SIGINT);   // PostgreSQL's fast shutdown
#endif
		m_deadline.start(30000);
		return;   // its end finishes the stop
	}
	finishStop();
}

void ServerHost::finishStop() {
	if (!m_stopping) {
		return;
	}
	m_stopping = false;
	++m_generation;
	if (m_tool) {
		// a step cut short ends here, so that what it leaves (half a database) is cleaned up now
		QProcess* tool = m_tool;
		tool->kill();
		tool->waitForFinished(5000);
	}
	log(QLatin1String(MATCHMAKER), m_error.isEmpty() ? tr("The server %1 has stopped.").arg(m_id) : tr("The server %1 has failed.").arg(m_id));
	m_logFile.close();
	m_lock.reset();
	setState(m_error.isEmpty() ? State::Stopped : State::Failed);
}

void ServerHost::stopAndWait(int msecs) {
	stop();
	QElapsedTimer clock;
	clock.start();
	while (active() && clock.elapsed() < msecs) {
		QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
		if (m_nakama) {
			m_nakama->waitForFinished(50);
		} else if (m_postgres) {
			m_postgres->waitForFinished(50);
		}
	}
	for (QProcess* p : { m_nakama, m_postgres }) {
		if (p && p->state() != QProcess::NotRunning) {
			p->kill();
			p->waitForFinished(3000);
		}
	}
}
