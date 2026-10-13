/* SPDX-License-Identifier: MIT */
#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

class Settings;

/* One game in the launcher's list.
 *
 * mgba and melonds games name a netplay profile of that emulator. Players are matched on
 * the profile's identity, a hash of the emulator build and every ROM and save, which the
 * launcher asks the emulator for (`--netplay-identity profile`); only players whose
 * identities are equal can play each other.
 *
 * ikemen games start IKEMEN GO with the launcher's server and account; matches are then
 * made from IKEMEN GO's own online menus, on that game's pool (game and build). */
struct Game {
	QString id;
	QString title;
	QString kind;             // "mgba", "melonds" or "ikemen"
	QString profile;          // mgba, melonds
	int players = 2;          // mgba, melonds: players per match
	QString ikemenGame;       // ikemen: the game id of its Nakama pool
	QString ikemenBuild;      // ikemen: the build of its Nakama pool
	QString workdir;          // ikemen: IKEMEN GO's folder
	QStringList args;         // extra arguments for the program
	QStringList command;      // optional: program and arguments instead of the configured emulator
	QStringList identityCommand;   // optional: prints the identity; {profile} is replaced

	QString kindLabel() const;
	bool matchesInLauncher() const { return kind == QLatin1String("mgba"); }
};

class GameCatalog {
public:
	explicit GameCatalog(const QString& path);

	bool load(QString* error);
	bool save(QString* error) const;

	const QList<Game>& games() const { return m_games; }
	const Game* find(const QString& id) const;
	void put(const Game&);   // adds, or replaces the game with the same id
	void remove(const QString& id);

	/* The emulator's identity for a game's profile. Blocks while the emulator computes it
	 * (it reads every ROM and save). With cached, an answer from earlier in this run may
	 * be returned, as long as the emulator and the profile have not changed since; a ROM
	 * replaced in place goes unnoticed then, so matchmaking asks for a fresh one. */
	QString identity(const Game&, Settings*, QString* error, bool cached = false);

private:
	QString m_path;
	QList<Game> m_games;
	QHash<QString, QString> m_identities;   // stamp -> identity
};
