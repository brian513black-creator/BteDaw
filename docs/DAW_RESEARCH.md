# DAW workflow research and BTE 0.3 implementation

Research date: 2026-11-08. This is a workflow comparison, not a claim of feature parity or a Windows release certification. Implementation uses BTE's existing Tracktion engine and original interface code.

## Sources examined

The cloud network blocked direct Apple Logic Pro, Ableton and REAPER product/manual pages. Those products were not verified through current public manuals in this pass. Accessible official source repositories provided the following evidence:

- **Ardour manual**, [revision 9628c8b](https://github.com/Ardour/manual/tree/9628c8bad094537fddd967ef137e8a692e2b1503). Read [MIDI quantize](https://github.com/Ardour/manual/blob/9628c8bad094537fddd967ef137e8a692e2b1503/include/quantize-midi.html), [velocity editing](https://github.com/Ardour/manual/blob/9628c8bad094537fddd967ef137e8a692e2b1503/include/midi-velocity-editing.html), [punch recording and takes](https://github.com/Ardour/manual/blob/9628c8bad094537fddd967ef137e8a692e2b1503/include/record-punch-options.html), [fades and crossfades](https://github.com/Ardour/manual/blob/9628c8bad094537fddd967ef137e8a692e2b1503/include/create-region-fades-and-crossfades.html), [location markers](https://github.com/Ardour/manual/blob/9628c8bad094537fddd967ef137e8a692e2b1503/include/creating-location-markers.html), and [export formats and normalization](https://github.com/Ardour/manual/blob/9628c8bad094537fddd967ef137e8a692e2b1503/include/export-format-profiles.html). These describe user workflows in detail.
- **REAPER official extension SDK**, [revision b2ca1ec](https://github.com/justinfrankel/reaper-sdk/blob/b2ca1ec92c9a7864fb72346037986a384971ed03/sdk/reaper_plugin_functions.h). `MIDI_SetNote` exposes start/end/pitch/velocity; `AddProjectMarker` exposes named positions/ranges; media-item/take and project render APIs expose clip and export properties. This establishes API capabilities, not current UI behavior or a full REAPER review.
- **LMMS official website source**, [revision 79ed633](https://github.com/LMMS/lmms.io/blob/79ed6330c00c4fd2811c1c1fc06450a988362148/templates/home.twig). Describes piano-roll composition, MIDI/typing-keyboard input, beat sequencing, effects mixing and plugin instruments. BTE already had a piano roll, built-in instruments, a generated drum groove and an effects mixer; deeper MIDI editing was missing.

## Gaps addressed in this revision

| Workflow | BTE before this change | BTE 0.3 behavior |
| --- | --- | --- |
| MIDI timing | Draw/move/resize notes | Region-wide grid quantize with strength, swing and optional note ends; seeded timing/velocity humanize |
| MIDI dynamics and pitch | Fixed new-note velocity; pitch drag | Selected-note velocity slider, bulk velocity and transpose with MIDI-range clamping |
| Audio region properties | Trim/split/move; short edge fades | Editable gain, fade times, pitch, reverse and length using SoundTouch stretch; visible draggable fade handles |
| Transfer regions | Duplicate in same channel | Copy/cut/paste at playhead, cross-track drag, unique clip IDs, copied media ownership across sessions |
| Arrange navigation | Ruler seek and loop | Named markers, ruler flags, jump/rename/move/delete, previous/next navigation |
| Recording preparation | Arm/record and metronome | Visible loop/punch range and zero/one/two-bar count-in settings |
| Missing source recovery | No relinking UI | Missing waveform warning; manual relink and recursive folder search that skips ambiguous filenames; copies replacements into session media |
| Delivery options | Fixed 48 kHz/24-bit WAV plus stems | WAV/FLAC/MIDI dialog; audio rate/depth/mono, whole-session or loop range, tail and LUFS target; existing WAV/stems commands retained |
| Insert order | Add/remove/bypass only | Move up/down with undo; instruments stay first, sends remain after the fader |

Session-media recovery is also a data-safety gap found in BTE's own code review. Do not attribute every implementation detail above to a competitor. BTE quantize uses a shared grid for starts/ends and no threshold control; velocity editing is a slider, not Ardour's dedicated velocity lane. BTE has per-region fades, not a linked crossfade editor. Punch shares the loop range and does not provide take comping. Count-in is an application preference rather than a session property.

## Remaining substantial gaps

| Area | Still required |
| --- | --- |
| Recording | Take lanes/comping, robust loop-record workflows, pre/post roll UI, physical Windows audio/MIDI acceptance |
| Detailed arrangement | Multiselection, groups, ripple/range editing, linked crossfades, stretch/warp markers, tempo-map editor, advanced fades |
| MIDI | Dedicated velocity/CC/expression lanes, multiselect/selection-only transforms, step entry, scale/chord tools and typing-keyboard performance |
| Automation | General parameter lanes, point selection/move/delete/curves, latch/touch/write modes and hardware control surfaces |
| Plugins | Isolated scan and crash recovery, plugin browser/presets, broad real-world VST3 compatibility and instruments with sample-library management |
| Mixing | Metering/loudness workflow beyond export normalization, comprehensive routing matrix, performance/latency inspection and larger-session usability |
| AI | The current assistant is a bounded local command tool. LLM planning, transcription and generative audio/MIDI need separate implementation and evaluation |
| Windows release | Successful Windows builds and installer evidence, Windows 11 hardware/driver tests, ASIO, signing, stress/soak and performance budgets |

These gaps prevent a claim that BTE has everything in an established professional DAW. See [validation evidence](VALIDATION.md) and the [Windows acceptance checklist](WINDOWS_ACCEPTANCE.md). New 0.3 workflow tests are part of both Windows workflows through CTest; source configuration is not proof those Windows runs passed.
