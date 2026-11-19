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

/* Capture -> encode -> in-memory clip buffer (+ optional recording). */
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

public slots:
	void SaveClip();
	void ToggleRecording();

signals:
	void ClipSaved(const QString &path);
	void RecordingStarted(const QString &path);
	void RecordingStopped(const QString &path);
	void ErrorOccurred(const QString &message);
	void BufferStateChanged(bool active);

private:
	struct Stored {
		int stream;
		AVPacket *pkt;
		int64_t us;
		bool key;
	};

	bool OpenEncoders(QString &error);
	void CloseEncoders();
	void VideoThread();
	void AudioThread();
	void OnPacket(int stream, AVPacket *pkt, AVRational tb);
	void StopThreads();
	QString NewFilePath(const QString &prefix) const;

	AppSettings s;
	int outW = 1920, outH = 1080;

	AVCodecContext *venc = nullptr;
	std::vector<AVCodecContext *> aenc;
	QStringList trackNames;
	std::vector<std::vector<int>> trackSources; // per track: indices into sources

	std::vector<std::unique_ptr<AudioCapture>> sources;

	std::thread videoThread, audioThread;
	std::atomic<bool> running{false};
	int64_t startQpc = 0;
	int64_t qpcFreq = 1;

	std::mutex storeMutex;
	std::deque<Stored> store;

	std::mutex recMutex;
	std::unique_ptr<Muxer> recMuxer;
	std::atomic<bool> recording{false};
	bool recWaitingForKey = false;
	int64_t recStartUs = 0;
	QString recPath;
	std::atomic<bool> forceKeyframe{false};
};
