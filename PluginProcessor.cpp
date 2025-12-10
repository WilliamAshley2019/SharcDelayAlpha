/*
  SHARC Echo/Delay Effect Plugin - Implementation (FIXED & OPTIMIZED)
  JUCE 8.0.11
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
SharcEchoAudioProcessor::SharcEchoAudioProcessor()
    : AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
    apvts(*this, nullptr, "PARAMS", createParameterLayout())
{
}

SharcEchoAudioProcessor::~SharcEchoAudioProcessor()
{
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout SharcEchoAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // Use AudioParameterFloat with Attributes for JUCE 8
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("delay", 1), "Delay Time",
        juce::NormalisableRange<float>(0.001f, 5.0f, 0.001f, 0.3f), 1.0f,
        juce::AudioParameterFloatAttributes().withLabel("s")));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("feedback", 1), "Feedback",
        juce::NormalisableRange<float>(0.0f, 0.99f, 0.01f), 0.3f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("wet", 1), "Wet Mix",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.5f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("dry", 1), "Dry Mix",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.5f));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID("bypass", 1), "Bypass", false));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID("simd", 1), "Use SIMD", false));

    return { params.begin(), params.end() };
}

//==============================================================================
void SharcEchoAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;

    // Prepare delay line
    delayLine.prepare(sampleRate, 5.0f);

    // Initialize smoothed CPU measurement (500ms smoothing time)
    smoothedCpuUsage.reset(sampleRate, 0.5);
    smoothedCpuUsage.setCurrentAndTargetValue(0.0f);
}

void SharcEchoAudioProcessor::releaseResources()
{
}

bool SharcEchoAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    // Only stereo input/output
    return layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
        && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

//==============================================================================
void SharcEchoAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    // CPU monitoring start
    auto startTime = juce::Time::getMillisecondCounterHiRes();

    auto numSamples = buffer.getNumSamples();

    // Bypass
    if (*apvts.getRawParameterValue("bypass") > 0.5f)
        return;

    // Get parameters
    float delaySeconds = *apvts.getRawParameterValue("delay");
    float feedback = *apvts.getRawParameterValue("feedback");
    float wetMix = *apvts.getRawParameterValue("wet");
    float dryMix = *apvts.getRawParameterValue("dry");
    useSIMD = *apvts.getRawParameterValue("simd") > 0.5f;

    // Update delay line parameters
    delayLine.setDelaySeconds(delaySeconds);
    delayLine.setFeedback(feedback);
    delayLine.setWetMix(wetMix);
    delayLine.setDryMix(dryMix);

    // Get audio pointers
    const float* inputLeft = buffer.getReadPointer(0);
    const float* inputRight = buffer.getReadPointer(1);
    float* outputLeft = buffer.getWritePointer(0);
    float* outputRight = buffer.getWritePointer(1);

    // Process with optimized SIMD or scalar
    if (useSIMD)
    {
        delayLine.processBlockSIMD(inputLeft, inputRight, outputLeft, outputRight, numSamples);
    }
    else
    {
        delayLine.processBlockScalar(inputLeft, inputRight, outputLeft, outputRight, numSamples);
    }

    // Update smoothed CPU usage
    auto endTime = juce::Time::getMillisecondCounterHiRes();
    double blockTime = (endTime - startTime) / 1000.0;
    double expectedBlockTime = static_cast<double>(numSamples) / currentSampleRate;
    float instantCpu = static_cast<float>(juce::jmin(1.0, blockTime / expectedBlockTime));

    smoothedCpuUsage.setTargetValue(instantCpu);
    smoothedCpuUsage.skip(1); // Advance smoother by 1 sample
}

//==============================================================================
juce::AudioProcessorEditor* SharcEchoAudioProcessor::createEditor()
{
    return new SharcEchoAudioProcessorEditor(*this);
}

//==============================================================================
void SharcEchoAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void SharcEchoAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));

    if (xmlState != nullptr)
        if (xmlState->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xmlState));
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SharcEchoAudioProcessor();
}