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
 * For mgba and melonds games the launcher finds the match itself, then starts the
 * emulator with the match in NETPLAY_CONNECT (a file with the server, a session, the match
 * and the players in order; docs/launch.md). The emulator joins the match, connects to the
 * other players directly and plays; the launcher stays in the game's room. */
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
	void play(const QString& gameId);   // without a match: offline, or IKEMEN GO's own online menus

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
	bool launch(const Game&, const QProcessEnvironment&, QString* error);
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
};

Q_DECLARE_METATYPE(LauncherCore::State)
