/* SPDX-License-Identifier: MIT */
#pragma once

#include "GameCatalog.h"

#include <QDialog>

class QComboBox;
class QLineEdit;
class QSpinBox;

class GameDialog : public QDialog {
	Q_OBJECT

public:
	GameDialog(const Game&, QWidget* parent = nullptr);
	Game game() const;

private:
	void updateKind();

	Game m_game;
	QLineEdit* m_id;
	QLineEdit* m_title;
	QComboBox* m_kind;
	QLineEdit* m_profile;
	QSpinBox* m_players;
	QLineEdit* m_ikemenGame;
	QLineEdit* m_ikemenBuild;
	QLineEdit* m_args;
};
