#include <QApplication>
#include <QDir>
#include <QIcon>
#include <QLockFile>
#include <QMessageBox>
#include <QStandardPaths>

#include "Engine.hpp"
#include "MainWindow.hpp"
#include "Theme.hpp"
#include "VideoCapture.hpp"

int main(int argc, char *argv[])
{
	QApplication app(argc, argv);
	QApplication::setApplicationName("ClipStudio");
	QApplication::setOrganizationName("ClipStudio");
	QApplication::setQuitOnLastWindowClosed(false); // keeps running in the tray
	ApplyDarkTheme(app);
	QApplication::setWindowIcon(QIcon(":/icon.png"));

	QLockFile lock(QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation)).filePath("clipstudio.lock"));
	if (!lock.tryLock(100)) {
		QMessageBox::information(nullptr, "ClipStudio", "ClipStudio is already running (check the system tray).");
		return 0;
	}

	if (!VideoCapture::IsSupported()) {
		QMessageBox::critical(nullptr, "ClipStudio",
				      "Screen capture is not supported on this PC. Windows 10 version 2004 or newer is required.");
		return 1;
	}

	Engine engine;
	int ret;
	{
		MainWindow w(&engine);
		w.ApplyInitialSettings();

		AppSettings s;
		s.load();
		if (!s.startMinimized)
			w.showMaximized();

		ret = app.exec();
	}
	engine.Shutdown();
	return ret;
}
