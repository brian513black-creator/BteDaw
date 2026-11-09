# Recording latency and low-delay monitoring

BTE distinguishes **monitoring delay**, **driver-reported latency** and **recorded-track alignment**. None should be labelled simply “zero latency.”

## Apollo Twin Thunderbolt / Twin X on Windows 11

Install Universal Audio's supported Windows Thunderbolt driver and UAD Console for the exact Apollo model and compatible PC. A USB-C connector alone does not establish Thunderbolt compatibility. Use the appropriate Thunderbolt connection and UA-supported configuration.

In BTE's **Audio** window, select **ASIO** and the Universal Audio driver. Windows builds now enable JUCE's native ASIO backend using its bundled Steinberg ASIO headers under the GPLv3 option. WASAPI remains available for other devices. This source change still needs successful Windows build and hardware acceptance evidence; it is not proof that the Apollo has been tested here.

For the shortest live monitoring path with an Apollo, monitor the mic/instrument through **UAD Console**, including supported Console DSP effects, and leave BTE's track **I** input-monitor button off. BTE can still arm and record the input. This avoids hearing a second path delayed by the DAW. Native host plugins and their reported delay are part of the software-monitoring path; Console's own converter/DSP path also has nonzero latency.

## Other Windows audio devices

Prefer the manufacturer's native ASIO driver when available. Otherwise choose the device's supported WASAPI mode and settings. BTE's **Use small supported buffer** button requests 64 frames if offered, otherwise the smallest supported size from 64 through 256; if the driver offers only smaller sizes, it uses the largest below 64. It preserves sample rate and uses the driver's supported list. It refuses changes during recording and reports the buffer the driver actually opened.

At 48 kHz, one buffer is 1.33 ms at 64 frames, 2.67 ms at 128, and 10.67 ms at 512. Those are buffer periods, **not measured physical round-trip times**. Smaller buffers require the engine to finish sooner and can produce crackles under load. Increase the buffer, reduce live processing, or use hardware direct monitoring when needed. Bluetooth, system routing, converters and plugins can add delay beyond a simple buffer calculation.

The Audio window shows sample rate/buffer period, driver-reported input/output delays, their sum, and the active graph's compensation delay. Physical round trip remains marked **not measured** until measured externally. A driver reporting zero does not establish zero physical latency.

## Align recorded takes

The engine automatically compensates recording using reported input/output and graph latency. A per-input **Correction** in the Audio window covers any remaining measured offset. Positive values move newly recorded material earlier; negative values move it later. This is a saved device preference and does not alter existing clips or make live monitoring faster. Changes are blocked during recording.

To calibrate with physical hardware:

1. Use a short reference impulse/click on the timeline, at a moderate level, and record a spare line output connected to a spare line input. Disable monitoring of the return in both BTE and the interface mixer so the loopback cannot feed itself.
2. Compare the recorded impulse with the reference on the timeline. Repeat to check consistency at the actual sample rate, buffer and plugin configuration.
3. If the recorded take is late by `N` samples, add `1000 * N / sampleRate` milliseconds to that input's correction. If it is early, subtract that value. Record again to verify. Reset correction when moving to a different driver/configuration unless the calibration remains applicable.
4. Evaluate actual audible/physical delay separately with an appropriate round-trip measurement. A compensated take aligned to the timeline is not a physical latency measurement.

## Automated validation

`bte_latency_tests` runs a causal generated loopback: the next callback receives previous output, and the hosted device adds known input/output delays. It records a real reference impulse to disk and compares the decoded recorded sample with the reference timeline. It covers 44.1/48/96 kHz, 64/128/256-frame buffers, automatic compensation, a real 128-sample DSP delay fixture, residual-offset calibration, and a negative control that cancels compensation and must stay late. Default CTest runs the matrix once; `--repeat 10` runs 80 record/reconfigure cases.

```powershell
& './build/bte_latency_tests_artefacts/Release/bte_latency_tests.exe' --repeat 10
```

These are synthetic engine tests, not Apollo hardware or Windows driver measurements. The separate [stress workload](STRESS_TESTING.md) measures engine callback compute deadlines under a large session. A tight sample-alignment result can coexist with insufficient CPU time at a small buffer. See the stress results and [Windows acceptance checks](WINDOWS_ACCEPTANCE.md) before making release or low-latency compatibility claims.
