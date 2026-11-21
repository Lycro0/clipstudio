#include "ClipEditor.hpp"

#include <QCheckBox>
#include <QDateTime>
#include <QFile>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QStandardPaths>
#include <QUrl>
#include <QVBoxLayout>

static const QStringList kClipFilters = {"*.mp4", "*.mkv", "*.mov"};

ClipEditor::ClipEditor(QWidget *parent) : QWidget(parent)
{
	auto *splitter = new QSplitter(this);

	/* ---- left: clip library ---- */
	auto *left = new QWidget;
	auto *ll = new QVBoxLayout(left);
	ll->setContentsMargins(0, 0, 0, 0);
	ll->addWidget(new QLabel(tr("<b>Clips folder</b>")));
	list = new QListWidget;
	ll->addWidget(list, 1);
	auto *lb = new QHBoxLayout;
	auto *refresh = new QPushButton(tr("Refresh"));
	auto *browse = new QPushButton(tr("Open file..."));
	auto *openFolder = new QPushButton(tr("Open folder"));
	lb->addWidget(refresh);
	lb->addWidget(browse);
	lb->addWidget(openFolder);
	ll->addLayout(lb);

	/* ---- right: track mixer ---- */
	auto *right = new QWidget;
	auto *rl = new QVBoxLayout(right);
	title = new QLabel(tr("Select a clip on the left"));
	details = new QLabel;
	rl->addWidget(title);
	rl->addWidget(details);

	auto *group = new QGroupBox(tr("Audio tracks"));
	auto *gl = new QVBoxLayout(group);
	tracksBox = new QWidget;
	tracksLayout = new QVBoxLayout(tracksBox);
	tracksLayout->setContentsMargins(0, 0, 0, 0);
	gl->addWidget(tracksBox);
	rebuildMaster = new QCheckBox(tr("Rebuild Track 1 (the main mix players use) from the other tracks"));
	rebuildMaster->setToolTip(
		tr("Most players and social sites only play Track 1. With this on, Track 1 is re-created from the\n"
		   "adjusted separate tracks, so turning Discord down also turns it down in Track 1."));
	gl->addWidget(rebuildMaster);
	rl->addWidget(group);

	auto *bl = new QHBoxLayout;
	playOrigBtn = new QPushButton(tr("Play original"));
	resetBtn = new QPushButton(tr("Reset"));
	previewBtn = new QPushButton(tr("Preview"));
	saveBtn = new QPushButton(tr("Save as copy"));
	overwriteBtn = new QPushButton(tr("Overwrite clip"));
	cancelBtn = new QPushButton(tr("Cancel"));
	for (QPushButton *b : {playOrigBtn, resetBtn, previewBtn, saveBtn, overwriteBtn, cancelBtn})
		bl->addWidget(b);
	rl->addLayout(bl);
	progress = new QProgressBar;
	progress->setRange(0, 1000);
	progress->setTextVisible(false);
	rl->addWidget(progress);
	rl->addStretch(1);

	splitter->addWidget(left);
	splitter->addWidget(right);
	splitter->setStretchFactor(0, 1);
	splitter->setStretchFactor(1, 2);

	auto *main = new QVBoxLayout(this);
	main->addWidget(splitter);

	connect(refresh, &QPushButton::clicked, this, &ClipEditor::RefreshList);
	connect(openFolder, &QPushButton::clicked, this,
		[this]() { QDesktopServices::openUrl(QUrl::fromLocalFile(folder)); });
	connect(browse, &QPushButton::clicked, this, [this]() {
		QString f = QFileDialog::getOpenFileName(this, tr("Open clip"), folder,
							 tr("Videos (*.mp4 *.mkv *.mov);;All files (*)"));
		if (!f.isEmpty())
			OpenClip(f);
	});
	connect(list, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *it) {
		if (it && !busy)
			OpenClip(it->data(Qt::UserRole).toString());
	});
	connect(playOrigBtn, &QPushButton::clicked, this,
		[this]() { QDesktopServices::openUrl(QUrl::fromLocalFile(currentPath)); });
	connect(resetBtn, &QPushButton::clicked, this, [this]() {
		for (TrackRow &r : rows) {
			r.slider->setValue(100);
			r.mute->setChecked(false);
		}
	});
	connect(previewBtn, &QPushButton::clicked, this, [this]() { StartRender(Target::Preview); });
	connect(saveBtn, &QPushButton::clicked, this, [this]() { StartRender(Target::SaveCopy); });
	connect(overwriteBtn, &QPushButton::clicked, this, [this]() { StartRender(Target::Overwrite); });
	connect(cancelBtn, &QPushButton::clicked, this, [this]() { cancel = true; });

	SetBusy(false);
}

ClipEditor::~ClipEditor()
{
	cancel = true;
	if (worker.joinable())
		worker.join();
}

void ClipEditor::SetScale(double scale)
{
	uiScale = scale;
	QFont tf = font();
	tf.setBold(true);
	tf.setPointSizeF(tf.pointSizeF() * 1.4);
	title->setFont(tf);
	for (QPushButton *b : {playOrigBtn, resetBtn, previewBtn, saveBtn, overwriteBtn, cancelBtn})
		b->setMinimumHeight((int)(36 * scale));
	progress->setMinimumHeight((int)(14 * scale));
}

void ClipEditor::SetFolder(const QString &f)
{
	folder = f;
	RefreshList();
}

void ClipEditor::RefreshList()
{
	QString keep = currentPath;
	list->blockSignals(true);
	list->clear();
	QDir dir(folder);
	QFileInfoList files = dir.entryInfoList(kClipFilters, QDir::Files, QDir::Time);
	for (const QFileInfo &fi : files) {
		auto *it = new QListWidgetItem(
			QString("%1\n%2 · %3 MB")
				.arg(fi.completeBaseName(), fi.lastModified().toString("yyyy-MM-dd hh:mm"))
				.arg(fi.size() / (1024.0 * 1024.0), 0, 'f', 1));
		it->setData(Qt::UserRole, fi.absoluteFilePath());
		list->addItem(it);
		if (fi.absoluteFilePath() == keep)
			list->setCurrentItem(it);
	}
	list->blockSignals(false);
}

void ClipEditor::OpenClip(const QString &path)
{
	QString err;
	ClipInfo ci;
	if (!ProbeClip(path, ci, err)) {
		QMessageBox::warning(this, tr("Open clip"), err);
		return;
	}
	currentPath = path;
	info = ci;
	title->setText(QFileInfo(path).fileName());
	details->setText(tr("%1 s · %2 audio track(s)").arg(ci.durationSec, 0, 'f', 1).arg(ci.tracks.size()));
	rebuildMaster->setEnabled(ci.tracks.size() > 1);
	rebuildMaster->setChecked(ci.tracks.size() > 1);
	BuildTrackRows();
	progress->setValue(0);
	SetBusy(false);
}

void ClipEditor::BuildTrackRows()
{
	QLayoutItem *child;
	while ((child = tracksLayout->takeAt(0)) != nullptr) {
		if (child->layout()) {
			QLayoutItem *c2;
			while ((c2 = child->layout()->takeAt(0)) != nullptr) {
				delete c2->widget();
				delete c2;
			}
		}
		delete child->widget();
		delete child;
	}
	rows.clear();

	for (int i = 0; i < info.tracks.size(); i++) {
		auto *row = new QHBoxLayout;
		auto *label = new QLabel(QString("%1. %2").arg(i + 1).arg(info.tracks[i].title));
		label->setMinimumWidth((int)(160 * uiScale));
		auto *slider = new QSlider(Qt::Horizontal);
		slider->setRange(0, 200);
		slider->setValue(100);
		auto *spin = new QSpinBox;
		spin->setRange(0, 200);
		spin->setSuffix(" %");
		spin->setValue(100);
		auto *mute = new QCheckBox(tr("Mute"));
		connect(slider, &QSlider::valueChanged, spin, &QSpinBox::setValue);
		connect(spin, &QSpinBox::valueChanged, slider, &QSlider::setValue);
		row->addWidget(label);
		row->addWidget(slider, 1);
		row->addWidget(spin);
		row->addWidget(mute);
		tracksLayout->addLayout(row);
		rows.append({slider, spin, mute});
	}
	if (info.tracks.isEmpty())
		tracksLayout->addWidget(new QLabel(tr("This file has no audio tracks.")));
}

RemixOptions ClipEditor::CurrentOptions() const
{
	RemixOptions o;
	for (const TrackRow &r : rows)
		o.gains.append(r.mute->isChecked() ? 0.0 : r.slider->value() / 100.0);
	o.rebuildMaster = rebuildMaster->isEnabled() && rebuildMaster->isChecked();
	o.audioBitrateKbps = audioKbps;
	return o;
}

void ClipEditor::SetBusy(bool b)
{
	busy = b;
	bool have = !currentPath.isEmpty() && !info.tracks.isEmpty();
	for (QPushButton *btn : {previewBtn, saveBtn, overwriteBtn, resetBtn})
		btn->setEnabled(!b && have);
	playOrigBtn->setEnabled(!b && !currentPath.isEmpty());
	tracksBox->setEnabled(!b);
	rebuildMaster->setEnabled(!b && info.tracks.size() > 1);
	cancelBtn->setVisible(b);
	list->setEnabled(!b);
}

void ClipEditor::StartRender(Target target)
{
	if (busy || currentPath.isEmpty())
		return;
	if (worker.joinable())
		worker.join();

	QFileInfo fi(currentPath);
	QString ext = fi.suffix();
	QString out;
	if (target == Target::Preview) {
		out = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
			      .filePath("clipstudio-preview." + ext);
	} else if (target == Target::SaveCopy) {
		QString suggested = fi.dir().filePath(fi.completeBaseName() + " (edited)." + ext);
		out = QFileDialog::getSaveFileName(this, tr("Save edited clip"), suggested,
						   tr("Video (*.%1)").arg(ext));
		if (out.isEmpty())
			return;
	} else {
		if (QMessageBox::question(this, tr("Overwrite clip"),
					  tr("Replace the original clip with the adjusted audio? This cannot be undone.")) !=
		    QMessageBox::Yes)
			return;
		out = fi.dir().filePath("." + fi.completeBaseName() + ".clipstudio-tmp." + ext);
	}

	cancel = false;
	progress->setValue(0);
	SetBusy(true);

	QString in = currentPath;
	RemixOptions opts = CurrentOptions();
	worker = std::thread([this, in, out, opts, target]() {
		QString err;
		bool ok = RemixClip(
			in, out, opts,
			[this](double p) {
				QMetaObject::invokeMethod(progress, "setValue", Qt::QueuedConnection,
							  Q_ARG(int, (int)(p * 1000)));
			},
			[this]() { return cancel.load(); }, err);

		QMetaObject::invokeMethod(
			this,
			[this, ok, err, in, out, target]() {
				SetBusy(false);
				if (!ok) {
					QFile::remove(out);
					if (!cancel)
						QMessageBox::warning(this, tr("Export failed"), err);
					progress->setValue(0);
					return;
				}
				if (target == Target::Preview) {
					QDesktopServices::openUrl(QUrl::fromLocalFile(out));
				} else if (target == Target::Overwrite) {
					QFile::remove(in);
					if (!QFile::rename(out, in)) {
						QMessageBox::warning(this, tr("Overwrite clip"),
								     tr("Could not replace the clip. The edited version "
									"was kept at:\n%1")
									     .arg(out));
						return;
					}
					OpenClip(in);
					RefreshList();
				} else {
					RefreshList();
				}
			},
			Qt::QueuedConnection);
	});
}
