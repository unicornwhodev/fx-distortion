#pragma once

#include <JuceHeader.h>
#include <array>
#include <cmath>
#include "FXDistortionDSP.h"

namespace distortion
{
struct DistortionParams
{
    int engine = 0;
    int variant = 0;
    int mode = 0;
    bool bypass = false;
    bool mono = false;
    float mix = 100.0f;
    float outputDb = 0.0f;

    float drive = 12.0f;
    float tone = 0.5f;
    float blend = 70.0f;
    float distFocus = 50.0f;
    float distKnee = 0.0f;

    float odDrive = 18.0f;
    float odTone = 0.55f;
    float odBias = 50.0f;
    float odBody = 45.0f;
    float odHeadroom = 55.0f;

    float fuzzGain = 24.0f;
    float fuzzTone = 0.45f;
    float fuzzGate = 20.0f;
    float fuzzOctave = 30.0f;
    float fuzzBias = 50.0f;

    float excAmount = 35.0f;
    float excFreq = 4200.0f;
    float excAir = 40.0f;
    float excTone = 0.6f;
    float excDrive = 8.0f;

    float crushBits = 10.0f;
    float crushRate = 8000.0f;
    float crushJitter = 10.0f;
    float crushTone = 0.55f;
    float crushDrive = 0.0f;

    float satDrive = 8.0f;
    float satTone = 0.55f;
    float satGlue = 35.0f;
    float satHeadroom = 60.0f;
    float satBias = 50.0f;
};

struct DistortionEngineSnapshot
{
    float primary = 0.0f;
    float secondary = 0.0f;
    float tertiary = 0.0f;
    float quaternary = 0.0f;
    float clipPeak = 0.0f;
    bool flagA = false;
    bool flagB = false;
};

inline float safeMean(float numerator, float denominator) noexcept
{
    return denominator > 1.0e-6f ? numerator / denominator : 0.0f;
}

class DistortionEngine
{
public:
    void prepare(double sampleRateIn, int)
    {
        sampleRate = sampleRateIn;
        toneFilter.prepare(sampleRateIn);
        dcBlock.prepare(sampleRateIn);
        reset();
        for (auto* smoother : { &driveSmoothed, &toneSmoothed, &blendSmoothed, &focusSmoothed, &kneeSmoothed })
            smoother->reset(sampleRateIn, 0.02);
    }

    void reset()
    {
        toneFilter.reset();
        dcBlock.reset();
    }

    void process(juce::dsp::AudioBlock<float> block,
                 const DistortionParams& params,
                 double processingSampleRate,
                 DistortionEngineSnapshot& snapshot)
    {
        toneFilter.setSampleRate(processingSampleRate);
        dcBlock.setSampleRate(processingSampleRate);

        driveSmoothed.setTargetValue(params.drive);
        toneSmoothed.setTargetValue(params.tone);
        blendSmoothed.setTargetValue(params.blend);
        focusSmoothed.setTargetValue(params.distFocus);
        kneeSmoothed.setTargetValue(params.distKnee);

        const int variant = juce::jlimit(0, 2, params.variant);
        const int numChannels = (int) block.getNumChannels();
        const int numSamples = (int) block.getNumSamples();
        float clipAccumulator = 0.0f;
        float wetAccumulator = 0.0f;

        for (int sample = 0; sample < numSamples; ++sample)
        {
            const float driveGain = distdsp::dbToGain(driveSmoothed.getNextValue());
            const float tone = toneSmoothed.getNextValue();
            const float blend = distdsp::normalisePercent(blendSmoothed.getNextValue());
            const float focus = focusSmoothed.getNextValue();
            const float knee = distdsp::normalisePercent(kneeSmoothed.getNextValue());

            for (int channel = 0; channel < numChannels; ++channel)
            {
                auto* channelData = block.getChannelPointer((size_t) channel);
                float shaped = distdsp::legacyShapeSample(channelData[sample], driveGain, blend, variant, knee);
                if (variant == 2)
                    shaped = dcBlock.process(channel, shaped);
                const float toned = distdsp::applyToneFocus(shaped, channel, tone, focus, toneFilter);
                clipAccumulator = juce::jmax(clipAccumulator, std::abs(toned));
                wetAccumulator += std::abs(toned);
                channelData[sample] = toned;
            }
        }

        snapshot.primary = variant == 0 ? juce::jmap(distdsp::normalisePercent(params.blend), 1.0f, 0.22f)
                                        : (variant == 1 ? juce::jmap(1.0f - distdsp::normalisePercent(params.blend), 4.0f, 14.0f)
                                                        : juce::jmap(distdsp::normalisePercent(params.blend), 0.0f, 0.35f));
        snapshot.secondary = params.distFocus;
        snapshot.tertiary = safeMean(wetAccumulator, (float) juce::jmax(1, numSamples * numChannels));
        snapshot.quaternary = params.distKnee;
        snapshot.clipPeak = clipAccumulator;
        snapshot.flagA = variant == 1;
        snapshot.flagB = clipAccumulator > 0.98f;
    }

private:
    double sampleRate = 44100.0;
    distdsp::LowPassState toneFilter;
    distdsp::DCBlockerState dcBlock;
    juce::SmoothedValue<float> driveSmoothed, toneSmoothed, blendSmoothed, focusSmoothed, kneeSmoothed;
};

class OverdriveEngine
{
public:
    void prepare(double sampleRateIn, int)
    {
        sampleRate = sampleRateIn;
        toneFilter.prepare(sampleRateIn);
        bodyFilter.prepare(sampleRateIn);
        dcBlock.prepare(sampleRateIn);
        reset();
        for (auto* smoother : { &driveSmoothed, &toneSmoothed, &biasSmoothed, &bodySmoothed, &headroomSmoothed })
            smoother->reset(sampleRateIn, 0.02);
    }

    void reset()
    {
        toneFilter.reset();
        bodyFilter.reset();
        dcBlock.reset();
    }

    void process(juce::dsp::AudioBlock<float> block,
                 const DistortionParams& params,
                 double processingSampleRate,
                 DistortionEngineSnapshot& snapshot)
    {
        toneFilter.setSampleRate(processingSampleRate);
        bodyFilter.setSampleRate(processingSampleRate);
        dcBlock.setSampleRate(processingSampleRate);

        driveSmoothed.setTargetValue(params.odDrive);
        toneSmoothed.setTargetValue(params.odTone);
        biasSmoothed.setTargetValue(params.odBias);
        bodySmoothed.setTargetValue(params.odBody);
        headroomSmoothed.setTargetValue(params.odHeadroom);

        const int variant = juce::jlimit(0, 2, params.variant);
        const int numChannels = (int) block.getNumChannels();
        const int numSamples = (int) block.getNumSamples();
        float clipAccumulator = 0.0f;
        float bodyAccumulator = 0.0f;

        for (int sample = 0; sample < numSamples; ++sample)
        {
            const float driveGain = distdsp::dbToGain(driveSmoothed.getNextValue());
            const float tone = toneSmoothed.getNextValue();
            const float bias = biasSmoothed.getNextValue();
            const float body = bodySmoothed.getNextValue();
            const float headroom = headroomSmoothed.getNextValue();
            const float bodyNorm = distdsp::normalisePercent(body);

            for (int channel = 0; channel < numChannels; ++channel)
            {
                auto* channelData = block.getChannelPointer((size_t) channel);
                const float lowBody = bodyFilter.process(channel, channelData[sample], 180.0f + bodyNorm * 860.0f);
                float shaped = distdsp::overdriveShapeSample(channelData[sample] + lowBody * bodyNorm * 0.3f,
                                                             driveGain,
                                                             distdsp::normalisePercent(bias),
                                                             distdsp::normalisePercent(headroom),
                                                             variant);
                shaped = dcBlock.process(channel, shaped);
                shaped = distdsp::applyToneFocus(shaped, channel, tone, 50.0f + (bias - 50.0f) * 0.35f, toneFilter);
                clipAccumulator = juce::jmax(clipAccumulator, std::abs(shaped));
                bodyAccumulator += std::abs(lowBody);
                channelData[sample] = shaped;
            }
        }

        snapshot.primary = params.odHeadroom;
        snapshot.secondary = params.odBias;
        snapshot.tertiary = safeMean(bodyAccumulator, (float) juce::jmax(1, numSamples * numChannels));
        snapshot.quaternary = params.odDrive;
        snapshot.clipPeak = clipAccumulator;
        snapshot.flagA = variant == 2;
        snapshot.flagB = clipAccumulator > 0.98f;
    }

private:
    double sampleRate = 44100.0;
    distdsp::LowPassState toneFilter;
    distdsp::LowPassState bodyFilter;
    distdsp::DCBlockerState dcBlock;
    juce::SmoothedValue<float> driveSmoothed, toneSmoothed, biasSmoothed, bodySmoothed, headroomSmoothed;
};

class FuzzEngine
{
public:
    void prepare(double sampleRateIn, int)
    {
        sampleRate = sampleRateIn;
        toneFilter.prepare(sampleRateIn);
        dcBlock.prepare(sampleRateIn);
        reset();
        for (auto* smoother : { &gainSmoothed, &toneSmoothed, &gateSmoothed, &octaveSmoothed, &biasSmoothed })
            smoother->reset(sampleRateIn, 0.02);
    }

    void reset()
    {
        toneFilter.reset();
        dcBlock.reset();
        gateEnvelope.fill(0.0f);
    }

    void process(juce::dsp::AudioBlock<float> block,
                 const DistortionParams& params,
                 double processingSampleRate,
                 DistortionEngineSnapshot& snapshot)
    {
        toneFilter.setSampleRate(processingSampleRate);
        dcBlock.setSampleRate(processingSampleRate);

        gainSmoothed.setTargetValue(params.fuzzGain);
        toneSmoothed.setTargetValue(params.fuzzTone);
        gateSmoothed.setTargetValue(params.fuzzGate);
        octaveSmoothed.setTargetValue(params.fuzzOctave);
        biasSmoothed.setTargetValue(params.fuzzBias);

        const int variant = juce::jlimit(0, 2, params.variant);
        const int numChannels = (int) block.getNumChannels();
        const int numSamples = (int) block.getNumSamples();
        float gateAccumulator = 0.0f;
        float clipAccumulator = 0.0f;

        for (int sample = 0; sample < numSamples; ++sample)
        {
            const float driveGain = distdsp::dbToGain(gainSmoothed.getNextValue());
            const float tone = toneSmoothed.getNextValue();
            const float gate = distdsp::normalisePercent(gateSmoothed.getNextValue());
            const float octave = distdsp::normalisePercent(octaveSmoothed.getNextValue());
            const float bias = biasSmoothed.getNextValue();
            const float threshold = juce::jmap(gate, 0.0f, 1.0f, 0.0f, 0.18f);

            for (int channel = 0; channel < numChannels; ++channel)
            {
                auto* channelData = block.getChannelPointer((size_t) channel);
                const float input = channelData[sample];
                auto& env = gateEnvelope[(size_t) channel];
                env = juce::jmax(std::abs(input), env * 0.992f);
                const float gateOpen = threshold <= 0.0001f ? 1.0f : juce::jlimit(0.0f, 1.0f, (env - threshold * 0.45f) / juce::jmax(0.01f, threshold));
                float shaped = distdsp::fuzzShapeSample(input,
                                                        driveGain,
                                                        distdsp::normalisePercent(bias),
                                                        octave,
                                                        variant);
                shaped *= variant == 2 ? std::pow(gateOpen, 1.8f) : gateOpen;
                shaped = dcBlock.process(channel, shaped);
                shaped = distdsp::applyToneFocus(shaped, channel, tone, 55.0f + octave * 20.0f, toneFilter);
                gateAccumulator += gateOpen;
                clipAccumulator = juce::jmax(clipAccumulator, std::abs(shaped));
                channelData[sample] = shaped;
            }
        }

        snapshot.primary = safeMean(gateAccumulator, (float) juce::jmax(1, numSamples * numChannels));
        snapshot.secondary = params.fuzzOctave;
        snapshot.tertiary = params.fuzzGain;
        snapshot.quaternary = params.fuzzBias;
        snapshot.clipPeak = clipAccumulator;
        snapshot.flagA = variant == 1;
        snapshot.flagB = clipAccumulator > 0.98f;
    }

private:
    double sampleRate = 44100.0;
    distdsp::LowPassState toneFilter;
    distdsp::DCBlockerState dcBlock;
    std::array<float, distdsp::maxChannels> gateEnvelope {};
    juce::SmoothedValue<float> gainSmoothed, toneSmoothed, gateSmoothed, octaveSmoothed, biasSmoothed;
};

class ExciterEngine
{
public:
    void prepare(double sampleRateIn, int)
    {
        sampleRate = sampleRateIn;
        highPass.prepare(sampleRateIn);
        toneFilter.prepare(sampleRateIn);
        reset();
        for (auto* smoother : { &amountSmoothed, &freqSmoothed, &airSmoothed, &toneSmoothed, &driveSmoothed })
            smoother->reset(sampleRateIn, 0.02);
    }

    void reset()
    {
        highPass.reset();
        toneFilter.reset();
        highEnvelope.fill(0.0f);
    }

    void process(juce::dsp::AudioBlock<float> block,
                 const DistortionParams& params,
                 double processingSampleRate,
                 DistortionEngineSnapshot& snapshot)
    {
        highPass.setSampleRate(processingSampleRate);
        toneFilter.setSampleRate(processingSampleRate);

        amountSmoothed.setTargetValue(params.excAmount);
        freqSmoothed.setTargetValue(params.excFreq);
        airSmoothed.setTargetValue(params.excAir);
        toneSmoothed.setTargetValue(params.excTone);
        driveSmoothed.setTargetValue(params.excDrive);

        const int variant = juce::jlimit(0, 2, params.variant);
        const int numChannels = (int) block.getNumChannels();
        const int numSamples = (int) block.getNumSamples();
        float harmonicEnergy = 0.0f;
        float sourceEnergy = 0.0f;
        float clipAccumulator = 0.0f;

        for (int sample = 0; sample < numSamples; ++sample)
        {
            float cutoff = freqSmoothed.getNextValue();
            const float amountBase = distdsp::normalisePercent(amountSmoothed.getNextValue());
            const float air = distdsp::normalisePercent(airSmoothed.getNextValue());
            const float tone = toneSmoothed.getNextValue();
            const float drive = driveSmoothed.getNextValue();
            if (variant == 1)
                cutoff *= 0.8f;
            else if (variant == 2)
                cutoff *= 1.25f;

            float amount = amountBase;
            if (variant == 0)
                amount *= 0.85f;
            else if (variant == 2)
                amount *= 1.18f;

            for (int channel = 0; channel < numChannels; ++channel)
            {
                auto* channelData = block.getChannelPointer((size_t) channel);
                const float input = channelData[sample];
                const float highBand = highPass.process(channel, input, cutoff);
                auto& env = highEnvelope[(size_t) channel];
                env = juce::jmax(std::abs(highBand), env * 0.985f);
                float harmonic = std::tanh(highBand * distdsp::dbToGain(drive) * (1.6f + air * 1.8f)) - highBand * 0.65f;
                harmonic = distdsp::applyToneFocus(harmonic, channel, tone, 55.0f + air * 30.0f, toneFilter);
                const float bandPresence = juce::jlimit(0.0f, 1.0f, env * (3.5f + air * 4.5f));
                const float mixed = input + harmonic * amount * (0.65f + air * 0.7f) * bandPresence;
                harmonicEnergy += std::abs(harmonic);
                sourceEnergy += std::abs(highBand);
                const float limited = juce::jlimit(-1.2f, 1.2f, mixed);
                clipAccumulator = juce::jmax(clipAccumulator, std::abs(limited));
                channelData[sample] = limited;
            }
        }

        snapshot.primary = params.excFreq;
        snapshot.secondary = params.excAmount;
        snapshot.tertiary = safeMean(harmonicEnergy, juce::jmax(1.0f, sourceEnergy));
        snapshot.quaternary = params.excAir;
        snapshot.clipPeak = clipAccumulator;
        snapshot.flagA = variant == 2;
        snapshot.flagB = clipAccumulator > 0.98f;
    }

private:
    double sampleRate = 44100.0;
    distdsp::HighPassState highPass;
    distdsp::LowPassState toneFilter;
    std::array<float, distdsp::maxChannels> highEnvelope {};
    juce::SmoothedValue<float> amountSmoothed, freqSmoothed, airSmoothed, toneSmoothed, driveSmoothed;
};

class BitCrusherEngine
{
public:
    void prepare(double sampleRateIn, int)
    {
        sampleRate = sampleRateIn;
        toneFilter.prepare(sampleRateIn);
        reset();
        for (auto* smoother : { &bitsSmoothed, &rateSmoothed, &jitterSmoothed, &toneSmoothed, &driveSmoothed })
            smoother->reset(sampleRateIn, 0.01);
    }

    void reset()
    {
        toneFilter.reset();
        slewLimiter.reset();
        heldSample.fill(0.0f);
        holdSamplesRemaining.fill(0);
        rngState = { 0x12345u, 0x54321u };
    }

    void process(juce::dsp::AudioBlock<float> block,
                 const DistortionParams& params,
                 double processingSampleRate,
                 DistortionEngineSnapshot& snapshot)
    {
        sampleRate = processingSampleRate;
        toneFilter.setSampleRate(processingSampleRate);

        bitsSmoothed.setTargetValue(params.crushBits);
        rateSmoothed.setTargetValue(params.crushRate);
        jitterSmoothed.setTargetValue(params.crushJitter);
        toneSmoothed.setTargetValue(params.crushTone);
        driveSmoothed.setTargetValue(params.crushDrive);

        const int variant = juce::jlimit(0, 2, params.variant);
        const int numChannels = (int) block.getNumChannels();
        const int numSamples = (int) block.getNumSamples();
        float stepAccumulator = 0.0f;
        float clipAccumulator = 0.0f;

        for (int sample = 0; sample < numSamples; ++sample)
        {
            const float bits = bitsSmoothed.getNextValue();
            const float holdRate = rateSmoothed.getNextValue();
            const float jitter = distdsp::normalisePercent(jitterSmoothed.getNextValue());
            const float tone = toneSmoothed.getNextValue();
            const float drive = distdsp::dbToGain(driveSmoothed.getNextValue());
            const float levels = std::pow(2.0f, juce::jlimit(4.0f, 16.0f, bits));
            const int baseHold = juce::jmax(1, (int) std::round(sampleRate / juce::jmax(60.0f, holdRate)));

            for (int channel = 0; channel < numChannels; ++channel)
            {
                auto* channelData = block.getChannelPointer((size_t) channel);
                auto& remaining = holdSamplesRemaining[(size_t) channel];
                auto& held = heldSample[(size_t) channel];
                if (remaining <= 0)
                {
                    const float jitterRange = 1.0f + jitter * 0.65f * distdsp::fastRandSigned(rngState[(size_t) channel]);
                    remaining = juce::jmax(1, (int) std::round((float) baseHold * juce::jlimit(0.35f, 1.75f, jitterRange)));
                    held = channelData[sample] * drive;
                }

                --remaining;

                float crushed = std::round(held * levels) / juce::jmax(1.0f, levels);
                if (variant == 1)
                    crushed = held;
                else if (variant == 2)
                    crushed = slewLimiter.process(channel, crushed, 0.025f + (1.0f - jitter) * 0.09f);

                crushed = distdsp::applyToneFocus(crushed, channel, tone, 40.0f + bits * 3.0f, toneFilter);
                clipAccumulator = juce::jmax(clipAccumulator, std::abs(crushed));
                stepAccumulator += (float) baseHold;
                channelData[sample] = juce::jlimit(-1.0f, 1.0f, crushed);
            }
        }

        snapshot.primary = params.crushBits;
        snapshot.secondary = params.crushRate;
        snapshot.tertiary = params.crushJitter;
        snapshot.quaternary = safeMean(stepAccumulator, (float) juce::jmax(1, numSamples * numChannels));
        snapshot.clipPeak = clipAccumulator;
        snapshot.flagA = variant != 0;
        snapshot.flagB = clipAccumulator > 0.98f;
    }

private:
    double sampleRate = 44100.0;
    distdsp::LowPassState toneFilter;
    distdsp::SlewLimiterState slewLimiter;
    std::array<float, distdsp::maxChannels> heldSample {};
    std::array<int, distdsp::maxChannels> holdSamplesRemaining {};
    std::array<uint32_t, distdsp::maxChannels> rngState { 0x12345u, 0x54321u };
    juce::SmoothedValue<float> bitsSmoothed, rateSmoothed, jitterSmoothed, toneSmoothed, driveSmoothed;
};

class ConsoleSaturationEngine
{
public:
    void prepare(double sampleRateIn, int)
    {
        sampleRate = sampleRateIn;
        toneFilter.prepare(sampleRateIn);
        dcBlock.prepare(sampleRateIn);
        reset();
        for (auto* smoother : { &driveSmoothed, &toneSmoothed, &glueSmoothed, &headroomSmoothed, &biasSmoothed })
            smoother->reset(sampleRateIn, 0.03);
    }

    void reset()
    {
        toneFilter.reset();
        dcBlock.reset();
        envelope.fill(0.0f);
    }

    void process(juce::dsp::AudioBlock<float> block,
                 const DistortionParams& params,
                 double processingSampleRate,
                 DistortionEngineSnapshot& snapshot)
    {
        toneFilter.setSampleRate(processingSampleRate);
        dcBlock.setSampleRate(processingSampleRate);

        driveSmoothed.setTargetValue(params.satDrive);
        toneSmoothed.setTargetValue(params.satTone);
        glueSmoothed.setTargetValue(params.satGlue);
        headroomSmoothed.setTargetValue(params.satHeadroom);
        biasSmoothed.setTargetValue(params.satBias);

        const int variant = juce::jlimit(0, 2, params.variant);
        const int numChannels = (int) block.getNumChannels();
        const int numSamples = (int) block.getNumSamples();
        float compressionAccumulator = 0.0f;
        float clipAccumulator = 0.0f;

        for (int sample = 0; sample < numSamples; ++sample)
        {
            const float driveGain = distdsp::dbToGain(driveSmoothed.getNextValue());
            const float tone = toneSmoothed.getNextValue();
            const float glue = distdsp::normalisePercent(glueSmoothed.getNextValue());
            const float headroom = distdsp::normalisePercent(headroomSmoothed.getNextValue());
            const float bias = biasSmoothed.getNextValue();

            float linkedPeak = 0.0f;
            for (int channel = 0; channel < numChannels; ++channel)
                linkedPeak = juce::jmax(linkedPeak, std::abs(block.getChannelPointer((size_t) channel)[sample]));

            for (int channel = 0; channel < numChannels; ++channel)
            {
                auto* channelData = block.getChannelPointer((size_t) channel);
                auto& env = envelope[(size_t) channel];
                env = juce::jmax(linkedPeak, env * (variant == 1 ? 0.992f : 0.996f));
                const float compression = 1.0f / (1.0f + env * driveGain * glue * (variant == 2 ? 0.65f : 0.42f));
                float shaped = distdsp::consoleShapeSample(channelData[sample] * compression,
                                                           driveGain,
                                                           distdsp::normalisePercent(bias),
                                                           headroom,
                                                           variant);
                shaped = dcBlock.process(channel, shaped);
                shaped = distdsp::applyToneFocus(shaped, channel, tone, 45.0f + headroom * 25.0f, toneFilter);
                compressionAccumulator += compression;
                clipAccumulator = juce::jmax(clipAccumulator, std::abs(shaped));
                channelData[sample] = shaped;
            }
        }

        snapshot.primary = params.satHeadroom;
        snapshot.secondary = params.satGlue;
        snapshot.tertiary = safeMean(compressionAccumulator, (float) juce::jmax(1, numSamples * numChannels));
        snapshot.quaternary = params.satDrive;
        snapshot.clipPeak = clipAccumulator;
        snapshot.flagA = variant == 1;
        snapshot.flagB = clipAccumulator > 0.98f;
    }

private:
    double sampleRate = 44100.0;
    distdsp::LowPassState toneFilter;
    distdsp::DCBlockerState dcBlock;
    std::array<float, distdsp::maxChannels> envelope {};
    juce::SmoothedValue<float> driveSmoothed, toneSmoothed, glueSmoothed, headroomSmoothed, biasSmoothed;
};
} // namespace distortion
