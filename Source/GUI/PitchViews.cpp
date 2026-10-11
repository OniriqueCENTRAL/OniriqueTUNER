#include "PitchViews.h"

#include "../DSP/Scales.h"

namespace onirique::ui
{

namespace
{
    constexpr float pi = juce::MathConstants<float>::pi;

    juce::String noteName (int midi)
    {
        return tune::noteNames[((midi % 12) + 12) % 12];
    }

    int octaveOf (int midi)
    {
        return (int) std::floor ((double) midi / 12.0) - 1;
    }

    /** Point sur un cercle, angle compté depuis le haut dans le sens horaire. */
    juce::Point<float> onCircle (juce::Point<float> centre, float radius, float angle)
    {
        return { centre.x + radius * std::sin (angle), centre.y - radius * std::cos (angle) };
    }
}

//==============================================================================
TunerDial::TunerDial()
{
    sigil = juce::ImageCache::getFromMemory (BinaryData::sigil_png, BinaryData::sigil_pngSize);
    setInterceptsMouseClicks (false, false);
}

void TunerDial::resized()
{
    const float side = (float) juce::jmin (getWidth(), getHeight());
    if (side < 8.0f || sigil.isNull())
        return;

    // Le sigil et son halo sont préparés une fois pour toutes à cette taille.
    const int width = juce::roundToInt (side * 0.66f);
    const int height = juce::roundToInt ((float) width * (float) sigil.getHeight() / (float) sigil.getWidth());
    sigilScaled = sigil.rescaled (width, height, juce::Graphics::highResamplingQuality);

    // Le halo déborde du sigil : on le calcule dans une image plus large.
    const int pad = juce::roundToInt (side * 0.12f);
    juce::Image padded (juce::Image::ARGB, width + 2 * pad, height + 2 * pad, true);
    {
        juce::Graphics pg (padded);
        pg.drawImageAt (sigilScaled, pad, pad);
    }

    sigilGlow = juce::Image (juce::Image::ARGB, padded.getWidth(), padded.getHeight(), true);
    juce::Graphics g (sigilGlow);
    juce::DropShadow (palette::ember.withAlpha (0.85f), pad / 2, {}).drawForImage (g, padded);
}

void TunerDial::push (const tune::DisplayFrame& frame)
{
    if (! frame.voiced)
        return;

    // Si la note change, on saute dessus ; sinon on lisse l'aiguille.
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

    repaint();
}

void TunerDial::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    const float side = juce::jmin (area.getWidth(), area.getHeight());
    const float u = side / 400.0f;
    const auto centre = area.getCentre();

    // Braise qui respire derrière le sigil
    {
        const float breathe = 0.82f + 0.18f * std::sin ((float) clock * 2.0f * pi / 5.0f);
        const float radius = 150.0f * u * (0.94f + 0.06f * breathe);
        juce::ColourGradient glow (palette::ember.withAlpha ((0.34f + 0.2f * presence) * breathe), centre.x, centre.y,
                                   palette::blood.withAlpha (0.0f), centre.x + radius, centre.y, true);
        glow.addColour (0.6, palette::blood.withAlpha (0.2f * breathe));
        g.setGradientFill (glow);
        g.fillEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre));
    }

    // Réticule : deux cercles en pointillés et quatre traits, un tour par minute
    {
        const auto spin = juce::AffineTransform::rotation ((float) clock * 2.0f * pi / 60.0f, centre.x, centre.y);
        g.setColour (juce::Colour (0xffffe1e4).withAlpha (0.38f));

        juce::Path inner, innerDashed;
        inner.addCentredArc (centre.x, centre.y, 150.0f * u, 150.0f * u, 0.0f, 0.0f, 2.0f * pi, true);
        const float innerDashes[] = { 180.0f * u, 40.0f * u, 60.0f * u, 40.0f * u, 120.0f * u, 50.0f * u };
        juce::PathStrokeType (1.2f * u).createDashedStroke (innerDashed, inner, innerDashes, 6, spin);
        g.fillPath (innerDashed);

        juce::Path outer, outerDashed;
        outer.addCentredArc (centre.x, centre.y, 186.0f * u, 186.0f * u, 0.0f, 0.0f, 2.0f * pi, true);
        const float outerDashes[] = { 2.0f * u, 9.0f * u };
        juce::PathStrokeType (0.9f * u).createDashedStroke (outerDashed, outer, outerDashes, 2, spin);
        g.fillPath (outerDashed);

        juce::Path ticks;
        for (int i = 0; i < 4; ++i)
        {
            const float a = (float) i * 0.5f * pi;
            ticks.startNewSubPath (onCircle (centre, 134.0f * u, a));
            ticks.lineTo (onCircle (centre, 194.0f * u, a));
        }
        g.strokePath (ticks, juce::PathStrokeType (1.4f * u), spin);
    }

    // Sigil : entier au repos, en filigrane quand la note s'affiche
    if (sigilScaled.isValid())
    {
        const float alpha = 1.0f - 0.9f * presence;
        const float hover = 4.0f * u * std::sin ((float) clock * 2.0f * pi / 7.0f);
        const auto glowAt = juce::Point<float> (centre.x - (float) sigilGlow.getWidth() * 0.5f, centre.y - (float) sigilGlow.getHeight() * 0.5f + hover);
        const auto imageAt = juce::Point<float> (centre.x - (float) sigilScaled.getWidth() * 0.5f, centre.y - (float) sigilScaled.getHeight() * 0.5f + hover);

        g.setOpacity (0.9f * alpha);
        g.drawImageAt (sigilGlow, juce::roundToInt (glowAt.x), juce::roundToInt (glowAt.y));
        g.setOpacity (alpha);
        g.drawImageAt (sigilScaled, juce::roundToInt (imageAt.x), juce::roundToInt (imageAt.y));
        g.setOpacity (1.0f);
    }

    // Étoiles qui scintillent autour
    {
        const juce::Point<float> spots[] = { { 0.80f, 0.10f }, { 0.10f, 0.84f }, { 0.97f, 0.47f } };
        const float sizes[] = { 20.0f, 13.0f, 15.0f };
        for (int i = 0; i < 3; ++i)
        {
            const float twinkle = 0.5f + 0.5f * std::sin ((float) clock * 2.0f * pi / 3.2f + (float) i * 2.1f);
            drawStar (g, { area.getX() + spots[i].x * side + (area.getWidth() - side) * 0.5f, area.getY() + spots[i].y * side + (area.getHeight() - side) * 0.5f },
                      sizes[i] * u * 1.3f * (0.6f + 0.4f * twinkle), 0.2f + 0.8f * twinkle);
        }
    }

    if (presence < 0.02f)
        return;

    // Avec le vocodeur, la note affichée est celle que joue son synthé.
    const int note = juce::roundToInt (vocoderOn ? noteMidi : outMidi);
    const float inOffset = juce::jlimit (-0.55f, 0.55f, inMidi - (float) note);
    const float outOffset = juce::jlimit (-0.55f, 0.55f, outMidi - (float) note);

    // Jauge : +/- 50 cents sur un arc de 120 degrés en haut du réticule
    {
        const float radius = 116.0f * u;
        const auto angleOf = [] (float semitones) { return semitones * 2.0f * (pi / 3.0f); };

        juce::Path track;
        track.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, angleOf (-0.5f), angleOf (0.5f), true);
        g.setColour (palette::rose.withAlpha (0.16f * presence));
        g.strokePath (track, juce::PathStrokeType (3.0f * u, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        juce::Path ticks;
        for (int i = -2; i <= 2; ++i)
        {
            const float a = angleOf ((float) i * 0.25f);
            ticks.startNewSubPath (onCircle (centre, radius + 7.0f * u, a));
            ticks.lineTo (onCircle (centre, radius + (i == 0 ? 17.0f : 12.0f) * u, a));
        }
        g.setColour (palette::rose.withAlpha (0.4f * presence));
        g.strokePath (ticks, juce::PathStrokeType (1.2f * u));

        const float a0 = angleOf (juce::jmin (inOffset, outOffset));
        const float a1 = angleOf (juce::jmax (inOffset, outOffset));
        if (a1 - a0 > 0.004f)
        {
            juce::Path correction;
            correction.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, a0, a1, true);
            strokeGlow (g, correction, palette::ember.withAlpha (presence), 3.0f * u, 2.4f * u);
            g.setColour (palette::ember.withAlpha (presence));
            g.strokePath (correction, juce::PathStrokeType (3.0f * u, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        const auto sung = onCircle (centre, radius, angleOf (inOffset));
        g.setColour (palette::bone.withAlpha (presence));
        g.fillEllipse (juce::Rectangle<float> (9.0f * u, 9.0f * u).withCentre (sung));

        const auto tuned = onCircle (centre, radius, angleOf (outOffset));
        g.setColour (palette::ember.withAlpha (0.45f * presence));
        g.fillEllipse (juce::Rectangle<float> (20.0f * u, 20.0f * u).withCentre (tuned));
        g.setColour (palette::emberHot.withAlpha (presence));
        g.fillEllipse (juce::Rectangle<float> (11.0f * u, 11.0f * u).withCentre (tuned));
    }

    // Note visée, en grand, incandescente
    {
        const auto font = fonts->displayFont (104.0f * u);
        const auto name = noteName (note);
        const float width = juce::GlyphArrangement::getStringWidth (font, name);
        const float baseline = centre.y + 40.0f * u;

        juce::GlyphArrangement glyphs;
        glyphs.addLineOfText (font, name, centre.x - width * 0.5f - 10.0f * u, baseline);
        juce::Path text;
        glyphs.createPath (text);

        strokeGlow (g, text, palette::ember.withAlpha (presence), 0.0f, 7.0f * u);
        g.setColour (palette::mist.withAlpha (presence));
        g.fillPath (text);

        g.setColour (palette::haze.withAlpha (presence));
        g.setFont (fonts->monoFont (22.0f * u));
        g.drawText (juce::String (octaveOf (note)), juce::Rectangle<float> (centre.x + width * 0.5f - 4.0f * u, baseline - 26.0f * u, 40.0f * u, 30.0f * u),
                    juce::Justification::centredLeft, false);

        if (vocoderOn)
        {
            const auto tagFont = fonts->monoBoldFont (13.0f * u, 0.14f);
            const auto tag = "MIDI " + juce::String (note);
            const float tagWidth = juce::GlyphArrangement::getStringWidth (tagFont, tag) + 22.0f * u;
            const auto pill = juce::Rectangle<float> (tagWidth, 24.0f * u).withCentre ({ centre.x, centre.y - 62.0f * u });

            g.setColour (palette::ember.withAlpha (0.18f * presence));
            g.fillRoundedRectangle (pill, pill.getHeight() * 0.5f);
            g.setColour (palette::ember.withAlpha (0.75f * presence));
            g.drawRoundedRectangle (pill, pill.getHeight() * 0.5f, 1.0f * u);
            g.setColour (palette::bone.withAlpha (presence));
            g.setFont (tagFont);
            g.drawText (tag, pill, juce::Justification::centred, false);
        }

        const int centsOff = juce::roundToInt ((inMidi - (float) note) * 100.0f);
        g.setColour (palette::bone.withAlpha (0.9f * presence));
        g.setFont (fonts->monoFont (17.0f * u, 0.04f));
        g.drawText ((centsOff > 0 ? "+" : "") + juce::String (centsOff) + " cents",
                    juce::Rectangle<float> (centre.x - 100.0f * u, baseline + 14.0f * u, 200.0f * u, 26.0f * u), juce::Justification::centred, false);
    }
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
    const auto bounds = getLocalBounds().toFloat().reduced (0.5f);

    juce::Path frame;
    frame.addRoundedRectangle (bounds, 10.0f);
    g.setColour (palette::well());
    g.fillPath (frame);

    {
        juce::Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (frame);

        const float labelWidth = 40.0f;
        const auto plot = bounds.withTrimmedLeft (labelWidth).reduced (0.0f, 8.0f);
        const auto yOf = [&] (float midi) { return plot.getBottom() - (midi - low) / (high - low) * plot.getHeight(); };

        // Lignes des notes : celles de la gamme ressortent et portent leur nom
        float lastLabelY = 1.0e6f;
        for (int note = (int) std::ceil (low); note <= (int) std::floor (high); ++note)
        {
            const bool allowed = (noteMask & (1u << (((note % 12) + 12) % 12))) != 0;
            const float y = yOf ((float) note);

            g.setColour (palette::rose.withAlpha (allowed ? 0.15f : 0.04f));
            g.fillRect (juce::Rectangle<float> (bounds.getX() + labelWidth, y - 0.5f, bounds.getWidth() - labelWidth, 1.0f));

            if (allowed && lastLabelY - y >= 11.0f)
            {
                lastLabelY = y;
                g.setColour (palette::haze.withAlpha (0.85f));
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

        g.setColour (palette::bone.withAlpha (0.6f));
        g.strokePath (sung, juce::PathStrokeType (1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        strokeGlow (g, tuned, palette::ember, 2.0f, 2.2f);
        g.setColour (palette::emberHot);
        g.strokePath (tuned, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Le passé s'estompe vers la gauche
        juce::ColourGradient fade (juce::Colour (0xff0b0607).withAlpha (0.85f), plot.getX(), 0.0f,
                                   juce::Colour (0xff0b0607).withAlpha (0.0f), plot.getX() + 150.0f, 0.0f, false);
        g.setGradientFill (fade);
        g.fillRect (plot.withWidth (150.0f).expanded (0.0f, 8.0f));

        // Légende
        const float legendY = bounds.getY() + 13.0f;
        float x = bounds.getRight() - 150.0f;
        g.setFont (fonts->monoFont (9.0f, 0.12f));

        g.setColour (palette::bone.withAlpha (0.6f));
        g.fillRoundedRectangle (x, legendY - 1.0f, 14.0f, 2.0f, 1.0f);
        g.setColour (palette::haze);
        g.drawText ("VOIX", juce::Rectangle<float> (x + 20.0f, legendY - 7.0f, 40.0f, 14.0f), juce::Justification::centredLeft, false);

        x += 68.0f;
        g.setColour (palette::emberHot);
        g.fillRoundedRectangle (x, legendY - 1.0f, 14.0f, 2.0f, 1.0f);
        g.setColour (palette::haze);
        g.drawText ("TUNER", juce::Rectangle<float> (x + 20.0f, legendY - 7.0f, 50.0f, 14.0f), juce::Justification::centredLeft, false);
    }

    g.setColour (palette::rose.withAlpha (0.1f));
    g.strokePath (frame, juce::PathStrokeType (1.0f));
}

} // namespace onirique::ui
