# Silta Cut

Native four-track editor: two video tracks, two audio tracks, audio-clocked
playback, background thumbnails/waveforms, undo/redo and MP4 export.

## Build and run (Windows x64)

Put the FFmpeg 8.1 shared development distribution in `third_party/ffmpeg`
(`include`, `lib`, `bin`). It must include `libopenh264` and `aac` encoders.
The editor uses CPU OpenH264 for predictable export on machines without a
hardware encoder. Hardware encoder selection is not implemented.

```powershell
cmake -S . -B build/win -G "Visual Studio 17 2022" -A x64 -DSILTA_EDITOR=ON
cmake --build build/win --config Release --target silta_cut cut_media_test cut_engine_test -j 1 -- /nr:false
./build/win/examples/editor/Release/silta_cut.exe
```

miniaudio 0.11.23 is downloaded on first configure and verified by SHA-256.
For offline builds place the unchanged header in `third_party/miniaudio` first.
FFmpeg DLLs and fonts are copied beside each executable. The editor is opt-in;
normal Silta builds do not require FFmpeg or miniaudio.

## Editing

- Import with the toolbar, Ctrl+O, or files dropped from Explorer. Double-click
  an asset to append it, or drag it from the library onto a compatible track.
- Click the ruler to seek; Space plays/pauses, arrows step a frame, Home rewinds.
- V selects, B activates the blade, S splits at the playhead. A selected clip is
  split alone; with no selection all unlocked tracks are split.
- Drag clips to move them; drag either edge to trim. Clips snap to frames,
  other clip edges and the playhead. N toggles snapping. Overlaps on a track
  are rejected; importing into an occupied range appends at the track end.
- Delete removes a clip. Shift+Delete / Ripple delete also closes its gap on
  that track. Ctrl+Z undoes; Ctrl+Shift+Z / Ctrl+Y redoes.
- The inspector adjusts volume and video opacity. Track controls lock edits,
  mute audio or hide video. Video sources retain their attached audio.
- Wheel over the timeline scrolls horizontally; Ctrl+wheel or Zoom changes scale.

Export writes 1920×1080, 30 fps H.264 with stereo AAC at 48 kHz. A `.partial.mp4`
is finalized and renamed only on success; cancel/failure removes it. Choose a
new filename: existing output files are preserved. Export uses a snapshot, so
subsequent edits cannot change an export in progress.

The preview follows audio samples consumed by miniaudio. On underrun it waits
for samples; without an audio device it runs silently on the system clock and
shows that status. Preview work, media analysis and export have separate decoders.

## Repeatable verification

```powershell
./examples/editor/smoke.ps1
./examples/editor/smoke.ps1 -Backend vulkan
```

The script generates four media fixtures in a fresh folder under
`build/editor-test`, tests decoding (including sample alignment after seek),
model edits, compositing, audio mixing, playback, export, cancellation and
existing-file protection. It drives the hidden editor through mouse/key events
to verify selection, blade/split, move, trim, ripple delete, undo/redo, track
locking, ruler seeking and zoom. It captures the editor, export progress,
completion dialog and toast, then decodes the complete exported MP4 with FFmpeg.

The same harness is available through `--hidden`, repeated `--import=<path>`
(imports and appends), `--backend=d3d11|vulkan` and `--script=<commands>`.
Commands are separated by semicolons: `click:x,y`, `down:x,y`, `move:x,y`,
`up:x,y`, `key:s|b|v|delete|undo|redo|home|right|space|escape`, `seek:seconds`,
`wait:frames`, `sleep:milliseconds`, `await:analysis|preview|export`,
`export:path`, `shot:path`, `check:field=value`, `quit`.
Coordinates are logical points, independent of display scale. Script failures
exit with code 2 and a diagnostic on stderr. UI automation uses the default
1440×900 window size.

This is an in-memory editing example. Project save/load, transitions, effects,
rotation metadata, color management/HDR and hardware export are not implemented.
The initial interface is designed for desktop windows around 1440×900.
