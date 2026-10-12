/* SPDX-License-Identifier: MIT
 * Nakama Matchmaker: finds online matches for mGBA, melonDS and IKEMEN GO games through a
 * Nakama server and starts the game. */
#include "GameCatalog.h"
#include "LauncherCore.h"
#include "LauncherWindow.h"
#include "ServerCli.h"
#include "ServerManager.h"
#include "Settings.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QStandardPaths>
#include <QTimer>

#include <cstdio>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

int main(int argc, char** argv) {
	// the arguments as given: on X11, QApplication takes -name/--name (an X11 option) for itself
	QStringList arguments = commandLineArguments(argc, argv);
	QString serverConfig;
	QStringList serverArgs;
	if (isServerCommand(arguments, &serverConfig, &serverArgs)) {
		// the community servers' commands need no window, nor a display (docs/hosting.md)
#ifdef Q_OS_WIN
		// a program with a window has no console of its own on Windows: use the one it was
		// started from, for the output that is not redirected elsewhere
		// (nakama-matchmaker-server is the console program for this)
		auto unset = [](DWORD which) {
			HANDLE h = GetStdHandle(which);
			return h == nullptr || h == INVALID_HANDLE_VALUE || GetFileType(h) == FILE_TYPE_UNKNOWN;
		};
		bool out = unset(STD_OUTPUT_HANDLE);
		bool err = unset(STD_ERROR_HANDLE);
		if ((out || err) && AttachConsole(ATTACH_PARENT_PROCESS)) {
			if (out) {
				freopen("CONOUT$", "w", stdout);
			}
			if (err) {
				freopen("CONOUT$", "w", stderr);
			}
		}
#endif
		QCoreApplication app(argc, argv);
		QCoreApplication::setApplicationName(QStringLiteral("nakama-matchmaker"));
		QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));
		return runServerCommand(serverConfig, serverArgs, QStringLiteral("nakama-matchmaker server"));
	}

	QApplication app(argc, argv);
	// the configuration folder: ~/.config/nakama-matchmaker, %LOCALAPPDATA%\nakama-matchmaker,
	// ~/Library/Preferences/nakama-matchmaker
	QApplication::setApplicationName(QStringLiteral("nakama-matchmaker"));
	QApplication::setApplicationDisplayName(QStringLiteral("Nakama Matchmaker"));
	QApplication::setApplicationVersion(QStringLiteral("0.1.0"));

	QCommandLineParser parser;
	parser.setApplicationDescription(QStringLiteral("Finds online matches for mGBA, melonDS and IKEMEN GO games through a Nakama server."));
	parser.addHelpOption();
	parser.addVersionOption();
	parser.addOption({ "config", "configuration folder (settings.ini and games.json)", "folder" });
	parser.addOption({ "name", "player name (saved)", "name" });
	parser.addOption({ "device", "device id to sign in with (saved)", "id" });
	parser.addOption({ "host", "Nakama server host (saved)", "host" });
	parser.addOption({ "port", "Nakama server port (saved)", "port" });
	parser.addOption({ "auto", "without a window: sign in, find a match for this game, play it and exit", "game" });
	parser.addPositionalArgument(QStringLiteral("server"), QStringLiteral("'server <command>' runs community servers without a window; 'server --help' lists the commands"), QStringLiteral("[server <command>]"));
	// Qt's own options (-platform, -style, ...) are gone from app's arguments, and on X11
	// --name with them: the player name comes from the arguments as given
	parser.process(app);
	QString name;
	bool nameGiven = parser.isSet("name");
	if (nameGiven) {
		name = parser.value("name");
	}
	for (int i = 1; i < arguments.size() && !nameGiven; ++i) {
		const QString& arg = arguments.at(i);
		if ((arg == QLatin1String("--name") || arg == QLatin1String("-name")) && i + 1 < arguments.size()) {
			name = arguments.at(i + 1);
			nameGiven = true;
		} else if (arg.startsWith(QLatin1String("--name=")) || arg.startsWith(QLatin1String("-name="))) {
			name = arg.section(QLatin1Char('='), 1);
			nameGiven = true;
		}
	}

	QString folder = parser.value("config");
	if (folder.isEmpty()) {
		folder = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
	}
	Settings settings(folder);
	if (nameGiven) {
		settings.setName(name);
	}
	if (parser.isSet("device")) {
		settings.setDeviceId(parser.value("device"));
	}
	if (parser.isSet("host") || parser.isSet("port")) {
		NetplayOnline::ServerConfig server = settings.server();
		if (parser.isSet("host")) {
			server.host = parser.value("host");
		}
		if (parser.isSet("port")) {
			server.port = parser.value("port").toInt();
		}
		settings.setServer(server);
	}
	GameCatalog catalog(QDir(folder).filePath(QStringLiteral("games.json")));
	QString error;
	if (!catalog.load(&error)) {
		fprintf(stderr, "%s\n", qPrintable(error));
	}
	LauncherCore core(&settings, &catalog);

	if (parser.isSet("auto")) {
		// for tests and scripts: one match, then exit with the game's exit code
		QString gameId = parser.value("auto");
		if (!catalog.find(gameId)) {
			fprintf(stderr, "no game %s in %s\n", qPrintable(gameId), qPrintable(folder));
			return 1;
		}
		int exitCode = 1;
		bool searched = false;
		QObject::connect(&core, &LauncherCore::status, [](const QString& text) {
			fprintf(stdout, "launcher: %s\n", qPrintable(text));
			fflush(stdout);
		});
		QObject::connect(&core, &LauncherCore::stateChanged, [&](LauncherCore::State state) {
			if (state == LauncherCore::State::Online && !searched) {
				searched = true;
				core.enterRoom(gameId);
				core.findMatch(gameId);
				if (core.state() != LauncherCore::State::Matchmaking && core.state() != LauncherCore::State::Playing) {
					QTimer::singleShot(0, &app, &QCoreApplication::quit);
				}
			} else if (state == LauncherCore::State::SignedOut) {
				// the sign-in failed, or the server went away
				QTimer::singleShot(0, &app, &QCoreApplication::quit);
			}
		});
		QObject::connect(&core, &LauncherCore::gameFinished, [&](const QString&, int code) {
			exitCode = code;
			QTimer::singleShot(0, &app, &QCoreApplication::quit);
		});
		core.signIn();
		app.exec();
		return exitCode;
	}

	ServerManager servers(folder);
	if (!servers.load(&error)) {
		fprintf(stderr, "%s\n", qPrintable(error));   // the servers' window shows it too, and saves nothing
	} else if (servers.config().filledSecrets() && !servers.save(&error)) {
		fprintf(stderr, "%s\n", qPrintable(error));
	}
	LauncherWindow window(&settings, &catalog, &core, &servers);
	window.show();
	return app.exec();
}
