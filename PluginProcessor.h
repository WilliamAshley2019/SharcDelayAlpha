/*
  SHARC Echo/Delay Effect Plugin - FIXED & OPTIMIZED
  JUCE 8.0.11 - Stable Delay Algorithm + Efficient SIMD

  Critical Fixes:
  - Stable delay formula: input + (feedback * delayed)
  - Optimized SIMD (no modulo in inner loop)
  - Smoothed CPU measurement
  - Hard clipping to prevent overflow
*/

#pragma once
#include <JuceHeader.h>

//==============================================================================
// SHARC-style Stereo Delay Line (CORRECTED STABLE ALGORITHM)
// Formula: buffer[n] = input[n] + (feedback * delayed[n-M])
//          output[n] = (input[n] * dry) + (delayed[n-M] * wet)
//==============================================================================
class SharcDelayLine
{
public:
    SharcDelayLine() = default;

    void prepare(double sRate, float maxDelaySeconds = 5.0f)
    {
        this->sRate = sRate;

        // Calculate max delay line size
        maxDelaySamples = static_cast<int>(sRate * maxDelaySeconds);

        // Allocate delay buffers
        delayLineLeft.resize(static_cast<size_t>(maxDelaySamples));
        delayLineRight.resize(static_cast<size_t>(maxDelaySamples));

        // Zero delay lines
        reset();
        prepared = true;
    }

    void setDelaySeconds(float seconds)
    {
        delaySamples = static_cast<int>(seconds * sRate);
        delaySamples = juce::jlimit(1, maxDelaySamples, delaySamples);
    }

    void setFeedback(float fb)
    {
        feedback = juce::jlimit(0.0f, 0.99f, fb);
    }

    void setWetMix(float wet)
    {
        wetMix = juce::jlimit(0.0f, 1.0f, wet);
    }

    void setDryMix(float dry)
    {
        dryMix = juce::jlimit(0.0f, 1.0f, dry);
    }

    void reset()
    {
        std::fill(delayLineLeft.begin(), delayLineLeft.end(), 0.0f);
        std::fill(delayLineRight.begin(), delayLineRight.end(), 0.0f);
        delayIndex = 0;
    }

    // Scalar version - CORRECTED STABLE FORMULA
    void processBlockScalar(const float* inputLeft, const float* inputRight,
        float* outputLeft, float* outputRight, int numSamples) noexcept
    {
        if (!prepared) return;

        auto* bufferLeft = delayLineLeft.data();
        auto* bufferRight = delayLineRight.data();
        int idx = delayIndex;
        const int len = delaySamples;
        const float fb = feedback;
        const float wet = wetMix;
        const float dry = dryMix;

        for (int i = 0; i < numSamples; ++i)
        {
            // 1. Read delayed sample
            const float delayedLeft = bufferLeft[idx];
            const float delayedRight = bufferRight[idx];

            // 2. Mix and output
            outputLeft[i] = (inputLeft[i] * dry) + (delayedLeft * wet);
            outputRight[i] = (inputRight[i] * dry) + (delayedRight * wet);

            // 3. STABLE FORMULA: input + (feedback * delayed)
            //    This ensures exponential decay, not growth
            float newLeft = inputLeft[i] + (fb * delayedLeft);
            float newRight = inputRight[i] + (fb * delayedRight);

            // 4. Hard clip to prevent overflow (safety)
            bufferLeft[idx] = juce::jlimit(-1.0f, 1.0f, newLeft);
            bufferRight[idx] = juce::jlimit(-1.0f, 1.0f, newRight);

            // 5. Circular buffer wraparound
            if (++idx >= len)
                idx = 0;
        }

        delayIndex = idx;
    }

    // SIMD version - OPTIMIZED (no modulo in inner loop!)
    void processBlockSIMD(const float* inputLeft, const float* inputRight,
        float* outputLeft, float* outputRight, int numSamples) noexcept
    {
        if (!prepared) return;

        using SIMD = juce::dsp::SIMDRegister<float>;
        constexpr int simdWidth = static_cast<int>(SIMD::SIMDRegister::size());

        auto* bufferLeft = delayLineLeft.data();
        auto* bufferRight = delayLineRight.data();
        int idx = delayIndex;
        const int len = delaySamples;
        const float fb = feedback;
        const float wet = wetMix;
        const float dry = dryMix;

        // Pre-load constants into SIMD registers
        const SIMD dryVec(dry);
        const SIMD wetVec(wet);
        const SIMD fbVec(fb);
        const SIMD clipMin(-1.0f);
        const SIMD clipMax(1.0f);

        int samplesRemaining = numSamples;

        // Process in chunks that don't cross buffer boundary
        while (samplesRemaining > 0)
        {
            // Calculate samples until buffer edge (no modulo needed!)
            const int samplesToEdge = juce::jmin(samplesRemaining, len - idx);
            const int simdChunks = samplesToEdge / simdWidth;
            const int scalarTail = samplesToEdge % simdWidth;

            // SIMD main loop - contiguous memory access
            for (int chunk = 0; chunk < simdChunks; ++chunk)
            {
                const int offset = idx + (chunk * simdWidth);

                // Load from contiguous memory (fast!)
                SIMD delayedLeftVec = SIMD::fromRawArray(bufferLeft + offset);
                SIMD delayedRightVec = SIMD::fromRawArray(bufferRight + offset);
                SIMD inputLeftVec = SIMD::fromRawArray(inputLeft);
                SIMD inputRightVec = SIMD::fromRawArray(inputRight);

                // Mix and output
                SIMD outL = inputLeftVec * dryVec + delayedLeftVec * wetVec;
                SIMD outR = inputRightVec * dryVec + delayedRightVec * wetVec;
                outL.copyToRawArray(outputLeft);
                outR.copyToRawArray(outputRight);

                // Update delay lines with STABLE FORMULA
                SIMD newL = inputLeftVec + (delayedLeftVec * fbVec);
                SIMD newR = inputRightVec + (delayedRightVec * fbVec);

                // Hard clip (SIMD max/min)
                newL = juce::jmax(clipMin, juce::jmin(clipMax, newL));
                newR = juce::jmax(clipMin, juce::jmin(clipMax, newR));

                newL.copyToRawArray(bufferLeft + offset);
                newR.copyToRawArray(bufferRight + offset);

                // Advance input/output pointers
                inputLeft += simdWidth;
                inputRight += simdWidth;
                outputLeft += simdWidth;
                outputRight += simdWidth;
            }

            // Update index after SIMD chunks
            idx += simdChunks * simdWidth;

            // Scalar tail (remaining samples before edge)
            for (int i = 0; i < scalarTail; ++i)
            {
                const float delayedLeft = bufferLeft[idx];
                const float delayedRight = bufferRight[idx];

                *outputLeft = (*inputLeft * dry) + (delayedLeft * wet);
                *outputRight = (*inputRight * dry) + (delayedRight * wet);

                float newLeft = *inputLeft + (fb * delayedLeft);
                float newRight = *inputRight + (fb * delayedRight);

                bufferLeft[idx] = juce::jlimit(-1.0f, 1.0f, newLeft);
                bufferRight[idx] = juce::jlimit(-1.0f, 1.0f, newRight);

                ++inputLeft;
                ++inputRight;
                ++outputLeft;
                ++outputRight;
                ++idx;
            }

            samplesRemaining -= samplesToEdge;

            // Wrap only at buffer edge (once per chunk)
            if (idx >= len)
                idx = 0;
        }

        delayIndex = idx;
    }

private:
    std::vector<float> delayLineLeft;
    std::vector<float> delayLineRight;
    int maxDelaySamples = 240000;
    int delaySamples = 48000;
    int delayIndex = 0;

    float feedback = 0.3f;
    float wetMix = 0.5f;
    float dryMix = 0.5f;

    double sRate = 48000.0;
    bool prepared = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SharcDelayLine)
};

//==============================================================================
// Main Plugin Processor
//==============================================================================
class SharcEchoAudioProcessor : public juce::AudioProcessor
{
public:
    SharcEchoAudioProcessor();
    ~SharcEchoAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "SHARC Echo"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 5.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getAPVTS() { return apvts; }
    float getCpuUsage() const { return smoothedCpuUsage.getCurrentValue(); }

private:
    juce::AudioProcessorValueTreeState apvts;
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    SharcDelayLine delayLine;

    double currentSampleRate = 48000.0;
    bool useSIMD = false;

    // Smoothed CPU measurement (500ms smoothing)
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedCpuUsage;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SharcEchoAudioProcessor)
};