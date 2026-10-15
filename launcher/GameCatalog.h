/* SPDX-License-Identifier: MIT */
#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

class Settings;

/* One game in the launcher's list.
 *
 * mgba games name a netplay profile of the mGBA fork; melonds games name a DS ROM (kept in
 * the profile field too). The launcher matches the players of both on the game's identity,
 * which the emulator computes (`--netplay-identity <profile or ROM>`): for mGBA a hash of
 * the build and every ROM and save of the profile, for melonDS a hash of the ROM and of its
 * online protocol. Only players whose identities are equal can play each other.
 *
 * ikemen and melonds games can also start the program signed in to the launcher's server
 * with the launcher's account (a server profile file), so that its own online menus make
 * the matches: IKEMEN GO's quick match and lobbies, melonDS's game channels. For IKEMEN GO
 * that is the only way, on its game's pool (game and build). */
struct Game {
	QString id;
	QString title;
	QString kind;             // "mgba", "melonds" or "ikemen"
	QString profile;          // mgba: the netplay profile; melonds: the ROM
	int players = 2;          // mgba: players per match; melonds: always 2
	QString ikemenGame;       // ikemen: the game id of its Nakama pool
	QString ikemenBuild;      // ikemen: the build of its Nakama pool
	QString workdir;          // ikemen: IKEMEN GO's folder
	QStringList args;         // extra arguments for the program
	QStringList command;      // optional: program and arguments instead of the configured emulator
	QStringList identityCommand;   // optional: prints the identity; {profile} is replaced

	QString kindLabel() const;
	bool matchesInLauncher() const { return kind == QLatin1String("mgba") || kind == QLatin1String("melonds"); }
	/* Whether the program has online menus of its own, which the launcher's Play online
	 * opens with the launcher's server and account. */
	bool playsOnlineItself() const { return kind == QLatin1String("ikemen") || kind == QLatin1String("melonds"); }
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
