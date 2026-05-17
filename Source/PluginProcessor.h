#pragma once
#include <JuceHeader.h>
#include <array>
#include <vector>
#include "FXAudioVisualState.h"
class MusiqueDistortionProcessor : public juce::AudioProcessor
{
public:
    MusiqueDistortionProcessor();
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void prepareToPlay(double, int) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Musique Distortion"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;
    juce::AudioProcessorValueTreeState& getAPVTS() noexcept { return parameters; }
    const fx::AudioVisualState& getVisualState() const noexcept { return visualState; }
    bool isOversamplingActive() const noexcept;
    int getOversamplingFactor() const noexcept { return isOversamplingActive() ? 2 : 1; }
    int getOversamplingLatencySamples() const noexcept { return oversamplingLatencySamples; }
private:
    juce::AudioProcessorValueTreeState parameters;
    fx::AudioVisualState visualState;
    juce::AudioBuffer<float> wetBuffer;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
    std::array<std::vector<float>, 2> dryDelayBuffer;
    std::array<float, 2> toneState {};
    int dryDelayWrite = 0;
    int oversamplingLatencySamples = 0;
    bool lastOversamplingActive = false;
    double preparedSampleRate = 44100.0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MusiqueDistortionProcessor)
};
