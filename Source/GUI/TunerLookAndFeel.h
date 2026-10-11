#pragma once

#include "Theme.h"

namespace onirique::ui
{

/** Habillage des contrôles JUCE standards (potards, listes déroulantes,
    menus) dans la charte ONIRIQUE. */
class TunerLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    TunerLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;

    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos, float minSliderPos,
                           float maxSliderPos, juce::Slider::SliderStyle, juce::Slider&) override;
    int getSliderThumbRadius (juce::Slider&) override { return 7; }

    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;

    juce::Font getPopupMenuFont() override;
    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
    void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                            bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& text,
                            const juce::String& shortcutKeyText, const juce::Drawable* icon, const juce::Colour* textColour) override;
    int getPopupMenuBorderSize() override { return 6; }

    void drawCornerResizer (juce::Graphics&, int width, int height, bool isMouseOver, bool isMouseDragging) override;

private:
    Fonts fonts;
};

} // namespace onirique::ui
