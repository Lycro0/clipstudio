#pragma once

#include <QMainWindow>
#include <QSystemTrayIcon>
#include <QVector>

#include "AppSettings.hpp"

class Engine;
class Hotkeys;
class ClipEditor;
class QLabel;
class QPushButton;
class QPlainTextEdit;
class QKeySequenceEdit;
class QLineEdit;
class QComboBox;
class QSpinBox;
class QCheckBox;
class QRadioButton;
class QTabWidget;
class QGroupBox;

class MainWindow : public QMainWindow {
	Q_OBJECT

public:
	MainWindow(Engine *engine, QWidget *parent = nullptr);

	void ApplyInitialSettings();
	void ToggleVisible();

protected:
	void closeEvent(QCloseEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;

private:
	QWidget *BuildHomeTab();
	QWidget *BuildSettingsTab();
	void LoadSettingsIntoUi();
	AppSettings SettingsFromUi() const;
	void RefreshDeviceLists();
	void RefreshAppLists();
	void UpdateModeVisibility();
	void UpdateStatus();
	void Log(const QString &msg);
	void Notify(const QString &title, const QString &msg);
	void ApplySettings();
	void ApplyScale();
	void UpdateAutoTracks();

	Engine *engine;
	Hotkeys *hotkeys;
	void ApplyHotkeys();
	AppSettings settings;
	bool reallyQuit = false;

	QTabWidget *tabs;
	ClipEditor *editor;
	QSystemTrayIcon *tray;
	QAction *trayRecord;

	/* home */
	QLabel *bufferStatus, *recStatus, *hotkeyHint;
	QPushButton *clipBtn, *recBtn;
	QPlainTextEdit *log;

	/* settings */
	QKeySequenceEdit *hkClip, *hkRecord, *hkShow;
	QLineEdit *outDir;
	QComboBox *container, *fpsBox, *encoder;
	QSpinBox *clipLen, *vBitrate, *aBitrate;
	QRadioButton *modeGame, *modeDesktop;
	QComboBox *gameWindow, *monitor;
	QCheckBox *cursor, *startMin;
	QRadioButton *audioSimple, *audioPerApp;
	QComboBox *micDev, *deskDev;
	QGroupBox *appsGroup;
	QSpinBox *autoTracks;
	QLabel *autoStatus;
	QCheckBox *allMonitors;
	QComboBox *resBox, *extraFpsBox;

	/* size-based scaling */
	double scale = 1.0;
	double baseFontPt = 9.0;
	QList<QPushButton *> bigButtons;
	struct AppRow {
		QComboBox *app;
		QLineEdit *name;
	};
	QVector<AppRow> appRows;
};
