#pragma once

#include <QString>
#include <QVector>
#include <functional>

struct ClipAudioTrack {
	int streamIndex = -1;
	QString title;
	int channels = 0;
	int sampleRate = 0;
};

struct ClipInfo {
	double durationSec = 0.0;
	bool hasVideo = false;
	QVector<ClipAudioTrack> tracks;
};

bool ProbeClip(const QString &path, ClipInfo &info, QString &error);

struct RemixOptions {
	QVector<double> gains;      // linear gain per audio track (1.0 = unchanged, 0 = muted)
	bool rebuildMaster = true;  // regenerate track 1 as the mix of tracks 2..N
	int audioBitrateKbps = 192;
};

/* Copies video untouched and re-encodes every audio track with its gain applied.
 * Runs synchronously; call from a worker thread. progress() receives 0..1. */
bool RemixClip(const QString &inPath, const QString &outPath, const RemixOptions &opts,
	       const std::function<void(double)> &progress, const std::function<bool()> &cancelled, QString &error);
