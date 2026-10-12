/* SPDX-License-Identifier: MIT
 * nakama-matchmaker-server: Nakama Matchmaker's community servers on the command line,
 * without the launcher's window or Qt Widgets, for headless computers and for Windows
 * consoles (docs/hosting.md). `nakama-matchmaker-server <command>` does what
 * `nakama-matchmaker server <command>` does. */
#include "ServerCli.h"

#include <QCoreApplication>

int main(int argc, char** argv) {
	QCoreApplication app(argc, argv);
	QCoreApplication::setApplicationName(QStringLiteral("nakama-matchmaker"));
	QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));
	QString config;
	QStringList args;
	splitServerArguments(argc, argv, &config, &args);
	return runServerCommand(config, args, QStringLiteral("nakama-matchmaker-server"));
}
