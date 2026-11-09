# Stress testing BTE Studio

The `bte_stress_tests` executable drives the real Tracktion graph, stock synths/effects and BTE VST3 fixture. It uses generated audio through the hosted-device callback, not WASAPI/ASIO or physical MIDI. It never opens the user's normal BTE preferences; sessions/settings/media use a fresh temporary directory.

## Reproduce on Windows

Build Release for meaningful timing measurements. From PowerShell at the repository root:

```powershell
cmake -S . -B build -A x64 -DBTE_BUILD_APP=ON -DBUILD_TESTING=ON -DBTE_ENABLE_STRESS_TESTS=ON
cmake --build build --config Release --parallel 2
ctest --test-dir build -C Release --output-on-failure
& './build/bte_stress_tests_artefacts/Release/bte_stress_tests.exe' `
  --vst "$PWD/build/BteTestEffect_artefacts/Release/VST3/BTE Test Effect.vst3" `
  --report "$PWD/build/reports/stress-soak.json" `
  --tracks 128 --notes 2048 --cycles 20 --edits 2000 `
  --seconds 180 --record-seconds 60 --threads 4
if ($LASTEXITCODE -ne 0) { throw 'Stress integrity check failed; inspect the report and console log.' }
```

The Windows CI job includes a short bounded stress case. The manually started Windows 11 workflow also runs a larger case; its `stress_seconds` input controls total generated playback duration. Both upload test evidence. Neither runs automatically on a personal runner in response to a pull request.

On Linux, the same source can run as development validation with `BTE_VALIDATE_LINUX=ON` and a working display/Xvfb. Use the single-config build's paths without `.exe` and replace `Release` with the configured build type. Linux results cannot establish Windows driver performance.

## Workload and assertions

- Build the requested number of audio tracks, each with four two-second wave regions and an EQ. Up to 32 audio tracks also host the real VST3 doubling fixture. Eight synth tracks contain `--notes` MIDI notes each. Group buses feed a final bus.
- Render an eight-second mix with MIDI muted. Decode the output and check its duration and mathematically expected amplitude, including VST3 processing and nested buses.
- Add four effect returns with post-fader sends; load sidechain-enabled VST3 fixtures on the group buses. Reject output, send and sidechain cycles.
- Apply seeded edits and undo: channel mono mode, region duplication, dense MIDI transpose/humanize, insert creation and output routes. Verify state after undo.
- Save/reopen repeatedly, checking track counts, routes, notes and media. Allow the engine's asynchronous post-load initialization to settle before beginning another edit. Verify recovery of a subsequent unsaved change.
- Play the large session with synths enabled at 44.1 kHz/64, 48 kHz/128, 48 kHz/512 and 96 kHz/256. Generated callbacks run on a separate thread while the message thread moves faders. Stop and reconfigure between profiles.
- Record generated input onto eight tracks, decode each recorded tail, check duration, save/reopen and verify all takes.
- Allocate all 32 auxiliary bus numbers across four sources, verify every send appears in its chain, and reject a 33rd without changing routing. Fill a channel to its engine insert limit and reject additional stock/VST3/send inserts. Destroy the edit, hosted device and engine.

`--tracks` means audio tracks, not the total including instruments/buses/returns. `--seconds` is total requested audio duration split between the four playback profiles. An overloaded run can take longer in wall time. `--record-seconds` is additional recording time. `--threads` explicitly sets the engine's audio CPU budget; zero/default application behavior is unchanged. `--keep-session` retains synthetic projects for inspection; failures also retain them. Successful runs otherwise remove generated media.

## Interpret the JSON report

`correctness_pass` and process exit status cover integrity assertions and caught exceptions. Phase checkpoints are written atomically; `status: running` with no final result means the run was interrupted or incomplete. A crash, timeout or missing report is a failed/incomplete run. A successful short run is not proof against leaks or long-session failures.

Callback timing measures only `io.processBlock`; the first half-second of each profile is excluded from timing to allow graph/file warm-up. A **compute deadline miss** means that call took longer than one buffer period. The internal diagnostic target allows at most 1% misses per playback profile; `all_profiles_compute_budget_pass_1_percent` reports it separately. Correctness can pass while that budget fails. These are not measured device xruns: wake-up latency, kernel/driver latency and physical audio continuity are outside this harness. Real-time acceptance requires hardware testing and a justified tighter budget for the target machine.

The engine's automatic CPU-overload muting guard is disabled for the test so that overload cannot turn the session silent and hide work. No code assertion is disabled in Debug builds. Release disables normal Debug assertions as usual. The actual application uses its normal overload guard.

Reports include build/revision information, fixed seed, requested/actual workload, phase durations, callback percentiles/maxima, misses, finite-sample checks, peaks, periodic resident-memory samples and OS handle counts. Linux counts file descriptors; Windows counts all process handles, so those figures are not directly comparable. Resident-memory changes include legitimate caches/allocator retention; they are not themselves a leak diagnosis. `source_dirty_at_configure` records whether the source was modified when CMake generated the binary.

The cloud has a CPU quota and lacks audio hardware. Do not compare its callback numbers with a dedicated Windows workstation or claim a tested Windows release from them. Retain the report, console output, build configuration, CPU budget and commit with results. See [Windows acceptance](WINDOWS_ACCEPTANCE.md) for physical tests and long recording/performance checks.
