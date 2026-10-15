/* SPDX-License-Identifier: MIT */
#include "GameCatalog.h"

#include "Settings.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>

namespace {

QStringList toList(const QJsonValue& v) {
	QStringList out;
	for (const QJsonValue& item : v.toArray()) {
		out.append(item.toString());
	}
	return out;
}

}

QString Game::kindLabel() const {
	if (kind == QLatin1String("mgba")) {
		return QStringLiteral("GBA");
	}
	if (kind == QLatin1String("melonds")) {
		return QStringLiteral("DS");
	}
	if (kind == QLatin1String("ikemen")) {
		return QStringLiteral("IKEMEN");
	}
	return kind.toUpper();
}

GameCatalog::GameCatalog(const QString& path)
	: m_path(path) {
}

bool GameCatalog::load(QString* error) {
	m_games.clear();
	QFile file(m_path);
	if (!file.exists()) {
		return true;
	}
	if (!file.open(QIODevice::ReadOnly)) {
		*error = file.errorString();
		return false;
	}
	QJsonParseError parseError;
	QJsonObject root = QJsonDocument::fromJson(file.readAll(), &parseError).object();
	if (parseError.error != QJsonParseError::NoError) {
		*error = QStringLiteral("%1: %2").arg(m_path, parseError.errorString());
		return false;
	}
	for (const QJsonValue& v : root.value(QStringLiteral("games")).toArray()) {
		QJsonObject o = v.toObject();
		Game g;
		g.id = o.value(QStringLiteral("id")).toString();
		g.title = o.value(QStringLiteral("title")).toString(g.id);
		g.kind = o.value(QStringLiteral("kind")).toString();
		g.profile = o.value(QStringLiteral("profile")).toString();
		g.players = o.value(QStringLiteral("players")).toInt(2);
		if (g.kind == QLatin1String("melonds")) {
			g.players = 2;   // the melonDS fork plays two players online
		}
		g.ikemenGame = o.value(QStringLiteral("game")).toString();
		g.ikemenBuild = o.value(QStringLiteral("build")).toString();
		g.workdir = o.value(QStringLiteral("workdir")).toString();
		g.args = toList(o.value(QStringLiteral("args")));
		g.command = toList(o.value(QStringLiteral("command")));
		g.identityCommand = toList(o.value(QStringLiteral("identity_command")));
		if (!g.id.isEmpty()) {
			m_games.append(g);
		}
	}
	return true;
}

bool GameCatalog::save(QString* error) const {
	QJsonArray games;
	for (const Game& g : m_games) {
		QJsonObject o {
			{ QStringLiteral("id"), g.id },
			{ QStringLiteral("title"), g.title },
			{ QStringLiteral("kind"), g.kind },
		};
		if (!g.profile.isEmpty()) {
			o.insert(QStringLiteral("profile"), g.profile);
		}
		if (g.kind == QLatin1String("mgba")) {
			o.insert(QStringLiteral("players"), g.players);
		}
		if (!g.ikemenGame.isEmpty()) {
			o.insert(QStringLiteral("game"), g.ikemenGame);
		}
		if (!g.ikemenBuild.isEmpty()) {
			o.insert(QStringLiteral("build"), g.ikemenBuild);
		}
		if (!g.workdir.isEmpty()) {
			o.insert(QStringLiteral("workdir"), g.workdir);
		}
		if (!g.args.isEmpty()) {
			o.insert(QStringLiteral("args"), QJsonArray::fromStringList(g.args));
		}
		if (!g.command.isEmpty()) {
			o.insert(QStringLiteral("command"), QJsonArray::fromStringList(g.command));
		}
		if (!g.identityCommand.isEmpty()) {
			o.insert(QStringLiteral("identity_command"), QJsonArray::fromStringList(g.identityCommand));
		}
		games.append(o);
	}
	QFile file(m_path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		*error = file.errorString();
		return false;
	}
	file.write(QJsonDocument(QJsonObject { { QStringLiteral("version"), 1 }, { QStringLiteral("games"), games } }).toJson());
	return true;
}

const Game* GameCatalog::find(const QString& id) const {
	for (const Game& g : m_games) {
		if (g.id == id) {
			return &g;
		}
	}
	return nullptr;
}

void GameCatalog::put(const Game& game) {
	for (Game& g : m_games) {
		if (g.id == game.id) {
			g = game;
			return;
		}
	}
	m_games.append(game);
}

void GameCatalog::remove(const QString& id) {
	for (int i = 0; i < m_games.size(); ++i) {
		if (m_games[i].id == id) {
			m_games.removeAt(i);
			return;
		}
	}
}

QString GameCatalog::identity(const Game& game, Settings* settings, QString* error, bool cached) {
	if (!game.matchesInLauncher()) {
		return QString();
	}
	QStringList command = game.identityCommand;
	if (command.isEmpty()) {
		QString emulator = game.kind == QLatin1String("mgba") ? settings->mgbaPath() : settings->melondsPath();
		if (emulator.isEmpty()) {
			*error = QStringLiteral("set the %1 program in the settings").arg(game.kind == QLatin1String("mgba") ? QStringLiteral("mGBA") : QStringLiteral("melonDS"));
			return QString();
		}
		command = QStringList { emulator, QStringLiteral("--netplay-identity"), QStringLiteral("{profile}") };
	}
	for (QString& part : command) {
		part.replace(QStringLiteral("{profile}"), game.profile);
	}
	QFileInfo program(command.first());
	QFileInfo profile(game.profile);
	QString stamp = command.join(QLatin1Char('\n')) + QLatin1Char('\n') +
	                QString::number(program.lastModified().toMSecsSinceEpoch()) + QLatin1Char('\n') +
	                QString::number(profile.lastModified().toMSecsSinceEpoch());
	if (cached && m_identities.contains(stamp)) {
		return m_identities.value(stamp);
	}
	QProcess process;
	process.start(command.first(), command.mid(1));
	if (!process.waitForFinished(60000)) {
		*error = QStringLiteral("%1 did not answer").arg(command.first());
		process.kill();
		return QString();
	}
	QString out = QString::fromLatin1(process.readAllStandardOutput()).trimmed().section(QLatin1Char('\n'), -1).trimmed();
	if (process.exitCode() != 0 || out.size() != 40) {
		QString err = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
		QString what = game.kind == QLatin1String("melonds") ? QStringLiteral("the ROM") : QStringLiteral("the profile");
		*error = err.isEmpty() ? QStringLiteral("%1 could not read %2 %3").arg(command.first(), what, game.profile) : err;
		return QString();
	}
	m_identities.insert(stamp, out);
	return out;
}
