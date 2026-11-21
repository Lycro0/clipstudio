#include "AudioCapture.hpp"

#include <windows.h>
#include <unknwn.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <audioclientactivationparams.h>
#include <ksmedia.h>
#include <tlhelp32.h>

#include <wrl/client.h>

#include <algorithm>
#include <cstring>
#include <cwctype>

#ifndef VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK
#define VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK L"VAD\\Process_Loopback"
#endif

static constexpr size_t kRingFrames = AudioCapture::kRate * 3; // 3 s

AudioCapture::AudioCapture(Kind k, std::wstring t) : kind(k), target(std::move(t))
{
	ring.resize(kRingFrames * kChannels);
}

AudioCapture::~AudioCapture()
{
	Stop();
}

void AudioCapture::Start()
{
	if (running)
		return;
	running = true;
	thread = std::thread([this]() { Run(); });
}

void AudioCapture::Stop()
{
	running = false;
	if (thread.joinable())
		thread.join();
}

void AudioCapture::SetTarget(const std::wstring &exe)
{
	std::lock_guard<std::mutex> lock(targetMutex);
	if (target == exe)
		return;
	target = exe;
	targetChanged = true;
}

std::wstring AudioCapture::Target()
{
	std::lock_guard<std::mutex> lock(targetMutex);
	return target;
}

void AudioCapture::Push(const float *src, size_t frames)
{
	std::lock_guard<std::mutex> lock(m);
	size_t cap = ring.size();
	size_t n = frames * kChannels;
	for (size_t i = 0; i < n; i++) {
		size_t pos = (head + size) % cap;
		ring[pos] = src ? src[i] : 0.0f;
		if (size < cap)
			size++;
		else
			head = (head + 1) % cap; // overflow: drop oldest
	}
}

size_t AudioCapture::Read(float *dst, size_t frames)
{
	std::lock_guard<std::mutex> lock(m);
	size_t cap = ring.size();
	size_t n = std::min(frames * kChannels, size);
	n -= n % kChannels;
	for (size_t i = 0; i < n; i++)
		dst[i] = ring[(head + i) % cap];
	head = (head + n) % cap;
	size -= n;
	return n / kChannels;
}

size_t AudioCapture::Available()
{
	std::lock_guard<std::mutex> lock(m);
	return size / kChannels;
}

void AudioCapture::DropTo(size_t frames)
{
	std::lock_guard<std::mutex> lock(m);
	size_t keep = frames * kChannels;
	if (size > keep) {
		size_t drop = size - keep;
		drop -= drop % kChannels;
		head = (head + drop) % ring.size();
		size -= drop;
	}
}

/* ------------------------------------------------------------------------ */

static WAVEFORMATEXTENSIBLE FloatFormat()
{
	WAVEFORMATEXTENSIBLE wf = {};
	wf.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
	wf.Format.nChannels = AudioCapture::kChannels;
	wf.Format.nSamplesPerSec = AudioCapture::kRate;
	wf.Format.wBitsPerSample = 32;
	wf.Format.nBlockAlign = wf.Format.nChannels * 4;
	wf.Format.nAvgBytesPerSec = wf.Format.nSamplesPerSec * wf.Format.nBlockAlign;
	wf.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
	wf.Samples.wValidBitsPerSample = 32;
	wf.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
	wf.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
	return wf;
}

static std::wstring Lower(std::wstring s)
{
	for (auto &c : s)
		c = (wchar_t)towlower(c);
	return s;
}

/* Finds the top-most process of the given exe (Discord/Spotify/Chrome run many child
 * processes; capturing the root with INCLUDE_TARGET_PROCESS_TREE gets all of them). */
static DWORD FindRootProcess(const std::wstring &exe)
{
	HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (snap == INVALID_HANDLE_VALUE)
		return 0;
	std::wstring want = Lower(exe);
	struct P {
		DWORD pid, parent;
	};
	std::vector<P> matches;
	PROCESSENTRY32W pe = {sizeof(pe)};
	for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe)) {
		if (Lower(pe.szExeFile) == want)
			matches.push_back({pe.th32ProcessID, pe.th32ParentProcessID});
	}
	CloseHandle(snap);
	for (const P &p : matches) {
		bool parentIsSame = std::any_of(matches.begin(), matches.end(),
						[&](const P &o) { return o.pid == p.parent; });
		if (!parentIsSame)
			return p.pid;
	}
	return matches.empty() ? 0 : matches.front().pid;
}

using Microsoft::WRL::ComPtr;

/* Completion handler for ActivateAudioInterfaceAsync. Must be agile (free-threaded). */
class ActivateHandler : public IActivateAudioInterfaceCompletionHandler, public IAgileObject {
public:
	HANDLE done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	ComPtr<IAudioClient> client;

	virtual ~ActivateHandler() { CloseHandle(done); }

	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) override
	{
		if (riid == __uuidof(IUnknown) || riid == __uuidof(IActivateAudioInterfaceCompletionHandler))
			*ppv = static_cast<IActivateAudioInterfaceCompletionHandler *>(this);
		else if (riid == __uuidof(IAgileObject))
			*ppv = static_cast<IAgileObject *>(this);
		else {
			*ppv = nullptr;
			return E_NOINTERFACE;
		}
		AddRef();
		return S_OK;
	}
	ULONG STDMETHODCALLTYPE AddRef() override { return (ULONG)InterlockedIncrement(&refs); }
	ULONG STDMETHODCALLTYPE Release() override
	{
		LONG r = InterlockedDecrement(&refs);
		if (r == 0)
			delete this;
		return (ULONG)r;
	}

	HRESULT STDMETHODCALLTYPE ActivateCompleted(IActivateAudioInterfaceAsyncOperation *op) override
	{
		ComPtr<IUnknown> unk;
		HRESULT hr = E_FAIL;
		if (SUCCEEDED(op->GetActivateResult(&hr, &unk)) && SUCCEEDED(hr) && unk)
			unk.As(&client);
		SetEvent(done);
		return S_OK;
	}

private:
	LONG refs = 1;
};

static ComPtr<IAudioClient> ActivateProcessLoopback(DWORD pid)
{
	AUDIOCLIENT_ACTIVATION_PARAMS ap = {};
	ap.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
	ap.ProcessLoopbackParams.TargetProcessId = pid;
	ap.ProcessLoopbackParams.ProcessLoopbackMode = PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;

	PROPVARIANT pv = {};
	pv.vt = VT_BLOB;
	pv.blob.cbSize = sizeof(ap);
	pv.blob.pBlobData = (BYTE *)&ap;

	ComPtr<ActivateHandler> handler;
	handler.Attach(new ActivateHandler());
	ComPtr<IActivateAudioInterfaceAsyncOperation> op;
	if (FAILED(ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK, __uuidof(IAudioClient), &pv,
					       handler.Get(), &op)))
		return nullptr;
	if (WaitForSingleObject(handler->done, 5000) != WAIT_OBJECT_0)
		return nullptr;
	return handler->client;
}

static ComPtr<IAudioClient> ActivateDevice(bool input, const std::wstring &id)
{
	ComPtr<IMMDeviceEnumerator> en;
	if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&en))))
		return nullptr;
	ComPtr<IMMDevice> dev;
	HRESULT hr = (id.empty() || id == L"default")
			     ? en->GetDefaultAudioEndpoint(input ? eCapture : eRender, eConsole, &dev)
			     : en->GetDevice(id.c_str(), &dev);
	if (FAILED(hr))
		return nullptr;
	ComPtr<IAudioClient> client;
	if (FAILED(dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void **)client.GetAddressOf())))
		return nullptr;
	return client;
}

void AudioCapture::Run()
{
	CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);

	while (running) {
		ComPtr<IAudioClient> client;
		HANDLE process = nullptr;

		targetChanged = false;
		std::wstring want = Target();

		if (kind == Kind::Application) {
			DWORD pid = want.empty() ? 0 : FindRootProcess(want);
			if (pid) {
				process = OpenProcess(SYNCHRONIZE, FALSE, pid);
				client = ActivateProcessLoopback(pid);
			}
		} else {
			client = ActivateDevice(kind == Kind::Input, want);
		}

		bool ok = false;
		if (client) {
			WAVEFORMATEXTENSIBLE wf = FloatFormat();
			DWORD flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
				      AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
			if (kind != Kind::Input)
				flags |= AUDCLNT_STREAMFLAGS_LOOPBACK;
			HRESULT hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, 2000000 /* 200 ms */, 0,
							(WAVEFORMATEX *)&wf, nullptr);
			if (SUCCEEDED(hr) && SUCCEEDED(client->SetEventHandle(event)) && SUCCEEDED(client->Start()))
				ok = true;
		}

		if (ok) {
			ComPtr<IAudioCaptureClient> cap;
			client->GetService(__uuidof(IAudioCaptureClient), (void **)cap.GetAddressOf());
			while (running && cap) {
				WaitForSingleObject(event, 20);
				if (process && WaitForSingleObject(process, 0) == WAIT_OBJECT_0)
					break; // app exited; re-attach when it comes back
				if (targetChanged)
					break; // switched to another app

				UINT32 packet = 0;
				bool failed = false;
				for (;;) {
					if (FAILED(cap->GetNextPacketSize(&packet))) {
						failed = true;
						break;
					}
					if (packet == 0)
						break;
					BYTE *data = nullptr;
					UINT32 frames = 0;
					DWORD bufFlags = 0;
					if (FAILED(cap->GetBuffer(&data, &frames, &bufFlags, nullptr, nullptr))) {
						failed = true;
						break;
					}
					Push((bufFlags & AUDCLNT_BUFFERFLAGS_SILENT) ? nullptr : (const float *)data, frames);
					cap->ReleaseBuffer(frames);
				}
				if (failed)
					break; // device lost (unplugged / default changed): reopen
			}
			client->Stop();
		}

		if (process)
			CloseHandle(process);

		// Retry after a short delay (app not running yet, device unplugged, ...).
		for (int i = 0; i < 20 && running && !targetChanged; i++)
			Sleep(100);
	}

	CloseHandle(event);
	CoUninitialize();
}
