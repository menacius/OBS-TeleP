#include "remote-server.hpp"
#include "settings-dialog.hpp"
#include "teleprompter-locale.hpp"
#include "teleprompter-dock.hpp"
#include "teleprompter-state.hpp"
#include "teleprompter-window.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>

#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QPointer>
#include <QTimer>
#include <QDateTime>

#include <util/config-file.h>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-telep", "en-US")

namespace {
QPointer<TeleprompterState> state;
QPointer<TeleprompterWindow> outputWindow;
QPointer<TeleprompterDock> dock;
QPointer<TeleprompterSettingsDialog> settingsDialog;
QPointer<RemoteServer> remote;
QPointer<QTimer> profileSaveTimer;
QPointer<QTimer> audioMonitorTimer;
bool pausedByInactiveAudio = false;
qint64 audioBecameActiveAtMs = 0;

obs_hotkey_id playPauseHotkey = OBS_INVALID_HOTKEY_ID;
obs_hotkey_id stopHotkey = OBS_INVALID_HOTKEY_ID;
obs_hotkey_id restartHotkey = OBS_INVALID_HOTKEY_ID;
obs_hotkey_id topHotkey = OBS_INVALID_HOTKEY_ID;
obs_hotkey_id speedUpHotkey = OBS_INVALID_HOTKEY_ID;
obs_hotkey_id speedDownHotkey = OBS_INVALID_HOTKEY_ID;
obs_hotkey_id nextMarkerHotkey = OBS_INVALID_HOTKEY_ID;
obs_hotkey_id prevMarkerHotkey = OBS_INVALID_HOTKEY_ID;

const char *profileSection = "OBS-TeleP";
const char *profileStateKey = "State";
const char *profileTokenKey = "RemoteToken";

void saveProfileConfig()
{
	if (!state)
		return;

	config_t *config = obs_frontend_get_profile_config();
	if (!config)
		return;

	const QByteArray json = QJsonDocument(state->toJson()).toJson(QJsonDocument::Compact);
	config_set_string(config, profileSection, profileStateKey, json.constData());
	if (remote)
		config_set_string(config, profileSection, profileTokenKey, remote->token().toUtf8().constData());
	config_save(config);
}

void scheduleProfileSave()
{
	if (profileSaveTimer)
		profileSaveTimer->start();
}

void checkMonitoredAudioSource()
{
	if (!state)
		return;

	const QString sourceName = state->pauseWhenAudioSourceInactive();
	if (sourceName.isEmpty()) {
		pausedByInactiveAudio = false;
		audioBecameActiveAtMs = 0;
		return;
	}

	obs_source_t *source = obs_get_source_by_name(sourceName.toUtf8().constData());
	const bool active = source && obs_source_active(source);
	if (source)
		obs_source_release(source);

	if (!active) {
		audioBecameActiveAtMs = 0;
		if (state->audioPauseOverride())
			return;
		if (state->isPlaying()) {
			pausedByInactiveAudio = true;
			state->pause();
		}
		return;
	}

	if (state->audioPauseOverride())
		state->setAudioPauseOverride(false);

	if (state->isPlaying()) {
		pausedByInactiveAudio = false;
		audioBecameActiveAtMs = 0;
		return;
	}

	if (!pausedByInactiveAudio || !state->resumeWhenAudioSourceActive()) {
		audioBecameActiveAtMs = 0;
		return;
	}

	const qint64 now = QDateTime::currentMSecsSinceEpoch();
	if (audioBecameActiveAtMs == 0)
		audioBecameActiveAtMs = now;

	const qint64 delayMs = qint64(state->audioResumeDelaySeconds() * 1000.0);
	if (now - audioBecameActiveAtMs >= delayMs) {
		pausedByInactiveAudio = false;
		audioBecameActiveAtMs = 0;
		state->play();
	}
}

void loadProfileConfig()
{
	if (!state)
		return;

	config_t *config = obs_frontend_get_profile_config();
	if (!config)
		return;

	const char *json = config_get_string(config, profileSection, profileStateKey);
	if (json && *json) {
		const QJsonDocument doc = QJsonDocument::fromJson(QByteArray(json));
		if (doc.isObject())
			state->loadFromJson(doc.object());
	}

	const char *token = config_get_string(config, profileSection, profileTokenKey);
	if (remote && token && *token)
		remote->setToken(QString::fromUtf8(token));
}

void invoke(void (TeleprompterState::*method)())
{
	if (state)
		QMetaObject::invokeMethod(state, method, Qt::QueuedConnection);
}

void hotkeyCallback(void *, obs_hotkey_id id, obs_hotkey_t *, bool pressed)
{
	if (!pressed)
		return;
	if (id == playPauseHotkey)
		invoke(&TeleprompterState::playPause);
	else if (id == stopHotkey)
		invoke(&TeleprompterState::stop);
	else if (id == restartHotkey)
		invoke(&TeleprompterState::restart);
	else if (id == topHotkey)
		invoke(&TeleprompterState::jumpToTop);
	else if (id == nextMarkerHotkey)
		invoke(&TeleprompterState::jumpToNextMarker);
	else if (id == prevMarkerHotkey)
		invoke(&TeleprompterState::jumpToPreviousMarker);
	else if (id == speedUpHotkey && state)
		QMetaObject::invokeMethod(state, [] { state->changeSpeed(5); }, Qt::QueuedConnection);
	else if (id == speedDownHotkey && state)
		QMetaObject::invokeMethod(state, [] { state->changeSpeed(-5); }, Qt::QueuedConnection);
}

void registerHotkeys()
{
	playPauseHotkey = obs_hotkey_register_frontend("OBS.TeleP.PlayPause", obs_module_text("Hotkey.PlayPause"), hotkeyCallback, nullptr);
	stopHotkey = obs_hotkey_register_frontend("OBS.TeleP.Stop", obs_module_text("Hotkey.Stop"), hotkeyCallback, nullptr);
	restartHotkey = obs_hotkey_register_frontend("OBS.TeleP.Restart", obs_module_text("Hotkey.Restart"), hotkeyCallback, nullptr);
	topHotkey = obs_hotkey_register_frontend("OBS.TeleP.Top", obs_module_text("Hotkey.JumpToTop"), hotkeyCallback, nullptr);
	speedUpHotkey = obs_hotkey_register_frontend("OBS.TeleP.SpeedUp", obs_module_text("Hotkey.SpeedUp"), hotkeyCallback, nullptr);
	speedDownHotkey = obs_hotkey_register_frontend("OBS.TeleP.SpeedDown", obs_module_text("Hotkey.SpeedDown"), hotkeyCallback, nullptr);
	nextMarkerHotkey = obs_hotkey_register_frontend("OBS.TeleP.NextMarker", obs_module_text("Hotkey.NextMarker"), hotkeyCallback, nullptr);
	prevMarkerHotkey = obs_hotkey_register_frontend("OBS.TeleP.PreviousMarker", obs_module_text("Hotkey.PreviousMarker"), hotkeyCallback, nullptr);
}

void unregisterHotkey(obs_hotkey_id &id)
{
	if (id != OBS_INVALID_HOTKEY_ID) {
		obs_hotkey_unregister(id);
		id = OBS_INVALID_HOTKEY_ID;
	}
}

void saveCallback(obs_data_t *saveData, bool saving, void *)
{
	const char *key = "obs_telep";
	if (!state)
		return;

	if (saving) {
		const QByteArray json = QJsonDocument(state->toJson()).toJson(QJsonDocument::Compact);
		obs_data_set_string(saveData, key, json.constData());
	} else {
		const char *json = obs_data_get_string(saveData, key);
		if (json && *json) {
			const QJsonDocument doc = QJsonDocument::fromJson(QByteArray(json));
			if (doc.isObject())
				state->loadFromJson(doc.object());
		}
	}
}

void frontendEvent(enum obs_frontend_event event, void *)
{
	if (event == OBS_FRONTEND_EVENT_EXIT) {
		saveProfileConfig();
		if (remote)
			remote->stop();
	} else if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING) {
		if (dock) {
			QMetaObject::invokeMethod(dock, "syncOutputWindow", Qt::QueuedConnection);
			QTimer::singleShot(0, dock, [] {
				if (dock)
					dock->refreshAudioSources();
			});
		}
	}
}

void showSettingsDialog(void *)
{
	if (!settingsDialog && state && outputWindow)
		settingsDialog = new TeleprompterSettingsDialog(state, outputWindow);
	if (settingsDialog) {
		settingsDialog->show();
		settingsDialog->raise();
		settingsDialog->activateWindow();
	}
}
}

bool obs_module_load()
{
	state = new TeleprompterState();
	outputWindow = new TeleprompterWindow(state);
	remote = new RemoteServer(state);
	loadProfileConfig();
	remote->start(4457);
	profileSaveTimer = new QTimer();
	profileSaveTimer->setSingleShot(true);
	profileSaveTimer->setInterval(1000);
	QObject::connect(profileSaveTimer, &QTimer::timeout, [] { saveProfileConfig(); });
	audioMonitorTimer = new QTimer();
	audioMonitorTimer->setInterval(500);
	QObject::connect(audioMonitorTimer, &QTimer::timeout, [] { checkMonitoredAudioSource(); });
	audioMonitorTimer->start();
	QObject::connect(state, &TeleprompterState::scriptChanged, [] { scheduleProfileSave(); });
	QObject::connect(state, &TeleprompterState::styleChanged, [] { scheduleProfileSave(); });
	QObject::connect(state, &TeleprompterState::playbackChanged, [] { scheduleProfileSave(); });
	QObject::connect(state, &TeleprompterState::displayChanged, [] { scheduleProfileSave(); });
	QObject::connect(remote, &RemoteServer::statusChanged, [] { scheduleProfileSave(); });
	dock = new TeleprompterDock(state, outputWindow, remote);

	obs_frontend_add_dock_by_id("obs_telep_dock", obs_module_text("Dock.Title"), dock);
	obs_frontend_add_tools_menu_item(obs_module_text("Settings.MenuItem"), showSettingsDialog, nullptr);
	obs_frontend_add_save_callback(saveCallback, nullptr);
	obs_frontend_add_event_callback(frontendEvent, nullptr);
	registerHotkeys();

	blog(LOG_INFO, "[obs-telep] loaded");
	return true;
}

void obs_module_unload()
{
	obs_frontend_remove_event_callback(frontendEvent, nullptr);
	obs_frontend_remove_save_callback(saveCallback, nullptr);
	obs_frontend_remove_dock("obs_telep_dock");
	delete settingsDialog;
	settingsDialog = nullptr;
	dock = nullptr;
	saveProfileConfig();
	if (profileSaveTimer)
		profileSaveTimer->stop();
	if (audioMonitorTimer)
		audioMonitorTimer->stop();

	unregisterHotkey(playPauseHotkey);
	unregisterHotkey(stopHotkey);
	unregisterHotkey(restartHotkey);
	unregisterHotkey(topHotkey);
	unregisterHotkey(speedUpHotkey);
	unregisterHotkey(speedDownHotkey);
	unregisterHotkey(nextMarkerHotkey);
	unregisterHotkey(prevMarkerHotkey);

	delete outputWindow;
	delete remote;
	delete profileSaveTimer;
	delete audioMonitorTimer;
	delete state;
	blog(LOG_INFO, "[obs-telep] unloaded");
}
