#pragma once

#include "Theme.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace onirique::ui
{

//==============================================================================
/** Potard relié à un paramètre : nom sérigraphié au-dessus, valeur en dessous (un clic dessus pour la taper).
    Double-clic : valeur par défaut ; Maj : réglage fin. */
class Knob final : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState&, const juce::String& parameterId, const juce::String& label, bool bipolar = false);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    void mouseUp (const juce::MouseEvent&) override;
    void enablementChanged() override { repaint(); }

private:
    juce::Rectangle<float> valueArea() const;
    void editValue();

    Fonts fonts;
    juce::String label;
    juce::RangedAudioParameter* parameter;
    juce::Slider slider;
    juce::AudioProcessorValueTreeState::SliderAttachment attachment;
    std::unique_ptr<juce::TextEditor> typing;
};

//==============================================================================
/** Curseur horizontal relié à un paramètre : nom, glissière gravée, valeur. */
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
/** Une rangée de touches à témoin reliée à un paramètre à choix (LIVE | HQ, forme du vibrato). */
class ChoiceSwitch final : public juce::Component, public juce::SettableTooltipClient
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
/** Touche à témoin reliée à un paramètre booléen. `litWhenOff` inverse le témoin (un Bypass affiché comme un
    interrupteur « ON »). */
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
/** Les douze notes sur un clavier d'une octave : une touche allumée est une note autorisée, la tonique est
    marquée. Un clic retire une note de la gamme, ou en ajoute une qui n'en fait pas partie ; un clic droit
    l'ignore (témoin blanc) : la voix qui en est le plus près passe sans correction, comme le « Bypass »
    du clavier d'Auto-Tune. */
class NoteGrid final : public juce::Component, public juce::SettableTooltipClient
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
    juce::Rectangle<float> keyBounds (int note) const;
    int noteAt (juce::Point<float>) const;

    Fonts fonts;
    juce::AudioProcessorValueTreeState& state;
    std::function<std::uint16_t()> getMask;
    std::atomic<float>* key = nullptr;
    std::array<std::atomic<float>*, 12> skips {};
    std::uint16_t shownMask = 0, shownSkips = 0;
    int shownKey = -1, hovered = -1;
};

} // namespace onirique::ui
