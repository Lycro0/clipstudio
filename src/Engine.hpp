#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "AppSettings.hpp"
#include "WinUtil.hpp"

extern "C" {
#include <libavutil/rational.h>
}

struct AVCodecContext;
struct AVPacket;
class AudioCapture;
class Muxer;

/* Capture -> encode -> in-memory clip buffer (+ optional recording).
 *
 * Video: one pipeline for the main capture (game or chosen monitor) and, if enabled, one per
 * other monitor. Every pipeline shares the same audio tracks, so a clip produces one file per
 * monitor with identical audio. */
class Engine : public QObject {
	Q_OBJECT

public:
	explicit Engine(QObject *parent = nullptr);
	~Engine() override;

	bool Apply(const AppSettings &settings, QString &error);
	void Shutdown();

	bool IsRecording() const { return recording; }
	bool IsBufferActive() const { return running; }

	/* Video encoders that work on this PC, as (display name, ffmpeg encoder name). */
	static NameValueList AvailableEncoders();

	/* What each app track (3, 4, 5, ...) is following right now, for the UI. */
	QStringList AppTrackStatus();

public slots:
	void SaveClip();
	void ToggleRecording();

signals:
	void ClipSaved(const QString &path);
	void RecordingStarted(const QString &path);
	void RecordingStopped(const QString &path);
	void ErrorOccurred(const QString &message);
	void BufferStateChanged(bool active);
	void AutoTracksChanged();

private:
	struct Stored {
		int stream; // 0..V-1 = video pipelines, V.. = audio tracks
		AVPacket *pkt;
		int64_t us;
		bool key;
	};

	struct VideoPipe {
		bool main = false;
		HMONITOR monitor = nullptr; // extra pipelines: the monitor to capture
		QString label;              // file name suffix, e.g. "Display 2"
		int width = 0, height = 0;
		int fps = 60;
		int bitrate = 20000; // kbps
		AVCodecContext *enc = nullptr;
		std::thread thread;
		std::atomic<bool> forceKeyframe{false};
	};

	struct Recording {
		std::unique_ptr<Muxer> muxer;
		bool waitingForKey = true;
		int64_t startUs = 0;
		QString path;
	};

	enum class SlotKind { Discord, Spotify, Game, Auto };
	struct AutoSlot {
		SlotKind kind = SlotKind::Auto;
		int source = -1;        // index into sources
		QString exe;            // app currently followed ("" = free)
		int64_t lastActive = 0; // GetTickCount64() when it last played sound
	};

	bool OpenEncoders(QString &error);
	void CloseEncoders();
	void VideoThread(int pipe);
	void AudioThread();
	void WatchThread();
	void OnPacket(int stream, AVPacket *pkt, AVRational tb);
	void StopThreads();
	QStringList CurrentTrackNames();
	QString NewFilePath(const QString &base, const QString &suffix) const;
	bool WriteFile(const QString &path, int pipe, const std::vector<Stored> &packets, int64_t startUs,
		       const QStringList &names, bool fastStart, QString &error);

	AppSettings s;

	std::vector<std::unique_ptr<VideoPipe>> pipes;
	std::vector<AVCodecContext *> aenc;
	QStringList fixedTrackNames;                // names of the non-auto tracks
	std::vector<std::vector<int>> trackSources; // per track: indices into sources
	std::vector<std::unique_ptr<AudioCapture>> sources;

	std::mutex autoMutex;
	std::vector<AutoSlot> autoSlots;
	QString capturedExe; // exe of the window the main capture follows (game mode), under autoMutex

	std::thread audioThread, watchThread;
	std::atomic<bool> running{false};
	int64_t startQpc = 0;
	int64_t qpcFreq = 1;

	std::mutex storeMutex;
	std::deque<Stored> store;

	std::mutex recMutex;
	std::vector<Recording> recs; // one per video pipeline while recording
	std::atomic<bool> recording{false};
};
