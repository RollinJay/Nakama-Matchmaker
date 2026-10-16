/* SPDX-License-Identifier: MIT */
#pragma once

#include "ServerHost.h"

#include <QWidget>

class QCheckBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QRadioButton;
class QSpinBox;
class QTabWidget;
class QTableWidget;
class RankedPanel;
class ServerManager;
class Settings;
struct ServerProfile;

/* The community servers in a window: the servers on the left; for the chosen one, its
 * state, the buttons that start and stop it, its settings, its log, what players enter,
 * and for a server with IKEMEN GO the levels its players see for their ranked ratings and
 * its ranked sets (RankedPanel); and the programs every server uses. Servers go on running
 * when the window is closed. */
class ServersWindow : public QWidget {
	Q_OBJECT

public:
	ServersWindow(ServerManager*, Settings*, QWidget* parent = nullptr);

	/* Saves the shown server's unsaved changes, or drops them, after asking; false when
	 * the user cancels. */
	bool settle();

protected:
	void closeEvent(QCloseEvent*) override;
	void changeEvent(QEvent*) override;

private:
	void reload(const QString& select = QString());
	QString selected() const;
	void showServer(const QString& id);
	void updateState();
	void updateItem(const QString& id);
	ServerProfile edited() const;
	bool save();
	void add();
	void remove();
	void start();
	void useInLauncher();
	void writeIkemenProfile();
	void markDirty();
	/* Reads servers.json again when another program changed it, so that a change made here
	 * does not undo theirs. */
	bool refresh();
	QLineEdit* fileField(QWidget** row, bool folder);
	/* The Ranked tab is there for a server that serves IKEMEN GO. */
	void updateRankedTab(const ServerProfile*);
	/* The Ranked levels group: the levels of the form into a profile, or why they cannot
	 * be; and the line that says what the default's levels mean. */
	QString readLevels(ServerProfile*) const;
	void updateLevelExample();
	void addGameLevels();

	ServerManager* m_manager;
	Settings* m_settings;
	QString m_current;
	bool m_dirty = false;
	bool m_loading = false;

	QListWidget* m_list = nullptr;
	QLabel* m_title = nullptr;
	QLabel* m_stateLabel = nullptr;
	QPushButton* m_start = nullptr;
	QPushButton* m_stop = nullptr;
	QPushButton* m_use = nullptr;
	QPushButton* m_ikemen = nullptr;
	QPushButton* m_folder = nullptr;
	QPushButton* m_remove = nullptr;
	QPushButton* m_save = nullptr;
	QTabWidget* m_tabs = nullptr;
	QWidget* m_form = nullptr;
	QLabel* m_id = nullptr;
	QLineEdit* m_titleEdit = nullptr;
	QLineEdit* m_message = nullptr;
	QCheckBox* m_kindIkemen = nullptr;
	QCheckBox* m_kindGba = nullptr;
	QCheckBox* m_kindDs = nullptr;
	QSpinBox* m_port = nullptr;
	QSpinBox* m_consolePort = nullptr;
	QLineEdit* m_bind = nullptr;
	QLineEdit* m_publicHost = nullptr;
	QSpinBox* m_publicPort = nullptr;
	QCheckBox* m_clientTls = nullptr;
	QLineEdit* m_serverKey = nullptr;
	QRadioButton* m_managed = nullptr;
	QRadioButton* m_external = nullptr;
	QSpinBox* m_databasePort = nullptr;
	QLineEdit* m_database = nullptr;
	QLineEdit* m_tlsCert = nullptr;
	QLineEdit* m_tlsKey = nullptr;
	QLineEdit* m_extraModules = nullptr;
	QWidget* m_levels = nullptr;
	QLineEdit* m_levelStart = nullptr;
	QLineEdit* m_levelTop = nullptr;
	QLineEdit* m_levelLabel = nullptr;
	QLabel* m_levelExample = nullptr;
	QTableWidget* m_gameLevels = nullptr;
	QPushButton* m_removeGameLevels = nullptr;
	QLabel* m_formError = nullptr;
	QPlainTextEdit* m_log = nullptr;
	RankedPanel* m_ranked = nullptr;
	QLabel* m_connect = nullptr;
	QLineEdit* m_nakama = nullptr;
	QLineEdit* m_postgres = nullptr;
	QLabel* m_programs = nullptr;
};
