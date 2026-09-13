#pragma once

#include "remote-server.hpp"
#include "teleprompter-state.hpp"
#include "teleprompter-window.hpp"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFontComboBox>
#include <QIcon>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QPlainTextEdit>
#include <QSpinBox>
#include <QTimer>
#include <QWidget>

class QCheckBox;
class QLineEdit;
class QPushButton;

class TeleprompterDock : public QWidget {
	Q_OBJECT

public:
	TeleprompterDock(TeleprompterState *state, TeleprompterWindow *window, RemoteServer *remote, QWidget *parent = nullptr);
	void refreshScreens();
	void refreshAudioSources();
	void syncFromState();

public slots:
	void syncOutputWindow();

protected:
	void changeEvent(QEvent *event) override;

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
	void updatePlaybackStatus();
	void applyStyle();

private:
	void refreshControlIcons();
	void updatePlayPauseButtonAppearance();

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
	QLabel *playbackHeaderLabel_ = nullptr;
	QLabel *remoteLabel_ = nullptr;
	QPushButton *playPauseButton_ = nullptr;
	QVector<QPair<QPushButton *, QString>> controlButtons_;
	QTimer playbackStatusTimer_;
	QString lastPlaybackStatusText_;
	int lastHeaderPlaybackState_ = -1;
	int lastHeaderProgress_ = -1;
	QNetworkAccessManager network_;
};
