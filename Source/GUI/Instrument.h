#pragma once
#include "OniriqueDA.h"     // la DA de VOCALBOX (fond, verre, boutons, témoins)
#include "OniriqueKnob.h"   // le potard B

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
    // un filet de verre (la DA de VOCALBOX : pas de gravure, juste une ligne très légère)
    g.setColour (juce::Colours::white.withAlpha (0.07f));
    g.drawLine (x1, y1, x2, y2, 1.0f);
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
    // le verre liquide de VOCALBOX, et dessous l'affichage sombre
    const float rad = juce::jmin (juce::jmax (radius, 10.0f), r.getHeight() * 0.5f);
    onirique::da::glass (g, r, rad, 1.0f, false);
    g.setColour (juce::Colours::black.withAlpha (0.38f));
    g.fillRoundedRectangle (r.reduced (1.0f), rad);
}

/** Par-dessus le contenu : l'ombre du bord haut, un reflet en biais, le cadre (ombre dedans, lèvre éclairée dehors). */
inline void glassFront (juce::Graphics& g, juce::Rectangle<float> r, float radius = 5.0f)
{
    // le liseré du verre, par-dessus le contenu
    const float rad = juce::jmin (juce::jmax (radius, 10.0f), r.getHeight() * 0.5f);
    g.setColour (onirique::look::emberPink.withAlpha (0.10f));
    g.drawRoundedRectangle (r.reduced (0.5f), rad, 1.0f);
}

/** Touche : son logement creusé dans la façade, puis le capuchon de métal sombre éclairé d'en haut ; enfoncée, la
    lumière s'inverse (le capuchon est sous le bord du logement). */
inline void keyCap (juce::Graphics& g, juce::Rectangle<float> r, bool down, bool hover, float radius = 4.0f)
{
    // le bouton de VOCALBOX : verre, et allumé (choisi / enfoncé) le dégradé de la couleur de l'interface
    juce::ignoreUnused (radius);
    const float rad = juce::jmin (r.getHeight() * 0.5f, 16.0f);
    onirique::da::glass (g, r, rad, 1.0f, false);
    onirique::da::seg (g, r.reduced (2.5f), down ? 1.0f : 0.0f, hover ? 1.0f : 0.0f, 1.0f, juce::jmax (0.0f, rad - 2.5f));
}

/** Témoin : la lentille éteinte, la braise qui s'allume et sa lueur, le reflet. */
inline void led (juce::Graphics& g, juce::Point<float> c, float level, float r = 2.8f)
{
    // le témoin doux de VOCALBOX
    onirique::da::led (g, c, r * 2.2f, juce::jlimit (0.0f, 1.0f, level));
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
    // le potard B de VOCALBOX (graduations, chapeau mat, index blanc)
    onirique::ui::drawKnob (g, area, pos, start, end, onirique::look::ember, bipolar, hot);
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
    // le fond de VOCALBOX : nuit, et trois taches floues de la couleur de l'interface
    juce::ignoreUnused (brushed);
    g.setColour (onirique::da::night);
    g.fillRect (b);
    const float u = b.getWidth() / 1100.0f, W = b.getWidth() / u, H = b.getHeight() / u;
    auto blob = [&] (float x, float y, float w, float h, juce::Colour c)
    {
        const float cx = b.getX() + (x + w * 0.5f) * u, cy = b.getY() + (y + h * 0.5f) * u, rx = (w * 0.5f + 90.0f) * u, ry = (h * 0.5f + 90.0f) * u;
        juce::ColourGradient gr (c, cx, cy, c.withAlpha (0.0f), cx + rx, cy, true);
        gr.addColour (0.55, c.withMultipliedAlpha (0.55f));
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (b.toNearestInt());
        g.addTransform (juce::AffineTransform::scale (1.0f, ry / rx, cx, cy));
        g.setGradientFill (gr);
        g.fillEllipse (cx - rx, cy - rx, 2.0f * rx, 2.0f * rx);
    };
    blob (-160.0f, -260.0f, 0.45f * W, 0.55f * H, onirique::look::emberBlood.withAlpha (0.55f));
    blob (0.6f * W, 0.62f * H, 0.4f * W, 0.55f * H, onirique::look::emberDeep.withAlpha (0.45f));
    blob (0.4f * W, 0.3f * H, 0.26f * W, 0.34f * H, onirique::look::emberLit.withAlpha (0.10f));
}

} // namespace onirique::ui
