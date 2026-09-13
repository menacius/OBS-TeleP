#include "settings-dialog.hpp"

#include "teleprompter-locale.hpp"

#include <obs-module.h>

#include <QCheckBox>
#include <QColorDialog>
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
#include <QSpinBox>
#include <QSvgRenderer>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <QHeaderView>

TeleprompterSettingsDialog::TeleprompterSettingsDialog(TeleprompterState *state, TeleprompterWindow *window, QWidget *parent)
	: QDialog(parent), state_(state), window_(window)
{
	setWindowTitle(Tr("Settings.Title"));
	setMinimumWidth(420);
	setWindowIcon(QIcon(iconPixmap(64)));

	auto *root = new QVBoxLayout(this);
	auto *form = new QFormLayout();

	displayTable_ = new QTableWidget(this);
	displayTable_->setColumnCount(2);
	displayTable_->setHorizontalHeaderLabels({Tr("Display.ColumnDisplay"), Tr("Display.ColumnEnable")});
	displayTable_->horizontalHeader()->setStretchLastSection(false);
	displayTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
	displayTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
	displayTable_->verticalHeader()->setVisible(false);
	displayTable_->setSelectionMode(QAbstractItemView::NoSelection);
	displayTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
	displayTable_->setMinimumHeight(120);
	renderScaleCombo_ = new QComboBox(this);
	renderScaleCombo_->addItem(Tr("RenderScale.Full"), 1.0);
	renderScaleCombo_->addItem(Tr("RenderScale.ThreeQuarter"), 0.75);
	renderScaleCombo_->addItem(Tr("RenderScale.Half"), 0.5);

	form->addRow(Tr("Label.TargetDisplay"), displayTable_);
	form->addRow(Tr("Label.OutputRenderScale"), renderScaleCombo_);

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
	progressBarEnabledCheck_ = new QCheckBox(Tr("ProgressBar.Enabled"), this);
	progressBarPositionCombo_ = new QComboBox(this);
	progressBarPositionCombo_->addItem(Tr("ProgressBar.Left"), 0);
	progressBarPositionCombo_->addItem(Tr("ProgressBar.Right"), 1);
	progressBarThicknessSpin_ = new QSpinBox(this);
	progressBarThicknessSpin_->setRange(2, 80);
	progressBarThicknessSpin_->setSuffix(Tr("Unit.Pixels"));
	progressBarColorButton_ = new QPushButton(Tr("ProgressBar.Color"), this);

	form->addRow(overlayEnabledCheck_);
	form->addRow(Tr("Overlay.Position"), overlayPositionCombo_);
	form->addRow(overlayProgressCheck_);
	form->addRow(overlayStateCheck_);
	form->addRow(overlaySpeedCheck_);
	form->addRow(overlayTitleCheck_);
	form->addRow(progressBarEnabledCheck_);
	form->addRow(Tr("ProgressBar.Position"), progressBarPositionCombo_);
	form->addRow(Tr("ProgressBar.Thickness"), progressBarThicknessSpin_);
	form->addRow(Tr("ProgressBar.Color"), progressBarColorButton_);
	root->addLayout(form);

	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
	auto *aboutButton = buttons->addButton(Tr("Button.About"), QDialogButtonBox::HelpRole);
	root->addWidget(buttons);

	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::hide);
	connect(aboutButton, &QPushButton::clicked, this, &TeleprompterSettingsDialog::showAbout);
	connect(displayTable_, &QTableWidget::itemChanged, this, &TeleprompterSettingsDialog::applyDisplay);
	connect(renderScaleCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &TeleprompterSettingsDialog::applyRenderScale);
	connect(overlayEnabledCheck_, &QCheckBox::toggled, this, &TeleprompterSettingsDialog::applyOverlay);
	connect(overlayPositionCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &TeleprompterSettingsDialog::applyOverlay);
	connect(overlayProgressCheck_, &QCheckBox::toggled, this, &TeleprompterSettingsDialog::applyOverlay);
	connect(overlayStateCheck_, &QCheckBox::toggled, this, &TeleprompterSettingsDialog::applyOverlay);
	connect(overlaySpeedCheck_, &QCheckBox::toggled, this, &TeleprompterSettingsDialog::applyOverlay);
	connect(overlayTitleCheck_, &QCheckBox::toggled, this, &TeleprompterSettingsDialog::applyOverlay);
	connect(progressBarEnabledCheck_, &QCheckBox::toggled, this, &TeleprompterSettingsDialog::applyProgressBar);
	connect(progressBarPositionCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &TeleprompterSettingsDialog::applyProgressBar);
	connect(progressBarThicknessSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, &TeleprompterSettingsDialog::applyProgressBar);
	connect(progressBarColorButton_, &QPushButton::clicked, this, &TeleprompterSettingsDialog::chooseProgressBarColor);
	connect(state_, &TeleprompterState::displayChanged, this, &TeleprompterSettingsDialog::syncFromState);
	connect(qApp, &QGuiApplication::screenAdded, this, &TeleprompterSettingsDialog::refreshScreens);
	connect(qApp, &QGuiApplication::screenRemoved, this, &TeleprompterSettingsDialog::refreshScreens);

	refreshScreens();
	syncFromState();
}

void TeleprompterSettingsDialog::refreshScreens()
{
	const QSignalBlocker blocker(displayTable_);
	syncingDisplay_ = true;
	displayTable_->setRowCount(0);
	const auto screens = QGuiApplication::screens();
	displayTable_->setRowCount(screens.size());
	const int target = qBound(0, state_->targetScreenIndex(), qMax(0, screens.size() - 1));
	for (int i = 0; i < screens.size(); ++i) {
		auto *displayItem = new QTableWidgetItem(Tr("Display.ItemFormat").arg(i + 1).arg(screens[i]->name()));
		displayItem->setData(Qt::UserRole, i);
		displayItem->setFlags(Qt::ItemIsEnabled);
		displayTable_->setItem(i, 0, displayItem);

		auto *enableItem = new QTableWidgetItem();
		enableItem->setData(Qt::UserRole, i);
		enableItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
		enableItem->setCheckState(state_->outputFullscreenEnabled() && i == target ? Qt::Checked : Qt::Unchecked);
		enableItem->setTextAlignment(Qt::AlignCenter);
		displayTable_->setItem(i, 1, enableItem);
	}
	displayTable_->resizeRowsToContents();
	syncingDisplay_ = false;

	if (screens.isEmpty()) {
		state_->setOutputFullscreenEnabled(false);
		syncOutputWindow();
	} else if (state_->targetScreenIndex() != target) {
		state_->setTargetScreenIndex(target);
		syncOutputWindow();
	}
}

void TeleprompterSettingsDialog::syncFromState()
{
	if (syncingDisplay_)
		return;
	const QSignalBlocker displayBlocker(displayTable_);
	const QSignalBlocker scaleBlocker(renderScaleCombo_);
	const QSignalBlocker overlayEnabledBlocker(overlayEnabledCheck_);
	const QSignalBlocker overlayPositionBlocker(overlayPositionCombo_);
	const QSignalBlocker overlayProgressBlocker(overlayProgressCheck_);
	const QSignalBlocker overlayStateBlocker(overlayStateCheck_);
	const QSignalBlocker overlaySpeedBlocker(overlaySpeedCheck_);
	const QSignalBlocker overlayTitleBlocker(overlayTitleCheck_);
	const QSignalBlocker progressBarEnabledBlocker(progressBarEnabledCheck_);
	const QSignalBlocker progressBarPositionBlocker(progressBarPositionCombo_);
	const QSignalBlocker progressBarThicknessBlocker(progressBarThicknessSpin_);

	const int target = state_->targetScreenIndex();
	for (int row = 0; row < displayTable_->rowCount(); ++row) {
		if (auto *item = displayTable_->item(row, 1))
			item->setCheckState(state_->outputFullscreenEnabled() && row == target ? Qt::Checked : Qt::Unchecked);
	}
	for (int i = 0; i < renderScaleCombo_->count(); ++i) {
		if (qAbs(renderScaleCombo_->itemData(i).toDouble() - state_->outputRenderScale()) < 0.01) {
			renderScaleCombo_->setCurrentIndex(i);
			break;
		}
	}

	const TeleprompterOverlaySettings overlay = state_->overlaySettings();
	overlayEnabledCheck_->setChecked(overlay.enabled);
	const int positionIndex = overlayPositionCombo_->findData(overlay.position);
	overlayPositionCombo_->setCurrentIndex(positionIndex >= 0 ? positionIndex : 1);
	overlayProgressCheck_->setChecked(overlay.showProgress);
	overlayStateCheck_->setChecked(overlay.showPlaybackState);
	overlaySpeedCheck_->setChecked(overlay.showSpeed);
	overlayTitleCheck_->setChecked(overlay.showTitle);

	const TeleprompterProgressBarSettings progressBar = state_->progressBarSettings();
	progressBarEnabledCheck_->setChecked(progressBar.enabled);
	const int progressBarPositionIndex = progressBarPositionCombo_->findData(progressBar.position);
	progressBarPositionCombo_->setCurrentIndex(progressBarPositionIndex >= 0 ? progressBarPositionIndex : 1);
	progressBarThicknessSpin_->setValue(progressBar.thickness);
	progressBarColorButton_->setStyleSheet(QStringLiteral("background-color: %1;").arg(progressBar.color.name()));
}

void TeleprompterSettingsDialog::applyDisplay(QTableWidgetItem *item)
{
	if (syncingDisplay_ || !item || item->column() != 1)
		return;

	const int row = item->row();
	const bool enabled = item->checkState() == Qt::Checked;
	syncingDisplay_ = true;
	for (int i = 0; i < displayTable_->rowCount(); ++i) {
		if (i == row)
			continue;
		if (auto *other = displayTable_->item(i, 1))
			other->setCheckState(Qt::Unchecked);
	}
	state_->setTargetScreenIndex(row);
	state_->setOutputFullscreenEnabled(enabled);
	syncingDisplay_ = false;
	syncOutputWindow();
}

void TeleprompterSettingsDialog::applyRenderScale()
{
	state_->setOutputRenderScale(renderScaleCombo_->currentData().toDouble());
	syncOutputWindow();
}

void TeleprompterSettingsDialog::syncOutputWindow()
{
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

void TeleprompterSettingsDialog::applyProgressBar()
{
	TeleprompterProgressBarSettings progressBar = state_->progressBarSettings();
	progressBar.enabled = progressBarEnabledCheck_->isChecked();
	progressBar.position = progressBarPositionCombo_->currentData().toInt();
	progressBar.thickness = progressBarThicknessSpin_->value();
	state_->setProgressBarSettings(progressBar);
}

void TeleprompterSettingsDialog::chooseProgressBarColor()
{
	TeleprompterProgressBarSettings progressBar = state_->progressBarSettings();
	const QColor color = QColorDialog::getColor(progressBar.color, this, Tr("ProgressBar.Color"));
	if (!color.isValid())
		return;
	progressBar.color = color;
	state_->setProgressBarSettings(progressBar);
	syncFromState();
}

QPixmap TeleprompterSettingsDialog::iconPixmap(int size) const
{
	QPixmap pixmap(size, size);
	pixmap.fill(Qt::transparent);
	char *path = obs_module_file("o-prompter-icon.svg");
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
	box.setInformativeText(Tr("About.Info").arg(QStringLiteral(PLUGIN_VERSION)));
	box.exec();
}
