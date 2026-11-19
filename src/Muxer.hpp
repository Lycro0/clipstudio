#pragma once

#include <QString>
#include <QStringList>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

/* Writes encoded packets (stream 0 = video, 1.. = audio tracks) to a file. */
class Muxer {
public:
	~Muxer();

	bool Open(const QString &path, const AVCodecContext *video, const std::vector<AVCodecContext *> &audio,
		  const QStringList &trackNames, bool fastStart, QString &error);

	/* `offsetUs` is subtracted from timestamps so the file starts at 0. */
	bool Write(int stream, const AVPacket *pkt, int64_t offsetUs);
	bool Close();
	bool IsOpen() const { return ctx != nullptr; }

private:
	AVFormatContext *ctx = nullptr;
	std::vector<AVRational> encoderTb;
	std::vector<int64_t> lastDts;
};
