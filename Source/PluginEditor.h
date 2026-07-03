#pragma once

#include <JuceHeader.h>
#include <array>
#include "PluginProcessor.h"
#include "FXTokens.h"
#include "FXLookAndFeel.h"
#include "FXComponents.h"

class MusiqueDistortionEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    struct EngineUiConfig
    {
        const char* title;
        std::array<const char*, 5> paramIds;
        std::array<const char*, 6> labels;
        juce::StringArray variants;
    };

    explicit MusiqueDistortionEditor(MusiqueDistortionProcessor&);
    ~MusiqueDistortionEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    using APVTS = juce::AudioProcessorValueTreeState;
    using SliderAttach = APVTS::SliderAttachment;
    using ButtonAttach = APVTS::ButtonAttachment;
    using ComboAttach = APVTS::ComboBoxAttachment;

    void timerCallback() override;
    void loadPresets();
    void refreshPresetBox();
    void normalisePreset(juce::var&) const;
    int getCurrentEngineIndex() const;
    int getCurrentVariantIndex(int engineIndex) const;
    void rebuildEngineUi(bool force = false);
    void bindEngineKnobs(int engineIndex);
    void rebuildVariantItems(int engineIndex);
    void applyVariantSelection(int engineIndex, int variantIndex);
    void storeCurrentABSlot();
    void recallABSlot(bool showSlotA);
    void updateStatusButtons();
    void paintVisualization(juce::Graphics&, juce::Rectangle<int>);

    MusiqueDistortionProcessor& proc;
    fx::FXLookAndFeel lnf { fx::accent::distortion };

    juce::Label titleLabel;
    juce::Image pluginIcon, logoImg;
    juce::TextButton bypassBtn { "Bypass" };
    juce::TextButton monoBtn { "Stereo In" };
    juce::TextButton modeBtn { "Mode" };
    juce::TextButton statusBtn { "Status" };

    juce::TextButton prevBtn { "<" };
    juce::TextButton nextBtn { ">" };
    juce::TextButton saveBtn { "Save" };
    juce::TextButton abBtn { "A/B" };
    juce::ComboBox presetBox;
    juce::ComboBox engineBox;
    juce::ComboBox variantBox;

    std::array<juce::Slider, 6> knobs;
    std::array<juce::Label, 6> knobLabels;

    fx::MeterComponent inMeter, outMeter;
    juce::Slider outputSlider;
    juce::Label versionLabel;
    fx::LEDComponent clipLED;

    std::unique_ptr<SliderAttach> mixAtt;
    std::unique_ptr<SliderAttach> outAtt;
    std::unique_ptr<ComboAttach> engineAtt;
    std::unique_ptr<ButtonAttach> bypassAtt;
    std::unique_ptr<ButtonAttach> monoAtt;
    std::array<std::unique_ptr<SliderAttach>, 5> engineKnobAtts;

    std::shared_ptr<juce::Array<juce::var>> presets;
    juce::ValueTree abStateA, abStateB;
    bool showingA = true;
    int displayedEngine = -1;
    float previewPhase = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MusiqueDistortionEditor)
};
