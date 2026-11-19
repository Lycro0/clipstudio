#pragma once

#include <QString>
#include <QStringList>
#include <QList>

enum class CaptureMode { Game, Desktop };
enum class AudioMode { Simple, PerApp };

struct AppAudioTrack {
	QString name;      // label shown in editors (becomes the audio track title)
	QString windowId;  // wasapi_process_output_capture "window" value (title:class:exe)
};

/* OBS supports 6 audio tracks:
 *   Simple mode : 1 = Mic + Desktop, 2 = Desktop, 3 = Mic
 *   Per-app mode: 1 = Mic + Desktop (master), 2 = Mic, 3..6 = up to 4 apps */
constexpr int kMaxAppTracks = 4;

struct AppSettings {
	// Hotkeys (Qt portable text, e.g. "Alt+O")
	QString clipHotkey = "Alt+O";
	QString recordHotkey = "Alt+R";
	QString showHotkey = "Alt+\\";

	// Output
	QString outputDir;
	QString container = "mp4";
	int clipSeconds = 30;
	int fps = 60;
	int videoBitrate = 20000; // kbps
	int audioBitrate = 192;   // kbps per track
	QString encoderId;        // empty = auto (NVENC > AMF > QSV > x264)

	// Video
	CaptureMode captureMode = CaptureMode::Game;
	QString gameWindow;       // empty = any fullscreen application
	QString monitorId;        // empty = primary monitor
	bool captureCursor = true;

	// Audio
	AudioMode audioMode = AudioMode::Simple;
	QString micDevice = "default";
	QString desktopDevice = "default";
	QList<AppAudioTrack> appTracks;

	bool startMinimized = false;

	void load();
	void save() const;
};
