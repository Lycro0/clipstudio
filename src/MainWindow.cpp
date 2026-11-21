#include "MainWindow.hpp"
#include "Engine.hpp"
#include "Hotkeys.hpp"
#include "Updater.hpp"
#include "WinUtil.hpp"
#include "ClipEditor.hpp"

#include <QAction>
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSpinBox>
#include <QStyle>
#include <QTabWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

MainWindow::MainWindow(Engine *eng, QWidget *parent) : QMainWindow(parent), engine(eng)
{
	hotkeys = new Hotkeys(this);
	connect(hotkeys, &Hotkeys::Triggered, this, [this](int id) {
		if (id == Hotkeys::SaveClip)
			engine->SaveClip();
		else if (id == Hotkeys::ToggleRecording)
			engine->ToggleRecording();
		else if (id == Hotkeys::ToggleWindow)
			ToggleVisible();
	});

	updater = new Updater(this, this);
	connect(updater, &Updater::Status, this, [this](const QString &m) {
		Log(m);
		Notify("ClipStudio", m);
	});
	connect(updater, &Updater::UpdateAvailable, this,
		[this](const QString &v) { Log(tr("Update available: ClipStudio %1").arg(v)); });

	setWindowTitle(QString("ClipStudio %1").arg(Updater::CurrentVersion()));
	setWindowIcon(QIcon(":/icon.png"));
	resize(1100, 720);

	settings.load();
	baseFontPt = QApplication::font().pointSizeF() > 0 ? QApplication::font().pointSizeF() : 9.0;

	tabs = new QTabWidget;
	editor = new ClipEditor;
	tabs->addTab(BuildHomeTab(), tr("Home"));
	tabs->addTab(editor, tr("Clips"));
	tabs->addTab(BuildSettingsTab(), tr("Settings"));
	setCentralWidget(tabs);

	/* tray */
	tray = new QSystemTrayIcon(windowIcon(), this);
	auto *menu = new QMenu(this);
	menu->addAction(tr("Show ClipStudio"), this, [this]() { ToggleVisible(); });
	menu->addAction(tr("Save Clip"), engine, &Engine::SaveClip);
	trayRecord = menu->addAction(tr("Start Recording"), engine, &Engine::ToggleRecording);
	menu->addSeparator();
	menu->addAction(tr("Quit"), this, [this]() {
		reallyQuit = true;
		close();
	});
	tray->setContextMenu(menu);
	tray->setToolTip("ClipStudio");
	connect(tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason r) {
		if (r == QSystemTrayIcon::Trigger || r == QSystemTrayIcon::DoubleClick)
			ToggleVisible();
	});
	tray->show();

	/* engine events */
	connect(engine, &Engine::ClipSaved, this, [this](const QString &p) {
		Log(tr("Clip saved: %1").arg(p));
		Notify(tr("Clip saved"), QFileInfo(p).fileName());
		editor->RefreshList();
	});
	connect(engine, &Engine::RecordingStarted, this, [this](const QString &p) {
		Log(tr("Recording started: %1").arg(p));
		Notify(tr("Recording started"), QFileInfo(p).fileName());
		UpdateStatus();
	});
	connect(engine, &Engine::RecordingStopped, this, [this](const QString &p) {
		Log(tr("Recording saved: %1").arg(p));
		Notify(tr("Recording saved"), QFileInfo(p).fileName());
		editor->RefreshList();
		UpdateStatus();
	});
	connect(engine, &Engine::BufferStateChanged, this, [this](bool) { UpdateStatus(); });
	connect(engine, &Engine::AutoTracksChanged, this, &MainWindow::UpdateAutoTracks);
	connect(engine, &Engine::ErrorOccurred, this, [this](const QString &m) {
		Log(tr("Error: %1").arg(m));
		Notify(tr("ClipStudio"), m);
	});

	ApplyScale();
}

/* ------------------------------------------------------------------------ */

QWidget *MainWindow::BuildHomeTab()
{
	auto *w = new QWidget;
	auto *l = new QVBoxLayout(w);

	auto *statusRow = new QHBoxLayout;
	bufferStatus = new QLabel;
	recStatus = new QLabel;
	statusRow->addWidget(bufferStatus);
	statusRow->addWidget(recStatus);
	statusRow->addStretch(1);
	l->addLayout(statusRow);

	auto *btnRow = new QHBoxLayout;
	clipBtn = new QPushButton(tr("Save Clip"));
	clipBtn->setObjectName("primary");
	recBtn = new QPushButton(tr("Start Recording"));
	auto *folderBtn = new QPushButton(tr("Open clips folder"));
	for (QPushButton *b : {clipBtn, recBtn, folderBtn}) {
		bigButtons << b;
		btnRow->addWidget(b);
	}
	l->addLayout(btnRow);

	hotkeyHint = new QLabel;
	hotkeyHint->setWordWrap(true);
	l->addWidget(hotkeyHint);

	l->addWidget(new QLabel(tr("<b>Activity</b>")));
	log = new QPlainTextEdit;
	log->setReadOnly(true);
	l->addWidget(log, 1);

	connect(clipBtn, &QPushButton::clicked, engine, &Engine::SaveClip);
	connect(recBtn, &QPushButton::clicked, engine, &Engine::ToggleRecording);
	connect(folderBtn, &QPushButton::clicked, this, [this]() {
		QDesktopServices::openUrl(QUrl::fromLocalFile(settings.outputDir));
	});
	return w;
}

QWidget *MainWindow::BuildSettingsTab()
{
	auto *inner = new QWidget;
	auto *l = new QVBoxLayout(inner);

	/* hotkeys */
	auto *hk = new QGroupBox(tr("Hotkeys (work while in game)"));
	auto *hkf = new QFormLayout(hk);
	hkClip = new QKeySequenceEdit;
	hkRecord = new QKeySequenceEdit;
	hkShow = new QKeySequenceEdit;
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
	for (QKeySequenceEdit *e : {hkClip, hkRecord, hkShow})
		e->setMaximumSequenceLength(1);
#endif
	hkf->addRow(tr("Save clip"), hkClip);
	hkf->addRow(tr("Start / stop recording"), hkRecord);
	hkf->addRow(tr("Show / hide this window"), hkShow);
	l->addWidget(hk);

	/* video */
	auto *vid = new QGroupBox(tr("What to capture"));
	auto *vf = new QFormLayout(vid);
	modeGame = new QRadioButton(tr("Game only"));
	modeDesktop = new QRadioButton(tr("Full desktop"));
	auto *modeRow = new QHBoxLayout;
	modeRow->addWidget(modeGame);
	modeRow->addWidget(modeDesktop);
	modeRow->addStretch(1);
	vf->addRow(tr("Mode"), modeRow);
	gameWindow = new QComboBox;
	auto *refreshGames = new QPushButton(tr("Refresh"));
	auto *gameRow = new QHBoxLayout;
	gameRow->addWidget(gameWindow, 1);
	gameRow->addWidget(refreshGames);
	vf->addRow(tr("Game"), gameRow);
	monitor = new QComboBox;
	vf->addRow(tr("Monitor"), monitor);
	cursor = new QCheckBox(tr("Capture mouse cursor"));
	vf->addRow("", cursor);
	allMonitors = new QCheckBox(tr("Also clip my other monitors (one extra file per monitor, same audio)"));
	allMonitors->setToolTip(tr("Every clip and recording also saves each other monitor as its own file, e.g.\n"
				   "\"Clip ... (Display 2).mp4\", so you catch Discord reactions on your second screen."));
	vf->addRow("", allMonitors);
	extraFpsBox = new QComboBox;
	for (int f : {15, 30, 60})
		extraFpsBox->addItem(tr("%1 FPS").arg(f), f);
	extraFpsBox->setToolTip(tr("Lower = less GPU/CPU. The main capture keeps its own FPS."));
	extraResBox = new QComboBox;
	extraResBox->addItem(tr("480p"), 480);
	extraResBox->addItem(tr("720p (recommended)"), 720);
	extraResBox->addItem(tr("1080p"), 1080);
	extraResBox->addItem(tr("Native"), 0);
	auto *extraRow = new QHBoxLayout;
	extraRow->addWidget(extraFpsBox);
	extraRow->addWidget(extraResBox);
	extraRow->addStretch(1);
	vf->addRow(tr("Other monitors"), extraRow);
	connect(allMonitors, &QCheckBox::toggled, extraFpsBox, &QWidget::setEnabled);
	connect(allMonitors, &QCheckBox::toggled, extraResBox, &QWidget::setEnabled);
	vf->addRow("", new QLabel(tr("The capture is always stretched to fill the whole video — no canvas to arrange.")));
	l->addWidget(vid);

	/* audio */
	auto *aud = new QGroupBox(tr("Audio"));
	auto *af = new QFormLayout(aud);
	audioSimple = new QRadioButton(tr("Mic + desktop audio"));
	audioPerApp = new QRadioButton(tr("Separate track per app"));
	auto *aRow = new QHBoxLayout;
	aRow->addWidget(audioSimple);
	aRow->addWidget(audioPerApp);
	aRow->addStretch(1);
	af->addRow(tr("Mode"), aRow);
	micDev = new QComboBox;
	deskDev = new QComboBox;
	af->addRow(tr("Microphone"), micDev);
	af->addRow(tr("Desktop audio"), deskDev);
	auto *trackHelp = new QLabel;
	trackHelp->setWordWrap(true);
	af->addRow("", trackHelp);

	appsGroup = new QGroupBox(tr("App tracks"));
	auto *ag = new QVBoxLayout(appsGroup);
	auto *layoutInfo = new QLabel(tr("Track 3: Discord (any edition)  ·  Track 4: Spotify  ·  Track 5: the game you are "
					 "playing (detected automatically)  ·  then automatic tracks: every other app that "
					 "plays sound (Chrome, ...) gets one. Closed apps free their track, and if all are "
					 "busy the one that has been quiet the longest is reused."));
	layoutInfo->setWordWrap(true);
	ag->addWidget(layoutInfo);
	autoTracks = new QSpinBox;
	autoTracks->setRange(1, kMaxAutoTracks);
	auto *autoRow = new QHBoxLayout;
	autoRow->addWidget(new QLabel(tr("Number of automatic tracks:")));
	autoRow->addWidget(autoTracks);
	autoRow->addStretch(1);
	ag->addLayout(autoRow);
	autoStatus = new QLabel;
	autoStatus->setObjectName("muted");
	autoStatus->setWordWrap(true);
	ag->addWidget(autoStatus);
	af->addRow(appsGroup);
	l->addWidget(aud);

	/* output */
	auto *out = new QGroupBox(tr("Output"));
	auto *of = new QFormLayout(out);
	outDir = new QLineEdit;
	auto *browse = new QPushButton(tr("Browse..."));
	auto *dirRow = new QHBoxLayout;
	dirRow->addWidget(outDir, 1);
	dirRow->addWidget(browse);
	of->addRow(tr("Save to"), dirRow);
	clipLen = new QSpinBox;
	clipLen->setRange(5, 600);
	clipLen->setSuffix(tr(" seconds"));
	of->addRow(tr("Clip length"), clipLen);
	container = new QComboBox;
	container->addItem("MP4", "mp4");
	container->addItem("MKV", "mkv");
	container->addItem("MOV", "mov");
	of->addRow(tr("File format"), container);
	fpsBox = new QComboBox;
	for (int f : {30, 60, 120, 144})
		fpsBox->addItem(QString::number(f), f);
	of->addRow(tr("FPS"), fpsBox);
	resBox = new QComboBox;
	resBox->addItem(tr("720p"), 720);
	resBox->addItem(tr("1080p (recommended)"), 1080);
	resBox->addItem(tr("1440p"), 1440);
	resBox->addItem(tr("Native (your monitor's resolution)"), 0);
	resBox->setToolTip(tr("Lower resolution = much less GPU, CPU and memory use."));
	of->addRow(tr("Resolution"), resBox);
	encoder = new QComboBox;
	of->addRow(tr("Video encoder"), encoder);
	vBitrate = new QSpinBox;
	vBitrate->setRange(1000, 150000);
	vBitrate->setSingleStep(1000);
	vBitrate->setSuffix(" kbps");
	of->addRow(tr("Video bitrate"), vBitrate);
	aBitrate = new QSpinBox;
	aBitrate->setRange(64, 320);
	aBitrate->setSingleStep(32);
	aBitrate->setSuffix(" kbps");
	of->addRow(tr("Audio bitrate (per track)"), aBitrate);
	startMin = new QCheckBox(tr("Start hidden in the system tray"));
	of->addRow("", startMin);
	autoUpdate = new QCheckBox(tr("Check for updates automatically (once a day)"));
	auto *checkNow = new QPushButton(tr("Check now"));
	auto *updRow = new QHBoxLayout;
	updRow->addWidget(autoUpdate);
	updRow->addWidget(checkNow);
	updRow->addStretch(1);
	of->addRow(tr("Updates"), updRow);
	connect(checkNow, &QPushButton::clicked, this, [this]() { updater->Check(true); });
	l->addWidget(out);

	auto *apply = new QPushButton(tr("Apply"));
	bigButtons << apply;
	l->addWidget(apply);
	l->addStretch(1);

	connect(modeGame, &QRadioButton::toggled, this, &MainWindow::UpdateModeVisibility);
	connect(audioPerApp, &QRadioButton::toggled, this, [this, trackHelp]() {
		UpdateModeVisibility();
		trackHelp->setText(audioPerApp->isChecked()
					   ? tr("Track 1: everything (desktop + mic)  ·  Track 2: Mic  ·  Tracks 3+: see below")
					   : tr("Track 1: Mic + Desktop · Track 2: Desktop · Track 3: Mic"));
	});
	connect(refreshGames, &QPushButton::clicked, this, &MainWindow::RefreshAppLists);
	connect(browse, &QPushButton::clicked, this, [this]() {
		QString d = QFileDialog::getExistingDirectory(this, tr("Clips folder"), outDir->text());
		if (!d.isEmpty())
			outDir->setText(d);
	});
	connect(apply, &QPushButton::clicked, this, &MainWindow::ApplySettings);

	auto *scroll = new QScrollArea;
	scroll->setWidgetResizable(true);
	scroll->setWidget(inner);
	return scroll;
}

/* ------------------------------------------------------------------------ */

static void SelectData(QComboBox *box, const QString &value)
{
	int idx = box->findData(value);
	if (idx < 0 && !value.isEmpty()) {
		box->addItem(QObject::tr("%1 (not running)").arg(value), value);
		idx = box->count() - 1;
	}
	box->setCurrentIndex(std::max(0, idx));
}

void MainWindow::RefreshDeviceLists()
{
	auto fill = [](QComboBox *box, const NameValueList &items, const QString &cur) {
		box->clear();
		for (auto &it : items)
			box->addItem(it.first, it.second);
		SelectData(box, cur);
	};
	fill(micDev, WinUtil::ListAudioDevices(true), settings.micDevice);
	fill(deskDev, WinUtil::ListAudioDevices(false), settings.desktopDevice);

	monitor->clear();
	monitor->addItem(tr("Primary monitor"), QString());
	for (auto &it : WinUtil::ListMonitors())
		monitor->addItem(it.first, it.second);
	SelectData(monitor, settings.monitorId);

	encoder->clear();
	encoder->addItem(tr("Automatic (NVIDIA > AMD > Intel > CPU)"), QString());
	for (auto &it : Engine::AvailableEncoders())
		encoder->addItem(it.first, it.second);
	SelectData(encoder, settings.encoder);
}

void MainWindow::RefreshAppLists()
{
	NameValueList apps = WinUtil::ListApps();

	QString curGame = gameWindow->count() ? gameWindow->currentData().toString() : settings.gameExe;
	gameWindow->clear();
	gameWindow->addItem(tr("Any fullscreen game (automatic)"), QString());
	for (auto &it : apps)
		gameWindow->addItem(it.first, it.second);
	SelectData(gameWindow, curGame);
}

void MainWindow::LoadSettingsIntoUi()
{
	hkClip->setKeySequence(QKeySequence::fromString(settings.clipHotkey, QKeySequence::PortableText));
	hkRecord->setKeySequence(QKeySequence::fromString(settings.recordHotkey, QKeySequence::PortableText));
	hkShow->setKeySequence(QKeySequence::fromString(settings.showHotkey, QKeySequence::PortableText));

	outDir->setText(settings.outputDir);
	clipLen->setValue(settings.clipSeconds);
	SelectData(container, settings.container);
	fpsBox->setCurrentIndex(std::max(0, fpsBox->findData(settings.fps)));
	vBitrate->setValue(settings.videoBitrate);
	aBitrate->setValue(settings.audioBitrate);
	startMin->setChecked(settings.startMinimized);
	autoUpdate->setChecked(settings.autoUpdate);
	updater->SetEnabled(settings.autoUpdate);

	modeGame->setChecked(settings.captureMode == CaptureMode::Game);
	modeDesktop->setChecked(settings.captureMode == CaptureMode::Desktop);
	cursor->setChecked(settings.captureCursor);
	allMonitors->setChecked(settings.clipAllMonitors);
	extraFpsBox->setCurrentIndex(std::max(0, extraFpsBox->findData(settings.extraFps)));
	extraFpsBox->setEnabled(settings.clipAllMonitors);
	{
		int idx = resBox->findData(settings.outputHeight);
		resBox->setCurrentIndex(idx >= 0 ? idx : 1);
		idx = extraResBox->findData(settings.extraHeight);
		extraResBox->setCurrentIndex(idx >= 0 ? idx : 1);
		extraResBox->setEnabled(settings.clipAllMonitors);
	}
	autoTracks->setValue(settings.autoTracks);
	audioSimple->setChecked(settings.audioMode == AudioMode::Simple);
	audioPerApp->setChecked(settings.audioMode == AudioMode::PerApp);

	RefreshDeviceLists();
	for (QComboBox *b : {gameWindow})
		b->clear();
	RefreshAppLists();

	UpdateModeVisibility();
	editor->SetFolder(settings.outputDir);
	editor->SetAudioBitrate(settings.audioBitrate);
	UpdateStatus();
}

AppSettings MainWindow::SettingsFromUi() const
{
	AppSettings s = settings;
	s.clipHotkey = hkClip->keySequence().toString(QKeySequence::PortableText);
	s.recordHotkey = hkRecord->keySequence().toString(QKeySequence::PortableText);
	s.showHotkey = hkShow->keySequence().toString(QKeySequence::PortableText);
	s.outputDir = outDir->text();
	s.clipSeconds = clipLen->value();
	s.container = container->currentData().toString();
	s.fps = fpsBox->currentData().toInt();
	s.encoder = encoder->currentData().toString();
	s.videoBitrate = vBitrate->value();
	s.audioBitrate = aBitrate->value();
	s.startMinimized = startMin->isChecked();
	s.autoUpdate = autoUpdate->isChecked();
	s.captureMode = modeDesktop->isChecked() ? CaptureMode::Desktop : CaptureMode::Game;
	s.gameExe = gameWindow->currentData().toString();
	s.monitorId = monitor->currentData().toString();
	s.captureCursor = cursor->isChecked();
	s.clipAllMonitors = allMonitors->isChecked();
	s.extraFps = extraFpsBox->currentData().toInt();
	s.outputHeight = resBox->currentData().toInt();
	s.extraHeight = extraResBox->currentData().toInt();
	s.autoTracks = autoTracks->value();
	s.audioMode = audioPerApp->isChecked() ? AudioMode::PerApp : AudioMode::Simple;
	s.micDevice = micDev->currentData().toString();
	s.desktopDevice = deskDev->currentData().toString();
	return s;
}

void MainWindow::UpdateModeVisibility()
{
	gameWindow->setEnabled(modeGame->isChecked());
	monitor->setEnabled(modeDesktop->isChecked());
	appsGroup->setVisible(audioPerApp->isChecked());
}

void MainWindow::ApplyHotkeys()
{
	QStringList failed;
	if (!hotkeys->Set(Hotkeys::SaveClip, settings.clipHotkey))
		failed << settings.clipHotkey;
	if (!hotkeys->Set(Hotkeys::ToggleRecording, settings.recordHotkey))
		failed << settings.recordHotkey;
	if (!hotkeys->Set(Hotkeys::ToggleWindow, settings.showHotkey))
		failed << settings.showHotkey;
	if (!failed.isEmpty())
		Log(tr("Error: these hotkeys are used by another app: %1").arg(failed.join(", ")));
}

void MainWindow::ApplyInitialSettings()
{
	LoadSettingsIntoUi();
	ApplyHotkeys();
	QString err;
	if (!engine->Apply(settings, err))
		Log(tr("Error: %1").arg(err));
	else
		Log(tr("Ready. Clip buffer holds the last %1 seconds.").arg(settings.clipSeconds));
	UpdateStatus();
}

void MainWindow::ApplySettings()
{
	AppSettings s = SettingsFromUi();
	QString err;
	if (!engine->Apply(s, err)) {
		QMessageBox::warning(this, tr("Settings"), err);
		return;
	}
	settings = s;
	updater->SetEnabled(settings.autoUpdate);
	ApplyHotkeys();
	editor->SetFolder(settings.outputDir);
	editor->SetAudioBitrate(settings.audioBitrate);
	UpdateAutoTracks();
	Log(tr("Settings applied."));
	UpdateStatus();
}

void MainWindow::UpdateStatus()
{
	bool buf = engine->IsBufferActive();
	bool rec = engine->IsRecording();
	/* Status: green dot = on, red dot = off. Text stays white/grey. */
	const QString green = "#3ddc84", red = "#ff4d4d";
	int px = (int)(18 * scale);
	auto line = [&](bool on, const QString &text) {
		return QString("<span style=\"color:%1\">●</span>&nbsp; <span style=\"color:%2\">%3</span>")
			.arg(on ? green : red, on ? "#ffffff" : "#9a9a9a", text.toHtmlEscaped());
	};
	bufferStatus->setText(line(buf, buf ? tr("Clip buffer ON (%1 s)").arg(settings.clipSeconds) : tr("Clip buffer OFF")));
	recStatus->setText(line(rec, rec ? tr("Recording") : tr("Not recording")));
	for (QLabel *lab : {bufferStatus, recStatus})
		lab->setStyleSheet(QString("font-size: %1px; font-weight: bold; padding: 8px;").arg(px));
	recBtn->setText(rec ? tr("Stop Recording") : tr("Start Recording"));
	trayRecord->setText(recBtn->text());
	clipBtn->setEnabled(buf);
	hotkeyHint->setText(tr("Hotkeys: <b>%1</b> save clip · <b>%2</b> start/stop recording · <b>%3</b> show/hide window")
				    .arg(settings.clipHotkey.toHtmlEscaped(), settings.recordHotkey.toHtmlEscaped(),
					 settings.showHotkey.toHtmlEscaped()));
	tray->setToolTip(rec ? "ClipStudio — recording" : "ClipStudio");
}

void MainWindow::Log(const QString &msg)
{
	log->appendPlainText(QDateTime::currentDateTime().toString("hh:mm:ss  ") + msg);
}

void MainWindow::Notify(const QString &title, const QString &msg)
{
	if (tray && QSystemTrayIcon::supportsMessages())
		tray->showMessage(title, msg, QSystemTrayIcon::Information, 2500);
}

void MainWindow::UpdateAutoTracks()
{
	if (settings.audioMode != AudioMode::PerApp) {
		autoStatus->clear();
		return;
	}
	autoStatus->setText(tr("Right now:\n") + engine->AppTrackStatus().join("\n"));
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
	QMainWindow::resizeEvent(event);
	double s2 = std::clamp(std::min(width() / 1100.0, height() / 720.0), 0.85, 2.2);
	if (std::abs(s2 - scale) > 0.03) {
		scale = s2;
		ApplyScale();
	}
}

/* Text, buttons and controls grow/shrink with the window. */
void MainWindow::ApplyScale()
{
	QFont f = QApplication::font();
	f.setPointSizeF(baseFontPt * scale);
	tabs->setFont(f);
	for (QPushButton *b : bigButtons)
		b->setMinimumHeight((int)(44 * scale));
	editor->SetScale(scale);
	UpdateStatus();
}

void MainWindow::ToggleVisible()
{
	if (isVisible() && isActiveWindow() && !isMinimized()) {
		hide();
		return;
	}
	showMaximized();
	raise();
	activateWindow();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
	if (!reallyQuit) {
		hide();
		Notify("ClipStudio", tr("Still running in the tray. %1 brings it back.").arg(settings.showHotkey));
		event->ignore();
		return;
	}
	if (engine->IsRecording() &&
	    QMessageBox::question(this, tr("Quit"), tr("A recording is in progress. Stop it and quit?")) !=
		    QMessageBox::Yes) {
		reallyQuit = false;
		event->ignore();
		return;
	}
	event->accept();
	qApp->quit();
}
