#pragma once

#include <QWidget>
#include <QVector>
#include <atomic>
#include <thread>

#include "AudioRemix.hpp"

class QListWidget;
class QLabel;
class QVBoxLayout;
class QCheckBox;
class QPushButton;
class QProgressBar;
class QSlider;
class QSpinBox;

class ClipEditor : public QWidget {
	Q_OBJECT

public:
	explicit ClipEditor(QWidget *parent = nullptr);
	~ClipEditor() override;

	void SetFolder(const QString &folder);
	void SetAudioBitrate(int kbps) { audioKbps = kbps; }

public slots:
	void RefreshList();
	void OpenClip(const QString &path);

private:
	struct TrackRow {
		QSlider *slider;
		QSpinBox *spin;
		QCheckBox *mute;
	};

	enum class Target { Preview, SaveCopy, Overwrite };

	void BuildTrackRows();
	void StartRender(Target target);
	void SetBusy(bool busy);
	RemixOptions CurrentOptions() const;

	QString folder;
	QString currentPath;
	ClipInfo info;
	int audioKbps = 192;

	QListWidget *list;
	QLabel *title;
	QLabel *details;
	QWidget *tracksBox;
	QVBoxLayout *tracksLayout;
	QVector<TrackRow> rows;
	QCheckBox *rebuildMaster;
	QPushButton *previewBtn, *saveBtn, *overwriteBtn, *resetBtn, *cancelBtn, *playOrigBtn;
	QProgressBar *progress;

	std::thread worker;
	std::atomic<bool> cancel{false};
	bool busy = false;
};
