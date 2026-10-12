/* SPDX-License-Identifier: MIT */
#include "SettingsDialog.h"

#include "Settings.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>

SettingsDialog::SettingsDialog(Settings* settings, QWidget* parent)
	: QDialog(parent)
	, m_settings(settings) {
	setWindowTitle(tr("Settings"));
	QFormLayout* form = new QFormLayout(this);
	m_name = new QLineEdit(settings->name());
	m_name->setMaxLength(16);
	m_name->setPlaceholderText(tr("2 to 16 characters"));
	form->addRow(tr("Name"), m_name);

	NetplayOnline::ServerConfig server = settings->server();
	m_host = new QLineEdit(server.host);
	m_port = new QSpinBox;
	m_port->setRange(1, 65535);
	m_port->setButtonSymbols(QAbstractSpinBox::NoButtons);
	m_port->setValue(server.port);
	m_ssl = new QCheckBox(tr("TLS"));
	m_ssl->setChecked(server.ssl);
	QHBoxLayout* serverRow = new QHBoxLayout;
	serverRow->addWidget(m_host, 1);
	serverRow->addWidget(m_port);
	serverRow->addWidget(m_ssl);
	form->addRow(tr("Nakama server"), serverRow);
	m_key = new QLineEdit(server.serverKey);
	form->addRow(tr("Server key"), m_key);
	m_stun = new QLineEdit(settings->stunServers().join(QStringLiteral(", ")));
	m_stun->setToolTip(tr("STUN servers (host:port) that tell each player its public address"));
	form->addRow(tr("STUN servers"), m_stun);

	m_mgba = pathField(settings->mgbaPath());
	form->addRow(tr("mGBA (fork)"), m_mgba->parentWidget());
	m_melonds = pathField(settings->melondsPath());
	form->addRow(tr("melonDS (fork)"), m_melonds->parentWidget());
	m_ikemen = pathField(settings->ikemenPath());
	form->addRow(tr("IKEMEN GO"), m_ikemen->parentWidget());

	QLabel* note = new QLabel(tr("The account is this computer's: it signs in with a device id kept in the settings."));
	note->setWordWrap(true);
	form->addRow(note);
	QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
	connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
		save();
		accept();
	});
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	form->addRow(buttons);
}

QLineEdit* SettingsDialog::pathField(const QString& value) {
	QWidget* row = new QWidget;
	QHBoxLayout* layout = new QHBoxLayout(row);
	layout->setContentsMargins(0, 0, 0, 0);
	QLineEdit* field = new QLineEdit(value);
	QPushButton* browse = new QPushButton(tr("Browse..."));
	connect(browse, &QPushButton::clicked, this, [this, field]() {
		QString path = QFileDialog::getOpenFileName(this, tr("Select the program"), field->text());
		if (!path.isEmpty()) {
			field->setText(path);
		}
	});
	layout->addWidget(field, 1);
	layout->addWidget(browse);
	return field;
}

void SettingsDialog::save() {
	m_settings->setName(m_name->text().trimmed());
	NetplayOnline::ServerConfig server;
	server.host = m_host->text().trimmed();
	server.port = m_port->value();
	server.serverKey = m_key->text();
	server.ssl = m_ssl->isChecked();
	m_settings->setServer(server);
	QStringList stun;
	for (const QString& s : m_stun->text().split(QLatin1Char(','))) {
		if (!s.trimmed().isEmpty()) {
			stun.append(s.trimmed());
		}
	}
	m_settings->setStunServers(stun);
	m_settings->setMgbaPath(m_mgba->text().trimmed());
	m_settings->setMelondsPath(m_melonds->text().trimmed());
	m_settings->setIkemenPath(m_ikemen->text().trimmed());
}
