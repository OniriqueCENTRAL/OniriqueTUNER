#include "TunerLookAndFeel.h"
#include "OniriqueKnob.h"

namespace onirique::ui
{

TunerLookAndFeel::TunerLookAndFeel()
{
    setColour (juce::ComboBox::textColourId, palette::mist);
    setColour (juce::ComboBox::backgroundColourId, palette::well());
    setColour (juce::ComboBox::outlineColourId, palette::line());
    setColour (juce::PopupMenu::backgroundColourId, palette::deep);
    setColour (juce::PopupMenu::textColourId, palette::mist);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, palette::ember.withAlpha (0.22f));
    setColour (juce::PopupMenu::highlightedTextColourId, palette::bone);
    setColour (juce::Label::textColourId, palette::mist);
}

//==============================================================================
//  Potard : le potard ONIRIQUE commun aux plugins de la marque (OniriqueKnob.h).
//==============================================================================
void TunerLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                         float rotaryStartAngle, float rotaryEndAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat();
    const bool hot = slider.isMouseOverOrDragging();
    const bool bipolar = (bool) slider.getProperties().getWithDefault ("bipolar", false);

    // Le potard ONIRIQUE, le même dans tous les plugins de la marque (OniriqueKnob.h) : graduations allumées jusqu'à la
    // valeur (depuis le centre si bipolaire), corps biseauté, aiguille à l'étoile.
    onirique::ui::drawKnob (g, bounds, sliderPos, rotaryStartAngle, rotaryEndAngle, palette::ember, bipolar, hot);
}

//==============================================================================
//  Curseur horizontal : piste fine, remplissage sang -> braise, poignée claire
//  (le fader du site, couché).
//==============================================================================
void TunerLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos, float,
                                         float, juce::Slider::SliderStyle, juce::Slider& slider)
{
    const float centreY = (float) y + (float) height * 0.5f;
    const auto track = juce::Rectangle<float> ((float) x, centreY - 3.0f, (float) width, 6.0f);

    g.setColour (palette::rose.withAlpha (0.1f));
    g.fillRoundedRectangle (track, 3.0f);

    const auto filled = track.withRight (juce::jmax (track.getX() + 1.0f, sliderPos));
    juce::Path fill;
    fill.addRoundedRectangle (filled, 3.0f);
    strokeGlow (g, fill, palette::ember.withAlpha (0.6f), 0.0f, 2.0f);
    g.setGradientFill (juce::ColourGradient (palette::blood, filled.getX(), centreY, palette::ember, filled.getRight(), centreY, false));
    g.fillPath (fill);

    const auto thumb = juce::Rectangle<float> (11.0f, 17.0f).withCentre ({ sliderPos, centreY });
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillRoundedRectangle (thumb.translated (0.0f, 1.5f), 3.5f);
    g.setGradientFill (juce::ColourGradient (palette::mist, thumb.getX(), thumb.getY(), juce::Colour (0xffb9a9ab), thumb.getX(), thumb.getBottom(), false));
    g.fillRoundedRectangle (thumb, 3.5f);

    if (slider.isMouseOverOrDragging())
    {
        g.setColour (palette::emberHot);
        g.drawRoundedRectangle (thumb, 3.5f, 1.0f);
    }
}

//==============================================================================
//  Liste déroulante : boîte arrondie, texte mono, chevron braise.
//==============================================================================
void TunerLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto bounds = juce::Rectangle<float> ((float) width, (float) height).reduced (0.5f);
    const bool hot = box.isMouseOver (true) || box.isPopupActive();

    g.setColour (palette::well());
    g.fillRoundedRectangle (bounds, 8.0f);
    g.setColour (hot ? palette::ember.withAlpha (0.7f) : palette::line());
    g.drawRoundedRectangle (bounds, 8.0f, 1.0f);

    const float cx = (float) width - 16.0f;
    const float cy = (float) height * 0.5f - 1.0f;
    juce::Path chevron;
    chevron.startNewSubPath (cx - 4.0f, cy - 2.0f);
    chevron.lineTo (cx, cy + 2.5f);
    chevron.lineTo (cx + 4.0f, cy - 2.0f);
    g.setColour (palette::ember);
    g.strokePath (chevron, juce::PathStrokeType (1.4f, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded));
}

juce::Font TunerLookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return fonts->monoFont (12.5f, 0.02f);
}

void TunerLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (11, 0, box.getWidth() - 36, box.getHeight());
    label.setFont (getComboBoxFont (box));
}

juce::Font TunerLookAndFeel::getPopupMenuFont()
{
    return fonts->monoFont (12.5f, 0.02f);
}

void TunerLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    const auto bounds = juce::Rectangle<float> ((float) width, (float) height);
    g.fillAll (palette::deep);
    g.setColour (palette::rose.withAlpha (0.2f));
    g.drawRect (bounds, 1.0f);
}

void TunerLookAndFeel::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                                          bool isHighlighted, bool isTicked, bool, const juce::String& text,
                                          const juce::String&, const juce::Drawable*, const juce::Colour*)
{
    if (isSeparator)
    {
        g.setColour (palette::line());
        g.fillRect (area.reduced (8, 0).withSizeKeepingCentre (area.getWidth() - 16, 1));
        return;
    }

    const auto row = area.reduced (4, 1).toFloat();
    if (isHighlighted && isActive)
    {
        g.setColour (palette::ember.withAlpha (0.22f));
        g.fillRoundedRectangle (row, 6.0f);
    }

    if (isTicked)
    {
        const auto dot = juce::Rectangle<float> (6.0f, 6.0f).withCentre ({ row.getX() + 11.0f, row.getCentreY() });
        g.setColour (palette::ember.withAlpha (0.45f));
        g.fillEllipse (dot.expanded (2.5f));
        g.setColour (palette::ember);
        g.fillEllipse (dot);
    }

    g.setColour (isActive ? (isHighlighted || isTicked ? palette::bone : palette::mist.withAlpha (0.82f)) : palette::haze.withAlpha (0.5f));
    g.setFont (getPopupMenuFont());
    g.drawText (text, row.withTrimmedLeft (24.0f).withTrimmedRight (8.0f), juce::Justification::centredLeft, true);
}

void TunerLookAndFeel::drawCornerResizer (juce::Graphics& g, int width, int height, bool isMouseOver, bool isMouseDragging)
{
    const float w = (float) width, h = (float) height;
    g.setColour (isMouseOver || isMouseDragging ? palette::ember : palette::rose.withAlpha (0.28f));

    for (const float inset : { 0.35f, 0.6f, 0.85f })
        g.drawLine (w * inset, h - 2.0f, w - 2.0f, h * inset, 1.0f);
}

} // namespace onirique::ui
