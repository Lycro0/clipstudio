#include "MainWindow.hpp"
#include "ClipperEngine.hpp"
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
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QStyle>
#include <QTabWidget>
#include <QUrl>
#include <QVBoxLayout>

#ifdef _WIN32
#define SRC_GAME "game_capture"
#define SRC_MONITOR "monitor_capture"
#define SRC_MIC "wasapi_input_capture"
#define SRC_DESKTOP "wasapi_output_capture"
#define SRC_APP "wasapi_process_output_capture"
#else
#define SRC_GAME ""
#define SRC_MONITOR ""
#define SRC_MIC "pulse_input_capture"
#define SRC_DESKTOP "pulse_output_capture"
#define SRC_APP ""
#endif

/* Turns "[Discord.exe]: #general - Discord" style list names into "Discord". */
static QString FriendlyAppName(const QString &listName, const QString &windowId)
{
	QString exe = windowId.section(':', -1);
	exe = QFileInfo(exe).completeBaseName();
	if (!exe.isEmpty()) {
		exe[0] = exe[0].toUpper();
		return exe;
	}
	return listName.left(32);
}

MainWindow::MainWindow(ClipperEngine *eng, QWidget *parent) : QMainWindow(parent), engine(eng)
{
	setWindowTitle("ClipStudio");
	setWindowIcon(style()->standardIcon(QStyle::SP_MediaPlay));
	resize(1100, 720);

	settings.load();

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
	menu->addAction(tr("Save Clip"), engine, &ClipperEngine::SaveClip);
	trayRecord = menu->addAction(tr("Start Recording"), engine, &ClipperEngine::ToggleRecording);
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
	connect(engine, &ClipperEngine::ShowWindowRequested, this, &MainWindow::ToggleVisible);
	connect(engine, &ClipperEngine::ClipSaved, this, [this](const QString &p) {
		Log(tr("Clip saved: %1").arg(p));
		Notify(tr("Clip saved"), QFileInfo(p).fileName());
		editor->RefreshList();
	});
	connect(engine, &ClipperEngine::RecordingStarted, this, [this](const QString &p) {
		Log(tr("Recording started: %1").arg(p));
		Notify(tr("Recording started"), QFileInfo(p).fileName());
		UpdateStatus();
	});
	connect(engine, &ClipperEngine::RecordingStopped, this, [this](const QString &p) {
		Log(tr("Recording saved: %1").arg(p));
		Notify(tr("Recording saved"), QFileInfo(p).fileName());
		editor->RefreshList();
		UpdateStatus();
	});
	connect(engine, &ClipperEngine::ReplayStateChanged, this, [this](bool) { UpdateStatus(); });
	connect(engine, &ClipperEngine::ErrorOccurred, this, [this](const QString &m) {
		Log(tr("Error: %1").arg(m));
		Notify(tr("ClipStudio"), m);
	});
}

/* ------------------------------------------------------------------------ */

QWidget *MainWindow::BuildHomeTab()
{
	auto *w = new QWidget;
	auto *l = new QVBoxLayout(w);

	auto *statusRow = new QHBoxLayout;
	bufferStatus = new QLabel;
	recStatus = new QLabel;
	for (QLabel *lab : {bufferStatus, recStatus})
		lab->setStyleSheet("font-size: 18px; font-weight: bold; padding: 8px;");
	statusRow->addWidget(bufferStatus);
	statusRow->addWidget(recStatus);
	statusRow->addStretch(1);
	l->addLayout(statusRow);

	auto *btnRow = new QHBoxLayout;
	clipBtn = new QPushButton(tr("Save Clip"));
	recBtn = new QPushButton(tr("Start Recording"));
	auto *folderBtn = new QPushButton(tr("Open clips folder"));
	for (QPushButton *b : {clipBtn, recBtn, folderBtn}) {
		b->setMinimumHeight(48);
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

	connect(clipBtn, &QPushButton::clicked, engine, &ClipperEngine::SaveClip);
	connect(recBtn, &QPushButton::clicked, engine, &ClipperEngine::ToggleRecording);
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

	appsGroup = new QGroupBox(tr("App tracks (each app gets its own audio track)"));
	auto *ag = new QGridLayout(appsGroup);
	for (int i = 0; i < kMaxAppTracks; i++) {
		AppRow r;
		r.app = new QComboBox;
		r.name = new QLineEdit;
		r.name->setPlaceholderText(tr("Track name"));
		ag->addWidget(new QLabel(tr("Track %1").arg(i + 3)), i, 0);
		ag->addWidget(r.app, i, 1);
		ag->addWidget(r.name, i, 2);
		connect(r.app, &QComboBox::activated, this, [this, r](int idx) {
			QString id = r.app->itemData(idx).toString();
			if (!id.isEmpty())
				r.name->setText(FriendlyAppName(r.app->itemText(idx), id));
			else
				r.name->clear();
		});
		appRows.append(r);
	}
	auto *refreshApps = new QPushButton(tr("Refresh app list (start Discord/Spotify/your game first)"));
	ag->addWidget(refreshApps, kMaxAppTracks, 0, 1, 3);
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
	l->addWidget(out);

	auto *apply = new QPushButton(tr("Apply"));
	apply->setMinimumHeight(40);
	l->addWidget(apply);
	l->addStretch(1);

	connect(modeGame, &QRadioButton::toggled, this, &MainWindow::UpdateModeVisibility);
	connect(audioPerApp, &QRadioButton::toggled, this, [this, trackHelp]() {
		UpdateModeVisibility();
		trackHelp->setText(audioPerApp->isChecked()
					   ? tr("Track 1: Mic + Desktop (everything) · Track 2: Mic · Tracks 3-6: the apps below")
					   : tr("Track 1: Mic + Desktop · Track 2: Desktop · Track 3: Mic"));
	});
	connect(refreshGames, &QPushButton::clicked, this, &MainWindow::RefreshAppLists);
	connect(refreshApps, &QPushButton::clicked, this, &MainWindow::RefreshAppLists);
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
		box->addItem(QObject::tr("%1 (not running)").arg(value.section(':', -1)), value);
		idx = box->count() - 1;
	}
	box->setCurrentIndex(std::max(0, idx));
}

void MainWindow::RefreshDeviceLists()
{
	auto fill = [](QComboBox *box, const char *id, const QString &cur) {
		box->clear();
		NameValueList items = ClipperEngine::ListProperty(id, "device_id");
		if (items.isEmpty())
			box->addItem(QObject::tr("Default"), "default");
		for (auto &it : items)
			box->addItem(it.first, it.second);
		SelectData(box, cur);
	};
	fill(micDev, SRC_MIC, settings.micDevice);
	fill(deskDev, SRC_DESKTOP, settings.desktopDevice);

	monitor->clear();
	monitor->addItem(tr("Primary monitor"), QString());
	for (auto &it : ClipperEngine::ListProperty(SRC_MONITOR, "monitor_id"))
		monitor->addItem(it.first, it.second);
	SelectData(monitor, settings.monitorId);

	encoder->clear();
	encoder->addItem(tr("Automatic (NVIDIA > AMD > Intel > CPU)"), QString());
	for (auto &it : ClipperEngine::ListEncoders())
		encoder->addItem(it.first, it.second);
	SelectData(encoder, settings.encoderId);
}

void MainWindow::RefreshAppLists()
{
	QString curGame = gameWindow->currentData().toString();
	if (gameWindow->count() == 0)
		curGame = settings.gameWindow;
	gameWindow->clear();
	gameWindow->addItem(tr("Any fullscreen game (automatic)"), QString());
	for (auto &it : ClipperEngine::ListProperty(SRC_GAME, "window"))
		gameWindow->addItem(it.first, it.second);
	SelectData(gameWindow, curGame);

	NameValueList apps = ClipperEngine::ListProperty(SRC_APP, "window");
	for (int i = 0; i < appRows.size(); i++) {
		AppRow &r = appRows[i];
		QString cur = r.app->count() ? r.app->currentData().toString()
					     : (i < settings.appTracks.size() ? settings.appTracks[i].windowId : QString());
		r.app->clear();
		r.app->addItem(tr("(none)"), QString());
		for (auto &it : apps)
			r.app->addItem(it.first, it.second);
		SelectData(r.app, cur);
	}
	if (apps.isEmpty() && !*SRC_APP)
		appsGroup->setToolTip(tr("Per-app audio capture requires Windows 10 2004 or newer."));
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

	modeGame->setChecked(settings.captureMode == CaptureMode::Game);
	modeDesktop->setChecked(settings.captureMode == CaptureMode::Desktop);
	cursor->setChecked(settings.captureCursor);
	audioSimple->setChecked(settings.audioMode == AudioMode::Simple);
	audioPerApp->setChecked(settings.audioMode == AudioMode::PerApp);

	RefreshDeviceLists();
	for (QComboBox *b : {gameWindow})
		b->clear();
	for (AppRow &r : appRows)
		r.app->clear();
	RefreshAppLists();
	for (int i = 0; i < appRows.size(); i++)
		appRows[i].name->setText(i < settings.appTracks.size() ? settings.appTracks[i].name : QString());

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
	s.encoderId = encoder->currentData().toString();
	s.videoBitrate = vBitrate->value();
	s.audioBitrate = aBitrate->value();
	s.startMinimized = startMin->isChecked();
	s.captureMode = modeDesktop->isChecked() ? CaptureMode::Desktop : CaptureMode::Game;
	s.gameWindow = gameWindow->currentData().toString();
	s.monitorId = monitor->currentData().toString();
	s.captureCursor = cursor->isChecked();
	s.audioMode = audioPerApp->isChecked() ? AudioMode::PerApp : AudioMode::Simple;
	s.micDevice = micDev->currentData().toString();
	s.desktopDevice = deskDev->currentData().toString();
	s.appTracks.clear();
	for (const AppRow &r : appRows) {
		QString id = r.app->currentData().toString();
		if (id.isEmpty())
			continue;
		QString name = r.name->text().trimmed();
		if (name.isEmpty())
			name = FriendlyAppName(r.app->currentText(), id);
		s.appTracks.append({name, id});
	}
	return s;
}

void MainWindow::UpdateModeVisibility()
{
	gameWindow->setEnabled(modeGame->isChecked());
	monitor->setEnabled(modeDesktop->isChecked());
	appsGroup->setVisible(audioPerApp->isChecked());
}

void MainWindow::ApplyInitialSettings()
{
	LoadSettingsIntoUi();
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
	editor->SetFolder(settings.outputDir);
	editor->SetAudioBitrate(settings.audioBitrate);
	Log(tr("Settings applied."));
	UpdateStatus();
}

void MainWindow::UpdateStatus()
{
	bool buf = engine->IsReplayActive();
	bool rec = engine->IsRecording();
	bufferStatus->setText(buf ? tr("● Clip buffer ON (%1 s)").arg(settings.clipSeconds) : tr("○ Clip buffer OFF"));
	bufferStatus->setStyleSheet(QString("font-size: 18px; font-weight: bold; padding: 8px; color: %1;")
					    .arg(buf ? "#2e9d4f" : "#999"));
	recStatus->setText(rec ? tr("● RECORDING") : tr("○ Not recording"));
	recStatus->setStyleSheet(QString("font-size: 18px; font-weight: bold; padding: 8px; color: %1;")
					 .arg(rec ? "#d23a3a" : "#999"));
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
