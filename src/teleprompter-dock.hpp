#pragma once

#include "remote-server.hpp"
#include "teleprompter-state.hpp"
#include "teleprompter-window.hpp"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFontComboBox>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QPlainTextEdit>
#include <QSpinBox>
#include <QWidget>

class QCheckBox;
class QLineEdit;

class TeleprompterDock : public QWidget {
	Q_OBJECT

public:
	TeleprompterDock(TeleprompterState *state, TeleprompterWindow *window, RemoteServer *remote, QWidget *parent = nullptr);
	void refreshScreens();
	void refreshAudioSources();
	void syncFromState();

public slots:
	void syncOutputWindow();

private slots:
	void applyEditor();
	void importText();
	void loadUrl();
	void saveText();
	void chooseTextColor();
	void chooseBackgroundColor();
	void showOutput();
	void applyDisplay();
	void updateStatus();
	void applyStyle();

private:
	TeleprompterState *state_ = nullptr;
	TeleprompterWindow *window_ = nullptr;
	RemoteServer *remote_ = nullptr;

	QLineEdit *titleEdit_ = nullptr;
	QLineEdit *urlEdit_ = nullptr;
	QPlainTextEdit *scriptEdit_ = nullptr;
	QComboBox *screenCombo_ = nullptr;
	QComboBox *renderScaleCombo_ = nullptr;
	QComboBox *audioSourceCombo_ = nullptr;
	QCheckBox *resumeOnAudioActiveCheck_ = nullptr;
	QDoubleSpinBox *audioResumeDelaySpin_ = nullptr;
	QCheckBox *fullscreenOutputCheck_ = nullptr;
	QFontComboBox *fontCombo_ = nullptr;
	QSpinBox *fontSizeSpin_ = nullptr;
	QDoubleSpinBox *lineSpacingSpin_ = nullptr;
	QSpinBox *paragraphSpacingSpin_ = nullptr;
	QDoubleSpinBox *speedSpin_ = nullptr;
	QSpinBox *marginSpin_ = nullptr;
	QComboBox *horizontalAlignCombo_ = nullptr;
	QComboBox *verticalAlignCombo_ = nullptr;
	QCheckBox *mirrorHorizontalCheck_ = nullptr;
	QCheckBox *mirrorVerticalCheck_ = nullptr;
	QCheckBox *positionIndicatorCheck_ = nullptr;
	QLabel *statusLabel_ = nullptr;
	QLabel *remoteLabel_ = nullptr;
	QNetworkAccessManager network_;
};
