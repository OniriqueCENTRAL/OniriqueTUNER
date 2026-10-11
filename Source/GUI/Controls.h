#pragma once

#include "Theme.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace onirique::ui
{

//==============================================================================
/** Potard relié à un paramètre : cadran, libellé, valeur. Double-clic pour
    revenir à la valeur par défaut. */
class Knob final : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState&, const juce::String& parameterId, const juce::String& label, bool bipolar = false);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    void enablementChanged() override { repaint(); }

private:
    Fonts fonts;
    juce::String label;
    juce::Slider slider;
    juce::AudioProcessorValueTreeState::SliderAttachment attachment;
};

//==============================================================================
/** Curseur horizontal compact relié à un paramètre : libellé, piste, valeur. */
class Fader final : public juce::Component
{
public:
    Fader (juce::AudioProcessorValueTreeState&, const juce::String& parameterId, const juce::String& label);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }

private:
    Fonts fonts;
    juce::String label;
    juce::Slider slider;
    juce::AudioProcessorValueTreeState::SliderAttachment attachment;
};

//==============================================================================
/** Sélecteur segmenté à pastille braise (LIVE | HQ). */
class ChoiceSwitch final : public juce::Component
{
public:
    ChoiceSwitch (juce::RangedAudioParameter&, juce::StringArray labels);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> segmentBounds (int index) const;

    Fonts fonts;
    juce::StringArray labels;
    int current = 0, hovered = -1;
    juce::ParameterAttachment attachment;
};

//==============================================================================
/** Pastille à LED reliée à un paramètre booléen. `litWhenOff` inverse le
    voyant (un Bypass affiché comme un interrupteur "ON"). */
class LedButton final : public juce::Button
{
public:
    LedButton (juce::RangedAudioParameter&, const juce::String& textWhenLit, const juce::String& textWhenDark, bool litWhenOff = false);

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    Fonts fonts;
    juce::String litText, darkText;
    bool inverted;
    juce::ButtonParameterAttachment attachment;
};

//==============================================================================
/** Les douze notes : allumée = autorisée. Un clic retire une note de la
    gamme, ou en ajoute une qui n'en fait pas partie. */
class NoteGrid final : public juce::Component
{
public:
    NoteGrid (juce::AudioProcessorValueTreeState&, std::function<std::uint16_t()> noteMask);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    /** À appeler régulièrement : redessine si la gamme a changé. */
    void refresh();

private:
    juce::Rectangle<float> cellBounds (int note) const;
    int noteAt (juce::Point<float>) const;

    Fonts fonts;
    juce::AudioProcessorValueTreeState& state;
    std::function<std::uint16_t()> getMask;
    std::atomic<float>* key = nullptr;
    std::uint16_t shownMask = 0;
    int shownKey = -1, hovered = -1;
};

} // namespace onirique::ui
