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
 *   Per-app: 1 = Mic + Desktop (everything), 2 = Mic, 3.. = one app each */
constexpr int kMaxAppTracks = 6;

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

	AudioMode audioMode = AudioMode::Simple;
	QString micDevice = "default";
	QString desktopDevice = "default";
	QList<AppAudioTrack> appTracks;

	bool startMinimized = false;

	void load();
	void save() const;
};
