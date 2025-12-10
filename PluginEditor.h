/*
  SHARC Echo/Delay Effect Plugin - Editor Header
  JUCE 8.0.11
*/

#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
class SharcEchoAudioProcessorEditor : public juce::AudioProcessorEditor,
    private juce::Timer
{
public:
    SharcEchoAudioProcessorEditor(SharcEchoAudioProcessor&);
    ~SharcEchoAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;

private:
    SharcEchoAudioProcessor& audioProcessor;

    // Control groups
    struct ControlGroup
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    ControlGroup delayControl;
    ControlGroup feedbackControl;
    ControlGroup wetControl;
    ControlGroup dryControl;

    juce::ToggleButton bypassButton;
    juce::ToggleButton simdButton;
    juce::Label modeLabel;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> simdAttachment;

    // CPU meter
    float currentCpuUsage = 0.0f;

    void setupControl(ControlGroup& control, const juce::String& paramID,
        const juce::String& labelText, juce::Slider::SliderStyle style);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SharcEchoAudioProcessorEditor)
};