#pragma once

#include <windows.h>
#include <cstdint>
#include <memory>
#include <vector>

struct VideoFrame {
	std::vector<uint8_t> bgra; // tightly packed, stride = width * 4
	int width = 0;
	int height = 0;
	uint64_t serial = 0;
};

/* Windows Graphics Capture (the API OBS uses for "Windows 10 (1903 and up)" window and
 * display capture). Works for borderless/windowed games and most fullscreen games. */
class VideoCapture {
public:
	VideoCapture();
	~VideoCapture();

	bool StartWindow(HWND hwnd, bool cursor);
	bool StartMonitor(HMONITOR monitor, bool cursor);
	void Stop();

	/* Only copy frames off the GPU at up to this rate (the monitor may refresh at 144+ Hz). */
	void SetMaxFps(int fps);

	/* Hands the newest frame to `out` (buffers are swapped, not copied) if it is newer than
	 * out.serial. */
	bool GetLatest(VideoFrame &out);

	HWND Window() const;
	bool Active() const;

	static bool IsSupported();

private:
	struct Impl;
	std::unique_ptr<Impl> d;
};
