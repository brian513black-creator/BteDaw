# Windows 11 release acceptance

A cloud/Linux test pass or a Windows Server CI run does not establish Windows 11 hardware readiness. Before labelling a build a Windows 11 release, retain evidence of these checks on actual Windows 11 x64 machines.

Use [Windows 11 runner setup](WINDOWS11_RUNNER.md) to connect a PC and execute the automated portion. A green workflow run does not replace the physical-device and manual acceptance checks below.

- Install the produced NSIS executable on a clean machine without development tools, start it from the Start menu, and uninstall it. Also run the portable ZIP as a standard user. Confirm dependency/source license material is present.
- Verify startup at 100%, 125%, 150% and 200% display scaling, including a 1280×720 logical work area, multiple displays, native VST3 editors and window resizing.
- Play and export the bundled example. Open the WAV in another audio application and confirm stereo audio, expected duration, 48 kHz / 24-bit encoding and headroom.
- Select WASAPI input/output devices; record mono and stereo audio; stop, save, close, reopen and replay the take. Confirm Windows microphone permissions and vendor device drivers are correctly configured. Check device switching, sample rates, buffer sizes and disconnect/reconnect behavior.
- Select the Universal Audio ASIO driver on a supported Apollo Twin Thunderbolt/Twin X Windows configuration. Verify reported input/output delays, supported buffer setup, Console monitoring with BTE input monitoring off, physical loopback alignment and manual correction. Repeat with another ASIO device and WASAPI. Follow [recording latency](RECORDING_LATENCY.md); synthetic alignment does not prove physical zero delay.
- Record a USB MIDI keyboard into 4OSC, check monitoring, edit notes, save/reopen, and render the result. Confirm notes release when stopping and switching sessions.
- Exercise output cables, nested buses, post-fader sends, mono sum, pan, mute/solo, meters and sidechains. Attempts to form primary/send/sidechain feedback loops must be rejected.
- Test several real Windows x64 VST3 instruments/effects with native editors, presets, sidechain inputs and reported latency. Save/reopen plugin state and compare live playback with mix/stem export. The automated fixture is insufficient as a compatibility matrix.
- Check Region gain/fade/pitch/reverse/stretch against playback and export; move and copy/cut/paste between channels and sessions. Verify quantize/swing/humanize, selected-note velocity and transpose with undo. Navigate/rename/delete markers and save/reopen.
- Record with one/two-bar count-in and punch-in/out on physical input. Check the resulting region/file bounds, loop behavior and monitoring.
- Export mono/stereo WAV and FLAC at multiple rates/depths, standard MIDI with notes/tempo, range/tail options and loudness targets. Open files in an independent application. Test cancel/overwrite behavior with existing output files.
- Move a session's source files aside, verify missing-region warnings, then manually relink/search; duplicate filenames must remain unresolved until explicitly chosen. Save/reopen and verify owned source files.
- Test audio edits, undo/redo, gain staging, automation, portable session copies, autosave recovery and save/discard/cancel. Check missing media, corrupt sessions, unavailable plugins, full disks and read-only output folders.
- Run long recording/playback sessions, increase track/plugin counts, monitor dropouts and CPU/memory use, and test cancellation of export without damaging a prior file. Retain sample rate, block size, device driver, CPU and plugin versions with performance results.

ASIO is now enabled in the Windows source configuration and still needs native driver/hardware acceptance. Signing and plugin isolation remain outstanding release work. The installer is unsigned, and Windows may show its normal unrecognized-publisher prompt.
