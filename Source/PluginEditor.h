#pragma once

#include "GUI/TunerLookAndFeel.h"
#include "License/LicenseOverlay.h"
#include "PluginProcessor.h"

class OniriqueTunerEditor final : public juce::AudioProcessorEditor,
                                  private juce::Timer
{
public:
    explicit OniriqueTunerEditor (OniriqueTunerProcessor&);
    ~OniriqueTunerEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Dépile les mesures du moteur et fait avancer les affichages. Appelé
        trente fois par seconde par le minuteur. */
    void refreshDisplays (double seconds);

    // Toute l'interface est dessinée à cette taille, puis mise à l'échelle.
    static constexpr int baseWidth = 1000;
    static constexpr int baseHeight = 660;

private:
    class Content;

    void timerCallback() override;

    OniriqueTunerProcessor& tuner;
    onirique::ui::TunerLookAndFeel lookAndFeel;
    std::unique_ptr<Content> content;

    // Pastille « mode démo » et page d'activation, par-dessus l'interface.
    std::unique_ptr<onirique::license::Overlay> licenseOverlay;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OniriqueTunerEditor)
};
