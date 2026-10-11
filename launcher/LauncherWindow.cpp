/* SPDX-License-Identifier: MIT */
#include "LauncherWindow.h"

#include "GameCatalog.h"
#include "GameDialog.h"
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
	"QMainWindow, QWidget#central { background: #1b1726; color: #e8e4f0; }"
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
	"QLineEdit, QTextEdit { background: #15111f; color: #e8e4f0; border: 1px solid #3a3150; padding: 4px; }"
	"QPushButton { background: #3a3150; color: #e8e4f0; border: 1px solid #4c4266; padding: 6px 14px; }"
	"QPushButton:hover { background: #4c4266; }"
	"QPushButton:disabled { color: #6c6480; border-color: #2c2540; }"
	"QPushButton#primary { background: #1f7f78; border-color: #2ee6d6; font-weight: bold; }"
	"QPushButton#primary:disabled { background: #1d3533; color: #5f7d7a; border-color: #2a4a47; }";

}

LauncherWindow::LauncherWindow(Settings* settings, GameCatalog* catalog, LauncherCore* core, QWidget* parent)
	: QMainWindow(parent)
	, m_settings(settings)
	, m_catalog(catalog)
	, m_core(core) {
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
	m_gameInfo->setWordWrap(true);
	channelLayout->addWidget(m_gameInfo);
	QHBoxLayout* actions = new QHBoxLayout;
	m_find = new QPushButton(tr("Find a match"));
	m_find->setObjectName("primary");
	m_cancel = new QPushButton(tr("Cancel"));
	m_play = new QPushButton(tr("Play offline"));
	connect(m_find, &QPushButton::clicked, this, [this]() { m_core->findMatch(selectedGame()); });
	connect(m_cancel, &QPushButton::clicked, m_core, &LauncherCore::cancelMatch);
	connect(m_play, &QPushButton::clicked, this, [this]() { m_core->play(selectedGame()); });
	actions->addWidget(m_find);
	actions->addWidget(m_cancel);
	actions->addWidget(m_play);
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
	m_status->setText(tr("Not signed in."));
	reloadGames();
	updateState();
}

void LauncherWindow::closeEvent(QCloseEvent* event) {
	// the game is the launcher's child process: closing the launcher ends it
	if (m_core->state() == LauncherCore::State::Playing &&
	    QMessageBox::question(this, tr("Quit"), tr("A game is running. Closing the launcher closes the game too."),
	                          QMessageBox::Close | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Close) {
		event->ignore();
		return;
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
	if (!game) {
		m_gameTitle->setText(tr("No game"));
		m_gameInfo->setText(tr("Add a game: an mGBA or melonDS netplay profile, or IKEMEN GO."));
		updateState();
		return;
	}
	m_gameTitle->setText(game->title);
	if (game->matchesInLauncher()) {
		m_gameInfo->setText(tr("%1 · %n players · profile %2", nullptr, game->players).arg(game->kindLabel(), QFileInfo(game->profile).fileName()));
		m_gameInfo->setToolTip(game->profile);
	} else {
		m_gameInfo->setToolTip(QString());
		m_gameInfo->setText(tr("IKEMEN GO · matches are made in IKEMEN GO's online menus, with this account and server"));
	}
	m_chat->clear();
	m_core->enterRoom(game->id);
	updateState();
}

void LauncherWindow::updateState() {
	LauncherCore::State state = m_core->state();
	bool online = state == LauncherCore::State::Online;
	const Game* game = m_catalog->find(selectedGame());
	m_signIn->setText(state == LauncherCore::State::SignedOut ? tr("Sign in") : tr("Sign out"));
	m_signIn->setEnabled(state != LauncherCore::State::SigningIn && state != LauncherCore::State::Playing);
	m_account->setText(state == LauncherCore::State::SignedOut ? tr("Not signed in") : m_settings->name());
	m_find->setEnabled(online && game);
	m_find->setText(game && !game->matchesInLauncher() ? tr("Play online") : tr("Find a match"));
	m_cancel->setVisible(state == LauncherCore::State::Matchmaking);
	m_play->setEnabled(game && state != LauncherCore::State::Playing && state != LauncherCore::State::Matchmaking);
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
