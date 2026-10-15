/* SPDX-License-Identifier: MIT */
#pragma once

#include "GameCatalog.h"

#include <NakamaClient.h>

#include <QHash>
#include <QObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QTimer>

#include <memory>

class Settings;

/* The launcher's work, without its window: signing in, the chat room of each game,
 * matchmaking, and starting the emulator or IKEMEN GO.
 *
 * For mgba and melonds games the launcher finds the match itself (Find a match), then
 * starts the emulator with the match in NETPLAY_CONNECT (a file with the server, a session,
 * the match and the players in order; docs/launch.md). The emulator joins the match,
 * connects to the other players directly and plays; the launcher stays in the game's room.
 *
 * For ikemen and melonds games, Play online starts the program with a server profile (a
 * file with the launcher's server and account) instead, and the program's own online
 * menus make the matches: IKEMEN GO's quick match and lobbies, melonDS's game channels. */
class LauncherCore : public QObject {
	Q_OBJECT

public:
	enum class State { SignedOut, SigningIn, Online, Matchmaking, Playing };

	/* What a community server that Nakama Matchmaker runs says about itself (its
	 * community_info RPC, docs/hosting.md); empty for other servers. */
	struct Community {
		QString title;
		QString message;
		QStringList kinds;
		QString kindsLabel() const;
	};

	LauncherCore(Settings*, GameCatalog*, QObject* parent = nullptr);
	~LauncherCore() override;

	State state() const { return m_state; }
	NetplayOnline::NakamaClient* client() const { return m_client.get(); }
	QString userId() const;
	const Community& community() const { return m_community; }

	void signIn();
	void signOut();
	void enterRoom(const QString& gameId);
	QString room() const { return m_roomGame; }
	QStringList roomMembers() const;
	void sendChat(const QString& text);
	void findMatch(const QString& gameId);
	void cancelMatch();
	void play(const QString& gameId);         // without a match (IKEMEN GO still gets the server profile)
	void playOnline(const QString& gameId);   // ikemen, melonds: signed in to the launcher's server

signals:
	void stateChanged(LauncherCore::State);
	void status(const QString& text);
	void chatReceived(const QString& name, const QString& text);
	void membersChanged();
	void gameStarted(const QString& gameId);
	void gameFinished(const QString& gameId, int exitCode);
	void communityChanged();

private:
	void setState(State);
	void onMatched(const NetplayOnline::Matched&);
	/* Hands the match to the emulator (NETPLAY_CONNECT), with a session of this account. */
	void startMatch(const NetplayOnline::Matched&, const NetplayOnline::Session&);
	/* Starts the game; handoff is the temporary folder of the files it is given, which is
	 * deleted when the game exits. */
	bool launch(const Game&, const QProcessEnvironment&, std::unique_ptr<QTemporaryDir> handoff, QString* error);
	/* Writes a server profile for IKEMEN GO or melonDS (IKEMEN GO's servers.json format)
	 * into a new temporary folder: the launcher's server, account and STUN servers. */
	std::unique_ptr<QTemporaryDir> writeServerProfile(const Game&, QString* path, QString* error) const;
	QStringList programFor(const Game&, QString* error) const;
	QString roomNameFor(const Game&);

	Settings* m_settings;
	GameCatalog* m_catalog;
	std::unique_ptr<NetplayOnline::NakamaClient> m_client;
	State m_state = State::SignedOut;
	QString m_roomGame;
	QString m_roomName;
	QString m_roomChannel;
	QHash<QString, QString> m_members;   // user id -> name
	QString m_matchGame;
	QString m_matchIdentity;
	std::unique_ptr<QProcess> m_process;
	std::unique_ptr<QTemporaryDir> m_handoff;
	QTimer m_ping;
	Community m_community;
	// signs in again when a match is found, for a new session that the emulator gets
	std::unique_ptr<NetplayOnline::NakamaClient> m_fresh;
};

Q_DECLARE_METATYPE(LauncherCore::State)
