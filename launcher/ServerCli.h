/* SPDX-License-Identifier: MIT */
#pragma once

#include <QString>
#include <QStringList>

/* `nakama-matchmaker [--config <folder>] server <command> ...`, or
 * `nakama-matchmaker-server [--config <folder>] <command> ...`: the community servers from
 * the command line, without a window or a display (docs/hosting.md). args are the
 * arguments after "server"; program is how the usage names the command. Returns the
 * program's exit code. */
int runServerCommand(const QString& configFolder, const QStringList& args, const QString& program);

/* The program's arguments, with the program first, as the user typed them (on Windows,
 * from the wide command line, so that no character is lost). */
QStringList commandLineArguments(int argc, char** argv);

/* Whether a command line asks for the server commands: "server" is its first argument
 * that is neither an option nor an option's value. */
bool isServerCommand(const QStringList& arguments, QString* configFolder, QStringList* rest);

/* The arguments of nakama-matchmaker-server: --config and its folder, then the command. */
void splitServerArguments(const QStringList& arguments, QString* configFolder, QStringList* rest);
