# Developer guide

This guide covers the signal processing, automated tests, and callback benchmark. For build prerequisites and application setup, see the [project README](../README.md).

## How the gain sharing works

Each input is squared and passed through an attack/release envelope follower. The gain for a channel is its share of the total measured power:

```text
P_i[n] = alpha * P_i[n-1] + (1 - alpha) * x_i[n]^2

gain_i[n] = (P_i[n] + epsilon) / (sum(P_j[n]) + 4 * epsilon)
```

With four equally powered inputs, each settles near a gain of `0.25` (`-12 dB`). When one input dominates, its gain approaches `1.0` while the others are attenuated.

The envelope uses a 10 ms attack and 100 ms release, followed by 30 ms gain smoothing. Muted channels are removed from the power calculation so a loud muted microphone cannot suppress the remaining inputs.

## Real-time design

The part I found most interesting was keeping the audio callback separate from the interface:

- DSP state uses fixed-size arrays, and `processBlock` does not explicitly allocate memory, access files, or acquire a mutex.
- The audio thread publishes meter values through atomics. The UI reads them at 30 Hz and clears stored peaks after each read.
- Input names are protected separately because the audio callback never needs to read them.
- Mute and bypass changes are faded to avoid abrupt discontinuities.
- Application state is serialized as XML for recall between runs.

## DSP tests

The console test runner processes generated signals directly through the processor; no audio interface is needed. It covers silence, equal inputs, a single active input, muted-input isolation, bypass, output trim, and identical stereo output at 44.1, 48, and 96 kHz with 32, 128, and 512 sample buffers (63 test cases).

Run these commands from the repository root after configuring the build directory as described in the README:

```sh
cmake --build build/macos --config Release --target DuganAutomixerDspTests
ctest --test-dir build/macos -C Release --output-on-failure
```

On Windows, substitute `build/windows` for `build/macos`. Tests are enabled by default; configure with `-DBUILD_TESTING=OFF` to omit the test target. Both CI platforms run the tests in Release builds.

## Callback benchmark

The separate `DuganAutomixerDspBenchmark` target measures `processBlock()` in Release mode. It runs silence, four active inputs, changing speakers, and mute/bypass/output-gain changes across the same nine sample-rate/buffer-size combinations as the tests. Each scenario warms up with two seconds of generated audio, then measures 10,000 callbacks by default. Inputs and timing storage are preallocated; signal preparation, control setters, and reporting are outside the timed section.

From the repository root, after configuring the macOS Xcode build described in the README:

```sh
cmake --build build/macos --config Release --target DuganAutomixerDspBenchmark
./build/macos/benchmarks/Release/DuganAutomixerDspBenchmark > build/macos/benchmark.csv
```

On Windows, build the same target in `build/windows`, then run:

```powershell
./build/windows/benchmarks/Release/DuganAutomixerDspBenchmark.exe > build/windows/benchmark.csv
```

Pass a callback count (for example, `DuganAutomixerDspBenchmark 100000`) for a longer run. The output contains machine metadata followed by CSV columns for median, p95, p99, maximum observed callback time, the callback deadline (`buffer size / sample rate`), maximum time as a percentage of that deadline, and the number of measurements exceeding it. Times are in microseconds. Lines starting with `#` are metadata; the checksum keeps processed audio observable during optimisation.

Run on an otherwise quiet machine and repeat to assess variation. The timer overhead is included, and operating-system interruptions can inflate individual measurements. This is an offline DSP benchmark: callbacks run as fast as possible, without an audio driver, UI, or concurrent meter reader. A maximum observed time is not a guaranteed worst case, and an over-deadline measurement is not a recorded hardware dropout. Validate live playback separately with your interface, meters, and controls. The benchmark is excluded from the default build and CTest/CI performance thresholds.

### Reading the benchmark results

The callback deadline is the time represented by one buffer: `buffer size / sample rate`. For example, 32 samples at 96 kHz gives about 333 microseconds for the whole callback path. The processor shares that time with driver and other audio overhead.

- **Median:** half the measured callbacks took this long or less.
- **p95 and p99:** 95% and 99% of callbacks took this long or less.
- **Maximum:** the slowest callback observed during this run.
- **Over deadline:** how many measured callbacks took longer than one buffer of audio.

Compare each row with its own deadline. A shorter deadline leaves less time, while a larger buffer also requires more processing. These measurements help identify performance problems; they do not guarantee live playback reliability.

## Code guide

- [`Source/PluginProcessor.cpp`](../Source/PluginProcessor.cpp): DSP, metering, bus layout, and state recall
- [`Source/PluginProcessor.h`](../Source/PluginProcessor.h): processor state and audio/UI communication
- [`Source/PluginEditor.cpp`](../Source/PluginEditor.cpp): controls, meters, and interface layout
- [`Tests/DspTests.cpp`](../Tests/DspTests.cpp): signal-based DSP regression tests
- [`Benchmarks/DspBenchmark.cpp`](../Benchmarks/DspBenchmark.cpp): offline callback timing benchmark
