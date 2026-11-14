#include "PluginProcessor.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
using Clock = std::chrono::steady_clock;
constexpr int channelCount = GainShareAutomixerAudioProcessor::automixerChannelCount;
constexpr double warmupSeconds = 2.0;

enum class Scenario { silence, fourInputs, changingSpeakers, controlChanges };

const char* scenarioName (Scenario scenario)
{
    switch (scenario)
    {
        case Scenario::silence:          return "silence";
        case Scenario::fourInputs:       return "four_inputs";
        case Scenario::changingSpeakers: return "changing_speakers";
        case Scenario::controlChanges:   return "control_changes";
    }
    throw std::runtime_error ("Unknown scenario");
}

double percentile (const std::vector<double>& sorted, double fraction)
{
    // Nearest-rank percentile; the input always has at least one measurement.
    const auto rank = static_cast<size_t> (std::ceil (fraction * static_cast<double> (sorted.size())));
    return sorted[rank - 1];
}

void benchmark (Scenario scenario, int sampleRate, int blockSize, int iterations)
{
    GainShareAutomixerAudioProcessor processor;
    juce::AudioBuffer<float> buffer (channelCount, blockSize);
    juce::MidiBuffer midi;
    std::vector<double> timings (static_cast<size_t> (iterations));

    // Precompute one second of periodic input, before any measurements.
    std::array<std::vector<float>, channelCount> source;
    const std::array<double, channelCount> frequencies { 173.0, 397.0, 613.0, 997.0 };
    for (size_t channel = 0; channel < source.size(); ++channel)
    {
        source[channel].resize (static_cast<size_t> (sampleRate));
        for (int sample = 0; sample < sampleRate; ++sample)
            source[channel][static_cast<size_t> (sample)] = static_cast<float> (
                0.2 * std::sin (2.0 * juce::MathConstants<double>::pi * frequencies[channel]
                                * static_cast<double> (sample) / sampleRate));
    }

    processor.prepareToPlay (sampleRate, blockSize);
    std::uint64_t samplePosition = 0;
    std::uint64_t nextControlSample = 0;
    std::uint64_t controlStep = 0;
    double checksum = 0.0;

    const auto fillInputsAndControls = [&]
    {
        // Setters and input preparation are outside the measured callback.
        // Controls change roughly every 20 ms, exercising the smoothing ramps.
        if (scenario == Scenario::controlChanges && samplePosition >= nextControlSample)
        {
            processor.setAutomixerBypassed ((controlStep % 2) != 0);
            processor.setOutputGainDb ((controlStep % 2) != 0 ? -12.0f : 6.0f);
            for (int channel = 0; channel < channelCount; ++channel)
                processor.setChannelMuted (channel,
                    static_cast<std::uint64_t> (channel) == controlStep % channelCount);
            ++controlStep;
            nextControlSample = samplePosition + static_cast<std::uint64_t> (sampleRate / 50);
        }

        const auto speaker = (samplePosition / static_cast<std::uint64_t> (sampleRate / 4))
                             % channelCount;
        for (size_t channel = 0; channel < source.size(); ++channel)
        {
            auto* destination = buffer.getWritePointer (static_cast<int> (channel));
            const auto amplitude = scenario == Scenario::silence ? 0.0f
                : (scenario == Scenario::changingSpeakers && channel != speaker ? 0.05f : 1.0f);
            for (int sample = 0; sample < blockSize; ++sample)
            {
                const auto index = (samplePosition + static_cast<std::uint64_t> (sample))
                                   % static_cast<std::uint64_t> (sampleRate);
                destination[sample] = amplitude * source[channel][static_cast<size_t> (index)];
            }
        }
    };

    const auto consumeOutput = [&]
    {
        // Make the computed audio observable even with link-time optimisation.
        // This work, including timing storage, is outside the timed section.
        const auto output = buffer.getSample (0, blockSize - 1);
        if (! std::isfinite (output))
            throw std::runtime_error ("Non-finite benchmark output");
        checksum += output;
        samplePosition += static_cast<std::uint64_t> (blockSize);
    };

    const auto warmupBlocks = static_cast<int> (
        std::ceil (warmupSeconds * sampleRate / blockSize));
    for (int block = 0; block < warmupBlocks; ++block)
    {
        fillInputsAndControls();
        processor.processBlock (buffer, midi);
        consumeOutput();
    }

    for (int block = 0; block < iterations; ++block)
    {
        fillInputsAndControls();
        const auto start = Clock::now();
        processor.processBlock (buffer, midi);
        const auto end = Clock::now();
        timings[static_cast<size_t> (block)] =
            std::chrono::duration<double, std::micro> (end - start).count();
        consumeOutput();
    }
    processor.releaseResources();

    const auto deadlineUs = 1.0e6 * blockSize / sampleRate;
    const auto overDeadline = std::count_if (timings.begin(), timings.end(),
        [deadlineUs] (double time) { return time > deadlineUs; });
    std::sort (timings.begin(), timings.end());
    const auto maximum = timings.back();
    std::cout << scenarioName (scenario) << ',' << sampleRate << ',' << blockSize << ','
              << iterations << ',' << deadlineUs << ',' << percentile (timings, 0.5) << ','
              << percentile (timings, 0.95) << ',' << percentile (timings, 0.99) << ','
              << maximum << ',' << 100.0 * maximum / deadlineUs << ',' << overDeadline
              << ',' << checksum << '\n';
}
}

int main (int argc, char* argv[])
{
    try
    {
        int iterations = 10000;
        if (argc == 2 && std::string (argv[1]) == "--help")
        {
            std::cout << "Usage: GainShareAutomixerDspBenchmark [callbacks-per-scenario]\n"
                         "Default: 10000; range: 1000 to 1000000. Emits CSV to stdout.\n";
            return 0;
        }
        if (argc > 2)
            throw std::runtime_error ("Expected at most one callback-count argument; use --help");
        if (argc == 2)
        {
            size_t consumed = 0;
            const std::string argument (argv[1]);
            iterations = std::stoi (argument, &consumed);
            if (consumed != argument.size())
                throw std::runtime_error ("Callback count must be an integer");
        }
        if (iterations < 1000 || iterations > 1000000)
            throw std::runtime_error ("Callback count must be between 1000 and 1000000");

       #ifndef NDEBUG
        throw std::runtime_error ("Build the benchmark in Release mode for representative timings");
       #endif

        juce::ScopedJuceInitialiser_GUI initialiseJuce;
        std::cout << "# CPU: " << juce::SystemStats::getCpuModel() << '\n'
                  << "# OS: " << juce::SystemStats::getOperatingSystemName() << '\n'
                  << "# " << juce::SystemStats::getJUCEVersion() << "; Release; "
                  << (sizeof (void*) * 8) << " bit; steady_clock period: "
                  << 1.0e9 * static_cast<double> (Clock::period::num) / Clock::period::den
                  << " ns\n"
                  << "# Warm-up: 2 seconds of audio per row; callbacks run without pacing.\n"
                  << "# Timings include clock overhead and OS interruptions; no UI, driver, or concurrent meter reader.\n"
                  << "scenario,sample_rate,buffer_size,callbacks,deadline_us,median_us,p95_us,p99_us,max_us,max_deadline_percent,over_deadline,checksum\n"
                  << std::fixed << std::setprecision (3);

        for (const auto sampleRate : { 44100, 48000, 96000 })
            for (const auto blockSize : { 32, 128, 512 })
                for (const auto scenario : { Scenario::silence, Scenario::fourInputs,
                                            Scenario::changingSpeakers, Scenario::controlChanges })
                    benchmark (scenario, sampleRate, blockSize, iterations);
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Benchmark error: " << error.what() << '\n';
        return 1;
    }
}
