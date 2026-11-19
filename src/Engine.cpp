#include "Engine.hpp"

#include "AudioCapture.hpp"
#include "Muxer.hpp"
#include "VideoCapture.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMetaObject>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <cstring>

#pragma comment(lib, "winmm.lib")

static int64_t Qpc()
{
	LARGE_INTEGER v;
	QueryPerformanceCounter(&v);
	return v.QuadPart;
}

static void SleepUntil(int64_t target, int64_t freq)
{
	for (;;) {
		int64_t now = Qpc();
		if (now >= target)
			return;
		int64_t ms = (target - now) * 1000 / freq;
		Sleep(ms > 2 ? (DWORD)(ms - 1) : 0);
	}
}

Engine::Engine(QObject *parent) : QObject(parent)
{
	LARGE_INTEGER f;
	QueryPerformanceFrequency(&f);
	qpcFreq = f.QuadPart;
	timeBeginPeriod(1);
}

Engine::~Engine()
{
	Shutdown();
	timeEndPeriod(1);
}

/* ------------------------------------------------------------------------ */
/* Encoders                                                                 */

static const char *kEncoderOrder[] = {"h264_nvenc", "h264_amf", "h264_qsv", "libx264"};

static const char *EncoderDisplayName(const char *name)
{
	if (!strcmp(name, "h264_nvenc"))
		return "NVIDIA NVENC (H.264)";
	if (!strcmp(name, "h264_amf"))
		return "AMD AMF (H.264)";
	if (!strcmp(name, "h264_qsv"))
		return "Intel Quick Sync (H.264)";
	return "CPU x264 (H.264)";
}

static AVCodecContext *CreateVideoEncoder(const char *name, int w, int h, int fps, int kbps)
{
	const AVCodec *codec = avcodec_find_encoder_by_name(name);
	if (!codec)
		return nullptr;
	AVCodecContext *c = avcodec_alloc_context3(codec);
	c->width = w;
	c->height = h;
	c->time_base = AVRational{1, fps};
	c->framerate = AVRational{fps, 1};
	c->pix_fmt = AV_PIX_FMT_NV12;
	c->bit_rate = (int64_t)kbps * 1000;
	c->rc_max_rate = c->bit_rate;
	c->rc_buffer_size = (int)std::min<int64_t>(c->bit_rate, INT32_MAX);
	c->gop_size = fps * 2;
	c->max_b_frames = 0; // keeps clip cutting simple (pts == dts)
	c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
	c->color_range = AVCOL_RANGE_MPEG;
	c->colorspace = AVCOL_SPC_BT709;
	c->color_primaries = AVCOL_PRI_BT709;
	c->color_trc = AVCOL_TRC_BT709;

	AVDictionary *o = nullptr;
	if (!strcmp(name, "h264_nvenc")) {
		av_dict_set(&o, "preset", "p5", 0);
		av_dict_set(&o, "rc", "cbr", 0);
	} else if (!strcmp(name, "h264_amf")) {
		av_dict_set(&o, "rc", "cbr", 0);
		av_dict_set(&o, "quality", "balanced", 0);
	} else if (!strcmp(name, "h264_qsv")) {
		av_dict_set(&o, "preset", "medium", 0);
	} else {
		av_dict_set(&o, "preset", "veryfast", 0);
	}
	int ret = avcodec_open2(c, codec, &o);
	av_dict_free(&o);
	if (ret < 0) {
		avcodec_free_context(&c);
		return nullptr;
	}
	return c;
}

NameValueList Engine::AvailableEncoders()
{
	static NameValueList cached;
	static bool done = false;
	if (!done) {
		av_log_set_level(AV_LOG_QUIET);
		for (const char *name : kEncoderOrder) {
			AVCodecContext *c = CreateVideoEncoder(name, 1280, 720, 30, 2000);
			if (c) {
				cached.append({QString::fromUtf8(EncoderDisplayName(name)), QString::fromUtf8(name)});
				avcodec_free_context(&c);
			}
		}
		done = true;
	}
	return cached;
}

bool Engine::OpenEncoders(QString &error)
{
	if (!s.encoder.isEmpty())
		venc = CreateVideoEncoder(s.encoder.toUtf8().constData(), outW, outH, s.fps, s.videoBitrate);
	for (const char *name : kEncoderOrder) {
		if (venc)
			break;
		venc = CreateVideoEncoder(name, outW, outH, s.fps, s.videoBitrate);
	}
	if (!venc) {
		error = tr("No working video encoder found.");
		return false;
	}

	const AVCodec *aac = avcodec_find_encoder(AV_CODEC_ID_AAC);
	for (int t = 0; t < trackNames.size(); t++) {
		AVCodecContext *c = avcodec_alloc_context3(aac);
		c->sample_fmt = AV_SAMPLE_FMT_FLTP;
		c->sample_rate = AudioCapture::kRate;
		AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
		av_channel_layout_copy(&c->ch_layout, &stereo);
		c->bit_rate = (int64_t)s.audioBitrate * 1000;
		c->time_base = AVRational{1, AudioCapture::kRate};
		c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
		if (avcodec_open2(c, aac, nullptr) < 0) {
			avcodec_free_context(&c);
			error = tr("Could not open the AAC audio encoder.");
			return false;
		}
		aenc.push_back(c);
	}
	return true;
}

void Engine::CloseEncoders()
{
	avcodec_free_context(&venc);
	for (AVCodecContext *&c : aenc)
		avcodec_free_context(&c);
	aenc.clear();
}

/* ------------------------------------------------------------------------ */
/* Lifecycle                                                                */

bool Engine::Apply(const AppSettings &settings, QString &error)
{
	if (recording) {
		error = tr("Stop the recording before changing settings.");
		return false;
	}
	StopThreads();

	s = settings;
	s.save();
	QDir().mkpath(s.outputDir);

	HMONITOR mon = WinUtil::MonitorById(s.captureMode == CaptureMode::Desktop ? s.monitorId : QString());
	QSize size = WinUtil::MonitorSize(mon);
	outW = std::max(2, size.width() & ~1);
	outH = std::max(2, size.height() & ~1);

	/* Audio sources and track layout. */
	sources.clear();
	trackSources.clear();
	trackNames.clear();
	sources.push_back(std::make_unique<AudioCapture>(AudioCapture::Kind::Loopback, s.desktopDevice.toStdWString()));
	sources.push_back(std::make_unique<AudioCapture>(AudioCapture::Kind::Input, s.micDevice.toStdWString()));
	const int desktop = 0, mic = 1;

	if (s.audioMode == AudioMode::Simple) {
		trackNames = {"Mic + Desktop", "Desktop", "Mic"};
		trackSources = {{desktop, mic}, {desktop}, {mic}};
	} else {
		trackNames = {"Mic + Desktop", "Mic"};
		trackSources = {{desktop, mic}, {mic}};
		for (int i = 0; i < s.appTracks.size() && i < kMaxAppTracks; i++) {
			const AppAudioTrack &app = s.appTracks[i];
			if (app.exe.isEmpty())
				continue;
			sources.push_back(
				std::make_unique<AudioCapture>(AudioCapture::Kind::Application, app.exe.toStdWString()));
			trackNames << (app.name.isEmpty() ? app.exe : app.name);
			trackSources.push_back({(int)sources.size() - 1});
		}
	}

	if (!OpenEncoders(error)) {
		CloseEncoders();
		return false;
	}

	for (auto &src : sources)
		src->Start();

	running = true;
	startQpc = Qpc();
	videoThread = std::thread([this]() { VideoThread(); });
	audioThread = std::thread([this]() { AudioThread(); });
	emit BufferStateChanged(true);
	return true;
}

void Engine::StopThreads()
{
	if (recording)
		ToggleRecording();
	running = false;
	if (videoThread.joinable())
		videoThread.join();
	if (audioThread.joinable())
		audioThread.join();
	for (auto &src : sources)
		src->Stop();
	sources.clear();

	{
		std::lock_guard<std::mutex> lock(storeMutex);
		for (Stored &st : store)
			av_packet_free(&st.pkt);
		store.clear();
	}
	CloseEncoders();
}

void Engine::Shutdown()
{
	StopThreads();
	emit BufferStateChanged(false);
}

/* ------------------------------------------------------------------------ */
/* Packets: clip buffer + recording                                         */

void Engine::OnPacket(int stream, AVPacket *pkt, AVRational tb)
{
	int64_t us = av_rescale_q(pkt->pts, tb, AV_TIME_BASE_Q);
	bool key = (pkt->flags & AV_PKT_FLAG_KEY) != 0;

	{
		std::lock_guard<std::mutex> lock(recMutex);
		if (recMuxer) {
			if (recWaitingForKey && stream == 0 && key) {
				recWaitingForKey = false;
				recStartUs = us;
			}
			if (!recWaitingForKey && us >= recStartUs)
				recMuxer->Write(stream, pkt, recStartUs);
		}
	}

	std::lock_guard<std::mutex> lock(storeMutex);
	store.push_back({stream, av_packet_clone(pkt), us, key});
	// Keep a little more than the clip length so we can always start on a keyframe.
	int64_t keepUs = (int64_t)(s.clipSeconds + 4) * 1000000;
	while (!store.empty() && store.front().us < us - keepUs) {
		av_packet_free(&store.front().pkt);
		store.pop_front();
	}
}

QString Engine::NewFilePath(const QString &prefix) const
{
	QString base = QString("%1 %2").arg(prefix, QDateTime::currentDateTime().toString("yyyy-MM-dd hh-mm-ss"));
	QDir dir(s.outputDir);
	QString path = dir.filePath(base + "." + s.container);
	for (int i = 2; QFile::exists(path); i++)
		path = dir.filePath(QString("%1 (%2).%3").arg(base).arg(i).arg(s.container));
	return path;
}

void Engine::SaveClip()
{
	if (!running) {
		emit ErrorOccurred(tr("The clip buffer is not running."));
		return;
	}

	std::vector<Stored> copy;
	int64_t startUs = 0;
	{
		std::lock_guard<std::mutex> lock(storeMutex);
		int64_t newest = INT64_MIN;
		for (const Stored &st : store)
			if (st.stream == 0)
				newest = std::max(newest, st.us);
		if (newest == INT64_MIN) {
			emit ErrorOccurred(tr("Nothing captured yet."));
			return;
		}
		int64_t cutoff = newest - (int64_t)s.clipSeconds * 1000000;
		bool found = false;
		for (const Stored &st : store) {
			if (st.stream == 0 && st.key && st.us >= cutoff) {
				startUs = st.us;
				found = true;
				break;
			}
		}
		if (!found) {
			for (const Stored &st : store)
				if (st.stream == 0 && st.key) {
					startUs = st.us;
					found = true;
					break;
				}
		}
		if (!found) {
			emit ErrorOccurred(tr("Nothing captured yet."));
			return;
		}
		for (const Stored &st : store)
			if (st.us >= startUs)
				copy.push_back({st.stream, av_packet_clone(st.pkt), st.us, st.key});
	}

	QString path = NewFilePath("Clip");
	Muxer mux;
	QString err;
	bool ok = mux.Open(path, venc, aenc, trackNames, true, err);
	if (ok) {
		for (Stored &st : copy)
			mux.Write(st.stream, st.pkt, startUs);
		ok = mux.Close();
	}
	for (Stored &st : copy)
		av_packet_free(&st.pkt);

	if (ok)
		emit ClipSaved(path);
	else
		emit ErrorOccurred(err.isEmpty() ? tr("Could not save the clip.") : err);
}

void Engine::ToggleRecording()
{
	if (recording) {
		std::unique_ptr<Muxer> mux;
		{
			std::lock_guard<std::mutex> lock(recMutex);
			mux = std::move(recMuxer);
		}
		recording = false;
		bool ok = mux && mux->Close();
		emit RecordingStopped(recPath);
		if (!ok)
			emit ErrorOccurred(tr("The recording may not have been finalized correctly."));
		return;
	}

	if (!running) {
		emit ErrorOccurred(tr("Capture is not running."));
		return;
	}

	auto mux = std::make_unique<Muxer>();
	QString path = NewFilePath("Recording");
	QString err;
	if (!mux->Open(path, venc, aenc, trackNames, false, err)) {
		emit ErrorOccurred(err);
		return;
	}
	{
		std::lock_guard<std::mutex> lock(recMutex);
		recMuxer = std::move(mux);
		recWaitingForKey = true;
	}
	recPath = path;
	recording = true;
	forceKeyframe = true;
	emit RecordingStarted(path);
}

/* ------------------------------------------------------------------------ */
/* Video                                                                    */

void Engine::VideoThread()
{
	CoInitializeEx(nullptr, COINIT_MULTITHREADED); // WinRT (Graphics Capture) needs an apartment

	VideoCapture capture;
	VideoFrame frame;
	SwsContext *sws = nullptr;

	AVFrame *out = av_frame_alloc();
	out->format = AV_PIX_FMT_NV12;
	out->width = outW;
	out->height = outH;
	av_frame_get_buffer(out, 32);
	// Start black.
	memset(out->data[0], 16, (size_t)out->linesize[0] * outH);
	memset(out->data[1], 128, (size_t)out->linesize[1] * (outH / 2));

	AVPacket *pkt = av_packet_alloc();
	const int64_t period = qpcFreq / s.fps;
	int64_t frameIndex = 0;
	int64_t nextTargetCheck = 0;

	auto retarget = [&]() {
		if (s.captureMode == CaptureMode::Desktop) {
			if (!capture.Active())
				capture.StartMonitor(WinUtil::MonitorById(s.monitorId), s.captureCursor);
			return;
		}
		HWND want = s.gameExe.isEmpty() ? WinUtil::FindFullscreenWindow() : WinUtil::FindWindowForExe(s.gameExe);
		if (want && (want != capture.Window() || !capture.Active())) {
			if (capture.StartWindow(want, s.captureCursor))
				frame.serial = 0;
		}
	};

	while (running) {
		int64_t target = startQpc + frameIndex * period;
		SleepUntil(target, qpcFreq);
		int64_t now = Qpc();

		if (now >= nextTargetCheck) {
			retarget();
			nextTargetCheck = now + qpcFreq * 2;
		}

		if (capture.GetLatest(frame) && frame.width > 0 && frame.height > 0) {
			sws = sws_getCachedContext(sws, frame.width, frame.height, AV_PIX_FMT_BGRA, outW, outH,
						   AV_PIX_FMT_NV12, SWS_BILINEAR, nullptr, nullptr, nullptr);
			if (sws && av_frame_make_writable(out) >= 0) {
				const uint8_t *src[1] = {frame.bgra.data()};
				int srcStride[1] = {frame.width * 4};
				// Always stretched to fill the whole output (no canvas, no black bars).
				sws_scale(sws, src, srcStride, 0, frame.height, out->data, out->linesize);
			}
		} else if (!capture.Active() && frame.serial != 0) {
			// Target closed: go black until a new one shows up.
			if (av_frame_make_writable(out) >= 0) {
				memset(out->data[0], 16, (size_t)out->linesize[0] * outH);
				memset(out->data[1], 128, (size_t)out->linesize[1] * (outH / 2));
			}
			frame.serial = 0;
		}

		out->pts = frameIndex;
		if (forceKeyframe.exchange(false)) {
			out->pict_type = AV_PICTURE_TYPE_I;
			out->flags |= AV_FRAME_FLAG_KEY;
		} else {
			out->pict_type = AV_PICTURE_TYPE_NONE;
			out->flags &= ~AV_FRAME_FLAG_KEY;
		}

		if (avcodec_send_frame(venc, out) >= 0) {
			while (avcodec_receive_packet(venc, pkt) >= 0) {
				OnPacket(0, pkt, venc->time_base);
				av_packet_unref(pkt);
			}
		}

		// If we fell behind (system hitch), skip ahead instead of bursting frames.
		int64_t elapsedFrames = (Qpc() - startQpc) / period;
		frameIndex = std::max(frameIndex + 1, elapsedFrames);
	}

	capture.Stop();
	av_packet_free(&pkt);
	av_frame_free(&out);
	sws_freeContext(sws);
	CoUninitialize();
}

/* ------------------------------------------------------------------------ */
/* Audio                                                                    */

void Engine::AudioThread()
{
	const int frameSize = aenc.empty() ? 1024 : (aenc[0]->frame_size > 0 ? aenc[0]->frame_size : 1024);
	const size_t nSources = sources.size();
	std::vector<std::vector<float>> in(nSources, std::vector<float>((size_t)frameSize * 2));

	std::vector<AVFrame *> frames;
	for (size_t t = 0; t < aenc.size(); t++) {
		AVFrame *f = av_frame_alloc();
		f->format = AV_SAMPLE_FMT_FLTP;
		f->sample_rate = AudioCapture::kRate;
		AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
		av_channel_layout_copy(&f->ch_layout, &stereo);
		f->nb_samples = frameSize;
		av_frame_get_buffer(f, 0);
		frames.push_back(f);
	}

	AVPacket *pkt = av_packet_alloc();
	int64_t samples = 0;
	const size_t maxLatency = AudioCapture::kRate / 5; // 200 ms

	while (running) {
		int64_t target = startQpc + (samples + frameSize) * qpcFreq / AudioCapture::kRate;
		SleepUntil(target, qpcFreq);

		for (size_t i = 0; i < nSources; i++) {
			// Keep every source close to real time so tracks stay in sync with video.
			if (sources[i]->Available() > maxLatency + (size_t)frameSize)
				sources[i]->DropTo(maxLatency);
			size_t got = sources[i]->Read(in[i].data(), (size_t)frameSize);
			std::fill(in[i].begin() + (ptrdiff_t)(got * 2), in[i].end(), 0.0f);
		}

		for (size_t t = 0; t < aenc.size(); t++) {
			AVFrame *f = frames[t];
			if (av_frame_make_writable(f) < 0)
				continue;
			float *l = (float *)f->data[0];
			float *r = (float *)f->data[1];
			for (int n = 0; n < frameSize; n++) {
				float sl = 0.0f, sr = 0.0f;
				for (int src : trackSources[t]) {
					sl += in[(size_t)src][(size_t)n * 2];
					sr += in[(size_t)src][(size_t)n * 2 + 1];
				}
				l[n] = std::clamp(sl, -1.0f, 1.0f);
				r[n] = std::clamp(sr, -1.0f, 1.0f);
			}
			f->pts = samples;
			if (avcodec_send_frame(aenc[t], f) >= 0) {
				while (avcodec_receive_packet(aenc[t], pkt) >= 0) {
					OnPacket((int)t + 1, pkt, aenc[t]->time_base);
					av_packet_unref(pkt);
				}
			}
		}
		samples += frameSize;
	}

	for (AVFrame *&f : frames)
		av_frame_free(&f);
	av_packet_free(&pkt);
}
