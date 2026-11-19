#include <QApplication>
#include <QMessageBox>

#include "ClipperEngine.hpp"
#include "MainWindow.hpp"

int main(int argc, char *argv[])
{
	QApplication app(argc, argv);
	QApplication::setApplicationName("ClipStudio");
	QApplication::setOrganizationName("ClipStudio");
	QApplication::setQuitOnLastWindowClosed(false); // keep running in the tray

	ClipperEngine engine;
	QString err;
	if (!engine.Startup(err)) {
		QMessageBox::critical(nullptr, "ClipStudio", err);
		return 1;
	}

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
