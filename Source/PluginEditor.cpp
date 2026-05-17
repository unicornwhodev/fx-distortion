#include "PluginEditor.h"
#include "BinaryData.h"
#include <cmath>

MusiqueDistortionEditor::MusiqueDistortionEditor(MusiqueDistortionProcessor& p)
    : AudioProcessorEditor(&p), proc(p)
{
    setLookAndFeel(&lnf);
    setSize(fx::dim::appW, fx::dim::appH);

    // Header
    titleLabel.setText("DISTORTION", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(juce::FontOptions{}.withHeight(fx::font::header).withStyle("Bold")));
    titleLabel.setJustificationType(juce::Justification::centredLeft);
    titleLabel.setColour(juce::Label::textColourId, fx::col::textPrimary);
    addAndMakeVisible(titleLabel);

    pluginIcon = juce::ImageCache::getFromMemory(BinaryData::icon_small_png, BinaryData::icon_small_pngSize);
    logoImg = juce::ImageCache::getFromMemory(BinaryData::logo_png, BinaryData::logo_pngSize);

    auto setupHdrBtn = [&](juce::TextButton& b, bool toggle = false) {
        b.setColour(juce::TextButton::buttonColourId, fx::col::surfSecondary);
        b.setColour(juce::TextButton::textColourOffId, fx::col::textPrimary);
        if (toggle) b.setClickingTogglesState(true);
        addAndMakeVisible(b);
    };
    setupHdrBtn(bypassBtn, true);
    setupHdrBtn(osBtn, true);
    setupHdrBtn(hqBtn, true);
    setupHdrBtn(settingsBtn);
    fx::ui::markUnsupportedControl(hqBtn);
    fx::ui::markUnsupportedControl(settingsBtn);
    osBtn.setTooltip("Shows whether the current mode is running through the 2x oversampled non-linear stage");
    osBtn.onClick = [] {};

    // Mode box in header area
    modeBox.addItem("Clipper", 1);
    modeBox.addItem("Bitcrush", 2);
    modeBox.addItem("Tube", 3);
    addAndMakeVisible(modeBox);

    // Preset bar
    setupHdrBtn(prevBtn); setupHdrBtn(nextBtn); setupHdrBtn(saveBtn); setupHdrBtn(abBtn);
    addAndMakeVisible(presetBox);

    presets = std::make_shared<juce::Array<juce::var>>(fx::preset::loadAllPresets("fx-distortion"));
    if (presets->isEmpty()) { presetBox.addItem("Init", 1); presetBox.setSelectedId(1); }
    else
    {
        int id = 1;
        for (auto& pv : *presets)
            if (auto* o = pv.getDynamicObject())
                presetBox.addItem(o->getProperty("name").toString(), id++);
        presetBox.setSelectedItemIndex(0, juce::dontSendNotification);
        fx::preset::applyToAPVTS(proc.getAPVTS(), presets->getReference(0));
    }
    presetBox.onChange = [this] {
        int i = presetBox.getSelectedItemIndex();
        if (i >= 0 && i < presets->size()) fx::preset::applyToAPVTS(proc.getAPVTS(), presets->getReference(i));
    };
    prevBtn.onClick = [this] { int i = presetBox.getSelectedItemIndex(); if (i > 0) presetBox.setSelectedItemIndex(i - 1); };
    nextBtn.onClick = [this] { int i = presetBox.getSelectedItemIndex(); if (i < presetBox.getNumItems() - 1) presetBox.setSelectedItemIndex(i + 1); };
    saveBtn.onClick = [this] {
        auto name = juce::String("User_") + juce::Time::getCurrentTime().formatted("%H%M%S");
        juce::StringArray ids {"drive","tone","blend","output","mode","mix","bypass","mono"};
        if (fx::preset::saveUserPreset("fx-distortion", name, ids, proc.getAPVTS()))
        {
            *presets = fx::preset::loadAllPresets("fx-distortion");
            presetBox.clear();
            int id = 1;
            for (auto& pv : *presets)
                if (auto* o = pv.getDynamicObject()) presetBox.addItem(o->getProperty("name").toString(), id++);
            presetBox.setSelectedItemIndex(presetBox.getNumItems() - 1);
        }
    };

    // Knobs (4: drive, tone, blend, mix)
    const char* labels[4] = {"DRIVE", "TONE", "CHAR", "MIX"};
    for (int i = 0; i < 4; ++i)
    {
        knobs[i].setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        knobs[i].setTextBoxStyle(juce::Slider::TextBoxBelow, false, 62, 16);
        addAndMakeVisible(knobs[i]);
        knobLabels[i].setText(labels[i], juce::dontSendNotification);
        knobLabels[i].setFont(juce::Font(juce::FontOptions{}.withHeight(fx::font::label).withStyle("Bold")));
        knobLabels[i].setJustificationType(juce::Justification::centred);
        knobLabels[i].setColour(juce::Label::textColourId, fx::col::textMuted);
        addAndMakeVisible(knobLabels[i]);
    }

    // Footer
    addAndMakeVisible(inMeter);
    addAndMakeVisible(outMeter);
    outputSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    outputSlider.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    addAndMakeVisible(outputSlider);
    clipLED.setAccent(fx::accent::distortion);
    addAndMakeVisible(clipLED);
    versionLabel.setText("Musique Distortion v1.0", juce::dontSendNotification);
    versionLabel.setFont(juce::Font(juce::FontOptions{}.withHeight(fx::font::footer)));
    versionLabel.setColour(juce::Label::textColourId, fx::col::textMuted);
    versionLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(versionLabel);

    // Attachments
    driveAtt = std::make_unique<SliderAttach>(proc.getAPVTS(), "drive",  knobs[0]);
    toneAtt  = std::make_unique<SliderAttach>(proc.getAPVTS(), "tone",   knobs[1]);
    blendAtt = std::make_unique<SliderAttach>(proc.getAPVTS(), "blend",  knobs[2]);
    mixAtt   = std::make_unique<SliderAttach>(proc.getAPVTS(), "mix",    knobs[3]);
    outAtt   = std::make_unique<SliderAttach>(proc.getAPVTS(), "output", outputSlider);
    modeAtt  = std::make_unique<ComboAttach>(proc.getAPVTS(), "mode", modeBox);
    bypassAtt = std::make_unique<ButtonAttach>(proc.getAPVTS(), "bypass", bypassBtn);

    startTimerHz(fx::anim::fftRefreshHz);
}

MusiqueDistortionEditor::~MusiqueDistortionEditor() { setLookAndFeel(nullptr); }

void MusiqueDistortionEditor::timerCallback()
{
    const auto inputLevels = proc.getVisualState().getInputLevels();
    const auto outputLevels = proc.getVisualState().getOutputLevels();
    inMeter.setLevel(inputLevels.left, inputLevels.right);
    outMeter.setLevel(outputLevels.left, outputLevels.right);

    phase += 0.05f;
    if (phase > juce::MathConstants<float>::twoPi) phase -= juce::MathConstants<float>::twoPi;

    float drive = 12.0f;
    if (auto* p = proc.getAPVTS().getRawParameterValue("drive")) drive = p->load();
    const bool osActive = proc.isOversamplingActive();
    osBtn.setToggleState(osActive, juce::dontSendNotification);
    osBtn.setButtonText(osActive ? (juce::String(proc.getOversamplingFactor()) + "x OS") : "DIRECT");
    osBtn.setColour(juce::TextButton::buttonColourId, osActive ? fx::accent::distortion.withAlpha(0.22f) : fx::col::surfSecondary);
    osBtn.setColour(juce::TextButton::textColourOffId, osActive ? fx::accent::distortion.brighter(0.2f) : fx::col::textPrimary);
    hqBtn.setButtonText(osActive ? "AA" : "RAW");
    hqBtn.setTooltip(osActive ? "Anti-aliased path active" : "Raw lo-fi path active");
    clipLED.setOn(juce::jmax(outputLevels.left, outputLevels.right) > 0.98f || drive > 30.0f);

    repaint(0, fx::dim::headerH + fx::dim::presetBarH, getWidth(), fx::dim::visualH);
}

void MusiqueDistortionEditor::paintVisualization(juce::Graphics& g, juce::Rectangle<int> area)
{
    float drive = 12.0f, tone = 0.5f, blend = 0.7f, mix = 1.0f;
    if (auto* p = proc.getAPVTS().getRawParameterValue("drive")) drive = p->load();
    if (auto* p = proc.getAPVTS().getRawParameterValue("tone")) tone = p->load();
    if (auto* p = proc.getAPVTS().getRawParameterValue("blend")) blend = p->load() / 100.0f;
    if (auto* p = proc.getAPVTS().getRawParameterValue("mix")) mix = p->load() / 100.0f;

    int mode = 0;
    if (auto* p = proc.getAPVTS().getRawParameterValue("mode")) mode = (int)(p->load());
    const bool osActive = proc.isOversamplingActive();
    const juce::String osStatus = osActive ? (juce::String(proc.getOversamplingFactor()) + "x AA ACTIVE") : "RAW DIRECT PATH";
    const juce::String osDetail = osActive
        ? "non-linear stage oversampled before clip or tube shaping"
        : "bitcrush keeps aliasing and steps intentionally audible";
    const float toneCutoff = juce::jlimit(120.0f, 19845.0f, 1200.0f + tone * 13800.0f);
    const float toneCoeff = std::exp(-juce::MathConstants<float>::twoPi * toneCutoff / 44100.0f);
    const float gain = juce::Decibels::decibelsToGain(drive);

    auto shapeSample = [&](float sample) -> float
    {
        if (mode == 0)
        {
            const float threshold = juce::jmap(blend, 0.0f, 1.0f, 1.0f, 0.22f);
            const float clipped = juce::jlimit(-threshold, threshold, sample * gain) / threshold;
            return juce::jlimit(-1.0f, 1.0f, clipped);
        }

        if (mode == 1)
        {
            const float driven = sample * gain;
            const float bits = juce::jmap(1.0f - blend, 0.0f, 1.0f, 4.0f, 14.0f);
            const float levels = std::pow(2.0f, bits);
            return juce::jlimit(-1.0f, 1.0f, std::round(driven * levels) / levels);
        }

        const float asym = juce::jmap(blend, 0.0f, 1.0f, 0.0f, 0.35f);
        const float biased = sample + asym;
        const float shaped = std::tanh(biased * gain) - std::tanh(asym * gain);
        return juce::jlimit(-1.0f, 1.0f, shaped);
    };

    const float w = (float)area.getWidth();
    const float h = (float)area.getHeight();
    const float cx = (float)area.getX();
    const float cy = (float)area.getY();
    const float midY = cy + h * 0.5f;
    const float pad = 24.0f;

    auto drawBadge = [&](juce::Rectangle<float> rect, const juce::String& text, juce::Colour colour)
    {
        g.setColour(colour.withAlpha(0.16f));
        g.fillRoundedRectangle(rect, 8.0f);
        g.setColour(colour.withAlpha(0.6f));
        g.drawRoundedRectangle(rect, 8.0f, 1.0f);
        g.setColour(fx::col::textPrimary);
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(10.0f).withStyle("Bold")));
        g.drawText(text, rect.toNearestInt(), juce::Justification::centred);
    };

    drawBadge({ cx + w - 304.0f, cy + 14.0f, 102.0f, 22.0f }, osActive ? "AA ACTIVE" : "RAW PATH", osActive ? fx::accent::distortion : fx::col::textSecondary);
    drawBadge({ cx + w - 194.0f, cy + 14.0f, 92.0f, 22.0f }, "CHAR " + juce::String(blend * 100.0f, 0) + "%", fx::accent::distortion);
    drawBadge({ cx + w - 96.0f, cy + 14.0f, 74.0f, 22.0f }, "MIX " + juce::String(mix * 100.0f, 0) + "%", fx::col::textSecondary);

    // === LEFT HALF: Transfer curve (input→output mapping) ===
    float halfW = (w - pad * 3.0f) * 0.45f;
    float tcX = cx + pad;
    float tcY = cy + pad;
    float tcH = h - 2.0f * pad;

    // Transfer curve axes
    g.setColour(fx::col::gridMajor);
    g.drawLine(tcX, midY, tcX + halfW, midY, 1.0f);                  // horizontal 0
    g.drawLine(tcX + halfW * 0.5f, tcY, tcX + halfW * 0.5f, tcY + tcH, 1.0f); // vertical 0

    // 1:1 reference
    g.setColour(fx::col::gridMinor);
    g.drawLine(tcX, tcY + tcH, tcX + halfW, tcY, 0.5f);

    // Transfer curve based on mode
    juce::Path transferPath;
    for (int i = 0; i <= 200; ++i)
    {
        float input = -1.0f + 2.0f * (float)i / 200.0f; // -1 to +1
        const float processed = shapeSample(input);

        float xPos = tcX + ((float)i / 200.0f) * halfW;
        float yPos = midY - processed * (tcH * 0.45f);

        if (i == 0) transferPath.startNewSubPath(xPos, yPos);
        else transferPath.lineTo(xPos, yPos);
    }

    g.setColour(fx::accent::distortion.withAlpha(0.9f));
    g.strokePath(transferPath, juce::PathStrokeType(2.5f));

    // Transfer curve label
    g.setColour(fx::col::textMuted);
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(10.0f)));
    g.drawText("TRANSFER", (int)tcX, (int)(tcY - 2), (int)halfW, 12, juce::Justification::centred);
    g.drawText("CHAR = mode character, MIX = dry/wet output", (int)tcX, (int)(cy + h - 18), (int)halfW + 40, 12, juce::Justification::left);
    g.drawText(osStatus, (int)tcX, (int)(tcY + 12), (int)halfW + 40, 12, juce::Justification::left);
    g.drawText(osDetail, (int)tcX, (int)(tcY + 26), (int)halfW + 86, 12, juce::Justification::left);

    // === RIGHT HALF: Animated waveform before/after ===
    float waveX = cx + pad * 2.0f + halfW;
    float waveW = w - halfW - pad * 3.0f;
    float waveH = tcH;
    float waveMid = midY;

    // Axis labels
    g.setColour(fx::col::textMuted);
    g.drawText("WAVEFORM", (int)waveX, (int)(tcY - 2), (int)waveW, 12, juce::Justification::centred);

    // Clean signal (dimmed)
    juce::Path cleanPath;
    for (int i = 0; i <= (int)waveW; ++i)
    {
        float t = (float)i / waveW;
        float sample = std::sin(t * juce::MathConstants<float>::twoPi * 3.0f + phase);
        // Add harmonic complexity based on tone
        sample += tone * 0.3f * std::sin(t * juce::MathConstants<float>::twoPi * 6.0f + phase * 1.5f);
        sample = juce::jlimit(-1.0f, 1.0f, sample);
        float yPos = waveMid - sample * (waveH * 0.35f);

        if (i == 0) cleanPath.startNewSubPath(waveX + (float)i, yPos);
        else cleanPath.lineTo(waveX + (float)i, yPos);
    }
    g.setColour(fx::col::textMuted.withAlpha(0.3f));
    g.strokePath(cleanPath, juce::PathStrokeType(1.0f));

    // Distorted signal
    juce::Path distPath;
    float previewToneState = 0.0f;
    for (int i = 0; i <= (int)waveW; ++i)
    {
        float t = (float)i / waveW;
        float sample = std::sin(t * juce::MathConstants<float>::twoPi * 3.0f + phase);
        sample += tone * 0.3f * std::sin(t * juce::MathConstants<float>::twoPi * 6.0f + phase * 1.5f);

        float processed = shapeSample(sample);
        previewToneState = previewToneState * toneCoeff + processed * (1.0f - toneCoeff);
        const float dark = previewToneState;
        const float high = processed - dark;
        const float bright = juce::jlimit(-1.25f, 1.25f, processed + high * 0.35f);
        processed = juce::jmap(tone, dark, bright);

        float yPos = waveMid - processed * (waveH * 0.35f);
        if (i == 0) distPath.startNewSubPath(waveX + (float)i, yPos);
        else distPath.lineTo(waveX + (float)i, yPos);
    }

    // Clipping zones highlight
    {
        juce::Path clipZone;
        float clipY = waveMid - (waveH * 0.35f);
        clipZone.addRectangle(waveX, clipY - 2.0f, waveW, 4.0f);
        float clipYB = waveMid + (waveH * 0.35f);
        clipZone.addRectangle(waveX, clipYB - 2.0f, waveW, 4.0f);
        g.setColour(fx::accent::distortion.withAlpha(0.08f));
        g.fillPath(clipZone);
        g.setColour(fx::accent::distortion.withAlpha(0.2f));
        g.drawHorizontalLine((int)clipY, waveX, waveX + waveW);
        g.drawHorizontalLine((int)clipYB, waveX, waveX + waveW);
    }

    g.setColour(fx::accent::distortion.withAlpha(0.85f));
    g.strokePath(distPath, juce::PathStrokeType(2.0f));

    // Mode indicator
    const char* modeNames[] = {"CLIPPER", "BITCRUSH", "TUBE"};
    g.setColour(fx::accent::distortion);
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(12.0f).withStyle("Bold")));
    g.drawText(modeNames[juce::jlimit(0, 2, mode)], (int)(cx + w - 100), (int)(cy + h - 26), 80, 16, juce::Justification::centredRight);
}

void MusiqueDistortionEditor::paint(juce::Graphics& g)
{
    g.fillAll(fx::col::bg);
    fx::paint::header(g, getWidth(), fx::accent::distortion);
    if (pluginIcon.isValid())
        g.drawImage(pluginIcon, juce::Rectangle<float>(12, 10, 40, 40), juce::RectanglePlacement::centred);
    fx::paint::presetBar(g, getWidth());
    fx::paint::graphArea(g, getWidth());
    fx::paint::graphGrid(g, getWidth());
    paintVisualization(g, juce::Rectangle<int>(0, fx::dim::headerH + fx::dim::presetBarH, getWidth(), fx::dim::visualH));
    fx::paint::controls(g, getWidth(), 4);
    fx::paint::footer(g, getWidth());
    if (logoImg.isValid())
    {
        const int fy = fx::dim::appH - fx::dim::footerH;
        g.drawImage(logoImg, juce::Rectangle<float>((float)getWidth() - 52.0f, (float)fy + 4.0f, 32.0f, 32.0f), juce::RectanglePlacement::centred);
    }
    fx::paint::footerLabel(g, "OUT", 80, 180);
    fx::paint::outline(g, getLocalBounds());
}

void MusiqueDistortionEditor::resized()
{
    // Header
    titleLabel.setBounds(56, 10, 160, 40);
    bypassBtn.setBounds(getWidth() - 370, 16, 64, fx::dim::btnH);
    modeBox.setBounds(getWidth() - 300, 16, 100, fx::dim::btnH);
    osBtn.setBounds(getWidth() - 194, 16, 52, fx::dim::btnH);
    hqBtn.setBounds(getWidth() - 136, 16, 38, fx::dim::btnH);
    settingsBtn.setBounds(getWidth() - 92, 16, 42, fx::dim::btnH);

    // Preset bar
    const int py = fx::dim::headerH + 11;
    prevBtn.setBounds(260, py, 30, fx::dim::btnH);
    presetBox.setBounds(294, py, 250, fx::dim::btnH);
    nextBtn.setBounds(548, py, 30, fx::dim::btnH);
    saveBtn.setBounds(590, py, 56, fx::dim::btnH);
    abBtn.setBounds(652, py, 48, fx::dim::btnH);

    // Knobs (4 knobs, centered)
    const int ctrlTop = fx::dim::headerH + fx::dim::presetBarH + fx::dim::visualH;
    const int numKnobs = 4;
    const int kW = getWidth() / numKnobs;
    const int kY = ctrlTop + 14;
    for (int i = 0; i < numKnobs; ++i)
    {
        int x = i * kW;
        knobs[i].setBounds(x + (kW - 92) / 2, kY, 92, 90);
        knobLabels[i].setBounds(x + (kW - 120) / 2, kY + 92, 120, 16);
    }

    // Footer
    const int fy = fx::dim::appH - fx::dim::footerH;
    inMeter.setBounds(16, fy + 6, 20, fx::dim::footerH - 12);
    outMeter.setBounds(42, fy + 6, 20, fx::dim::footerH - 12);
    outputSlider.setBounds(80, fy + 8, 180, 24);
    clipLED.setBounds(280, fy + 14, 12, 12);
    versionLabel.setBounds(getWidth() - 220, fy + 8, 160, 24);
}
