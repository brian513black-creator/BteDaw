# Validation — 2026-11-08

This report describes BTE Studio 0.2's Tracktion-based workstation, which replaces the original in-memory prototype. It distinguishes native development validation from Windows release acceptance.

## Build and automated execution

The development-only Linux target compiled using GCC 14.2 and CMake 4.4.4, with JUCE 9.0.5 at commit `7ff644f65d7daa1428c34b9738ebb5c449dc9172` and Tracktion Engine at `b0c7dd090a56b27fe01bb394617891906eb34daf`. Downloads are pinned by SHA-256 in CMake.

CTest completed both suites: **20 core checks and 66 workstation checks, all passing**. The workstation suite exercises:

- WAV import and project-owned media; expected-gain audio through a bus; full +24 dB input trim and its rendered gain.
- Feedback prevention across primary routes and auxiliary sends; audio and bus stem exports.
- Region split/duplicate and undo; mono channel and master-sum state with undo.
- RMS gain staging and undo; automation points that measurably change the rendered signal.
- Auxiliary sends/returns and bundled reverb; built-in 4OSC producing decoded, non-silent rendered audio.
- Assistant preview without mutation, applying an edit and undoing it.
- A real dynamically loaded VST3 fixture: doubling its main input produces the expected samples; enabling its auxiliary sidechain halves the main input and produces the expected samples. This tests actual plugin audio processing, not only stored routing.
- Session and insert-chain restoration, portable copies resolving their own copied media, recovery snapshots, and preserving the edit when a corrupt session is opened.
- The original BTE percussion synth producing audio without samples; a playable six-channel factory example rendering with headroom.
- Mono and stereo disk recording through the engine's hosted audio-device callback: feeding generated input, stopping, creating timeline regions, checking the recorded channel counts, and decoding the files to verify the injected signal.

The tests deliberately failed during development and identified integration defects in empty-session defaults, rendering track selection/progress handling, undo synchronization, stem routing, and disconnected-track render output. Those defects were corrected before the final pass.

## Native interface and installation checks

Launched the native application and generated its built-in six-channel example. Inspected the arrangement, routing, instrument inserts, mixer and master controls. Checked the full 1600×1000 layout and compact 1280×720 layout, where the library hides and the inspector scrolls. The native interface was also exercised for cable routing and assistant preview/apply/undo during development.

A CMake installation into an isolated prefix completed with the executable and dependency license material. The installed **Linux development executable** completed `--smoke-test` and exited with code 0. This establishes installation-layout/startup behavior on this machine, not an installed Windows result.

The cloud machine has no physical audio/MIDI hardware. Hosted-device recording and rendered sample checks establish engine behavior; they do not establish audible WASAPI output, USB MIDI compatibility or low-latency performance. The Linux logs contain the expected missing-ALSA-sequencer warning/debug assertion. No code assertion was disabled to make a test pass.

## Windows workflow and unverified results

The Windows x64 workflow builds with a static Microsoft C runtime, runs the same engine tests, starts the native app, builds NSIS and portable packages, and checks silent installation/startup/uninstallation. It packages corresponding application/dependency source and SHA-256 checksums.

That workflow is configured in source; its successful execution is not established by this local report. GitHub Actions metadata could not be read from the cloud environment: the API connection was denied (403), and the Actions website returned 503. A Git push can be verified separately through the existing Git proxy, but that does not establish CI success or artifact availability.

Actual Windows 11 hardware acceptance, signing, ASIO, plugin isolation and the remaining product work are documented in [WINDOWS_ACCEPTANCE.md](WINDOWS_ACCEPTANCE.md) and the README. The application must not be advertised as a fully tested professional Windows release based on these Linux checks. No environment configuration was republished.
