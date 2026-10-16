/* SPDX-License-Identifier: MIT */
#include "LauncherWindow.h"

#include "GameCatalog.h"
#include "GameDialog.h"
#include "LeaderboardDialog.h"
#include "ServerManager.h"
#include "ServersWindow.h"
#include "Settings.h"
#include "SettingsDialog.h"

#include <QCloseEvent>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollBar>
#include <QSplitter>
#include <QTextEdit>
#include <QVBoxLayout>

namespace {

const char* const STYLE =
	"QMainWindow, QWidget#central, QWidget#servers, QDialog { background: #1b1726; color: #e8e4f0; }"
	"QWidget#topbar, QWidget#statusbar { background: #15111f; }"
	"QWidget#sidebar, QWidget#members { background: #211a2e; }"
	"QLabel { color: #e8e4f0; }"
	"QLabel#brand { color: #2ee6d6; font-weight: bold; font-size: 16px; letter-spacing: 1px; }"
	"QLabel#heading { color: #2ee6d6; font-weight: bold; }"
	"QLabel#gameTitle { color: #ffffff; font-weight: bold; font-size: 18px; }"
	"QLabel#dim { color: #9a92b0; }"
	"QListWidget { background: transparent; color: #e8e4f0; border: none; }"
	"QListWidget::item { padding: 6px 4px; }"
	"QListWidget::item:selected { background: #3a3150; color: #ffffff; }"
	"QTreeWidget { background: #15111f; color: #e8e4f0; border: 1px solid #3a3150; }"
	"QTreeWidget::item { padding: 3px 2px; }"
	"QTreeWidget::item:selected { background: #3a3150; color: #ffffff; }"
	"QHeaderView::section { background: #211a2e; color: #9a92b0; border: none; padding: 4px; }"
	"QComboBox { background: #15111f; color: #e8e4f0; border: 1px solid #3a3150; padding: 3px 6px; }"
	"QComboBox:disabled { color: #6c6480; }"
	"QComboBox QAbstractItemView { background: #15111f; color: #e8e4f0; selection-background-color: #3a3150; }"
	"QLineEdit, QTextEdit, QPlainTextEdit, QSpinBox { background: #15111f; color: #e8e4f0; border: 1px solid #3a3150; padding: 4px; }"
	"QLineEdit:disabled, QSpinBox:disabled { color: #6c6480; }"
	"QCheckBox, QRadioButton { color: #e8e4f0; }"
	"QCheckBox:disabled, QRadioButton:disabled, QLabel:disabled { color: #6c6480; }"
	"QScrollArea, QScrollArea > QWidget > QWidget { background: transparent; }"
	"QTabWidget::pane { border: 1px solid #3a3150; }"
	"QTabBar::tab { background: #211a2e; color: #9a92b0; padding: 6px 14px; }"
	"QTabBar::tab:selected { background: #3a3150; color: #ffffff; }"
	"QCheckBox::indicator, QRadioButton::indicator { width: 12px; height: 12px; border: 1px solid #6c6480; background: #15111f; }"
	"QRadioButton::indicator { border-radius: 7px; }"
	"QCheckBox::indicator:checked, QRadioButton::indicator:checked { background: #2ee6d6; border-color: #2ee6d6; }"
	"QCheckBox::indicator:disabled, QRadioButton::indicator:disabled { border-color: #3a3150; }"
	"QCheckBox::indicator:checked:disabled, QRadioButton::indicator:checked:disabled { background: #1d3533; }"
	"QScrollBar:vertical { background: #15111f; width: 10px; margin: 0; }"
	"QScrollBar:horizontal { background: #15111f; height: 10px; margin: 0; }"
	"QScrollBar::handle { background: #3a3150; min-height: 24px; min-width: 24px; }"
	"QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }"
	"QScrollBar::add-page, QScrollBar::sub-page { background: none; }"
	"QPushButton { background: #3a3150; color: #e8e4f0; border: 1px solid #4c4266; padding: 6px 14px; }"
	"QPushButton:hover { background: #4c4266; }"
	"QPushButton:disabled { color: #6c6480; border-color: #2c2540; }"
	"QPushButton#primary { background: #1f7f78; border-color: #2ee6d6; font-weight: bold; }"
	"QPushButton#primary:disabled { background: #1d3533; color: #5f7d7a; border-color: #2a4a47; }"
	"QGroupBox { color: #e8e4f0; border: 1px solid #3a3150; margin-top: 12px; padding: 8px 6px 6px 6px; }"
	"QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; color: #2ee6d6; }"
	"QTableWidget { background: #15111f; color: #e8e4f0; border: 1px solid #3a3150; gridline-color: #3a3150; }"
	"QTableWidget::item:selected { background: #3a3150; color: #ffffff; }"
	"QTableCornerButton::section { background: #211a2e; border: none; }";

}

LauncherWindow::LauncherWindow(Settings* settings, GameCatalog* catalog, LauncherCore* core, ServerManager* servers, QWidget* parent)
	: QMainWindow(parent)
	, m_settings(settings)
	, m_catalog(catalog)
	, m_core(core)
	, m_servers(servers) {
	setWindowTitle(tr("Nakama Matchmaker"));
	setStyleSheet(QString::fromLatin1(STYLE));
	resize(1100, 680);

	QWidget* central = new QWidget;
	central->setObjectName("central");
	QVBoxLayout* rows = new QVBoxLayout(central);
	rows->setContentsMargins(0, 0, 0, 0);
	rows->setSpacing(0);

	QWidget* top = new QWidget;
	top->setObjectName("topbar");
	QHBoxLayout* topLayout = new QHBoxLayout(top);
	topLayout->setContentsMargins(14, 8, 14, 8);
	QLabel* brand = new QLabel(tr("NAKAMA MATCHMAKER"));
	brand->setObjectName("brand");
	topLayout->addWidget(brand);
	topLayout->addStretch();
	m_account = new QLabel;
	m_account->setObjectName("dim");
	topLayout->addWidget(m_account);
	m_signIn = new QPushButton(tr("Sign in"));
	connect(m_signIn, &QPushButton::clicked, this, [this]() {
		if (m_core->state() == LauncherCore::State::SignedOut) {
			if (m_settings->name().trimmed().size() < 2) {
				openSettings();
				if (m_settings->name().trimmed().size() < 2) {
					return;
				}
			}
			m_core->signIn();
		} else {
			m_core->signOut();
		}
	});
	topLayout->addWidget(m_signIn);
	m_serversButton = new QPushButton(tr("Servers"));
	m_serversButton->setToolTip(tr("Run Nakama servers for IKEMEN GO, GBA and DS communities"));
	connect(m_serversButton, &QPushButton::clicked, this, &LauncherWindow::openServers);
	topLayout->addWidget(m_serversButton);
	QPushButton* settingsButton = new QPushButton(tr("Settings"));
	connect(settingsButton, &QPushButton::clicked, this, &LauncherWindow::openSettings);
	topLayout->addWidget(settingsButton);
	rows->addWidget(top);

	QHBoxLayout* columns = new QHBoxLayout;
	columns->setSpacing(0);

	QWidget* sidebar = new QWidget;
	sidebar->setObjectName("sidebar");
	sidebar->setFixedWidth(260);
	QVBoxLayout* side = new QVBoxLayout(sidebar);
	side->setContentsMargins(10, 10, 10, 10);
	QLabel* gamesHeading = new QLabel(tr("GAMES"));
	gamesHeading->setObjectName("heading");
	side->addWidget(gamesHeading);
	m_games = new QListWidget;
	connect(m_games, &QListWidget::currentRowChanged, this, &LauncherWindow::selectGame);
	side->addWidget(m_games, 1);
	QHBoxLayout* gameButtons = new QHBoxLayout;
	QPushButton* add = new QPushButton(tr("Add"));
	QPushButton* edit = new QPushButton(tr("Edit"));
	QPushButton* remove = new QPushButton(tr("Remove"));
	connect(add, &QPushButton::clicked, this, [this]() { editGame(true); });
	connect(edit, &QPushButton::clicked, this, [this]() { editGame(false); });
	connect(remove, &QPushButton::clicked, this, &LauncherWindow::removeGame);
	gameButtons->addWidget(add);
	gameButtons->addWidget(edit);
	gameButtons->addWidget(remove);
	side->addLayout(gameButtons);
	columns->addWidget(sidebar);

	QWidget* channel = new QWidget;
	QVBoxLayout* channelLayout = new QVBoxLayout(channel);
	channelLayout->setContentsMargins(16, 12, 16, 12);
	m_gameTitle = new QLabel;
	m_gameTitle->setObjectName("gameTitle");
	channelLayout->addWidget(m_gameTitle);
	m_gameInfo = new QLabel;
	m_gameInfo->setObjectName("dim");
	m_gameInfo->setTextFormat(Qt::PlainText);
	m_gameInfo->setWordWrap(true);
	channelLayout->addWidget(m_gameInfo);
	QHBoxLayout* actions = new QHBoxLayout;
	m_find = new QPushButton(tr("Find a match"));
	m_find->setObjectName("primary");
	m_cancel = new QPushButton(tr("Cancel"));
	// a DS game also has melonDS's own online window: its game channels and challenges
	m_online = new QPushButton(tr("Play online"));
	m_online->setToolTip(tr("Open melonDS's online window, signed in to this server as this account, in the game's channel"));
	m_play = new QPushButton(tr("Play offline"));
	// an IKEMEN GO game's ranked players on this server, with their levels
	m_leaderboardButton = new QPushButton(tr("Leaderboard"));
	m_leaderboardButton->setToolTip(tr("This game's ranked players on this server, with their levels"));
	connect(m_leaderboardButton, &QPushButton::clicked, this, &LauncherWindow::openLeaderboard);
	connect(m_find, &QPushButton::clicked, this, [this]() { m_core->findMatch(selectedGame()); });
	connect(m_cancel, &QPushButton::clicked, m_core, &LauncherCore::cancelMatch);
	connect(m_online, &QPushButton::clicked, this, [this]() { m_core->playOnline(selectedGame()); });
	connect(m_play, &QPushButton::clicked, this, [this]() { m_core->play(selectedGame()); });
	actions->addWidget(m_find);
	actions->addWidget(m_cancel);
	actions->addWidget(m_online);
	actions->addWidget(m_play);
	actions->addWidget(m_leaderboardButton);
	actions->addStretch();
	channelLayout->addLayout(actions);
	QLabel* chatHeading = new QLabel(tr("CHAT"));
	chatHeading->setObjectName("heading");
	channelLayout->addWidget(chatHeading);
	m_chat = new QTextEdit;
	m_chat->setReadOnly(true);
	channelLayout->addWidget(m_chat, 1);
	m_chatInput = new QLineEdit;
	m_chatInput->setMaxLength(200);
	m_chatInput->setPlaceholderText(tr("Say something to the room"));
	connect(m_chatInput, &QLineEdit::returnPressed, this, [this]() {
		m_core->sendChat(m_chatInput->text());
		m_chatInput->clear();
	});
	channelLayout->addWidget(m_chatInput);
	columns->addWidget(channel, 1);

	QWidget* members = new QWidget;
	members->setObjectName("members");
	members->setFixedWidth(220);
	QVBoxLayout* membersLayout = new QVBoxLayout(members);
	membersLayout->setContentsMargins(10, 10, 10, 10);
	m_membersTitle = new QLabel(tr("IN THE ROOM"));
	m_membersTitle->setObjectName("heading");
	membersLayout->addWidget(m_membersTitle);
	m_members = new QListWidget;
	membersLayout->addWidget(m_members, 1);
	columns->addWidget(members);
	rows->addLayout(columns, 1);

	QWidget* bottom = new QWidget;
	bottom->setObjectName("statusbar");
	QHBoxLayout* bottomLayout = new QHBoxLayout(bottom);
	bottomLayout->setContentsMargins(14, 6, 14, 6);
	m_status = new QLabel;
	m_status->setObjectName("dim");
	m_status->setTextFormat(Qt::PlainText);
	bottomLayout->addWidget(m_status, 1);
	rows->addWidget(bottom);
	setCentralWidget(central);

	connect(m_core, &LauncherCore::stateChanged, this, &LauncherWindow::updateState);
	connect(m_core, &LauncherCore::status, m_status, &QLabel::setText);
	connect(m_core, &LauncherCore::chatReceived, this, &LauncherWindow::appendChat);
	connect(m_core, &LauncherCore::membersChanged, this, [this]() {
		m_members->clear();
		QStringList names = m_core->roomMembers();
		m_members->addItems(names);
		m_membersTitle->setText(tr("IN THE ROOM (%1)").arg(names.size()));
	});
	connect(m_servers, &ServerManager::stateChanged, this, &LauncherWindow::updateServers);
	connect(m_core, &LauncherCore::communityChanged, this, [this]() {
		updateGameInfo();
		if (!m_core->community().message.isEmpty()) {
			appendChat(m_core->community().title, m_core->community().message);
		}
	});
	m_status->setText(tr("Not signed in."));
	reloadGames();
	updateState();
	updateServers();
}

void LauncherWindow::openServers() {
	if (!m_serversWindow) {
		m_serversWindow = new ServersWindow(m_servers, m_settings, this);
	}
	m_serversWindow->show();
	m_serversWindow->raise();
	m_serversWindow->activateWindow();
}

void LauncherWindow::openLeaderboard() {
	const Game* game = m_catalog->find(selectedGame());
	if (!game || game->kind != QLatin1String("ikemen")) {
		return;
	}
	if (m_leaderboard && m_leaderboard->gameId() != game->id) {
		m_leaderboard->close();   // deletes itself, later
		m_leaderboard = nullptr;
	}
	if (!m_leaderboard) {
		m_leaderboard = new LeaderboardDialog(m_core, *game, this);
		m_leaderboard->setAttribute(Qt::WA_DeleteOnClose);
	} else {
		m_leaderboard->setGame(*game);   // as it is now: Edit may have changed its IKEMEN GO game id
		m_leaderboard->reload();
	}
	m_leaderboard->show();
	m_leaderboard->raise();
	m_leaderboard->activateWindow();
}

void LauncherWindow::updateServers() {
	int running = m_servers->active().size();
	m_serversButton->setText(running ? tr("Servers (%1 running)").arg(running) : tr("Servers"));
}

void LauncherWindow::closeEvent(QCloseEvent* event) {
	if (m_serversWindow && !m_serversWindow->settle()) {
		event->ignore();
		return;
	}
	// the servers are the launcher's child processes: closing the launcher stops them
	QStringList running = m_servers->active();
	if (!running.isEmpty() &&
	    QMessageBox::question(this, tr("Quit"), tr("Servers are running (%1). Closing the launcher stops them.").arg(running.join(QStringLiteral(", "))),
	                          QMessageBox::Close | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Close) {
		event->ignore();
		return;
	}
	// the game is the launcher's child process: closing the launcher ends it
	if (m_core->state() == LauncherCore::State::Playing &&
	    QMessageBox::question(this, tr("Quit"), tr("A game is running. Closing the launcher closes the game too."),
	                          QMessageBox::Close | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Close) {
		event->ignore();
		return;
	}
	if (m_serversWindow) {
		m_serversWindow->close();
	}
	event->accept();
}

void LauncherWindow::reloadGames() {
	QString current = selectedGame();
	m_games->clear();
	int select = 0;
	for (const Game& g : m_catalog->games()) {
		QListWidgetItem* item = new QListWidgetItem(QStringLiteral("[%1]  %2").arg(g.kindLabel(), g.title));
		item->setData(Qt::UserRole, g.id);
		m_games->addItem(item);
		if (g.id == current) {
			select = m_games->count() - 1;
		}
	}
	if (m_games->count()) {
		m_games->setCurrentRow(select);
	}
	selectGame();
}

QString LauncherWindow::selectedGame() const {
	QListWidgetItem* item = m_games->currentItem();
	return item ? item->data(Qt::UserRole).toString() : QString();
}

void LauncherWindow::selectGame() {
	const Game* game = m_catalog->find(selectedGame());
	updateGameInfo();
	if (game) {
		m_chat->clear();
		m_core->enterRoom(game->id);
	}
	updateState();
}

void LauncherWindow::updateGameInfo() {
	const Game* game = m_catalog->find(selectedGame());
	if (!game) {
		m_gameTitle->setText(tr("No game"));
		m_gameInfo->setText(tr("Add a game: an mGBA netplay profile, a DS ROM, or IKEMEN GO."));
		return;
	}
	m_gameTitle->setText(game->title);
	QString info;
	if (game->kind == QLatin1String("melonds")) {
		m_gameInfo->setToolTip(game->profile);
		info = tr("DS (melonDS) · 2 players · ROM %1 · Find a match looks for an opponent from here; Play online opens melonDS's own online window "
		          "(the game's channel, challenges, quick match), with this account and server")
		       .arg(QFileInfo(game->profile).fileName());
	} else if (game->matchesInLauncher()) {
		info = tr("%1 · %n players · profile %2", nullptr, game->players).arg(game->kindLabel(), QFileInfo(game->profile).fileName());
		m_gameInfo->setToolTip(game->profile);
	} else {
		m_gameInfo->setToolTip(QString());
		info = tr("IKEMEN GO · matches are made in IKEMEN GO's online menus, with this account and server");
	}
	const LauncherCore::Community& community = m_core->community();
	if (!community.kinds.isEmpty() && !community.kinds.contains(game->kind)) {
		info += tr(" · this server's community plays other games (%1)").arg(community.kindsLabel());
	}
	m_gameInfo->setText(info);
}

void LauncherWindow::updateState() {
	LauncherCore::State state = m_core->state();
	bool online = state == LauncherCore::State::Online;
	const Game* game = m_catalog->find(selectedGame());
	m_signIn->setText(state == LauncherCore::State::SignedOut ? tr("Sign in") : tr("Sign out"));
	m_signIn->setEnabled(state != LauncherCore::State::SigningIn && state != LauncherCore::State::Playing);
	m_account->setText(state == LauncherCore::State::SignedOut ? tr("Not signed in") : m_settings->name());
	m_find->setEnabled(online && game);
	// IKEMEN GO makes its own matches: its one online button starts it with the server
	m_find->setText(game && !game->matchesInLauncher() ? tr("Play online") : tr("Find a match"));
	m_online->setVisible(game && game->matchesInLauncher() && game->playsOnlineItself());
	m_online->setEnabled(online);
	m_cancel->setVisible(state == LauncherCore::State::Matchmaking);
	m_play->setEnabled(game && state != LauncherCore::State::Playing && state != LauncherCore::State::Matchmaking);
	bool ikemen = game && game->kind == QLatin1String("ikemen");
	bool signedIn = online || state == LauncherCore::State::Matchmaking || state == LauncherCore::State::Playing;
	m_leaderboardButton->setVisible(ikemen);
	m_leaderboardButton->setEnabled(ikemen && signedIn && !game->ikemenGame.isEmpty());
	m_leaderboardButton->setToolTip(ikemen && game->ikemenGame.isEmpty() ? tr("Give the game its IKEMEN GO game id (Edit) to see its leaderboard")
	                                                                     : tr("This game's ranked players on this server, with their levels"));
	m_chatInput->setEnabled(state != LauncherCore::State::SignedOut && state != LauncherCore::State::SigningIn);
	m_games->setEnabled(state != LauncherCore::State::Matchmaking && state != LauncherCore::State::Playing);
}

void LauncherWindow::appendChat(const QString& name, const QString& text) {
	m_chat->append(QStringLiteral("<span style='color:#2ee6d6'>%1</span>&nbsp; %2").arg(name.toHtmlEscaped(), text.toHtmlEscaped()));
	m_chat->verticalScrollBar()->setValue(m_chat->verticalScrollBar()->maximum());
}

void LauncherWindow::openSettings() {
	SettingsDialog dialog(m_settings, this);
	dialog.exec();
	updateState();
}

void LauncherWindow::editGame(bool add) {
	Game game;
	if (!add) {
		const Game* current = m_catalog->find(selectedGame());
		if (!current) {
			return;
		}
		game = *current;
	}
	GameDialog dialog(game, this);
	if (dialog.exec() != QDialog::Accepted) {
		return;
	}
	Game edited = dialog.game();
	if (!add && edited.id != game.id) {
		m_catalog->remove(game.id);
	}
	m_catalog->put(edited);
	QString error;
	if (!m_catalog->save(&error)) {
		QMessageBox::warning(this, tr("Games"), error);
	}
	// an open leaderboard of the game shows the game as it is now
	if (!add && m_leaderboard && m_leaderboard->gameId() == game.id) {
		if (edited.kind == QLatin1String("ikemen")) {
			m_leaderboard->setGame(edited);
			m_leaderboard->reload();
		} else {
			m_leaderboard->close();   // deletes itself, later
			m_leaderboard = nullptr;
		}
	}
	reloadGames();
}

void LauncherWindow::removeGame() {
	QString id = selectedGame();
	if (id.isEmpty()) {
		return;
	}
	m_catalog->remove(id);
	QString error;
	m_catalog->save(&error);
	reloadGames();
}
