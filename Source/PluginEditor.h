#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "FXTokens.h"
#include "FXLookAndFeel.h"
#include "FXComponents.h"

class MusiqueDistortionEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
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
    void paintVisualization(juce::Graphics&, juce::Rectangle<int> area);

    MusiqueDistortionProcessor& proc;
    fx::FXLookAndFeel lnf { fx::accent::distortion };

    // Header
    juce::Label titleLabel;
    juce::Image pluginIcon, logoImg;
    juce::TextButton bypassBtn{"Bypass"}, osBtn{"2x OS"}, hqBtn{"HQ"}, settingsBtn{juce::CharPointer_UTF8("\xe2\x9a\x99")};

    // Preset bar
    juce::TextButton prevBtn{"<"}, nextBtn{">"}, saveBtn{"Save"}, abBtn{"A/B"};
    juce::ComboBox presetBox, modeBox;

    // 4 knobs: Drive, Tone, Blend, Mix
    juce::Slider knobs[4];
    juce::Label knobLabels[4];

    // Footer
    fx::MeterComponent inMeter, outMeter;
    juce::Slider outputSlider;
    juce::Label versionLabel;
    fx::LEDComponent clipLED;

    // Visualization
    float phase = 0.0f;

    // Attachments
    std::unique_ptr<SliderAttach> driveAtt, toneAtt, blendAtt, mixAtt, outAtt;
    std::unique_ptr<ComboAttach> modeAtt;
    std::unique_ptr<ButtonAttach> bypassAtt;

    std::shared_ptr<juce::Array<juce::var>> presets;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MusiqueDistortionEditor)
};
