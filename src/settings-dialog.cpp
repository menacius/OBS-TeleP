#include "settings-dialog.hpp"

#include "teleprompter-locale.hpp"

#include <obs-module.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QScreen>
#include <QSignalBlocker>
#include <QSvgRenderer>
#include <QVBoxLayout>

TeleprompterSettingsDialog::TeleprompterSettingsDialog(TeleprompterState *state, TeleprompterWindow *window, QWidget *parent)
	: QDialog(parent), state_(state), window_(window)
{
	setWindowTitle(Tr("Settings.Title"));
	setMinimumWidth(420);
	setWindowIcon(QIcon(iconPixmap(64)));

	auto *root = new QVBoxLayout(this);
	auto *form = new QFormLayout();

	screenCombo_ = new QComboBox(this);
	renderScaleCombo_ = new QComboBox(this);
	renderScaleCombo_->addItem(Tr("RenderScale.Full"), 1.0);
	renderScaleCombo_->addItem(Tr("RenderScale.ThreeQuarter"), 0.75);
	renderScaleCombo_->addItem(Tr("RenderScale.Half"), 0.5);
	fullscreenOutputCheck_ = new QCheckBox(Tr("Check.ShowFullscreenOutput"), this);

	form->addRow(Tr("Label.TargetDisplay"), screenCombo_);
	form->addRow(Tr("Label.OutputRenderScale"), renderScaleCombo_);
	form->addRow(fullscreenOutputCheck_);

	overlayEnabledCheck_ = new QCheckBox(Tr("Overlay.Enabled"), this);
	overlayPositionCombo_ = new QComboBox(this);
	overlayPositionCombo_->addItem(Tr("Overlay.TopLeft"), 0);
	overlayPositionCombo_->addItem(Tr("Overlay.TopRight"), 1);
	overlayPositionCombo_->addItem(Tr("Overlay.BottomLeft"), 2);
	overlayPositionCombo_->addItem(Tr("Overlay.BottomRight"), 3);
	overlayPositionCombo_->addItem(Tr("Overlay.TopCenter"), 4);
	overlayPositionCombo_->addItem(Tr("Overlay.BottomCenter"), 5);
	overlayProgressCheck_ = new QCheckBox(Tr("Overlay.Progress"), this);
	overlayStateCheck_ = new QCheckBox(Tr("Overlay.PlaybackState"), this);
	overlaySpeedCheck_ = new QCheckBox(Tr("Overlay.Speed"), this);
	overlayTitleCheck_ = new QCheckBox(Tr("Overlay.Title"), this);

	form->addRow(overlayEnabledCheck_);
	form->addRow(Tr("Overlay.Position"), overlayPositionCombo_);
	form->addRow(overlayProgressCheck_);
	form->addRow(overlayStateCheck_);
	form->addRow(overlaySpeedCheck_);
	form->addRow(overlayTitleCheck_);
	root->addLayout(form);

	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
	auto *aboutButton = buttons->addButton(Tr("Button.About"), QDialogButtonBox::HelpRole);
	root->addWidget(buttons);

	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::hide);
	connect(aboutButton, &QPushButton::clicked, this, &TeleprompterSettingsDialog::showAbout);
	connect(screenCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &TeleprompterSettingsDialog::applyDisplay);
	connect(renderScaleCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &TeleprompterSettingsDialog::applyDisplay);
	connect(fullscreenOutputCheck_, &QCheckBox::toggled, this, &TeleprompterSettingsDialog::applyDisplay);
	connect(overlayEnabledCheck_, &QCheckBox::toggled, this, &TeleprompterSettingsDialog::applyOverlay);
	connect(overlayPositionCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &TeleprompterSettingsDialog::applyOverlay);
	connect(overlayProgressCheck_, &QCheckBox::toggled, this, &TeleprompterSettingsDialog::applyOverlay);
	connect(overlayStateCheck_, &QCheckBox::toggled, this, &TeleprompterSettingsDialog::applyOverlay);
	connect(overlaySpeedCheck_, &QCheckBox::toggled, this, &TeleprompterSettingsDialog::applyOverlay);
	connect(overlayTitleCheck_, &QCheckBox::toggled, this, &TeleprompterSettingsDialog::applyOverlay);
	connect(state_, &TeleprompterState::displayChanged, this, &TeleprompterSettingsDialog::syncFromState);
	connect(qApp, &QGuiApplication::screenAdded, this, &TeleprompterSettingsDialog::refreshScreens);
	connect(qApp, &QGuiApplication::screenRemoved, this, &TeleprompterSettingsDialog::refreshScreens);

	refreshScreens();
	syncFromState();
}

void TeleprompterSettingsDialog::refreshScreens()
{
	const QSignalBlocker blocker(screenCombo_);
	screenCombo_->clear();
	const auto screens = QGuiApplication::screens();
	for (int i = 0; i < screens.size(); ++i)
		screenCombo_->addItem(Tr("Display.ItemFormat").arg(i + 1).arg(screens[i]->name()), i);
	screenCombo_->setCurrentIndex(qBound(0, state_->targetScreenIndex(), qMax(0, screenCombo_->count() - 1)));
}

void TeleprompterSettingsDialog::syncFromState()
{
	const QSignalBlocker screenBlocker(screenCombo_);
	const QSignalBlocker scaleBlocker(renderScaleCombo_);
	const QSignalBlocker fullscreenBlocker(fullscreenOutputCheck_);
	const QSignalBlocker overlayEnabledBlocker(overlayEnabledCheck_);
	const QSignalBlocker overlayPositionBlocker(overlayPositionCombo_);
	const QSignalBlocker overlayProgressBlocker(overlayProgressCheck_);
	const QSignalBlocker overlayStateBlocker(overlayStateCheck_);
	const QSignalBlocker overlaySpeedBlocker(overlaySpeedCheck_);
	const QSignalBlocker overlayTitleBlocker(overlayTitleCheck_);

	screenCombo_->setCurrentIndex(qBound(0, state_->targetScreenIndex(), qMax(0, screenCombo_->count() - 1)));
	for (int i = 0; i < renderScaleCombo_->count(); ++i) {
		if (qAbs(renderScaleCombo_->itemData(i).toDouble() - state_->outputRenderScale()) < 0.01) {
			renderScaleCombo_->setCurrentIndex(i);
			break;
		}
	}
	fullscreenOutputCheck_->setChecked(state_->outputFullscreenEnabled());

	const TeleprompterOverlaySettings overlay = state_->overlaySettings();
	overlayEnabledCheck_->setChecked(overlay.enabled);
	const int positionIndex = overlayPositionCombo_->findData(overlay.position);
	overlayPositionCombo_->setCurrentIndex(positionIndex >= 0 ? positionIndex : 1);
	overlayProgressCheck_->setChecked(overlay.showProgress);
	overlayStateCheck_->setChecked(overlay.showPlaybackState);
	overlaySpeedCheck_->setChecked(overlay.showSpeed);
	overlayTitleCheck_->setChecked(overlay.showTitle);
}

void TeleprompterSettingsDialog::applyDisplay()
{
	state_->setTargetScreenIndex(screenCombo_->currentData().toInt());
	state_->setOutputRenderScale(renderScaleCombo_->currentData().toDouble());
	state_->setOutputFullscreenEnabled(fullscreenOutputCheck_->isChecked());
	if (state_->outputFullscreenEnabled())
		window_->showOnScreen(state_->targetScreenIndex());
	else
		window_->hide();
}

void TeleprompterSettingsDialog::applyOverlay()
{
	TeleprompterOverlaySettings overlay;
	overlay.enabled = overlayEnabledCheck_->isChecked();
	overlay.position = overlayPositionCombo_->currentData().toInt();
	overlay.showProgress = overlayProgressCheck_->isChecked();
	overlay.showPlaybackState = overlayStateCheck_->isChecked();
	overlay.showSpeed = overlaySpeedCheck_->isChecked();
	overlay.showTitle = overlayTitleCheck_->isChecked();
	state_->setOverlaySettings(overlay);
}

QPixmap TeleprompterSettingsDialog::iconPixmap(int size) const
{
	QPixmap pixmap(size, size);
	pixmap.fill(Qt::transparent);
	char *path = obs_module_file("telep-icon.svg");
	if (path) {
		QSvgRenderer renderer(QString::fromUtf8(path));
		QPainter painter(&pixmap);
		renderer.render(&painter);
		bfree(path);
	}
	return pixmap;
}

void TeleprompterSettingsDialog::showAbout()
{
	QMessageBox box(this);
	box.setWindowTitle(Tr("About.Title"));
	box.setIconPixmap(iconPixmap(96));
	box.setText(Tr("About.Text"));
	box.setInformativeText(Tr("About.Info"));
	box.exec();
}
