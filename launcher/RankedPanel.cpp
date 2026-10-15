/* SPDX-License-Identifier: MIT */
#include "RankedPanel.h"

#include "ServerManager.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSaveFile>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {

const int PAGE = 50;

bool decidable(const RankedSet& set) {
	return set.status == QLatin1String("open") || set.status == QLatin1String("disputed");
}

}

RankedPanel::RankedPanel(ServerManager* manager, QWidget* parent)
	: QWidget(parent)
	, m_manager(manager) {
	QVBoxLayout* layout = new QVBoxLayout(this);
	QLabel* intro = new QLabel(tr("IKEMEN GO's ranked sets: each best-of set of a ranked session, whose two players each claim its winner. A set whose claims disagree, "
	                              "or whose second claim did not come in time, is disputed: watch a player's replay of it in IKEMEN GO, then settle it for its winner, "
	                              "or void it."));
	intro->setObjectName("dim");
	intro->setWordWrap(true);
	layout->addWidget(intro);

	QHBoxLayout* top = new QHBoxLayout;
	m_status = new QComboBox;
	m_status->addItem(tr("Disputed sets"), QStringLiteral("disputed"));
	m_status->addItem(tr("Open sets"), QStringLiteral("open"));
	m_status->addItem(tr("Settled sets"), QStringLiteral("settled"));
	m_status->addItem(tr("Void sets"), QStringLiteral("void"));
	m_status->addItem(tr("All sets"), QStringLiteral("all"));
	connect(m_status, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, [this]() { refresh(); });
	m_refresh = new QPushButton(tr("Refresh"));
	connect(m_refresh, &QPushButton::clicked, this, [this]() { refresh(); });
	m_state = new QLabel;
	m_state->setObjectName("dim");
	m_state->setTextFormat(Qt::PlainText);
	m_state->setWordWrap(true);
	top->addWidget(m_status);
	top->addWidget(m_refresh);
	top->addWidget(m_state, 1);
	layout->addLayout(top);

	m_list = new QTreeWidget;
	m_list->setHeaderLabels({ tr("Created"), tr("Players"), tr("Status"), tr("Claims"), tr("Replays"), tr("Set") });
	m_list->setRootIsDecorated(false);
	m_list->setUniformRowHeights(true);
	m_list->setAllColumnsShowFocus(true);
	m_list->setSelectionMode(QAbstractItemView::SingleSelection);
	m_list->header()->setStretchLastSection(true);
	connect(m_list, &QTreeWidget::itemSelectionChanged, this, &RankedPanel::showDetails);
	layout->addWidget(m_list, 1);
	m_more = new QPushButton(tr("More sets"));
	m_more->setToolTip(tr("The next, older sets of the list"));
	connect(m_more, &QPushButton::clicked, this, [this]() { refresh(true); });
	QHBoxLayout* moreRow = new QHBoxLayout;
	moreRow->addWidget(m_more);
	moreRow->addStretch();
	layout->addLayout(moreRow);

	m_details = new QLabel;
	m_details->setTextFormat(Qt::PlainText);
	m_details->setTextInteractionFlags(Qt::TextSelectableByMouse);
	m_details->setWordWrap(true);
	m_details->setMinimumHeight(m_details->fontMetrics().lineSpacing() * 6);
	m_details->setAlignment(Qt::AlignLeft | Qt::AlignTop);
	layout->addWidget(m_details);

	QHBoxLayout* actions = new QHBoxLayout;
	m_export = new QPushButton(tr("Export replay..."));
	m_export->setToolTip(tr("Save a player's replay of the set, which IKEMEN GO plays (WATCH MODE > REPLAY)"));
	m_settle = new QPushButton(tr("Settle..."));
	m_settle->setToolTip(tr("Decide the set's winner: both players' ratings change once"));
	m_void = new QPushButton(tr("Void..."));
	m_void->setToolTip(tr("Decide that the set has no result: nobody's rating changes"));
	connect(m_export, &QPushButton::clicked, this, &RankedPanel::exportReplay);
	connect(m_settle, &QPushButton::clicked, this, [this]() { decide(false); });
	connect(m_void, &QPushButton::clicked, this, [this]() { decide(true); });
	actions->addWidget(m_export);
	actions->addWidget(m_settle);
	actions->addWidget(m_void);
	actions->addStretch();
	layout->addLayout(actions);
	m_message = new QLabel;
	m_message->setTextFormat(Qt::PlainText);
	m_message->setTextInteractionFlags(Qt::TextSelectableByMouse);
	m_message->setWordWrap(true);
	layout->addWidget(m_message);
	showDetails();
}

void RankedPanel::setServer(const QString& id) {
	if (id == m_server) {
		return;
	}
	m_server = id;
	++m_request;   // what was asked of the other server is dropped
	m_busy = false;
	m_loaded = false;
	m_sets.clear();
	m_cursor.clear();
	m_summary.clear();
	m_list->clear();
	setMessage(QString(), false);
	updateState();
}

void RankedPanel::activate() {
	if (!m_loaded && !m_busy) {
		refresh();
	}
}

void RankedPanel::updateState() {
	ServerProfile profile;
	QString why;
	bool running = !m_server.isEmpty() && m_manager->runningProfile(m_server, &profile, &why);
	bool started = running && !m_running;
	m_running = running;
	if (!m_busy) {
		m_state->setText(running || m_server.isEmpty() ? m_summary : why + tr(" Its ranked sets are in its database, which only the running server reads."));
	}
	if (started && isVisible()) {
		refresh();   // the server has just started
	}
	showDetails();
}

void RankedPanel::setBusy(const QString& message) {
	m_busy = true;
	m_state->setText(message);
	showDetails();
}

void RankedPanel::setMessage(const QString& text, bool error) {
	m_message->setText(text);
	m_message->setStyleSheet(error ? QStringLiteral("color: #ff8080;") : QString());
}

const RankedSet* RankedPanel::current() const {
	QTreeWidgetItem* item = m_list->currentItem();
	if (!item || !item->isSelected()) {
		return nullptr;
	}
	int index = item->data(0, Qt::UserRole).toInt();
	return index >= 0 && index < m_sets.size() ? &m_sets.at(index) : nullptr;
}

void RankedPanel::refresh(bool more) {
	if (m_server.isEmpty()) {
		return;
	}
	ServerProfile profile;
	QString why;
	if (!m_manager->runningProfile(m_server, &profile, &why)) {
		++m_request;
		m_busy = false;
		m_running = false;
		m_loaded = false;
		m_sets.clear();
		m_cursor.clear();
		m_summary.clear();
		m_list->clear();
		updateState();
		return;
	}
	QString status = m_status->currentData().toString();
	QJsonObject payload { { QStringLiteral("status"), status }, { QStringLiteral("limit"), PAGE } };
	if (more && !m_cursor.isEmpty()) {
		payload.insert(QStringLiteral("cursor"), m_cursor);
	}
	int request = ++m_request;
	QString selected;
	if (const RankedSet* set = current()) {
		selected = set->matchId;
	}
	setBusy(tr("Reading the ranked sets..."));
	m_manager->moderatorRpc(m_server, QStringLiteral("ikemen_ranked_list"), payload, this, [this, request, more, selected, status](const QJsonObject& answer, const QString& error) {
		if (request != m_request) {
			return;
		}
		m_busy = false;
		m_loaded = true;
		m_state->setText(m_summary);
		if (!error.isEmpty()) {
			setMessage(error, true);
			showDetails();
			return;
		}
		if (!more) {
			m_sets.clear();
			m_list->clear();
		}
		for (const QJsonValue& v : answer.value(QStringLiteral("sets")).toArray()) {
			RankedSet set = RankedSet::fromJson(v.toObject());
			QTreeWidgetItem* item = new QTreeWidgetItem({ RankedSet::timeLabel(set.created), set.playersLabel(), set.statusLabel(), set.claimsLabel(),
			                                               set.replaysLabel(), set.matchId });
			item->setData(0, Qt::UserRole, m_sets.size());
			item->setToolTip(1, set.playersLabel());
			item->setToolTip(3, set.claimsLabel());
			m_sets.append(set);
			m_list->addTopLevelItem(item);
			if (set.matchId == selected) {
				m_list->setCurrentItem(item);
			}
		}
		for (int column = 0; column < m_list->columnCount() - 1; ++column) {
			m_list->resizeColumnToContents(column);
			m_list->setColumnWidth(column, qMin(m_list->columnWidth(column), 300));   // the tooltips and the details show the rest
		}
		m_cursor = answer.value(QStringLiteral("cursor")).toString();
		if (m_sets.isEmpty()) {
			m_summary = status == QLatin1String("all") ? tr("The server has no ranked sets yet.") : tr("No %1 sets.").arg(status);
		} else {
			m_summary = m_sets.size() == 1 ? tr("1 set.") : tr("%1 sets, newest first.").arg(m_sets.size());
		}
		m_state->setText(m_summary);
		showDetails();
	});
}

void RankedPanel::showDetails() {
	const RankedSet* set = current();
	m_details->setText(set ? set->describe().join(QLatin1Char('\n')) : tr("Select a set to see its players, claims and replays."));
	// one request at a time: a new list would drop the answer of an export or a settlement
	bool ready = m_running && !m_busy;
	m_refresh->setEnabled(ready);
	m_status->setEnabled(ready);
	m_more->setVisible(!m_cursor.isEmpty());
	m_more->setEnabled(ready);
	m_export->setEnabled(ready && set && !set->replays.isEmpty());
	m_export->setToolTip(set && set->replays.isEmpty() ? tr("No replay is kept for this set")
	                                                   : tr("Save a player's replay of the set, which IKEMEN GO plays (WATCH MODE > REPLAY)"));
	m_settle->setEnabled(ready && set && decidable(*set));
	m_void->setEnabled(ready && set && decidable(*set));
}

void RankedPanel::exportReplay() {
	const RankedSet* chosen = current();
	if (!chosen || chosen->replays.isEmpty()) {
		return;
	}
	RankedSet set = *chosen;
	QString userId = set.replays.first().userId;
	if (set.replays.size() > 1) {
		// both hold the set's inputs; one can be incomplete (its player left, or it hit the size limit)
		QStringList items;
		int preferred = -1;
		for (int i = 0; i < set.replays.size(); ++i) {
			const RankedSet::Replay& r = set.replays.at(i);
			items.append(tr("%1's replay (%2, %3)").arg(set.name(r.userId), RankedSet::sizeLabel(r.bytes), r.complete ? tr("complete") : tr("incomplete")));
			if (preferred < 0 && r.complete) {
				preferred = i;
			}
		}
		bool ok = false;
		QString item = QInputDialog::getItem(this, tr("Export replay"), tr("Both players' replays of a set hold the same inputs. Which one?"), items,
		                                     preferred < 0 ? 0 : preferred, false, &ok);
		if (!ok) {
			return;
		}
		userId = set.replays.at(qMax(0, items.indexOf(item))).userId;
	}
	QJsonObject payload { { QStringLiteral("match_id"), set.matchId }, { QStringLiteral("user_id"), userId } };
	int request = ++m_request;
	setBusy(tr("Fetching the replay..."));
	setMessage(QString(), false);
	m_manager->moderatorRpc(m_server, QStringLiteral("ikemen_ranked_replay"), payload, this, [this, request, set](const QJsonObject& answer, const QString& error) {
		if (request != m_request) {
			return;
		}
		m_busy = false;
		m_state->setText(m_summary);
		showDetails();
		if (!error.isEmpty()) {
			setMessage(error, true);
			return;
		}
		QByteArray replay = QByteArray::fromBase64(answer.value(QStringLiteral("replay")).toString().toLatin1());
		if (replay.isEmpty()) {
			setMessage(tr("The server sent no replay for the set %1.").arg(set.matchId), true);
			return;
		}
		QString name = RankedSet::safeFileName(answer.value(QStringLiteral("file_name")).toString());
		QString folder = m_folder.isEmpty() ? QDir::homePath() : m_folder;
		QString path = QFileDialog::getSaveFileName(this, tr("Save the replay"), QDir(folder).filePath(name), tr("IKEMEN GO replays (*.replay);;All files (*)"));
		if (path.isEmpty()) {
			return;
		}
		// a file the dialog let the moderator replace stays as it was until the new one is
		// whole (flush first: commit does not notice that writing the buffer failed)
		QSaveFile file(path);
		if (!file.open(QIODevice::WriteOnly) || file.write(replay) != replay.size() || !file.flush() || !file.commit()) {
			setMessage(tr("Cannot write %1: %2").arg(QDir::toNativeSeparators(path), file.errorString()), true);
			return;
		}
		m_folder = QFileInfo(path).absolutePath();
		int matches = answer.value(QStringLiteral("matches")).toInt();
		setMessage(tr("Saved %1: %2's replay, %3, %4, %5. To watch it, put it in the save/replays folder of an IKEMEN GO with the set's game and build "
		              "(%6, %7) and open WATCH MODE > REPLAY, or run Ikemen_GO -replay <file>.")
		               .arg(QDir::toNativeSeparators(path), set.name(answer.value(QStringLiteral("user_id")).toString()),
		                    matches == 1 ? tr("1 match") : tr("%1 matches").arg(matches), RankedSet::sizeLabel(replay.size()),
		                    answer.value(QStringLiteral("complete")).toBool() ? tr("complete") : tr("incomplete"), set.game, set.build),
		           false);
	});
}

void RankedPanel::decide(bool voiding) {
	const RankedSet* chosen = current();
	if (!chosen || !decidable(*chosen)) {
		return;
	}
	RankedSet set = *chosen;
	QDialog dialog(this);
	dialog.setWindowTitle(voiding ? tr("Void a ranked set") : tr("Settle a ranked set"));
	dialog.setMinimumWidth(520);
	QFormLayout* form = new QFormLayout(&dialog);
	QLabel* about = new QLabel(voiding ? tr("Void the set %1 (%2)? It then has no result: nobody's rating changes, and it cannot be settled later.").arg(set.matchId, set.playersLabel())
	                                   : tr("Who won the set %1? Both players' ratings change once, as when both claim the same winner.").arg(set.matchId));
	about->setWordWrap(true);
	about->setTextFormat(Qt::PlainText);
	form->addRow(about);
	QButtonGroup* group = new QButtonGroup(&dialog);
	QList<QRadioButton*> choices;
	if (!voiding) {
		for (const RankedSet::Player& p : set.players) {
			QString claim;
			for (const RankedSet::Claim& c : set.claims) {
				if (c.userId == p.userId) {
					claim = tr(" (claimed that %1 won)").arg(set.name(c.winner));
				}
			}
			QRadioButton* choice = new QRadioButton(tr("%1 won%2").arg(p.username.isEmpty() ? p.userId : p.username, claim));
			choice->setToolTip(p.userId);
			group->addButton(choice);
			choices.append(choice);
			form->addRow(choice);
		}
	}
	QLineEdit* note = new QLineEdit;
	note->setMaxLength(500);
	note->setPlaceholderText(tr("kept on the set's record, such as what the replay showed (optional)"));
	form->addRow(tr("Note"), note);
	QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	buttons->button(QDialogButtonBox::Ok)->setText(voiding ? tr("Void") : tr("Settle"));
	buttons->button(QDialogButtonBox::Ok)->setEnabled(voiding);
	connect(group, static_cast<void (QButtonGroup::*)(QAbstractButton*)>(&QButtonGroup::buttonClicked), &dialog,
	        [buttons]() { buttons->button(QDialogButtonBox::Ok)->setEnabled(true); });
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	form->addRow(buttons);
	if (dialog.exec() != QDialog::Accepted) {
		return;
	}
	QJsonObject payload { { QStringLiteral("match_id"), set.matchId } };
	QString winner;
	if (voiding) {
		payload.insert(QStringLiteral("void"), true);
	} else {
		for (int i = 0; i < choices.size(); ++i) {
			if (choices.at(i)->isChecked()) {
				winner = set.players.at(i).userId;
			}
		}
		if (winner.isEmpty()) {
			return;
		}
		payload.insert(QStringLiteral("winner"), winner);
	}
	if (!note->text().trimmed().isEmpty()) {
		payload.insert(QStringLiteral("note"), note->text().trimmed());
	}
	int request = ++m_request;
	setBusy(voiding ? tr("Voiding the set...") : tr("Settling the set..."));
	setMessage(QString(), false);
	m_manager->moderatorRpc(m_server, QStringLiteral("ikemen_ranked_settle"), payload, this, [this, request, set, winner](const QJsonObject& answer, const QString& error) {
		if (request != m_request) {
			return;
		}
		m_busy = false;
		if (!error.isEmpty()) {
			setMessage(error, true);
		} else if (answer.value(QStringLiteral("status")).toString() == QLatin1String("void")) {
			setMessage(tr("Voided the set %1 (%2): nobody's rating changed.").arg(set.matchId, set.playersLabel()), false);
		} else {
			setMessage(tr("Settled the set %1 (%2): %3 won, and both players' ratings changed once.").arg(set.matchId, set.playersLabel(), set.name(winner)), false);
		}
		refresh();   // the set's new status, or what changed it meanwhile
	});
}
