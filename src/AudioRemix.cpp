#include "AudioRemix.hpp"

#include <QObject>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>
}

#include <memory>
#include <vector>
#include <algorithm>

static QString AvErr(int err)
{
	char buf[AV_ERROR_MAX_STRING_SIZE] = {0};
	av_strerror(err, buf, sizeof(buf));
	return QString::fromUtf8(buf);
}

bool ProbeClip(const QString &path, ClipInfo &info, QString &error)
{
	info = ClipInfo();
	AVFormatContext *fmt = nullptr;
	int ret = avformat_open_input(&fmt, path.toUtf8().constData(), nullptr, nullptr);
	if (ret < 0) {
		error = QObject::tr("Cannot open file: %1").arg(AvErr(ret));
		return false;
	}
	avformat_find_stream_info(fmt, nullptr);
	if (fmt->duration > 0)
		info.durationSec = (double)fmt->duration / AV_TIME_BASE;

	int n = 0;
	for (unsigned i = 0; i < fmt->nb_streams; i++) {
		AVStream *st = fmt->streams[i];
		if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
			info.hasVideo = true;
		} else if (st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
			ClipAudioTrack t;
			t.streamIndex = (int)i;
			AVDictionaryEntry *e = av_dict_get(st->metadata, "title", nullptr, 0);
			if (!e || !*e->value)
				e = av_dict_get(st->metadata, "handler_name", nullptr, 0);
			QString name = e ? QString::fromUtf8(e->value).trimmed() : QString();
			if (name.isEmpty() || name == "SoundHandler")
				name = QString("Track %1").arg(n + 1);
			t.title = name;
			t.channels = st->codecpar->ch_layout.nb_channels;
			t.sampleRate = st->codecpar->sample_rate;
			info.tracks.append(t);
			n++;
		}
	}
	avformat_close_input(&fmt);
	return true;
}

namespace {

struct AudioPipe {
	AVStream *in = nullptr;
	AVStream *out = nullptr;
	AVCodecContext *dec = nullptr;
	AVCodecContext *enc = nullptr;
	SwrContext *swr = nullptr;
	AVAudioFifo *fifo = nullptr;
	int64_t nextPts = AV_NOPTS_VALUE; // in encoder time base (1/sample_rate)
	double gain = 1.0;

	~AudioPipe()
	{
		avcodec_free_context(&dec);
		avcodec_free_context(&enc);
		swr_free(&swr);
		if (fifo)
			av_audio_fifo_free(fifo);
	}
};

struct Remuxer {
	AVFormatContext *ifmt = nullptr;
	AVFormatContext *ofmt = nullptr;
	std::vector<int> streamMap;            // input stream -> output stream (-1 = drop)
	std::vector<int> audioOf;              // input stream -> audio pipe index (-1 = not audio)
	std::vector<std::unique_ptr<AudioPipe>> audio;
	bool rebuildMaster = false;
	int frameSize = 1024;
	int channels = 2;

	~Remuxer()
	{
		audio.clear();
		if (ofmt) {
			if (!(ofmt->oformat->flags & AVFMT_NOFILE))
				avio_closep(&ofmt->pb);
			avformat_free_context(ofmt);
		}
		avformat_close_input(&ifmt);
	}

	bool encodeFrame(AudioPipe &p, AVFrame *frame, QString &error)
	{
		int ret = avcodec_send_frame(p.enc, frame);
		if (ret < 0 && ret != AVERROR_EOF) {
			error = QObject::tr("Audio encode failed: %1").arg(AvErr(ret));
			return false;
		}
		AVPacket *pkt = av_packet_alloc();
		while ((ret = avcodec_receive_packet(p.enc, pkt)) >= 0) {
			av_packet_rescale_ts(pkt, p.enc->time_base, p.out->time_base);
			pkt->stream_index = p.out->index;
			ret = av_interleaved_write_frame(ofmt, pkt);
			if (ret < 0) {
				av_packet_free(&pkt);
				error = QObject::tr("Write failed: %1").arg(AvErr(ret));
				return false;
			}
		}
		av_packet_free(&pkt);
		return true;
	}

	/* Pop one frame from every track's FIFO in lockstep, apply gains / rebuild the
	 * master mix, and encode. If flushing, pads short FIFOs with silence. */
	bool mixStep(bool flushing, QString &error)
	{
		for (;;) {
			int avail = INT32_MAX, most = 0;
			for (auto &p : audio) {
				int sz = av_audio_fifo_size(p->fifo);
				avail = std::min(avail, sz);
				most = std::max(most, sz);
			}
			int n;
			if (avail >= frameSize)
				n = frameSize;
			else if (flushing && most > 0)
				n = std::min(most, frameSize);
			else
				return true;

			size_t T = audio.size();
			std::vector<AVFrame *> frames(T, nullptr);
			for (size_t t = 0; t < T; t++) {
				AVFrame *f = av_frame_alloc();
				f->nb_samples = n;
				f->format = AV_SAMPLE_FMT_FLTP;
				av_channel_layout_copy(&f->ch_layout, &audio[t]->enc->ch_layout);
				f->sample_rate = audio[t]->enc->sample_rate;
				av_frame_get_buffer(f, 0);
				for (int c = 0; c < channels; c++)
					std::fill_n((float *)f->data[c], n, 0.0f);
				av_audio_fifo_read(audio[t]->fifo, (void **)f->data, n); // short reads leave silence
				frames[t] = f;
			}

			for (size_t t = 0; t < T; t++) {
				if (t == 0 && rebuildMaster && T > 1)
					continue;
				float g = (float)audio[t]->gain;
				for (int c = 0; c < channels; c++) {
					float *d = (float *)frames[t]->data[c];
					for (int i = 0; i < n; i++)
						d[i] *= g;
				}
			}

			if (rebuildMaster && T > 1) {
				float g0 = (float)audio[0]->gain;
				for (int c = 0; c < channels; c++) {
					float *m = (float *)frames[0]->data[c];
					for (int i = 0; i < n; i++) {
						float sum = 0.0f;
						for (size_t t = 1; t < T; t++)
							sum += ((float *)frames[t]->data[c])[i];
						m[i] = std::clamp(sum * g0, -1.0f, 1.0f);
					}
				}
			}

			bool ok = true;
			for (size_t t = 0; t < T && ok; t++) {
				AudioPipe &p = *audio[t];
				for (int c = 0; c < channels; c++) {
					float *d = (float *)frames[t]->data[c];
					for (int i = 0; i < n; i++)
						d[i] = std::clamp(d[i], -1.0f, 1.0f);
				}
				frames[t]->pts = p.nextPts == AV_NOPTS_VALUE ? 0 : p.nextPts;
				p.nextPts = frames[t]->pts + n;
				ok = encodeFrame(p, frames[t], error);
			}
			for (AVFrame *f : frames)
				av_frame_free(&f);
			if (!ok)
				return false;
		}
	}

	bool decodeInto(AudioPipe &p, AVPacket *pkt, QString &error)
	{
		int ret = avcodec_send_packet(p.dec, pkt);
		if (ret < 0 && ret != AVERROR_EOF && ret != AVERROR(EAGAIN)) {
			// Corrupt packet at clip boundaries is common; skip it.
			return true;
		}
		AVFrame *f = av_frame_alloc();
		while ((ret = avcodec_receive_frame(p.dec, f)) >= 0) {
			if (p.nextPts == AV_NOPTS_VALUE) {
				int64_t ts = f->best_effort_timestamp;
				p.nextPts = ts == AV_NOPTS_VALUE ? 0 : av_rescale_q(ts, p.in->time_base, p.enc->time_base);
			}
			int outMax = swr_get_out_samples(p.swr, f->nb_samples);
			uint8_t **buf = nullptr;
			av_samples_alloc_array_and_samples(&buf, nullptr, channels, outMax, AV_SAMPLE_FMT_FLTP, 0);
			int got = swr_convert(p.swr, buf, outMax, (const uint8_t **)f->extended_data, f->nb_samples);
			if (got > 0)
				av_audio_fifo_write(p.fifo, (void **)buf, got);
			av_freep(&buf[0]);
			av_freep(&buf);
			av_frame_unref(f);
		}
		av_frame_free(&f);
		(void)error;
		return true;
	}
};

} // namespace

bool RemixClip(const QString &inPath, const QString &outPath, const RemixOptions &opts,
	       const std::function<void(double)> &progress, const std::function<bool()> &cancelled, QString &error)
{
	Remuxer r;
	int ret = avformat_open_input(&r.ifmt, inPath.toUtf8().constData(), nullptr, nullptr);
	if (ret < 0) {
		error = QObject::tr("Cannot open clip: %1").arg(AvErr(ret));
		return false;
	}
	avformat_find_stream_info(r.ifmt, nullptr);

	ret = avformat_alloc_output_context2(&r.ofmt, nullptr, nullptr, outPath.toUtf8().constData());
	if (ret < 0 || !r.ofmt) {
		error = QObject::tr("Cannot create output: %1").arg(AvErr(ret));
		return false;
	}

	const AVCodec *aac = avcodec_find_encoder(AV_CODEC_ID_AAC);
	if (!aac) {
		error = QObject::tr("No AAC encoder available.");
		return false;
	}

	r.streamMap.assign(r.ifmt->nb_streams, -1);
	r.audioOf.assign(r.ifmt->nb_streams, -1);

	AVChannelLayout targetLayout = {};
	int targetRate = 0;

	for (unsigned i = 0; i < r.ifmt->nb_streams; i++) {
		AVStream *in = r.ifmt->streams[i];
		AVMediaType type = in->codecpar->codec_type;

		if (type == AVMEDIA_TYPE_VIDEO) {
			if (in->disposition & AV_DISPOSITION_ATTACHED_PIC)
				continue;
			AVStream *out = avformat_new_stream(r.ofmt, nullptr);
			avcodec_parameters_copy(out->codecpar, in->codecpar);
			out->codecpar->codec_tag = 0;
			out->time_base = in->time_base;
			av_dict_copy(&out->metadata, in->metadata, 0);
			r.streamMap[i] = out->index;
			continue;
		}
		if (type != AVMEDIA_TYPE_AUDIO)
			continue;

		auto p = std::make_unique<AudioPipe>();
		p->in = in;
		size_t idx = r.audio.size();
		p->gain = idx < (size_t)opts.gains.size() ? opts.gains[(int)idx] : 1.0;

		const AVCodec *dc = avcodec_find_decoder(in->codecpar->codec_id);
		if (!dc) {
			error = QObject::tr("No decoder for audio track %1.").arg(idx + 1);
			return false;
		}
		p->dec = avcodec_alloc_context3(dc);
		avcodec_parameters_to_context(p->dec, in->codecpar);
		p->dec->pkt_timebase = in->time_base;
		if ((ret = avcodec_open2(p->dec, dc, nullptr)) < 0) {
			error = QObject::tr("Cannot open audio decoder: %1").arg(AvErr(ret));
			return false;
		}

		if (!targetRate) {
			targetRate = p->dec->sample_rate;
			av_channel_layout_copy(&targetLayout, &p->dec->ch_layout);
			if (!av_channel_layout_check(&targetLayout) || targetLayout.nb_channels == 0)
				targetLayout = AV_CHANNEL_LAYOUT_STEREO;
		}

		p->enc = avcodec_alloc_context3(aac);
		p->enc->sample_fmt = AV_SAMPLE_FMT_FLTP;
		p->enc->sample_rate = targetRate;
		av_channel_layout_copy(&p->enc->ch_layout, &targetLayout);
		p->enc->bit_rate = (int64_t)opts.audioBitrateKbps * 1000;
		p->enc->time_base = AVRational{1, targetRate};
		if (r.ofmt->oformat->flags & AVFMT_GLOBALHEADER)
			p->enc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
		if ((ret = avcodec_open2(p->enc, aac, nullptr)) < 0) {
			error = QObject::tr("Cannot open AAC encoder: %1").arg(AvErr(ret));
			return false;
		}
		if (p->enc->frame_size > 0)
			r.frameSize = p->enc->frame_size;

		ret = swr_alloc_set_opts2(&p->swr, &p->enc->ch_layout, AV_SAMPLE_FMT_FLTP, targetRate,
					  &p->dec->ch_layout, p->dec->sample_fmt, p->dec->sample_rate, 0, nullptr);
		if (ret < 0 || swr_init(p->swr) < 0) {
			error = QObject::tr("Cannot create resampler.");
			return false;
		}
		p->fifo = av_audio_fifo_alloc(AV_SAMPLE_FMT_FLTP, targetLayout.nb_channels, r.frameSize * 4);

		p->out = avformat_new_stream(r.ofmt, nullptr);
		avcodec_parameters_from_context(p->out->codecpar, p->enc);
		p->out->time_base = p->enc->time_base;
		av_dict_copy(&p->out->metadata, in->metadata, 0);
		if (AVDictionaryEntry *t = av_dict_get(in->metadata, "title", nullptr, 0))
			if (!av_dict_get(in->metadata, "handler_name", nullptr, 0))
				av_dict_set(&p->out->metadata, "handler_name", t->value, 0);
		p->out->disposition = idx == 0 ? AV_DISPOSITION_DEFAULT : 0;

		r.audioOf[i] = (int)idx;
		r.streamMap[i] = p->out->index;
		r.audio.push_back(std::move(p));
	}
	r.channels = targetLayout.nb_channels;
	r.rebuildMaster = opts.rebuildMaster;
	av_channel_layout_uninit(&targetLayout);

	if (r.audio.empty()) {
		error = QObject::tr("This clip has no audio tracks.");
		return false;
	}

	if (!(r.ofmt->oformat->flags & AVFMT_NOFILE)) {
		ret = avio_open(&r.ofmt->pb, outPath.toUtf8().constData(), AVIO_FLAG_WRITE);
		if (ret < 0) {
			error = QObject::tr("Cannot write output: %1").arg(AvErr(ret));
			return false;
		}
	}
	AVDictionary *muxOpts = nullptr;
	if (strcmp(r.ofmt->oformat->name, "mov") == 0 || strstr(r.ofmt->oformat->name, "mp4"))
		av_dict_set(&muxOpts, "movflags", "+faststart", 0);
	ret = avformat_write_header(r.ofmt, &muxOpts);
	av_dict_free(&muxOpts);
	if (ret < 0) {
		error = QObject::tr("Cannot write header: %1").arg(AvErr(ret));
		return false;
	}

	double duration = r.ifmt->duration > 0 ? (double)r.ifmt->duration / AV_TIME_BASE : 0.0;
	AVPacket *pkt = av_packet_alloc();
	bool ok = true;
	while (ok && av_read_frame(r.ifmt, pkt) >= 0) {
		int si = pkt->stream_index;
		if (cancelled && cancelled()) {
			error = QObject::tr("Cancelled.");
			ok = false;
		} else if (r.audioOf[si] >= 0) {
			ok = r.decodeInto(*r.audio[r.audioOf[si]], pkt, error) && r.mixStep(false, error);
		} else if (r.streamMap[si] >= 0) {
			AVStream *in = r.ifmt->streams[si];
			AVStream *out = r.ofmt->streams[r.streamMap[si]];
			if (progress && duration > 0 && pkt->pts != AV_NOPTS_VALUE)
				progress(std::clamp(pkt->pts * av_q2d(in->time_base) / duration, 0.0, 1.0));
			if (pkt->pts == AV_NOPTS_VALUE)
				pkt->pts = pkt->dts;
			av_packet_rescale_ts(pkt, in->time_base, out->time_base);
			pkt->stream_index = out->index;
			pkt->pos = -1;
			int wr = av_interleaved_write_frame(r.ofmt, pkt);
			if (wr < 0) {
				error = QObject::tr("Write failed: %1").arg(AvErr(wr));
				ok = false;
			}
		}
		av_packet_unref(pkt);
	}
	av_packet_free(&pkt);

	if (ok) {
		for (auto &p : r.audio)
			r.decodeInto(*p, nullptr, error); // drain decoders
		ok = r.mixStep(true, error);
	}
	if (ok) {
		for (auto &p : r.audio)
			if (!(ok = r.encodeFrame(*p, nullptr, error)))
				break;
	}
	if (ok) {
		ret = av_write_trailer(r.ofmt);
		if (ret < 0) {
			error = QObject::tr("Cannot finalize file: %1").arg(AvErr(ret));
			ok = false;
		}
	}
	if (ok && progress)
		progress(1.0);
	return ok;
}
