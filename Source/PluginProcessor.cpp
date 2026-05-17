#include "PluginProcessor.h"
#include "PluginEditor.h"
namespace { static float dbToGain(float dB){ return std::pow(10.0f,dB/20.0f);} }
MusiqueDistortionProcessor::MusiqueDistortionProcessor():AudioProcessor(BusesProperties().withInput("Input",juce::AudioChannelSet::stereo(),true).withOutput("Output",juce::AudioChannelSet::stereo(),true)),parameters(*this,nullptr,"MusiqueDistortion",createParameterLayout()){}
juce::AudioProcessorValueTreeState::ParameterLayout MusiqueDistortionProcessor::createParameterLayout(){ std::vector<std::unique_ptr<juce::RangedAudioParameter>> p; p.push_back(std::make_unique<juce::AudioParameterFloat>("drive","Drive",0.0f,40.0f,12.0f)); p.push_back(std::make_unique<juce::AudioParameterFloat>("tone","Tone",0.0f,1.0f,0.5f)); p.push_back(std::make_unique<juce::AudioParameterFloat>("blend","Blend",0.0f,100.0f,70.0f)); p.push_back(std::make_unique<juce::AudioParameterFloat>("output","Output",-24.0f,12.0f,0.0f)); p.push_back(std::make_unique<juce::AudioParameterChoice>("mode","Mode",juce::StringArray{"Clipper","Bitcrush","Tube"},0)); p.push_back(std::make_unique<juce::AudioParameterFloat>("mix","Mix",0.0f,100.0f,100.0f)); p.push_back(std::make_unique<juce::AudioParameterBool>("bypass","Bypass",false)); p.push_back(std::make_unique<juce::AudioParameterBool>("mono","Mono",false)); return {p.begin(),p.end()}; }
void MusiqueDistortionProcessor::prepareToPlay(double sr, int bs)
{
    preparedSampleRate = sr;
    toneState = { 0.0f, 0.0f };
    wetBuffer.setSize(2, bs, false, false, true);
    oversampling = std::make_unique<juce::dsp::Oversampling<float>>(2, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true);
    oversampling->reset();
    oversampling->initProcessing((size_t) bs);

    oversamplingLatencySamples = oversampling != nullptr
        ? juce::jmax(0, (int) std::round(oversampling->getLatencyInSamples()))
        : 0;
    dryDelayWrite = 0;
    lastOversamplingActive = false;
    for (auto& channelDelay : dryDelayBuffer)
        channelDelay.assign((size_t) juce::jmax(oversamplingLatencySamples, 0), 0.0f);
}

bool MusiqueDistortionProcessor::isOversamplingActive() const noexcept
{
    if (auto* modeParam = parameters.getRawParameterValue("mode"))
        return (int) modeParam->load() != 1 && oversampling != nullptr;

    return false;
}

bool MusiqueDistortionProcessor::isBusesLayoutSupported(const BusesLayout& l) const{ return l.getMainInputChannelSet()==juce::AudioChannelSet::stereo()&&l.getMainOutputChannelSet()==juce::AudioChannelSet::stereo(); }
void MusiqueDistortionProcessor::processBlock(juce::AudioBuffer<float>& b, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    visualState.captureInput(b);
    const int numSamples = b.getNumSamples();
    if (numSamples <= 0)
        return;

    if (*parameters.getRawParameterValue("mono") > 0.5f)
        for (int i = 0; i < numSamples; ++i)
        {
            const float m = 0.5f * (b.getSample(0, i) + b.getSample(1, i));
            b.setSample(0, i, m);
            b.setSample(1, i, m);
        }

    if (*parameters.getRawParameterValue("bypass") > 0.5f)
    {
        b.applyGain(dbToGain(*parameters.getRawParameterValue("output")));
        visualState.captureOutput(b);
        return;
    }

    const float drive = dbToGain(*parameters.getRawParameterValue("drive"));
    const float blend = *parameters.getRawParameterValue("blend") / 100.0f;
    const float mix = *parameters.getRawParameterValue("mix") / 100.0f;
    const float tone = *parameters.getRawParameterValue("tone");
    const int mode = (int) *parameters.getRawParameterValue("mode");
    const float out = dbToGain(*parameters.getRawParameterValue("output"));
    const float safeSampleRate = juce::jmax(1.0f, (float) preparedSampleRate);
    const float toneCutoff = juce::jlimit(120.0f, safeSampleRate * 0.45f, 1200.0f + tone * 13800.0f);
    const float toneCoeff = std::exp(-juce::MathConstants<float>::twoPi * toneCutoff / safeSampleRate);
    const bool useOversampling = mode != 1 && oversampling != nullptr;

    if (useOversampling != lastOversamplingActive)
    {
        dryDelayWrite = 0;
        toneState = { 0.0f, 0.0f };
        for (auto& channelDelay : dryDelayBuffer)
            std::fill(channelDelay.begin(), channelDelay.end(), 0.0f);
        lastOversamplingActive = useOversampling;
    }

    if (mix <= 0.0001f)
    {
        b.applyGain(out);
        visualState.captureOutput(b);
        return;
    }

    wetBuffer.setSize(2, numSamples, false, false, true);
    wetBuffer.makeCopyOf(b, true);

    auto processSample = [&](float x) -> float
    {
        if (mode == 0)
        {
            const float threshold = juce::jmap(blend, 0.0f, 1.0f, 1.0f, 0.22f);
            const float clipped = juce::jlimit(-threshold, threshold, x * drive) / threshold;
            return juce::jlimit(-1.0f, 1.0f, clipped);
        }

        if (mode == 1)
        {
            const float driven = x * drive;
            const float bits = juce::jmap(1.0f - blend, 0.0f, 1.0f, 4.0f, 14.0f);
            const float levels = std::pow(2.0f, bits);
            return juce::jlimit(-1.0f, 1.0f, std::round(driven * levels) / levels);
        }

        const float asym = juce::jmap(blend, 0.0f, 1.0f, 0.0f, 0.35f);
        const float biased = x + asym;
        const float shaped = std::tanh(biased * drive) - std::tanh(asym * drive);
        return juce::jlimit(-1.0f, 1.0f, shaped);
    };

    if (useOversampling)
    {
        juce::dsp::AudioBlock<float> wetBlock(wetBuffer);
        auto upsampledBlock = oversampling->processSamplesUp(wetBlock);
        for (size_t ch = 0; ch < upsampledBlock.getNumChannels(); ++ch)
        {
            auto* channelData = upsampledBlock.getChannelPointer(ch);
            for (size_t i = 0; i < upsampledBlock.getNumSamples(); ++i)
                channelData[i] = processSample(channelData[i]);
        }
        oversampling->processSamplesDown(wetBlock);
    }
    else
    {
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < numSamples; ++i)
                wetBuffer.setSample(ch, i, processSample(wetBuffer.getSample(ch, i)));
    }

    for (int i = 0; i < numSamples; ++i)
    {
        float delayedDry[2] = { b.getSample(0, i), b.getSample(1, i) };
        if (useOversampling && oversamplingLatencySamples > 0)
        {
            const size_t delayIndex = (size_t) dryDelayWrite;
            for (int ch = 0; ch < 2; ++ch)
            {
                auto& channelDelay = dryDelayBuffer[(size_t) ch];
                delayedDry[ch] = channelDelay.empty() ? delayedDry[ch] : channelDelay[delayIndex];
                if (! channelDelay.empty())
                    channelDelay[delayIndex] = b.getSample(ch, i);
            }

            dryDelayWrite = (dryDelayWrite + 1) % oversamplingLatencySamples;
        }

        for (int ch = 0; ch < 2; ++ch)
        {
            const float dry = useOversampling ? delayedDry[ch] : b.getSample(ch, i);
            const float wetRaw = wetBuffer.getSample(ch, i);
            toneState[(size_t) ch] = toneState[(size_t) ch] * toneCoeff + wetRaw * (1.0f - toneCoeff);
            const float dark = toneState[(size_t) ch];
            const float high = wetRaw - dark;
            const float bright = juce::jlimit(-1.25f, 1.25f, wetRaw + high * 0.35f);
            const float wet = juce::jmap(tone, dark, bright);
            b.setSample(ch, i, (dry * (1.0f - mix) + wet * mix) * out);
        }
    }

    visualState.captureOutput(b);
}
void MusiqueDistortionProcessor::getStateInformation(juce::MemoryBlock& d){auto s=parameters.copyState();std::unique_ptr<juce::XmlElement> x(s.createXml());copyXmlToBinary(*x,d);} void MusiqueDistortionProcessor::setStateInformation(const void* data,int size){std::unique_ptr<juce::XmlElement> x(getXmlFromBinary(data,size));if(x&&x->hasTagName(parameters.state.getType()))parameters.replaceState(juce::ValueTree::fromXml(*x));}
juce::AudioProcessorEditor* MusiqueDistortionProcessor::createEditor(){return new MusiqueDistortionEditor(*this);} juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter(){return new MusiqueDistortionProcessor();}
