# ClipStudio

A clipping and recording app built on libobs (OBS Studio 33.0.1).
It has none of OBS's streaming, scenes, studio mode, docks, browser panels or preview canvas.

This repo holds only the ClipStudio files. To build, drop them into an OBS Studio 33.0.1 source tree:

1. Copy `clipper/` into the root of the OBS 33.0.1 source.
2. Apply the patch: `git apply obs-33.0.1-clipstudio.patch` (run from the OBS source root). It adds the `ENABLE_CLIPPER` option to the root `CMakeLists.txt` and makes the OBS muxer also write audio track names as `handler_name`, so MP4 files keep names like "Discord" and "Spotify".
3. Build on Windows:

```
cmake --preset windows-x64
cmake --build --preset windows-x64 --config RelWithDebInfo --target clipstudio
```

See `clipper/README.md` for features and usage.
