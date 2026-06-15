#include "teleprompter-state.hpp"

#include "teleprompter-locale.hpp"

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>

TeleprompterState::TeleprompterState(QObject *parent) : QObject(parent)
{
	title_ = Tr("Script.Untitled");
	timer_.setTimerType(Qt::PreciseTimer);
	timer_.setInterval(33);
	connect(&timer_, &QTimer::timeout, this, &TeleprompterState::tick);
	elapsed_.start();
	recalculateMarkers();
}

double TeleprompterState::progress() const
{
	return qBound(0.0, positionPx_ / qMax(1.0, contentHeight_), 1.0);
}

void TeleprompterState::setTitle(const QString &title)
{
	title_ = title.trimmed().isEmpty() ? Tr("Script.Untitled") : title.trimmed();
	emit statusChanged();
}

void TeleprompterState::setScript(const QString &script)
{
	script_ = script;
	recalculateMarkers();
	emit scriptChanged();
	emit statusChanged();
}

void TeleprompterState::setStyle(const TeleprompterStyle &style)
{
	style_ = style;
	emit styleChanged();
	emit statusChanged();
}

void TeleprompterState::setOverlaySettings(const TeleprompterOverlaySettings &settings)
{
	overlaySettings_ = settings;
	emit displayChanged();
	emit statusChanged();
}

void TeleprompterState::setSpeed(double pxPerSecond)
{
	speedPxPerSecond_ = qBound(0.0, pxPerSecond, 600.0);
	emit playbackChanged();
	emit statusChanged();
}

void TeleprompterState::setContentHeight(double height)
{
	contentHeight_ = qMax(1.0, height);
	recalculateMarkers();
}

void TeleprompterState::setPosition(double positionPx)
{
	positionPx_ = qBound(0.0, positionPx, contentHeight_);
	emit positionChanged();
	emit statusChanged();
}

void TeleprompterState::setTargetScreenIndex(int screenIndex)
{
	targetScreenIndex_ = qMax(0, screenIndex);
	emit displayChanged();
	emit statusChanged();
}

void TeleprompterState::setOutputFullscreenEnabled(bool enabled)
{
	outputFullscreenEnabled_ = enabled;
	emit displayChanged();
	emit statusChanged();
}

void TeleprompterState::setOutputRenderScale(double scale)
{
	outputRenderScale_ = qBound(0.5, scale, 1.0);
	emit displayChanged();
	emit statusChanged();
}

void TeleprompterState::setPauseWhenAudioSourceInactive(const QString &sourceName)
{
	const QString trimmed = sourceName.trimmed();
	if (pauseWhenAudioSourceInactive_ == trimmed)
		return;
	pauseWhenAudioSourceInactive_ = trimmed;
	emit displayChanged();
	emit statusChanged();
}

void TeleprompterState::setResumeWhenAudioSourceActive(bool enabled)
{
	if (resumeWhenAudioSourceActive_ == enabled)
		return;
	resumeWhenAudioSourceActive_ = enabled;
	emit displayChanged();
	emit statusChanged();
}

void TeleprompterState::setAudioResumeDelaySeconds(double seconds)
{
	audioResumeDelaySeconds_ = qBound(0.0, seconds, 30.0);
	emit displayChanged();
	emit statusChanged();
}

void TeleprompterState::setAudioPauseOverride(bool enabled)
{
	audioPauseOverride_ = enabled;
	emit statusChanged();
}

void TeleprompterState::play()
{
	if (playing_)
		return;
	playing_ = true;
	elapsed_.restart();
	timer_.start();
	emit playbackChanged();
	emit statusChanged();
}

void TeleprompterState::pause()
{
	if (!playing_)
		return;
	playing_ = false;
	timer_.stop();
	emit playbackChanged();
	emit statusChanged();
}

void TeleprompterState::playPause()
{
	playing_ ? pause() : play();
}

void TeleprompterState::stop()
{
	playing_ = false;
	timer_.stop();
	setPosition(0.0);
	emit playbackChanged();
	emit statusChanged();
}

void TeleprompterState::restart()
{
	setPosition(0.0);
	play();
}

void TeleprompterState::jumpToTop()
{
	setPosition(0.0);
}

void TeleprompterState::jumpToNextMarker()
{
	for (const auto &marker : markers_) {
		if (marker.positionPx > positionPx_ + 8.0) {
			setPosition(marker.positionPx);
			return;
		}
	}
}

void TeleprompterState::jumpToPreviousMarker()
{
	for (auto it = markers_.crbegin(); it != markers_.crend(); ++it) {
		if (it->positionPx < positionPx_ - 8.0) {
			setPosition(it->positionPx);
			return;
		}
	}
	setPosition(0.0);
}

void TeleprompterState::changeSpeed(double delta)
{
	setSpeed(speedPxPerSecond_ + delta);
}

void TeleprompterState::changeFontSize(int delta)
{
	TeleprompterStyle next = style_;
	next.fontSize = qBound(12, next.fontSize + delta, 220);
	setStyle(next);
}

void TeleprompterState::setJogMultiplier(double multiplier)
{
	jogMultiplier_ = qBound(-5.0, multiplier, 5.0);
	if (!qFuzzyIsNull(jogMultiplier_) && !timer_.isActive()) {
		elapsed_.restart();
		timer_.start();
	} else if (qFuzzyIsNull(jogMultiplier_) && !playing_) {
		timer_.stop();
	}
	emit playbackChanged();
	emit statusChanged();
}

void TeleprompterState::tick()
{
	const double seconds = elapsed_.restart() / 1000.0;
	const double velocity = (playing_ ? speedPxPerSecond_ : 0.0) + (jogMultiplier_ * speedPxPerSecond_);
	positionPx_ = qBound(0.0, positionPx_ + velocity * seconds, contentHeight_);
	emit positionChanged();
	if (positionPx_ >= contentHeight_ && velocity > 0.0)
		pause();
	else if (!playing_ && qFuzzyIsNull(jogMultiplier_))
		timer_.stop();
}

void TeleprompterState::recalculateMarkers()
{
	markers_.clear();
	const QRegularExpression markerRegex(QStringLiteral("^\\s*#marker\\s*(.*)$"), QRegularExpression::CaseInsensitiveOption);
	const QStringList lines = script_.split('\n');
	int offset = 0;
	for (const QString &line : lines) {
		const auto match = markerRegex.match(line);
		if (match.hasMatch()) {
			TeleprompterMarker marker;
			marker.name = match.captured(1).trimmed();
			if (marker.name.isEmpty())
				marker.name = Tr("Marker.DefaultName").arg(markers_.size() + 1);
			marker.characterOffset = offset;
			const double fraction = script_.isEmpty() ? 0.0 : double(offset) / double(script_.size());
			marker.positionPx = fraction * contentHeight_;
			markers_.push_back(marker);
		}
		offset += line.size() + 1;
	}
	emit statusChanged();
}

QJsonObject TeleprompterState::toJson() const
{
	QJsonObject style;
	style["fontFamily"] = style_.fontFamily;
	style["fontSize"] = style_.fontSize;
	style["lineSpacing"] = style_.lineSpacing;
	style["paragraphSpacing"] = style_.paragraphSpacing;
	style["margin"] = style_.margin;
	style["textColor"] = style_.textColor.name(QColor::HexArgb);
	style["backgroundColor"] = style_.backgroundColor.name(QColor::HexArgb);
	style["horizontalAlignment"] = int(style_.horizontalAlignment);
	style["verticalAlignment"] = int(style_.verticalAlignment);
	style["mirrorHorizontal"] = style_.mirrorHorizontal;
	style["mirrorVertical"] = style_.mirrorVertical;
	style["showPositionIndicator"] = style_.showPositionIndicator;

	QJsonObject object;
	QJsonObject overlay;
	overlay["enabled"] = overlaySettings_.enabled;
	overlay["position"] = overlaySettings_.position;
	overlay["showProgress"] = overlaySettings_.showProgress;
	overlay["showPlaybackState"] = overlaySettings_.showPlaybackState;
	overlay["showSpeed"] = overlaySettings_.showSpeed;
	overlay["showTitle"] = overlaySettings_.showTitle;

	object["title"] = title_;
	object["script"] = script_;
	object["speed"] = speedPxPerSecond_;
	object["position"] = positionPx_;
	object["targetScreenIndex"] = targetScreenIndex_;
	object["outputFullscreenEnabled"] = outputFullscreenEnabled_;
	object["outputRenderScale"] = outputRenderScale_;
	object["pauseWhenAudioSourceInactive"] = pauseWhenAudioSourceInactive_;
	object["resumeWhenAudioSourceActive"] = resumeWhenAudioSourceActive_;
	object["audioResumeDelaySeconds"] = audioResumeDelaySeconds_;
	object["style"] = style;
	object["overlay"] = overlay;
	return object;
}

void TeleprompterState::loadFromJson(const QJsonObject &object)
{
	if (object.contains("title"))
		title_ = object["title"].toString(Tr("Script.Untitled"));
	if (object.contains("script"))
		script_ = object["script"].toString();
	speedPxPerSecond_ = object["speed"].toDouble(speedPxPerSecond_);
	positionPx_ = object["position"].toDouble(0.0);
	targetScreenIndex_ = object["targetScreenIndex"].toInt(targetScreenIndex_);
	outputFullscreenEnabled_ = object["outputFullscreenEnabled"].toBool(outputFullscreenEnabled_);
	outputRenderScale_ = qBound(0.5, object["outputRenderScale"].toDouble(outputRenderScale_), 1.0);
	pauseWhenAudioSourceInactive_ = object["pauseWhenAudioSourceInactive"].toString(pauseWhenAudioSourceInactive_);
	resumeWhenAudioSourceActive_ = object["resumeWhenAudioSourceActive"].toBool(resumeWhenAudioSourceActive_);
	audioResumeDelaySeconds_ = qBound(0.0, object["audioResumeDelaySeconds"].toDouble(audioResumeDelaySeconds_), 30.0);

	const QJsonObject style = object["style"].toObject();
	style_.fontFamily = style["fontFamily"].toString(style_.fontFamily);
	style_.fontSize = style["fontSize"].toInt(style_.fontSize);
	style_.lineSpacing = style["lineSpacing"].toDouble(style_.lineSpacing);
	style_.paragraphSpacing = style["paragraphSpacing"].toInt(style_.paragraphSpacing);
	style_.margin = style["margin"].toInt(style_.margin);
	style_.textColor = QColor(style["textColor"].toString(style_.textColor.name(QColor::HexArgb)));
	style_.backgroundColor = QColor(style["backgroundColor"].toString(style_.backgroundColor.name(QColor::HexArgb)));
	style_.horizontalAlignment = Qt::Alignment(style["horizontalAlignment"].toInt(int(style_.horizontalAlignment)));
	style_.verticalAlignment = Qt::Alignment(style["verticalAlignment"].toInt(int(style_.verticalAlignment)));
	style_.mirrorHorizontal = style["mirrorHorizontal"].toBool(style_.mirrorHorizontal);
	style_.mirrorVertical = style["mirrorVertical"].toBool(style_.mirrorVertical);
	style_.showPositionIndicator = style["showPositionIndicator"].toBool(style_.showPositionIndicator);

	const QJsonObject overlay = object["overlay"].toObject();
	overlaySettings_.enabled = overlay["enabled"].toBool(overlaySettings_.enabled);
	overlaySettings_.position = overlay["position"].toInt(overlaySettings_.position);
	overlaySettings_.showProgress = overlay["showProgress"].toBool(overlaySettings_.showProgress);
	overlaySettings_.showPlaybackState = overlay["showPlaybackState"].toBool(overlaySettings_.showPlaybackState);
	overlaySettings_.showSpeed = overlay["showSpeed"].toBool(overlaySettings_.showSpeed);
	overlaySettings_.showTitle = overlay["showTitle"].toBool(overlaySettings_.showTitle);

	recalculateMarkers();
	emit scriptChanged();
	emit styleChanged();
	emit playbackChanged();
	emit positionChanged();
	emit displayChanged();
	emit statusChanged();
}
