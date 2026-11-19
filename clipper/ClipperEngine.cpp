#include "ClipperEngine.hpp"
#include "HotkeyUtil.hpp"

#include <QDir>
#include <QGuiApplication>
#include <QScreen>
#include <QMetaObject>

#include <algorithm>
#include <cstring>
#include <initializer_list>

#include <util/platform.h>

#ifdef _WIN32
#define VIDEO_GAME_ID "game_capture"
#define VIDEO_DESKTOP_ID "monitor_capture"
#define AUDIO_DESKTOP_ID "wasapi_output_capture"
#define AUDIO_MIC_ID "wasapi_input_capture"
#define AUDIO_APP_ID "wasapi_process_output_capture"
#define GRAPHICS_MODULE DL_D3D11
#elif defined(__APPLE__)
#define VIDEO_GAME_ID "screen_capture"
#define VIDEO_DESKTOP_ID "screen_capture"
#define AUDIO_DESKTOP_ID "coreaudio_output_capture"
#define AUDIO_MIC_ID "coreaudio_input_capture"
#define AUDIO_APP_ID ""
#define GRAPHICS_MODULE DL_METAL
#else
#define VIDEO_GAME_ID "xshm_input"
#define VIDEO_DESKTOP_ID "xshm_input"
#define AUDIO_DESKTOP_ID "pulse_output_capture"
#define AUDIO_MIC_ID "pulse_input_capture"
#define AUDIO_APP_ID ""
#define GRAPHICS_MODULE DL_OPENGL
#endif

/* libobs' window_priority enum (util/windows/window-helpers.h): match by executable name
 * so Discord/Spotify/etc. keep being captured even when their window title changes. */
static constexpr int kWindowPriorityExe = 2;

static constexpr int kMixerAll = 0;

ClipperEngine::ClipperEngine(QObject *parent) : QObject(parent) {}

ClipperEngine::~ClipperEngine()
{
	Shutdown();
}

/* ------------------------------------------------------------------------ */
/* Startup / shutdown                                                       */

bool ClipperEngine::Startup(QString &error)
{
	if (started)
		return true;

	if (!obs_startup("en-US", nullptr, nullptr)) {
		error = tr("Failed to initialize libobs.");
		return false;
	}
	started = true;

	struct obs_audio_info ai = {};
	ai.samples_per_sec = 48000;
	ai.speakers = SPEAKERS_STEREO;
	if (!obs_reset_audio(&ai)) {
		error = tr("Failed to initialize audio.");
		return false;
	}

	s.load();
	if (!ResetVideo(error))
		return false;

	if (!obs_load_core_modules())
		blog(LOG_WARNING, "[ClipStudio] Some core modules failed to load");
	obs_post_load_modules();

	RegisterHotkeys();
	obs_hotkey_enable_background_press(true);
	return true;
}

void ClipperEngine::Shutdown()
{
	if (!started)
		return;
	shuttingDown = true;

	if (recordOutput && obs_output_active(recordOutput))
		obs_output_stop(recordOutput);
	ClearPipeline();

	if (clipHotkey != OBS_INVALID_HOTKEY_ID)
		obs_hotkey_unregister(clipHotkey);
	if (recordHotkey != OBS_INVALID_HOTKEY_ID)
		obs_hotkey_unregister(recordHotkey);
	if (showHotkey != OBS_INVALID_HOTKEY_ID)
		obs_hotkey_unregister(showHotkey);

	obs_shutdown();
	started = false;
}

bool ClipperEngine::ResetVideo(QString &error)
{
	QScreen *screen = QGuiApplication::primaryScreen();
	QSize size = screen ? screen->size() * screen->devicePixelRatio() : QSize(1920, 1080);

	struct obs_video_info ovi = {};
	ovi.graphics_module = GRAPHICS_MODULE;
	ovi.fps_num = (uint32_t)std::max(1, s.fps);
	ovi.fps_den = 1;
	/* Canvas == screen size: the capture is always stretched to fill it, so there is
	 * nothing to lay out and no canvas/preview is ever shown. */
	ovi.base_width = ovi.output_width = (uint32_t)(size.width() & ~1);
	ovi.base_height = ovi.output_height = (uint32_t)(size.height() & ~1);
	ovi.output_format = VIDEO_FORMAT_NV12;
	ovi.colorspace = VIDEO_CS_709;
	ovi.range = VIDEO_RANGE_PARTIAL;
	ovi.adapter = 0;
	ovi.gpu_conversion = true;
	ovi.scale_type = OBS_SCALE_BICUBIC;

	int ret = obs_reset_video(&ovi);
	if (ret != OBS_VIDEO_SUCCESS) {
		error = tr("Failed to initialize video (error %1). Your GPU/driver may not be supported.").arg(ret);
		return false;
	}
	canvasW = (int)ovi.base_width;
	canvasH = (int)ovi.base_height;
	canvasFps = s.fps;
	return true;
}

/* ------------------------------------------------------------------------ */
/* Hotkeys                                                                  */

void ClipperEngine::RegisterHotkeys()
{
	/* Hotkey callbacks run on libobs' hotkey thread (which also fires while a game
	 * has focus); bounce them to the Qt thread. */
	auto clipCb = [](void *data, obs_hotkey_id, obs_hotkey_t *, bool pressed) {
		if (pressed)
			QMetaObject::invokeMethod(static_cast<ClipperEngine *>(data), "SaveClip", Qt::QueuedConnection);
	};
	auto recCb = [](void *data, obs_hotkey_id, obs_hotkey_t *, bool pressed) {
		if (pressed)
			QMetaObject::invokeMethod(static_cast<ClipperEngine *>(data), "ToggleRecording",
						  Qt::QueuedConnection);
	};
	auto showCb = [](void *data, obs_hotkey_id, obs_hotkey_t *, bool pressed) {
		if (pressed)
			QMetaObject::invokeMethod(static_cast<ClipperEngine *>(data), "ShowWindowRequested",
						  Qt::QueuedConnection);
	};

	clipHotkey = obs_hotkey_register_frontend("ClipStudio.SaveClip", "Save Clip", clipCb, this);
	recordHotkey = obs_hotkey_register_frontend("ClipStudio.ToggleRecording", "Start/Stop Recording", recCb, this);
	showHotkey = obs_hotkey_register_frontend("ClipStudio.ShowWindow", "Show ClipStudio", showCb, this);
}

/* ------------------------------------------------------------------------ */
/* Pipeline                                                                 */

bool ClipperEngine::Apply(const AppSettings &settings, QString &error)
{
	if (IsRecording()) {
		error = tr("Stop the recording before changing settings.");
		return false;
	}

	s = settings;
	s.save();

	OBSDataArrayAutoRelease b1 = HotkeyStringToObsBindings(s.clipHotkey);
	OBSDataArrayAutoRelease b2 = HotkeyStringToObsBindings(s.recordHotkey);
	OBSDataArrayAutoRelease b3 = HotkeyStringToObsBindings(s.showHotkey);
	obs_hotkey_load(clipHotkey, b1);
	obs_hotkey_load(recordHotkey, b2);
	obs_hotkey_load(showHotkey, b3);

	ClearPipeline();

	if (canvasFps != s.fps && !ResetVideo(error))
		return false;

	if (!BuildSources()) {
		error = tr("Failed to create capture sources.");
		return false;
	}
	if (!BuildEncoders()) {
		error = tr("Failed to create encoders.");
		return false;
	}
	if (!BuildOutputs(error))
		return false;

	StartReplayBuffer();
	return true;
}

void ClipperEngine::ClearPipeline()
{
	replaySavedSignal.Disconnect();
	replayStopSignal.Disconnect();
	recordStopSignal.Disconnect();

	if (replayOutput && obs_output_active(replayOutput))
		obs_output_force_stop(replayOutput);
	if (recordOutput && obs_output_active(recordOutput))
		obs_output_force_stop(recordOutput);
	replayOutput = nullptr;
	recordOutput = nullptr;

	audioEncoders.clear();
	videoEncoder = nullptr;

	for (uint32_t ch = 0; ch < 16; ch++)
		obs_set_output_source(ch, nullptr);

	audioSources.clear();
	videoSource = nullptr;
	scene = nullptr;
}

static void SetMixers(obs_source_t *src, std::initializer_list<int> tracks)
{
	uint32_t mask = 0;
	for (int t : tracks)
		mask |= 1u << t;
	obs_source_set_audio_mixers(src, mask);
}

bool ClipperEngine::BuildSources()
{
	scene = obs_scene_create_private("ClipStudio");
	if (!scene)
		return false;

	/* ---- video ---- */
	OBSDataAutoRelease vs = obs_data_create();
	const char *videoId;
	if (s.captureMode == CaptureMode::Game) {
		videoId = VIDEO_GAME_ID;
		if (s.gameWindow.isEmpty()) {
			obs_data_set_string(vs, "capture_mode", "any_fullscreen");
		} else {
			obs_data_set_string(vs, "capture_mode", "window");
			obs_data_set_string(vs, "window", s.gameWindow.toUtf8().constData());
			obs_data_set_int(vs, "priority", kWindowPriorityExe);
		}
		obs_data_set_bool(vs, "capture_cursor", s.captureCursor);
		obs_data_set_bool(vs, "allow_transparency", false);
	} else {
		videoId = VIDEO_DESKTOP_ID;
		QString monitor = s.monitorId;
		if (monitor.isEmpty()) {
			NameValueList monitors = ListProperty(VIDEO_DESKTOP_ID, "monitor_id");
			if (!monitors.isEmpty())
				monitor = monitors.first().second;
		}
		obs_data_set_string(vs, "monitor_id", monitor.toUtf8().constData());
		obs_data_set_bool(vs, "capture_cursor", s.captureCursor);
	}

	videoSource = obs_source_create_private(videoId, "Capture", vs);
	if (!videoSource)
		return false;

	obs_sceneitem_t *item = obs_scene_add(scene, videoSource);
	struct obs_transform_info ti = {};
	vec2_set(&ti.pos, 0.0f, 0.0f);
	vec2_set(&ti.scale, 1.0f, 1.0f);
	ti.alignment = OBS_ALIGN_LEFT | OBS_ALIGN_TOP;
	ti.bounds_type = OBS_BOUNDS_STRETCH; // always stretch to fill the whole frame
	ti.bounds_alignment = OBS_ALIGN_CENTER;
	vec2_set(&ti.bounds, (float)canvasW, (float)canvasH);
	ti.crop_to_bounds = false;
	obs_sceneitem_set_info2(item, &ti);

	obs_set_output_source(0, obs_scene_get_source(scene));

	/* ---- audio ---- */
	OBSDataAutoRelease ds = obs_data_create();
	obs_data_set_string(ds, "device_id", s.desktopDevice.toUtf8().constData());
	OBSSourceAutoRelease desktop = obs_source_create_private(AUDIO_DESKTOP_ID, "Desktop Audio", ds);

	OBSDataAutoRelease ms = obs_data_create();
	obs_data_set_string(ms, "device_id", s.micDevice.toUtf8().constData());
	OBSSourceAutoRelease mic = obs_source_create_private(AUDIO_MIC_ID, "Mic", ms);

	/* Track layout (mixer index = track - 1):
	 *   Simple : 1 = Mic + Desktop, 2 = Desktop, 3 = Mic
	 *   Per-app: 1 = Mic + Desktop, 2 = Mic, 3..6 = one app each */
	if (s.audioMode == AudioMode::Simple) {
		if (desktop)
			SetMixers(desktop, {kMixerAll, 1});
		if (mic)
			SetMixers(mic, {kMixerAll, 2});
	} else {
		if (desktop)
			SetMixers(desktop, {kMixerAll});
		if (mic)
			SetMixers(mic, {kMixerAll, 1});
	}

	uint32_t channel = 1;
	if (desktop) {
		obs_set_output_source(channel++, desktop);
		audioSources.append(OBSSource(desktop.Get()));
	}
	if (mic) {
		obs_set_output_source(channel++, mic);
		audioSources.append(OBSSource(mic.Get()));
	}

	if (s.audioMode == AudioMode::PerApp && *AUDIO_APP_ID) {
		for (int i = 0; i < s.appTracks.size() && i < kMaxAppTracks; i++) {
			const AppAudioTrack &app = s.appTracks[i];
			if (app.windowId.isEmpty())
				continue;
			OBSDataAutoRelease as = obs_data_create();
			obs_data_set_string(as, "window", app.windowId.toUtf8().constData());
			obs_data_set_int(as, "priority", kWindowPriorityExe);
			OBSSourceAutoRelease src =
				obs_source_create_private(AUDIO_APP_ID, app.name.toUtf8().constData(), as);
			if (!src)
				continue;
			SetMixers(src, {2 + i}); // tracks 3..6
			obs_set_output_source(channel++, src);
			audioSources.append(OBSSource(src.Get()));
		}
	}
	return true;
}

static bool EncoderAvailable(const char *id)
{
	const char *t;
	for (size_t i = 0; obs_enum_encoder_types(i, &t); i++)
		if (strcmp(t, id) == 0)
			return true;
	return false;
}

bool ClipperEngine::BuildEncoders()
{
	QString vid = s.encoderId;
	if (vid.isEmpty() || !EncoderAvailable(vid.toUtf8().constData())) {
		static const char *preferred[] = {"obs_nvenc_h264_tex", "jim_nvenc", "h264_texture_amf", "obs_qsv11_v2",
						  "obs_x264"};
		vid.clear();
		for (const char *id : preferred) {
			if (EncoderAvailable(id)) {
				vid = id;
				break;
			}
		}
	}
	if (vid.isEmpty())
		return false;

	OBSDataAutoRelease ves = obs_data_create();
	obs_data_set_string(ves, "rate_control", "CBR");
	obs_data_set_int(ves, "bitrate", s.videoBitrate);
	obs_data_set_int(ves, "keyint_sec", 2);
	videoEncoder = obs_video_encoder_create(vid.toUtf8().constData(), "ClipStudio Video", ves, nullptr);
	if (!videoEncoder)
		return false;
	obs_encoder_set_video(videoEncoder, obs_get_video());

	QStringList trackNames;
	if (s.audioMode == AudioMode::Simple) {
		trackNames = {"Mic + Desktop", "Desktop", "Mic"};
	} else {
		trackNames = {"Mic + Desktop", "Mic"};
		for (int i = 0; i < s.appTracks.size() && i < kMaxAppTracks; i++)
			trackNames << (s.appTracks[i].name.isEmpty() ? QString("App %1").arg(i + 1) : s.appTracks[i].name);
	}

	const char *aacId = EncoderAvailable("ffmpeg_aac") ? "ffmpeg_aac" : "CoreAudio_AAC";
	for (int t = 0; t < trackNames.size() && t < MAX_AUDIO_MIXES; t++) {
		OBSDataAutoRelease aes = obs_data_create();
		obs_data_set_int(aes, "bitrate", s.audioBitrate);
		/* The encoder name becomes the audio track title in the file, so video
		 * editors show "Discord", "Spotify", ... */
		OBSEncoderAutoRelease enc =
			obs_audio_encoder_create(aacId, trackNames[t].toUtf8().constData(), aes, (size_t)t, nullptr);
		if (!enc)
			return false;
		obs_encoder_set_audio(enc, obs_get_audio());
		audioEncoders.append(OBSEncoder(enc.Get()));
	}
	return true;
}

bool ClipperEngine::BuildOutputs(QString &error)
{
	QDir().mkpath(s.outputDir);
	QByteArray dir = QDir::toNativeSeparators(s.outputDir).toUtf8();
	QByteArray ext = s.container.toUtf8();

	int tracks = (int)audioEncoders.size();
	int64_t kbps = s.videoBitrate + (int64_t)s.audioBitrate * tracks;
	int64_t maxMb = kbps * s.clipSeconds / 8 / 1024 * 3 / 2 + 64;

	OBSDataAutoRelease rs = obs_data_create();
	obs_data_set_string(rs, "directory", dir.constData());
	obs_data_set_string(rs, "format", "Clip %CCYY-%MM-%DD %hh-%mm-%ss");
	obs_data_set_string(rs, "extension", ext.constData());
	obs_data_set_bool(rs, "allow_spaces", true);
	obs_data_set_int(rs, "max_time_sec", s.clipSeconds);
	obs_data_set_int(rs, "max_size_mb", maxMb);
	replayOutput = obs_output_create("replay_buffer", "ClipStudio Replay", rs, nullptr);
	if (!replayOutput) {
		error = tr("Failed to create the replay buffer.");
		return false;
	}

	recordOutput = obs_output_create("ffmpeg_muxer", "ClipStudio Recording", nullptr, nullptr);
	if (!recordOutput) {
		error = tr("Failed to create the recording output.");
		return false;
	}

	/* Both outputs share the same encoders, so recording while the clip buffer
	 * runs costs no extra encoding. */
	for (obs_output_t *out : {replayOutput.Get(), recordOutput.Get()}) {
		obs_output_set_video_encoder(out, videoEncoder);
		for (int t = 0; t < tracks; t++)
			obs_output_set_audio_encoder(out, audioEncoders[t], (size_t)t);
	}

	signal_handler_t *rsh = obs_output_get_signal_handler(replayOutput);
	replaySavedSignal.Connect(rsh, "saved", OnReplaySaved, this);
	replayStopSignal.Connect(rsh, "stop", OnReplayStop, this);
	recordStopSignal.Connect(obs_output_get_signal_handler(recordOutput), "stop", OnRecordStop, this);
	return true;
}

void ClipperEngine::StartReplayBuffer()
{
	if (!replayOutput)
		return;
	if (!obs_output_start(replayOutput)) {
		const char *err = obs_output_get_last_error(replayOutput);
		emit ErrorOccurred(tr("Could not start clip buffer: %1").arg(err ? err : "unknown error"));
		emit ReplayStateChanged(false);
		return;
	}
	emit ReplayStateChanged(true);
}

/* ------------------------------------------------------------------------ */
/* Actions                                                                  */

bool ClipperEngine::IsRecording() const
{
	return recordOutput && obs_output_active(recordOutput);
}

bool ClipperEngine::IsReplayActive() const
{
	return replayOutput && obs_output_active(replayOutput);
}

void ClipperEngine::SaveClip()
{
	if (!IsReplayActive()) {
		emit ErrorOccurred(tr("Clip buffer is not running."));
		return;
	}
	calldata_t cd = {0};
	proc_handler_call(obs_output_get_proc_handler(replayOutput), "save", &cd);
	calldata_free(&cd);
}

void ClipperEngine::ToggleRecording()
{
	if (!recordOutput)
		return;

	if (IsRecording()) {
		obs_output_stop(recordOutput);
		return;
	}

	QDir().mkpath(s.outputDir);
	char *name = os_generate_formatted_filename(s.container.toUtf8().constData(), true,
						    "Recording %CCYY-%MM-%DD %hh-%mm-%ss");
	currentRecordingPath = QDir(s.outputDir).filePath(QString::fromUtf8(name));
	bfree(name);

	OBSDataAutoRelease os = obs_data_create();
	obs_data_set_string(os, "path", QDir::toNativeSeparators(currentRecordingPath).toUtf8().constData());
	obs_output_update(recordOutput, os);

	if (!obs_output_start(recordOutput)) {
		const char *err = obs_output_get_last_error(recordOutput);
		emit ErrorOccurred(tr("Could not start recording: %1").arg(err ? err : "unknown error"));
		return;
	}
	emit RecordingStarted(currentRecordingPath);
}

/* Signal handlers run on libobs threads; bounce everything to the Qt thread. */

void ClipperEngine::OnReplaySaved(void *data, calldata_t *)
{
	auto *self = static_cast<ClipperEngine *>(data);
	calldata_t cd = {0};
	proc_handler_call(obs_output_get_proc_handler(self->replayOutput), "get_last_replay", &cd);
	QString path = QString::fromUtf8(calldata_string(&cd, "path"));
	calldata_free(&cd);
	QMetaObject::invokeMethod(self, [self, path]() { emit self->ClipSaved(path); }, Qt::QueuedConnection);
}

void ClipperEngine::OnReplayStop(void *data, calldata_t *cd)
{
	auto *self = static_cast<ClipperEngine *>(data);
	int code = (int)calldata_int(cd, "code");
	QMetaObject::invokeMethod(
		self,
		[self, code]() {
			if (self->shuttingDown)
				return;
			emit self->ReplayStateChanged(false);
			if (code != OBS_OUTPUT_SUCCESS)
				emit self->ErrorOccurred(tr("Clip buffer stopped unexpectedly (code %1).").arg(code));
		},
		Qt::QueuedConnection);
}

void ClipperEngine::OnRecordStop(void *data, calldata_t *cd)
{
	auto *self = static_cast<ClipperEngine *>(data);
	int code = (int)calldata_int(cd, "code");
	QMetaObject::invokeMethod(
		self,
		[self, code]() {
			emit self->RecordingStopped(self->currentRecordingPath);
			if (code != OBS_OUTPUT_SUCCESS && !self->shuttingDown)
				emit self->ErrorOccurred(tr("Recording stopped with an error (code %1).").arg(code));
		},
		Qt::QueuedConnection);
}

/* ------------------------------------------------------------------------ */
/* UI helpers                                                               */

NameValueList ClipperEngine::ListProperty(const char *sourceId, const char *propName)
{
	NameValueList list;
	if (!sourceId || !*sourceId)
		return list;
	obs_properties_t *props = obs_get_source_properties(sourceId);
	if (!props)
		return list;
	obs_property_t *p = obs_properties_get(props, propName);
	if (p && obs_property_get_type(p) == OBS_PROPERTY_LIST &&
	    obs_property_list_format(p) == OBS_COMBO_FORMAT_STRING) {
		size_t n = obs_property_list_item_count(p);
		for (size_t i = 0; i < n; i++) {
			const char *name = obs_property_list_item_name(p, i);
			const char *val = obs_property_list_item_string(p, i);
			if (val && *val)
				list.append({QString::fromUtf8(name), QString::fromUtf8(val)});
		}
	}
	obs_properties_destroy(props);
	return list;
}

NameValueList ClipperEngine::ListEncoders()
{
	NameValueList list;
	const char *id;
	for (size_t i = 0; obs_enum_encoder_types(i, &id); i++) {
		if (obs_get_encoder_type(id) != OBS_ENCODER_VIDEO)
			continue;
		uint32_t caps = obs_get_encoder_caps(id);
		if (caps & (OBS_ENCODER_CAP_DEPRECATED | OBS_ENCODER_CAP_INTERNAL))
			continue;
		const char *codec = obs_get_encoder_codec(id);
		if (!codec || strcmp(codec, "h264") != 0) // H.264 imports into every editor
			continue;
		list.append({QString::fromUtf8(obs_encoder_get_display_name(id)), QString::fromUtf8(id)});
	}
	return list;
}
