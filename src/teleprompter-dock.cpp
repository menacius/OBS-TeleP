#include "teleprompter-dock.hpp"

#include "script-url.hpp"
#include "teleprompter-locale.hpp"

#include <obs.h>

#include <QCheckBox>
#include <QColorDialog>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontComboBox>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QPushButton>
#include <QScreen>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QTextStream>
#include <QVBoxLayout>

namespace {
bool IsAudioInputCaptureSource(obs_source_t *source)
{
	if (!source)
		return false;
	if ((obs_source_get_output_flags(source) & OBS_SOURCE_AUDIO) == 0)
		return false;

	const char *id = obs_source_get_unversioned_id(source);
	if (!id || !*id)
		id = obs_source_get_id(source);
	const QString sourceId = QString::fromUtf8(id ? id : "");
	return sourceId.contains(QStringLiteral("input_capture"), Qt::CaseInsensitive) &&
	       !sourceId.contains(QStringLiteral("output_capture"), Qt::CaseInsensitive);
}
}

TeleprompterDock::TeleprompterDock(TeleprompterState *state, TeleprompterWindow *window, RemoteServer *remote, QWidget *parent)
	: QWidget(parent), state_(state), window_(window), remote_(remote)
{
	auto *root = new QVBoxLayout(this);
	auto *tabs = new QTabWidget(this);
	root->addWidget(tabs);

	auto *scriptPage = new QWidget(this);
	auto *scriptLayout = new QVBoxLayout(scriptPage);
	titleEdit_ = new QLineEdit(state_->title(), scriptPage);
	scriptEdit_ = new QPlainTextEdit(state_->script(), scriptPage);
	scriptEdit_->setPlaceholderText(Tr("Script.Placeholder"));
	scriptLayout->addWidget(titleEdit_);
	scriptLayout->addWidget(scriptEdit_, 1);
	auto *fileRow = new QHBoxLayout();
	auto *importButton = new QPushButton(Tr("Button.Import"), scriptPage);
	auto *saveButton = new QPushButton(Tr("Button.SaveText"), scriptPage);
	urlEdit_ = new QLineEdit(scriptPage);
	urlEdit_->setPlaceholderText(Tr("Script.UrlPlaceholder"));
	auto *loadUrlButton = new QPushButton(Tr("Button.LoadUrl"), scriptPage);
	fileRow->addWidget(importButton);
	fileRow->addWidget(saveButton);
	fileRow->addStretch();
	scriptLayout->addLayout(fileRow);
	auto *urlRow = new QHBoxLayout();
	urlRow->addWidget(urlEdit_, 1);
	urlRow->addWidget(loadUrlButton);
	scriptLayout->addLayout(urlRow);
	tabs->addTab(scriptPage, Tr("Tab.Script"));

	auto *controlPage = new QWidget(this);
	auto *controlLayout = new QVBoxLayout(controlPage);
	auto *buttonRow = new QHBoxLayout();
	const auto addButton = [&](const QString &text, auto slot) {
		auto *button = new QPushButton(text, controlPage);
		connect(button, &QPushButton::clicked, state_, slot);
		buttonRow->addWidget(button);
	};
	addButton(Tr("Button.PlayPause"), &TeleprompterState::playPause);
	addButton(Tr("Button.Stop"), &TeleprompterState::stop);
	addButton(Tr("Button.Restart"), &TeleprompterState::restart);
	addButton(Tr("Button.Top"), &TeleprompterState::jumpToTop);
	controlLayout->addLayout(buttonRow);

	auto *markerRow = new QHBoxLayout();
	auto *prevMarker = new QPushButton(Tr("Button.PreviousMarker"), controlPage);
	auto *nextMarker = new QPushButton(Tr("Button.NextMarker"), controlPage);
	connect(prevMarker, &QPushButton::clicked, state_, &TeleprompterState::jumpToPreviousMarker);
	connect(nextMarker, &QPushButton::clicked, state_, &TeleprompterState::jumpToNextMarker);
	markerRow->addWidget(prevMarker);
	markerRow->addWidget(nextMarker);
	controlLayout->addLayout(markerRow);

	auto *controlForm = new QFormLayout();
	speedSpin_ = new QDoubleSpinBox(controlPage);
	speedSpin_->setRange(0, 600);
	speedSpin_->setSuffix(Tr("Unit.PixelsPerSecond"));
	speedSpin_->setValue(state_->speed());
	controlForm->addRow(Tr("Label.ScrollSpeed"), speedSpin_);
	audioSourceCombo_ = new QComboBox(controlPage);
	controlForm->addRow(Tr("Label.PauseWhenAudioInactive"), audioSourceCombo_);
	resumeOnAudioActiveCheck_ = new QCheckBox(Tr("Check.ResumeWhenAudioActive"), controlPage);
	audioResumeDelaySpin_ = new QDoubleSpinBox(controlPage);
	audioResumeDelaySpin_->setRange(0.0, 30.0);
	audioResumeDelaySpin_->setSingleStep(0.5);
	audioResumeDelaySpin_->setSuffix(Tr("Unit.Seconds"));
	controlForm->addRow(resumeOnAudioActiveCheck_);
	controlForm->addRow(Tr("Label.AudioResumeDelay"), audioResumeDelaySpin_);
	controlLayout->addLayout(controlForm);
	statusLabel_ = new QLabel(controlPage);
	remoteLabel_ = new QLabel(controlPage);
	controlLayout->addWidget(statusLabel_);
	controlLayout->addWidget(remoteLabel_);
	controlLayout->addStretch();
	tabs->addTab(controlPage, Tr("Tab.Control"));

	auto *stylePage = new QWidget(this);
	auto *styleForm = new QFormLayout(stylePage);
	fontCombo_ = new QFontComboBox(stylePage);
	fontSizeSpin_ = new QSpinBox(stylePage);
	fontSizeSpin_->setRange(12, 220);
	lineSpacingSpin_ = new QDoubleSpinBox(stylePage);
	lineSpacingSpin_->setRange(0.75, 3.0);
	lineSpacingSpin_->setSingleStep(0.05);
	paragraphSpacingSpin_ = new QSpinBox(stylePage);
	paragraphSpacingSpin_->setRange(0, 160);
	marginSpin_ = new QSpinBox(stylePage);
	marginSpin_->setRange(0, 400);
	horizontalAlignCombo_ = new QComboBox(stylePage);
	horizontalAlignCombo_->addItem(Tr("Alignment.Left"), int(Qt::AlignLeft));
	horizontalAlignCombo_->addItem(Tr("Alignment.Center"), int(Qt::AlignHCenter));
	horizontalAlignCombo_->addItem(Tr("Alignment.Right"), int(Qt::AlignRight));
	verticalAlignCombo_ = new QComboBox(stylePage);
	verticalAlignCombo_->addItem(Tr("Alignment.Top"), int(Qt::AlignTop));
	verticalAlignCombo_->addItem(Tr("Alignment.Center"), int(Qt::AlignVCenter));
	verticalAlignCombo_->addItem(Tr("Alignment.Bottom"), int(Qt::AlignBottom));
	mirrorHorizontalCheck_ = new QCheckBox(Tr("Check.MirrorHorizontal"), stylePage);
	mirrorVerticalCheck_ = new QCheckBox(Tr("Check.FlipVertical"), stylePage);
	positionIndicatorCheck_ = new QCheckBox(Tr("Check.PositionIndicator"), stylePage);
	auto *textColor = new QPushButton(Tr("Button.TextColor"), stylePage);
	auto *backgroundColor = new QPushButton(Tr("Button.BackgroundColor"), stylePage);
	styleForm->addRow(Tr("Label.Font"), fontCombo_);
	styleForm->addRow(Tr("Label.FontSize"), fontSizeSpin_);
	styleForm->addRow(Tr("Label.LineSpacing"), lineSpacingSpin_);
	styleForm->addRow(Tr("Label.ParagraphSpacing"), paragraphSpacingSpin_);
	styleForm->addRow(Tr("Label.Margins"), marginSpin_);
	styleForm->addRow(Tr("Label.HorizontalAlign"), horizontalAlignCombo_);
	styleForm->addRow(Tr("Label.VerticalAlign"), verticalAlignCombo_);
	styleForm->addRow(mirrorHorizontalCheck_);
	styleForm->addRow(mirrorVerticalCheck_);
	styleForm->addRow(positionIndicatorCheck_);
	styleForm->addRow(textColor, backgroundColor);
	tabs->addTab(stylePage, Tr("Tab.Style"));

	connect(titleEdit_, &QLineEdit::editingFinished, this, &TeleprompterDock::applyEditor);
	connect(scriptEdit_, &QPlainTextEdit::textChanged, this, &TeleprompterDock::applyEditor);
	connect(importButton, &QPushButton::clicked, this, &TeleprompterDock::importText);
	connect(loadUrlButton, &QPushButton::clicked, this, &TeleprompterDock::loadUrl);
	connect(urlEdit_, &QLineEdit::returnPressed, this, &TeleprompterDock::loadUrl);
	connect(saveButton, &QPushButton::clicked, this, &TeleprompterDock::saveText);
	connect(audioSourceCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] {
		state_->setPauseWhenAudioSourceInactive(audioSourceCombo_->currentData().toString());
	});
	connect(resumeOnAudioActiveCheck_, &QCheckBox::toggled, this, [this](bool enabled) {
		state_->setResumeWhenAudioSourceActive(enabled);
		audioResumeDelaySpin_->setEnabled(enabled);
	});
	connect(audioResumeDelaySpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), state_, &TeleprompterState::setAudioResumeDelaySeconds);
	connect(speedSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), state_, &TeleprompterState::setSpeed);
	connect(textColor, &QPushButton::clicked, this, &TeleprompterDock::chooseTextColor);
	connect(backgroundColor, &QPushButton::clicked, this, &TeleprompterDock::chooseBackgroundColor);

	const QList<QComboBox *> comboBoxes{fontCombo_, horizontalAlignCombo_, verticalAlignCombo_};
	for (auto *widget : comboBoxes)
		connect(widget, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &TeleprompterDock::applyStyle);
	connect(fontSizeSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, &TeleprompterDock::applyStyle);
	connect(lineSpacingSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &TeleprompterDock::applyStyle);
	connect(paragraphSpacingSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, &TeleprompterDock::applyStyle);
	connect(marginSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, &TeleprompterDock::applyStyle);
	connect(mirrorHorizontalCheck_, &QCheckBox::toggled, this, &TeleprompterDock::applyStyle);
	connect(mirrorVerticalCheck_, &QCheckBox::toggled, this, &TeleprompterDock::applyStyle);
	connect(positionIndicatorCheck_, &QCheckBox::toggled, this, &TeleprompterDock::applyStyle);

	connect(state_, &TeleprompterState::statusChanged, this, &TeleprompterDock::updateStatus);
	connect(state_, &TeleprompterState::displayChanged, this, &TeleprompterDock::syncOutputWindow);
	connect(state_, &TeleprompterState::scriptChanged, this, [this] {
		if (scriptEdit_->toPlainText() != state_->script() || titleEdit_->text() != state_->title())
			syncFromState();
	});
	connect(remote_, &RemoteServer::statusChanged, this, &TeleprompterDock::updateStatus);
	connect(qApp, &QGuiApplication::screenAdded, this, &TeleprompterDock::refreshScreens);
	connect(qApp, &QGuiApplication::screenRemoved, this, &TeleprompterDock::refreshScreens);

	refreshScreens();
	refreshAudioSources();
	syncFromState();
	updateStatus();
}

void TeleprompterDock::refreshScreens()
{
	syncOutputWindow();
}

void TeleprompterDock::refreshAudioSources()
{
	const QSignalBlocker blocker(audioSourceCombo_);
	const QString selected = state_->pauseWhenAudioSourceInactive();
	audioSourceCombo_->clear();
	audioSourceCombo_->addItem(Tr("AudioSource.None"), QString());

	obs_enum_sources(
		[](void *param, obs_source_t *source) {
			auto *combo = static_cast<QComboBox *>(param);
			if (!IsAudioInputCaptureSource(source))
				return true;
			const char *name = obs_source_get_name(source);
			if (name && *name)
				combo->addItem(QString::fromUtf8(name), QString::fromUtf8(name));
			return true;
		},
		audioSourceCombo_);

	const int index = audioSourceCombo_->findData(selected);
	audioSourceCombo_->setCurrentIndex(index >= 0 ? index : 0);
}

void TeleprompterDock::syncFromState()
{
	const TeleprompterStyle style = state_->style();
	const QSignalBlocker titleBlocker(titleEdit_);
	const QSignalBlocker scriptBlocker(scriptEdit_);
	const QSignalBlocker fontBlocker(fontCombo_);
	const QSignalBlocker fontSizeBlocker(fontSizeSpin_);
	const QSignalBlocker lineSpacingBlocker(lineSpacingSpin_);
	const QSignalBlocker paragraphSpacingBlocker(paragraphSpacingSpin_);
	const QSignalBlocker marginBlocker(marginSpin_);
	const QSignalBlocker mirrorHorizontalBlocker(mirrorHorizontalCheck_);
	const QSignalBlocker mirrorVerticalBlocker(mirrorVerticalCheck_);
	const QSignalBlocker positionIndicatorBlocker(positionIndicatorCheck_);
	const QSignalBlocker speedBlocker(speedSpin_);
	const QSignalBlocker audioSourceBlocker(audioSourceCombo_);
	const QSignalBlocker resumeOnAudioActiveBlocker(resumeOnAudioActiveCheck_);
	const QSignalBlocker audioResumeDelayBlocker(audioResumeDelaySpin_);

	titleEdit_->setText(state_->title());
	scriptEdit_->setPlainText(state_->script());
	fontCombo_->setCurrentFont(QFont(style.fontFamily));
	fontSizeSpin_->setValue(style.fontSize);
	lineSpacingSpin_->setValue(style.lineSpacing);
	paragraphSpacingSpin_->setValue(style.paragraphSpacing);
	marginSpin_->setValue(style.margin);
	mirrorHorizontalCheck_->setChecked(style.mirrorHorizontal);
	mirrorVerticalCheck_->setChecked(style.mirrorVertical);
	positionIndicatorCheck_->setChecked(style.showPositionIndicator);
	speedSpin_->setValue(state_->speed());
	const int audioSourceIndex = audioSourceCombo_->findData(state_->pauseWhenAudioSourceInactive());
	audioSourceCombo_->setCurrentIndex(audioSourceIndex >= 0 ? audioSourceIndex : 0);
	resumeOnAudioActiveCheck_->setChecked(state_->resumeWhenAudioSourceActive());
	audioResumeDelaySpin_->setValue(state_->audioResumeDelaySeconds());
	audioResumeDelaySpin_->setEnabled(state_->resumeWhenAudioSourceActive());
}

void TeleprompterDock::applyEditor()
{
	state_->setTitle(titleEdit_->text());
	state_->setScript(scriptEdit_->toPlainText());
}

void TeleprompterDock::importText()
{
	const QString path = QFileDialog::getOpenFileName(this, Tr("Dialog.ImportScript"), QString(), Tr("Dialog.TextFileFilter"));
	if (path.isEmpty())
		return;
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
		return;
	scriptEdit_->setPlainText(QString::fromUtf8(file.readAll()));
	titleEdit_->setText(QFileInfo(path).completeBaseName());
	applyEditor();
}

void TeleprompterDock::loadUrl()
{
	const QUrl url = TelePTextUrlFromInput(urlEdit_->text());
	if (!url.isValid() || url.scheme().isEmpty())
		return;

	statusLabel_->setText(Tr("Status.LoadingUrl"));
	auto *reply = network_.get(TelePTextRequest(url));
	connect(reply, &QNetworkReply::finished, this, [this, reply, url] {
		const QByteArray body = reply->readAll();
		const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
		const QString errorString = reply->errorString();
		const bool ok = reply->error() == QNetworkReply::NoError;
		reply->deleteLater();

		const auto applyText = [this, url](const QByteArray &data) {
			const QString text = QString::fromUtf8(data);
			if (text.trimmed().isEmpty()) {
				statusLabel_->setText(Tr("Status.UrlEmpty"));
				return;
			}
			titleEdit_->setText(TelePTitleFromUrl(url));
			scriptEdit_->setPlainText(text);
			applyEditor();
			statusLabel_->setText(Tr("Status.UrlLoaded").arg(text.size()));
		};

		if (ok) {
			applyText(body);
			return;
		}

		auto *process = new QProcess(this);
		connect(process, &QProcess::finished, this, [this, process, applyText, httpStatus, errorString](int exitCode, QProcess::ExitStatus exitStatus) {
			const QByteArray output = process->readAllStandardOutput();
			const QString curlError = QString::fromUtf8(process->readAllStandardError()).trimmed();
			process->deleteLater();
			if (exitStatus == QProcess::NormalExit && exitCode == 0) {
				applyText(output);
				return;
			}

			QString message = Tr("Status.UrlLoadFailed").arg(httpStatus).arg(errorString);
			if (!curlError.isEmpty())
				message += QStringLiteral(" | curl: ") + curlError;
			statusLabel_->setText(message);
		});
		process->start(QStringLiteral("curl.exe"), TelePCurlTextArguments(url));
		if (!process->waitForStarted(3000)) {
			process->deleteLater();
			statusLabel_->setText(Tr("Status.UrlLoadFailed").arg(httpStatus).arg(errorString));
		}
	});
}

void TeleprompterDock::saveText()
{
	const QString path = QFileDialog::getSaveFileName(this, Tr("Dialog.SaveScript"), state_->title() + QStringLiteral(".txt"), Tr("Dialog.TextFileSaveFilter"));
	if (path.isEmpty())
		return;
	QFile file(path);
	if (file.open(QIODevice::WriteOnly | QIODevice::Text))
		file.write(scriptEdit_->toPlainText().toUtf8());
}

void TeleprompterDock::chooseTextColor()
{
	TeleprompterStyle style = state_->style();
	const QColor color = QColorDialog::getColor(style.textColor, this);
	if (color.isValid()) {
		style.textColor = color;
		state_->setStyle(style);
	}
}

void TeleprompterDock::chooseBackgroundColor()
{
	TeleprompterStyle style = state_->style();
	const QColor color = QColorDialog::getColor(style.backgroundColor, this);
	if (color.isValid()) {
		style.backgroundColor = color;
		state_->setStyle(style);
	}
}

void TeleprompterDock::showOutput()
{
	state_->setOutputFullscreenEnabled(true);
	syncOutputWindow();
}

void TeleprompterDock::applyDisplay()
{
}

void TeleprompterDock::syncOutputWindow()
{
	if (state_->outputFullscreenEnabled())
		window_->showOnScreen(state_->targetScreenIndex());
	else
		window_->hide();
}

void TeleprompterDock::applyStyle()
{
	TeleprompterStyle style = state_->style();
	style.fontFamily = fontCombo_->currentFont().family();
	style.fontSize = fontSizeSpin_->value();
	style.lineSpacing = lineSpacingSpin_->value();
	style.paragraphSpacing = paragraphSpacingSpin_->value();
	style.margin = marginSpin_->value();
	style.horizontalAlignment = Qt::Alignment(horizontalAlignCombo_->currentData().toInt());
	style.verticalAlignment = Qt::Alignment(verticalAlignCombo_->currentData().toInt());
	style.mirrorHorizontal = mirrorHorizontalCheck_->isChecked();
	style.mirrorVertical = mirrorVerticalCheck_->isChecked();
	style.showPositionIndicator = positionIndicatorCheck_->isChecked();
	state_->setStyle(style);
}

void TeleprompterDock::updateStatus()
{
	statusLabel_->setText(Tr("Status.PlaybackFormat")
				      .arg(state_->isPlaying() ? Tr("State.Playing") : Tr("State.Paused"))
				      .arg(state_->speed(), 0, 'f', 0)
				      .arg(state_->progress() * 100.0, 0, 'f', 0));
	remoteLabel_->setText(Tr("Status.RemoteFormat")
				      .arg(remote_->isListening() ? Tr("State.Listening") : Tr("State.Stopped"))
				      .arg(remote_->port())
				      .arg(remote_->token()));
}
