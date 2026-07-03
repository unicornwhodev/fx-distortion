#include "PluginProcessor.h"
#if ! MUSIQUE_DISTORTION_DSP_TESTS
#include "PluginEditor.h"
#endif

namespace
{
float getRawValue(const juce::AudioProcessorValueTreeState& apvts, const juce::String& id, float fallback = 0.0f)
{
    if (auto* raw = apvts.getRawParameterValue(id))
        return raw->load();
    return fallback;
}

int getChoiceValue(const juce::AudioProcessorValueTreeState& apvts, const juce::String& id, int fallback = 0)
{
    return (int) std::round(getRawValue(apvts, id, (float) fallback));
}

void setParameterValue(juce::AudioProcessorValueTreeState& apvts, const juce::String& id, float value)
{
    if (auto* parameter = apvts.getParameter(id))
        parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}
}

MusiqueDistortionProcessor::MusiqueDistortionProcessor()
    : AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "MusiqueDistortion", createParameterLayout())
{
    mixSmoothed.reset(preparedSampleRate, 0.025);
    outputSmoothed.reset(preparedSampleRate, 0.025);
    transitionSmoothed.reset(preparedSampleRate, 0.08);
    transitionSmoothed.setCurrentAndTargetValue(1.0f);
    clearSnapshot();
    postExternalStateChange();
}

MusiqueDistortionProcessor::~MusiqueDistortionProcessor() = default;

juce::AudioProcessorValueTreeState::ParameterLayout MusiqueDistortionProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "engine", "Engine",
        juce::StringArray { "Distortion", "Overdrive", "Fuzz", "Exciter", "Bit Crusher", "Console Saturation" }, 0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "variant", "Variant", juce::StringArray { "Variant A", "Variant B", "Variant C" }, 0));

    params.push_back(std::make_unique<juce::AudioParameterFloat>("drive", "Drive", 0.0f, 40.0f, 12.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("tone", "Tone", 0.0f, 1.0f, 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("blend", "Blend", 0.0f, 100.0f, 70.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("output", "Output", -24.0f, 12.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "mode", "Mode", juce::StringArray { "Clipper", "Bitcrush", "Tube" }, 0));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("mix", "Mix", 0.0f, 100.0f, 100.0f));
    params.push_back(std::make_unique<juce::AudioParameterBool>("bypass", "Bypass", false));
    params.push_back(std::make_unique<juce::AudioParameterBool>("mono", "Mono", false));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("dist_focus", "Dist Focus", 0.0f, 100.0f, 50.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("dist_knee", "Dist Knee", 0.0f, 100.0f, 0.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>("od_drive", "OD Drive", 0.0f, 36.0f, 18.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("od_tone", "OD Tone", 0.0f, 1.0f, 0.55f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("od_bias", "OD Bias", 0.0f, 100.0f, 50.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("od_body", "OD Body", 0.0f, 100.0f, 45.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("od_headroom", "OD Headroom", 0.0f, 100.0f, 55.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>("fuzz_gain", "Fuzz Gain", 0.0f, 40.0f, 24.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("fuzz_tone", "Fuzz Tone", 0.0f, 1.0f, 0.45f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("fuzz_gate", "Fuzz Gate", 0.0f, 100.0f, 20.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("fuzz_octave", "Fuzz Octave", 0.0f, 100.0f, 30.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("fuzz_bias", "Fuzz Bias", 0.0f, 100.0f, 50.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>("exc_amount", "Exc Amount", 0.0f, 100.0f, 35.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("exc_freq", "Exc Frequency", 1200.0f, 12000.0f, 4200.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("exc_air", "Exc Air", 0.0f, 100.0f, 40.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("exc_tone", "Exc Tone", 0.0f, 1.0f, 0.6f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("exc_drive", "Exc Drive", 0.0f, 24.0f, 8.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "crush_bits", "Crush Bits", juce::NormalisableRange<float>(4.0f, 16.0f, 1.0f), 10.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "crush_rate", "Crush Rate", juce::NormalisableRange<float>(400.0f, 22050.0f, 1.0f, 0.4f), 8000.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("crush_jitter", "Crush Jitter", 0.0f, 100.0f, 10.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("crush_tone", "Crush Tone", 0.0f, 1.0f, 0.55f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("crush_drive", "Crush Drive", 0.0f, 24.0f, 0.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>("sat_drive", "Sat Drive", 0.0f, 24.0f, 8.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("sat_tone", "Sat Tone", 0.0f, 1.0f, 0.55f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("sat_glue", "Sat Glue", 0.0f, 100.0f, 35.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("sat_headroom", "Sat Headroom", 0.0f, 100.0f, 60.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("sat_bias", "Sat Bias", 0.0f, 100.0f, 50.0f));

    return { params.begin(), params.end() };
}

juce::StringArray MusiqueDistortionProcessor::getAllParameterIds()
{
    return {
        "engine", "variant",
        "drive", "tone", "blend", "output", "mode", "mix", "bypass", "mono", "dist_focus", "dist_knee",
        "od_drive", "od_tone", "od_bias", "od_body", "od_headroom",
        "fuzz_gain", "fuzz_tone", "fuzz_gate", "fuzz_octave", "fuzz_bias",
        "exc_amount", "exc_freq", "exc_air", "exc_tone", "exc_drive",
        "crush_bits", "crush_rate", "crush_jitter", "crush_tone", "crush_drive",
        "sat_drive", "sat_tone", "sat_glue", "sat_headroom", "sat_bias"
    };
}

void MusiqueDistortionProcessor::normalisePresetObject(juce::var& preset)
{
    auto* object = preset.getDynamicObject();
    if (object == nullptr)
        return;

    auto ensure = [&](const char* key, const juce::var& value)
    {
        if (!object->hasProperty(key))
            object->setProperty(key, value);
    };

    const int engineValue = juce::jlimit(0, numEngines - 1,
        (int) (object->hasProperty("engine") ? object->getProperty("engine") : juce::var(0)));
    const int legacyMode = juce::jlimit(0, 2,
        (int) (object->hasProperty("mode") ? object->getProperty("mode") : juce::var(0)));
    ensure("engine", 0);
    if (!object->hasProperty("variant"))
        object->setProperty("variant", engineValue == distortion ? legacyMode : 0);

    if (!object->hasProperty("mode"))
        object->setProperty("mode", legacyMode);

    ensure("drive", 12.0f);
    ensure("tone", 0.5f);
    ensure("blend", 70.0f);
    ensure("output", 0.0f);
    ensure("mix", 100.0f);
    ensure("bypass", false);
    ensure("mono", false);
    ensure("dist_focus", 50.0f);
    ensure("dist_knee", 0.0f);

    ensure("od_drive", 18.0f);
    ensure("od_tone", 0.55f);
    ensure("od_bias", 50.0f);
    ensure("od_body", 45.0f);
    ensure("od_headroom", 55.0f);

    ensure("fuzz_gain", 24.0f);
    ensure("fuzz_tone", 0.45f);
    ensure("fuzz_gate", 20.0f);
    ensure("fuzz_octave", 30.0f);
    ensure("fuzz_bias", 50.0f);

    ensure("exc_amount", 35.0f);
    ensure("exc_freq", 4200.0f);
    ensure("exc_air", 40.0f);
    ensure("exc_tone", 0.6f);
    ensure("exc_drive", 8.0f);

    ensure("crush_bits", 10.0f);
    ensure("crush_rate", 8000.0f);
    ensure("crush_jitter", 10.0f);
    ensure("crush_tone", 0.55f);
    ensure("crush_drive", 0.0f);

    ensure("sat_drive", 8.0f);
    ensure("sat_tone", 0.55f);
    ensure("sat_glue", 35.0f);
    ensure("sat_headroom", 60.0f);
    ensure("sat_bias", 50.0f);
}

void MusiqueDistortionProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    preparedSampleRate = sampleRate;
    preparedBlockSize = juce::jmax(juce::jmax(1, samplesPerBlock * 2), 4096);

    mixSmoothed.reset(sampleRate, 0.025);
    outputSmoothed.reset(sampleRate, 0.025);
    transitionSmoothed.reset(sampleRate, 0.08);
    mixSmoothed.setCurrentAndTargetValue(getRawValue(parameters, "mix", 100.0f) / 100.0f);
    outputSmoothed.setCurrentAndTargetValue(distdsp::dbToGain(getRawValue(parameters, "output", 0.0f)));
    transitionSmoothed.setCurrentAndTargetValue(1.0f);

    const int channels = juce::jmax(1, getTotalNumOutputChannels());
    wetBuffer.setSize(channels, preparedBlockSize, false, false, true);

    oversampling = std::make_unique<juce::dsp::Oversampling<float>>(
        (size_t) channels,
        2,
        juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple,
        true,
        true);
    oversampling->reset();
    oversampling->initProcessing((size_t) preparedBlockSize);
    oversamplingLatencySamples = juce::jmax(0, (int) std::round(oversampling->getLatencyInSamples()));

    for (auto& channelDelay : dryDelayBuffer)
        channelDelay.assign((size_t) juce::jmax(1, oversamplingLatencySamples), 0.0f);
    dryDelayWrite = 0;

    distortionEngineState.prepare(sampleRate, preparedBlockSize);
    overdriveEngineState.prepare(sampleRate, preparedBlockSize);
    fuzzEngineState.prepare(sampleRate, preparedBlockSize);
    exciterEngineState.prepare(sampleRate, preparedBlockSize);
    bitCrusherEngineState.prepare(sampleRate, preparedBlockSize);
    consoleEngineState.prepare(sampleRate, preparedBlockSize);

    postExternalStateChange();
}

void MusiqueDistortionProcessor::releaseResources()
{
    wetBuffer.setSize(0, 0);
    oversampling.reset();
    for (auto& channelDelay : dryDelayBuffer)
        channelDelay.clear();
    lastLatencySamples = 0;
    setLatencySamples(0);
    resetAllEngines();
}

bool MusiqueDistortionProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto input = layouts.getMainInputChannelSet();
    const auto output = layouts.getMainOutputChannelSet();
    const bool monoLayout = input == juce::AudioChannelSet::mono() && output == juce::AudioChannelSet::mono();
    const bool stereoLayout = input == juce::AudioChannelSet::stereo() && output == juce::AudioChannelSet::stereo();
    return monoLayout || stereoLayout;
}

void MusiqueDistortionProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    visualState.captureInput(buffer);

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    if (numSamples <= 0 || numChannels <= 0)
        return;

    if (getRawValue(parameters, "bypass") > 0.5f)
    {
        clearSnapshot();
        visualState.captureOutput(buffer);
        return;
    }

    const auto params = buildParameterSnapshot();
    const bool oversampled = engineUsesOversampling(params);
    updateLatencyForParams(params);

    const int routeKey = params.engine * 16 + params.variant * 2 + (oversampled ? 1 : 0);
    if (routeKey != lastRouteKey)
    {
        resetAllEngines();
        clearDelayBuffers();
        transitionSmoothed.setCurrentAndTargetValue(0.0f);
        transitionSmoothed.setTargetValue(1.0f);
        lastRouteKey = routeKey;
    }

    if (params.engine != lastEngineIndex)
        lastEngineIndex = params.engine;

    if (params.mono && numChannels > 1)
    {
        for (int sample = 0; sample < numSamples; ++sample)
        {
            const float monoSample = 0.5f * (buffer.getSample(0, sample) + buffer.getSample(1, sample));
            buffer.setSample(0, sample, monoSample);
            buffer.setSample(1, sample, monoSample);
        }
    }

    if (numSamples > wetBuffer.getNumSamples() || numChannels > wetBuffer.getNumChannels())
    {
        wetBuffer.setSize(numChannels, juce::jmax(numSamples, preparedBlockSize), false, false, false);
    }

    copyBuffer(wetBuffer, buffer);

    distortion::DistortionEngineSnapshot snapshot;
    if (oversampled && oversampling != nullptr)
    {
        juce::dsp::AudioBlock<float> wetBlock(wetBuffer);
        auto upsampledBlock = oversampling->processSamplesUp(wetBlock);
        switch (params.engine)
        {
            case distortion:
                distortionEngineState.process(upsampledBlock, params, preparedSampleRate * 4.0, snapshot);
                break;
            case overdrive:
                overdriveEngineState.process(upsampledBlock, params, preparedSampleRate * 4.0, snapshot);
                break;
            case fuzz:
                fuzzEngineState.process(upsampledBlock, params, preparedSampleRate * 4.0, snapshot);
                break;
            case exciter:
                exciterEngineState.process(upsampledBlock, params, preparedSampleRate * 4.0, snapshot);
                break;
            case consoleSaturation:
                consoleEngineState.process(upsampledBlock, params, preparedSampleRate * 4.0, snapshot);
                break;
            default:
                break;
        }
        oversampling->processSamplesDown(wetBlock);
    }
    else
    {
        juce::dsp::AudioBlock<float> wetBlock(wetBuffer);
        switch (params.engine)
        {
            case distortion:
                distortionEngineState.process(wetBlock, params, preparedSampleRate, snapshot);
                break;
            case overdrive:
                overdriveEngineState.process(wetBlock, params, preparedSampleRate, snapshot);
                break;
            case fuzz:
                fuzzEngineState.process(wetBlock, params, preparedSampleRate, snapshot);
                break;
            case exciter:
                exciterEngineState.process(wetBlock, params, preparedSampleRate, snapshot);
                break;
            case bitCrusher:
                bitCrusherEngineState.process(wetBlock, params, preparedSampleRate, snapshot);
                break;
            case consoleSaturation:
                consoleEngineState.process(wetBlock, params, preparedSampleRate, snapshot);
                break;
            default:
                break;
        }
    }

    mixSmoothed.setTargetValue(params.mix / 100.0f);
    outputSmoothed.setTargetValue(distdsp::dbToGain(params.outputDb));
    for (int sample = 0; sample < numSamples; ++sample)
    {
        const float mix = juce::jlimit(0.0f, 1.0f, mixSmoothed.getNextValue());
        const float transition = juce::jlimit(0.0f, 1.0f, transitionSmoothed.getNextValue());
        const float output = outputSmoothed.getNextValue();
        const float effectiveMix = mix * transition;

        float delayedDry[distdsp::maxChannels] {};
        for (int channel = 0; channel < numChannels; ++channel)
            delayedDry[channel] = buffer.getSample(channel, sample);

        if (oversampled && lastLatencySamples > 0)
        {
            const size_t delayIndex = (size_t) dryDelayWrite;
            for (int channel = 0; channel < numChannels; ++channel)
            {
                auto& channelDelay = dryDelayBuffer[(size_t) channel];
                delayedDry[channel] = channelDelay.empty() ? delayedDry[channel] : channelDelay[delayIndex];
                if (!channelDelay.empty())
                    channelDelay[delayIndex] = buffer.getSample(channel, sample);
            }
            dryDelayWrite = (dryDelayWrite + 1) % juce::jmax(1, lastLatencySamples);
        }

        for (int channel = 0; channel < numChannels; ++channel)
        {
            const float dry = oversampled ? delayedDry[channel] : buffer.getSample(channel, sample);
            const float wet = wetBuffer.getSample(channel, sample);
            buffer.setSample(channel, sample, (dry * (1.0f - effectiveMix) + wet * effectiveMix) * output);
        }
    }

    storeSnapshot(snapshot, params.engine, params.variant, oversampled);
    visualState.captureOutput(buffer);
}

void MusiqueDistortionProcessor::processBlockBypassed(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    clearSnapshot();
    visualState.captureInput(buffer);
    visualState.captureOutput(buffer);
}

juce::AudioProcessorEditor* MusiqueDistortionProcessor::createEditor()
{
#if MUSIQUE_DISTORTION_DSP_TESTS
    return nullptr;
#else
    return new MusiqueDistortionEditor(*this);
#endif
}

double MusiqueDistortionProcessor::getTailLengthSeconds() const
{
    return preparedSampleRate > 0.0 ? (double) lastLatencySamples / preparedSampleRate : 0.0;
}

juce::AudioProcessorParameter* MusiqueDistortionProcessor::getBypassParameter() const
{
    return const_cast<juce::AudioProcessorValueTreeState&>(parameters).getParameter("bypass");
}

void MusiqueDistortionProcessor::getStateInformation(juce::MemoryBlock& destination)
{
    auto state = parameters.copyState();
    normaliseStateTree(state);
    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destination);
}

void MusiqueDistortionProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (xml == nullptr || !xml->hasTagName(parameters.state.getType()))
        return;

    auto state = juce::ValueTree::fromXml(*xml);
    normaliseStateTree(state);
    parameters.replaceState(state);
    postExternalStateChange();
}

DistortionSnapshot MusiqueDistortionProcessor::getDistortionSnapshot() const noexcept
{
    return {
        visualPrimary.load(std::memory_order_relaxed),
        visualSecondary.load(std::memory_order_relaxed),
        visualTertiary.load(std::memory_order_relaxed),
        visualQuaternary.load(std::memory_order_relaxed),
        visualClipPeak.load(std::memory_order_relaxed),
        visualEngine.load(std::memory_order_relaxed),
        visualVariant.load(std::memory_order_relaxed),
        visualOversampled.load(std::memory_order_relaxed),
        visualClipped.load(std::memory_order_relaxed)
    };
}

void MusiqueDistortionProcessor::postExternalStateChange()
{
    resetAllEngines();
    lastEngineIndex = -1;
    lastRouteKey = -1;
    clearDelayBuffers();
    updateLatencyForParams(buildParameterSnapshot());
}

void MusiqueDistortionProcessor::ensureStateParamValue(juce::ValueTree& state, const char* paramId, const juce::var& value)
{
    for (int childIndex = 0; childIndex < state.getNumChildren(); ++childIndex)
    {
        auto child = state.getChild(childIndex);
        if (child.hasType("PARAM") && child.getProperty("id").toString() == paramId)
        {
            if (!child.hasProperty("value"))
                child.setProperty("value", value, nullptr);
            return;
        }
    }

    juce::ValueTree child("PARAM");
    child.setProperty("id", paramId, nullptr);
    child.setProperty("value", value, nullptr);
    state.appendChild(child, nullptr);
}

juce::var MusiqueDistortionProcessor::readStateParamValue(const juce::ValueTree& state, const char* paramId, const juce::var& fallback)
{
    for (int childIndex = 0; childIndex < state.getNumChildren(); ++childIndex)
    {
        auto child = state.getChild(childIndex);
        if (child.hasType("PARAM") && child.getProperty("id").toString() == paramId)
            return child.getProperty("value", fallback);
    }
    return fallback;
}

void MusiqueDistortionProcessor::normaliseStateTree(juce::ValueTree& state)
{
    ensureStateParamValue(state, "engine", 0);
    ensureStateParamValue(state, "drive", 12.0f);
    ensureStateParamValue(state, "tone", 0.5f);
    ensureStateParamValue(state, "blend", 70.0f);
    ensureStateParamValue(state, "output", 0.0f);
    ensureStateParamValue(state, "mode", 0);
    ensureStateParamValue(state, "mix", 100.0f);
    ensureStateParamValue(state, "bypass", false);
    ensureStateParamValue(state, "mono", false);
    ensureStateParamValue(state, "dist_focus", 50.0f);
    ensureStateParamValue(state, "dist_knee", 0.0f);

    const int engineValue = juce::jlimit(0, numEngines - 1, (int) readStateParamValue(state, "engine", 0));
    const int legacyMode = juce::jlimit(0, 2, (int) readStateParamValue(state, "mode", 0));
    ensureStateParamValue(state, "variant", engineValue == distortion ? legacyMode : 0);
    if (engineValue == distortion)
        ensureStateParamValue(state, "mode", (int) readStateParamValue(state, "variant", legacyMode));

    ensureStateParamValue(state, "od_drive", 18.0f);
    ensureStateParamValue(state, "od_tone", 0.55f);
    ensureStateParamValue(state, "od_bias", 50.0f);
    ensureStateParamValue(state, "od_body", 45.0f);
    ensureStateParamValue(state, "od_headroom", 55.0f);

    ensureStateParamValue(state, "fuzz_gain", 24.0f);
    ensureStateParamValue(state, "fuzz_tone", 0.45f);
    ensureStateParamValue(state, "fuzz_gate", 20.0f);
    ensureStateParamValue(state, "fuzz_octave", 30.0f);
    ensureStateParamValue(state, "fuzz_bias", 50.0f);

    ensureStateParamValue(state, "exc_amount", 35.0f);
    ensureStateParamValue(state, "exc_freq", 4200.0f);
    ensureStateParamValue(state, "exc_air", 40.0f);
    ensureStateParamValue(state, "exc_tone", 0.6f);
    ensureStateParamValue(state, "exc_drive", 8.0f);

    ensureStateParamValue(state, "crush_bits", 10.0f);
    ensureStateParamValue(state, "crush_rate", 8000.0f);
    ensureStateParamValue(state, "crush_jitter", 10.0f);
    ensureStateParamValue(state, "crush_tone", 0.55f);
    ensureStateParamValue(state, "crush_drive", 0.0f);

    ensureStateParamValue(state, "sat_drive", 8.0f);
    ensureStateParamValue(state, "sat_tone", 0.55f);
    ensureStateParamValue(state, "sat_glue", 35.0f);
    ensureStateParamValue(state, "sat_headroom", 60.0f);
    ensureStateParamValue(state, "sat_bias", 50.0f);
}

distortion::DistortionParams MusiqueDistortionProcessor::buildParameterSnapshot() const
{
    distortion::DistortionParams snapshot;
    snapshot.engine = juce::jlimit(0, numEngines - 1, getChoiceValue(parameters, "engine", 0));
    snapshot.mode = juce::jlimit(0, 2, getChoiceValue(parameters, "mode", 0));
    snapshot.variant = snapshot.engine == distortion
        ? snapshot.mode
        : juce::jlimit(0, 2, getChoiceValue(parameters, "variant", 0));
    snapshot.bypass = getRawValue(parameters, "bypass") > 0.5f;
    snapshot.mono = getRawValue(parameters, "mono") > 0.5f;
    snapshot.mix = getRawValue(parameters, "mix", 100.0f);
    snapshot.outputDb = getRawValue(parameters, "output", 0.0f);

    snapshot.drive = getRawValue(parameters, "drive", 12.0f);
    snapshot.tone = getRawValue(parameters, "tone", 0.5f);
    snapshot.blend = getRawValue(parameters, "blend", 70.0f);
    snapshot.distFocus = getRawValue(parameters, "dist_focus", 50.0f);
    snapshot.distKnee = getRawValue(parameters, "dist_knee", 0.0f);

    snapshot.odDrive = getRawValue(parameters, "od_drive", 18.0f);
    snapshot.odTone = getRawValue(parameters, "od_tone", 0.55f);
    snapshot.odBias = getRawValue(parameters, "od_bias", 50.0f);
    snapshot.odBody = getRawValue(parameters, "od_body", 45.0f);
    snapshot.odHeadroom = getRawValue(parameters, "od_headroom", 55.0f);

    snapshot.fuzzGain = getRawValue(parameters, "fuzz_gain", 24.0f);
    snapshot.fuzzTone = getRawValue(parameters, "fuzz_tone", 0.45f);
    snapshot.fuzzGate = getRawValue(parameters, "fuzz_gate", 20.0f);
    snapshot.fuzzOctave = getRawValue(parameters, "fuzz_octave", 30.0f);
    snapshot.fuzzBias = getRawValue(parameters, "fuzz_bias", 50.0f);

    snapshot.excAmount = getRawValue(parameters, "exc_amount", 35.0f);
    snapshot.excFreq = getRawValue(parameters, "exc_freq", 4200.0f);
    snapshot.excAir = getRawValue(parameters, "exc_air", 40.0f);
    snapshot.excTone = getRawValue(parameters, "exc_tone", 0.6f);
    snapshot.excDrive = getRawValue(parameters, "exc_drive", 8.0f);

    snapshot.crushBits = getRawValue(parameters, "crush_bits", 10.0f);
    snapshot.crushRate = getRawValue(parameters, "crush_rate", 8000.0f);
    snapshot.crushJitter = getRawValue(parameters, "crush_jitter", 10.0f);
    snapshot.crushTone = getRawValue(parameters, "crush_tone", 0.55f);
    snapshot.crushDrive = getRawValue(parameters, "crush_drive", 0.0f);

    snapshot.satDrive = getRawValue(parameters, "sat_drive", 8.0f);
    snapshot.satTone = getRawValue(parameters, "sat_tone", 0.55f);
    snapshot.satGlue = getRawValue(parameters, "sat_glue", 35.0f);
    snapshot.satHeadroom = getRawValue(parameters, "sat_headroom", 60.0f);
    snapshot.satBias = getRawValue(parameters, "sat_bias", 50.0f);
    return snapshot;
}

bool MusiqueDistortionProcessor::engineUsesOversampling(const distortion::DistortionParams& params) const noexcept
{
    switch (params.engine)
    {
        case distortion: return params.variant != 1;
        case overdrive:
        case fuzz:
        case exciter:
        case consoleSaturation:
            return true;
        case bitCrusher:
        default:
            return false;
    }
}

void MusiqueDistortionProcessor::updateLatencyForParams(const distortion::DistortionParams& params)
{
    const int desiredLatency = engineUsesOversampling(params) ? oversamplingLatencySamples : 0;
    if (desiredLatency != lastLatencySamples)
    {
        setLatencySamples(desiredLatency);
        lastLatencySamples = desiredLatency;
        clearDelayBuffers();
    }
}

void MusiqueDistortionProcessor::copyBuffer(juce::AudioBuffer<float>& destination, const juce::AudioBuffer<float>& source)
{
    const int channels = juce::jmin(destination.getNumChannels(), source.getNumChannels());
    const int samples = juce::jmin(destination.getNumSamples(), source.getNumSamples());
    for (int channel = 0; channel < channels; ++channel)
        destination.copyFrom(channel, 0, source, channel, 0, samples);
}

void MusiqueDistortionProcessor::clearDelayBuffers() noexcept
{
    dryDelayWrite = 0;
    for (auto& channelDelay : dryDelayBuffer)
        std::fill(channelDelay.begin(), channelDelay.end(), 0.0f);
}

void MusiqueDistortionProcessor::resetAllEngines()
{
    distortionEngineState.reset();
    overdriveEngineState.reset();
    fuzzEngineState.reset();
    exciterEngineState.reset();
    bitCrusherEngineState.reset();
    consoleEngineState.reset();
    clearSnapshot();
}

void MusiqueDistortionProcessor::clearSnapshot() noexcept
{
    distortion::DistortionEngineSnapshot snapshot;
    storeSnapshot(snapshot, 0, 0, false);
}

void MusiqueDistortionProcessor::storeSnapshot(const distortion::DistortionEngineSnapshot& snapshot,
                                               int engine,
                                               int variant,
                                               bool oversampled) noexcept
{
    visualPrimary.store(snapshot.primary, std::memory_order_relaxed);
    visualSecondary.store(snapshot.secondary, std::memory_order_relaxed);
    visualTertiary.store(snapshot.tertiary, std::memory_order_relaxed);
    visualQuaternary.store(snapshot.quaternary, std::memory_order_relaxed);
    visualClipPeak.store(snapshot.clipPeak, std::memory_order_relaxed);
    visualEngine.store(engine, std::memory_order_relaxed);
    visualVariant.store(variant, std::memory_order_relaxed);
    visualOversampled.store(oversampled, std::memory_order_relaxed);
    visualClipped.store(snapshot.flagB || snapshot.clipPeak > 0.98f, std::memory_order_relaxed);
}

#if ! MUSIQUE_DISTORTION_DSP_TESTS
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MusiqueDistortionProcessor();
}
#endif
