#include "TunerLookAndFeel.h"
#include "Instrument.h"

namespace onirique::ui
{

TunerLookAndFeel::TunerLookAndFeel()
{
    setColour (juce::ComboBox::textColourId, pal::ember.brighter (0.15f));
    setColour (juce::ComboBox::backgroundColourId, pal::glass);
    setColour (juce::ComboBox::outlineColourId, juce::Colours::black);
    setColour (juce::PopupMenu::backgroundColourId, pal::menu);
    setColour (juce::PopupMenu::textColourId, pal::ink);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, pal::ember.withAlpha (0.22f));
    setColour (juce::PopupMenu::highlightedTextColourId, pal::bone);
    setColour (juce::Label::textColourId, pal::ember.brighter (0.15f));
    setColour (juce::TooltipWindow::backgroundColourId, pal::menu);
    setColour (juce::TooltipWindow::textColourId, pal::ink);
    setColour (juce::TooltipWindow::outlineColourId, juce::Colours::black);
    setColour (juce::TextEditor::backgroundColourId, pal::glass);
    setColour (juce::TextEditor::textColourId, pal::emberHot);
    setColour (juce::TextEditor::highlightColourId, pal::ember.withAlpha (0.35f));
    setColour (juce::TextEditor::outlineColourId, juce::Colours::black);
    setColour (juce::TextEditor::focusedOutlineColourId, pal::ember.withAlpha (0.6f));
    setColour (juce::CaretComponent::caretColourId, pal::emberHot);
}

//==============================================================================
//  Potard : l'aluminium usiné de l'instrument (Instrument.h).
//==============================================================================
void TunerLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                         float rotaryStartAngle, float rotaryEndAngle, juce::Slider& slider)
{
    const bool bipolar = (bool) slider.getProperties().getWithDefault ("bipolar", false);
    drawInstrumentKnob (g, juce::Rectangle<int> (x, y, width, height).toFloat(), sliderPos, rotaryStartAngle, rotaryEndAngle,
                        slider.isEnabled() && slider.isMouseOverOrDragging(), bipolar);
}

//==============================================================================
//  Curseur horizontal : une glissière gravée dans la façade, la braise jusqu'au curseur, un curseur de métal.
//==============================================================================
void TunerLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos, float,
                                         float, juce::Slider::SliderStyle, juce::Slider& slider)
{
    const float centreY = (float) y + (float) height * 0.5f;
    const auto slot = juce::Rectangle<float> ((float) x, centreY - 2.5f, (float) width, 5.0f);

    g.setColour (juce::Colours::black.withAlpha (0.75f));
    g.fillRoundedRectangle (slot, 2.5f);
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.drawLine (slot.getX() + 2.0f, slot.getBottom() + 0.5f, slot.getRight() - 2.0f, slot.getBottom() + 0.5f, 1.0f);

    const auto filled = slot.reduced (1.0f).withRight (juce::jmax (slot.getX() + 1.0f, sliderPos));
    g.setColour (pal::ember.withAlpha (slider.isEnabled() ? 1.0f : 0.4f));
    g.fillRoundedRectangle (filled, 1.5f);

    const auto cap = juce::Rectangle<float> (12.0f, 20.0f).withCentre ({ sliderPos, centreY });
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillRoundedRectangle (cap.translated (0.0f, 1.5f), 2.5f);
    g.setGradientFill (juce::ColourGradient (pal::metalHi.brighter (slider.isMouseOverOrDragging() ? 0.3f : 0.15f), 0.0f, cap.getY(),
                                             pal::metalLo.brighter (0.3f), 0.0f, cap.getBottom(), false));
    g.fillRoundedRectangle (cap, 2.5f);
    g.setColour (juce::Colours::black.withAlpha (0.8f));
    g.drawRoundedRectangle (cap, 2.5f, 1.0f);
    g.setColour (pal::bone.withAlpha (0.85f));
    g.drawLine (cap.getCentreX(), cap.getY() + 4.0f, cap.getCentreX(), cap.getBottom() - 4.0f, 1.3f);
}

//==============================================================================
//  Liste déroulante : un petit affichage de verre, la valeur en braise, une flèche sérigraphiée.
//==============================================================================
void TunerLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto bounds = juce::Rectangle<float> ((float) width, (float) height).reduced (2.0f);
    const bool hot = box.isEnabled() && (box.isMouseOver (true) || box.isPopupActive());

    glassBack (g, bounds, 4.0f);
    menuArrow (g, bounds.getRight() - 16.0f, bounds.getCentreY(), pal::rose.withAlpha (hot ? 0.8f : 0.45f));
    glassFront (g, bounds, 4.0f);
}

juce::Font TunerLookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return fonts->monoBoldFont (12.0f, 0.02f);
}

void TunerLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (11, 0, box.getWidth() - 36, box.getHeight());
    label.setFont (getComboBoxFont (box));
}

juce::Font TunerLookAndFeel::getPopupMenuFont()
{
    return fonts->bodyFont (14.0f, 0.02f);
}

void TunerLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    g.fillAll (pal::menu);
    g.setColour (juce::Colours::black);
    g.drawRect (juce::Rectangle<float> ((float) width, (float) height), 1.0f);
}

void TunerLookAndFeel::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                                          bool isHighlighted, bool isTicked, bool, const juce::String& text,
                                          const juce::String&, const juce::Drawable*, const juce::Colour*)
{
    if (isSeparator)
    {
        groove (g, (float) area.getX() + 8.0f, (float) area.getCentreY(), (float) area.getRight() - 8.0f, (float) area.getCentreY());
        return;
    }

    const auto row = area.reduced (4, 1).toFloat();
    if (isHighlighted && isActive)
    {
        g.setColour (pal::ember.withAlpha (0.2f));
        g.fillRoundedRectangle (row, 3.0f);
    }

    if (isTicked)
        led (g, { row.getX() + 11.0f, row.getCentreY() }, 1.0f, 2.6f);

    g.setColour (isActive ? (isHighlighted || isTicked ? pal::bone : pal::ink.withAlpha (0.85f)) : pal::inkDim.withAlpha (0.5f));
    g.setFont (getPopupMenuFont());
    g.drawText (text, row.withTrimmedLeft (24.0f).withTrimmedRight (8.0f), juce::Justification::centredLeft, true);
}

// le coin pour agrandir la fenêtre : trois traits gravés
void TunerLookAndFeel::drawCornerResizer (juce::Graphics& g, int width, int height, bool isMouseOver, bool isMouseDragging)
{
    const bool hot = isMouseOver || isMouseDragging;
    for (int i = 1; i <= 3; ++i)
    {
        const float d = (float) i * (float) juce::jmin (width, height) / 4.0f;
        const float w = (float) width, h = (float) height;
        g.setColour (juce::Colours::black.withAlpha (0.7f));
        g.drawLine (w - d, h, w, h - d, 1.0f);
        g.setColour ((hot ? pal::emberHot : pal::ink).withAlpha (hot ? 0.8f : 0.25f));
        g.drawLine (w - d + 1.0f, h, w, h - d + 1.0f, 1.0f);
    }
}

} // namespace onirique::ui
