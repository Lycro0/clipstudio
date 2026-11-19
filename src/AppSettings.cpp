#include "AppSettings.hpp"

#include <QSettings>
#include <QStandardPaths>
#include <QDir>

static QSettings &store()
{
	static QSettings s(QSettings::IniFormat, QSettings::UserScope, "ClipStudio", "ClipStudio");
	return s;
}

void AppSettings::load()
{
	QSettings &s = store();
	AppSettings d;
	QString videos = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
	d.outputDir = QDir(videos.isEmpty() ? QDir::homePath() : videos).filePath("ClipStudio");

	clipHotkey = s.value("hotkeys/clip", d.clipHotkey).toString();
	recordHotkey = s.value("hotkeys/record", d.recordHotkey).toString();
	showHotkey = s.value("hotkeys/show", d.showHotkey).toString();

	outputDir = s.value("output/dir", d.outputDir).toString();
	container = s.value("output/container", d.container).toString();
	clipSeconds = s.value("output/clipSeconds", d.clipSeconds).toInt();
	fps = s.value("output/fps", d.fps).toInt();
	videoBitrate = s.value("output/videoBitrate", d.videoBitrate).toInt();
	audioBitrate = s.value("output/audioBitrate", d.audioBitrate).toInt();
	encoder = s.value("output/encoder", d.encoder).toString();

	captureMode = s.value("video/mode", "game").toString() == "desktop" ? CaptureMode::Desktop : CaptureMode::Game;
	gameExe = s.value("video/gameExe").toString();
	monitorId = s.value("video/monitorId").toString();
	captureCursor = s.value("video/cursor", d.captureCursor).toBool();

	audioMode = s.value("audio/mode", "simple").toString() == "perapp" ? AudioMode::PerApp : AudioMode::Simple;
	micDevice = s.value("audio/mic", d.micDevice).toString();
	desktopDevice = s.value("audio/desktop", d.desktopDevice).toString();
	appTracks.clear();
	int n = s.beginReadArray("audio/apps");
	for (int i = 0; i < n && i < kMaxAppTracks; i++) {
		s.setArrayIndex(i);
		appTracks.append({s.value("name").toString(), s.value("exe").toString()});
	}
	s.endArray();

	startMinimized = s.value("general/startMinimized", false).toBool();
}

void AppSettings::save() const
{
	QSettings &s = store();
	s.setValue("hotkeys/clip", clipHotkey);
	s.setValue("hotkeys/record", recordHotkey);
	s.setValue("hotkeys/show", showHotkey);
	s.setValue("output/dir", outputDir);
	s.setValue("output/container", container);
	s.setValue("output/clipSeconds", clipSeconds);
	s.setValue("output/fps", fps);
	s.setValue("output/videoBitrate", videoBitrate);
	s.setValue("output/audioBitrate", audioBitrate);
	s.setValue("output/encoder", encoder);
	s.setValue("video/mode", captureMode == CaptureMode::Desktop ? "desktop" : "game");
	s.setValue("video/gameExe", gameExe);
	s.setValue("video/monitorId", monitorId);
	s.setValue("video/cursor", captureCursor);
	s.setValue("audio/mode", audioMode == AudioMode::PerApp ? "perapp" : "simple");
	s.setValue("audio/mic", micDevice);
	s.setValue("audio/desktop", desktopDevice);
	s.beginWriteArray("audio/apps", (int)appTracks.size());
	for (int i = 0; i < appTracks.size(); i++) {
		s.setArrayIndex(i);
		s.setValue("name", appTracks[i].name);
		s.setValue("exe", appTracks[i].exe);
	}
	s.endArray();
	s.setValue("general/startMinimized", startMinimized);
	s.sync();
}
