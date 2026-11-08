# BTE Studio

A native **Windows 11 x64** music workstation built with C++20, JUCE and Tracktion Engine. It implements recording, region editing, MIDI composition, synthesis, effects, routing, automation, session recovery and audio export. The previous in-memory playback prototype has been replaced by Tracktion's audio graph and disk-streaming engine.

**Release status:** development build. Local cloud validation runs on Linux; a successful Windows workflow and real Windows 11 hardware acceptance testing are required before treating a build as a tested Windows release. This is not yet a feature-complete replacement for Logic Pro.

## Windows installer

Open [Windows build and installer](https://github.com/brian513black-creator/BteDaw/actions/workflows/windows.yml). From a successful run, download and extract the **BTE-Studio-Windows-x64** artifact:

- Run `BTE-Studio-0.2.0-Windows-x64.exe` to install the app and Start menu shortcut.
- Or extract the portable ZIP and run `BTE Studio.exe`.
- The executable uses a static Microsoft C runtime. End users do not need Visual Studio, CMake, JUCE, an API key, or a separate sound library.
- The built-in 4OSC synth, original BTE percussion synth, and stock effects work offline. Third-party VST3 plugins remain separate installations.
- Select **Audio** to choose your WASAPI output, input channels, sample rate, buffer size and MIDI devices. Its input-grouping buttons choose separate mono sources or stereo pairs; reselect track inputs after changing grouping. Hardware drivers and Windows microphone permissions remain system settings. ASIO is not included in this build.

The installer is unsigned. The workflow builds the app and test VST3, tests the engine, starts the native application, packages NSIS/portable outputs, and checks silent installation, installed-app startup, and uninstallation. It also uploads corresponding source and SHA-256 checksums. It does not publish a GitHub release.

## Start making music

Choose **Project → Create playable built-in demo session** for an original six-channel example: keys, bass, arpeggio, synthesized drums, a reverb return, and a music bus. It creates real editable MIDI regions, routing and effects, saves the session in Documents/BTE Studio/Sessions, and can play, loop and export without additional content.

For a blank session:

1. Add **Audio**, **Instrument**, or **Bus** channels. Instrument channels start with the built-in 4OSC synth.
2. Import/drop WAV, AIFF, FLAC, OGG, MP3 or MIDI. Imported audio is copied into the current session's `Media` directory. Audio streams from disk rather than loading the whole project into RAM.
3. For recording, choose an input in the inspector, click **R** on the track to arm, and click **REC**. **I** enables automatic input monitoring for the assigned device; monitoring starts off. Stop commits the recorded audio/MIDI to the timeline.
4. Use the piano roll or imported MIDI to play instruments. Choose **Track → Create BTE drum instrument and groove** for a drum instrument and editable pattern without samples/downloads.
5. Set channel levels, insert effects, route groups, then choose **Project → Export stereo WAV**.

## Editing and transport

- Space toggles playback. Stop ends recording; `|<` stops and returns to zero. **Loop** and **Click** toggle loop playback and the metronome.
- Click the ruler to seek. Shift-click it to set a four-bar loop; Mix can set the loop to the selected region.
- Drag a region to move it horizontally. Drag its left/right edges to trim it non-destructively. **Split** cuts at the playhead; Duplicate and Delete act on the selected region. Edit can add short audio edge fades.
- The Snap menu controls quarter-beat, beat, bar, or free positioning. Ctrl+wheel zooms; Shift+wheel scrolls horizontally; ordinary wheel scrolls tracks. A scrollbar also moves the timeline.
- Double-click a MIDI region to open its piano roll. Click to draw notes, drag to move pitch/time, Shift-drag to change note length, and right-click to remove notes. Scroll the piano roll to change its displayed pitch range.
- Undo/Redo uses the edit's undo history, including region edits, routing, gain staging and assistant edits.
- On smaller windows the library hides and the inspector scrolls; editing commands remain in menus/keyboard shortcuts.

## Mixing and routing

- Each channel has a smoothed **-60 to +24 dB input trim**, channel fader, pan/balance, mono fold, mute/solo, output routing, inserts and meters. Mono fold is `(L + R) / 2` before trim and inserts. Stereo Out has its own fader and mono-sum switch.
- Drag the small output jack in a track header onto another track to route its output there. Drop on the Stereo Out mixer strip to return to Master. The inspector's Output menu does the same thing. Buses can feed other buses.
- **+ Send** adds a post-fader auxiliary send and a return on the chosen destination. Its level is editable through the send's parameter editor. Sends keep the main route in parallel. Primary routes, sends and sidechains are checked together for feedback cycles.
- Choose an insert, choose a sidechain source, then **Connect SC**. The effect must expose a sidechain input. The plugin may also require its own external-sidechain mode.
- Double-click a library item to insert a built-in effect/instrument. **Browse VST3** loads a trusted 64-bit VST3 bundle. A bundle's first discovered class is loaded; all discovered classes are retained in the engine's known-plugin list.
- Double-click an insert to open its native editor or parameter controls. Right-click for bypass or removal. The insert list scrolls with the inspector.
- Built-ins include 4OSC, BTE Drum Machine, EQ, compressor, reverb, delay, chorus, phaser and low-pass filter. The engine provides plugin delay compensation and graph processing.
- **Mix → Auto level match** measures whole-region source RMS and sets input trims toward **-18 dBFS RMS**, limited by a **-1 dBFS source peak ceiling** and the trim range. This is gain staging, not perceptual LUFS or post-effect matching. Silence is unchanged. Faders remain unchanged; Undo restores trims.
- **Automation** displays volume points. Alt-click a track lane to add a point. Mix can clear that channel's volume automation. Plugin parameters are exposed in their editors, but a full parameter-automation lane editor is not implemented.

## Sessions and export

Ctrl+S saves the current `.bte` session atomically. Save Copy exports a separate session and copies referenced audio into its `Media` folder without relocating the live recording session. External plugin sample libraries remain managed by their plugins.

The session stores MIDI, clips, routing, channel settings, tempo, automation, stock instrument settings and VST3 state. Autosave writes a recovery snapshot every 30 seconds when changed and not recording. Open the original session and choose **Project → Recover last autosave** to restore its snapshot. Closing with unsaved changes offers Save, Discard, or Cancel. Old JSON `.btedaw` prototype files are not migrated to the new engine format.

Export writes **24-bit / 48 kHz stereo WAV**, including instruments and effects. The normalized option targets **-14 LUFS with a -1 dBTP ceiling** using the engine's loudness measurement. Individual channel/bus stems use an offline session copy, keep required input/sidechain/send dependencies, and bypass master processing. Stereo mix export includes master processing. Render cancellation or failure preserves an existing destination file.

## Offline assistant

The assistant previews edits before applying them. It is a bounded local command/arrangement tool, **not an LLM or audio-generation model**. Supported examples:

```text
Set tempo to 128
Turn track 2 down 3 dB
Mute track 1
Unmute track 1
Solo track 3
Unsolo track 3
Create chord progression
Create drum groove
```

Composition commands create editable MIDI with bundled instruments. Ctrl+Z undoes the changes. There is no external AI account, background upload, or model download.

## Build from source

Install Visual Studio 2022 with Desktop development with C++, a current Windows SDK, CMake 3.22+, and Git. From PowerShell in this checkout:

```powershell
cmake -S . -B build -A x64 -DBTE_BUILD_APP=ON
cmake --build build --config Release --parallel 2
ctest --test-dir build -C Release --output-on-failure
& '.\build\BteDaw_artefacts\Release\BTE Studio.exe'
```

JUCE and Tracktion source archives are pinned to exact commits and SHA-256 hashes. The first configure needs access to codeload.github.com. Install NSIS to build an installer:

```powershell
cpack --config build/CPackConfig.cmake -C Release -G NSIS
cpack --config build/CPackConfig.cmake -C Release -G ZIP
```

`BTE_VALIDATE_LINUX=ON` enables a development-only native build using ALSA, X11 and font development headers. It is not a supported Linux distribution target and cannot establish Windows hardware compatibility. See [validation](docs/VALIDATION.md), [Windows acceptance checks](docs/WINDOWS_ACCEPTANCE.md), and [dependency licenses](docs/THIRD_PARTY.md).

## Remaining release work

Real Windows 11 interface/driver testing, long-session performance and stress tests, ASIO support, code signing, plugin scan/crash isolation, missing-media relinking, take comping, full automation-lane controls, broader export-format/options UI, advanced time/pitch editing, and LLM/generative AI are outstanding. The current app has a working core production workflow; it is not certified as a professional release and should not be described as having every feature of an established commercial DAW.

The repository's GPL-3.0 license covers BTE source. JUCE and Tracktion retain their own licenses. The Windows artifact includes dependency notices and corresponding source; see THIRD_PARTY.md before distribution.
