#include "Controls.h"

#include "../DSP/Scales.h"
#include "../Parameters.h"

namespace onirique::ui
{

namespace
{
    constexpr float knobTextHeight = 36.0f;
}

void drawLed (juce::Graphics& g, juce::Point<float> centre, float diameter, bool lit)
{
        const auto dot = juce::Rectangle<float> (diameter, diameter).withCentre (centre);
        if (lit)
        {
            g.setColour (palette::ember.withAlpha (0.22f));
            g.fillEllipse (dot.expanded (diameter * 0.75f));
            g.setColour (palette::ember.withAlpha (0.4f));
            g.fillEllipse (dot.expanded (diameter * 0.35f));
            g.setColour (palette::ember);
        }
        else
        {
            g.setColour (palette::ledOff());
        }

        g.fillEllipse (dot);
}

//==============================================================================
Knob::Knob (juce::AudioProcessorValueTreeState& state, const juce::String& parameterId, const juce::String& labelToUse, bool bipolar)
    : label (labelToUse),
      attachment (state, parameterId, slider)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    slider.setMouseDragSensitivity (220);
    slider.getProperties().set ("bipolar", bipolar);
    slider.onValueChange = [this] { repaint(); };
    slider.addMouseListener (this, false);
    addAndMakeVisible (slider);
}

void Knob::resized()
{
    auto area = getLocalBounds();
    area.removeFromBottom ((int) knobTextHeight);
    const int size = juce::jmin (area.getWidth(), area.getHeight());
    slider.setBounds (area.withSizeKeepingCentre (size, size));
}

void Knob::paint (juce::Graphics& g)
{
    const bool hot = slider.isMouseOverOrDragging();
    auto text = getLocalBounds().toFloat().removeFromBottom (knobTextHeight);

    g.setColour (hot ? palette::mist : palette::haze);
    g.setFont (fonts->bodyFont (11.0f, 0.1f));
    g.drawText (label.toUpperCase(), text.removeFromTop (16.0f), juce::Justification::centred, false);

    const auto valueText = slider.getTextFromValue (slider.getValue());
    const auto font = fonts->monoFont (11.0f);
    const float width = juce::GlyphArrangement::getStringWidth (font, valueText) + 16.0f;
    const auto pill = juce::Rectangle<float> (width, 17.0f).withCentre ({ text.getCentreX(), text.getY() + 10.0f });

    g.setColour (juce::Colours::black.withAlpha (0.4f));
    g.fillRoundedRectangle (pill, 8.5f);
    g.setColour (hot ? palette::ember.withAlpha (0.7f) : palette::line());
    g.drawRoundedRectangle (pill, 8.5f, 1.0f);
    g.setColour (palette::bone.withAlpha (isEnabled() ? 1.0f : 0.5f));
    g.setFont (font);
    g.drawText (valueText, pill, juce::Justification::centred, false);
}

//==============================================================================
Fader::Fader (juce::AudioProcessorValueTreeState& state, const juce::String& parameterId, const juce::String& labelToUse)
    : label (labelToUse),
      attachment (state, parameterId, slider)
{
    slider.setSliderStyle (juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
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

    g.setColour (slider.isMouseOverOrDragging() ? palette::mist : palette::haze);
    g.setFont (fonts->monoFont (9.5f, 0.14f));
    g.drawText (label.toUpperCase(), bounds.withWidth (52.0f), juce::Justification::centredLeft, false);

    g.setColour (palette::bone);
    g.setFont (fonts->monoFont (11.0f));
    g.drawText (slider.getTextFromValue (slider.getValue()), bounds.withTrimmedLeft (bounds.getWidth() - 46.0f), juce::Justification::centredRight, false);
}

//==============================================================================
ChoiceSwitch::ChoiceSwitch (juce::RangedAudioParameter& parameter, juce::StringArray labelsToUse)
    : labels (std::move (labelsToUse)),
      attachment (parameter, [this] (float value)
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
    const auto inner = getLocalBounds().toFloat().reduced (3.0f);
    const float width = inner.getWidth() / (float) labels.size();
    return { inner.getX() + width * (float) index, inner.getY(), width, inner.getHeight() };
}

void ChoiceSwitch::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    const float radius = bounds.getHeight() * 0.5f;

    g.setColour (palette::well());
    g.fillRoundedRectangle (bounds, radius);
    g.setColour (palette::line());
    g.drawRoundedRectangle (bounds, radius, 1.0f);

    for (int i = 0; i < labels.size(); ++i)
    {
        const auto segment = segmentBounds (i);
        const bool active = i == current;

        if (active)
        {
            juce::Path pill;
            pill.addRoundedRectangle (segment, segment.getHeight() * 0.5f);
            juce::DropShadow (palette::ember.withAlpha (0.8f), 10, {}).drawForPath (g, pill);
            g.setColour (palette::ember);
            g.fillPath (pill);
        }

        g.setColour (active ? juce::Colours::white : (i == hovered ? palette::mist : palette::haze));
        g.setFont (fonts->monoBoldFont (12.5f, 0.08f));
        g.drawText (labels[i], segment, juce::Justification::centred, false);
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
LedButton::LedButton (juce::RangedAudioParameter& parameter, const juce::String& textWhenLit, const juce::String& textWhenDark, bool litWhenOff)
    : juce::Button (textWhenLit),
      litText (textWhenLit),
      darkText (textWhenDark),
      inverted (litWhenOff),
      attachment (parameter, *this)
{
    setClickingTogglesState (true);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    attachment.sendInitialUpdate();
}

void LedButton::paintButton (juce::Graphics& g, bool highlighted, bool)
{
    const bool lit = getToggleState() != inverted;
    const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    const float radius = bounds.getHeight() * 0.5f;

    g.setColour (juce::Colours::black.withAlpha (0.3f));
    g.fillRoundedRectangle (bounds, radius);
    g.setColour (lit ? palette::ember.withAlpha (0.6f) : (highlighted ? palette::rose.withAlpha (0.3f) : palette::line()));
    g.drawRoundedRectangle (bounds, radius, 1.0f);

    const auto font = fonts->monoBoldFont (11.5f, 0.08f);
    const auto& caption = lit ? litText : darkText;
    const float textWidth = juce::GlyphArrangement::getStringWidth (font, caption);
    const float ledSize = 8.0f, gap = 8.0f;
    const float startX = bounds.getCentreX() - 0.5f * (ledSize + gap + textWidth);

    drawLed (g, { startX + ledSize * 0.5f, bounds.getCentreY() }, ledSize, lit);

    g.setColour (lit ? palette::mist : (highlighted ? palette::mist : palette::haze));
    g.setFont (font);
    g.drawText (caption, juce::Rectangle<float> (startX + ledSize + gap, bounds.getY(), textWidth + 4.0f, bounds.getHeight()),
                juce::Justification::centredLeft, false);
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

juce::Rectangle<float> NoteGrid::cellBounds (int note) const
{
    constexpr int columns = 4, rows = 3;
    constexpr float gap = 6.0f;
    const auto bounds = getLocalBounds().toFloat();
    const float width = (bounds.getWidth() - gap * (columns - 1)) / columns;
    const float height = (bounds.getHeight() - gap * (rows - 1)) / rows;
    return { (float) (note % columns) * (width + gap), (float) (note / columns) * (height + gap), width, height };
}

int NoteGrid::noteAt (juce::Point<float> position) const
{
    for (int note = 0; note < 12; ++note)
        if (cellBounds (note).contains (position))
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
    for (int note = 0; note < 12; ++note)
    {
        const auto cell = cellBounds (note).reduced (0.5f);
        const bool lit = (shownMask & (1u << note)) != 0;
        const bool root = note == shownKey;
        const float radius = cell.getHeight() * 0.5f;

        g.setColour (juce::Colours::black.withAlpha (0.3f));
        g.fillRoundedRectangle (cell, radius);
        g.setColour (lit ? palette::ember.withAlpha (root ? 0.95f : 0.55f)
                         : (note == hovered ? palette::rose.withAlpha (0.3f) : palette::line()));
        g.drawRoundedRectangle (cell, radius, 1.0f);

        drawLed (g, { cell.getX() + 12.0f, cell.getCentreY() }, 6.5f, lit);

        g.setColour (lit ? palette::mist : (note == hovered ? palette::mist : palette::haze.withAlpha (0.75f)));
        g.setFont (fonts->monoBoldFont (11.5f, 0.04f));
        g.drawText (tune::noteNames[note], cell.withTrimmedLeft (20.0f).withTrimmedRight (6.0f), juce::Justification::centred, false);
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
