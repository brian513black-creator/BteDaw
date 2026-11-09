# Stress and recording-latency results — 2026-11-09

**Integrity passed; universal low-latency and Windows hardware acceptance did not pass or become established.** The heavy session exceeded compute budgets at small buffers. Synthetic recording alignment was sample-accurate with compensation. Memory growth in the large reload phase needs further profiling.

## Recording alignment

[Raw 80-case results](stress-results/2026-11-09-latency-80-cases.json), matching source commit `a8ede3c`, contain **1,140 passing checks** from ten repetitions of eight recording/reconfiguration scenarios:

| Scenario | Repetitions | Recorded alignment error |
| --- | ---: | ---: |
| 48 kHz / 64 frames, no reported driver delay | 10 | 0 samples |
| 44.1 kHz / 64 frames, 64 input + 96 output samples reported | 10 | 0 samples |
| 48 kHz / 64 frames, 128 input + 192 output samples reported | 10 | 0 samples |
| 48 kHz / 128 frames, 256 input + 128 output samples reported | 10 | 0 samples |
| 96 kHz / 256 frames, 96 input + 160 output samples reported | 10 | 0 samples |
| 48 kHz / 128 frames, driver delay plus a real 128-sample DSP delay | 10 | 0 samples |
| 48 kHz / 128 frames, additional 17-sample late input with calibrated correction | 10 | 0 samples |
| Control: cancel automatic compensation for 128 + 192 driver samples | 10 | 320 samples late, as expected |

The test sends an actual reference impulse through a causal generated loopback and records it to disk. It decodes the recorded file to compare impulse location with the reference timeline. It checks readable audio, correction/device enumeration, supported buffer changes, and refusal to change correction/buffer during recording. See [method and calibration](RECORDING_LATENCY.md).

These are **alignment errors**, not measured physical round-trip latency. They do not measure an Apollo, Thunderbolt controller, converter or Windows ASIO driver. They establish correct compensation in the synthetic cases, including a control that would fail if the checker simply forced every result to zero.

## Optimized large-session workload

[Raw Release report](stress-results/2026-11-09-release-128.json), source commit `66758ba`, was generated from a clean source tree on Linux with GCC 14.2, JUCE 9.0.5 and the pinned Tracktion engine. The host exposed five logical CPUs with a four-CPU cgroup quota and 32 GiB memory limit; the test explicitly used four audio CPUs. No other build or native GUI workload ran during this optimized measurement.

The session contained **157 tracks**: 128 audio tracks, eight synth tracks, 16 group buses, a mix bus and four effect returns. It had **512 audio regions**, **16,384 MIDI notes**, **48 real VST3 fixture instances**, EQs, synths, sends, returns and sidechains. **852 plugin instances on tracks include 628 internal channel utilities**; it is not 852 third-party effects.

The run passed **3,388 integrity assertions** in **406.16 seconds**:

- Eight-second mix export completed in 2.05 seconds with the expected decoded duration/amplitude, including VST3 doubling and nested buses. MIDI was muted for this numerical comparison.
- 2,000 edits/undo operations completed in 132.76 seconds; 20 save/reload cycles plus recovery completed in 16.91 seconds. Routes, MIDI note counts and media references survived.
- The full session played with synths enabled for 180 seconds of requested audio across four profiles. All inspected output samples were finite, and no silent blocks were observed.
- A **separate** eight-track session recorded generated input for 60 seconds, verified each recorded tail/duration, and saved/reloaded all takes. This was not simultaneous recording into the 157-track session.
- All 32 auxiliary bus numbers were allocated across four source channels; a 33rd was refused. Channel capacity checks verified that successful sends appear in the graph and that full channels refuse extra stock effects, VST3 and sends.

## Compute latency under the large workload

| Rate / frames | One-buffer budget | Callback p99 | Missed compute deadlines | Internal ≤1% diagnostic target |
| --- | ---: | ---: | ---: | --- |
| 44.1 kHz / 64 | 1.451 ms | 2.328 ms | 10,948 / 30,663 — 35.70% | Fail |
| 48 kHz / 128 | 2.667 ms | 2.893 ms | 245 / 16,687 — 1.47% | Fail |
| 48 kHz / 512 | 10.667 ms | 9.134 ms | 14 / 4,172 — 0.34% | Pass diagnostic target; misses still occurred |
| 96 kHz / 256 | 2.667 ms | 5.480 ms | 12,574 / 16,687 — 75.35% | Fail |

Only `io.processBlock` is timed; the first half-second is excluded from timing. These figures are callback compute overruns, **not measured driver xruns or physical monitoring delay**. The test disables automatic overload muting to keep the workload active. The 1% threshold is an internal diagnostic, not a professional audio acceptance standard. The 512-frame profile's result does not establish glitch-free playback.

The separate eight-track recording session at 48 kHz/256 frames had p99 compute time 0.488 ms against a 5.333 ms buffer period, with two measured overruns among 11,156 timed calls. Its physical audio latency was not measured.

## Memory and remaining concerns

Sampled peak resident memory reached **952.63 MiB**. During the large reload phase, RSS increased from 191.79 MiB after the first reload to 826.81 MiB after the twentieth; after engine destruction it remained 871.04 MiB. This run therefore **does not establish bounded long-session memory use**.

Separate diagnostic probes reopened the same synthetic project 30 times. A save/reload probe plateaued around 90.9 MiB; after engine destruction, Linux allocator trimming reduced RSS from 90.8 MiB to 16.0 MiB. Another probe that trimmed between loads remained around 49 MiB and ended near 15 MiB. These observations show allocator retention can materially affect RSS. They do not explain or clear every source of growth in the larger combined workload. Windows multi-hour memory/handle testing and allocation profiling remain necessary.

## Fix and regression coverage

Stress testing exposed a real error: reaching Tracktion's 16-instance channel limit could reject an insertion internally while BTE reported success. BTE now checks capacity before creating sends/returns or loading inserts, uses the engine's actual insertion result, preserves routing on rejection, and shows failures in the UI. The global 32-bus limit and per-channel plugin limit are separate; channel utilities count toward the latter.

The final Debug CTest run passed **all five suites**: 20 core, 114 recording-latency, 72 workflow, 391 bounded-stress and 66 workstation checks (**663 checks**). The independent 80-case latency run and large Release run are additional workloads. Debug logs contain the known missing-ALSA-sequencer warning/assertion from this hardware-free cloud environment; the insert-limit assertions found during the first trial were corrected.

ASIO is enabled in the Windows build configuration using JUCE's bundled headers. Native Windows build success, Apollo/other driver performance, audible monitoring, physical round-trip measurement, disconnect/reconnect behavior, multi-hour stress and unsigned-installer acceptance remain unverified here. Use [Windows acceptance](WINDOWS_ACCEPTANCE.md), [latency setup](RECORDING_LATENCY.md) and [stress reproduction](STRESS_TESTING.md) before claiming a validated release.
