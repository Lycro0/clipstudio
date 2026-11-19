# ClipStudio

A clipping and recording app built on libobs (OBS Studio 33.0.1).
It has none of OBS's streaming, scenes, studio mode, docks, browser panels, filters UI or preview canvas.

## Default hotkeys (they work while a game has focus)

| Action                   | Default  |
|--------------------------|----------|
| Save clip (instant replay) | `Alt+O` |
| Start / stop recording   | `Alt+R`  |
| Show / hide the window   | `Alt+\`  |

You can change them in **Settings → Hotkeys**.

## Video
* **Game only**: game capture. Use *Any fullscreen game* or pick one game.
* **Full desktop**: monitor capture. Pick which monitor.

The canvas is set to your screen resolution, and the capture is always **stretched to fill the
whole frame**. No preview canvas is shown and there is nothing to position.

## Audio tracks
* **Mic + desktop audio**: Track 1 = Mic + Desktop, Track 2 = Desktop, Track 3 = Mic
* **Separate track per app** (Windows 10 2004+): Track 1 = Mic + Desktop (everything),
  Track 2 = Mic, Tracks 3–6 = up to 4 apps you pick (e.g. Game, Discord, Spotify).
  Apps are matched by .exe name, so a capture keeps working after the app restarts.

Every track carries its name (`title` / `handler_name`), so Premiere, Resolve and other editors show
"Discord", "Spotify" and so on. The default container is MP4, which most editors import.

## Clips tab (built-in track volume editor)
Pick a clip from your clips folder, or open any file. Each audio track gets a 0–200 % slider and a mute box.
* **Rebuild Track 1 from the other tracks** (on by default): most players and websites play only
  Track 1, so this re-creates the main mix from your adjusted tracks.
* **Preview** renders a temporary copy and opens it in your default player.
* **Save as copy** or **Overwrite clip** writes the result. Video is copied untouched; only the audio is re-encoded.

## Building (Windows)
ClipStudio builds as part of the normal OBS build. It is on by default (`-DENABLE_CLIPPER=ON`).

```
cmake --preset windows-x64
cmake --build --preset windows-x64 --config RelWithDebInfo --target clipstudio
```

It installs next to `obs64.exe` (`bin/64bit/clipstudio.exe`) and uses the same plugins
and data folders. To build only ClipStudio, add `-DENABLE_FRONTEND=OFF`.

Settings are stored in `%APPDATA%/ClipStudio/ClipStudio.ini`.
