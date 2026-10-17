/* SPDX-License-Identifier: MIT */
#include "ServerCli.h"

#include "RankedSets.h"
#include "ServerManager.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
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
#include <shellapi.h>
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
	for (int signal : { SIGINT, SIGTERM, SIGHUP }) {
		// a signal the program was started to ignore (nohup ignores SIGHUP) stays ignored
		struct sigaction old = {};
		if (sigaction(signal, nullptr, &old) == 0 && old.sa_handler == SIG_IGN) {
			continue;
		}
		sigaction(signal, &action, nullptr);
	}
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

/* A level option's number: none (or nothing) for no number. */
bool levelOption(const QString& name, const QString& value, qint64* result, QString* error) {
	if (value.trimmed().toLower() == QLatin1String("none")) {
		*result = 0;
		return true;
	}
	if (!ServerConfig::parseLevel(value, result, error)) {
		*error = QStringLiteral("--%1: %2").arg(name, *error);
		return false;
	}
	return true;
}

/* IKEMEN GO's ranked levels: --level-start, --level-top and --level-label set the default,
 * and each --game-level <game>=<start>,<top>[,<label>] the levels of a game of its own
 * (<game>=,,<label> its label alone; <game>= removes them). */
bool applyLevelOptions(const QCommandLineParser& parser, ServerProfile* s, QString* error) {
	if (parser.isSet("level-start") && !levelOption(QStringLiteral("level-start"), parser.value("level-start"), &s->levels.start, error)) {
		return false;
	}
	if (parser.isSet("level-top") && !levelOption(QStringLiteral("level-top"), parser.value("level-top"), &s->levels.top, error)) {
		return false;
	}
	if (parser.isSet("level-label")) {
		s->levels.label = ServerConfig::levelLabel(parser.value("level-label"));
	}
	for (const QString& option : parser.values("game-level")) {
		int equals = option.indexOf(QLatin1Char('='));
		if (equals < 0) {
			*error = QStringLiteral("--game-level takes <game>=<start>,<top>[,<label>] (or <game>= to remove the game's levels), not %1").arg(option);
			return false;
		}
		QString game = option.left(equals).trimmed();
		QString value = option.mid(equals + 1);
		QString bad = ServerConfig::checkLevelGame(game);
		if (!bad.isEmpty()) {
			*error = QStringLiteral("--game-level %1: %2").arg(option, bad);
			return false;
		}
		if (value.trimmed().isEmpty()) {
			if (!s->gameLevels.remove(game)) {
				*error = QStringLiteral("--game-level %1=: the server has no ranked levels of its own for the game %1").arg(game);
				return false;
			}
			continue;
		}
		// the label is the rest, commas and all
		int first = value.indexOf(QLatin1Char(','));
		int second = first < 0 ? -1 : value.indexOf(QLatin1Char(','), first + 1);
		if (first < 0) {
			*error = QStringLiteral("--game-level %1: give <start>,<top>[,<label>] after the game id (both empty for the default's, as in %2=,,<label>)").arg(option, game);
			return false;
		}
		RankedLevels l;
		QString name = QStringLiteral("game-level %1").arg(game);
		if (!levelOption(name, value.left(first), &l.start, error) ||
		    !levelOption(name, second < 0 ? value.mid(first + 1) : value.mid(first + 1, second - first - 1), &l.top, error)) {
			return false;
		}
		if (second >= 0) {
			l.label = ServerConfig::levelLabel(value.mid(second + 1));
		}
		s->gameLevels.insert(game, l);
	}
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
	return applyLevelOptions(parser, s, error);
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
	if (s.levelsInArgs()) {
		out(QStringLiteral("  Ranked levels:  IKEMEN_LEVELS in the Nakama options above"));
	} else if (s.serves(QStringLiteral("ikemen")) || !s.levels.isEmpty() || !s.gameLevels.isEmpty()) {
		auto numbers = [](const RankedLevels& l) {
			return l.hasRange() ? QStringLiteral(" (start %1, top %2)").arg(l.start).arg(l.top) : QString();
		};
		QString unused = s.serves(QStringLiteral("ikemen")) ? QString() : QStringLiteral(" (unused: the server is not for IKEMEN GO)");
		out(QStringLiteral("  Ranked levels:  %1%2%3").arg(ServerConfig::describeLevels(s.levelsOf(QString())), numbers(s.levels), unused));
		for (auto it = s.gameLevels.constBegin(); it != s.gameLevels.constEnd(); ++it) {
			QString own = it->hasRange() ? numbers(*it) : QStringLiteral(" (the default's start and top)");
			out(QStringLiteral("    game %1: %2%3").arg(it.key(), ServerConfig::describeLevels(s.levelsOf(it.key())), own));
		}
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

/* Calls one of IKEMEN GO's moderator RPCs on a running server and waits for the answer. */
bool moderate(ServerManager& manager, const QString& id, const QString& rpc, const QJsonObject& payload, QJsonObject* answer, QString* error) {
	QEventLoop loop;
	bool ok = false;
	manager.moderatorRpc(id, rpc, payload, &loop, [&](const QJsonObject& a, const QString& e) {
		*answer = a;
		*error = e;
		ok = e.isEmpty();
		loop.quit();
	});
	loop.exec();
	return ok;
}

/* A ranked set, read from the server, to name its players by their usernames. */
bool readRankedSet(ServerManager& manager, const QString& id, const QString& matchId, RankedSet* set, QString* error) {
	QJsonObject answer;
	QJsonObject payload { { QStringLiteral("match_id"), matchId }, { QStringLiteral("status"), QStringLiteral("all") } };
	if (!moderate(manager, id, QStringLiteral("ikemen_ranked_list"), payload, &answer, error)) {
		return false;
	}
	QJsonArray sets = answer.value(QStringLiteral("sets")).toArray();
	if (sets.isEmpty()) {
		*error = QStringLiteral("The server %1 has no ranked set %2.").arg(id, matchId);
		return false;
	}
	*set = RankedSet::fromJson(sets.first().toObject());
	return true;
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
	"IKEMEN GO's ranked sets, on a server that runs (here or in another program):\n"
	"  ranked <id>               the disputed sets (--status open|settled|void|all, --match)\n"
	"  ranked-replay <id> <match id> <file or folder>  save a set's replay (--player <user>, --force)\n"
	"  ranked-settle <id> <match id> --winner <user> | --void  decide a set (--note)\n"
	"\n"
	"Run '%1 <command> --help' for a command's options.\n").arg(g_program);
}

}

QStringList commandLineArguments(int argc, char** argv) {
	QStringList out;
#ifdef Q_OS_WIN
	Q_UNUSED(argc);
	Q_UNUSED(argv);
	int count = 0;
	LPWSTR* wide = CommandLineToArgvW(GetCommandLineW(), &count);
	if (wide) {
		for (int i = 0; i < count; ++i) {
			out.append(QString::fromWCharArray(wide[i]));
		}
		LocalFree(wide);
		return out;
	}
#endif
	for (int i = 0; i < argc; ++i) {
		out.append(QString::fromLocal8Bit(argv[i]));
	}
	return out;
}

bool isServerCommand(const QStringList& arguments, QString* configFolder, QStringList* rest) {
	// the launcher's options that take a value
	static const QStringList withValue { QStringLiteral("config"), QStringLiteral("name"), QStringLiteral("device"),
		                                 QStringLiteral("host"), QStringLiteral("port"), QStringLiteral("auto") };
	for (int i = 1; i < arguments.size(); ++i) {
		QString arg = arguments.at(i);
		if (arg.startsWith(QLatin1Char('-'))) {
			QString name = arg.mid(arg.startsWith(QLatin1String("--")) ? 2 : 1);
			QString value;
			bool inlineValue = name.contains(QLatin1Char('='));
			if (inlineValue) {
				value = name.section(QLatin1Char('='), 1);
				name = name.section(QLatin1Char('='), 0, 0);
			}
			if (withValue.contains(name) && !inlineValue && i + 1 < arguments.size()) {
				value = arguments.at(++i);
			}
			if (name == QLatin1String("config")) {
				*configFolder = value;
			}
			continue;
		}
		if (arg != QLatin1String("server")) {
			return false;
		}
		*rest = arguments.mid(i + 1);
		return true;
	}
	return false;
}

void splitServerArguments(const QStringList& arguments, QString* configFolder, QStringList* rest) {
	rest->clear();
	for (int i = 1; i < arguments.size(); ++i) {
		const QString& arg = arguments.at(i);
		if (rest->isEmpty() && (arg == QLatin1String("--config") || arg == QLatin1String("-config")) && i + 1 < arguments.size()) {
			*configFolder = arguments.at(++i);
		} else if (rest->isEmpty() && arg.startsWith(QLatin1String("--config="))) {
			*configFolder = arg.mid(9);
		} else {
			rest->append(arg);
		}
	}
}

int runServerCommand(const QString& configFolder, const QStringList& givenArgs, const QString& program) {
	g_program = program;
	// --config may also come after "server", as the usage shows it
	QString folderOption = configFolder;
	QStringList args;
	splitServerArguments(QStringList { program } + givenArgs, &folderOption, &args);
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
		parser.addOption({ "level-start", "IKEMEN GO's ranked levels: the level of a new player, such as 9001 (none: the plain Elo ratings, with --level-top none)", "number" });
		parser.addOption({ "level-top", "the level of a player 1000 Elo above a new one, such as 1e9 (none: see --level-start)", "number" });
		parser.addOption({ "level-label", "what players see the level called, such as \"Power level\" (empty: Rating)", "text" });
		parser.addOption({ "game-level", "a game's own levels, by its IKEMEN GO game id (repeat for several; <game>=,,<label> names them only; <game>= removes them)",
		                   "game=start,top[,label]" });
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
	if (command == QLatin1String("ranked")) {
		parser.addOption({ "status", "the sets to list: disputed (default), open, settled, void or all", "status" });
		parser.addOption({ "limit", "at most this many sets (1 to 100, default 20)", "count" });
		parser.addOption({ "cursor", "the next page: the cursor that the previous page printed", "cursor" });
		parser.addOption({ "match", "only this set (its match id)", "match id" });
		parser.addOption({ "json", "print the server's answer as JSON" });
	}
	if (command == QLatin1String("ranked-replay")) {
		parser.addOption({ "player", "whose replay (user id or username; default: the first complete one)", "user" });
		parser.addOption({ "force", "replace a file of the same name" });
	}
	if (command == QLatin1String("ranked-settle")) {
		parser.addOption({ "winner", "the player who won the set (user id or username)", "user" });
		parser.addOption({ "void", "void the set: nobody's rating changes" });
		parser.addOption({ "note", "a note kept on the set's record (at most 500 characters)", "text" });
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

	QString folder = folderOption;
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
			if (parser.isSet("data-dir")) {
				s.dataDir = QFileInfo(parser.value("data-dir")).absoluteFilePath();
			}
			QString taken = config.folderTaken(s);
			if (!taken.isEmpty()) {
				return fail(taken);
			}
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

	if (command == QLatin1String("ranked")) {
		if (!need(1, "ranked <id> [--status <status>] [--limit <count>] [--cursor <cursor>] [--match <match id>] [--json]")) {
			return 1;
		}
		QString id = positional.first();
		if (!config.find(id)) {
			return fail(QStringLiteral("There is no server %1.").arg(id));
		}
		QString status = parser.isSet("status") ? parser.value("status").trimmed().toLower() : QStringLiteral("disputed");
		static const QStringList statuses { QStringLiteral("disputed"), QStringLiteral("open"), QStringLiteral("settled"), QStringLiteral("void"),
			                                QStringLiteral("all") };
		if (!statuses.contains(status)) {
			return fail(QStringLiteral("--status takes disputed, open, settled, void or all"));
		}
		int limit = 20;
		if (!number(parser, "limit", &limit, &error)) {
			return fail(error);
		}
		if (limit < 1 || limit > 100) {
			return fail(QStringLiteral("--limit takes 1 to 100"));
		}
		QJsonObject payload { { QStringLiteral("status"), status }, { QStringLiteral("limit"), limit } };
		if (parser.isSet("cursor")) {
			payload.insert(QStringLiteral("cursor"), parser.value("cursor"));
		}
		if (parser.isSet("match")) {
			payload.insert(QStringLiteral("match_id"), parser.value("match").trimmed());
			if (!parser.isSet("status")) {
				payload.insert(QStringLiteral("status"), QStringLiteral("all"));
			}
		}
		QJsonObject answer;
		if (!moderate(manager, id, QStringLiteral("ikemen_ranked_list"), payload, &answer, &error)) {
			return fail(error);
		}
		if (parser.isSet("json")) {
			fprintf(stdout, "%s", QJsonDocument(answer).toJson(QJsonDocument::Indented).constData());
			return 0;
		}
		QJsonArray sets = answer.value(QStringLiteral("sets")).toArray();
		QString which = payload.value(QStringLiteral("status")).toString();
		if (sets.isEmpty()) {
			out(parser.isSet("match") ? QStringLiteral("The server %1 has no ranked set %2%3.")
			                                .arg(id, parser.value("match").trimmed(), which == QLatin1String("all") ? QString() : QStringLiteral(" that is ") + which)
			    : which == QLatin1String("all") ? QStringLiteral("The server %1 has no ranked sets%2.").arg(id, parser.isSet("cursor") ? QStringLiteral(" after that cursor") : QString())
			                                     : QStringLiteral("The server %1 has no %2 ranked sets%3.").arg(id, which, parser.isSet("cursor") ? QStringLiteral(" after that cursor") : QString()));
			return 0;
		}
		if (!parser.isSet("match")) {
			out(which == QLatin1String("all") ? QStringLiteral("Ranked sets on %1, newest first:").arg(id)
			                                  : QStringLiteral("%1 ranked sets on %2, newest first:").arg(which.left(1).toUpper() + which.mid(1), id));
		}
		for (int i = 0; i < sets.size(); ++i) {
			if (i > 0 || !parser.isSet("match")) {
				out(QString());
			}
			for (const QString& line : RankedSet::fromJson(sets.at(i).toObject()).describe()) {
				out(line);
			}
		}
		QString cursor = answer.value(QStringLiteral("cursor")).toString();
		if (!cursor.isEmpty()) {
			out(QString());
			out(QStringLiteral("More: %1 ranked %2 --status %3 --limit %4 --cursor %5").arg(g_program, id, which).arg(limit).arg(cursor));
		}
		return 0;
	}

	if (command == QLatin1String("ranked-replay")) {
		if (!need(3, "ranked-replay <id> <match id> <file or folder> [--player <user id or name>] [--force]")) {
			return 1;
		}
		QString id = positional.at(0);
		QString matchId = positional.at(1).trimmed();
		if (!config.find(id)) {
			return fail(QStringLiteral("There is no server %1.").arg(id));
		}
		// a folder (one that exists, or a name that ends with a separator, which is made),
		// or a replay file's name; anything else is most likely a folder not made yet
		QString path = positional.at(2);
		bool folder = QFileInfo(path).isDir() || path.endsWith(QLatin1Char('/')) || path.endsWith(QDir::separator());
		if (!folder && !path.endsWith(QLatin1String(".replay"), Qt::CaseInsensitive)) {
			return fail(QStringLiteral("%1 is no folder, and no replay file's name: give a folder (one that exists, or a name that ends with %2), "
			                           "or a file name that ends with .replay.")
			                .arg(QDir::toNativeSeparators(path), QString(QDir::separator())));
		}
		if (!folder && QFileInfo::exists(path) && !parser.isSet("force")) {
			return fail(QStringLiteral("%1 exists already: --force replaces it.").arg(QDir::toNativeSeparators(path)));
		}
		QJsonObject payload { { QStringLiteral("match_id"), matchId } };
		RankedSet set;
		bool known = false;
		if (parser.isSet("player")) {
			if (!readRankedSet(manager, id, matchId, &set, &error)) {
				return fail(error);
			}
			known = true;
			QString player = set.findPlayer(parser.value("player"), &error);
			if (player.isEmpty()) {
				return fail(error);
			}
			payload.insert(QStringLiteral("user_id"), player);
		}
		QJsonObject answer;
		if (!moderate(manager, id, QStringLiteral("ikemen_ranked_replay"), payload, &answer, &error)) {
			return fail(error);
		}
		QByteArray replay = QByteArray::fromBase64(answer.value(QStringLiteral("replay")).toString().toLatin1());
		if (replay.isEmpty()) {
			return fail(QStringLiteral("The server %1 sent no replay for the set %2.").arg(id, matchId));
		}
		if (folder) {
			if (!QDir().mkpath(path)) {
				return fail(QStringLiteral("Cannot make the folder %1.").arg(QDir::toNativeSeparators(path)));
			}
			path = QDir(path).filePath(RankedSet::safeFileName(answer.value(QStringLiteral("file_name")).toString()));
		}
		if (QFileInfo::exists(path) && !parser.isSet("force")) {
			return fail(QStringLiteral("%1 exists already: --force replaces it.").arg(QDir::toNativeSeparators(path)));
		}
		// written whole, or not at all: a file replaced stays as it was until the new one is
		// complete (flush first: commit does not notice that writing the buffer failed)
		QSaveFile file(path);
		if (!file.open(QIODevice::WriteOnly) || file.write(replay) != replay.size() || !file.flush() || !file.commit()) {
			return fail(QStringLiteral("Cannot write %1: %2").arg(QDir::toNativeSeparators(path), file.errorString()));
		}
		if (!known) {
			QString ignored;
			known = readRankedSet(manager, id, matchId, &set, &ignored);
		}
		QString player = answer.value(QStringLiteral("user_id")).toString();
		int matches = answer.value(QStringLiteral("matches")).toInt();
		out(QStringLiteral("Wrote %1: %2's replay of the set %3, %4 match%5, %6, %7.")
		        .arg(QDir::toNativeSeparators(path), known ? set.name(player) : player, matchId)
		        .arg(matches)
		        .arg(matches == 1 ? QString() : QStringLiteral("es"))
		        .arg(RankedSet::sizeLabel(replay.size()),
		             answer.value(QStringLiteral("complete")).toBool() ? QStringLiteral("complete")
		                                                               : QStringLiteral("incomplete (the player left, a stream did not end, or it reached the size limit)")));
		out(QStringLiteral("Watch it with IKEMEN GO, with the set's game and build: put it in save/replays and open WATCH MODE > REPLAY, or run Ikemen_GO -replay <file>."));
		return 0;
	}

	if (command == QLatin1String("ranked-settle")) {
		if (!need(2, "ranked-settle <id> <match id> (--winner <user id or name> | --void) [--note <text>]")) {
			return 1;
		}
		QString id = positional.at(0);
		QString matchId = positional.at(1).trimmed();
		if (!config.find(id)) {
			return fail(QStringLiteral("There is no server %1.").arg(id));
		}
		if (parser.isSet("winner") == parser.isSet("void")) {
			return fail(QStringLiteral("Give either --winner <user id or name> or --void."));
		}
		QString note = parser.value("note");
		if (note.toUcs4().size() > 500) {   // characters, as the server counts them, not UTF-16 units
			return fail(QStringLiteral("--note takes at most 500 characters."));
		}
		RankedSet set;
		if (!readRankedSet(manager, id, matchId, &set, &error)) {
			return fail(error);
		}
		QJsonObject payload { { QStringLiteral("match_id"), set.matchId } };
		QString winner;
		if (parser.isSet("winner")) {
			winner = set.findPlayer(parser.value("winner"), &error);
			if (winner.isEmpty()) {
				return fail(error);
			}
			payload.insert(QStringLiteral("winner"), winner);
		} else {
			payload.insert(QStringLiteral("void"), true);
		}
		if (parser.isSet("note")) {
			payload.insert(QStringLiteral("note"), note);
		}
		QJsonObject answer;
		if (!moderate(manager, id, QStringLiteral("ikemen_ranked_settle"), payload, &answer, &error)) {
			return fail(error);
		}
		if (answer.value(QStringLiteral("status")).toString() == QLatin1String("void")) {
			out(QStringLiteral("Voided the set %1 (%2): nobody's rating changed.").arg(set.matchId, set.playersLabel()));
		} else {
			out(QStringLiteral("Settled the set %1 (%2): %3 won, and both players' ratings changed once.").arg(set.matchId, set.playersLabel(), set.name(winner)));
		}
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
			if (stopping) {
				break;   // a server has failed already: the others are not started
			}
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
