#include "PluginProcessor.h"
#include "FXComponents.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

namespace
{
struct Runner
{
    int checks = 0;
    int failures = 0;

    void expect(bool condition, const std::string& name)
    {
        ++checks;
        if (condition)
        {
            std::cout << "[PASS] " << name << '\n';
            return;
        }

        ++failures;
        std::cout << "[FAIL] " << name << '\n';
    }
};

void setParameter(MusiqueDistortionProcessor& processor, const juce::String& id, float value)
{
    auto* parameter = processor.getAPVTS().getParameter(id);
    if (parameter == nullptr)
    {
        std::cerr << "Missing parameter: " << id << '\n';
        std::exit(2);
    }

    parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}

float getParameterValue(MusiqueDistortionProcessor& processor, const juce::String& id)
{
    if (auto* raw = processor.getAPVTS().getRawParameterValue(id))
        return raw->load();

    std::cerr << "Missing parameter: " << id << '\n';
    std::exit(2);
}

void prepare(MusiqueDistortionProcessor& processor, int numInputs, int numOutputs, double sampleRate, int maximumBlockSize)
{
    processor.setPlayConfigDetails(numInputs, numOutputs, sampleRate, maximumBlockSize);
    processor.prepareToPlay(sampleRate, maximumBlockSize);
}

juce::AudioBuffer<float> makeStereoSine(int samples, double sampleRate, double frequency, float amplitude = 0.14f)
{
    juce::AudioBuffer<float> buffer(2, samples);
    constexpr double pi = 3.14159265358979323846;
    for (int index = 0; index < samples; ++index)
    {
        const double t = (double) index / sampleRate;
        buffer.setSample(0, index, (float) (amplitude * std::sin(2.0 * pi * frequency * t)));
        buffer.setSample(1, index, (float) (amplitude * std::sin(2.0 * pi * frequency * t + 0.37)));
    }
    return buffer;
}

juce::AudioBuffer<float> makeMonoSine(int samples, double sampleRate, double frequency, float amplitude = 0.14f)
{
    juce::AudioBuffer<float> buffer(1, samples);
    constexpr double pi = 3.14159265358979323846;
    for (int index = 0; index < samples; ++index)
    {
        const double t = (double) index / sampleRate;
        buffer.setSample(0, index, (float) (amplitude * std::sin(2.0 * pi * frequency * t)));
    }
    return buffer;
}

juce::AudioBuffer<float> makeImpulse(int samples, int channels = 2, float amplitude = 0.8f)
{
    juce::AudioBuffer<float> buffer(channels, samples);
    buffer.clear();
    for (int channel = 0; channel < channels; ++channel)
        buffer.setSample(channel, 0, amplitude);
    return buffer;
}

juce::AudioBuffer<float> makeSilence(int samples, int channels = 2)
{
    juce::AudioBuffer<float> buffer(channels, samples);
    buffer.clear();
    return buffer;
}

float maxAbs(const juce::AudioBuffer<float>& buffer)
{
    float value = 0.0f;
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            value = juce::jmax(value, std::abs(buffer.getSample(channel, sample)));
    return value;
}

float meanValue(const juce::AudioBuffer<float>& buffer)
{
    double sum = 0.0;
    const int count = juce::jmax(1, buffer.getNumChannels() * buffer.getNumSamples());
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            sum += buffer.getSample(channel, sample);
    return (float) (sum / (double) count);
}

bool isFiniteBuffer(const juce::AudioBuffer<float>& buffer)
{
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            if (!std::isfinite(buffer.getSample(channel, sample)))
                return false;
    return true;
}

float differenceEnergy(const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    const int channels = juce::jmin(a.getNumChannels(), b.getNumChannels());
    const int samples = juce::jmin(a.getNumSamples(), b.getNumSamples());
    float sum = 0.0f;
    for (int channel = 0; channel < channels; ++channel)
        for (int sample = 0; sample < samples; ++sample)
            sum += std::abs(a.getSample(channel, sample) - b.getSample(channel, sample));
    return sum / (float) juce::jmax(1, channels * samples);
}

float differenceEnergyAligned(const juce::AudioBuffer<float>& wet,
                              const juce::AudioBuffer<float>& dry,
                              int latencySamples)
{
    const int channels = juce::jmin(wet.getNumChannels(), dry.getNumChannels());
    const int latency = juce::jmax(0, latencySamples);
    const int samples = juce::jmin(wet.getNumSamples(), dry.getNumSamples() + latency);
    if (samples <= latency)
        return 0.0f;

    float sum = 0.0f;
    int count = 0;
    for (int channel = 0; channel < channels; ++channel)
    {
        for (int sample = latency; sample < samples; ++sample)
        {
            sum += std::abs(wet.getSample(channel, sample) - dry.getSample(channel, sample - latency));
            ++count;
        }
    }

    return sum / (float) juce::jmax(1, count);
}

juce::ValueTree copyStateTree(MusiqueDistortionProcessor& processor)
{
    juce::MemoryBlock stateData;
    processor.getStateInformation(stateData);
    auto xml = juce::AudioProcessor::getXmlFromBinary(stateData.getData(), (int) stateData.getSize());
    if (xml == nullptr)
    {
        std::cerr << "Failed to decode state XML\n";
        std::exit(2);
    }

    return juce::ValueTree::fromXml(*xml);
}

void loadStateTree(MusiqueDistortionProcessor& processor, const juce::ValueTree& state)
{
    auto xml = state.createXml();
    if (xml == nullptr)
    {
        std::cerr << "Failed to encode state XML\n";
        std::exit(2);
    }

    juce::MemoryBlock stateData;
    juce::AudioProcessor::copyXmlToBinary(*xml, stateData);
    processor.setStateInformation(stateData.getData(), (int) stateData.getSize());
}

void removeParameterFromState(juce::ValueTree& state, const juce::String& id)
{
    for (int index = state.getNumChildren() - 1; index >= 0; --index)
    {
        auto child = state.getChild(index);
        if (child.hasType("PARAM") && child.getProperty("id").toString() == id)
            state.removeChild(index, nullptr);
    }
}

void process(MusiqueDistortionProcessor& processor, juce::AudioBuffer<float>& buffer)
{
    juce::MidiBuffer midi;
    processor.processBlock(buffer, midi);
}

juce::File findFactoryBankForTests()
{
    auto dir = juce::File::getCurrentWorkingDirectory();
    for (int depth = 0; depth < 8; ++depth)
    {
        const std::array<juce::File, 3> candidates {
            dir.getChildFile("Presets").getChildFile("factory_bank.json"),
            dir.getChildFile("fx-distortion").getChildFile("Presets").getChildFile("factory_bank.json"),
            dir.getChildFile("FX").getChildFile("fx-distortion").getChildFile("Presets").getChildFile("factory_bank.json")
        };

        for (const auto& candidate : candidates)
            if (candidate.existsAsFile())
                return candidate;

        auto parent = dir.getParentDirectory();
        if (parent == dir)
            break;
        dir = parent;
    }

    return {};
}

juce::Array<juce::var> loadFactoryPresetsForTests(Runner& runner)
{
    const auto file = findFactoryBankForTests();
    runner.expect(file.existsAsFile(), "factory preset bank is discoverable");
    if (!file.existsAsFile())
        return {};

    auto presets = fx::preset::loadPresetsFromBank(file);
    for (auto& preset : presets)
        MusiqueDistortionProcessor::normalisePresetObject(preset);
    return presets;
}
}

int main()
{
    Runner runner;
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 256;

    {
        MusiqueDistortionProcessor processor;
        prepare(processor, 2, 2, sampleRate, blockSize);
        setParameter(processor, "mode", 2.0f);
        setParameter(processor, "drive", 9.0f);

        auto state = copyStateTree(processor);
        removeParameterFromState(state, "engine");
        removeParameterFromState(state, "variant");
        loadStateTree(processor, state);

        runner.expect((int) std::round(getParameterValue(processor, "engine")) == 0, "legacy state defaults to distortion engine");
        runner.expect((int) std::round(getParameterValue(processor, "variant")) == 2, "legacy state derives variant from legacy mode");
        runner.expect(std::abs(getParameterValue(processor, "drive") - 9.0f) < 0.001f, "legacy state preserves drive");
    }

    {
        MusiqueDistortionProcessor processor;
        prepare(processor, 2, 2, sampleRate, blockSize);

        juce::DynamicObject::Ptr object = new juce::DynamicObject();
        object->setProperty("name", "Legacy");
        object->setProperty("mode", 1);
        object->setProperty("drive", 18.0f);
        object->setProperty("mix", 42.0f);
        juce::var preset(object.get());
        MusiqueDistortionProcessor::normalisePresetObject(preset);
        fx::preset::applyToAPVTS(processor.getAPVTS(), preset);
        processor.postExternalStateChange();

        runner.expect((int) std::round(getParameterValue(processor, "engine")) == 0, "legacy preset keeps distortion engine");
        runner.expect((int) std::round(getParameterValue(processor, "variant")) == 1, "legacy preset derives variant from mode");
        runner.expect(std::abs(getParameterValue(processor, "mix") - 42.0f) < 0.001f, "legacy preset keeps mix field");
    }

    {
        MusiqueDistortionProcessor processor;
        prepare(processor, 2, 2, sampleRate, blockSize);
        setParameter(processor, "engine", 0.0f);
        setParameter(processor, "mode", 2.0f);
        setParameter(processor, "variant", 2.0f);
        setParameter(processor, "drive", 40.0f);
        setParameter(processor, "dist_focus", 70.0f);
        setParameter(processor, "dist_knee", 25.0f);
        setParameter(processor, "mix", 100.0f);
        processor.postExternalStateChange();

        float peak = 0.0f;
        for (int block = 0; block < 24; ++block)
        {
            auto buffer = block == 0 ? makeImpulse(blockSize) : makeStereoSine(blockSize, sampleRate, 330.0);
            process(processor, buffer);
            peak = juce::jmax(peak, maxAbs(buffer));
            runner.expect(isFiniteBuffer(buffer), "distortion engine remains finite at extreme drive");
        }

        runner.expect(peak > 0.0001f, "distortion engine remains audible");
        runner.expect(peak < 1.35f, "distortion engine remains bounded");
    }

    {
        MusiqueDistortionProcessor processor;
        prepare(processor, 2, 2, sampleRate, blockSize);
        setParameter(processor, "engine", 4.0f);
        setParameter(processor, "variant", 2.0f);
        setParameter(processor, "crush_bits", 4.0f);
        setParameter(processor, "crush_rate", 400.0f);
        setParameter(processor, "crush_jitter", 100.0f);
        setParameter(processor, "crush_drive", 12.0f);
        processor.postExternalStateChange();

        float peak = 0.0f;
        for (int block = 0; block < 16; ++block)
        {
            auto buffer = makeStereoSine(blockSize, sampleRate, 220.0);
            process(processor, buffer);
            peak = juce::jmax(peak, maxAbs(buffer));
            runner.expect(isFiniteBuffer(buffer), "bit crusher stays finite at extreme settings");
        }

        runner.expect(peak > 0.0001f, "bit crusher remains audible");
    }

    {
        MusiqueDistortionProcessor processor;
        prepare(processor, 1, 1, sampleRate, blockSize);

        juce::AudioProcessor::BusesLayout monoLayout;
        monoLayout.inputBuses.add(juce::AudioChannelSet::mono());
        monoLayout.outputBuses.add(juce::AudioChannelSet::mono());
        juce::AudioProcessor::BusesLayout stereoLayout;
        stereoLayout.inputBuses.add(juce::AudioChannelSet::stereo());
        stereoLayout.outputBuses.add(juce::AudioChannelSet::stereo());

        runner.expect(processor.isBusesLayoutSupported(monoLayout), "mono->mono layout is supported");
        runner.expect(processor.isBusesLayoutSupported(stereoLayout), "stereo->stereo layout is supported");

        auto monoBuffer = makeMonoSine(blockSize, sampleRate, 220.0);
        process(processor, monoBuffer);
        runner.expect(isFiniteBuffer(monoBuffer), "mono processing remains finite");
        runner.expect(maxAbs(monoBuffer) > 0.0001f, "mono processing remains audible");
    }

    {
        MusiqueDistortionProcessor processor;
        prepare(processor, 2, 2, sampleRate, blockSize);
        setParameter(processor, "output", -6.0f);
        setParameter(processor, "mono", 1.0f);
        setParameter(processor, "bypass", 1.0f);
        processor.postExternalStateChange();

        auto buffer = makeStereoSine(blockSize, sampleRate, 330.0);
        auto expected = buffer;
        process(processor, buffer);
        runner.expect(differenceEnergy(buffer, expected) < 1.0e-6f, "bypass returns the dry signal unchanged");
    }

    {
        MusiqueDistortionProcessor processor;
        prepare(processor, 2, 2, sampleRate, blockSize);
        setParameter(processor, "engine", 0.0f);
        setParameter(processor, "mode", 0.0f);
        setParameter(processor, "variant", 0.0f);
        processor.postExternalStateChange();
        runner.expect(processor.getLatencySamples() > 0, "oversampled distortion reports non-zero latency");

        setParameter(processor, "engine", 4.0f);
        setParameter(processor, "variant", 0.0f);
        processor.postExternalStateChange();
        runner.expect(processor.getLatencySamples() == 0, "bit crusher reports zero latency");
    }

    {
        MusiqueDistortionProcessor processor;
        prepare(processor, 2, 2, sampleRate, blockSize);
        setParameter(processor, "engine", 0.0f);
        setParameter(processor, "mode", 2.0f);
        setParameter(processor, "variant", 2.0f);
        processor.postExternalStateChange();

        auto first = makeStereoSine(blockSize, sampleRate, 196.0);
        process(processor, first);

        setParameter(processor, "engine", 4.0f);
        setParameter(processor, "variant", 1.0f);
        processor.postExternalStateChange();
        auto second = makeStereoSine(blockSize, sampleRate, 196.0);
        process(processor, second);

        runner.expect(isFiniteBuffer(second), "engine switch from oversampled to raw stays finite");
        runner.expect(maxAbs(second) > 0.0001f, "engine switch from oversampled to raw does not mute the buffer");
        runner.expect(maxAbs(second) < 1.5f, "engine switch from oversampled to raw avoids runaway peaks");
    }

    {
        MusiqueDistortionProcessor processor;
        prepare(processor, 2, 2, sampleRate, blockSize);
        setParameter(processor, "engine", 1.0f);
        setParameter(processor, "variant", 2.0f);
        setParameter(processor, "od_drive", 28.0f);
        setParameter(processor, "od_bias", 70.0f);
        processor.postExternalStateChange();

        auto buffer = makeStereoSine(blockSize * 4, sampleRate, 440.0);
        process(processor, buffer);
        runner.expect(std::abs(meanValue(buffer)) < 0.08f, "asymmetric overdrive DC offset remains bounded");
    }

    {
        auto measureExciterDiff = [&](double frequency)
        {
            MusiqueDistortionProcessor processor;
            prepare(processor, 2, 2, sampleRate, blockSize);
            setParameter(processor, "engine", 3.0f);
            setParameter(processor, "variant", 2.0f);
            setParameter(processor, "exc_amount", 70.0f);
            setParameter(processor, "exc_freq", 4800.0f);
            setParameter(processor, "exc_air", 78.0f);
            setParameter(processor, "exc_drive", 14.0f);
            processor.postExternalStateChange();

            auto warmupA = makeStereoSine(blockSize, sampleRate, frequency);
            auto warmupB = makeStereoSine(blockSize, sampleRate, frequency);
            process(processor, warmupA);
            process(processor, warmupB);

            auto wet = makeStereoSine(blockSize, sampleRate, frequency);
            auto dry = wet;
            process(processor, wet);
            return differenceEnergyAligned(wet, dry, processor.getLatencySamples());
        };

        const float lowDiff = measureExciterDiff(220.0);
        const float highDiff = measureExciterDiff(6200.0);
        runner.expect(highDiff > lowDiff * 1.2f, "exciter changes high-frequency content more than low-frequency content");
    }

    {
        MusiqueDistortionProcessor processor;
        prepare(processor, 2, 2, sampleRate, blockSize);
        setParameter(processor, "engine", 5.0f);
        setParameter(processor, "variant", 2.0f);
        setParameter(processor, "sat_drive", 18.0f);
        setParameter(processor, "sat_glue", 72.0f);
        setParameter(processor, "sat_headroom", 24.0f);
        processor.postExternalStateChange();

        auto buffer = makeStereoSine(blockSize * 3, sampleRate, 110.0, 0.5f);
        process(processor, buffer);
        runner.expect(isFiniteBuffer(buffer), "console saturation output stays finite");
        runner.expect(maxAbs(buffer) < 1.35f, "console saturation output remains bounded under heavy drive");
    }

    {
        MusiqueDistortionProcessor source;
        prepare(source, 2, 2, sampleRate, blockSize);
        setParameter(source, "engine", 5.0f);
        setParameter(source, "variant", 2.0f);
        setParameter(source, "sat_drive", 12.0f);
        setParameter(source, "mix", 82.0f);
        auto state = copyStateTree(source);

        MusiqueDistortionProcessor target;
        prepare(target, 2, 2, sampleRate, blockSize);
        loadStateTree(target, state);
        runner.expect((int) std::round(getParameterValue(target, "engine")) == 5, "round-trip state preserves engine");
        runner.expect((int) std::round(getParameterValue(target, "variant")) == 2, "round-trip state preserves variant");
        runner.expect(std::abs(getParameterValue(target, "mix") - 82.0f) < 0.001f, "round-trip state preserves mix");
    }

    {
        auto presets = loadFactoryPresetsForTests(runner);
        runner.expect(presets.size() == 18, "factory bank exposes 18 presets");
        MusiqueDistortionProcessor processor;
        prepare(processor, 2, 2, sampleRate, blockSize);
        for (int index = 0; index < presets.size(); ++index)
        {
            auto preset = presets.getReference(index);
            fx::preset::applyToAPVTS(processor.getAPVTS(), preset);
            processor.postExternalStateChange();
            auto buffer = makeStereoSine(blockSize, sampleRate, 330.0);
            process(processor, buffer);
            runner.expect(isFiniteBuffer(buffer), "factory preset " + std::to_string(index + 1) + " processes without NaN");
        }
    }

    std::cout << "Checks: " << runner.checks << ", failures: " << runner.failures << '\n';
    return runner.failures == 0 ? 0 : 1;
}
