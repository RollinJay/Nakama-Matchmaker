/* SPDX-License-Identifier: MIT */
#include "LeaderboardDialog.h"

#include "GameCatalog.h"
#include "LauncherCore.h"

#include <QColor>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {

const int PAGE = 50;   // records per page; the RPC takes 1 to 100

enum Column { Rank, Name, Level, Won, Lost, Columns };

qint64 whole(const QJsonValue& v) {
	return qint64(v.toDouble(0));
}

}

LeaderboardDialog::LeaderboardDialog(LauncherCore* core, const Game& game, QWidget* parent)
	: QDialog(parent)
	, m_core(core) {
	setGame(game);
	resize(600, 600);
	QVBoxLayout* rows = new QVBoxLayout(this);
	m_heading = new QLabel;
	m_heading->setObjectName("heading");
	m_heading->setTextFormat(Qt::PlainText);
	m_heading->setWordWrap(true);
	rows->addWidget(m_heading);
	m_list = new QTreeWidget;
	m_list->setColumnCount(Columns);
	m_list->setHeaderLabels({ tr("#"), tr("Player"), tr("Level"), tr("Won"), tr("Lost") });
	m_list->setRootIsDecorated(false);
	m_list->setUniformRowHeights(true);
	m_list->setSelectionMode(QAbstractItemView::NoSelection);
	m_list->setFocusPolicy(Qt::NoFocus);
	m_list->header()->setStretchLastSection(false);
	m_list->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
	m_list->header()->setSectionResizeMode(Name, QHeaderView::Stretch);
	for (int c : { int(Rank), int(Level), int(Won), int(Lost) }) {
		m_list->headerItem()->setTextAlignment(c, Qt::AlignRight | Qt::AlignVCenter);
	}
	rows->addWidget(m_list, 1);
	m_mine = new QLabel;
	m_mine->setTextFormat(Qt::PlainText);
	m_mine->setStyleSheet(QStringLiteral("color: #2ee6d6; font-weight: bold;"));
	rows->addWidget(m_mine);
	m_status = new QLabel;
	m_status->setObjectName("dim");
	m_status->setTextFormat(Qt::PlainText);
	m_status->setWordWrap(true);
	rows->addWidget(m_status);
	QHBoxLayout* buttons = new QHBoxLayout;
	m_more = new QPushButton(tr("More"));
	m_more->setToolTip(tr("The next players"));
	m_refresh = new QPushButton(tr("Refresh"));
	QPushButton* close = new QPushButton(tr("Close"));
	connect(m_more, &QPushButton::clicked, this, [this]() { request(m_cursor); });
	connect(m_refresh, &QPushButton::clicked, this, &LeaderboardDialog::reload);
	connect(close, &QPushButton::clicked, this, &QDialog::close);
	buttons->addWidget(m_more);
	buttons->addWidget(m_refresh);
	buttons->addStretch();
	buttons->addWidget(close);
	rows->addLayout(buttons);

	connect(m_core, &LauncherCore::stateChanged, this, [this](LauncherCore::State state) {
		if (state == LauncherCore::State::SignedOut) {
			++m_request;   // the answer of a client that is gone never comes
			m_loading = false;
			// the next sign-in may be to another server, or as another account: it reads
			// the leaderboard again, and nothing of this one stays meanwhile
			m_loaded = false;
			m_cursor.clear();
			m_list->clear();
			m_mine->clear();
		} else if (state == LauncherCore::State::Online && !m_loaded && !m_loading) {
			reload();
		}
		updateState();
	});
	// a community server's name comes just after the sign-in
	connect(m_core, &LauncherCore::communityChanged, this, &LeaderboardDialog::updateHeading);
	reload();
}

void LeaderboardDialog::setGame(const Game& game) {
	m_gameId = game.id;
	m_title = game.title;
	m_ikemenGame = game.ikemenGame;
	setWindowTitle(tr("Leaderboard: %1").arg(game.title));
}

void LeaderboardDialog::reload() {
	m_cursor.clear();
	m_loaded = false;
	request(QString());
}

void LeaderboardDialog::updateHeading() {
	NetplayOnline::NakamaClient* client = m_core->client();
	QString where = m_core->community().title;
	if (where.isEmpty() && client) {
		where = client->server().host;
	}
	m_heading->setText(where.isEmpty() ? m_title : tr("%1 on %2").arg(m_title, where));
}

void LeaderboardDialog::request(const QString& cursor) {
	NetplayOnline::NakamaClient* client = m_core->client();
	updateHeading();
	LauncherCore::State state = m_core->state();
	bool signedIn = state == LauncherCore::State::Online || state == LauncherCore::State::Matchmaking || state == LauncherCore::State::Playing;
	if (!client || !signedIn) {
		updateState();
		return;
	}
	if (m_ikemenGame.isEmpty()) {
		m_status->setText(tr("This game has no IKEMEN GO game id: give it one with Edit."));
		updateState();
		return;
	}
	int id = ++m_request;
	m_loading = true;
	m_status->setText(tr("Reading the leaderboard..."));
	updateState();
	QJsonObject payload { { QStringLiteral("game"), m_ikemenGame }, { QStringLiteral("limit"), PAGE } };
	if (!cursor.isEmpty()) {
		payload.insert(QStringLiteral("cursor"), cursor);
	}
	bool more = !cursor.isEmpty();
	QPointer<LeaderboardDialog> self(this);
	client->rpc(QStringLiteral("ikemen_leaderboard"), payload, [self, id, more](bool ok, const QByteArray& reply, const QString& error) {
		if (!self || id != self->m_request) {
			return;   // closed, or another page was asked for since
		}
		self->m_loading = false;
		QJsonDocument doc = QJsonDocument::fromJson(reply);
		if (!ok) {
			self->failed(error);
		} else if (!doc.isObject()) {
			self->failed(tr("the server's answer is not a leaderboard"));
		} else {
			self->received(doc.object(), more);
		}
	});
}

QString LeaderboardDialog::recordLine(const QJsonObject& r) const {
	// one arg() for all, which does not look for placeholders in the label it puts in
	return tr("You: #%1 · %2 %3 · %4 won, %5 lost")
	    .arg(QString::number(whole(r.value(QStringLiteral("rank")))), m_label, r.value(QStringLiteral("level_text")).toString(),
	         QString::number(whole(r.value(QStringLiteral("wins")))), QString::number(whole(r.value(QStringLiteral("losses")))));
}

void LeaderboardDialog::received(const QJsonObject& answer, bool more) {
	m_label = answer.value(QStringLiteral("label")).toString();
	if (m_label.isEmpty()) {
		m_label = tr("Level");
	}
	m_list->headerItem()->setText(Level, m_label);
	if (!more) {
		m_list->clear();
	}
	QString me = m_core->userId();
	for (const QJsonValue& v : answer.value(QStringLiteral("records")).toArray()) {
		QJsonObject r = v.toObject();
		// the level as the server wrote it: levels pass 2^31, and the text is the same everywhere
		QString level = r.value(QStringLiteral("level_text")).toString();
		if (level.isEmpty()) {
			level = QString::number(whole(r.value(QStringLiteral("level"))));
		}
		QTreeWidgetItem* item = new QTreeWidgetItem(QStringList {
			QString::number(whole(r.value(QStringLiteral("rank")))),
			r.value(QStringLiteral("name")).toString(),
			level,
			QString::number(whole(r.value(QStringLiteral("wins")))),
			QString::number(whole(r.value(QStringLiteral("losses")))),
		});
		for (int c : { int(Rank), int(Level), int(Won), int(Lost) }) {
			item->setTextAlignment(c, Qt::AlignRight | Qt::AlignVCenter);
		}
		if (!me.isEmpty() && r.value(QStringLiteral("user_id")).toString() == me) {
			QFont bold = item->font(Name);
			bold.setBold(true);
			for (int c = 0; c < Columns; ++c) {
				item->setFont(c, bold);
				item->setForeground(c, QColor(0x2e, 0xe6, 0xd6));
			}
		}
		m_list->addTopLevelItem(item);
	}
	m_cursor = answer.value(QStringLiteral("cursor")).toString();
	QJsonObject mine = answer.value(QStringLiteral("mine")).toObject();
	m_mine->setText(mine.isEmpty() ? tr("You have no ranked set of this game on this server yet.") : recordLine(mine));
	int count = m_list->topLevelItemCount();
	if (count == 0) {
		m_status->setText(tr("No ranked set of this game has been decided on this server yet."));
	} else {
		QString players = count == 1 ? tr("1 player") : tr("%1 players").arg(count);
		m_status->setText(m_cursor.isEmpty() ? tr("%1, highest rating first.").arg(players)
		                                     : tr("The first %1, highest rating first; More shows the next ones.").arg(players));
	}
	m_loaded = true;
	updateState();
}

void LeaderboardDialog::failed(const QString& error) {
	if (error.contains(QLatin1String("RPC function not found"), Qt::CaseInsensitive)) {
		m_status->setText(tr("This server has no IKEMEN GO leaderboards: it runs without IKEMEN GO's modules, or with older ones."));
	} else {
		m_status->setText(tr("The leaderboard could not be read: %1").arg(error));
	}
	updateState();
}

void LeaderboardDialog::updateState() {
	LauncherCore::State state = m_core->state();
	bool signedIn = state == LauncherCore::State::Online || state == LauncherCore::State::Matchmaking || state == LauncherCore::State::Playing;
	if (!signedIn) {
		m_status->setText(tr("Sign in to see the leaderboard."));
	}
	m_refresh->setEnabled(signedIn && !m_loading);
	m_more->setEnabled(signedIn && !m_loading && m_loaded && !m_cursor.isEmpty());
}
