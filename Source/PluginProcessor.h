#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <vector>
#include "FXAudioVisualState.h"
#include "DistortionEngines.h"

struct DistortionSnapshot
{
    float primary = 0.0f;
    float secondary = 0.0f;
    float tertiary = 0.0f;
    float quaternary = 0.0f;
    float clipPeak = 0.0f;
    int engine = 0;
    int variant = 0;
    bool oversampled = false;
    bool clipped = false;
};

class MusiqueDistortionProcessor : public juce::AudioProcessor
{
public:
    enum EngineIndex
    {
        distortion = 0,
        overdrive,
        fuzz,
        exciter,
        bitCrusher,
        consoleSaturation,
        numEngines
    };

    MusiqueDistortionProcessor();
    ~MusiqueDistortionProcessor() override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    static juce::StringArray getAllParameterIds();
    static void normalisePresetObject(juce::var& preset);

    void prepareToPlay(double, int) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override
    {
#if MUSIQUE_DISTORTION_DSP_TESTS
        return "Musique Distortion";
#else
        return JucePlugin_Name;
#endif
    }

    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;
    juce::AudioProcessorParameter* getBypassParameter() const override;

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    juce::AudioProcessorValueTreeState& getAPVTS() noexcept { return parameters; }
    const fx::AudioVisualState& getVisualState() const noexcept { return visualState; }
    DistortionSnapshot getDistortionSnapshot() const noexcept;
    void postExternalStateChange();

private:
    static void ensureStateParamValue(juce::ValueTree& state, const char* paramId, const juce::var& value);
    static juce::var readStateParamValue(const juce::ValueTree& state, const char* paramId, const juce::var& fallback);
    static void normaliseStateTree(juce::ValueTree& state);

    distortion::DistortionParams buildParameterSnapshot() const;
    bool engineUsesOversampling(const distortion::DistortionParams& params) const noexcept;
    void updateLatencyForParams(const distortion::DistortionParams& params);
    void copyBuffer(juce::AudioBuffer<float>& destination, const juce::AudioBuffer<float>& source);
    void clearDelayBuffers() noexcept;
    void resetAllEngines();
    void clearSnapshot() noexcept;
    void storeSnapshot(const distortion::DistortionEngineSnapshot& snapshot,
                       int engine,
                       int variant,
                       bool oversampled) noexcept;

    juce::AudioProcessorValueTreeState parameters;
    fx::AudioVisualState visualState;
    juce::AudioBuffer<float> wetBuffer;
    juce::SmoothedValue<float> mixSmoothed;
    juce::SmoothedValue<float> outputSmoothed;
    juce::SmoothedValue<float> transitionSmoothed;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
    std::array<std::vector<float>, distdsp::maxChannels> dryDelayBuffer;
    int dryDelayWrite = 0;
    int preparedBlockSize = 0;
    int oversamplingLatencySamples = 0;
    int lastEngineIndex = -1;
    int lastRouteKey = -1;
    int lastLatencySamples = 0;
    double preparedSampleRate = 44100.0;

    distortion::DistortionEngine distortionEngineState;
    distortion::OverdriveEngine overdriveEngineState;
    distortion::FuzzEngine fuzzEngineState;
    distortion::ExciterEngine exciterEngineState;
    distortion::BitCrusherEngine bitCrusherEngineState;
    distortion::ConsoleSaturationEngine consoleEngineState;

    std::atomic<float> visualPrimary { 0.0f };
    std::atomic<float> visualSecondary { 0.0f };
    std::atomic<float> visualTertiary { 0.0f };
    std::atomic<float> visualQuaternary { 0.0f };
    std::atomic<float> visualClipPeak { 0.0f };
    std::atomic<int> visualEngine { 0 };
    std::atomic<int> visualVariant { 0 };
    std::atomic<bool> visualOversampled { false };
    std::atomic<bool> visualClipped { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MusiqueDistortionProcessor)
};
