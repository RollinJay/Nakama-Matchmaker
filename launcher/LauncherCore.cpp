/* SPDX-License-Identifier: MIT */
#include "LauncherCore.h"

#include "Settings.h"

#include <ConnectInfo.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

using namespace NetplayOnline;

LauncherCore::LauncherCore(Settings* settings, GameCatalog* catalog, QObject* parent)
	: QObject(parent)
	, m_settings(settings)
	, m_catalog(catalog) {
	qRegisterMetaType<LauncherCore::State>();
	m_ping.setInterval(5000);
	connect(&m_ping, &QTimer::timeout, this, [this]() {
		if (m_client && m_client->isSocketOpen()) {
			m_client->ping();
		}
	});
}

LauncherCore::~LauncherCore() {
	// the client and the game outlive the members their handlers use while this is destroyed
	if (m_process) {
		m_process->disconnect(this);
	}
	if (m_client) {
		m_client->disconnect(this);
	}
	if (m_fresh) {
		m_fresh->disconnect(this);
	}
}

void LauncherCore::setState(State state) {
	if (m_state != state) {
		m_state = state;
		emit stateChanged(state);
	}
}

QString LauncherCore::Community::kindsLabel() const {
	QStringList labels;
	for (const QString& kind : kinds) {
		Game g;
		g.kind = kind;
		labels.append(g.kindLabel());
	}
	return labels.join(QStringLiteral(", "));
}

QString LauncherCore::userId() const {
	return m_client ? m_client->session().userId : QString();
}

/* A game's room is named after what the players must share to play each other, not after
 * the id in each player's own list: the identity for the emulators (their build, the ROMs
 * and the saves) and the pool (game and build) for IKEMEN GO. Nakama room names have at
 * most 64 characters. */
QString LauncherCore::roomNameFor(const Game& game) {
	QString name;
	if (game.matchesInLauncher()) {
		QString error;
		QString identity = m_catalog->identity(game, m_settings, &error, true);
		if (!identity.isEmpty()) {
			name = game.kind + QLatin1Char('.') + identity.left(24);
		}
	} else if (game.kind == QLatin1String("ikemen")) {
		name = QStringLiteral("ikemen.%1.%2").arg(game.ikemenGame, game.ikemenBuild);
	}
	if (name.isEmpty()) {
		name = QStringLiteral("game.") + game.id;   // the identity is unknown for now
	}
	return name.left(64);
}

void LauncherCore::signIn() {
	if (m_state != State::SignedOut) {
		return;
	}
	m_client.reset(new NakamaClient(m_settings->server()));
	NakamaClient* client = m_client.get();
	connect(client, &NakamaClient::failed, this, [this](const QString& error) {
		emit status(tr("Sign-in failed: %1").arg(error));
		signOut();
	});
	connect(client, &NakamaClient::signedIn, this, [this, client]() {
		emit status(tr("Signed in as %1. Connecting...").arg(client->session().username));
		client->connectSocket();
	});
	connect(client, &NakamaClient::socketOpened, this, [this, client]() {
		emit status(tr("Online as %1.").arg(client->session().username));
		// a community server says which community it is for; other servers have no such RPC
		client->rpc(QStringLiteral("community_info"), QJsonObject(), [this, client](bool ok, const QByteArray& reply, const QString&) {
			if (client != m_client.get()) {
				return;
			}
			if (!ok) {
				// not a community server: nothing of an earlier one stays
				if (!m_community.title.isEmpty() || !m_community.kinds.isEmpty()) {
					m_community = Community();
					emit communityChanged();
				}
				return;
			}
			QJsonObject o = QJsonDocument::fromJson(reply).object();
			m_community.title = o.value(QStringLiteral("title")).toString().left(64);
			m_community.message = o.value(QStringLiteral("message")).toString().left(500);
			m_community.kinds.clear();
			for (const QJsonValue& v : o.value(QStringLiteral("kinds")).toArray()) {
				m_community.kinds.append(v.toString());
			}
			if (!m_community.title.isEmpty() && m_state != State::SignedOut) {
				emit status(tr("Online as %1 on %2.").arg(client->session().username, m_community.title));
			}
			emit communityChanged();
		});
		m_ping.start();
		client->ping();
		if (!m_roomGame.isEmpty()) {
			QString game = m_roomGame;
			m_roomGame.clear();
			enterRoom(game);
		}
		setState(State::Online);
	});
	connect(client, &NakamaClient::socketClosed, this, [this](const QString& reason) {
		emit status(tr("Disconnected: %1").arg(reason));
		m_ping.stop();
		if (!m_community.title.isEmpty() || !m_community.kinds.isEmpty()) {
			m_community = Community();
			emit communityChanged();
		}
		m_members.clear();
		emit membersChanged();
		if (m_state != State::Playing) {
			setState(State::SignedOut);
		}
	});
	connect(client, &NakamaClient::serverError, this, [this](const QString& message) { emit status(message); });
	connect(client, &NakamaClient::channelJoined, this, [this](const QString& channelId, const QString& roomName, const QList<Presence>& presences) {
		if (roomName != m_roomName) {
			return;
		}
		m_roomChannel = channelId;
		m_members.clear();
		for (const Presence& p : presences) {
			m_members.insert(p.userId, p.username);
		}
		const Session& session = m_client->session();
		m_members.insert(session.userId, session.username);
		emit membersChanged();
	});
	connect(client, &NakamaClient::channelPresence, this, [this](const QString& channelId, const QList<Presence>& joins, const QList<Presence>& leaves) {
		if (channelId != m_roomChannel) {
			return;
		}
		for (const Presence& p : leaves) {
			m_members.remove(p.userId);
		}
		for (const Presence& p : joins) {
			m_members.insert(p.userId, p.username);
		}
		emit membersChanged();
	});
	connect(client, &NakamaClient::chatMessage, this, [this](const QString& channelId, const QString&, const QString& username, const QString& text) {
		if (channelId == m_roomChannel) {
			emit chatReceived(username, text);
		}
	});
	connect(client, &NakamaClient::matchmakerMatched, this, &LauncherCore::onMatched);
	connect(client, &NakamaClient::pong, this, [this](qint64 ms) {
		Q_UNUSED(ms);
	});
	setState(State::SigningIn);
	emit status(tr("Signing in to %1...").arg(m_settings->server().host));
	client->authenticateDevice(m_settings->deviceId(), m_settings->name());
}

void LauncherCore::signOut() {
	m_ping.stop();
	m_fresh.reset();
	if (m_client) {
		m_client->disconnect(this);
		m_client->disconnectSocket();
		m_client.release()->deleteLater();
	}
	m_members.clear();
	m_roomChannel.clear();
	emit membersChanged();
	if (!m_community.title.isEmpty() || !m_community.kinds.isEmpty()) {
		m_community = Community();
		emit communityChanged();
	}
	if (m_state != State::Playing) {
		setState(State::SignedOut);
	}
}

void LauncherCore::enterRoom(const QString& gameId) {
	if (gameId == m_roomGame && !m_roomChannel.isEmpty()) {
		return;
	}
	if (m_client && !m_roomChannel.isEmpty()) {
		m_client->leaveChannel(m_roomChannel);
	}
	m_roomChannel.clear();
	m_members.clear();
	emit membersChanged();
	m_roomGame = gameId;
	const Game* game = m_catalog->find(gameId);
	m_roomName = game ? roomNameFor(*game) : QString();
	if (m_client && m_client->isSocketOpen() && !m_roomName.isEmpty()) {
		m_client->joinRoom(m_roomName);
	}
}

QStringList LauncherCore::roomMembers() const {
	QStringList names = m_members.values();
	names.sort(Qt::CaseInsensitive);
	return names;
}

void LauncherCore::sendChat(const QString& text) {
	if (m_client && !m_roomChannel.isEmpty() && !text.trimmed().isEmpty()) {
		m_client->sendChat(m_roomChannel, text.trimmed().left(200));
	}
}

void LauncherCore::findMatch(const QString& gameId) {
	const Game* game = m_catalog->find(gameId);
	if (!game || !m_client || m_state != State::Online) {
		return;
	}
	if (!game->matchesInLauncher()) {
		playOnline(gameId);   // IKEMEN GO makes its own matches
		return;
	}
	QString error;
	QString identity = m_catalog->identity(*game, m_settings, &error);
	if (identity.isEmpty()) {
		emit status(tr("Cannot match %1: %2").arg(game->title, error));
		return;
	}
	m_matchGame = gameId;
	m_matchIdentity = identity;
	QString players = QString::number(game->players);
	QVariantMap properties {
		{ QStringLiteral("game"), identity },
		{ QStringLiteral("kind"), game->kind },
		{ QStringLiteral("mode"), QStringLiteral("unranked") },
		{ QStringLiteral("players"), players },
	};
	// the same query as the emulators' own quick match, so both find each other
	QString query = QStringLiteral("+properties.game:%1 +properties.kind:%2 +properties.mode:unranked +properties.players:%3")
	                    .arg(identity, game->kind, players);
	m_client->matchmake(query, game->players, game->players, properties, QVariantMap());
	setState(State::Matchmaking);
	emit status(tr("Looking for %n other player(s) for %1...", nullptr, game->players - 1).arg(game->title));
}

void LauncherCore::cancelMatch() {
	if (m_state == State::Matchmaking && m_client) {
		m_fresh.reset();   // a match found, but not started yet: it is not
		m_client->cancelMatchmaking();
		setState(State::Online);
		emit status(tr("Matchmaking cancelled."));
	}
}

void LauncherCore::onMatched(const Matched& matched) {
	if (!m_catalog->find(m_matchGame) || m_state != State::Matchmaking || m_fresh) {
		return;
	}
	// The emulator plays as this account, with a session of the account that it refreshes
	// itself. The launcher's own session can be older than its refresh token, which expires
	// an hour after the session began on Nakama's default settings, and the emulator could
	// not refresh it: the launcher signs in again for a new session.
	m_fresh.reset(new NakamaClient(m_settings->server()));
	NakamaClient* fresh = m_fresh.get();
	connect(fresh, &NakamaClient::signedIn, this, [this, fresh, matched]() {
		Session session = fresh->session();
		m_fresh.release()->deleteLater();
		startMatch(matched, session);
	});
	connect(fresh, &NakamaClient::failed, this, [this, matched](const QString& error) {
		m_fresh.release()->deleteLater();
		if (m_client) {
			emit status(tr("Cannot sign in again for the game (%1); it gets the launcher's session.").arg(error));
			startMatch(matched, m_client->session());
		}
	});
	fresh->authenticateDevice(m_settings->deviceId());
}

void LauncherCore::startMatch(const Matched& matched, const Session& session) {
	const Game* game = m_catalog->find(m_matchGame);
	if (!game || !m_client || m_state != State::Matchmaking) {
		return;   // cancelled, or signed out, meanwhile
	}
	ConnectInfo info;
	info.server = m_settings->server();
	info.token = session.token;
	info.refreshToken = session.refreshToken;
	info.matchId = matched.matchId;
	info.matchToken = matched.token;
	for (const MatchedUser& u : matched.users) {
		info.players.append(MatchPlayer { u.presence.userId, u.presence.username });
	}
	info.self = matched.selfIndex();
	info.kind = game->kind;
	info.profile = game->profile;
	info.identity = m_matchIdentity;
	info.stunServers = m_settings->stunServers();
	std::unique_ptr<QTemporaryDir> handoff(new QTemporaryDir);
	QString path = handoff->filePath(QStringLiteral("match.json"));
	QString error;
	if (!handoff->isValid() || !info.write(path, &error)) {
		emit status(tr("Cannot write the match for the emulator: %1").arg(handoff->isValid() ? error : handoff->errorString()));
		setState(State::Online);
		return;
	}
	QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
	env.insert(QStringLiteral("NETPLAY_CONNECT"), QDir::toNativeSeparators(path));
	QStringList names;
	for (const MatchPlayer& p : info.players) {
		names.append(p.name);
	}
	emit status(tr("Match found: %1. Starting %2...").arg(names.join(QStringLiteral(" vs ")), game->title));
	// the emulator loads the profile, or for melonDS the ROM, that the file names
	if (!launch(*game, env, std::move(handoff), &error)) {
		emit status(error);
		setState(State::Online);
	}
}

QStringList LauncherCore::programFor(const Game& game, QString* error) const {
	if (!game.command.isEmpty()) {
		return game.command + game.args;
	}
	QString program;
	if (game.kind == QLatin1String("mgba")) {
		program = m_settings->mgbaPath();
	} else if (game.kind == QLatin1String("melonds")) {
		program = m_settings->melondsPath();
	} else if (game.kind == QLatin1String("ikemen")) {
		program = m_settings->ikemenPath();
	}
	if (program.isEmpty()) {
		*error = tr("Set the program for %1 games in the settings.").arg(game.kindLabel());
		return QStringList();
	}
	return QStringList { program } + game.args;
}

bool LauncherCore::launch(const Game& game, const QProcessEnvironment& env, std::unique_ptr<QTemporaryDir> handoff, QString* error) {
	if (m_process && m_process->state() != QProcess::NotRunning) {
		*error = tr("A game is already running.");
		return false;
	}
	QStringList command = programFor(game, error);
	if (command.isEmpty()) {
		return false;
	}
	m_handoff = std::move(handoff);
	m_process.reset(new QProcess);
	m_process->setProcessEnvironment(env);
	m_process->setProcessChannelMode(QProcess::ForwardedChannels);
	if (!game.workdir.isEmpty()) {
		m_process->setWorkingDirectory(game.workdir);
	}
	QString gameId = game.id;
	connect(m_process.get(), static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished), this,
	        [this, gameId](int code, QProcess::ExitStatus) {
		m_handoff.reset();
		setState(m_client && m_client->isSocketOpen() ? State::Online : State::SignedOut);
		emit status(tr("The game ended."));
		emit gameFinished(gameId, code);
	});
	m_process->start(command.first(), command.mid(1));
	if (!m_process->waitForStarted(10000)) {
		*error = tr("Cannot start %1: %2").arg(command.first(), m_process->errorString());
		m_handoff.reset();
		return false;
	}
	setState(State::Playing);
	emit gameStarted(gameId);
	return true;
}

std::unique_ptr<QTemporaryDir> LauncherCore::writeServerProfile(const Game& game, QString* path, QString* error) const {
	std::unique_ptr<QTemporaryDir> dir(new QTemporaryDir);
	if (!dir->isValid()) {
		*error = tr("Cannot write the game's server profile: %1").arg(dir->errorString());
		return nullptr;
	}
	*path = dir->filePath(QStringLiteral("servers.json"));
	NetplayOnline::ServerConfig server = m_settings->server();
	QJsonObject profile {
		{ QStringLiteral("id"), QStringLiteral("launcher") },
		{ QStringLiteral("name"), QStringLiteral("Nakama Matchmaker") },
		{ QStringLiteral("host"), server.host },
		{ QStringLiteral("port"), server.port },
		{ QStringLiteral("server_key"), server.serverKey },
		{ QStringLiteral("ssl"), server.ssl },
		{ QStringLiteral("status"), true },
		{ QStringLiteral("device_id"), m_settings->deviceId() },
		{ QStringLiteral("username"), m_settings->name() },
		{ QStringLiteral("stun_servers"), QJsonArray::fromStringList(m_settings->stunServers()) },
	};
	if (game.kind == QLatin1String("ikemen")) {
		profile.insert(QStringLiteral("game"), game.ikemenGame);
		profile.insert(QStringLiteral("build"), game.ikemenBuild);
	}
	QJsonObject root {
		{ QStringLiteral("version"), 1 },
		{ QStringLiteral("default"), QStringLiteral("launcher") },
		{ QStringLiteral("servers"), QJsonArray { profile } },
	};
	// the file holds the account's device id: only its owner may read it
	QFile file(*path);
	if (!file.open(QIODevice::WriteOnly) || !file.setPermissions(QFile::ReadOwner | QFile::WriteOwner) ||
	    file.write(QJsonDocument(root).toJson()) < 0) {
		*error = tr("Cannot write the game's server profile: %1").arg(file.errorString());
		return nullptr;
	}
	file.close();
	return dir;
}

void LauncherCore::play(const QString& gameId) {
	const Game* game = m_catalog->find(gameId);
	if (!game) {
		return;
	}
	if (game->kind == QLatin1String("ikemen")) {
		// IKEMEN GO always gets the launcher's server and account: it goes online, or not,
		// from its own menus
		playOnline(gameId);
		return;
	}
	QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
	env.remove(QStringLiteral("NETPLAY_CONNECT"));
	Game started = *game;
	if (game->kind == QLatin1String("melonds") && !game->profile.isEmpty()) {
		started.args << game->profile;   // the ROM; an mGBA profile is for netplay only
	}
	QString error;
	if (!launch(started, env, nullptr, &error)) {
		emit status(error);
	}
}

void LauncherCore::playOnline(const QString& gameId) {
	const Game* game = m_catalog->find(gameId);
	if (!game || !game->playsOnlineItself()) {
		return;
	}
	QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
	env.remove(QStringLiteral("NETPLAY_CONNECT"));
	QString path;
	QString error;
	std::unique_ptr<QTemporaryDir> handoff = writeServerProfile(*game, &path, &error);
	if (!handoff) {
		emit status(error);
		return;
	}
	Game started = *game;
	if (game->kind == QLatin1String("melonds")) {
		// melonDS opens its online window signed in as this account and joins the game's
		// channel, where the player chats, challenges or asks for a quick match; it stays
		// open for more games until the player closes it (docs/launch.md)
		started.args = QStringList { QStringLiteral("--netplay-config"), path, QStringLiteral("--netplay-online") } + game->args;
		if (!game->profile.isEmpty()) {
			started.args << game->profile;   // the ROM
		}
	} else {
		// IKEMEN GO's online menus (quick match, lobbies) make the matches
		started.args = QStringList { QStringLiteral("-nakama-config"), path, QStringLiteral("-nakama-server"), QStringLiteral("launcher") } + game->args;
		if (started.workdir.isEmpty()) {
			started.workdir = QFileInfo(m_settings->ikemenPath()).absolutePath();
		}
	}
	if (!launch(started, env, std::move(handoff), &error)) {
		emit status(error);
	}
}
