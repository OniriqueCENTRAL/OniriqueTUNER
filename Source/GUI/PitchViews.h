#pragma once

#include "../DSP/TuneEngine.h"
#include "Theme.h"

namespace onirique::ui
{

//==============================================================================
/** Le sigil dans son réticule, comme sur le site. Dès qu'une voix arrive, il
    s'efface derrière la note visée, et une jauge montre l'écart de la voix
    (repère clair) et ce que le plugin en fait (arc braise). */
class TunerDial final : public juce::Component
{
public:
    TunerDial();

    void push (const tune::DisplayFrame&);
    void tick (double seconds);

    /** Vocodeur en marche : l'afficheur montre la note MIDI tirée de la voix. */
    void setVocoder (bool isOn) noexcept { vocoderOn = isOn; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    Fonts fonts;
    juce::Image sigil, sigilScaled, sigilGlow;

    bool vocoderOn = false;
    float inMidi = 60.0f, outMidi = 60.0f, noteMidi = 60.0f;
    float presence = 0.0f;
    double silentFor = 10.0, clock = 0.0;
};

//==============================================================================
/** Courbe défilante : la hauteur chantée et la hauteur corrigée, sur les
    lignes des notes de la gamme. */
class PitchTrace final : public juce::Component
{
public:
    PitchTrace();

    void push (const tune::DisplayFrame&);
    void setNoteMask (std::uint16_t mask);
    void tick();

    void paint (juce::Graphics&) override;

private:
    static constexpr int capacity = 1536;      // un peu plus de 4 secondes

    Fonts fonts;
    std::array<float, capacity> in {}, out {};  // 0 = pas de voix
    int head = 0;
    float low = 53.5f, high = 66.5f;            // notes visibles en bas et en haut
    std::uint16_t noteMask = 0x0fff;
    bool dirty = true;
};

} // namespace onirique::ui
