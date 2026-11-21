#pragma once

#include <QString>
#include <QList>

enum class CaptureMode { Game, Desktop };
enum class AudioMode { Simple, PerApp };

struct AppAudioTrack {
	QString name; // track title shown in video editors
	QString exe;  // executable to capture, e.g. "Discord.exe"
};

/* Audio track layout:
 *   Simple : 1 = Mic + Desktop, 2 = Desktop, 3 = Mic
 *   Per-app: 1 = Mic + Desktop (everything), 2 = Mic, 3 = Discord (any edition),
 *            4 = Spotify, 5 = the game being played (detected automatically),
 *            6.. = automatic tracks for any other app that plays sound. */
constexpr int kMaxAppTracks = 4; // legacy setting, no longer used
constexpr int kMaxAutoTracks = 8;

struct AppSettings {
	QString clipHotkey = "Alt+O";
	QString recordHotkey = "Alt+R";
	QString showHotkey = "Alt+\\";

	QString outputDir;
	QString container = "mp4";
	int clipSeconds = 30;
	int fps = 60;
	int videoBitrate = 20000; // kbps
	int audioBitrate = 192;   // kbps per track
	QString encoder;          // empty = automatic

	CaptureMode captureMode = CaptureMode::Game;
	QString gameExe;   // empty = any fullscreen game
	QString monitorId; // empty = primary monitor (\\.\DISPLAY1 style device name)
	bool captureCursor = true;

	int outputHeight = 1080;     // 0 = native; otherwise scale down to this height (keeps aspect)

	bool clipAllMonitors = true; // also save a separate clip of every other monitor
	int extraFps = 30;           // other monitors are recorded lighter than the main one
	int extraHeight = 720;       // other monitors' resolution (0 = native)

	AudioMode audioMode = AudioMode::PerApp;
	QString micDevice = "default";
	QString desktopDevice = "default";
	QList<AppAudioTrack> appTracks; // fixed app tracks
	int autoTracks = 5;             // extra tracks that follow whatever else plays sound

	bool startMinimized = false;

	void load();
	void save() const;
};
