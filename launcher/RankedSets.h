/* SPDX-License-Identifier: MIT */
#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

/* One of IKEMEN GO's ranked sets on a community server, as the IKEMEN GO modules' moderator
 * RPCs describe it (docs/hosting.md). The modules record each best-of set of a ranked
 * session, take each of its two players' claim of the winner, change the ratings once when
 * the claims agree, and keep both players' replays of the set; a set whose claims disagree,
 * or whose second claim did not come, is disputed, and a moderator watches its replay and
 * settles or voids it.
 *
 * The RPCs (ikemen_ranked_list, ikemen_ranked_replay and ikemen_ranked_settle) accept only
 * calls made with the server's runtime HTTP key, never a player's session:
 * ServerManager::moderatorRpc calls them. */
struct RankedSet {
	struct Player {
		QString userId;
		QString username;
		QString levelText;   // the player's level now, as the server writes it (9,001); empty from older modules
	};
	struct Claim {
		QString userId;
		QString winner;   // the user id the claim names
		qint64 at = 0;
	};
	struct Replay {
		QString userId;
		qint64 bytes = 0;
		bool complete = false;
	};

	QString matchId;    // the set's id: the session's match id, then "<match id>#<n>"
	QString session;    // the session's match id
	int set = 1;        // the set's number in the session
	QString game;
	QString build;
	int bestOf = 0;
	QString label;      // what the game's levels are called ("Rating" without level settings)
	QString status;     // open, settled, disputed or void
	QString reason;     // why it is disputed or void (or how it was decided)
	QList<Player> players;   // in the matchmaker's order: the first one hosted
	QList<Claim> claims;
	QStringList leftEarly;   // players who left the session before the set was decided
	QList<Replay> replays;
	bool played = false;     // a match of the set started
	qint64 created = 0;      // Unix times in seconds; 0: not yet
	qint64 ended = 0;
	qint64 decided = 0;
	QString winner;
	QString settledBy;       // players or moderator
	QString note;

	static RankedSet fromJson(const QJsonObject&);

	/* A player's username, or the user id itself when it is no player of the set. */
	QString name(const QString& userId) const;
	/* The user id of the player that a user id or a username names; empty, with the reason,
	 * when it names none of the two (or both). */
	QString findPlayer(const QString& idOrName, QString* error) const;
	/* "disputed (the claims disagree)" */
	QString statusLabel() const;
	QString playersLabel() const;   // "KAI vs ElPeleador"
	QString claimsLabel() const;
	QString replaysLabel() const;
	/* Everything about the set, as lines of text. */
	QStringList describe() const;

	static QString timeLabel(qint64 unixSeconds);
	static QString sizeLabel(qint64 bytes);
	/* A file name the server gave (a replay's), made safe for every system's folders. */
	static QString safeFileName(const QString&);
};
