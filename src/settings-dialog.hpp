#pragma once

#include "teleprompter-state.hpp"
#include "teleprompter-window.hpp"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;

class TeleprompterSettingsDialog : public QDialog {
	Q_OBJECT

public:
	explicit TeleprompterSettingsDialog(TeleprompterState *state, TeleprompterWindow *window, QWidget *parent = nullptr);

private slots:
	void refreshScreens();
	void syncFromState();
	void applyDisplay();
	void applyOverlay();
	void showAbout();

private:
	QPixmap iconPixmap(int size) const;

	TeleprompterState *state_ = nullptr;
	TeleprompterWindow *window_ = nullptr;
	QComboBox *screenCombo_ = nullptr;
	QComboBox *renderScaleCombo_ = nullptr;
	QCheckBox *fullscreenOutputCheck_ = nullptr;
	QCheckBox *overlayEnabledCheck_ = nullptr;
	QComboBox *overlayPositionCombo_ = nullptr;
	QCheckBox *overlayProgressCheck_ = nullptr;
	QCheckBox *overlayStateCheck_ = nullptr;
	QCheckBox *overlaySpeedCheck_ = nullptr;
	QCheckBox *overlayTitleCheck_ = nullptr;
};
