# ClipStudio

A lightweight Windows app for **clipping and recording games**. It runs in the system tray, keeps the
last N seconds in memory, and saves a clip when you press a hotkey. It can put each app's audio
(game, Discord, Spotify…) on its own audio track, and lets you rebalance those tracks afterwards.

## Download
Grab **`ClipStudio-Setup-x.y.z.exe`** from the [Releases](../../releases) page and run it.
A portable `.zip` is attached too.

Requires Windows 10 version 2004 (May 2020) or newer, 64-bit.

## Default hotkeys (work while a game has focus)

| Action                       | Default  |
|------------------------------|----------|
| Save clip (last N seconds)   | `Alt+O`  |
| Start / stop recording       | `Alt+R`  |
| Show / hide ClipStudio       | `Alt+\`  |

Change them in **Settings → Hotkeys**.

## Video
* **Game only**: captures the fullscreen game automatically, or a game you pick from the list.
* **Full desktop**: captures a whole monitor.

The video is the size of your screen, and the capture is always **stretched to fill the whole
frame**. There is no canvas or preview to arrange.

**Multi-monitor clips** (on by default): every clip and recording also saves each of your other
monitors as its own file, for example `Clip 2026-11-21 20-15-03 (Display 2).mp4`, with the same audio
tracks. That way you also catch what was happening on your second screen, such as Discord reactions.

Encoding uses your GPU when possible (NVIDIA NVENC, AMD AMF, Intel Quick Sync), and falls back to x264 on the CPU.

## Audio tracks
* **Mic + desktop audio**: Track 1 = Mic + Desktop, Track 2 = Desktop, Track 3 = Mic
* **Separate track per app** (default): Track 1 = Mic + Desktop (everything), Track 2 = Mic,
  then **Discord** and **Spotify** (always looked for; you can change or add up to 4 apps), then
  **automatic tracks** (3 by default). When any other app starts playing sound (a game you just
  launched, a browser), it gets the next free automatic track. When it closes, that track is freed.
  You never have to pick the game by hand.
  Apps are matched by their .exe, including their child processes, and are picked up again when
  restarted.

Tracks are named inside the file, so Premiere Pro, DaVinci Resolve and other editors show
"Discord", "Spotify" and so on as separate audio tracks.

## Clips tab: change track volumes inside the app
Pick a clip, then use a 0–200 % slider or **Mute** for each track. For example, turn Discord down and the game up.
* **Rebuild Track 1** (on by default): most players and websites play only Track 1, so it is
  re-mixed from your adjusted tracks.
* **Preview** opens a quick render in your video player. **Save as copy** or **Overwrite clip** writes the file.
  The video is copied untouched; only the audio is re-encoded.

## Building from source
GitHub Actions builds every push (`.github/workflows/build.yml`). To publish a release with an
installer, do either of these:
* push a tag such as `v1.0.0`, or
* open **Actions → Build → Run workflow** and enter a version such as `1.0.0`.

To build locally you need Visual Studio 2022, Qt 6.7 (MSVC 64-bit), CMake, and a shared FFmpeg build
(e.g. from [BtbN/FFmpeg-Builds](https://github.com/BtbN/FFmpeg-Builds)) unpacked to `deps/ffmpeg`:

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## Credits
ClipStudio's capture approach (Windows Graphics Capture, WASAPI process loopback, per-track audio
encoders) follows the design OBS Studio uses. ClipStudio does not contain OBS code.
It uses Qt and FFmpeg.

## License
GPL-3.0-or-later (see LICENSE). The bundled FFmpeg build is GPL.
