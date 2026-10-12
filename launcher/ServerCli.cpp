/* SPDX-License-Identifier: MIT */
#include "ServerCli.h"

#include "ServerManager.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSocketNotifier>
#include <QStandardPaths>
#include <QTimer>

#include <cstdio>
#include <functional>

#ifdef Q_OS_UNIX
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>
#endif
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {

void out(const QString& text) {
	fprintf(stdout, "%s\n", qPrintable(text));
	fflush(stdout);
}

int fail(const QString& text) {
	fprintf(stderr, "%s\n", qPrintable(text));
	return 1;
}

/* Ctrl+C, SIGTERM and SIGHUP become one call on the event loop's thread. */
std::function<void()> g_onStop;

#ifdef Q_OS_UNIX
int g_signalPipe[2] = { -1, -1 };

void onSignal(int) {
	char c = 1;
	ssize_t written = ::write(g_signalPipe[1], &c, 1);
	(void) written;
}
#endif
#ifdef Q_OS_WIN
BOOL WINAPI onConsoleEvent(DWORD) {
	QMetaObject::invokeMethod(QCoreApplication::instance(), []() {
		if (g_onStop) {
			g_onStop();
		}
	}, Qt::QueuedConnection);
	return TRUE;
}
#endif

void installStopHandler(std::function<void()> onStop) {
	g_onStop = onStop;
#ifdef Q_OS_UNIX
	if (::socketpair(AF_UNIX, SOCK_STREAM, 0, g_signalPipe) != 0) {
		return;
	}
	QSocketNotifier* notifier = new QSocketNotifier(g_signalPipe[0], QSocketNotifier::Read, QCoreApplication::instance());
	QObject::connect(notifier, &QSocketNotifier::activated, []() {
		char c;
		ssize_t got = ::read(g_signalPipe[0], &c, 1);
		(void) got;
		if (g_onStop) {
			g_onStop();
		}
	});
	struct sigaction action = {};
	action.sa_handler = onSignal;
	sigemptyset(&action.sa_mask);
	action.sa_flags = SA_RESTART;
	sigaction(SIGINT, &action, nullptr);
	sigaction(SIGTERM, &action, nullptr);
	sigaction(SIGHUP, &action, nullptr);
#endif
#ifdef Q_OS_WIN
	SetConsoleCtrlHandler(onConsoleEvent, TRUE);
#endif
}

bool yesNo(const QString& value, bool* result) {
	QString v = value.trimmed().toLower();
	if (v == QLatin1String("yes") || v == QLatin1String("on") || v == QLatin1String("true") || v == QLatin1String("1")) {
		*result = true;
		return true;
	}
	if (v == QLatin1String("no") || v == QLatin1String("off") || v == QLatin1String("false") || v == QLatin1String("0")) {
		*result = false;
		return true;
	}
	return false;
}

bool number(const QCommandLineParser& parser, const QString& name, int* result, QString* error) {
	if (!parser.isSet(name)) {
		return true;
	}
	bool ok = false;
	int n = parser.value(name).toInt(&ok);
	if (!ok) {
		*error = QStringLiteral("--%1 needs a number").arg(name);
		return false;
	}
	*result = n;
	return true;
}

/* The options of add and set, applied to a profile. */
bool applyOptions(const QCommandLineParser& parser, ServerProfile* s, QString* error) {
	if (parser.isSet("title")) {
		s->title = parser.value("title").trimmed();
	}
	if (parser.isSet("message")) {
		s->message = parser.value("message");
	}
	if (parser.isSet("kinds")) {
		QStringList kinds = ServerConfig::parseKinds(parser.value("kinds"), error);
		if (kinds.isEmpty()) {
			return false;
		}
		s->kinds = kinds;
	}
	if (!number(parser, "port", &s->port, error) || !number(parser, "console-port", &s->consolePort, error) ||
	    !number(parser, "public-port", &s->publicPort, error) || !number(parser, "database-port", &s->databasePort, error)) {
		return false;
	}
	if (parser.isSet("port") && !parser.isSet("console-port")) {
		s->consolePort = s->port + 1;
	}
	for (const QString& flag : { QStringLiteral("console-public"), QStringLiteral("client-tls") }) {
		if (parser.isSet(flag)) {
			bool value = false;
			if (!yesNo(parser.value(flag), &value)) {
				*error = QStringLiteral("--%1 takes yes or no").arg(flag);
				return false;
			}
			(flag == QLatin1String("console-public") ? s->consolePublic : s->clientTls) = value;
		}
	}
	if (parser.isSet("bind")) {
		s->bind = parser.value("bind").trimmed();
	}
	if (parser.isSet("public-host")) {
		s->publicHost = parser.value("public-host").trimmed();
	}
	if (parser.isSet("database")) {
		QString db = parser.value("database").trimmed();
		s->database = db == QLatin1String("managed") ? QString() : db;
	}
	if (parser.isSet("server-key")) {
		QString key = parser.value("server-key");
		s->serverKey = key == QLatin1String("new") ? ServerConfig::secret() : key;
	}
	if (parser.isSet("tls-cert")) {
		s->tlsCertificate = parser.value("tls-cert").isEmpty() ? QString() : QFileInfo(parser.value("tls-cert")).absoluteFilePath();
	}
	if (parser.isSet("tls-key")) {
		s->tlsPrivateKey = parser.value("tls-key").isEmpty() ? QString() : QFileInfo(parser.value("tls-key")).absoluteFilePath();
	}
	if (parser.isSet("extra-modules")) {
		s->extraModules = parser.value("extra-modules").isEmpty() ? QString() : QFileInfo(parser.value("extra-modules")).absoluteFilePath();
	}
	if (parser.isSet("data-dir")) {
		s->dataDir = parser.value("data-dir").isEmpty() ? QString() : QFileInfo(parser.value("data-dir")).absoluteFilePath();
	}
	if (parser.isSet("nakama-arg")) {
		s->extraArgs.clear();
		for (const QString& arg : parser.values("nakama-arg")) {
			if (!arg.isEmpty()) {
				s->extraArgs.append(arg);
			}
		}
	}
	return true;
}

void applyPrograms(const QCommandLineParser& parser, ServerConfig* config) {
	auto absolute = [](const QString& path) { return path.isEmpty() ? QString() : QFileInfo(path).absoluteFilePath(); };
	if (parser.isSet("nakama")) {
		config->nakamaPath = absolute(parser.value("nakama"));
	}
	if (parser.isSet("postgres")) {
		config->postgresPath = absolute(parser.value("postgres"));
	}
	if (parser.isSet("modules")) {
		config->modulesPath = absolute(parser.value("modules"));
	}
}

QString orNone(const QString& text, const QString& none) {
	return text.isEmpty() ? none : text;
}

void printServer(const ServerManager& manager, const ServerProfile& s, bool secrets) {
	auto hidden = [secrets](const QString& value) { return secrets ? value : QStringLiteral("(hidden; --secrets shows it)"); };
	out(QStringLiteral("Server %1: %2").arg(s.id, s.title));
	out(QStringLiteral("  Games:          %1").arg(s.kindsLabel()));
	if (!s.message.isEmpty()) {
		out(QStringLiteral("  Message:        %1").arg(s.message));
	}
	out(QStringLiteral("  Listens on:     %1, port %2 (and %3)").arg(orNone(s.bind, QStringLiteral("every address"))).arg(s.port).arg(s.port - 1));
	out(QStringLiteral("  Console:        port %1 (and %2), %3, user %4, password %5")
	        .arg(s.consolePort)
	        .arg(s.consolePort - 3)
	        .arg(s.consolePublic ? QStringLiteral("open to other computers") : QStringLiteral("this computer only"), s.consoleUser,
	             hidden(s.consolePassword)));
	if (s.managedDatabase()) {
		out(QStringLiteral("  Database:       its own PostgreSQL on 127.0.0.1, port %1").arg(s.databasePort));
	} else {
		out(QStringLiteral("  Database:       %1").arg(secrets ? s.database : QStringLiteral("(an address of its own; --secrets shows it)")));
	}
	if (!s.tlsCertificate.isEmpty()) {
		out(QStringLiteral("  TLS:            %1, %2").arg(s.tlsCertificate, s.tlsPrivateKey));
	}
	if (!s.extraModules.isEmpty()) {
		out(QStringLiteral("  Own modules:    %1").arg(s.extraModules));
	}
	if (!s.extraArgs.isEmpty()) {
		out(QStringLiteral("  Nakama options: %1").arg(s.extraArgs.join(QLatin1Char(' '))));
	}
	out(QStringLiteral("  Data folder:    %1").arg(QDir::toNativeSeparators(manager.config().dataDir(s))));
	if (secrets) {
		out(QStringLiteral("  HTTP key:       %1").arg(s.httpKey));
	}
	QString conflicts = manager.config().conflicts(s).join(QStringLiteral(", "));
	if (!conflicts.isEmpty()) {
		out(QStringLiteral("  Shares ports or its folder with: %1 (they cannot run at the same time)").arg(conflicts));
	}
	out(QStringLiteral("  Players enter:"));
	for (const QString& line : manager.connectionInfo(s)) {
		out(QStringLiteral("    ") + line);
	}
}

QString g_program = QStringLiteral("nakama-matchmaker server");

QString usage() {
	return QStringLiteral(
	"Usage: %1 [--config <folder>] <command> [options]\n"
	"\n"
	"Runs Nakama servers for IKEMEN GO, GBA (mGBA fork) and DS (melonDS fork) communities.\n"
	"\n"
	"Commands:\n"
	"  list                      the servers\n"
	"  add <id> [options]        a new server (keys and passwords are generated)\n"
	"  set <id> [options]        change a server\n"
	"  remove <id>               forget a server (--delete-data also deletes its folder)\n"
	"  show <id>                 a server's settings and what players enter (--secrets)\n"
	"  run <id>... | --all       run servers until Ctrl+C or SIGTERM\n"
	"  ikemen-profile <id> <file>  write an IKEMEN GO server profile (--game, --build, --stun)\n"
	"  programs                  the programs the servers use (--nakama, --postgres, --modules)\n"
	"\n"
	"Run '%1 <command> --help' for a command's options.\n").arg(g_program);
}

}

bool isServerCommand(int argc, char** argv, QString* configFolder, QStringList* rest) {
	// the launcher's options that take a value
	static const QStringList withValue { QStringLiteral("config"), QStringLiteral("name"), QStringLiteral("device"),
		                                 QStringLiteral("host"), QStringLiteral("port"), QStringLiteral("auto") };
	for (int i = 1; i < argc; ++i) {
		QString arg = QString::fromLocal8Bit(argv[i]);
		if (arg.startsWith(QLatin1Char('-'))) {
			QString name = arg.mid(arg.startsWith(QLatin1String("--")) ? 2 : 1);
			QString value;
			bool inlineValue = name.contains(QLatin1Char('='));
			if (inlineValue) {
				value = name.section(QLatin1Char('='), 1);
				name = name.section(QLatin1Char('='), 0, 0);
			}
			if (withValue.contains(name) && !inlineValue && i + 1 < argc) {
				value = QString::fromLocal8Bit(argv[++i]);
			}
			if (name == QLatin1String("config")) {
				*configFolder = value;
			}
			continue;
		}
		if (arg != QLatin1String("server")) {
			return false;
		}
		rest->clear();
		for (int j = i + 1; j < argc; ++j) {
			rest->append(QString::fromLocal8Bit(argv[j]));
		}
		return true;
	}
	return false;
}

void splitServerArguments(int argc, char** argv, QString* configFolder, QStringList* rest) {
	rest->clear();
	for (int i = 1; i < argc; ++i) {
		QString arg = QString::fromLocal8Bit(argv[i]);
		if (rest->isEmpty() && (arg == QLatin1String("--config") || arg == QLatin1String("-config")) && i + 1 < argc) {
			*configFolder = QString::fromLocal8Bit(argv[++i]);
		} else if (rest->isEmpty() && arg.startsWith(QLatin1String("--config="))) {
			*configFolder = arg.mid(9);
		} else {
			rest->append(arg);
		}
	}
}

int runServerCommand(const QString& configFolder, const QStringList& args, const QString& program) {
	g_program = program;
	QString command = args.value(0);
	if (command.isEmpty() || command == QLatin1String("--help") || command == QLatin1String("-h") || command == QLatin1String("help")) {
		fprintf(command.isEmpty() ? stderr : stdout, "%s", qPrintable(usage()));
		return command.isEmpty() ? 1 : 0;
	}
	if (command == QLatin1String("--version") || command == QLatin1String("-v")) {
		out(QStringLiteral("%1 %2").arg(QCoreApplication::applicationName(), QCoreApplication::applicationVersion()));
		return 0;
	}

	QCommandLineParser parser;
	parser.setApplicationDescription(QStringLiteral("%1 %2").arg(g_program, command));
	parser.addHelpOption();
	parser.addPositionalArgument(QStringLiteral("command"), QStringLiteral("the server command"));
	bool edits = command == QLatin1String("add") || command == QLatin1String("set");
	if (edits) {
		parser.addOption({ "title", "the community's name (Nakama Matchmaker shows it; it also names IKEMEN GO profiles)", "text" });
		parser.addOption({ "message", "a message Nakama Matchmaker's players see when they sign in", "text" });
		parser.addOption({ "kinds", "the games: ikemen, gba, ds, comma separated, or all", "list" });
		parser.addOption({ "port", "the API port (default 7350; also uses the port before it)", "port" });
		parser.addOption({ "console-port", "Nakama's console port (default: the port + 1; also uses this port - 3)", "port" });
		parser.addOption({ "console-public", "open the console to other computers (plain HTTP: prefer an SSH tunnel)", "yes|no" });
		parser.addOption({ "bind", "the address to listen on (default: every address)", "address" });
		parser.addOption({ "public-host", "the address players enter", "host" });
		parser.addOption({ "public-port", "the port players enter, when a proxy listens on another one (0: the port)", "port" });
		parser.addOption({ "client-tls", "players connect with TLS (through a TLS proxy)", "yes|no" });
		parser.addOption({ "database", "a PostgreSQL or CockroachDB address (user:password@host:port/database), or managed", "address" });
		parser.addOption({ "database-port", "the port of the server's own PostgreSQL (127.0.0.1)", "port" });
		parser.addOption({ "server-key", "the key players enter (new: a new random key)", "key" });
		parser.addOption({ "tls-cert", "TLS certificate file (empty: none)", "file" });
		parser.addOption({ "tls-key", "TLS private key file", "file" });
		parser.addOption({ "extra-modules", "a folder of more Lua modules for the server", "folder" });
		parser.addOption({ "data-dir", "the server's folder (default: servers/<id> in the configuration folder)", "folder" });
		parser.addOption({ "nakama-arg", "an argument for Nakama (repeat for several; replaces the earlier ones; other users of the computer can see them)", "argument" });
	}
	if (edits || command == QLatin1String("programs")) {
		parser.addOption({ "nakama", "Nakama's executable (every server)", "file" });
		parser.addOption({ "postgres", "the folder of PostgreSQL's programs: initdb, postgres, pg_ctl, pg_isready (every server)", "folder" });
		parser.addOption({ "modules", "the folder of the bundled server modules (every server)", "folder" });
	}
	if (command == QLatin1String("remove")) {
		parser.addOption({ "delete-data", "also delete the server's folder: its database, logs and configuration" });
	}
	if (command == QLatin1String("show")) {
		parser.addOption({ "secrets", "also show the passwords and keys only the operator needs" });
	}
	if (command == QLatin1String("run")) {
		parser.addOption({ "all", "run every server" });
	}
	if (command == QLatin1String("ikemen-profile")) {
		parser.addOption({ "game", "the IKEMEN GO game id of the profile's pools", "id" });
		parser.addOption({ "build", "the IKEMEN GO build of the profile's pools", "build" });
		parser.addOption({ "host", "the server address in the profile (default: the server's public address)", "host" });
		parser.addOption({ "region", "the region the profile names", "region" });
		parser.addOption({ "stun", "STUN servers, host:port, comma separated (default: Nakama Matchmaker's)", "list" });
	}
	if (!parser.parse(QStringList { QStringLiteral("nakama-matchmaker") } + args)) {
		return fail(parser.errorText());
	}
	if (parser.isSet("help")) {
		out(parser.helpText());
		return 0;
	}
	QStringList positional = parser.positionalArguments().mid(1);

	QString folder = configFolder;
	if (folder.isEmpty()) {
		QCoreApplication::setApplicationName(QStringLiteral("nakama-matchmaker"));
		folder = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
	}
	ServerManager manager(folder);
	QString error;
	if (!manager.load(&error)) {
		return fail(error);
	}
	ServerConfig& config = manager.config();
	if (config.filledSecrets() && !config.save(&error)) {
		return fail(error);
	}
	auto need = [&](int count, const char* usage) {
		if (positional.size() != count) {
			fprintf(stderr, "usage: %s %s\n", qPrintable(g_program), usage);
			return false;
		}
		return true;
	};

	if (command == QLatin1String("list")) {
		if (config.servers().isEmpty()) {
			out(QStringLiteral("No servers in %1. Add one: %2 add <id> --kinds ikemen,gba,ds").arg(QDir::toNativeSeparators(config.path()), g_program));
			return 0;
		}
		for (const ServerProfile& s : config.servers()) {
			out(QStringLiteral("%1  %2  [%3]  port %4%5").arg(s.id.leftJustified(16), s.title, s.kindsLabel()).arg(s.port).arg(s.managedDatabase() ? QString() : QStringLiteral(", own database")));
		}
		return 0;
	}

	if (command == QLatin1String("programs")) {
		applyPrograms(parser, &config);
		if ((parser.isSet("nakama") || parser.isSet("postgres") || parser.isSet("modules")) && !config.save(&error)) {
			return fail(error);
		}
		out(QStringLiteral("Nakama:          %1").arg(orNone(QDir::toNativeSeparators(manager.nakamaProgram()), QStringLiteral("not found (--nakama <file>)"))));
		out(QStringLiteral("PostgreSQL:      %1").arg(orNone(QDir::toNativeSeparators(manager.postgresFolder()), QStringLiteral("not found (--postgres <folder>)"))));
		out(QStringLiteral("Server modules:  %1").arg(orNone(QDir::toNativeSeparators(manager.modulesFolder()), QStringLiteral("not found (--modules <folder>)"))));
		return 0;
	}

	if (command == QLatin1String("add") || command == QLatin1String("set")) {
		if (!need(1, command == QLatin1String("add") ? "add <id> [options]" : "set <id> [options]")) {
			return 1;
		}
		QString id = positional.first();
		ServerProfile s;
		if (command == QLatin1String("add")) {
			if (config.find(id)) {
				return fail(QStringLiteral("There already is a server %1; change it with: %2 set %1").arg(id, g_program));
			}
			if (!ServerConfig::validId(id)) {
				return fail(QStringLiteral("The id must be 1 to 16 lowercase letters, digits and dashes."));
			}
			if (!parser.isSet("kinds")) {
				return fail(QStringLiteral("Say which games the server is for: --kinds ikemen, gba, ds (comma separated) or all"));
			}
			s = config.create(id, QStringList());
		} else {
			const ServerProfile* existing = config.find(id);
			if (!existing) {
				return fail(QStringLiteral("There is no server %1.").arg(id));
			}
			if (ServerHost::runningElsewhere(config.dataDir(*existing))) {
				out(QStringLiteral("The server %1 is running: the changes apply when it next starts.").arg(id));
			}
			s = *existing;
		}
		if (!applyOptions(parser, &s, &error)) {
			return fail(error);
		}
		QString invalid = config.validate(s);
		if (!invalid.isEmpty()) {
			return fail(invalid);
		}
		if (parser.isSet("database") && !s.database.isEmpty() && s.database.section(QLatin1Char('@'), 0, 0).contains(QLatin1Char(':'))) {
			fprintf(stderr, "Note: the database password is now in your shell's history; servers.json keeps it for its owner only.\n");
		}
		if (s.consolePublic) {
			fprintf(stderr, "Warning: the console will be open to other computers over plain HTTP; an SSH tunnel to 127.0.0.1 is safer (docs/hosting.md).\n");
		}
		applyPrograms(parser, &config);
		config.put(s);
		if (!config.save(&error)) {
			return fail(error);
		}
		out(command == QLatin1String("add") ? QStringLiteral("Added the server %1.").arg(id) : QStringLiteral("Changed the server %1.").arg(id));
		printServer(manager, s, false);
		if (manager.nakamaProgram().isEmpty()) {
			out(QStringLiteral("Nakama was not found: download it from https://github.com/heroiclabs/nakama/releases, then: %1 programs --nakama <file>").arg(g_program));
		}
		if (s.managedDatabase() && manager.postgresFolder().isEmpty()) {
			out(QStringLiteral("PostgreSQL was not found: install it, then: %1 programs --postgres <folder of initdb>").arg(g_program));
		}
		return 0;
	}

	if (command == QLatin1String("remove")) {
		if (!need(1, "remove <id> [--delete-data]")) {
			return 1;
		}
		const ServerProfile* s = config.find(positional.first());
		if (!s) {
			return fail(QStringLiteral("There is no server %1.").arg(positional.first()));
		}
		QString data = config.dataDir(*s);
		QString id = s->id;
		if (ServerHost::runningElsewhere(data)) {
			return fail(QStringLiteral("The server %1 is running; stop it first.").arg(id));
		}
		config.remove(id);
		if (!config.save(&error)) {
			return fail(error);
		}
		if (parser.isSet("delete-data") && QFileInfo::exists(data) && !QFileInfo::exists(QDir(data).filePath(ServerHost::markerName()))) {
			out(QStringLiteral("Removed the server %1. Its folder %2 was not made by Nakama Matchmaker, so it was left alone.").arg(id, QDir::toNativeSeparators(data)));
			return 0;
		}
		if (parser.isSet("delete-data")) {
			if (!QDir(data).removeRecursively()) {
				return fail(QStringLiteral("Cannot delete all of %1.").arg(QDir::toNativeSeparators(data)));
			}
			out(QStringLiteral("Removed the server %1 and deleted %2.").arg(id, QDir::toNativeSeparators(data)));
		} else {
			out(QStringLiteral("Removed the server %1. Its folder, with its database, is still in %2.").arg(id, QDir::toNativeSeparators(data)));
		}
		return 0;
	}

	if (command == QLatin1String("show")) {
		if (!need(1, "show <id> [--secrets]")) {
			return 1;
		}
		const ServerProfile* s = config.find(positional.first());
		if (!s) {
			return fail(QStringLiteral("There is no server %1.").arg(positional.first()));
		}
		printServer(manager, *s, parser.isSet("secrets"));
		return 0;
	}

	if (command == QLatin1String("ikemen-profile")) {
		if (!need(2, "ikemen-profile <id> <file> [--game <id>] [--build <build>]")) {
			return 1;
		}
		const ServerProfile* s = config.find(positional.first());
		if (!s) {
			return fail(QStringLiteral("There is no server %1.").arg(positional.first()));
		}
		if (!s->serves(QStringLiteral("ikemen"))) {
			fprintf(stderr, "Note: the server %s is not set up for IKEMEN GO (--kinds).\n", qPrintable(s->id));
		}
		ServerManager::IkemenProfileOptions options;
		options.game = parser.value("game");
		options.build = parser.value("build");
		options.host = parser.value("host").trimmed();
		options.region = parser.value("region").trimmed();
		for (const QString& server : parser.value("stun").split(QLatin1Char(','))) {
			if (!server.trimmed().isEmpty()) {
				options.stun.append(server.trimmed());
			}
		}
		QString host = options.host.isEmpty() ? s->clientHost() : options.host;
		if (ServerManager::localOnly(host)) {
			fprintf(stderr, "Warning: the profile sends players to %s, their own computer. For players elsewhere: %s set %s --public-host <address>, or --host here.\n",
			        qPrintable(host), qPrintable(g_program), qPrintable(s->id));
		}
		QFile file(positional.at(1));
		if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
			return fail(QStringLiteral("%1: %2").arg(positional.at(1), file.errorString()));
		}
		file.write(manager.ikemenProfile(*s, options));
		out(QStringLiteral("Wrote %1: put it in IKEMEN GO's data/online/servers.json.").arg(QDir::toNativeSeparators(positional.at(1))));
		return 0;
	}

	if (command == QLatin1String("run")) {
		QStringList ids = positional;
		if (parser.isSet("all")) {
			ids.clear();
			for (const ServerProfile& s : config.servers()) {
				ids.append(s.id);
			}
		}
		if (ids.isEmpty()) {
			fprintf(stderr, "usage: %s run <id>... | --all\n", qPrintable(g_program));
			return 1;
		}
		for (const QString& id : ids) {
			if (!config.find(id)) {
				return fail(QStringLiteral("There is no server %1.").arg(id));
			}
		}
		int exitCode = 0;
		bool stopping = false;
		auto stopAll = [&]() {
			if (!stopping) {
				stopping = true;
				out(QStringLiteral("Stopping..."));
				manager.stopAll();
			}
			if (manager.active().isEmpty()) {
				QTimer::singleShot(0, QCoreApplication::instance(), &QCoreApplication::quit);
			}
		};
		QObject::connect(&manager, &ServerManager::logLine, [](const QString& id, const QString& line) { out(QStringLiteral("[%1] %2").arg(id, line)); });
		QObject::connect(&manager, &ServerManager::stateChanged, [&](const QString& id, ServerHost::State state) {
			if (state == ServerHost::State::Failed) {
				// one server failing ends the run, so that a service manager can restart it
				exitCode = 1;
				fprintf(stderr, "[%s] %s\n", qPrintable(id), qPrintable(manager.host(id)->error()));
				stopAll();
			} else if (state == ServerHost::State::Stopped && stopping) {
				stopAll();
			} else if (state == ServerHost::State::Stopped) {
				exitCode = 1;   // stopped without being asked
				stopAll();
			}
		});
		installStopHandler([&]() {
			if (stopping) {
				out(QStringLiteral("Already stopping; waiting for the servers."));
			}
			stopAll();
		});
		for (const QString& id : ids) {
			if (!manager.start(id, &error)) {
				fprintf(stderr, "[%s] %s\n", qPrintable(id), qPrintable(error));
				exitCode = 1;
				stopAll();
				break;
			}
			out(QStringLiteral("[%1] log: %2").arg(id, QDir::toNativeSeparators(QDir(config.dataDir(*config.find(id))).filePath(QStringLiteral("server.log")))));
		}
		if (!manager.active().isEmpty()) {
			QCoreApplication::exec();
		}
		manager.stopAllAndWait(30000);
		return exitCode;
	}

	fprintf(stderr, "Unknown server command: %s\n\n%s", qPrintable(command), qPrintable(usage()));
	return 1;
}
