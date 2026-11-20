#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include <array>
#include <functional>

//==============================================================================
// Draws the animated portion of a gain or peak meter. The component
// is opaque so repainting it never requires JUCE to redraw the static artwork
// behind it.
class GainMeter : public juce::Component
{
public:
    enum class ColourMode
    {
        gain,
        peak
    };

    explicit GainMeter (ColourMode mode = ColourMode::gain);
    ~GainMeter() override = default;

    void setGain (float newGain);
    void paint (juce::Graphics& g) override;

private:
    ColourMode colourMode;
    float gain = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GainMeter)
};

//==============================================================================
// Owns the static meter scale and the animated meter layered on top of it.
// The scale is buffered because it changes only when this component is resized.
class GainMeterBackground : public juce::Component
{
public:
    GainMeterBackground();
    ~GainMeterBackground() override = default;

    void setGain (float newGain);
    void paint (juce::Graphics& g) override;
    void resized() override;

    static constexpr int getScaleWidth() noexcept { return scaleWidth; }

private:
    static constexpr int scaleWidth = 40;
    GainMeter gainMeter;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GainMeterBackground)
};

// Input level meter with a dBFS scale.
class InputMeterBackground : public juce::Component
{
public:
    InputMeterBackground();
    ~InputMeterBackground() override = default;

    void setLevel (float newLevel);
    bool isClipIndicatorActive() const noexcept;
    void paint (juce::Graphics& g) override;
    void resized() override;

    static constexpr int getScaleWidth() noexcept { return scaleWidth; }

private:
    static constexpr int scaleWidth = 32;
    GainMeter meter { GainMeter::ColourMode::peak };
    double clipIndicatorExpiryMs = 0.0;
    bool clipIndicatorActive = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InputMeterBackground)
};

//==============================================================================
// Groups the input name, input and gain meters, and mute control for one channel.
class ChannelStrip : public juce::Component
{
public:
    ChannelStrip();

    void setInputName (const juce::String& inputName);
    void setInputPeak (float newPeak);
    void setGain (float newGain);
    void setMuted (bool shouldBeMuted);
    void resized() override;

    std::function<void (bool)> onMuteChanged;
    std::function<void (const juce::String&)> onInputNameChanged;

private:
    juce::Label inputNameLabel;
    juce::Label inputValueLabel;
    juce::Label gainValueLabel;
    juce::Label inputMeterLabel;
    juce::Label gainMeterLabel;
    InputMeterBackground inputMeter;
    GainMeterBackground gainMeterBackground;
    juce::ToggleButton muteButton { "Mute" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChannelStrip)
};

//==============================================================================
class GainShareAutomixerAudioProcessorEditor  : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    GainShareAutomixerAudioProcessorEditor (GainShareAutomixerAudioProcessor&);
    ~GainShareAutomixerAudioProcessorEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    GainShareAutomixerAudioProcessor& audioProcessor;

    static constexpr int channelCount =
        GainShareAutomixerAudioProcessor::automixerChannelCount;

    std::array<ChannelStrip, channelCount> channelStrips;
    juce::ToggleButton bypassButton { "Automix On" };
    juce::Label outputPeakLabel;
    juce::Label outputPeakMeterLabel;
    InputMeterBackground outputPeakMeter;
    juce::Label outputGainLabel;
    juce::Slider outputGainSlider;

    void timerCallback() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GainShareAutomixerAudioProcessorEditor)
};
