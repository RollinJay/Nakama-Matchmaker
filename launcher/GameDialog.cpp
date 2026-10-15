/* SPDX-License-Identifier: MIT */
#include "GameDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSpinBox>

GameDialog::GameDialog(const Game& game, QWidget* parent)
	: QDialog(parent)
	, m_game(game) {
	setWindowTitle(game.id.isEmpty() ? tr("Add a game") : tr("Edit %1").arg(game.title));
	QFormLayout* form = new QFormLayout(this);
	m_title = new QLineEdit(game.title);
	form->addRow(tr("Title"), m_title);
	m_id = new QLineEdit(game.id);
	m_id->setToolTip(tr("Letters, digits and dashes: names the game's room"));
	form->addRow(tr("Id"), m_id);
	m_kind = new QComboBox;
	m_kind->addItem(tr("GBA (mGBA)"), QStringLiteral("mgba"));
	m_kind->addItem(tr("DS (melonDS)"), QStringLiteral("melonds"));
	m_kind->addItem(tr("IKEMEN GO"), QStringLiteral("ikemen"));
	int kind = m_kind->findData(game.kind.isEmpty() ? QStringLiteral("mgba") : game.kind);
	m_kind->setCurrentIndex(kind < 0 ? 0 : kind);
	connect(m_kind, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, &GameDialog::updateKind);
	form->addRow(tr("Kind"), m_kind);

	QWidget* profileRow = new QWidget;
	QHBoxLayout* profileLayout = new QHBoxLayout(profileRow);
	profileLayout->setContentsMargins(0, 0, 0, 0);
	m_profile = new QLineEdit(game.profile);
	QPushButton* browse = new QPushButton(tr("Browse..."));
	connect(browse, &QPushButton::clicked, this, [this]() {
		bool ds = m_kind->currentData().toString() == QLatin1String("melonds");
		QString path = ds ? QFileDialog::getOpenFileName(this, tr("Select a DS ROM"), m_profile->text(), tr("DS ROMs (*.nds);;All files (*)"))
		                  : QFileDialog::getOpenFileName(this, tr("Select a netplay profile"), m_profile->text(), tr("Netplay profiles (*.ini)"));
		if (!path.isEmpty()) {
			m_profile->setText(path);
		}
	});
	profileLayout->addWidget(m_profile, 1);
	profileLayout->addWidget(browse);
	m_profileLabel = new QLabel(tr("Netplay profile"));
	form->addRow(m_profileLabel, profileRow);
	m_players = new QSpinBox;
	m_players->setRange(2, 4);
	m_players->setValue(game.players);
	m_mgbaPlayers = game.kind == QLatin1String("mgba") ? game.players : 2;
	connect(m_players, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), this, [this](int value) {
		if (m_kind->currentData().toString() == QLatin1String("mgba")) {
			m_mgbaPlayers = value;
		}
	});
	form->addRow(tr("Players per match"), m_players);
	m_ikemenGame = new QLineEdit(game.ikemenGame);
	form->addRow(tr("IKEMEN GO game id"), m_ikemenGame);
	m_ikemenBuild = new QLineEdit(game.ikemenBuild);
	form->addRow(tr("IKEMEN GO build"), m_ikemenBuild);
	m_args = new QLineEdit(game.args.join(QLatin1Char(' ')));
	form->addRow(tr("Extra arguments"), m_args);
	QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	form->addRow(buttons);
	updateKind();
}

void GameDialog::updateKind() {
	QString kind = m_kind->currentData().toString();
	bool emulator = kind != QLatin1String("ikemen");
	m_profile->parentWidget()->setEnabled(emulator);
	// a DS game is its ROM (kept in the profile field), and the melonDS fork plays two
	// players online
	bool ds = kind == QLatin1String("melonds");
	m_profileLabel->setText(ds ? tr("ROM") : tr("Netplay profile"));
	m_profile->setToolTip(ds ? tr("The DS ROM (.nds), or an archive that holds one") : tr("The mGBA fork's netplay profile (.ini)"));
	m_players->setValue(ds ? 2 : m_mgbaPlayers);
	m_players->setEnabled(kind == QLatin1String("mgba"));
	m_ikemenGame->setEnabled(!emulator);
	m_ikemenBuild->setEnabled(!emulator);
}

Game GameDialog::game() const {
	Game g = m_game;
	g.title = m_title->text().trimmed();
	g.id = m_id->text().trimmed().toLower().replace(QRegularExpression(QStringLiteral("[^a-z0-9-]+")), QStringLiteral("-"));
	if (g.id.isEmpty()) {
		g.id = g.title.toLower().replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral("-"));
	}
	g.kind = m_kind->currentData().toString();
	g.profile = m_profile->text().trimmed();
	g.players = g.kind == QLatin1String("melonds") ? 2 : m_players->value();
	g.ikemenGame = m_ikemenGame->text().trimmed();
	g.ikemenBuild = m_ikemenBuild->text().trimmed();
	g.args.clear();
	for (const QString& arg : m_args->text().split(QLatin1Char(' '))) {
		if (!arg.isEmpty()) {
			g.args.append(arg);
		}
	}
	return g;
}
