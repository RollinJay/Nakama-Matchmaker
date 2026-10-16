/* SPDX-License-Identifier: MIT */
#pragma once

#include <QDialog>
#include <QJsonObject>

class LauncherCore;
class QLabel;
class QPushButton;
class QTreeWidget;
struct Game;

/* The leaderboard of an IKEMEN GO game on the launcher's server: its ranked players,
 * highest rating first, with the levels that the server shows for their ratings, and the
 * player's own place. It comes from the IKEMEN GO modules' ikemen_leaderboard RPC, page by
 * page (docs/hosting.md, "IKEMEN GO's ranked levels"); the server writes the levels as text
 * (level_text) too, which is what is shown, so that every client shows the same. */
class LeaderboardDialog : public QDialog {
	Q_OBJECT

public:
	LeaderboardDialog(LauncherCore*, const Game&, QWidget* parent = nullptr);

	QString gameId() const { return m_gameId; }
	/* The game as it is now, after Edit changed it (its title, or its IKEMEN GO game id,
	 * which names its leaderboard); reload() then reads its leaderboard. */
	void setGame(const Game&);
	/* Reads the leaderboard again from its first page. */
	void reload();

private:
	void request(const QString& cursor);
	void received(const QJsonObject& answer, bool more);
	void failed(const QString& error);
	void updateState();
	void updateHeading();   // the game, and the server's community or address
	QString recordLine(const QJsonObject&) const;

	LauncherCore* m_core;
	QString m_gameId;       // in the launcher's game list
	QString m_title;
	QString m_ikemenGame;   // the leaderboard's: the IKEMEN GO game id
	QString m_label;        // what the server calls the level
	QString m_cursor;       // the next page's; empty after the last
	int m_request = 0;      // the answer still wanted
	bool m_loading = false;
	bool m_loaded = false;

	QLabel* m_heading = nullptr;
	QTreeWidget* m_list = nullptr;
	QLabel* m_mine = nullptr;
	QLabel* m_status = nullptr;
	QPushButton* m_more = nullptr;
	QPushButton* m_refresh = nullptr;
};
