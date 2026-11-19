#include "Muxer.hpp"

#include <QObject>

#include <cstring>

Muxer::~Muxer()
{
	Close();
}

bool Muxer::Open(const QString &path, const AVCodecContext *video, const std::vector<AVCodecContext *> &audio,
		 const QStringList &trackNames, bool fastStart, QString &error)
{
	QByteArray p = path.toUtf8();
	if (avformat_alloc_output_context2(&ctx, nullptr, nullptr, p.constData()) < 0 || !ctx) {
		error = QObject::tr("Unsupported file format.");
		ctx = nullptr;
		return false;
	}

	auto addStream = [&](const AVCodecContext *enc) -> AVStream * {
		AVStream *st = avformat_new_stream(ctx, nullptr);
		avcodec_parameters_from_context(st->codecpar, enc);
		st->time_base = enc->time_base;
		encoderTb.push_back(enc->time_base);
		lastDts.push_back(INT64_MIN);
		return st;
	};

	AVStream *vs = addStream(video);
	vs->avg_frame_rate = video->framerate;
	for (size_t i = 0; i < audio.size(); i++) {
		AVStream *st = addStream(audio[i]);
		QByteArray name = (int)i < trackNames.size() ? trackNames[(int)i].toUtf8() : QByteArray();
		if (!name.isEmpty()) {
			// "title" for MKV, "handler_name" for MP4/MOV (MP4 drops per-track titles).
			av_dict_set(&st->metadata, "title", name.constData(), 0);
			av_dict_set(&st->metadata, "handler_name", name.constData(), 0);
		}
		st->disposition = i == 0 ? AV_DISPOSITION_DEFAULT : 0;
	}

	if (!(ctx->oformat->flags & AVFMT_NOFILE) && avio_open(&ctx->pb, p.constData(), AVIO_FLAG_WRITE) < 0) {
		error = QObject::tr("Cannot write to %1").arg(path);
		avformat_free_context(ctx);
		ctx = nullptr;
		return false;
	}

	AVDictionary *opts = nullptr;
	if (fastStart && (strstr(ctx->oformat->name, "mp4") || strstr(ctx->oformat->name, "mov")))
		av_dict_set(&opts, "movflags", "+faststart", 0);
	int ret = avformat_write_header(ctx, &opts);
	av_dict_free(&opts);
	if (ret < 0) {
		error = QObject::tr("Cannot start writing the file.");
		if (!(ctx->oformat->flags & AVFMT_NOFILE))
			avio_closep(&ctx->pb);
		avformat_free_context(ctx);
		ctx = nullptr;
		return false;
	}
	return true;
}

bool Muxer::Write(int stream, const AVPacket *pkt, int64_t offsetUs)
{
	if (!ctx || stream < 0 || stream >= (int)ctx->nb_streams)
		return false;
	AVPacket *out = av_packet_clone(pkt);
	if (!out)
		return false;
	int64_t off = av_rescale_q(offsetUs, AV_TIME_BASE_Q, encoderTb[stream]);
	if (out->pts != AV_NOPTS_VALUE)
		out->pts -= off;
	if (out->dts != AV_NOPTS_VALUE)
		out->dts -= off;
	if (out->dts < 0 || out->dts <= lastDts[stream]) { // never write out-of-order packets
		av_packet_free(&out);
		return true;
	}
	lastDts[stream] = out->dts;
	av_packet_rescale_ts(out, encoderTb[stream], ctx->streams[stream]->time_base);
	out->stream_index = stream;
	out->pos = -1;
	int ret = av_interleaved_write_frame(ctx, out);
	av_packet_free(&out);
	return ret >= 0;
}

bool Muxer::Close()
{
	if (!ctx)
		return true;
	int ret = av_write_trailer(ctx);
	if (!(ctx->oformat->flags & AVFMT_NOFILE))
		avio_closep(&ctx->pb);
	avformat_free_context(ctx);
	ctx = nullptr;
	return ret >= 0;
}
