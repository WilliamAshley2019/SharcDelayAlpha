/*
  SHARC Echo/Delay Effect Plugin - Editor Implementation (OPTIMIZED)
  JUCE 8.0.11
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
SharcEchoAudioProcessorEditor::SharcEchoAudioProcessorEditor(SharcEchoAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    setSize(500, 350);

    // Setup controls
    setupControl(delayControl, "delay", "Delay Time", juce::Slider::RotaryVerticalDrag);
    setupControl(feedbackControl, "feedback", "Feedback", juce::Slider::RotaryVerticalDrag);
    setupControl(wetControl, "wet", "Wet Mix", juce::Slider::LinearVertical);
    setupControl(dryControl, "dry", "Dry Mix", juce::Slider::LinearVertical);

    // Bypass button
    addAndMakeVisible(bypassButton);
    bypassButton.setButtonText("Bypass");
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.getAPVTS(), "bypass", bypassButton);

    // SIMD mode toggle
    addAndMakeVisible(simdButton);
    simdButton.setButtonText("Use SIMD (Low CPU)");
    simdAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.getAPVTS(), "simd", simdButton);

    // Mode label
    addAndMakeVisible(modeLabel);
    modeLabel.setText("Processing Mode:", juce::dontSendNotification);
    modeLabel.setJustificationType(juce::Justification::centredLeft);
    modeLabel.setFont(juce::FontOptions(14.0f, juce::Font::bold));

    // Start timer for CPU monitoring (30Hz refresh rate)
    startTimerHz(30);
}

SharcEchoAudioProcessorEditor::~SharcEchoAudioProcessorEditor()
{
    stopTimer();
}

//==============================================================================
void SharcEchoAudioProcessorEditor::setupControl(ControlGroup& control,
    const juce::String& paramID,
    const juce::String& labelText,
    juce::Slider::SliderStyle style)
{
    addAndMakeVisible(control.slider);
    control.slider.setSliderStyle(style);
    control.slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 20);

    addAndMakeVisible(control.label);
    control.label.setText(labelText, juce::dontSendNotification);
    control.label.setJustificationType(juce::Justification::centred);
    control.label.attachToComponent(&control.slider, false);

    control.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), paramID, control.slider);
}

//==============================================================================
void SharcEchoAudioProcessorEditor::paint(juce::Graphics& g)
{
    // Dark background (SHARC hardware aesthetic)
    g.fillAll(juce::Colour(0xff1a1d2a));

    auto bounds = getLocalBounds();

    // Header
    auto headerArea = bounds.removeFromTop(70);
    g.setGradientFill(juce::ColourGradient(
        juce::Colour(0xff2a3a4a), 0.0f, 0.0f,
        juce::Colour(0xff1a2a3a), 0.0f, static_cast<float>(headerArea.getHeight()), false));
    g.fillRect(headerArea);

    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(32.0f, juce::Font::bold));
    g.drawFittedText("SHARC ECHO", headerArea.reduced(10, 0),
        juce::Justification::centred, 1);

    g.setFont(juce::FontOptions(13.0f));
    g.drawFittedText("Analog Devices ADSP-21569 Algorithm",
        headerArea.reduced(10, 40).removeFromBottom(18),
        juce::Justification::centred, 1);

    // Control panel
    auto controlArea = bounds.removeFromTop(200);
    g.setColour(juce::Colour(0xff2a2d3a));
    g.fillRoundedRectangle(controlArea.reduced(10, 5).toFloat(), 8.0f);

    // Footer
    auto footerArea = bounds;
    g.setColour(juce::Colour(0xff0a0d1a));
    g.fillRect(footerArea.reduced(10, 5));

    // CPU meter with color coding
    bool usingSIMD = *audioProcessor.getAPVTS().getRawParameterValue("simd") > 0.5f;
    float cpuPercent = currentCpuUsage * 100.0f;

    // Color code based on CPU usage
    juce::Colour cpuColor = juce::Colours::lime;
    if (cpuPercent > 50.0f)
        cpuColor = juce::Colours::orange;
    if (cpuPercent > 75.0f)
        cpuColor = juce::Colours::red;

    juce::String cpuText = juce::String("CPU: ") + juce::String(cpuPercent, 1) + "% | "
        + (usingSIMD ? "SIMD Mode (Optimized)" : "Scalar Mode (Authentic)");

    g.setColour(cpuColor);
    g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
    g.drawText(cpuText, footerArea.reduced(15, 8), juce::Justification::centredLeft);

    // Info text
    g.setColour(juce::Colours::grey);
    g.setFont(juce::FontOptions(10.0f));
    g.drawText("Max Delay: 5.0s (240k samples @ 48kHz) | Stable Feedback Algorithm",
        footerArea.reduced(15, 30), juce::Justification::centredLeft);

    // Dividers
    g.setColour(juce::Colour(0xff4a5a6a).withAlpha(0.3f));
    g.drawLine(10.0f, 70.0f, static_cast<float>(getWidth()) - 10.0f, 70.0f, 1.0f);
    g.drawLine(10.0f, static_cast<float>(getHeight()) - 75.0f,
        static_cast<float>(getWidth()) - 10.0f,
        static_cast<float>(getHeight()) - 75.0f, 1.0f);
}

//==============================================================================
void SharcEchoAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds();
    bounds.removeFromTop(75); // Skip header

    // Control sliders
    auto controlArea = bounds.removeFromTop(190).reduced(25, 15);

    // Rotary controls (Delay, Feedback)
    auto rotaryArea = controlArea.removeFromLeft(280);
    delayControl.slider.setBounds(rotaryArea.removeFromLeft(130));
    rotaryArea.removeFromLeft(20);
    feedbackControl.slider.setBounds(rotaryArea.removeFromLeft(130));

    // Vertical sliders (Wet, Dry)
    controlArea.removeFromLeft(20);
    wetControl.slider.setBounds(controlArea.removeFromLeft(50));
    controlArea.removeFromLeft(20);
    dryControl.slider.setBounds(controlArea.removeFromLeft(50));

    // Footer controls
    auto footerArea = bounds.removeFromTop(65).reduced(20, 10);
    modeLabel.setBounds(footerArea.removeFromTop(20));

    auto buttonArea = footerArea.removeFromTop(25);
    bypassButton.setBounds(buttonArea.removeFromLeft(100));
    buttonArea.removeFromLeft(15);
    simdButton.setBounds(buttonArea.removeFromLeft(180));
}

//==============================================================================
void SharcEchoAudioProcessorEditor::timerCallback()
{
    // Update CPU usage
    currentCpuUsage = audioProcessor.getCpuUsage();

    // Only repaint footer area for efficiency
    auto footerBounds = getLocalBounds().removeFromBottom(75);
    repaint(footerBounds);
}