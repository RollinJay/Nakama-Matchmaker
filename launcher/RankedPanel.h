/* SPDX-License-Identifier: MIT */
#pragma once

#include "RankedSets.h"

#include <QList>
#include <QWidget>

class QComboBox;
class QLabel;
class QPushButton;
class QTreeWidget;
class ServerManager;

/* The Ranked tab of the servers' window, for a server that serves IKEMEN GO: its ranked
 * sets (the disputed ones first of all), and what a moderator does with them: export a
 * player's replay of a set to watch it in IKEMEN GO, settle the set for its winner, or
 * void it. It works while the server runs, whether this window or another program of this
 * computer runs it (ServerManager::moderatorRpc). */
class RankedPanel : public QWidget {
	Q_OBJECT

public:
	explicit RankedPanel(ServerManager*, QWidget* parent = nullptr);

	/* Shows another server's sets; they are read when the tab is shown (activate). */
	void setServer(const QString& id);
	/* The tab is shown: reads the sets unless they were read already. */
	void activate();
	/* The server started or stopped. */
	void updateState();

private:
	void refresh(bool more = false);
	void showDetails();
	const RankedSet* current() const;
	void exportReplay();
	void decide(bool voiding);
	void setBusy(const QString& message);
	void setMessage(const QString& text, bool error);

	ServerManager* m_manager;
	QString m_server;
	QList<RankedSet> m_sets;
	QString m_cursor;      // the next page of the list
	QString m_summary;     // what the list holds
	bool m_loaded = false;
	bool m_running = false;
	int m_request = 0;     // answers to earlier requests are dropped
	bool m_busy = false;
	QString m_folder;      // where the last replay was saved

	QComboBox* m_status = nullptr;
	QPushButton* m_refresh = nullptr;
	QLabel* m_state = nullptr;
	QTreeWidget* m_list = nullptr;
	QPushButton* m_more = nullptr;
	QLabel* m_details = nullptr;
	QPushButton* m_export = nullptr;
	QPushButton* m_settle = nullptr;
	QPushButton* m_void = nullptr;
	QLabel* m_message = nullptr;
};
