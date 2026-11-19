#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

/* Captures 48 kHz stereo float audio from one of:
 *   - a microphone / input device
 *   - an output device (desktop audio loopback)
 *   - one application (WASAPI process loopback, Windows 10 2004+; the same API OBS's
 *     "Application Audio Capture" uses). Includes the app's child processes, and
 *     re-attaches automatically if the app is restarted. */
class AudioCapture {
public:
	enum class Kind { Input, Loopback, Application };

	AudioCapture(Kind kind, std::wstring target); // device id ("default") or exe name
	~AudioCapture();

	void Start();
	void Stop();

	/* Reads up to `frames` stereo frames (interleaved). Returns frames read. */
	size_t Read(float *dst, size_t frames);
	size_t Available();
	void DropTo(size_t frames); // discard oldest audio so at most `frames` remain

	static constexpr int kRate = 48000;
	static constexpr int kChannels = 2;

private:
	void Run();
	void Push(const float *src, size_t frames);

	Kind kind;
	std::wstring target;
	std::thread thread;
	std::atomic<bool> running{false};

	std::mutex m;
	std::vector<float> ring; // interleaved, capacity in samples
	size_t head = 0, size = 0;
};
