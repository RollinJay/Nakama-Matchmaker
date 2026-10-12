/* SPDX-License-Identifier: MIT */
#include "ServersWindow.h"

#include "ServerManager.h"
#include "Settings.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QTabWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace {

QColor stateColor(ServerHost::State state) {
	switch (state) {
	case ServerHost::State::Running:
		return QColor(0x2e, 0xe6, 0xd6);
	case ServerHost::State::Starting:
	case ServerHost::State::Stopping:
		return QColor(0xf0, 0xc0, 0x40);
	case ServerHost::State::Failed:
		return QColor(0xff, 0x60, 0x60);
	case ServerHost::State::Stopped:
		break;
	}
	return QColor(0x9a, 0x92, 0xb0);
}

QSpinBox* portBox(int minimum) {
	QSpinBox* box = new QSpinBox;
	box->setRange(minimum, 65535);
	box->setButtonSymbols(QAbstractSpinBox::NoButtons);
	box->setMinimumWidth(80);
	return box;
}

}

ServersWindow::ServersWindow(ServerManager* manager, Settings* settings, QWidget* parent)
	: QWidget(parent, Qt::Window)
	, m_manager(manager)
	, m_settings(settings) {
	setObjectName("servers");
	setWindowTitle(tr("Community servers"));
	resize(1040, 820);

	QVBoxLayout* rows = new QVBoxLayout(this);
	QHBoxLayout* columns = new QHBoxLayout;

	QVBoxLayout* side = new QVBoxLayout;
	QLabel* heading = new QLabel(tr("SERVERS"));
	heading->setObjectName("heading");
	side->addWidget(heading);
	m_list = new QListWidget;
	m_list->setFixedWidth(240);
	connect(m_list, &QListWidget::currentRowChanged, this, [this]() {
		QString id = selected();
		if (id == m_current) {
			return;
		}
		if (!settle()) {
			m_loading = true;
			for (int i = 0; i < m_list->count(); ++i) {
				if (m_list->item(i)->data(Qt::UserRole).toString() == m_current) {
					m_list->setCurrentRow(i);
				}
			}
			m_loading = false;
			return;
		}
		showServer(id);
	});
	side->addWidget(m_list, 1);
	QHBoxLayout* listButtons = new QHBoxLayout;
	QPushButton* add = new QPushButton(tr("Add"));
	m_remove = new QPushButton(tr("Remove"));
	connect(add, &QPushButton::clicked, this, &ServersWindow::add);
	connect(m_remove, &QPushButton::clicked, this, &ServersWindow::remove);
	listButtons->addWidget(add);
	listButtons->addWidget(m_remove);
	side->addLayout(listButtons);
	columns->addLayout(side);

	QVBoxLayout* main = new QVBoxLayout;
	QHBoxLayout* titleRow = new QHBoxLayout;
	m_title = new QLabel;
	m_title->setObjectName("gameTitle");
	m_title->setTextFormat(Qt::PlainText);
	m_stateLabel = new QLabel;
	m_stateLabel->setTextFormat(Qt::PlainText);
	m_stateLabel->setWordWrap(true);
	titleRow->addWidget(m_title);
	titleRow->addSpacing(12);
	titleRow->addWidget(m_stateLabel);
	titleRow->addStretch();
	main->addLayout(titleRow);

	QHBoxLayout* actions = new QHBoxLayout;
	m_start = new QPushButton(tr("Start"));
	m_start->setObjectName("primary");
	m_stop = new QPushButton(tr("Stop"));
	m_use = new QPushButton(tr("Use in this launcher"));
	m_use->setToolTip(tr("Sign this launcher in to this server (Settings > Nakama server)"));
	m_ikemen = new QPushButton(tr("IKEMEN GO profile..."));
	m_ikemen->setToolTip(tr("Save a server profile for IKEMEN GO's data/online/servers.json"));
	m_folder = new QPushButton(tr("Open folder"));
	m_folder->setToolTip(tr("The server's folder: its database, Nakama's configuration and the log"));
	connect(m_start, &QPushButton::clicked, this, &ServersWindow::start);
	connect(m_stop, &QPushButton::clicked, this, [this]() { m_manager->stop(m_current); });
	connect(m_use, &QPushButton::clicked, this, &ServersWindow::useInLauncher);
	connect(m_ikemen, &QPushButton::clicked, this, &ServersWindow::writeIkemenProfile);
	connect(m_folder, &QPushButton::clicked, this, [this]() {
		const ServerProfile* p = m_manager->config().find(m_current);
		if (p) {
			QString dir = m_manager->config().dataDir(*p);
			QDir().mkpath(dir);
			QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
		}
	});
	for (QPushButton* b : { m_start, m_stop, m_use, m_ikemen, m_folder }) {
		actions->addWidget(b);
	}
	actions->addStretch();
	main->addLayout(actions);

	m_connect = new QLabel;
	m_connect->setObjectName("dim");
	m_connect->setTextFormat(Qt::PlainText);
	m_connect->setTextInteractionFlags(Qt::TextSelectableByMouse);
	m_connect->setWordWrap(true);
	main->addWidget(m_connect);

	m_tabs = new QTabWidget;
	m_form = new QWidget;
	QFormLayout* form = new QFormLayout(m_form);
	m_id = new QLabel;
	m_id->setTextFormat(Qt::PlainText);
	form->addRow(tr("Id"), m_id);
	m_titleEdit = new QLineEdit;
	m_titleEdit->setMaxLength(64);
	m_titleEdit->setPlaceholderText(tr("The community's name: Nakama Matchmaker shows it, and it names IKEMEN GO profiles"));
	form->addRow(tr("Title"), m_titleEdit);
	m_message = new QLineEdit;
	m_message->setMaxLength(500);
	m_message->setPlaceholderText(tr("Shown in Nakama Matchmaker when players sign in (optional)"));
	form->addRow(tr("Message"), m_message);
	QHBoxLayout* kinds = new QHBoxLayout;
	m_kindIkemen = new QCheckBox(tr("IKEMEN GO"));
	m_kindGba = new QCheckBox(tr("GBA (mGBA fork)"));
	m_kindDs = new QCheckBox(tr("DS (melonDS fork)"));
	kinds->addWidget(m_kindIkemen);
	kinds->addWidget(m_kindGba);
	kinds->addWidget(m_kindDs);
	kinds->addStretch();
	form->addRow(tr("Games"), kinds);
	QHBoxLayout* ports = new QHBoxLayout;
	m_port = portBox(2);
	m_port->setToolTip(tr("The port players connect to (TCP); the server also takes the port before it"));
	m_consolePort = portBox(4);
	m_consolePort->setToolTip(tr("Nakama's web console, for the operator; the server also takes this port - 3"));
	ports->addWidget(m_port);
	ports->addWidget(new QLabel(tr("Console (this computer only)")));
	ports->addWidget(m_consolePort);
	ports->addStretch();
	form->addRow(tr("Port"), ports);
	m_bind = new QLineEdit;
	m_bind->setPlaceholderText(tr("every address"));
	form->addRow(tr("Listen address"), m_bind);
	QHBoxLayout* publicRow = new QHBoxLayout;
	m_publicHost = new QLineEdit;
	m_publicHost->setPlaceholderText(tr("the address players enter, e.g. games.example.org"));
	m_publicPort = portBox(0);
	m_publicPort->setSpecialValueText(tr("same port"));
	m_publicPort->setToolTip(tr("The port players enter, when a proxy in front of the server listens on another one"));
	m_clientTls = new QCheckBox(tr("players use TLS"));
	m_clientTls->setToolTip(tr("Players connect with TLS: through a TLS proxy, or with the certificate below"));
	publicRow->addWidget(m_publicHost, 1);
	publicRow->addWidget(m_publicPort);
	publicRow->addWidget(m_clientTls);
	form->addRow(tr("Public address"), publicRow);
	QHBoxLayout* keyRow = new QHBoxLayout;
	m_serverKey = new QLineEdit;
	QPushButton* newKey = new QPushButton(tr("New"));
	connect(newKey, &QPushButton::clicked, this, [this]() { m_serverKey->setText(ServerConfig::secret()); });
	keyRow->addWidget(m_serverKey, 1);
	keyRow->addWidget(newKey);
	form->addRow(tr("Server key"), keyRow);
	QWidget* databaseBox = new QWidget;
	QVBoxLayout* databaseLayout = new QVBoxLayout(databaseBox);
	databaseLayout->setContentsMargins(0, 0, 0, 0);
	QHBoxLayout* managedRow = new QHBoxLayout;
	m_managed = new QRadioButton(tr("its own PostgreSQL, on 127.0.0.1 port"));
	m_databasePort = portBox(1);
	managedRow->addWidget(m_managed);
	managedRow->addWidget(m_databasePort);
	managedRow->addStretch();
	QHBoxLayout* externalRow = new QHBoxLayout;
	m_external = new QRadioButton(tr("this database:"));
	m_database = new QLineEdit;
	m_database->setPlaceholderText(tr("user:password@host:5432/nakama"));
	externalRow->addWidget(m_external);
	externalRow->addWidget(m_database, 1);
	QButtonGroup* databaseGroup = new QButtonGroup(this);
	databaseGroup->addButton(m_managed);
	databaseGroup->addButton(m_external);
	databaseLayout->addLayout(managedRow);
	databaseLayout->addLayout(externalRow);
	form->addRow(tr("Database"), databaseBox);
	QWidget* row = nullptr;
	m_tlsCert = fileField(&row, false);
	m_tlsCert->setPlaceholderText(tr("none"));
	form->addRow(tr("TLS certificate"), row);
	m_tlsKey = fileField(&row, false);
	form->addRow(tr("TLS private key"), row);
	m_extraModules = fileField(&row, true);
	m_extraModules->setPlaceholderText(tr("a folder of Lua modules of your own (optional)"));
	form->addRow(tr("More modules"), row);
	QHBoxLayout* saveRow = new QHBoxLayout;
	m_save = new QPushButton(tr("Save"));
	connect(m_save, &QPushButton::clicked, this, [this]() { save(); });
	m_formError = new QLabel;
	m_formError->setTextFormat(Qt::PlainText);
	m_formError->setWordWrap(true);
	m_formError->setStyleSheet(QStringLiteral("color: #ff8080;"));
	saveRow->addWidget(m_save);
	saveRow->addWidget(m_formError, 1);
	form->addRow(saveRow);
	QScrollArea* scroll = new QScrollArea;
	scroll->setWidget(m_form);
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	m_tabs->addTab(scroll, tr("Settings"));
	m_log = new QPlainTextEdit;
	m_log->setReadOnly(true);
	m_log->setMaximumBlockCount(2000);
	m_log->setLineWrapMode(QPlainTextEdit::NoWrap);
	m_tabs->addTab(m_log, tr("Log"));
	main->addWidget(m_tabs, 1);
	columns->addLayout(main, 1);
	rows->addLayout(columns, 1);

	QFormLayout* programs = new QFormLayout;
	QLabel* programsHeading = new QLabel(tr("PROGRAMS (every server)"));
	programsHeading->setObjectName("heading");
	programs->addRow(programsHeading);
	m_nakama = fileField(&row, false);
	m_nakama->setPlaceholderText(tr("found next to this program or in PATH"));
	programs->addRow(tr("Nakama"), row);
	m_postgres = fileField(&row, true);
	m_postgres->setPlaceholderText(tr("searched where PostgreSQL usually is"));
	programs->addRow(tr("PostgreSQL's programs"), row);
	m_programs = new QLabel;
	m_programs->setObjectName("dim");
	m_programs->setTextFormat(Qt::PlainText);
	m_programs->setWordWrap(true);
	programs->addRow(m_programs);
	rows->addLayout(programs);
	m_nakama->setText(m_manager->config().nakamaPath);
	m_postgres->setText(m_manager->config().postgresPath);
	auto savePrograms = [this]() {
		QString nakama = m_nakama->text().trimmed();
		QString postgres = m_postgres->text().trimmed();
		ServerConfig& config = m_manager->config();
		if (config.nakamaPath == nakama && config.postgresPath == postgres) {
			return;
		}
		bool refreshed = refresh();
		config.nakamaPath = nakama;
		config.postgresPath = postgres;
		m_nakama->setText(nakama);
		m_postgres->setText(postgres);
		QString error;
		if (!config.save(&error)) {
			QMessageBox::warning(this, windowTitle(), error);
		}
		if (refreshed && !m_dirty) {
			reload(m_current);
		}
		updateState();
	};
	connect(m_nakama, &QLineEdit::editingFinished, this, savePrograms);
	connect(m_postgres, &QLineEdit::editingFinished, this, savePrograms);

	for (QLineEdit* e : { m_titleEdit, m_message, m_bind, m_publicHost, m_serverKey, m_database, m_tlsCert, m_tlsKey, m_extraModules }) {
		connect(e, &QLineEdit::textChanged, this, &ServersWindow::markDirty);
	}
	for (QCheckBox* c : { m_kindIkemen, m_kindGba, m_kindDs, m_clientTls }) {
		connect(c, &QCheckBox::toggled, this, &ServersWindow::markDirty);
	}
	for (QSpinBox* s : { m_port, m_consolePort, m_publicPort, m_databasePort }) {
		connect(s, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), this, &ServersWindow::markDirty);
	}
	connect(m_managed, &QRadioButton::toggled, this, [this]() {
		m_databasePort->setEnabled(m_managed->isChecked());
		m_database->setEnabled(!m_managed->isChecked());
		markDirty();
	});

	connect(m_manager, &ServerManager::stateChanged, this, [this](const QString& id, ServerHost::State) {
		updateItem(id);
		if (id == m_current) {
			updateState();
		}
	});
	connect(m_manager, &ServerManager::logLine, this, [this](const QString& id, const QString& line) {
		if (id != m_current) {
			return;
		}
		QScrollBar* bar = m_log->verticalScrollBar();
		bool atEnd = bar->value() == bar->maximum();
		m_log->appendPlainText(line);
		if (atEnd) {
			bar->setValue(bar->maximum());
		}
	});
	reload();
}

QLineEdit* ServersWindow::fileField(QWidget** row, bool folder) {
	*row = new QWidget;
	QHBoxLayout* layout = new QHBoxLayout(*row);
	layout->setContentsMargins(0, 0, 0, 0);
	QLineEdit* field = new QLineEdit;
	QPushButton* browse = new QPushButton(tr("Browse..."));
	connect(browse, &QPushButton::clicked, this, [this, field, folder]() {
		QString path = folder ? QFileDialog::getExistingDirectory(this, tr("Select a folder"), field->text())
		                      : QFileDialog::getOpenFileName(this, tr("Select a file"), field->text());
		if (!path.isEmpty()) {
			field->setText(path);
			emit field->editingFinished();
		}
	});
	layout->addWidget(field, 1);
	layout->addWidget(browse);
	return field;
}

bool ServersWindow::refresh() {
	ServerConfig& config = m_manager->config();
	if (!config.changedOnDisk()) {
		return false;
	}
	QString error;
	if (!config.load(&error)) {
		QMessageBox::warning(this, windowTitle(), error);
		return false;
	}
	if (config.filledSecrets()) {
		config.save(&error);
	}
	m_nakama->setText(config.nakamaPath);
	m_postgres->setText(config.postgresPath);
	return true;
}

void ServersWindow::changeEvent(QEvent* event) {
	QWidget::changeEvent(event);
	if (event->type() == QEvent::ActivationChange && isActiveWindow() && !m_dirty && m_manager->config().changedOnDisk()) {
		refresh();
		reload(m_current);
	}
}

void ServersWindow::closeEvent(QCloseEvent* event) {
	if (!settle()) {
		event->ignore();
		return;
	}
	event->accept();
}

QString ServersWindow::selected() const {
	QListWidgetItem* item = m_list->currentItem();
	return item ? item->data(Qt::UserRole).toString() : QString();
}

void ServersWindow::reload(const QString& select) {
	m_loading = true;
	QString want = select.isEmpty() ? m_current : select;
	m_list->clear();
	int row = 0;
	for (const ServerProfile& s : m_manager->config().servers()) {
		QListWidgetItem* item = new QListWidgetItem;
		item->setData(Qt::UserRole, s.id);
		m_list->addItem(item);
		updateItem(s.id);
		if (s.id == want) {
			row = m_list->count() - 1;
		}
	}
	if (m_list->count()) {
		m_list->setCurrentRow(row);
	}
	m_loading = false;
	showServer(selected());
}

void ServersWindow::updateItem(const QString& id) {
	const ServerProfile* s = m_manager->config().find(id);
	for (int i = 0; i < m_list->count(); ++i) {
		QListWidgetItem* item = m_list->item(i);
		if (item->data(Qt::UserRole).toString() != id || !s) {
			continue;
		}
		ServerHost::State state = m_manager->state(id);
		item->setText(QStringLiteral("●  %1\n     %2 · %3").arg(s->title, s->kindsLabel(), ServerHost::stateLabel(state)));
		item->setForeground(stateColor(state));
		item->setToolTip(s->id);
	}
}

void ServersWindow::showServer(const QString& id) {
	m_loading = true;
	m_current = id;
	const ServerProfile* s = m_manager->config().find(id);
	ServerProfile p = s ? *s : ServerProfile();
	m_title->setText(s ? p.title : tr("No server"));
	m_id->setText(s ? p.id : QString());
	m_titleEdit->setText(p.title);
	m_message->setText(p.message);
	m_kindIkemen->setChecked(p.serves(QStringLiteral("ikemen")));
	m_kindGba->setChecked(p.serves(QStringLiteral("mgba")));
	m_kindDs->setChecked(p.serves(QStringLiteral("melonds")));
	m_port->setValue(p.port);
	m_consolePort->setValue(p.consolePort);
	m_bind->setText(p.bind);
	m_publicHost->setText(p.publicHost);
	m_publicPort->setValue(p.publicPort);
	m_clientTls->setChecked(p.clientTls);
	m_serverKey->setText(p.serverKey);
	m_managed->setChecked(p.managedDatabase());
	m_external->setChecked(!p.managedDatabase());
	m_databasePort->setValue(p.databasePort);
	m_database->setText(p.database);
	m_databasePort->setEnabled(p.managedDatabase());
	m_database->setEnabled(!p.managedDatabase());
	m_tlsCert->setText(p.tlsCertificate);
	m_tlsKey->setText(p.tlsPrivateKey);
	m_extraModules->setText(p.extraModules);
	m_formError->clear();
	m_log->clear();
	if (ServerHost* h = m_manager->host(id)) {
		m_log->setPlainText(h->recentLog().join(QLatin1Char('\n')));
		m_log->verticalScrollBar()->setValue(m_log->verticalScrollBar()->maximum());
	} else if (s) {
		// the end of the log of earlier runs
		QFile file(QDir(m_manager->config().dataDir(p)).filePath(QStringLiteral("server.log")));
		if (file.open(QIODevice::ReadOnly)) {
			if (file.size() > 64 * 1024) {
				file.seek(file.size() - 64 * 1024);
				file.readLine();
			}
			m_log->setPlainText(QString::fromUtf8(file.readAll()).trimmed());
			m_log->verticalScrollBar()->setValue(m_log->verticalScrollBar()->maximum());
		}
	}
	m_dirty = false;
	m_loading = false;
	updateState();
}

void ServersWindow::markDirty() {
	if (!m_loading && !m_current.isEmpty()) {
		m_dirty = true;
		m_save->setEnabled(true);
		m_formError->clear();
	}
}

void ServersWindow::updateState() {
	const ServerProfile* s = m_manager->config().find(m_current);
	ServerHost::State state = m_manager->state(m_current);
	ServerHost* h = m_manager->host(m_current);
	bool active = h && h->active();
	QString label = s ? ServerHost::stateLabel(state) : QString();
	if (state == ServerHost::State::Failed && h) {
		label += QStringLiteral(": ") + h->error();
	}
	m_stateLabel->setText(label);
	m_stateLabel->setStyleSheet(QStringLiteral("color: %1;").arg(stateColor(state).name()));
	m_start->setEnabled(s && !active);
	m_stop->setEnabled(s && active && state != ServerHost::State::Stopping);
	m_use->setEnabled(s && (s->tlsCertificate.isEmpty() || !s->publicHost.isEmpty()));
	m_use->setToolTip(s && !s->tlsCertificate.isEmpty() && s->publicHost.isEmpty() ? tr("With TLS, the launcher connects to the address the certificate names: set the public address")
	                                                                           : tr("Sign this launcher in to this server (Settings > Nakama server)"));
	m_ikemen->setEnabled(s);
	m_folder->setEnabled(s);
	m_remove->setEnabled(s && !active);
	m_form->setEnabled(s && !active);
	m_save->setEnabled(s && !active && m_dirty);
	if (s) {
		QString info = m_manager->connectionInfo(*s).mid(0, 4).join(QStringLiteral(" · "));
		if (state == ServerHost::State::Running) {
			info = tr("Players connect with: %1").arg(info);
		} else {
			info = tr("When it runs, players connect with: %1").arg(info);
		}
		m_connect->setText(info);
	} else {
		m_connect->setText(tr("Add a server for an IKEMEN GO, GBA or DS community: Nakama Matchmaker runs Nakama and its database for it."));
	}
	QString nakama = m_manager->nakamaProgram();
	QString postgres = m_manager->postgresFolder();
	QString modules = m_manager->modulesFolder();
	m_programs->setText(tr("Nakama: %1 · PostgreSQL: %2 · server modules: %3")
	                        .arg(nakama.isEmpty() ? tr("not found (https://github.com/heroiclabs/nakama/releases)") : QDir::toNativeSeparators(nakama),
	                             postgres.isEmpty() ? tr("not found (needed for servers with their own database)") : QDir::toNativeSeparators(postgres),
	                             modules.isEmpty() ? tr("not found") : QDir::toNativeSeparators(modules)));
}

ServerProfile ServersWindow::edited() const {
	const ServerProfile* s = m_manager->config().find(m_current);
	ServerProfile p = s ? *s : ServerProfile();
	p.title = m_titleEdit->text().trimmed();
	p.message = m_message->text().trimmed();
	p.kinds.clear();
	if (m_kindIkemen->isChecked()) {
		p.kinds.append(QStringLiteral("ikemen"));
	}
	if (m_kindGba->isChecked()) {
		p.kinds.append(QStringLiteral("mgba"));
	}
	if (m_kindDs->isChecked()) {
		p.kinds.append(QStringLiteral("melonds"));
	}
	p.port = m_port->value();
	p.consolePort = m_consolePort->value();
	p.bind = m_bind->text().trimmed();
	p.publicHost = m_publicHost->text().trimmed();
	p.publicPort = m_publicPort->value();
	p.clientTls = m_clientTls->isChecked();
	p.serverKey = m_serverKey->text().trimmed();
	p.database = m_managed->isChecked() ? QString() : m_database->text().trimmed();
	p.databasePort = m_databasePort->value();
	p.tlsCertificate = m_tlsCert->text().trimmed();
	p.tlsPrivateKey = m_tlsKey->text().trimmed();
	p.extraModules = m_extraModules->text().trimmed();
	return p;
}

bool ServersWindow::save() {
	if (m_current.isEmpty()) {
		return true;
	}
	ServerProfile p = edited();
	if (p.title.isEmpty()) {
		p.title = p.id;
	}
	bool refreshed = refresh();
	if (!m_manager->config().find(p.id)) {
		m_formError->setText(tr("Another program removed the server %1.").arg(p.id));
		m_dirty = false;
		reload();
		return false;
	}
	if (!p.managedDatabase() && m_external->isChecked() && p.database.isEmpty()) {
		m_formError->setText(tr("Enter the database's address, or let the server have its own."));
		return false;
	}
	ServerConfig& config = m_manager->config();
	QString invalid = config.validate(p);
	if (!invalid.isEmpty()) {
		m_formError->setText(invalid);
		m_tabs->setCurrentIndex(0);
		return false;
	}
	config.put(p);
	QString error;
	if (!config.save(&error)) {
		m_formError->setText(error);
		return false;
	}
	m_dirty = false;
	QStringList conflicts = config.conflicts(p);
	m_formError->setText(conflicts.isEmpty() ? QString() : tr("Saved. It shares ports or its folder with %1: they cannot run at the same time.").arg(conflicts.join(QStringLiteral(", "))));
	m_title->setText(p.title);
	if (refreshed) {
		QString note = m_formError->text();
		reload(p.id);   // the servers another program added or changed
		m_formError->setText(note);
	}
	updateItem(p.id);
	updateState();
	return true;
}

bool ServersWindow::settle() {
	if (!m_dirty || m_current.isEmpty()) {
		return true;
	}
	QMessageBox::StandardButton answer = QMessageBox::question(this, windowTitle(), tr("Save the changes to the server %1?").arg(m_current),
	                                                           QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
	if (answer == QMessageBox::Cancel) {
		return false;
	}
	if (answer == QMessageBox::Save) {
		return save();
	}
	m_dirty = false;
	return true;
}

void ServersWindow::add() {
	if (!settle()) {
		return;
	}
	refresh();
	QDialog dialog(this);
	dialog.setWindowTitle(tr("New server"));
	QFormLayout* form = new QFormLayout(&dialog);
	QLineEdit* id = new QLineEdit;
	id->setMaxLength(16);
	id->setPlaceholderText(tr("e.g. ds-club: lowercase letters, digits and dashes"));
	form->addRow(tr("Id"), id);
	QLineEdit* title = new QLineEdit;
	title->setMaxLength(64);
	title->setPlaceholderText(tr("The community's name"));
	form->addRow(tr("Title"), title);
	QCheckBox* ikemen = new QCheckBox(tr("IKEMEN GO"));
	QCheckBox* gba = new QCheckBox(tr("GBA (mGBA fork)"));
	QCheckBox* ds = new QCheckBox(tr("DS (melonDS fork)"));
	QVBoxLayout* kinds = new QVBoxLayout;
	kinds->addWidget(ikemen);
	kinds->addWidget(gba);
	kinds->addWidget(ds);
	form->addRow(tr("Games"), kinds);
	QLabel* error = new QLabel;
	error->setStyleSheet(QStringLiteral("color: #ff8080;"));
	error->setWordWrap(true);
	form->addRow(error);
	QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	form->addRow(buttons);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	ServerConfig& config = m_manager->config();
	connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
		QString value = id->text().trimmed();
		if (!ServerConfig::validId(value)) {
			error->setText(tr("The id must be 1 to 16 lowercase letters, digits and dashes."));
		} else if (config.find(value)) {
			error->setText(tr("There already is a server %1.").arg(value));
		} else if (!ikemen->isChecked() && !gba->isChecked() && !ds->isChecked()) {
			error->setText(tr("Choose the games the community plays."));
		} else {
			dialog.accept();
		}
	});
	if (dialog.exec() != QDialog::Accepted) {
		return;
	}
	QStringList chosen;
	if (ikemen->isChecked()) {
		chosen.append(QStringLiteral("ikemen"));
	}
	if (gba->isChecked()) {
		chosen.append(QStringLiteral("mgba"));
	}
	if (ds->isChecked()) {
		chosen.append(QStringLiteral("melonds"));
	}
	refresh();
	if (config.find(id->text().trimmed())) {
		QMessageBox::warning(this, windowTitle(), tr("There already is a server %1.").arg(id->text().trimmed()));
		reload();
		return;
	}
	ServerProfile p = config.create(id->text().trimmed(), chosen);
	if (!title->text().trimmed().isEmpty()) {
		p.title = title->text().trimmed();
	}
	config.put(p);
	QString why;
	if (!config.save(&why)) {
		QMessageBox::warning(this, windowTitle(), why);
	}
	reload(p.id);
}

void ServersWindow::remove() {
	const ServerProfile* s = m_manager->config().find(m_current);
	if (!s) {
		return;
	}
	QString id = s->id;
	QString data = m_manager->config().dataDir(*s);
	QMessageBox box(QMessageBox::Question, windowTitle(), tr("Remove the server %1?").arg(s->title), QMessageBox::Yes | QMessageBox::Cancel, this);
	QCheckBox* deleteData = new QCheckBox(tr("Also delete its folder, with its database and log (%1)").arg(QDir::toNativeSeparators(data)));
	box.setCheckBox(deleteData);
	if (box.exec() != QMessageBox::Yes) {
		return;
	}
	refresh();
	m_manager->config().remove(id);
	QString error;
	if (!m_manager->config().save(&error)) {
		QMessageBox::warning(this, windowTitle(), error);
	}
	if (deleteData->isChecked() && ServerHost::runningElsewhere(data)) {
		QMessageBox::information(this, windowTitle(), tr("The server runs in another program, so its folder was left alone."));
	} else if (deleteData->isChecked()) {
		if (QFileInfo::exists(QDir(data).filePath(ServerHost::markerName()))) {
			QDir(data).removeRecursively();
		} else if (QFileInfo::exists(data)) {
			QMessageBox::information(this, windowTitle(), tr("%1 was not made by Nakama Matchmaker, so it was left alone.").arg(QDir::toNativeSeparators(data)));
		}
	}
	m_dirty = false;
	m_current.clear();
	reload();
}

void ServersWindow::start() {
	if (m_dirty && !save()) {
		return;
	}
	QString error;
	if (!m_manager->start(m_current, &error)) {
		QMessageBox::warning(this, windowTitle(), error);
		updateState();
		return;
	}
	m_log->clear();
	if (ServerHost* h = m_manager->host(m_current)) {
		m_log->setPlainText(h->recentLog().join(QLatin1Char('\n')));
	}
	m_tabs->setCurrentIndex(1);
	updateState();
}

void ServersWindow::useInLauncher() {
	const ServerProfile* s = m_manager->config().find(m_current);
	if (!s) {
		return;
	}
	NetplayOnline::ServerConfig server;
	if (s->tlsCertificate.isEmpty()) {
		// this computer: straight to the server's port, without the proxy players may use
		server.host = s->bind.isEmpty() || s->bind == QLatin1String("0.0.0.0") || s->bind == QLatin1String("::") ? QStringLiteral("127.0.0.1") : s->bind;
		server.port = s->port;
		server.ssl = false;
	} else {
		// the certificate names the public address
		server.host = s->clientHost();
		server.port = s->port;
		server.ssl = true;
	}
	server.serverKey = s->serverKey;
	m_settings->setServer(server);
	QMessageBox::information(this, windowTitle(), tr("The launcher now signs in to %1 (%2, port %3). If it is signed in, sign out and in again.")
	                                                   .arg(s->title, server.host)
	                                                   .arg(server.port));
}

void ServersWindow::writeIkemenProfile() {
	const ServerProfile* s = m_manager->config().find(m_current);
	if (!s) {
		return;
	}
	QDialog dialog(this);
	dialog.setWindowTitle(tr("IKEMEN GO profile"));
	QFormLayout* form = new QFormLayout(&dialog);
	QLabel* note = new QLabel(tr("A server profile that an IKEMEN GO build can ship in data/online/servers.json, so that its players need no settings. The game and build name the pools its players match in; leave them empty to let the game say."));
	note->setWordWrap(true);
	form->addRow(note);
	QLineEdit* host = new QLineEdit(s->clientHost());
	QLineEdit* game = new QLineEdit;
	QLineEdit* build = new QLineEdit;
	QLineEdit* region = new QLineEdit;
	QLineEdit* stun = new QLineEdit(m_settings->stunServers().join(QStringLiteral(", ")));
	stun->setToolTip(tr("STUN servers (host:port) that tell each player its public address; without them only players on one network reach each other"));
	form->addRow(tr("Server address"), host);
	form->addRow(tr("Game id"), game);
	form->addRow(tr("Build"), build);
	form->addRow(tr("Region"), region);
	form->addRow(tr("STUN servers"), stun);
	QLabel* warning = new QLabel;
	warning->setWordWrap(true);
	warning->setTextFormat(Qt::PlainText);
	warning->setStyleSheet(QStringLiteral("color: #f0c040;"));
	form->addRow(warning);
	auto check = [host, warning]() {
		warning->setText(ServerManager::localOnly(host->text().trimmed())
		                     ? tr("This address is the player's own computer: players elsewhere need the server's public address.")
		                     : QString());
	};
	connect(host, &QLineEdit::textChanged, &dialog, check);
	check();
	QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
	form->addRow(buttons);
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	if (dialog.exec() != QDialog::Accepted) {
		return;
	}
	QString path = QFileDialog::getSaveFileName(this, tr("Save IKEMEN GO's server profile"), QStringLiteral("servers.json"), tr("JSON (*.json)"));
	if (path.isEmpty()) {
		return;
	}
	ServerManager::IkemenProfileOptions options;
	options.host = host->text().trimmed();
	options.game = game->text().trimmed();
	options.build = build->text().trimmed();
	options.region = region->text().trimmed();
	for (const QString& server : stun->text().split(QLatin1Char(','))) {
		if (!server.trimmed().isEmpty()) {
			options.stun.append(server.trimmed());
		}
	}
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		QMessageBox::warning(this, windowTitle(), file.errorString());
		return;
	}
	file.write(m_manager->ikemenProfile(*s, options));
}
