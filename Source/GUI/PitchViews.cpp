#include "PitchViews.h"
#include "Instrument.h"

#include "../DSP/Scales.h"

namespace onirique::ui
{

namespace
{
    juce::String noteName (int midi)
    {
        return tune::noteNames[((midi % 12) + 12) % 12];
    }

    int octaveOf (int midi)
    {
        return (int) std::floor ((double) midi / 12.0) - 1;
    }

    /** Écart en demi-tons -> angle sur l'échelle : +/- 50 cents sur +/- 60 degrés. */
    float angleOf (float semitones) noexcept
    {
        return juce::jlimit (-0.55f, 0.55f, semitones) * 2.0f * (pi / 3.0f);
    }
}

//==============================================================================
TunerDial::TunerDial()
{
    sigil = juce::ImageCache::getFromMemory (BinaryData::sigil_png, BinaryData::sigil_pngSize);
    setInterceptsMouseClicks (false, false);
}

void TunerDial::push (const tune::DisplayFrame& frame)
{
    if (! frame.voiced)
        return;

    // Si la note change, on saute dessus ; sinon on lisse.
    const bool jump = std::abs (frame.outMidi - outMidi) > 0.75f || silentFor > 0.25;
    inMidi = jump ? frame.inMidi : inMidi + 0.35f * (frame.inMidi - inMidi);
    outMidi = jump ? frame.outMidi : outMidi + 0.35f * (frame.outMidi - outMidi);
    noteMidi = frame.noteMidi;
    silentFor = 0.0;
}

void TunerDial::tick (double seconds)
{
    clock += seconds;
    silentFor += seconds;

    const float target = silentFor < 0.18 ? 1.0f : 0.0f;
    const float rate = target > presence ? 14.0f : 2.2f;
    presence += juce::jlimit (0.0f, 1.0f, rate * (float) seconds) * (target - presence);

    // l'aiguille a un peu d'inertie, comme une vraie
    const int note = juce::roundToInt (vocoderOn ? noteMidi : outMidi);
    approach (needle, inMidi - (float) note, 0.45f);
    approach (tuned, outMidi - (float) note, 0.6f);

    repaint();
}

void TunerDial::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    const float side = juce::jmin (area.getWidth(), area.getHeight());
    const auto c = area.getCentre();
    const float u = side / 276.0f;
    const float R = side * 0.5f - 3.0f, glassR = R - 11.0f * u;

    // le logement, la lunette de métal tourné, puis le verre
    g.setColour (juce::Colours::black.withAlpha (0.7f));
    g.fillEllipse (circle (c.translated (0.0f, 2.0f), R + 2.0f));
    g.setGradientFill (juce::ColourGradient (pal::metalHi.brighter (0.25f), c.x, c.y - R, pal::metalLo, c.x, c.y + R, false));
    g.fillEllipse (circle (c, R));
    for (int i = 0; i < 120; ++i)
    {
        const float a = (float) i / 120.0f * twoPi;
        const float light = juce::jmax (0.0f, -std::cos (a - 0.35f));
        g.setColour (juce::Colours::white.withAlpha (0.015f + 0.05f * light * light));
        g.drawLine (juce::Line<float> (polar (c, a, glassR + 2.0f), polar (c, a, R - 1.0f)), 1.0f);
    }
    g.setColour (juce::Colours::black.withAlpha (0.85f));
    g.drawEllipse (circle (c, R), 1.0f);

    {
        juce::Graphics::ScopedSaveState save (g);
        juce::Path face;
        face.addEllipse (circle (c, glassR));
        g.reduceClipRegion (face);

        // le fond du cadran : la voix l'éclaire de l'intérieur
        juce::ColourGradient bg (pal::bloodDeep.withMultipliedBrightness (0.6f + 0.9f * presence), c.x, c.y, pal::glass, c.x + glassR, c.y, true);
        g.setGradientFill (bg);
        g.fillEllipse (circle (c, glassR));

        // l'échelle : -50 à +50 cents, un trait tous les 10, et ses chiffres
        const float scaleR = glassR - 20.0f * u;
        for (int i = -5; i <= 5; ++i)
        {
            const float a = angleOf ((float) i * 0.1f);
            const bool major = i == 0 || std::abs (i) == 5;
            g.setColour (pal::rose.withAlpha (major ? 0.55f : 0.25f));
            g.drawLine (juce::Line<float> (polar (c, a, scaleR - (major ? 12.0f : 7.0f) * u), polar (c, a, scaleR)), major ? 1.4f * u : 1.0f * u);
        }
        g.setFont (fonts->monoFont (9.0f * u));
        g.setColour (pal::rose.withAlpha (0.45f));
        for (auto [s, t] : { std::pair { -0.5f, "-50" }, std::pair { 0.0f, "0" }, std::pair { 0.5f, "+50" } })
            g.drawText (t, circle (polar (c, angleOf (s), scaleR - 22.0f * u), 12.0f * u), juce::Justification::centred, false);

        // Flex-Tune : deux crochets bornent la zone où la voix est corrigée (même calcul que le correcteur)
        if (flex > 0.001f)
        {
            const float inner = std::pow (1.0f - juce::jmin (1.0f, flex), 1.5f) * 0.5f + 0.04f;
            const float outer = inner * 1.6f + 0.08f;
            if (outer < 0.55f)
            {
                g.setColour (pal::bone.withAlpha (0.75f));
                for (const float edge : { -1.0f, 1.0f })
                {
                    const float a = angleOf (edge * outer), cap = -edge * 0.06f;
                    juce::Path bracket;
                    bracket.startNewSubPath (polar (c, a + cap, scaleR + 9.0f * u));
                    bracket.lineTo (polar (c, a, scaleR + 9.0f * u));
                    bracket.lineTo (polar (c, a, scaleR - 9.0f * u));
                    bracket.lineTo (polar (c, a + cap, scaleR - 9.0f * u));
                    g.strokePath (bracket, juce::PathStrokeType (1.4f * u));
                }
            }
        }

        // au repos, le sigil en filigrane
        if (sigil.isValid() && presence < 0.98f)
        {
            const float w = glassR * 0.95f, h = w * (float) sigil.getHeight() / (float) sigil.getWidth();
            g.setOpacity (0.22f * (1.0f - presence));
            g.drawImage (sigil, juce::Rectangle<float> (w, h).withCentre (c.translated (0.0f, 8.0f * u)), juce::RectanglePlacement::centred);
            g.setOpacity (1.0f);
        }

        if (presence > 0.02f)
        {
            // la correction : l'arc de braise entre la voix chantée et la sortie
            const float a0 = angleOf (juce::jmin (needle, tuned)), a1 = angleOf (juce::jmax (needle, tuned));
            if (a1 - a0 > 0.004f)
            {
                juce::Path correction;
                correction.addCentredArc (c.x, c.y, scaleR + 4.0f * u, scaleR + 4.0f * u, 0.0f, a0, a1, true);
                g.setColour (pal::ember.withAlpha (0.25f * presence));
                g.strokePath (correction, juce::PathStrokeType (7.0f * u, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                g.setColour (pal::emberHot.withAlpha (presence));
                g.strokePath (correction, juce::PathStrokeType (2.5f * u, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }

            // la sortie : un témoin de braise sur l'échelle ; la voix : l'aiguille claire
            led (g, polar (c, angleOf (tuned), scaleR + 4.0f * u), presence, 3.6f * u);
            const float an = angleOf (needle);
            juce::Path pointer;
            const auto tip = polar (c, an, scaleR + 9.0f * u);
            const auto base = polar (c, an, scaleR - 16.0f * u);
            const auto side2 = polar ({}, an + 0.5f * pi, 2.2f * u);
            pointer.addTriangle (tip, base + side2, base - side2);
            g.setColour (juce::Colours::black.withAlpha (0.5f * presence));
            g.fillPath (pointer, juce::AffineTransform::translation (0.0f, 1.0f));
            g.setColour (pal::bone.withAlpha (presence));
            g.fillPath (pointer);

            // la note visée, en grand
            const int note = juce::roundToInt (vocoderOn ? noteMidi : outMidi);
            const auto font = fonts->displayFont (78.0f * u);
            const auto name = noteName (note);
            const float width = juce::GlyphArrangement::getStringWidth (font, name);
            const float baseline = c.y + 36.0f * u;
            juce::GlyphArrangement glyphs;
            glyphs.addLineOfText (font, name, c.x - width * 0.5f - 8.0f * u, baseline);
            juce::Path text;
            glyphs.createPath (text);
            g.setColour (pal::ember.withAlpha (0.18f * presence));
            g.strokePath (text, juce::PathStrokeType (6.0f * u, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (pal::mist.withAlpha (presence));
            g.fillPath (text);

            g.setColour (pal::rose.withAlpha (0.6f * presence));
            g.setFont (fonts->monoFont (18.0f * u));
            g.drawText (juce::String (octaveOf (note)), juce::Rectangle<float> (c.x + width * 0.5f - 4.0f * u, baseline - 22.0f * u, 36.0f * u, 24.0f * u),
                        juce::Justification::centredLeft, false);

            const int centsOff = juce::roundToInt ((inMidi - (float) note) * 100.0f);
            g.setColour (pal::bone.withAlpha (0.85f * presence));
            g.setFont (fonts->monoFont (14.0f * u, 0.04f));
            g.drawText ((centsOff > 0 ? "+" : "") + juce::String (centsOff) + " cents",
                        juce::Rectangle<float> (c.x - 80.0f * u, baseline + 10.0f * u, 160.0f * u, 20.0f * u), juce::Justification::centred, false);

            if (vocoderOn)
            {
                g.setColour (pal::emberHot.withAlpha (presence));
                g.setFont (fonts->monoBoldFont (11.0f * u, 0.16f));
                g.drawText ("MIDI " + juce::String (note), juce::Rectangle<float> (c.x - 60.0f * u, baseline + 32.0f * u, 120.0f * u, 16.0f * u),
                            juce::Justification::centred, false);
            }
        }

        // le verre : ombre du bord haut et reflet en biais
        g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.55f), c.x, c.y - glassR, juce::Colours::transparentBlack, c.x, c.y - glassR + 30.0f * u, false));
        g.fillEllipse (circle (c, glassR));
        juce::ColourGradient sheen (juce::Colours::white.withAlpha (0.0f), c.x - glassR, c.y - glassR, juce::Colours::white.withAlpha (0.0f), c.x + glassR, c.y + glassR, false);
        sheen.addColour (0.38, juce::Colours::white.withAlpha (0.0f));
        sheen.addColour (0.45, juce::Colours::white.withAlpha (0.035f));
        sheen.addColour (0.55, juce::Colours::white.withAlpha (0.0f));
        g.setGradientFill (sheen);
        g.fillEllipse (circle (c, glassR));
    }
    g.setColour (juce::Colours::black.withAlpha (0.9f));
    g.drawEllipse (circle (c, glassR), 1.2f);
}

//==============================================================================
PitchTrace::PitchTrace()
{
    setInterceptsMouseClicks (false, false);
}

void PitchTrace::push (const tune::DisplayFrame& frame)
{
    in[(size_t) head] = frame.voiced ? frame.inMidi : 0.0f;
    out[(size_t) head] = frame.voiced ? frame.outMidi : 0.0f;
    head = (head + 1) % capacity;
    dirty = true;
}

void PitchTrace::setNoteMask (std::uint16_t mask)
{
    if (mask != noteMask)
    {
        noteMask = mask;
        dirty = true;
    }
}

void PitchTrace::tick()
{
    // La fenêtre suit doucement la tessiture de ce qui est affiché : jamais
    // moins d'une octave, et toute la courbe reste visible.
    float lowest = 1000.0f, highest = 0.0f;
    int count = 0;
    for (int i = 0; i < capacity; ++i)
    {
        for (const float v : { in[(size_t) i], out[(size_t) i] })
        {
            if (v > 0.0f)
            {
                lowest = juce::jmin (lowest, v);
                highest = juce::jmax (highest, v);
                ++count;
            }
        }
    }

    if (count > 12)
    {
        float targetLow = lowest - 2.0f, targetHigh = highest + 2.0f;
        const float missing = 13.0f - (targetHigh - targetLow);
        if (missing > 0.0f)
        {
            targetLow -= 0.5f * missing;
            targetHigh += 0.5f * missing;
        }

        if (std::abs (targetLow - low) > 0.02f || std::abs (targetHigh - high) > 0.02f)
        {
            low += 0.12f * (targetLow - low);
            high += 0.12f * (targetHigh - high);
            dirty = true;
        }
    }

    if (dirty)
    {
        dirty = false;
        repaint();
    }
}

void PitchTrace::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced (2.0f);
    glassBack (g, bounds, 6.0f);

    {
        juce::Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (bounds.toNearestInt());

        const float labelWidth = 40.0f;
        const auto plot = bounds.withTrimmedLeft (labelWidth).reduced (0.0f, 8.0f);
        const auto yOf = [&] (float midi) { return plot.getBottom() - (midi - low) / (high - low) * plot.getHeight(); };

        // Lignes des notes : celles de la gamme ressortent et portent leur nom
        float lastLabelY = 1.0e6f;
        for (int note = (int) std::ceil (low); note <= (int) std::floor (high); ++note)
        {
            const bool allowed = (noteMask & (1u << (((note % 12) + 12) % 12))) != 0;
            const float y = yOf ((float) note);

            g.setColour (pal::rose.withAlpha (allowed ? 0.12f : 0.035f));
            g.fillRect (juce::Rectangle<float> (bounds.getX() + labelWidth, y - 0.5f, bounds.getWidth() - labelWidth, 1.0f));

            if (allowed && lastLabelY - y >= 11.0f)
            {
                lastLabelY = y;
                g.setColour (pal::rose.withAlpha (0.45f));
                g.setFont (fonts->monoFont (9.0f));
                g.drawText (noteName (note) + juce::String (octaveOf (note)), juce::Rectangle<float> (bounds.getX() + 8.0f, y - 6.0f, labelWidth - 10.0f, 12.0f),
                            juce::Justification::centredLeft, false);
            }
        }

        // Les deux courbes ; un trou dans la voix interrompt le trait
        const auto build = [&] (const std::array<float, capacity>& values)
        {
            juce::Path path;
            bool drawing = false;
            for (int i = 0; i < capacity; ++i)
            {
                const float v = values[(size_t) ((head + i) % capacity)];
                if (v <= 0.0f)
                {
                    drawing = false;
                    continue;
                }

                const float x = plot.getX() + plot.getWidth() * (float) i / (float) (capacity - 1);
                const float y = yOf (juce::jlimit (low - 1.0f, high + 1.0f, v));
                if (drawing)
                    path.lineTo (x, y);
                else
                    path.startNewSubPath (x, y);

                drawing = true;
            }
            return path;
        };

        const auto sung = build (in);
        const auto tuned = build (out);

        g.setColour (pal::bone.withAlpha (0.5f));
        g.strokePath (sung, juce::PathStrokeType (1.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (pal::ember.withAlpha (0.22f));
        g.strokePath (tuned, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (pal::emberHot);
        g.strokePath (tuned, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Le passé s'estompe vers la gauche
        juce::ColourGradient fade (pal::glass.withAlpha (0.9f), plot.getX(), 0.0f, pal::glass.withAlpha (0.0f), plot.getX() + 150.0f, 0.0f, false);
        g.setGradientFill (fade);
        g.fillRect (plot.withWidth (150.0f).expanded (0.0f, 8.0f));

        // Légende
        const float legendY = bounds.getY() + 13.0f;
        float x = bounds.getRight() - 150.0f;
        g.setFont (fonts->monoFont (9.0f, 0.12f));

        g.setColour (pal::bone.withAlpha (0.5f));
        g.fillRect (x, legendY - 0.75f, 14.0f, 1.5f);
        g.setColour (pal::rose.withAlpha (0.5f));
        g.drawText ("VOIX", juce::Rectangle<float> (x + 20.0f, legendY - 7.0f, 40.0f, 14.0f), juce::Justification::centredLeft, false);

        x += 68.0f;
        g.setColour (pal::emberHot);
        g.fillRect (x, legendY - 0.75f, 14.0f, 1.5f);
        g.setColour (pal::rose.withAlpha (0.5f));
        g.drawText ("TUNER", juce::Rectangle<float> (x + 20.0f, legendY - 7.0f, 50.0f, 14.0f), juce::Justification::centredLeft, false);
    }

    glassFront (g, bounds, 6.0f);
}

} // namespace onirique::ui
