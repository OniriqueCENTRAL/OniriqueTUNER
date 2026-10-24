#include "Controls.h"
#include "Instrument.h"

#include "../DSP/Scales.h"
#include "../Parameters.h"

namespace onirique::ui
{

//==============================================================================
Knob::Knob (juce::AudioProcessorValueTreeState& state, const juce::String& parameterId, const juce::String& labelToUse, bool bipolar)
    : label (labelToUse),
      parameter (state.getParameter (parameterId)),
      attachment (state, parameterId, slider)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (pi * 1.25f, pi * 2.75f, true);
    slider.setMouseDragSensitivity (220);
    slider.setVelocityModeParameters (0.25, 1, 0.0, true, juce::ModifierKeys::shiftModifier);   // Maj : réglage fin
    slider.setDoubleClickReturnValue (true, parameter->convertFrom0to1 (parameter->getDefaultValue()));
    slider.getProperties().set ("bipolar", bipolar);
    slider.setTitle (label);
    slider.onValueChange = [this] { repaint(); };
    slider.addMouseListener (this, false);
    addAndMakeVisible (slider);
}

void Knob::resized()
{
    auto area = getLocalBounds();
    area.removeFromTop (16);
    area.removeFromBottom (20);
    const int size = juce::jmin (area.getWidth(), area.getHeight());
    slider.setBounds (area.withSizeKeepingCentre (size, size));
    if (typing != nullptr)
        typing->setBounds (valueArea().toNearestInt());
}

juce::Rectangle<float> Knob::valueArea() const
{
    const auto b = getLocalBounds().toFloat();
    return b.withTop (b.getBottom() - 18.0f).reduced (4.0f, 0.0f);
}

void Knob::mouseUp (const juce::MouseEvent& e)
{
    if (e.eventComponent == this && isEnabled() && valueArea().contains (e.position) && ! e.mouseWasDraggedSinceMouseDown())
        editValue();
}

// la valeur tapée au clavier : « 20 », « 2,5 », « -7 », « 1,2 s », « 442 » (Hz, pour Detune réglé en cents)
void Knob::editValue()
{
    const auto shown = slider.getTextFromValue (slider.getValue());
    const auto parse = [shown, range = parameter->getNormalisableRange()] (const juce::String& text)
    {
        float v = text.getFloatValue();
        if (shown.endsWith (" Hz") && range.end <= 100.0f && v > 200.0f)
            v = 1200.0f * std::log2 (v / 440.0f);                      // Detune : affiché en Hz, réglé en cents
        else if (shown.endsWith (" s") || shown.endsWith (" ms"))
        {
            const bool seconds = text.endsWithIgnoreCase ("s") && ! text.endsWithIgnoreCase ("ms");
            if (seconds || (! text.containsIgnoreCase ("m") && shown.endsWith (" s") && v < 10.0f))
                v *= 1000.0f;
        }
        return range.snapToLegalValue (v);
    };

    typing = std::make_unique<juce::TextEditor>();
    typing->setFont (fonts->monoBoldFont (11.5f));
    typing->setJustification (juce::Justification::centred);
    typing->setIndents (2, 0);
    typing->setText (shown.upToFirstOccurrenceOf (" ", false, false), false);
    typing->selectAll();
    addAndMakeVisible (*typing);
    typing->setBounds (valueArea().toNearestInt());
    typing->grabKeyboardFocus();

    auto close = [safe = juce::Component::SafePointer<Knob> (this), parse] (bool apply)
    {
        if (safe == nullptr || safe->typing == nullptr) return;
        const auto text = safe->typing->getText().trim().replaceCharacter (',', '.');
        if (apply && text.isNotEmpty())
        {
            auto* p = safe->parameter;
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 (parse (text)));
            p->endChangeGesture();
        }
        juce::MessageManager::callAsync ([safe] { if (safe != nullptr) { safe->typing.reset(); safe->repaint(); } });
    };
    typing->onReturnKey = [close] { close (true); };
    typing->onEscapeKey = [close] { close (false); };
    typing->onFocusLost = [close] { close (true); };
    repaint();
}

void Knob::paint (juce::Graphics& g)
{
    const bool hot = isEnabled() && (slider.isMouseOverOrDragging() || isMouseOver (true));
    const auto b = getLocalBounds().toFloat();
    ink (g, label.toUpperCase(), b.withHeight (15.0f), juce::Justification::centred, 10.0f, hot ? 0.95f : 0.68f);

    if (typing == nullptr)
    {
        g.setColour (hot ? pal::emberHot : pal::mist.withAlpha (0.82f));
        g.setFont (fonts->monoFont (11.0f));
        g.drawText (slider.getTextFromValue (slider.getValue()), valueArea(), juce::Justification::centred, false);
    }
}

//==============================================================================
Fader::Fader (juce::AudioProcessorValueTreeState& state, const juce::String& parameterId, const juce::String& labelToUse)
    : label (labelToUse),
      attachment (state, parameterId, slider)
{
    slider.setSliderStyle (juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setTitle (label);
    slider.onValueChange = [this] { repaint(); };
    slider.addMouseListener (this, false);
    addAndMakeVisible (slider);
}

void Fader::resized()
{
    slider.setBounds (getLocalBounds().withTrimmedLeft (52).withTrimmedRight (48));
}

void Fader::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    ink (g, label.toUpperCase(), bounds.withWidth (52.0f), juce::Justification::centredLeft, 10.0f, slider.isMouseOverOrDragging() ? 0.95f : 0.68f);

    g.setColour (pal::mist.withAlpha (0.82f));
    g.setFont (fonts->monoFont (11.0f));
    g.drawText (slider.getTextFromValue (slider.getValue()), bounds.withTrimmedLeft (bounds.getWidth() - 46.0f), juce::Justification::centredRight, false);
}

//==============================================================================
ChoiceSwitch::ChoiceSwitch (juce::RangedAudioParameter& parameterToUse, juce::StringArray labelsToUse)
    : labels (std::move (labelsToUse)),
      attachment (parameterToUse, [this] (float value)
                  {
                      current = juce::roundToInt (value);
                      repaint();
                  })
{
    attachment.sendInitialUpdate();
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

juce::Rectangle<float> ChoiceSwitch::segmentBounds (int index) const
{
    constexpr float gap = 6.0f;
    const auto inner = getLocalBounds().toFloat().reduced (2.0f);
    const float width = (inner.getWidth() - gap * (float) (labels.size() - 1)) / (float) labels.size();
    return { inner.getX() + (width + gap) * (float) index, inner.getY(), width, inner.getHeight() };
}

void ChoiceSwitch::paint (juce::Graphics& g)
{
    for (int i = 0; i < labels.size(); ++i)
    {
        const auto key = segmentBounds (i);
        const bool active = i == current;
        keyCap (g, key, active, i == hovered);
        led (g, { key.getX() + 11.0f, key.getCentreY() }, active ? 1.0f : 0.0f);
        g.setFont (fonts->bodyFont (10.5f, 0.2f));
        g.setColour (active ? pal::bone : (i == hovered ? pal::ink : pal::inkDim));
        g.drawText (labels[i], key.withTrimmedLeft (18.0f).withTrimmedRight (2.0f), juce::Justification::centred, false);
    }
}

void ChoiceSwitch::mouseDown (const juce::MouseEvent& e)
{
    for (int i = 0; i < labels.size(); ++i)
        if (segmentBounds (i).contains (e.position) && i != current)
            attachment.setValueAsCompleteGesture ((float) i);
}

void ChoiceSwitch::mouseMove (const juce::MouseEvent& e)
{
    int over = -1;
    for (int i = 0; i < labels.size(); ++i)
        if (segmentBounds (i).contains (e.position))
            over = i;

    if (over != hovered)
    {
        hovered = over;
        repaint();
    }
}

void ChoiceSwitch::mouseExit (const juce::MouseEvent&)
{
    hovered = -1;
    repaint();
}

//==============================================================================
LedButton::LedButton (juce::RangedAudioParameter& parameterToUse, const juce::String& textWhenLit, const juce::String& textWhenDark, bool litWhenOff)
    : juce::Button (textWhenLit),
      litText (textWhenLit),
      darkText (textWhenDark),
      inverted (litWhenOff),
      attachment (parameterToUse, *this)
{
    setClickingTogglesState (true);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    attachment.sendInitialUpdate();
}

void LedButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const bool lit = getToggleState() != inverted;
    const auto key = getLocalBounds().toFloat().reduced (2.0f);
    keyCap (g, key, down || lit, highlighted);

    const auto font = fonts->bodyFont (10.5f, 0.2f);
    const auto& caption = lit ? litText : darkText;
    const float textWidth = juce::GlyphArrangement::getStringWidth (font, caption);
    const float startX = key.getCentreX() - 0.5f * (6.0f + 9.0f + textWidth);

    led (g, { startX + 3.0f, key.getCentreY() }, lit ? 1.0f : 0.0f);
    g.setColour (lit ? pal::bone : (highlighted ? pal::ink : pal::inkDim));
    g.setFont (font);
    g.drawText (caption, juce::Rectangle<float> (startX + 15.0f, key.getY(), textWidth + 6.0f, key.getHeight()), juce::Justification::centredLeft, false);
}

//==============================================================================
NoteGrid::NoteGrid (juce::AudioProcessorValueTreeState& stateToUse, std::function<std::uint16_t()> noteMask)
    : state (stateToUse),
      getMask (std::move (noteMask)),
      key (stateToUse.getRawParameterValue (params::id::key))
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    refresh();
}

namespace
{
    constexpr bool isBlack (int note) noexcept { return note == 1 || note == 3 || note == 6 || note == 8 || note == 10; }
    constexpr int whiteIndex[12] { 0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6 };   // touche blanche de la note (ou à gauche de la noire)
}

juce::Rectangle<float> NoteGrid::keyBounds (int note) const
{
    const auto bounds = getLocalBounds().toFloat().reduced (3.0f);
    const float white = bounds.getWidth() / 7.0f;
    if (! isBlack (note))
        return { bounds.getX() + white * (float) whiteIndex[note], bounds.getY(), white, bounds.getHeight() };

    const float black = white * 0.62f;
    return { bounds.getX() + white * (float) (whiteIndex[note] + 1) - 0.5f * black, bounds.getY(), black, bounds.getHeight() * 0.58f };
}

int NoteGrid::noteAt (juce::Point<float> position) const
{
    for (int note = 0; note < 12; ++note)
        if (isBlack (note) && keyBounds (note).contains (position))
            return note;
    for (int note = 0; note < 12; ++note)
        if (! isBlack (note) && keyBounds (note).contains (position))
            return note;
    return -1;
}

void NoteGrid::refresh()
{
    const auto mask = getMask();
    const int currentKey = juce::roundToInt (key->load());
    if (mask != shownMask || currentKey != shownKey)
    {
        shownMask = mask;
        shownKey = currentKey;
        repaint();
    }
}

void NoteGrid::paint (juce::Graphics& g)
{
    // le logement du clavier
    const auto bed = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (juce::Colours::black.withAlpha (0.75f));
    g.fillRoundedRectangle (bed, 4.0f);
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.drawRoundedRectangle (bed.expanded (0.5f), 4.5f, 0.8f);

    for (const bool black : { false, true })
    {
        for (int note = 0; note < 12; ++note)
        {
            if (isBlack (note) != black)
                continue;

            const auto k = keyBounds (note).reduced (black ? 0.0f : 0.6f, 0.0f);
            const bool lit = (shownMask & (1u << note)) != 0;
            const bool over = note == hovered;
            juce::Path shape;
            shape.addRoundedRectangle (k.getX(), k.getY(), k.getWidth(), k.getHeight(), 2.5f, 2.5f, false, false, true, true);

            if (black)
            {
                g.setColour (juce::Colours::black.withAlpha (0.6f));
                g.fillRoundedRectangle (k.translated (1.0f, 1.5f), 2.5f);
                g.setGradientFill (juce::ColourGradient (juce::Colour (lit ? 0xff2a2222 : 0xff171212).brighter (over ? 0.15f : 0.0f), 0.0f, k.getY(),
                                                         juce::Colour (0xff0a0707), 0.0f, k.getBottom(), false));
                g.fillPath (shape);
                g.setColour (juce::Colours::white.withAlpha (lit ? 0.14f : 0.06f));
                g.drawLine (k.getX() + 2.0f, k.getBottom() - 4.0f, k.getRight() - 2.0f, k.getBottom() - 4.0f, 1.0f);
            }
            else
            {
                const auto top = lit ? pal::ivory : juce::Colour (0xff4a403e);
                g.setGradientFill (juce::ColourGradient (top.darker (0.25f).brighter (over ? 0.1f : 0.0f), 0.0f, k.getY(),
                                                         top.brighter (over ? 0.1f : 0.0f), 0.0f, k.getY() + k.getHeight() * 0.3f, false));
                g.fillPath (shape);
                g.setColour (juce::Colours::black.withAlpha (0.55f));
                g.strokePath (shape, juce::PathStrokeType (1.0f));
                g.setFont (fonts->bodyFont (9.0f, 0.1f));
                g.setColour (note == shownKey ? pal::ember : (lit ? juce::Colour (0xff3b302e) : pal::inkDim.withAlpha (0.7f)));
                g.drawText (tune::noteNames[note], k.withTop (k.getBottom() - 15.0f), juce::Justification::centred, false);
            }

            led (g, { k.getCentreX(), black ? k.getBottom() - 10.0f : k.getY() + k.getHeight() * 0.62f }, lit ? 1.0f : 0.0f, 2.4f);

            if (note == shownKey)   // la tonique : un trait de braise en haut de la touche
            {
                g.setColour (pal::emberHot);
                g.fillRect (k.getX() + 3.0f, k.getY() + 1.0f, k.getWidth() - 6.0f, 2.0f);
            }
        }
    }
}

void NoteGrid::mouseDown (const juce::MouseEvent& e)
{
    const int note = noteAt (e.position);
    if (note < 0)
        return;

    if (auto* parameter = state.getParameter (params::noteToggleId (note)))
    {
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (parameter->getValue() > 0.5f ? 0.0f : 1.0f);
        parameter->endChangeGesture();
    }

    refresh();
}

void NoteGrid::mouseMove (const juce::MouseEvent& e)
{
    const int over = noteAt (e.position);
    if (over != hovered)
    {
        hovered = over;
        repaint();
    }
}

void NoteGrid::mouseExit (const juce::MouseEvent&)
{
    hovered = -1;
    repaint();
}

} // namespace onirique::ui
