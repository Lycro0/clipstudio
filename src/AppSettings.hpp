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
 *   Per-app: 1 = Mic + Desktop (everything), 2 = Mic,
 *            then the fixed app tracks (default Discord, Spotify),
 *            then "auto" tracks that attach to any other app playing sound (e.g. a game). */
constexpr int kMaxAppTracks = 4;
constexpr int kMaxAutoTracks = 4;

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

	AudioMode audioMode = AudioMode::PerApp;
	QString micDevice = "default";
	QString desktopDevice = "default";
	QList<AppAudioTrack> appTracks; // fixed app tracks
	int autoTracks = 3;             // extra tracks that follow whatever else plays sound

	bool startMinimized = false;

	void load();
	void save() const;
};
