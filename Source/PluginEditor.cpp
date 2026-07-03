#include "PluginEditor.h"
#include "BinaryData.h"
#include "FXDistortionDSP.h"

namespace
{
const std::array<MusiqueDistortionEditor::EngineUiConfig, MusiqueDistortionProcessor::numEngines> kEngineConfigs {{
    {
        "DISTORTION",
        { "drive", "tone", "blend", "dist_focus", "dist_knee" },
        { "DRIVE", "TONE", "CHAR", "FOCUS", "KNEE", "MIX" },
        juce::StringArray { "Clipper", "Bitcrush", "Tube" }
    },
    {
        "OVERDRIVE",
        { "od_drive", "od_tone", "od_bias", "od_body", "od_headroom" },
        { "DRIVE", "TONE", "BIAS", "BODY", "HEADROOM", "MIX" },
        juce::StringArray { "Soft", "Amp", "Edge" }
    },
    {
        "FUZZ",
        { "fuzz_gain", "fuzz_tone", "fuzz_gate", "fuzz_octave", "fuzz_bias" },
        { "GAIN", "TONE", "GATE", "OCTAVE", "BIAS", "MIX" },
        juce::StringArray { "Vintage", "Octave", "Gate" }
    },
    {
        "EXCITER",
        { "exc_amount", "exc_freq", "exc_air", "exc_tone", "exc_drive" },
        { "AMOUNT", "FREQ", "AIR", "TONE", "DRIVE", "MIX" },
        juce::StringArray { "Tape", "Presence", "Air" }
    },
    {
        "BIT CRUSHER",
        { "crush_bits", "crush_rate", "crush_jitter", "crush_tone", "crush_drive" },
        { "BITS", "RATE", "JITTER", "TONE", "DRIVE", "MIX" },
        juce::StringArray { "Crunch", "Downsample", "Retro" }
    },
    {
        "CONSOLE",
        { "sat_drive", "sat_tone", "sat_glue", "sat_headroom", "sat_bias" },
        { "DRIVE", "TONE", "GLUE", "HEADROOM", "BIAS", "MIX" },
        juce::StringArray { "Clean", "Bus", "Transformer" }
    }
}};

float getParamValue(const juce::AudioProcessorValueTreeState& apvts, const juce::String& paramId, float fallback = 0.0f)
{
    if (auto* param = apvts.getRawParameterValue(paramId))
        return param->load();
    return fallback;
}

int getChoiceValue(const juce::AudioProcessorValueTreeState& apvts, const juce::String& paramId, int fallback = 0)
{
    return (int) std::round(getParamValue(apvts, paramId, (float) fallback));
}

void setParameter(juce::AudioProcessorValueTreeState& apvts, const juce::String& paramId, float value)
{
    if (auto* parameter = apvts.getParameter(paramId))
        parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}

void setupButton(juce::TextButton& button, bool toggle = false)
{
    button.setColour(juce::TextButton::buttonColourId, fx::col::surfSecondary);
    button.setColour(juce::TextButton::textColourOffId, fx::col::textPrimary);
    if (toggle)
        button.setClickingTogglesState(true);
}

void setupKnob(juce::Slider& slider, juce::Label& label, const char* text)
{
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 62, 16);
    label.setText(text, juce::dontSendNotification);
    label.setFont(juce::Font(juce::FontOptions{}.withHeight(fx::font::label).withStyle("Bold")));
    label.setJustificationType(juce::Justification::centred);
    label.setColour(juce::Label::textColourId, fx::col::textMuted);
}

void drawBadge(juce::Graphics& g, juce::Rectangle<float> rect, const juce::String& text, juce::Colour colour)
{
    g.setColour(colour.withAlpha(0.16f));
    g.fillRoundedRectangle(rect, 8.0f);
    g.setColour(colour.withAlpha(0.6f));
    g.drawRoundedRectangle(rect, 8.0f, 1.0f);
    g.setColour(fx::col::textPrimary);
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(10.0f).withStyle("Bold")));
    g.drawText(text, rect.toNearestInt(), juce::Justification::centred);
}
}

MusiqueDistortionEditor::MusiqueDistortionEditor(MusiqueDistortionProcessor& processor)
    : AudioProcessorEditor(&processor), proc(processor)
{
    setLookAndFeel(&lnf);
    setSize(fx::dim::appW, fx::dim::appH);

    titleLabel.setText("DISTORTION", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(juce::FontOptions{}.withHeight(fx::font::header).withStyle("Bold")));
    titleLabel.setJustificationType(juce::Justification::centredLeft);
    titleLabel.setColour(juce::Label::textColourId, fx::col::textPrimary);
    addAndMakeVisible(titleLabel);

    pluginIcon = juce::ImageCache::getFromMemory(BinaryData::icon_small_png, BinaryData::icon_small_pngSize);
    logoImg = juce::ImageCache::getFromMemory(BinaryData::logo_png, BinaryData::logo_pngSize);

    for (auto* button : { &bypassBtn, &monoBtn, &modeBtn, &statusBtn, &prevBtn, &nextBtn, &saveBtn, &abBtn })
    {
        setupButton(*button, button == &bypassBtn || button == &monoBtn);
        addAndMakeVisible(*button);
    }

    bypassBtn.setTooltip("Native host bypass parameter");
    monoBtn.setTooltip("Fold the input to mono before the active engine");
    modeBtn.setTooltip("Current engine variant and route state");
    statusBtn.setTooltip("Current live engine status");
    modeBtn.onClick = [] {};
    statusBtn.onClick = [] {};

    addAndMakeVisible(presetBox);
    addAndMakeVisible(engineBox);
    addAndMakeVisible(variantBox);
    engineBox.addItemList(juce::StringArray {
        "Distortion", "Overdrive", "Fuzz", "Exciter", "Bit Crusher", "Console Saturation"
    }, 1);

    for (int index = 0; index < 6; ++index)
    {
        setupKnob(knobs[(size_t) index], knobLabels[(size_t) index], index == 5 ? "MIX" : "");
        addAndMakeVisible(knobs[(size_t) index]);
        addAndMakeVisible(knobLabels[(size_t) index]);
    }

    addAndMakeVisible(inMeter);
    addAndMakeVisible(outMeter);
    outputSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    outputSlider.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    addAndMakeVisible(outputSlider);

    clipLED.setAccent(fx::accent::distortion);
    addAndMakeVisible(clipLED);

    versionLabel.setText("Musique Distortion v1.1", juce::dontSendNotification);
    versionLabel.setFont(juce::Font(juce::FontOptions{}.withHeight(fx::font::footer)));
    versionLabel.setColour(juce::Label::textColourId, fx::col::textMuted);
    versionLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(versionLabel);

    mixAtt = std::make_unique<SliderAttach>(proc.getAPVTS(), "mix", knobs[5]);
    outAtt = std::make_unique<SliderAttach>(proc.getAPVTS(), "output", outputSlider);
    engineAtt = std::make_unique<ComboAttach>(proc.getAPVTS(), "engine", engineBox);
    bypassAtt = std::make_unique<ButtonAttach>(proc.getAPVTS(), "bypass", bypassBtn);
    monoAtt = std::make_unique<ButtonAttach>(proc.getAPVTS(), "mono", monoBtn);

    loadPresets();
    abStateA = proc.getAPVTS().copyState();
    abStateB = abStateA.createCopy();
    showingA = true;
    abBtn.setButtonText("A");

    presetBox.onChange = [this]
    {
        const int presetIndex = presetBox.getSelectedItemIndex();
        if (presetIndex <= 0 || presets == nullptr)
            return;

        const int presetArrayIndex = presetIndex - 1;
        if (presetArrayIndex >= presets->size())
            return;

        auto preset = presets->getReference(presetArrayIndex);
        normalisePreset(preset);
        fx::preset::applyToAPVTS(proc.getAPVTS(), preset);
        proc.postExternalStateChange();
        abStateA = proc.getAPVTS().copyState();
        abStateB = abStateA.createCopy();
        showingA = true;
        abBtn.setButtonText("A");
        rebuildEngineUi(true);
    };

    prevBtn.onClick = [this]
    {
        const int index = presetBox.getSelectedItemIndex();
        if (index > 0)
            presetBox.setSelectedItemIndex(index - 1);
    };

    nextBtn.onClick = [this]
    {
        const int index = presetBox.getSelectedItemIndex();
        if (index < presetBox.getNumItems() - 1)
            presetBox.setSelectedItemIndex(index + 1);
    };

    saveBtn.onClick = [this]
    {
        const auto name = juce::String("User_") + juce::Time::getCurrentTime().formatted("%H%M%S");
        if (fx::preset::saveUserPreset("fx-distortion", name, MusiqueDistortionProcessor::getAllParameterIds(), proc.getAPVTS()))
        {
            loadPresets();
            if (presetBox.getNumItems() > 1)
                presetBox.setSelectedItemIndex(presetBox.getNumItems() - 1);
        }
    };

    abBtn.onClick = [this]
    {
        storeCurrentABSlot();
        recallABSlot(!showingA);
    };

    engineBox.onChange = [this] { rebuildEngineUi(true); };
    variantBox.onChange = [this]
    {
        applyVariantSelection(getCurrentEngineIndex(), variantBox.getSelectedItemIndex());
        proc.postExternalStateChange();
        rebuildEngineUi(true);
    };

    rebuildEngineUi(true);
    startTimerHz(fx::anim::fftRefreshHz);
}

MusiqueDistortionEditor::~MusiqueDistortionEditor()
{
    setLookAndFeel(nullptr);
}

void MusiqueDistortionEditor::loadPresets()
{
    presets = std::make_shared<juce::Array<juce::var>>(fx::preset::loadAllPresets("fx-distortion"));
    for (auto& preset : *presets)
        normalisePreset(preset);
    refreshPresetBox();
}

void MusiqueDistortionEditor::refreshPresetBox()
{
    presetBox.clear(juce::dontSendNotification);
    presetBox.addItem("Current State", 1);

    if (presets != nullptr)
    {
        int itemId = 2;
        for (auto& preset : *presets)
        {
            if (auto* object = preset.getDynamicObject())
                presetBox.addItem(object->getProperty("name").toString(), itemId++);
        }
    }

    presetBox.setSelectedId(1, juce::dontSendNotification);
}

void MusiqueDistortionEditor::normalisePreset(juce::var& preset) const
{
    MusiqueDistortionProcessor::normalisePresetObject(preset);
}

int MusiqueDistortionEditor::getCurrentEngineIndex() const
{
    return juce::jlimit(0, MusiqueDistortionProcessor::numEngines - 1, getChoiceValue(proc.getAPVTS(), "engine", 0));
}

int MusiqueDistortionEditor::getCurrentVariantIndex(int engineIndex) const
{
    if (engineIndex == MusiqueDistortionProcessor::distortion)
        return juce::jlimit(0, 2, getChoiceValue(proc.getAPVTS(), "mode", 0));

    const auto& config = kEngineConfigs[(size_t) engineIndex];
    return juce::jlimit(0, config.variants.size() - 1, getChoiceValue(proc.getAPVTS(), "variant", 0));
}

void MusiqueDistortionEditor::rebuildEngineUi(bool force)
{
    const int engineIndex = getCurrentEngineIndex();
    if (!force && engineIndex == displayedEngine)
        return;

    displayedEngine = engineIndex;
    const auto& config = kEngineConfigs[(size_t) engineIndex];
    titleLabel.setText("DISTORTION", juce::dontSendNotification);
    for (int index = 0; index < 6; ++index)
        knobLabels[(size_t) index].setText(config.labels[(size_t) index], juce::dontSendNotification);

    bindEngineKnobs(engineIndex);
    rebuildVariantItems(engineIndex);
    updateStatusButtons();
    repaint(0, fx::dim::headerH + fx::dim::presetBarH, getWidth(), fx::dim::visualH);
}

void MusiqueDistortionEditor::bindEngineKnobs(int engineIndex)
{
    const auto& config = kEngineConfigs[(size_t) engineIndex];
    for (int index = 0; index < 5; ++index)
        engineKnobAtts[(size_t) index] = std::make_unique<SliderAttach>(proc.getAPVTS(), config.paramIds[(size_t) index], knobs[(size_t) index]);
}

void MusiqueDistortionEditor::rebuildVariantItems(int engineIndex)
{
    const auto& config = kEngineConfigs[(size_t) engineIndex];
    variantBox.clear(juce::dontSendNotification);
    variantBox.addItemList(config.variants, 1);
    variantBox.setSelectedItemIndex(getCurrentVariantIndex(engineIndex), juce::dontSendNotification);
}

void MusiqueDistortionEditor::applyVariantSelection(int engineIndex, int variantIndex)
{
    const auto& config = kEngineConfigs[(size_t) engineIndex];
    const int clampedVariant = juce::jlimit(0, config.variants.size() - 1, variantIndex);
    if (engineIndex == MusiqueDistortionProcessor::distortion)
    {
        setParameter(proc.getAPVTS(), "mode", (float) clampedVariant);
        setParameter(proc.getAPVTS(), "variant", (float) clampedVariant);
        return;
    }

    setParameter(proc.getAPVTS(), "variant", (float) clampedVariant);
}

void MusiqueDistortionEditor::storeCurrentABSlot()
{
    const auto currentState = proc.getAPVTS().copyState();
    if (!abStateA.isValid())
    {
        abStateA = currentState;
        abStateB = currentState.createCopy();
        showingA = true;
        return;
    }

    if (showingA)
        abStateA = currentState;
    else
        abStateB = currentState;
}

void MusiqueDistortionEditor::recallABSlot(bool showSlotA)
{
    if (!abStateA.isValid())
        return;

    proc.getAPVTS().replaceState(showSlotA ? abStateA : abStateB);
    proc.postExternalStateChange();
    showingA = showSlotA;
    abBtn.setButtonText(showingA ? "A" : "B");
    rebuildEngineUi(true);
}

void MusiqueDistortionEditor::updateStatusButtons()
{
    const auto snapshot = proc.getDistortionSnapshot();
    const int engineIndex = getCurrentEngineIndex();
    const auto& config = kEngineConfigs[(size_t) engineIndex];
    const int variantIndex = getCurrentVariantIndex(engineIndex);
    const bool mono = getParamValue(proc.getAPVTS(), "mono") > 0.5f;

    monoBtn.setButtonText(mono ? "MONO IN" : "STEREO IN");
    modeBtn.setButtonText(config.variants[variantIndex].toUpperCase() + (snapshot.oversampled ? " 4X" : " RAW"));
    modeBtn.setColour(juce::TextButton::buttonColourId,
        snapshot.oversampled ? fx::accent::distortion.withAlpha(0.18f) : fx::col::surfSecondary);

    juce::String statusText;
    switch (engineIndex)
    {
        case MusiqueDistortionProcessor::distortion:
            statusText = variantIndex == 1
                ? "BITS " + juce::String(snapshot.primary, 1)
                : "CHAR " + juce::String(getParamValue(proc.getAPVTS(), "blend"), 0) + "%";
            break;
        case MusiqueDistortionProcessor::overdrive:
            statusText = "HR " + juce::String(getParamValue(proc.getAPVTS(), "od_headroom"), 0) + "%";
            break;
        case MusiqueDistortionProcessor::fuzz:
            statusText = "GATE " + juce::String(snapshot.primary * 100.0f, 0) + "%";
            break;
        case MusiqueDistortionProcessor::exciter:
            statusText = "HF " + juce::String(getParamValue(proc.getAPVTS(), "exc_freq"), 0) + " Hz";
            break;
        case MusiqueDistortionProcessor::bitCrusher:
            statusText = juce::String((int) std::round(getParamValue(proc.getAPVTS(), "crush_bits")))
                + " bit @ " + juce::String(getParamValue(proc.getAPVTS(), "crush_rate"), 0);
            break;
        case MusiqueDistortionProcessor::consoleSaturation:
            statusText = "GLUE " + juce::String(getParamValue(proc.getAPVTS(), "sat_glue"), 0) + "%";
            break;
        default:
            statusText = "READY";
            break;
    }

    if (snapshot.clipped)
        statusText += "  HOT";

    statusBtn.setButtonText(statusText);
    statusBtn.setColour(juce::TextButton::buttonColourId, fx::accent::distortion.withAlpha(0.14f));
}

void MusiqueDistortionEditor::timerCallback()
{
    rebuildEngineUi();

    const auto inputLevels = proc.getVisualState().getInputLevels();
    const auto outputLevels = proc.getVisualState().getOutputLevels();
    inMeter.setLevel(inputLevels.left, inputLevels.right);
    outMeter.setLevel(outputLevels.left, outputLevels.right);

    previewPhase += 0.05f;
    if (previewPhase > juce::MathConstants<float>::twoPi)
        previewPhase -= juce::MathConstants<float>::twoPi;

    const auto snapshot = proc.getDistortionSnapshot();
    clipLED.setOn(snapshot.clipped || juce::jmax(outputLevels.left, outputLevels.right) > 0.98f);
    updateStatusButtons();
    repaint(0, fx::dim::headerH + fx::dim::presetBarH, getWidth(), fx::dim::visualH);
}

void MusiqueDistortionEditor::paintVisualization(juce::Graphics& g, juce::Rectangle<int> area)
{
    const auto snapshot = proc.getDistortionSnapshot();
    const auto& apvts = proc.getAPVTS();
    const int engineIndex = getCurrentEngineIndex();
    const int variantIndex = getCurrentVariantIndex(engineIndex);
    const auto& config = kEngineConfigs[(size_t) engineIndex];

    const float left = (float) area.getX();
    const float top = (float) area.getY();
    const float width = (float) area.getWidth();
    const float height = (float) area.getHeight();
    drawBadge(g, { left + width - 320.0f, top + 14.0f, 108.0f, 22.0f }, config.variants[variantIndex].toUpperCase(), fx::accent::distortion);
    drawBadge(g, { left + width - 204.0f, top + 14.0f, 88.0f, 22.0f }, snapshot.oversampled ? "4X AA" : "RAW", snapshot.oversampled ? fx::accent::distortion : fx::col::textSecondary);
    drawBadge(g, { left + width - 108.0f, top + 14.0f, 84.0f, 22.0f }, snapshot.clipped ? "HOT" : "HEADROOM", snapshot.clipped ? fx::col::meterHigh : fx::col::textSecondary);

    const float panelPad = 24.0f;
    const float panelWidth = (width - panelPad * 3.0f) * 0.5f;
    const float panelHeight = height - 72.0f;
    const float panelTop = top + 50.0f;
    const float leftPanelX = left + panelPad;
    const float rightPanelX = leftPanelX + panelWidth + panelPad;

    g.setColour(fx::col::gridMinor);
    g.drawRect(juce::Rectangle<float>(leftPanelX, panelTop, panelWidth, panelHeight), 1.0f);
    g.drawRect(juce::Rectangle<float>(rightPanelX, panelTop, panelWidth, panelHeight), 1.0f);

    auto drawTransferAxis = [&](float x, float y, float w, float h)
    {
        const float midY = y + h * 0.5f;
        g.setColour(fx::col::gridMajor);
        g.drawLine(x, midY, x + w, midY, 1.0f);
        g.drawLine(x + w * 0.5f, y, x + w * 0.5f, y + h, 1.0f);
        g.setColour(fx::col::gridMinor);
        g.drawLine(x, y + h, x + w, y, 0.75f);
    };

    auto drawCompositeWave = [&](juce::Path& path, float x, float y, float w, float h, auto&& sampleFn)
    {
        for (int i = 0; i <= 220; ++i)
        {
            const float t = (float) i / 220.0f;
            const float sample = sampleFn(t);
            const float px = x + t * w;
            const float py = y + h * 0.5f - sample * h * 0.34f;
            if (i == 0)
                path.startNewSubPath(px, py);
            else
                path.lineTo(px, py);
        }
    };

    if (engineIndex == MusiqueDistortionProcessor::distortion || engineIndex == MusiqueDistortionProcessor::overdrive)
    {
        drawTransferAxis(leftPanelX, panelTop, panelWidth, panelHeight);
        juce::Path curve;
        for (int i = 0; i <= 200; ++i)
        {
            const float input = -1.0f + 2.0f * (float) i / 200.0f;
            float output = input;
            if (engineIndex == MusiqueDistortionProcessor::distortion)
            {
                output = distdsp::legacyShapeSample(input,
                                                    distdsp::dbToGain(getParamValue(apvts, "drive")),
                                                    distdsp::normalisePercent(getParamValue(apvts, "blend")),
                                                    variantIndex,
                                                    distdsp::normalisePercent(getParamValue(apvts, "dist_knee")));
            }
            else
            {
                output = distdsp::overdriveShapeSample(input,
                                                       distdsp::dbToGain(getParamValue(apvts, "od_drive")),
                                                       distdsp::normalisePercent(getParamValue(apvts, "od_bias")),
                                                       distdsp::normalisePercent(getParamValue(apvts, "od_headroom")),
                                                       variantIndex);
            }

            const float px = leftPanelX + ((float) i / 200.0f) * panelWidth;
            const float py = panelTop + panelHeight * 0.5f - output * panelHeight * 0.4f;
            if (i == 0)
                curve.startNewSubPath(px, py);
            else
                curve.lineTo(px, py);
        }

        g.setColour(fx::accent::distortion.withAlpha(0.85f));
        g.strokePath(curve, juce::PathStrokeType(2.4f));

        juce::Path dryWave;
        juce::Path wetWave;
        distdsp::LowPassState previewFilter;
        previewFilter.prepare(44100.0);
        drawCompositeWave(dryWave, rightPanelX, panelTop, panelWidth, panelHeight, [&](float t)
        {
            return juce::jlimit(-1.0f, 1.0f,
                std::sin(t * juce::MathConstants<float>::twoPi * 2.6f + previewPhase)
                + 0.24f * std::sin(t * juce::MathConstants<float>::twoPi * 5.2f + previewPhase * 1.4f));
        });

        drawCompositeWave(wetWave, rightPanelX, panelTop, panelWidth, panelHeight, [&](float t)
        {
            float sample = juce::jlimit(-1.0f, 1.0f,
                std::sin(t * juce::MathConstants<float>::twoPi * 2.6f + previewPhase)
                + 0.24f * std::sin(t * juce::MathConstants<float>::twoPi * 5.2f + previewPhase * 1.4f));

            if (engineIndex == MusiqueDistortionProcessor::distortion)
            {
                sample = distdsp::legacyShapeSample(sample,
                                                    distdsp::dbToGain(getParamValue(apvts, "drive")),
                                                    distdsp::normalisePercent(getParamValue(apvts, "blend")),
                                                    variantIndex,
                                                    distdsp::normalisePercent(getParamValue(apvts, "dist_knee")));
                return distdsp::applyToneFocus(sample, 0, getParamValue(apvts, "tone"), getParamValue(apvts, "dist_focus"), previewFilter);
            }

            sample = distdsp::overdriveShapeSample(sample,
                                                   distdsp::dbToGain(getParamValue(apvts, "od_drive")),
                                                   distdsp::normalisePercent(getParamValue(apvts, "od_bias")),
                                                   distdsp::normalisePercent(getParamValue(apvts, "od_headroom")),
                                                   variantIndex);
            return distdsp::applyToneFocus(sample, 0, getParamValue(apvts, "od_tone"), 50.0f + (getParamValue(apvts, "od_bias") - 50.0f) * 0.35f, previewFilter);
        });

        g.setColour(fx::col::textMuted.withAlpha(0.3f));
        g.strokePath(dryWave, juce::PathStrokeType(1.0f));
        g.setColour(fx::accent::distortion.withAlpha(0.88f));
        g.strokePath(wetWave, juce::PathStrokeType(2.2f));
        g.setColour(fx::col::textPrimary);
        g.drawText(engineIndex == MusiqueDistortionProcessor::distortion
            ? "Legacy transfer preserved, with focus and knee layered on top."
            : "Soft clipping, body lift and headroom reshape the curve.", (int) leftPanelX, (int) (panelTop + panelHeight + 10.0f), (int) (width - 48.0f), 16, juce::Justification::centredLeft);
        return;
    }

    if (engineIndex == MusiqueDistortionProcessor::fuzz)
    {
        juce::Path fuzzWave;
        distdsp::LowPassState toneFilter;
        toneFilter.prepare(44100.0);
        drawCompositeWave(fuzzWave, leftPanelX, panelTop, panelWidth, panelHeight, [&](float t)
        {
            float sample = std::sin(t * juce::MathConstants<float>::twoPi * 2.0f + previewPhase)
                + 0.35f * std::sin(t * juce::MathConstants<float>::twoPi * 4.0f + previewPhase * 1.5f);
            sample = distdsp::fuzzShapeSample(sample,
                                              distdsp::dbToGain(getParamValue(apvts, "fuzz_gain")),
                                              distdsp::normalisePercent(getParamValue(apvts, "fuzz_bias")),
                                              distdsp::normalisePercent(getParamValue(apvts, "fuzz_octave")),
                                              variantIndex);
            return distdsp::applyToneFocus(sample, 0, getParamValue(apvts, "fuzz_tone"), 60.0f, toneFilter);
        });

        g.setColour(fx::accent::distortion.withAlpha(0.86f));
        g.strokePath(fuzzWave, juce::PathStrokeType(2.5f));

        const float gateOpen = snapshot.primary;
        const std::array<juce::String, 3> labels { "GATE", "OCTAVE", "BIAS" };
        const std::array<float, 3> values {
            gateOpen,
            distdsp::normalisePercent(getParamValue(apvts, "fuzz_octave")),
            distdsp::normalisePercent(getParamValue(apvts, "fuzz_bias"))
        };
        for (int index = 0; index < 3; ++index)
        {
            const float barX = rightPanelX + 48.0f + index * 92.0f;
            const float barH = values[(size_t) index] * (panelHeight - 40.0f);
            g.setColour(fx::col::meterBg);
            g.fillRoundedRectangle(barX, panelTop + 20.0f, 36.0f, panelHeight - 40.0f, 6.0f);
            g.setColour(fx::accent::distortion.withAlpha(0.8f));
            g.fillRoundedRectangle(barX, panelTop + panelHeight - 20.0f - barH, 36.0f, barH, 6.0f);
            g.setColour(fx::col::textMuted);
            g.drawText(labels[(size_t) index], (int) barX - 12, (int) (panelTop + panelHeight - 12.0f), 60, 14, juce::Justification::centred);
        }
        g.setColour(fx::col::textPrimary);
        g.drawText("Gate tracking, octave density and bias are live-linked to the active fuzz voice.",
            (int) leftPanelX, (int) (panelTop + panelHeight + 10.0f), (int) (width - 48.0f), 16, juce::Justification::centredLeft);
        return;
    }

    if (engineIndex == MusiqueDistortionProcessor::exciter)
    {
        juce::Path sourcePath;
        juce::Path harmonicPath;
        drawCompositeWave(sourcePath, leftPanelX, panelTop, panelWidth, panelHeight, [&](float t)
        {
            return std::sin(t * juce::MathConstants<float>::twoPi * 2.3f + previewPhase) * 0.8f;
        });

        distdsp::HighPassState previewHighPass;
        distdsp::LowPassState previewTone;
        previewHighPass.prepare(44100.0);
        previewTone.prepare(44100.0);
        drawCompositeWave(harmonicPath, leftPanelX, panelTop, panelWidth, panelHeight, [&](float t)
        {
            const float input = std::sin(t * juce::MathConstants<float>::twoPi * 2.3f + previewPhase) * 0.8f;
            const float high = previewHighPass.process(0, input, getParamValue(apvts, "exc_freq"));
            float harmonic = std::tanh(high * distdsp::dbToGain(getParamValue(apvts, "exc_drive")) * 2.2f) - high * 0.65f;
            harmonic = distdsp::applyToneFocus(harmonic, 0, getParamValue(apvts, "exc_tone"), 55.0f + getParamValue(apvts, "exc_air") * 0.3f, previewTone);
            return harmonic * (0.8f + distdsp::normalisePercent(getParamValue(apvts, "exc_amount")));
        });

        g.setColour(fx::col::textMuted.withAlpha(0.3f));
        g.strokePath(sourcePath, juce::PathStrokeType(1.0f));
        g.setColour(fx::accent::distortion.withAlpha(0.86f));
        g.strokePath(harmonicPath, juce::PathStrokeType(2.2f));

        const float harmonicRatio = juce::jlimit(0.0f, 1.0f, snapshot.tertiary);
        const float freqNorm = juce::jlimit(0.0f, 1.0f, (getParamValue(apvts, "exc_freq") - 1200.0f) / 10800.0f);
        const float airNorm = distdsp::normalisePercent(getParamValue(apvts, "exc_air"));
        const std::array<float, 3> values { harmonicRatio, freqNorm, airNorm };
        const std::array<juce::String, 3> labels { "HARM", "XOVER", "AIR" };
        for (int index = 0; index < 3; ++index)
        {
            const float barX = rightPanelX + 48.0f + index * 92.0f;
            const float barH = values[(size_t) index] * (panelHeight - 40.0f);
            g.setColour(fx::col::meterBg);
            g.fillRoundedRectangle(barX, panelTop + 20.0f, 36.0f, panelHeight - 40.0f, 6.0f);
            g.setColour(fx::accent::distortion.withAlpha(0.8f));
            g.fillRoundedRectangle(barX, panelTop + panelHeight - 20.0f - barH, 36.0f, barH, 6.0f);
            g.setColour(fx::col::textMuted);
            g.drawText(labels[(size_t) index], (int) barX - 12, (int) (panelTop + panelHeight - 12.0f), 60, 14, juce::Justification::centred);
        }
        g.setColour(fx::col::textPrimary);
        g.drawText("The harmonic lane stays anchored to the high band and grows with the exciter amount.",
            (int) leftPanelX, (int) (panelTop + panelHeight + 10.0f), (int) (width - 48.0f), 16, juce::Justification::centredLeft);
        return;
    }

    if (engineIndex == MusiqueDistortionProcessor::bitCrusher)
    {
        juce::Path steppedPath;
        const float bits = getParamValue(apvts, "crush_bits");
        const float rate = getParamValue(apvts, "crush_rate");
        const float jitter = distdsp::normalisePercent(getParamValue(apvts, "crush_jitter"));
        const int hold = juce::jmax(2, (int) std::round(16.0f + 84.0f * (1.0f - juce::jlimit(0.0f, 1.0f, rate / 22050.0f))));
        float held = 0.0f;
        for (int i = 0; i <= 220; ++i)
        {
            const float t = (float) i / 220.0f;
            const float input = std::sin(t * juce::MathConstants<float>::twoPi * 3.0f + previewPhase) * 0.85f;
            if (i % hold == 0)
            {
                const float levels = std::pow(2.0f, bits);
                held = std::round(input * levels) / juce::jmax(1.0f, levels);
            }
            const float px = leftPanelX + t * panelWidth;
            const float py = panelTop + panelHeight * 0.5f - held * panelHeight * 0.36f;
            if (i == 0)
                steppedPath.startNewSubPath(px, py);
            else
                steppedPath.lineTo(px, py);
        }

        g.setColour(fx::accent::distortion.withAlpha(0.86f));
        g.strokePath(steppedPath, juce::PathStrokeType(2.4f));

        drawBadge(g, { rightPanelX + 30.0f, panelTop + 34.0f, 140.0f, 24.0f }, juce::String((int) std::round(bits)) + " bit", fx::accent::distortion);
        drawBadge(g, { rightPanelX + 30.0f, panelTop + 72.0f, 140.0f, 24.0f }, juce::String(rate, 0) + " Hz hold", fx::accent::distortion);
        drawBadge(g, { rightPanelX + 30.0f, panelTop + 110.0f, 140.0f, 24.0f }, "Jitter " + juce::String(jitter * 100.0f, 0) + "%", fx::accent::distortion);

        g.setColour(fx::col::gridMajor);
        for (int step = 0; step < 8; ++step)
        {
            const float y = panelTop + 28.0f + step * ((panelHeight - 56.0f) / 7.0f);
            g.drawHorizontalLine((int) y, rightPanelX + 212.0f, rightPanelX + panelWidth - 28.0f);
        }
        g.setColour(fx::col::textPrimary);
        g.drawText("Held sample stairs show the downsample interval and quantised level grid.",
            (int) leftPanelX, (int) (panelTop + panelHeight + 10.0f), (int) (width - 48.0f), 16, juce::Justification::centredLeft);
        return;
    }

    juce::Path consoleCurve;
    drawTransferAxis(leftPanelX, panelTop, panelWidth, panelHeight);
    for (int i = 0; i <= 200; ++i)
    {
        const float input = -1.0f + 2.0f * (float) i / 200.0f;
        const float output = distdsp::consoleShapeSample(input,
                                                         distdsp::dbToGain(getParamValue(apvts, "sat_drive")),
                                                         distdsp::normalisePercent(getParamValue(apvts, "sat_bias")),
                                                         distdsp::normalisePercent(getParamValue(apvts, "sat_headroom")),
                                                         variantIndex);
        const float px = leftPanelX + ((float) i / 200.0f) * panelWidth;
        const float py = panelTop + panelHeight * 0.5f - output * panelHeight * 0.4f;
        if (i == 0)
            consoleCurve.startNewSubPath(px, py);
        else
            consoleCurve.lineTo(px, py);
    }
    g.setColour(fx::accent::distortion.withAlpha(0.86f));
    g.strokePath(consoleCurve, juce::PathStrokeType(2.4f));

    const float headroomNorm = distdsp::normalisePercent(getParamValue(apvts, "sat_headroom"));
    const float glueNorm = distdsp::normalisePercent(getParamValue(apvts, "sat_glue"));
    const float biasNorm = distdsp::normalisePercent(getParamValue(apvts, "sat_bias"));
    const std::array<float, 3> values { headroomNorm, glueNorm, biasNorm };
    const std::array<juce::String, 3> labels { "HEADROOM", "GLUE", "BIAS" };
    for (int index = 0; index < 3; ++index)
    {
        const float barX = rightPanelX + 48.0f + index * 92.0f;
        const float barH = values[(size_t) index] * (panelHeight - 40.0f);
        g.setColour(fx::col::meterBg);
        g.fillRoundedRectangle(barX, panelTop + 20.0f, 36.0f, panelHeight - 40.0f, 6.0f);
        g.setColour(fx::accent::distortion.withAlpha(0.8f));
        g.fillRoundedRectangle(barX, panelTop + panelHeight - 20.0f - barH, 36.0f, barH, 6.0f);
        g.setColour(fx::col::textMuted);
        g.drawText(labels[(size_t) index], (int) barX - 22, (int) (panelTop + panelHeight - 12.0f), 80, 14, juce::Justification::centred);
    }

    g.setColour(fx::col::textPrimary);
    g.drawText("Headroom and glue govern how gently the console stage compresses transients.",
        (int) leftPanelX, (int) (panelTop + panelHeight + 10.0f), (int) (width - 48.0f), 16, juce::Justification::centredLeft);
}

void MusiqueDistortionEditor::paint(juce::Graphics& g)
{
    g.fillAll(fx::col::bg);
    fx::paint::header(g, getWidth(), fx::accent::distortion);
    if (pluginIcon.isValid())
        g.drawImage(pluginIcon, juce::Rectangle<float>(12.0f, 10.0f, 40.0f, 40.0f), juce::RectanglePlacement::centred);
    fx::paint::presetBar(g, getWidth());
    fx::paint::graphArea(g, getWidth());
    fx::paint::graphGrid(g, getWidth());
    paintVisualization(g, juce::Rectangle<int>(0, fx::dim::headerH + fx::dim::presetBarH, getWidth(), fx::dim::visualH));
    fx::paint::controls(g, getWidth(), 6);
    fx::paint::footer(g, getWidth());
    if (logoImg.isValid())
    {
        const int footerY = fx::dim::appH - fx::dim::footerH;
        g.drawImage(logoImg, juce::Rectangle<float>((float) getWidth() - 52.0f, (float) footerY + 4.0f, 32.0f, 32.0f), juce::RectanglePlacement::centred);
    }
    fx::paint::footerLabel(g, "OUT", 80, 180);
    fx::paint::outline(g, getLocalBounds());
}

void MusiqueDistortionEditor::resized()
{
    titleLabel.setBounds(56, 10, 180, 40);
    bypassBtn.setBounds(getWidth() - 392, 16, 72, fx::dim::btnH);
    monoBtn.setBounds(getWidth() - 314, 16, 96, fx::dim::btnH);
    modeBtn.setBounds(getWidth() - 212, 16, 100, fx::dim::btnH);
    statusBtn.setBounds(getWidth() - 104, 16, 88, fx::dim::btnH);

    const int presetY = fx::dim::headerH + 11;
    prevBtn.setBounds(164, presetY, 30, fx::dim::btnH);
    presetBox.setBounds(198, presetY, 220, fx::dim::btnH);
    nextBtn.setBounds(422, presetY, 30, fx::dim::btnH);
    engineBox.setBounds(462, presetY, 146, fx::dim::btnH);
    variantBox.setBounds(616, presetY, 136, fx::dim::btnH);
    saveBtn.setBounds(760, presetY, 56, fx::dim::btnH);
    abBtn.setBounds(822, presetY, 48, fx::dim::btnH);

    const int controlTop = fx::dim::headerH + fx::dim::presetBarH + fx::dim::visualH;
    const int knobWidth = getWidth() / 6;
    const int knobY = controlTop + 14;
    for (int index = 0; index < 6; ++index)
    {
        const int x = index * knobWidth;
        knobs[(size_t) index].setBounds(x + (knobWidth - 92) / 2, knobY, 92, 90);
        knobLabels[(size_t) index].setBounds(x + (knobWidth - 120) / 2, knobY + 92, 120, 16);
    }

    const int footerY = fx::dim::appH - fx::dim::footerH;
    inMeter.setBounds(16, footerY + 6, 20, fx::dim::footerH - 12);
    outMeter.setBounds(42, footerY + 6, 20, fx::dim::footerH - 12);
    outputSlider.setBounds(80, footerY + 8, 180, 24);
    clipLED.setBounds(280, footerY + 14, 12, 12);
    versionLabel.setBounds(getWidth() - 240, footerY + 8, 180, 24);
}
