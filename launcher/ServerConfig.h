/* SPDX-License-Identifier: MIT */
#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QMap>
#include <QPair>
#include <QString>
#include <QStringList>

/* What IKEMEN GO players see for their ranked ratings (docs/hosting.md, "IKEMEN GO's ranked
 * levels"). Each player keeps an ordinary Elo rating, which starts at 1000; IKEMEN GO's
 * modules show a level computed from it:
 *
 *     level = start × (top ÷ start) ^ ((Elo − 1000) ÷ 1000)
 *
 * start is the level of a new player, top the level of a player 1000 Elo above a new one.
 * Levels are whole numbers, at least 1 and at most LEVEL_MAX. Without start and top, the
 * level is the Elo rating itself. The modules read the levels from IKEMEN_LEVELS in Nakama's
 * runtime.env (ServerProfile::ikemenLevels), when the server starts; they change no rating. */
struct RankedLevels {
	qint64 start = 0;   // 0 with top 0: none (the default: the plain Elo ratings; a game: the default's)
	qint64 top = 0;     // -1 in either: a value servers.json holds that is no whole number in range
	QString label;      // what the number is called; empty: the default's ("Rating" for the default)

	bool hasRange() const { return start != 0 || top != 0; }
	bool isEmpty() const { return !hasRange() && label.isEmpty(); }
};

/* A community server: a Nakama server that the matchmaker runs for the players of one
 * community, of IKEMEN GO, of GBA games (the mGBA fork), of DS games (the melonDS fork),
 * or of several of them. The matchmaker does not implement the server itself: it runs
 * Nakama, by default with a PostgreSQL database of the server's own, and installs the
 * server modules that each kind of game needs (docs/hosting.md). */
struct ServerProfile {
	QString id;                  // letters, digits and dashes, 1 to 16 (Nakama's node name)
	QString title;               // the community's name, which players see
	QString message;             // a message shown to players when they sign in
	QStringList kinds;           // "ikemen", "mgba", "melonds"
	int port = 7350;             // API (HTTP and WebSocket); Nakama's gRPC API takes port - 1
	int consolePort = 7351;      // Nakama's web console; its gRPC side takes console port - 3
	bool consolePublic = false;  // console open to other computers (otherwise 127.0.0.1 only)
	QString bind;                // address the API listens on; empty: every address
	QString publicHost;          // the address players enter
	int publicPort = 0;          // the port players enter, when a proxy listens on another; 0: port
	bool clientTls = false;      // players connect with TLS (through a proxy that does it); also with a certificate
	QString database;            // empty: a PostgreSQL the matchmaker runs; else user:password@host:port/database
	int databasePort = 54330;    // that PostgreSQL's port, on 127.0.0.1
	QString serverKey;           // what players enter as the server key
	QString httpKey;
	QString sessionKey;
	QString refreshKey;
	QString consoleUser;
	QString consolePassword;
	QString consoleSigningKey;
	QString mfaKey;              // encrypts the console's MFA secrets; 32 characters
	QString databasePassword;    // of the nakama role in the PostgreSQL the matchmaker runs
	QString tlsCertificate;
	QString tlsPrivateKey;
	QString extraModules;        // a folder of the operator's own Lua modules
	QStringList extraArgs;       // more arguments for Nakama
	QString dataDir;             // empty: servers/<id> in the configuration folder
	RankedLevels levels;         // IKEMEN GO: the levels of every game without levels of its own
	QMap<QString, RankedLevels> gameLevels;   // IKEMEN GO: games with levels of their own, by game id

	bool managedDatabase() const { return database.isEmpty(); }
	bool serves(const QString& kind) const { return kinds.contains(kind); }
	/* Every TCP port the server listens on, with what it is for. */
	QList<QPair<int, QString>> ports() const;
	QString kindsLabel() const;
	/* The address players enter: the public address, else the listen address, else this
	 * computer. */
	QString clientHost() const;
	int clientPort() const { return publicPort > 0 ? publicPort : port; }
	bool clientSsl() const { return clientTls || !tlsCertificate.isEmpty(); }
	/* The level settings as IKEMEN GO's modules read them, the JSON value of IKEMEN_LEVELS:
	 * {"default": {start, top, label}, "games": {"<game id>": {start, top, label}}}, each
	 * part only when it is set; empty when nothing is. */
	QByteArray ikemenLevels() const;
	/* The levels a game shows: its own entry, with what it leaves out taken from the
	 * default's, as the modules take it; the default's for a game without one. */
	RankedLevels levelsOf(const QString& game) const;
	/* Whether the Nakama arguments give IKEMEN_LEVELS (--runtime.env IKEMEN_LEVELS=...),
	 * which Nakama prefers to the configuration file's, and so to the level settings. */
	bool levelsInArgs() const;
};

/* servers.json in the configuration folder: the programs the servers need and every
 * server's profile. It holds the servers' secrets, so it is written for its owner only. */
class ServerConfig {
public:
	explicit ServerConfig(const QString& folder);

	QString path() const;
	/* Reads servers.json. A server whose file lacks a key or password gets a new one;
	 * filledSecrets() then says the file should be saved. */
	bool load(QString* error);
	bool save(QString* error) const;
	bool filledSecrets() const { return m_filled; }
	/* Why servers.json could not be read; while it is set, save() refuses to overwrite it. */
	QString loadError() const { return m_loadError; }
	/* Whether another program (the command line, another window) changed servers.json
	 * since it was read or written here. */
	bool changedOnDisk() const;

	QString nakamaPath;     // Nakama's executable; empty: next to this program, then PATH
	QString postgresPath;   // the folder of PostgreSQL's programs; empty: searched
	QString modulesPath;    // the server modules; empty: those installed with this program

	const QList<ServerProfile>& servers() const { return m_servers; }
	const ServerProfile* find(const QString& id) const;
	void put(const ServerProfile&);   // adds, or replaces the server with the same id
	bool remove(const QString& id);

	/* A new server: free ports, new secrets, the given kinds. */
	ServerProfile create(const QString& id, const QStringList& kinds) const;
	/* Why a profile cannot be used, or an empty string. */
	QString validate(const ServerProfile&) const;
	/* The other servers that use one of its ports: they cannot run at the same time. */
	QStringList conflicts(const ServerProfile&) const;
	QString dataDir(const ServerProfile&) const;
	/* Why a folder cannot be a server's data folder (the home folder, a root, the
	 * configuration folder), or an empty string. */
	QString checkDataDir(const QString& folder) const;
	/* Why a new server cannot have its folder: it holds files already (a removed server's
	 * database, whose password the new server would not know). */
	QString folderTaken(const ServerProfile&) const;

	static QString secret();
	/* "ikemen", "gba"/"mgba", "ds"/"melonds"; empty for anything else. */
	static QString kindFromName(const QString&);
	/* A comma-separated list of kinds, or "all". */
	static QStringList parseKinds(const QString&, QString* error);
	static QString kindLabel(const QString& kind);
	static bool validId(const QString&);

	/* The highest level: up to there the modules' numbers (64-bit floating point) hold every
	 * whole number exactly. */
	static const qint64 LEVEL_MAX = 9000000000000000LL;
	static const int LEVEL_LABEL_MAX = 32;     // characters
	static const int LEVEL_GAME_ID_MAX = 114;  // bytes: "ikemen_rating.<game id>" fits Nakama's 128
	static QString defaultLevelLabel() { return QStringLiteral("Rating"); }
	/* A level setting as it is typed: a whole number such as 9001 or 1000000000, or written
	 * with an exponent, such as 1e9; 0 for an empty text. False, with the reason, for
	 * anything else. */
	static bool parseLevel(const QString& text, qint64* value, QString* error);
	/* Why an entry of the level settings is not valid, or an empty string; what names it in
	 * the message ("the ranked levels", "the ranked levels of the game x"). */
	static QString checkLevels(const RankedLevels&, const QString& what);
	/* Why a game id cannot have levels of its own (1 to LEVEL_GAME_ID_MAX bytes, no spaces
	 * or control characters, as IKEMEN GO sends it), or an empty string. */
	static QString checkLevelGame(const QString& game);
	/* A label as the modules keep it: without surrounding spaces. */
	static QString levelLabel(const QString& text) { return text.trimmed(); }
	/* The level of an Elo rating, as the modules compute it, under an entry that levelsOf
	 * gave. */
	static qint64 levelOf(const RankedLevels&, double elo);
	/* A level with thousands separators, as the modules write it: 9,001. */
	static QString levelText(qint64);
	/* What an entry that levelsOf gave shows, in words: "Power level: 9,001 for a new
	 * player, 1,000,000,000 at +1000 Elo". */
	static QString describeLevels(const RankedLevels&);

	/* Writes a file that only its owner can read. */
	static bool writePrivate(const QString& path, const QByteArray& data, QString* error);
	/* Nakama's configuration file for a server (docs/hosting.md). */
	static QByteArray nakamaYaml(const ServerProfile&, const QString& dataDir);
	/* Replaces the settings of a profile that its running server may have been started
	 * with before they changed in servers.json: the port, the listen address, the HTTP key,
	 * the TLS certificate and the games, read from the nakama.yml that the start wrote into
	 * the data folder. False when that file cannot be read or lacks the port or the key. */
	static bool readStarted(const QString& dataDir, ServerProfile*);

private:
	QString m_folder;
	QList<ServerProfile> m_servers;
	bool m_filled = false;
	QString m_loadError;
	mutable QDateTime m_stamp;   // servers.json's time when last read or written here
};
