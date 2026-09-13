#pragma once

#include <QColor>
#include <QElapsedTimer>
#include <QFont>
#include <QJsonObject>
#include <QObject>
#include <QTimer>
#include <QVector>

struct TeleprompterMarker {
	QString name;
	int characterOffset = 0;
	double positionPx = 0.0;
};

struct TeleprompterStyle {
	QString fontFamily = QStringLiteral("Arial");
	int fontSize = 54;
	double lineSpacing = 1.15;
	int paragraphSpacing = 18;
	int margin = 96;
	QColor textColor = Qt::white;
	QColor backgroundColor = Qt::black;
	Qt::Alignment horizontalAlignment = Qt::AlignHCenter;
	Qt::Alignment verticalAlignment = Qt::AlignVCenter;
	bool mirrorHorizontal = false;
	bool mirrorVertical = false;
	bool showPositionIndicator = true;
};

struct TeleprompterOverlaySettings {
	bool enabled = false;
	int position = 1;
	bool showProgress = true;
	bool showPlaybackState = true;
	bool showSpeed = true;
	bool showTitle = true;
};

struct TeleprompterProgressBarSettings {
	bool enabled = false;
	int position = 1;
	int thickness = 10;
	QColor color = QColor(14, 165, 255);
};

enum class TeleprompterPlaybackState {
	Stopped,
	Paused,
	Playing,
};

class TeleprompterState : public QObject {
	Q_OBJECT

public:
	explicit TeleprompterState(QObject *parent = nullptr);

	QString title() const { return title_; }
	QString script() const { return script_; }
	TeleprompterStyle style() const { return style_; }
	TeleprompterOverlaySettings overlaySettings() const { return overlaySettings_; }
	TeleprompterProgressBarSettings progressBarSettings() const { return progressBarSettings_; }
	double speed() const { return speedPxPerSecond_; }
	double position() const { return positionPx_; }
	double progress() const;
	bool isPlaying() const { return playbackState_ == TeleprompterPlaybackState::Playing; }
	TeleprompterPlaybackState playbackState() const { return playbackState_; }
	int targetScreenIndex() const { return targetScreenIndex_; }
	bool outputFullscreenEnabled() const { return outputFullscreenEnabled_; }
	double outputRenderScale() const { return outputRenderScale_; }
	QString pauseWhenAudioSourceInactive() const { return pauseWhenAudioSourceInactive_; }
	bool resumeWhenAudioSourceActive() const { return resumeWhenAudioSourceActive_; }
	double audioResumeDelaySeconds() const { return audioResumeDelaySeconds_; }
	bool audioPauseOverride() const { return audioPauseOverride_; }
	QVector<TeleprompterMarker> markers() const { return markers_; }
	double contentHeight() const { return contentHeight_; }

	void setTitle(const QString &title);
	void setScript(const QString &script);
	void setStyle(const TeleprompterStyle &style);
	void setOverlaySettings(const TeleprompterOverlaySettings &settings);
	void setProgressBarSettings(const TeleprompterProgressBarSettings &settings);
	void setSpeed(double pxPerSecond);
	void setContentHeight(double height);
	void setPosition(double positionPx);
	void setTargetScreenIndex(int screenIndex);
	void setOutputFullscreenEnabled(bool enabled);
	void setOutputRenderScale(double scale);
	void setPauseWhenAudioSourceInactive(const QString &sourceName);
	void setResumeWhenAudioSourceActive(bool enabled);
	void setAudioResumeDelaySeconds(double seconds);
	void setAudioPauseOverride(bool enabled);

	void loadFromJson(const QJsonObject &object);
	QJsonObject toJson() const;

public slots:
	void play();
	void pause();
	void playPause();
	void stop();
	void restart();
	void jumpToTop();
	void jumpToNextMarker();
	void jumpToPreviousMarker();
	void changeSpeed(double delta);
	void changeFontSize(int delta);
	void setJogMultiplier(double multiplier);

signals:
	void scriptChanged();
	void styleChanged();
	void playbackChanged();
	void positionChanged();
	void displayChanged();
	void statusChanged();

private slots:
	void tick();

private:
	void recalculateMarkers();

	QString title_;
	QString script_;
	TeleprompterStyle style_;
	TeleprompterOverlaySettings overlaySettings_;
	TeleprompterProgressBarSettings progressBarSettings_;
	double speedPxPerSecond_ = 70.0;
	double positionPx_ = 0.0;
	double contentHeight_ = 1.0;
	TeleprompterPlaybackState playbackState_ = TeleprompterPlaybackState::Stopped;
	int targetScreenIndex_ = 0;
	bool outputFullscreenEnabled_ = false;
	double outputRenderScale_ = 1.0;
	QString pauseWhenAudioSourceInactive_;
	bool resumeWhenAudioSourceActive_ = false;
	double audioResumeDelaySeconds_ = 0.0;
	bool audioPauseOverride_ = false;
	double jogMultiplier_ = 0.0;
	QTimer timer_;
	QElapsedTimer elapsed_;
	QVector<TeleprompterMarker> markers_;
};
