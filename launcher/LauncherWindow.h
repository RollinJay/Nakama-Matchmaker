/* SPDX-License-Identifier: MIT */
#pragma once

#include "LauncherCore.h"

#include <QMainWindow>

class GameCatalog;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QTextEdit;
class ServerManager;
class ServersWindow;
class Settings;

/* The launcher's window, organized like Fightcade's: the games on the left; for the chosen
 * game, its room (chat and the players in it) and the buttons that find a match or start
 * the game. */
class LauncherWindow : public QMainWindow {
	Q_OBJECT

public:
	LauncherWindow(Settings*, GameCatalog*, LauncherCore*, ServerManager*, QWidget* parent = nullptr);

protected:
	void closeEvent(QCloseEvent*) override;

private:
	void reloadGames();
	void selectGame();
	void updateGameInfo();
	QString selectedGame() const;
	void updateState();
	void appendChat(const QString& name, const QString& text);
	void openSettings();
	void editGame(bool add);
	void removeGame();
	void openServers();
	void updateServers();

	Settings* m_settings;
	GameCatalog* m_catalog;
	LauncherCore* m_core;
	ServerManager* m_servers;
	ServersWindow* m_serversWindow = nullptr;

	QLabel* m_account = nullptr;
	QPushButton* m_signIn = nullptr;
	QPushButton* m_serversButton = nullptr;
	QListWidget* m_games = nullptr;
	QLabel* m_gameTitle = nullptr;
	QLabel* m_gameInfo = nullptr;
	QPushButton* m_find = nullptr;
	QPushButton* m_online = nullptr;
	QPushButton* m_cancel = nullptr;
	QPushButton* m_play = nullptr;
	QTextEdit* m_chat = nullptr;
	QLineEdit* m_chatInput = nullptr;
	QListWidget* m_members = nullptr;
	QLabel* m_membersTitle = nullptr;
	QLabel* m_status = nullptr;
};
