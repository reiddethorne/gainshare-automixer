#include "PluginProcessor.h"

#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
using Inputs = std::array<float, DuganAutomixerAudioProcessor::automixerChannelCount>;

void expectNear (float actual, float expected, float tolerance, const std::string& description)
{
    // Unlike assert(), these checks remain active in Release builds.
    if (! std::isfinite (actual) || std::abs (actual - expected) > tolerance)
        throw std::runtime_error (description + ": expected " + std::to_string (expected)
                                  + ", got " + std::to_string (actual));
}

struct Fixture
{
    Fixture (double rate, int size) : sampleRate (rate), blockSize (size), buffer (4, size)
    {
        processor.prepareToPlay (sampleRate, blockSize);
    }

    ~Fixture() { processor.releaseResources(); }

    template <typename Signal, typename Check>
    void process (double seconds, Signal signal, Check check)
    {
        const auto sampleCount = static_cast<int> (std::ceil (seconds * sampleRate));
        int processed = 0;

        while (processed < sampleCount)
        {
            const auto count = juce::jmin (blockSize, sampleCount - processed);
            buffer.setSize (4, count, false, false, true);

            for (int sample = 0; sample < count; ++sample)
            {
                const auto inputs = signal (samplePosition + sample);
                // The processor overwrites the first two input channels.
                for (int channel = 0; channel < 4; ++channel)
                    buffer.setSample (channel, sample, inputs[static_cast<size_t> (channel)]);
            }

            processor.processBlock (buffer, midi);

            for (int sample = 0; sample < count; ++sample)
            {
                for (int output = 0; output < 2; ++output)
                    if (! std::isfinite (buffer.getSample (output, sample)))
                        throw std::runtime_error ("Non-finite output sample");

                check (samplePosition + sample, buffer.getSample (0, sample),
                       buffer.getSample (1, sample));
            }

            processed += count;
            samplePosition += count;
        }
    }

    void settle (Inputs inputs)
    {
        // One second is independent of block size and allows the 100 ms
        // power envelope and 30 ms gain smoother to settle.
        process (1.0, [inputs] (int) { return inputs; }, [] (int, float, float) {});
    }

    void expectOutput (Inputs inputs, float expected, float tolerance = 0.0001f)
    {
        process (0.1, [inputs] (int) { return inputs; },
                 [expected, tolerance] (int, float left, float right)
                 {
                     expectNear (left, expected, tolerance, "Left output");
                     expectNear (right, expected, tolerance, "Right output");
                 });
    }

    DuganAutomixerAudioProcessor processor;
    double sampleRate;
    int blockSize;
    juce::AudioBuffer<float> buffer;
    juce::MidiBuffer midi;
    int samplePosition = 0;
};

void runTest (const std::string& name, Fixture& f)
{
    if (name == "silence")
    {
        f.expectOutput ({}, 0.0f, 0.0f);
        f.settle ({ 0.1f, 0.2f, 0.3f, 0.4f });
        f.expectOutput ({}, 0.0f, 0.0f);
        for (int channel = 0; channel < 4; ++channel)
            if (! std::isfinite (f.processor.getMeterGain (channel)))
                throw std::runtime_error ("Non-finite gain during silence");
    }
    else if (name == "equal_inputs")
    {
        const Inputs inputs { 0.1f, 0.1f, 0.1f, 0.1f };
        f.settle (inputs);
        f.expectOutput (inputs, 0.1f);
        for (int channel = 0; channel < 4; ++channel)
            expectNear (f.processor.getMeterGain (channel), 0.25f, 0.001f, "Equal-input gain");
    }
    else if (name == "single_input")
    {
        // Exercise every input, including the two channels overwritten by output.
        for (int active = 0; active < 4; ++active)
        {
            Inputs inputs {};
            inputs[static_cast<size_t> (active)] = 0.2f;
            f.settle (inputs);
            f.expectOutput (inputs, 0.2f);
            for (int channel = 0; channel < 4; ++channel)
                expectNear (f.processor.getMeterGain (channel), channel == active ? 1.0f : 0.0f,
                            0.001f, "Single-input gain");
        }
    }
    else if (name == "muted_input")
    {
        for (int muted = 0; muted < 4; ++muted)
        {
            for (int channel = 0; channel < 4; ++channel)
                f.processor.setChannelMuted (channel, false);

            const auto active = (muted + 1) % 4;
            Inputs inputs {};
            inputs[static_cast<size_t> (muted)] = 0.9f;
            inputs[static_cast<size_t> (active)] = 0.1f;
            f.settle (inputs);
            f.processor.setChannelMuted (muted, true);
            f.settle (inputs);
            f.expectOutput (inputs, 0.1f);
            expectNear (f.processor.getMeterGain (muted), 0.0f, 0.0f, "Muted gain");
            expectNear (f.processor.getMeterGain (active), 1.0f, 0.001f, "Unmuted gain");
        }
    }
    else if (name == "bypass")
    {
        const Inputs inputs { 0.1f, 0.2f, -0.05f, 0.025f };
        f.settle (inputs);
        f.processor.setAutomixerBypassed (true);
        f.settle (inputs);
        f.expectOutput (inputs, 0.275f);
        for (int channel = 0; channel < 4; ++channel)
            expectNear (f.processor.getMeterGain (channel), 1.0f, 0.0f, "Bypassed gain");

        // Channel mute must still work while automix is bypassed.
        f.processor.setChannelMuted (1, true);
        f.settle (inputs);
        f.expectOutput (inputs, 0.075f);

        f.processor.setChannelMuted (1, false);
        f.processor.setAutomixerBypassed (false);
        const Inputs equal { 0.1f, 0.1f, 0.1f, 0.1f };
        f.settle (equal);
        f.expectOutput (equal, 0.1f);
    }
    else if (name == "output_trim")
    {
        const Inputs inputs { 0.1f, 0.1f, 0.1f, 0.1f };
        f.settle (inputs);
        for (const auto db : { -6.0f, -24.0f, 6.0f, 0.0f })
        {
            f.processor.setOutputGainDb (db);
            f.settle (inputs);
            const auto expected = 0.1f * std::pow (10.0f, db / 20.0f);
            f.expectOutput (inputs, expected);
        }
    }
    else if (name == "stereo")
    {
        const auto signal = [&f] (int sample)
        {
            const auto time = static_cast<double> (sample) / f.sampleRate;
            return Inputs { static_cast<float> (0.2 * std::sin (2.0 * juce::MathConstants<double>::pi * 173.0 * time)),
                            static_cast<float> (0.1 * std::cos (2.0 * juce::MathConstants<double>::pi * 397.0 * time)),
                            -0.03f, 0.01f };
        };
        const auto check = [] (int, float left, float right)
        {
            expectNear (right, left, 0.0f, "Stereo equality");
        };
        f.process (0.2, signal, check);
        f.processor.setAutomixerBypassed (true);
        f.processor.setChannelMuted (2, true);
        f.processor.setOutputGainDb (-6.0f);
        f.process (0.2, signal, check);
    }
    else
    {
        throw std::runtime_error ("Unknown test: " + name);
    }
}
}

int main (int argc, char* argv[])
{
    if (argc != 4)
    {
        std::cerr << "Usage: DuganAutomixerDspTests <case> <sample-rate> <block-size>\n";
        return 1;
    }

    try
    {
        const auto sampleRate = std::stod (argv[2]);
        const auto blockSize = std::stoi (argv[3]);
        if (! std::isfinite (sampleRate) || sampleRate <= 0.0 || blockSize <= 0)
            throw std::runtime_error ("Sample rate and block size must be positive");

        juce::ScopedJuceInitialiser_GUI initialiseJuce;
        Fixture fixture (sampleRate, blockSize);
        runTest (argv[1], fixture);
        std::cout << "PASS: " << argv[1] << " at " << sampleRate << " Hz / " << blockSize << " samples\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << argv[1] << " (" << argv[2] << " Hz / " << argv[3]
                  << " samples): " << error.what() << '\n';
        return 1;
    }
}
