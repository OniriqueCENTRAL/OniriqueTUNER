#pragma once

#include "Theme.h"

//==============================================================================
//  Les pièces de l'instrument : façade, rainures, sérigraphie, fenêtres de verre, touches, témoins, potards.
//  Tout se dessine à la taille de base de l'interface ; l'éditeur met le tout à l'échelle.
//==============================================================================
namespace onirique::ui
{
namespace pal = palette;

inline constexpr float pi = juce::MathConstants<float>::pi, twoPi = juce::MathConstants<float>::twoPi;

/** Point sur un cercle ; angles de JUCE : 0 en haut, sens horaire. */
inline juce::Point<float> polar (juce::Point<float> c, float angle, float r) noexcept
{
    return { c.x + r * std::sin (angle), c.y - r * std::cos (angle) };
}

inline juce::Rectangle<float> circle (juce::Point<float> c, float r) noexcept { return juce::Rectangle<float> (2.0f * r, 2.0f * r).withCentre (c); }

inline bool approach (float& v, float target, float k) noexcept
{
    if (std::abs (target - v) < 1.0e-3f) { const bool moved = v != target; v = target; return moved; }
    v += k * (target - v);
    return true;
}

/** Texte sérigraphié sur la façade. */
inline void ink (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r, juce::Justification j, float size = 10.5f, float alpha = 0.72f)
{
    g.setColour (pal::ink.withAlpha (alpha));
    g.setFont (Fonts()->bodyFont (size, 0.18f));
    g.drawText (text, r, j, false);
}

/** Rainure gravée dans le métal : un trait d'ombre, puis la lèvre éclairée juste dessous (ou à droite). */
inline void groove (juce::Graphics& g, float x1, float y1, float x2, float y2)
{
    const bool horizontal = std::abs (y2 - y1) < 0.5f;
    g.setColour (juce::Colours::black.withAlpha (0.75f));
    g.drawLine (x1, y1, x2, y2, 1.0f);
    g.setColour (juce::Colours::white.withAlpha (0.065f));
    g.drawLine (x1 + (horizontal ? 0.0f : 1.0f), y1 + (horizontal ? 1.0f : 0.0f), x2 + (horizontal ? 0.0f : 1.0f), y2 + (horizontal ? 1.0f : 0.0f), 1.0f);
}

/** Titre de section : le nom, puis une rainure jusqu'au bout de la section. */
inline void sectionTitle (juce::Graphics& g, const juce::String& text, float x, float y, float right)
{
    const auto font = Fonts()->bodyFont (10.5f, 0.26f);
    ink (g, text, { x, y - 8.0f, 300.0f, 16.0f }, juce::Justification::centredLeft, 10.5f, 0.62f);
    groove (g, x + juce::GlyphArrangement::getStringWidth (font, text) + 12.0f, y, right, y);
}

/** Fenêtre d'affichage : le verre fumé enchâssé (le fond ; le contenu se dessine ensuite, puis glassFront). */
inline void glassBack (juce::Graphics& g, juce::Rectangle<float> r, float radius = 5.0f)
{
    g.setColour (pal::glass);
    g.fillRoundedRectangle (r, radius);
}

/** Par-dessus le contenu : l'ombre du bord haut, un reflet en biais, le cadre (ombre dedans, lèvre éclairée dehors). */
inline void glassFront (juce::Graphics& g, juce::Rectangle<float> r, float radius = 5.0f)
{
    {
        juce::Graphics::ScopedSaveState save (g);
        juce::Path clip;
        clip.addRoundedRectangle (r, radius);
        g.reduceClipRegion (clip);
        g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.6f), 0.0f, r.getY(),
                                                 juce::Colours::transparentBlack, 0.0f, r.getY() + juce::jmin (16.0f, r.getHeight() * 0.3f), false));
        g.fillRect (r);
        juce::ColourGradient sheen (juce::Colours::white.withAlpha (0.0f), r.getX(), r.getY(),
                                    juce::Colours::white.withAlpha (0.0f), r.getX() + r.getHeight() * 1.4f, r.getBottom(), false);
        sheen.addColour (0.42, juce::Colours::white.withAlpha (0.0f));
        sheen.addColour (0.5, juce::Colours::white.withAlpha (0.028f));
        sheen.addColour (0.62, juce::Colours::white.withAlpha (0.0f));
        g.setGradientFill (sheen);
        g.fillRect (r);
    }
    g.setColour (juce::Colours::black.withAlpha (0.85f));
    g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
    juce::Path lip;
    lip.addRoundedRectangle (r.expanded (1.0f), radius + 1.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.0f), 0.0f, r.getY(),
                                             juce::Colours::white.withAlpha (0.1f), 0.0f, r.getBottom() + 1.0f, false));
    g.strokePath (lip, juce::PathStrokeType (1.0f));
}

/** Touche : son logement creusé dans la façade, puis le capuchon de métal sombre éclairé d'en haut ; enfoncée, la
    lumière s'inverse (le capuchon est sous le bord du logement). */
inline void keyCap (juce::Graphics& g, juce::Rectangle<float> r, bool down, bool hover, float radius = 4.0f)
{
    g.setColour (juce::Colours::black.withAlpha (0.7f));
    g.fillRoundedRectangle (r.expanded (2.0f), radius + 2.0f);
    g.setColour (juce::Colours::white.withAlpha (0.05f));
    g.drawRoundedRectangle (r.expanded (2.5f), radius + 2.5f, 0.8f);
    const auto cap = down ? r.reduced (0.5f).translated (0.0f, 0.5f) : r;
    const auto top = juce::Colour (0xff2c2423).brighter (hover ? 0.12f : 0.0f), bottom = juce::Colour (0xff141010).brighter (hover ? 0.12f : 0.0f);
    g.setGradientFill (juce::ColourGradient (down ? bottom : top, 0.0f, cap.getY(), down ? top.darker (0.2f) : bottom, 0.0f, cap.getBottom(), false));
    g.fillRoundedRectangle (cap, radius);
    if (! down)
    {
        g.setColour (juce::Colours::white.withAlpha (0.11f));
        g.drawLine (cap.getX() + radius, cap.getY() + 0.8f, cap.getRight() - radius, cap.getY() + 0.8f, 1.0f);
    }
    g.setColour (juce::Colours::black.withAlpha (0.8f));
    g.drawRoundedRectangle (cap, radius, 1.0f);
}

/** Témoin : la lentille éteinte, la braise qui s'allume et sa lueur, le reflet. */
inline void led (juce::Graphics& g, juce::Point<float> c, float level, float r = 2.8f)
{
    g.setColour (juce::Colours::black.withAlpha (0.8f));
    g.fillEllipse (circle (c, r + 1.0f));
    if (level > 0.02f)
    {
        g.setGradientFill (juce::ColourGradient (pal::ember.withAlpha (0.4f * level), c.x, c.y, pal::ember.withAlpha (0.0f), c.x + r * 4.5f, c.y, true));
        g.fillEllipse (circle (c, r * 4.5f));
    }
    g.setColour (pal::ledOff.interpolatedWith (pal::emberHot, level));
    g.fillEllipse (circle (c, r));
    g.setColour (juce::Colours::white.withAlpha (0.18f + 0.5f * level));
    g.fillEllipse (circle (c.translated (-r * 0.35f, -r * 0.35f), r * 0.32f));
}

/** Petit triangle de menu. */
inline void menuArrow (juce::Graphics& g, float x, float y, juce::Colour c)
{
    juce::Path tri;
    tri.addTriangle (x, y - 2.0f, x + 7.0f, y - 2.0f, x + 3.5f, y + 2.5f);
    g.setColour (c);
    g.fillPath (tri);
}

/** Le potard : un bouton d'aluminium noir usiné posé sur la façade.
    - une échelle sérigraphiée, et dans la piste gravée la braise qui va de l'origine (le milieu pour un réglage
      bipolaire) à la valeur ;
    - une jupe moletée, éclairée d'en haut ;
    - un chapeau tourné (reflets en croix du métal usiné), et l'index gravé qui va jusque sur la jupe. */
inline void drawInstrumentKnob (juce::Graphics& g, juce::Rectangle<float> area, float pos, float start, float end, bool hot, bool bipolar)
{
    const float R = juce::jmin (area.getWidth(), area.getHeight()) * 0.5f - 1.0f;
    if (R < 8.0f) return;
    const auto c = area.getCentre();
    const float angle = start + juce::jlimit (0.0f, 1.0f, pos) * (end - start);
    const float origin = bipolar ? 0.5f * (start + end) : start;
    const bool big = R > 55.0f;

    // l'échelle
    const int count = big ? 40 : 20;
    for (int i = 0; i <= count; ++i)
    {
        const float a = start + (float) i / (float) count * (end - start);
        const bool major = i % 5 == 0 || (bipolar && i == count / 2);
        g.setColour (pal::ink.withAlpha (major ? 0.5f : 0.2f));
        g.drawLine (juce::Line<float> (polar (c, a, R - R * (major ? 0.11f : 0.07f)), polar (c, a, R)), major ? 1.3f : 1.0f);
    }

    // la piste et la valeur
    const float rt = R * 0.81f, tw = juce::jmax (2.2f, R * 0.045f);
    juce::Path track;
    track.addCentredArc (c.x, c.y, rt, rt, 0.0f, start, end, true);
    g.setColour (juce::Colours::black.withAlpha (0.65f));
    g.strokePath (track, juce::PathStrokeType (tw + 1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    if (std::abs (angle - origin) > 1.0e-3f)
    {
        juce::Path value;
        value.addCentredArc (c.x, c.y, rt, rt, 0.0f, juce::jmin (origin, angle), juce::jmax (origin, angle), true);
        g.setColour (pal::ember.withAlpha (hot ? 0.3f : 0.16f));
        g.strokePath (value, juce::PathStrokeType (tw + 4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (hot ? pal::emberHot : pal::ember);
        g.strokePath (value, juce::PathStrokeType (tw, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // l'ombre portée du bouton
    const float rk = R * 0.66f;
    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.7f), c.x, c.y + rk * 0.2f,
                                             juce::Colours::transparentBlack, c.x + rk * 1.35f, c.y + rk * 0.2f, true));
    g.fillEllipse (circle (c.translated (0.0f, rk * 0.2f), rk * 1.35f));

    // la jupe moletée
    g.setGradientFill (juce::ColourGradient (pal::metalHi.brighter (0.15f), c.x, c.y - rk, pal::metalLo, c.x, c.y + rk, false));
    g.fillEllipse (circle (c, rk));
    const int ridges = big ? 90 : 56;
    for (int j = 0; j < ridges; ++j)
    {
        const float a = (float) j / (float) ridges * twoPi;
        const float light = juce::jmax (0.0f, -std::cos (a - 0.35f));
        g.setColour (juce::Colours::white.withAlpha (0.02f + 0.11f * light * light));
        g.drawLine (juce::Line<float> (polar (c, a, rk * 0.86f), polar (c, a, rk - 0.7f)), 1.0f);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.drawLine (juce::Line<float> (polar (c, a + pi / (float) ridges, rk * 0.86f), polar (c, a + pi / (float) ridges, rk - 0.7f)), 1.0f);
    }
    g.setColour (juce::Colours::black.withAlpha (0.8f));
    g.drawEllipse (circle (c, rk), 1.0f);

    // le chapeau tourné
    const float rc = rk * 0.8f;
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff120e0e), c.x, c.y - rc, juce::Colour (0xff2c2422), c.x, c.y + rc, false));
    g.fillEllipse (circle (c, rc));
    {
        juce::Graphics::ScopedSaveState save (g);
        juce::Path clip;
        clip.addEllipse (circle (c, rc));
        g.reduceClipRegion (clip);
        const int wedges = 72;
        for (int s = 0; s < wedges; ++s)
        {
            const float a0 = (float) s / (float) wedges * twoPi, a1 = a0 + twoPi / (float) wedges + 0.01f;
            const float sheen = std::pow (std::abs (std::cos (a0 + 0.5f * twoPi / (float) wedges - 0.75f)), 7.0f);
            if (sheen < 0.02f) continue;
            juce::Path wedge;
            wedge.addPieSegment (circle (c, rc), a0, a1, 0.0f);
            g.setColour (juce::Colours::white.withAlpha (0.11f * sheen));
            g.fillPath (wedge);
        }
        for (float rr = rc * 0.12f; rr < rc; rr += juce::jmax (1.6f, rc / 20.0f))
        {
            g.setColour (juce::Colours::black.withAlpha (0.08f));
            g.drawEllipse (circle (c, rr), 0.7f);
        }
    }
    juce::Path rim;
    rim.addEllipse (circle (c, rc));
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.24f), c.x, c.y - rc, juce::Colours::black.withAlpha (0.4f), c.x, c.y + rc, false));
    g.strokePath (rim, juce::PathStrokeType (1.1f));

    // l'index gravé, prolongé sur la jupe
    const float iw = juce::jmax (1.7f, R * 0.034f);
    const auto across = polar ({}, angle + pi * 0.5f, 1.0f);
    const auto p0 = polar (c, angle, rc * 0.2f), p1 = polar (c, angle, rc * 0.9f);
    g.setColour (juce::Colours::black.withAlpha (0.75f));
    g.drawLine (juce::Line<float> (p0 + across, p1 + across), iw);
    g.setColour (pal::bone.withAlpha (0.95f));
    g.drawLine (juce::Line<float> (p0, p1), iw);
    g.setColour (pal::bone.withAlpha (0.6f));
    g.drawLine (juce::Line<float> (polar (c, angle, rk * 0.88f), polar (c, angle, rk * 0.99f)), juce::jmax (1.3f, iw * 0.8f));
}

/** Le métal brossé : des stries horizontales, chaque rangée un peu plus claire ou plus sombre (tuile à répéter). */
inline juce::Image brushedMetal()
{
    juce::Image tile (juce::Image::ARGB, 256, 256, true);
    juce::Random r (11);
    for (int y = 0; y < tile.getHeight(); ++y)
    {
        const float row = r.nextFloat() * 2.0f - 1.0f;
        for (int x = 0; x < tile.getWidth(); ++x)
        {
            const float v = 0.75f * row + 0.25f * (r.nextFloat() * 2.0f - 1.0f);
            tile.setPixelAt (x, y, (v > 0.0f ? juce::Colours::white : juce::Colours::black).withAlpha (std::abs (v) * 0.03f));
        }
    }
    return tile;
}

/** La façade : métal anodisé éclairé d'en haut, brossé, assombri sur les bords. */
inline void drawFaceplate (juce::Graphics& g, juce::Rectangle<float> b, const juce::Image& brushed)
{
    g.setGradientFill (juce::ColourGradient (pal::plateTop, 0.0f, b.getY(), pal::plateBottom, 0.0f, b.getBottom(), false));
    g.fillRect (b);
    g.setTiledImageFill (brushed, 0, 0, 1.0f);
    g.fillRect (b);
    juce::ColourGradient v (juce::Colours::transparentBlack, b.getCentreX(), b.getCentreY(), juce::Colours::black.withAlpha (0.32f), b.getX(), b.getY(), true);
    v.addColour (0.55, juce::Colours::transparentBlack);
    g.setGradientFill (v);
    g.fillRect (b);
    g.setColour (juce::Colours::white.withAlpha (0.07f));
    g.drawHorizontalLine ((int) b.getY(), b.getX(), b.getRight());
}

} // namespace onirique::ui
