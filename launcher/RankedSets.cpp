/* SPDX-License-Identifier: MIT */
#include "RankedSets.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QJsonArray>
#include <QRegularExpression>

namespace {

QString tr(const char* text) {
	return QCoreApplication::translate("RankedSet", text);
}

qint64 seconds(const QJsonValue& v) {
	return qint64(v.toDouble(0));
}

QString reasonLabel(const QString& reason) {
	if (reason == QLatin1String("claims_disagree")) {
		return tr("the claims disagree");
	}
	if (reason == QLatin1String("replay_hash_disagree")) {
		return tr("the players' replay hashes disagree");
	}
	if (reason == QLatin1String("claim_timeout")) {
		return tr("a claim did not come in time");
	}
	if (reason == QLatin1String("no_claims")) {
		return tr("nobody claimed a winner");
	}
	if (reason == QLatin1String("moderator")) {
		return tr("by a moderator");
	}
	return reason;
}

}

RankedSet RankedSet::fromJson(const QJsonObject& o) {
	RankedSet s;
	s.matchId = o.value(QStringLiteral("match_id")).toString();
	s.session = o.value(QStringLiteral("session")).toString(s.matchId);
	s.set = o.value(QStringLiteral("set")).toInt(1);
	s.game = o.value(QStringLiteral("game")).toString();
	s.build = o.value(QStringLiteral("build")).toString();
	s.bestOf = o.value(QStringLiteral("best_of")).toInt(0);
	s.label = o.value(QStringLiteral("label")).toString();
	s.status = o.value(QStringLiteral("status")).toString();
	s.reason = o.value(QStringLiteral("reason")).toString();
	for (const QJsonValue& v : o.value(QStringLiteral("players")).toArray()) {
		QJsonObject p = v.toObject();
		s.players.append(Player { p.value(QStringLiteral("user_id")).toString(), p.value(QStringLiteral("username")).toString(),
		                          p.value(QStringLiteral("level_text")).toString() });
	}
	for (const QJsonValue& v : o.value(QStringLiteral("claims")).toArray()) {
		QJsonObject c = v.toObject();
		s.claims.append(Claim { c.value(QStringLiteral("user_id")).toString(), c.value(QStringLiteral("winner")).toString(), seconds(c.value(QStringLiteral("at"))) });
	}
	for (const QJsonValue& v : o.value(QStringLiteral("left_early")).toArray()) {
		s.leftEarly.append(v.toString());
	}
	for (const QJsonValue& v : o.value(QStringLiteral("replays")).toArray()) {
		QJsonObject r = v.toObject();
		s.replays.append(Replay { r.value(QStringLiteral("user_id")).toString(), qint64(r.value(QStringLiteral("bytes")).toDouble(0)),
		                          r.value(QStringLiteral("complete")).toBool(false) });
	}
	s.played = o.value(QStringLiteral("played")).toBool(false);
	s.created = seconds(o.value(QStringLiteral("created")));
	s.ended = seconds(o.value(QStringLiteral("ended")));
	s.decided = seconds(o.value(QStringLiteral("decided")));
	s.winner = o.value(QStringLiteral("winner")).toString();
	s.settledBy = o.value(QStringLiteral("settled_by")).toString();
	s.note = o.value(QStringLiteral("note")).toString();
	return s;
}

QString RankedSet::name(const QString& userId) const {
	for (const Player& p : players) {
		if (p.userId == userId) {
			return p.username.isEmpty() ? userId : p.username;
		}
	}
	return userId;
}

QString RankedSet::findPlayer(const QString& idOrName, QString* error) const {
	QString wanted = idOrName.trimmed();
	for (const Player& p : players) {
		if (p.userId == wanted) {
			return p.userId;
		}
	}
	// a username: as written, or else in any case, as long as it names one player
	for (Qt::CaseSensitivity cs : { Qt::CaseSensitive, Qt::CaseInsensitive }) {
		QStringList found;
		for (const Player& p : players) {
			if (!p.username.isEmpty() && p.username.compare(wanted, cs) == 0) {
				found.append(p.userId);
			}
		}
		if (found.size() == 1) {
			return found.first();
		}
		if (found.size() > 1) {
			*error = tr("Both players of the set %1 are named %2: give the winner's user id (%3).")
			             .arg(matchId, wanted, found.join(QStringLiteral(", ")));
			return QString();
		}
	}
	QStringList names;
	for (const Player& p : players) {
		names.append(QStringLiteral("%1 (%2)").arg(p.username, p.userId));
	}
	*error = tr("%1 is not a player of the set %2, whose players are %3.").arg(wanted, matchId, names.join(tr(" and ")));
	return QString();
}

QString RankedSet::statusLabel() const {
	QString text = status;
	if (!reason.isEmpty() && (status == QLatin1String("disputed") || status == QLatin1String("void"))) {
		text += QStringLiteral(" (%1)").arg(reasonLabel(reason));
	}
	return text;
}

QString RankedSet::playersLabel() const {
	QStringList names;
	for (const Player& p : players) {
		names.append(p.username.isEmpty() ? p.userId : p.username);
	}
	return names.join(tr(" vs "));
}

QString RankedSet::claimsLabel() const {
	if (claims.isEmpty()) {
		return tr("none");
	}
	QStringList out;
	for (const Claim& c : claims) {
		out.append(tr("%1 says %2 won").arg(name(c.userId), name(c.winner)));
	}
	return out.join(QStringLiteral("; "));
}

QString RankedSet::replaysLabel() const {
	if (replays.isEmpty()) {
		return tr("none");
	}
	QStringList out;
	for (const Replay& r : replays) {
		out.append(tr("%1's, %2, %3").arg(name(r.userId), sizeLabel(r.bytes), r.complete ? tr("complete") : tr("incomplete")));
	}
	return out.join(QStringLiteral("; "));
}

QStringList RankedSet::describe() const {
	QStringList lines;
	lines << tr("Set %1: %2").arg(matchId, statusLabel());
	QString what = tr("%1, build %2, best of %3").arg(game.isEmpty() ? tr("(no game)") : game, build.isEmpty() ? tr("(none)") : build).arg(bestOf);
	if (set > 1) {
		what += tr(", set %1 of the session %2").arg(set).arg(session);
	}
	lines << QStringLiteral("  ") + what;
	QString times = tr("created %1").arg(timeLabel(created));
	if (ended > 0) {
		times += tr(", ended %1").arg(timeLabel(ended));
	}
	if (decided > 0) {
		times += tr(", decided %1").arg(timeLabel(decided));
	}
	lines << QStringLiteral("  ") + times;
	QStringList players;
	for (const Player& p : this->players) {
		// the level the player has now, not the one at the set's time
		players.append(p.levelText.isEmpty() || label.isEmpty() ? QStringLiteral("%1 (%2)").arg(p.username, p.userId)
		                                                        : tr("%1 (%2; %3 %4 now)").arg(p.username, p.userId, label, p.levelText));
	}
	lines << tr("  Players: %1%2").arg(players.join(tr(" vs ")), played ? QString() : tr("; no match of the set started"));
	lines << tr("  Claims: %1").arg(claimsLabel());
	if (!leftEarly.isEmpty()) {
		QStringList names;
		for (const QString& id : leftEarly) {
			names.append(name(id));
		}
		lines << tr("  Left before the set was decided: %1").arg(names.join(QStringLiteral(", ")));
	}
	lines << tr("  Replays: %1").arg(replaysLabel());
	if (!winner.isEmpty()) {
		lines << tr("  Winner: %1, settled by %2").arg(name(winner), settledBy == QLatin1String("moderator") ? tr("a moderator") : tr("the players' claims"));
	}
	if (!note.isEmpty()) {
		lines << tr("  Note: %1").arg(note);
	}
	return lines;
}

QString RankedSet::timeLabel(qint64 unixSeconds) {
	if (unixSeconds <= 0) {
		return tr("unknown");
	}
	return QDateTime::fromMSecsSinceEpoch(unixSeconds * 1000).toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
}

QString RankedSet::sizeLabel(qint64 bytes) {
	if (bytes < 1024) {
		return tr("%1 bytes").arg(bytes);
	}
	if (bytes < 1024 * 1024) {
		return tr("%1 KB").arg(QString::number(bytes / 1024.0, 'f', 1));
	}
	return tr("%1 MB").arg(QString::number(bytes / (1024.0 * 1024.0), 'f', 1));
}

QString RankedSet::safeFileName(const QString& name) {
	QString out = name;
	out.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|\\x00-\\x1f\\x7f]")), QStringLiteral("_"));
	out = out.trimmed();
	while (out.startsWith(QLatin1Char('.'))) {
		out.remove(0, 1);
	}
	return out.isEmpty() ? QStringLiteral("ranked.replay") : out;
}
