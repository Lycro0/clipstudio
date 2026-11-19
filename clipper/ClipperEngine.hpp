#pragma once

#include <QObject>
#include <QString>
#include <QList>
#include <QPair>
#include <obs.hpp>

#include "AppSettings.hpp"

using NameValueList = QList<QPair<QString, QString>>; // (display name, value)

class ClipperEngine : public QObject {
	Q_OBJECT

public:
	explicit ClipperEngine(QObject *parent = nullptr);
	~ClipperEngine() override;

	bool Startup(QString &error);
	void Shutdown();

	/* (Re)builds sources, encoders and outputs from settings.
	 * Refuses while a recording is in progress. */
	bool Apply(const AppSettings &settings, QString &error);

	bool IsRecording() const;
	bool IsReplayActive() const;

	/* Helpers for the settings UI. */
	static NameValueList ListProperty(const char *sourceId, const char *propName);
	static NameValueList ListEncoders();

public slots:
	void SaveClip();
	void ToggleRecording();

signals:
	void ClipSaved(const QString &path);
	void RecordingStarted(const QString &path);
	void RecordingStopped(const QString &path);
	void ShowWindowRequested();
	void ErrorOccurred(const QString &message);
	void ReplayStateChanged(bool active);

private:
	bool ResetVideo(QString &error);
	void ClearPipeline();
	bool BuildSources();
	bool BuildEncoders();
	bool BuildOutputs(QString &error);
	void RegisterHotkeys();
	void StartReplayBuffer();

	static void OnReplaySaved(void *data, calldata_t *cd);
	static void OnReplayStop(void *data, calldata_t *cd);
	static void OnRecordStop(void *data, calldata_t *cd);

	AppSettings s;
	bool started = false;
	int canvasW = 0, canvasH = 0, canvasFps = 0;

	OBSSceneAutoRelease scene;
	OBSSourceAutoRelease videoSource;
	QList<OBSSource> audioSources;

	OBSEncoderAutoRelease videoEncoder;
	QList<OBSEncoder> audioEncoders;

	OBSOutputAutoRelease replayOutput;
	OBSOutputAutoRelease recordOutput;
	OBSSignal replaySavedSignal, replayStopSignal, recordStopSignal;
	QString currentRecordingPath;
	bool shuttingDown = false;

	obs_hotkey_id clipHotkey = OBS_INVALID_HOTKEY_ID;
	obs_hotkey_id recordHotkey = OBS_INVALID_HOTKEY_ID;
	obs_hotkey_id showHotkey = OBS_INVALID_HOTKEY_ID;
};
