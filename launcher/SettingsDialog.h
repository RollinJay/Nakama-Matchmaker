/* SPDX-License-Identifier: MIT */
#pragma once

#include <QDialog>

class QCheckBox;
class QLineEdit;
class QSpinBox;
class Settings;

class SettingsDialog : public QDialog {
	Q_OBJECT

public:
	SettingsDialog(Settings*, QWidget* parent = nullptr);

private:
	void save();
	QLineEdit* pathField(const QString& value);

	Settings* m_settings;
	QLineEdit* m_name;
	QLineEdit* m_host;
	QSpinBox* m_port;
	QLineEdit* m_key;
	QCheckBox* m_ssl;
	QLineEdit* m_stun;
	QLineEdit* m_mgba;
	QLineEdit* m_melonds;
	QLineEdit* m_ikemen;
};
