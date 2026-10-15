#include "PluginEditor.h"

#include "GUI/Controls.h"
#include "GUI/PitchViews.h"
#include "Parameters.h"

namespace ids = onirique::params::id;
using namespace onirique::ui;

namespace
{
    constexpr float cardRadius = 16.0f;

    const juce::Rectangle<float> sourceCard { 20.0f, 80.0f, 264.0f, 260.0f };
    const juce::Rectangle<float> timbreCard { 716.0f, 80.0f, 264.0f, 170.0f };
    const juce::Rectangle<float> vocoderCard { 716.0f, 260.0f, 264.0f, 80.0f };
    const juce::Rectangle<float> knobCard { 20.0f, 478.0f, 960.0f, 166.0f };

    /** Texte incandescent, comme l'accent "rêve" des titres du site. */
    void drawDreamText (juce::Graphics& g, const juce::Font& font, const juce::String& text, float x, float baseline)
    {
        juce::GlyphArrangement glyphs;
        glyphs.addLineOfText (font, text, x, baseline);
        juce::Path path;
        glyphs.createPath (path);

        juce::DropShadow (palette::ember.withAlpha (0.55f), 26, {}).drawForPath (g, path);
        juce::DropShadow (juce::Colour (0xffffbec4).withAlpha (0.45f), 12, {}).drawForPath (g, path);
        juce::DropShadow (juce::Colours::white.withAlpha (0.5f), 4, {}).drawForPath (g, path);
        g.setColour (palette::mist);
        g.fillPath (path);
    }

    /** Petite pastille d'information (latence déclarée). */
    class Chip final : public juce::Component
    {
    public:
        void setText (const juce::String& newText)
        {
            if (newText != text)
            {
                text = newText;
                repaint();
            }
        }

        void paint (juce::Graphics& g) override
        {
            const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
            g.setColour (palette::well());
            g.fillRoundedRectangle (bounds, bounds.getHeight() * 0.5f);
            g.setColour (palette::line());
            g.drawRoundedRectangle (bounds, bounds.getHeight() * 0.5f, 1.0f);
            g.setColour (palette::haze);
            g.setFont (fonts->monoFont (9.5f, 0.14f));
            g.drawText ("LATENCY", bounds.withTrimmedLeft (14.0f), juce::Justification::centredLeft, false);
            g.setColour (palette::bone);
            g.setFont (fonts->monoFont (11.5f, 0.02f));
            g.drawText (text, bounds.withTrimmedRight (14.0f), juce::Justification::centredRight, false);
        }

    private:
        Fonts fonts;
        juce::String text;
    };
}

//==============================================================================
class OniriqueTunerEditor::Content final : public juce::Component
{
public:
    explicit Content (OniriqueTunerProcessor& p)
        : tuner (p),
          modeSwitch (*p.apvts.getParameter (ids::mode), { "LIVE", "HQ" }),
          power (*p.apvts.getParameter (ids::bypass), "ON", "OFF", true),
          hardTuneButton (*p.apvts.getParameter (ids::hardTune), "HARD TUNE", "HARD TUNE"),
          notes (p.apvts, [&p] { return p.getNoteMask(); }),
          formantButton (*p.apvts.getParameter (ids::formant), "FORMANT", "FORMANT"),
          throat (p.apvts, ids::throat, "Throat", true),
          transpose (p.apvts, ids::transpose, "Transpose", true),
          detune (p.apvts, ids::detune, "Detune", true),
          vocoderButton (*p.apvts.getParameter (ids::vocoder), "VOCODER", "VOCODER"),
          blend (p.apvts, ids::blend, "Blend"),
          tracking (p.apvts, ids::tracking, "Tracking"),
          retune (p.apvts, ids::retune, "Retune Speed"),
          flex (p.apvts, ids::flex, "Flex-Tune"),
          humanize (p.apvts, ids::humanize, "Humanize"),
          vibrato (p.apvts, ids::vibrato, "Natural Vibrato", true),
          mix (p.apvts, ids::mix, "Mix")
    {
        sigil = juce::ImageCache::getFromMemory (BinaryData::sigil_png, BinaryData::sigil_pngSize);

        const auto fill = [&p] (juce::ComboBox& box, const char* parameterId)
        {
            if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (p.apvts.getParameter (parameterId)))
                box.addItemList (choice->choices, 1);
        };

        fill (inputBox, ids::input);
        fill (keyBox, ids::key);
        fill (scaleBox, ids::scale);
        fill (voicingBox, ids::voicing);
        inputAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.apvts, ids::input, inputBox);
        keyAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.apvts, ids::key, keyBox);
        scaleAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.apvts, ids::scale, scaleBox);
        voicingAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.apvts, ids::voicing, voicingBox);

        for (auto* child : std::initializer_list<juce::Component*> { &dial, &trace, &modeSwitch, &latency, &power, &hardTuneButton, &inputBox, &keyBox, &scaleBox,
                                                                      &notes, &formantButton, &throat, &transpose, &detune, &vocoderButton,
                                                                      &voicingBox, &blend, &tracking, &retune, &flex, &humanize, &vibrato, &mix })
            addAndMakeVisible (child);

        formant = p.apvts.getRawParameterValue (ids::formant);
        setSize (baseWidth, baseHeight);
        layout();
        refresh (0.0);
    }

    void refresh (double seconds)
    {
        onirique::tune::DisplayFrame frame;
        while (tuner.popDisplayFrame (frame))
        {
            dial.push (frame);
            trace.push (frame);
        }

        dial.tick (seconds);
        trace.setNoteMask (tuner.getNoteMask());
        trace.tick();
        notes.refresh();

        const double ms = tuner.getLatencyMs();
        latency.setText (ms < 0.05 ? "0 ms" : juce::String (ms, 1) + " ms");

        // La longueur de gorge n'agit qu'avec les formants préservés.
        const bool preserve = formant->load() > 0.5f;
        if (preserve != throat.isEnabled())
        {
            throat.setEnabled (preserve);
            throat.setAlpha (preserve ? 1.0f : 0.4f);
        }

        // Hard Tune impose une note plate : Flex-Tune, Humanize et Natural
        // Vibrato n'ont plus rien à régler.
        const bool natural = ! tuner.isHardTuneOn();
        if (natural != flex.isEnabled())
        {
            for (auto* knob : { &flex, &humanize, &vibrato })
            {
                knob->setEnabled (natural);
                knob->setAlpha (natural ? 1.0f : 0.4f);
            }
        }

        // Les réglages du vocodeur s'éclairent quand il est en marche.
        const bool vocoding = tuner.isVocoderOn();
        dial.setVocoder (vocoding);
        if (vocoding != blend.isEnabled())
        {
            blend.setEnabled (vocoding);
            voicingBox.setEnabled (vocoding);
            blend.setAlpha (vocoding ? 1.0f : 0.45f);
            voicingBox.setAlpha (vocoding ? 1.0f : 0.45f);
        }
    }

    void paint (juce::Graphics& g) override
    {
        // Le fond ne change jamais : on le dessine une fois, à la résolution
        // réelle de l'écran, et on le recolle ensuite.
        const float scale = g.getInternalContext().getPhysicalPixelScaleFactor();
        if (background.isNull() || std::abs (scale - backgroundScale) > 0.01f)
        {
            backgroundScale = scale;
            background = juce::Image (juce::Image::RGB, juce::roundToInt ((float) baseWidth * scale), juce::roundToInt ((float) baseHeight * scale), false);
            juce::Graphics bg (background);
            bg.addTransform (juce::AffineTransform::scale (scale));
            drawBackground (bg);
        }

        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (background, getLocalBounds().toFloat());
    }

    /** HARD TUNE se range dans l'en-tête, à gauche de la pastille de licence
        dont la largeur suit son texte. */
    void placeHardTune (int right)
    {
        hardTuneButton.setBounds (right - 124, 18, 124, 30);
    }

private:
    void layout()
    {
        // En-tête
        power.setBounds (906, 18, 74, 30);
        latency.setBounds (750, 18, 146, 30);
        modeSwitch.setBounds (582, 16, 158, 34);

        // Carte SOURCE
        inputBox.setBounds (36, 118, 232, 32);
        keyBox.setBounds (36, 188, 82, 32);
        scaleBox.setBounds (126, 188, 142, 32);
        notes.setBounds (36, 254, 232, 72);

        // Centre
        dial.setBounds (362, 70, 276, 276);

        // Carte TIMBRE
        formantButton.setBounds (732, 114, 232, 30);
        throat.setBounds (728, 150, 78, 94);
        transpose.setBounds (809, 150, 78, 94);
        detune.setBounds (890, 150, 78, 94);

        // Carte VOCODER
        vocoderButton.setBounds (732, 270, 118, 30);
        voicingBox.setBounds (858, 270, 106, 30);
        blend.setBounds (732, 306, 232, 24);

        trace.setBounds (20, 352, 960, 114);

        // Rangée du bas
        tracking.setBounds (30, 512, 108, 110);
        retune.setBounds (166, 488, 150, 148);
        flex.setBounds (342, 488, 150, 148);
        humanize.setBounds (508, 488, 150, 148);
        vibrato.setBounds (684, 488, 150, 148);
        mix.setBounds (862, 512, 108, 110);
    }

    void drawCard (juce::Graphics& g, juce::Rectangle<float> card) const
    {
        g.setColour (juce::Colours::black.withAlpha (0.3f));
        g.fillRoundedRectangle (card, cardRadius);
        g.setColour (palette::line());
        g.drawRoundedRectangle (card.reduced (0.5f), cardRadius, 1.0f);
    }

    void drawBackground (juce::Graphics& g) const
    {
        const auto bounds = juce::Rectangle<float> ((float) baseWidth, (float) baseHeight);

        // Dégradé sang -> noir
        juce::ColourGradient night (palette::blood, 0.0f, -80.0f, palette::night, 0.0f, (float) baseHeight * 0.92f, false);
        night.addColour (0.42, palette::bloodDeep);
        g.setGradientFill (night);
        g.fillRect (bounds);

        // Lueur de braise, en haut à droite du centre
        juce::ColourGradient glow (palette::ember.withAlpha (0.24f), 640.0f, 150.0f,
                                   palette::ember.withAlpha (0.0f), 640.0f + 470.0f, 150.0f, true);
        g.setGradientFill (glow);
        g.fillRect (bounds);

        // Grain argentique
        {
            juce::Random random (0x0111c);
            juce::Image grain (juce::Image::ARGB, 180, 180, true);
            for (int y = 0; y < 180; ++y)
                for (int x = 0; x < 180; ++x)
                {
                    const auto v = (juce::uint8) random.nextInt (256);
                    grain.setPixelAt (x, y, juce::Colour (v, v, v, (juce::uint8) 14));
                }

            g.setTiledImageFill (grain, 0, 0, 1.0f);
            g.fillRect (bounds);
        }

        // En-tête
        g.setColour (juce::Colour (0xff0b0607).withAlpha (0.35f));
        g.fillRect (0.0f, 0.0f, (float) baseWidth, 66.0f);
        g.setColour (palette::line());
        g.fillRect (0.0f, 66.0f, (float) baseWidth, 1.0f);

        if (sigil.isValid())
        {
            // Le halo est flou par nature : une petite image suffit. Le sigil
            // lui-même est redessiné depuis l'original pour rester net.
            const int logoWidth = 56;
            const int logoHeight = juce::roundToInt ((float) logoWidth * (float) sigil.getHeight() / (float) sigil.getWidth());
            const int pad = 12;

            juce::Image padded (juce::Image::ARGB, logoWidth + 2 * pad, logoHeight + 2 * pad, true);
            {
                juce::Graphics pg (padded);
                pg.drawImageAt (sigil.rescaled (logoWidth, logoHeight, juce::Graphics::highResamplingQuality), pad, pad);
            }

            juce::Graphics::ScopedSaveState state (g);
            g.addTransform (juce::AffineTransform::translation (8.0f, -4.0f));
            juce::DropShadow (palette::ember.withAlpha (0.9f), 7, {}).drawForImage (g, padded);
            g.setOpacity (1.0f);
            g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
            g.drawImage (sigil, juce::Rectangle<float> ((float) pad, (float) pad, (float) logoWidth, (float) logoHeight));
        }

        const auto brandFont = fonts->displayFont (18.0f, 0.16f);
        g.setColour (palette::mist);
        g.setFont (brandFont);
        g.drawText ("ONIRIQUE", juce::Rectangle<float> (84.0f, 20.0f, 260.0f, 28.0f), juce::Justification::centredLeft, false);
        drawDreamText (g, fonts->dreamFont (38.0f), "Tuner", 84.0f + juce::GlyphArrangement::getStringWidth (brandFont, "ONIRIQUE") + 14.0f, 46.0f);

        // Cartes et étiquettes
        drawCard (g, sourceCard);
        drawCard (g, timbreCard);
        drawCard (g, vocoderCard);
        drawCard (g, knobCard);

        drawEyebrow (g, *fonts, "Input type", 36.0f, 102.0f);
        drawEyebrow (g, *fonts, "Key", 36.0f, 172.0f);
        drawEyebrow (g, *fonts, "Scale", 126.0f, 172.0f);
        drawEyebrow (g, *fonts, "Notes", 36.0f, 238.0f);
        drawEyebrow (g, *fonts, "Timbre", 732.0f, 99.0f);
    }

    OniriqueTunerProcessor& tuner;
    Fonts fonts;
    juce::Image sigil, background;
    float backgroundScale = 0.0f;
    std::atomic<float>* formant = nullptr;

    TunerDial dial;
    PitchTrace trace;
    ChoiceSwitch modeSwitch;
    Chip latency;
    LedButton power, hardTuneButton;
    juce::ComboBox inputBox, keyBox, scaleBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> inputAttachment, keyAttachment, scaleAttachment;
    NoteGrid notes;
    LedButton formantButton;
    Knob throat, transpose, detune;
    LedButton vocoderButton;
    juce::ComboBox voicingBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> voicingAttachment;
    Fader blend;
    Knob tracking, retune, flex, humanize, vibrato, mix;
};

//==============================================================================
OniriqueTunerEditor::OniriqueTunerEditor (OniriqueTunerProcessor& p)
    : AudioProcessorEditor (&p),
      tuner (p)
{
    setLookAndFeel (&lookAndFeel);

    content = std::make_unique<Content> (p);
    addAndMakeVisible (*content);

    licenseOverlay = std::make_unique<onirique::license::Overlay> ("ONIRIQUE TUNER", p.license);
    addAndMakeVisible (*licenseOverlay);

    const float scale = p.getEditorScale();
    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio ((double) baseWidth / (double) baseHeight);
    setResizeLimits (baseWidth * 7 / 10, baseHeight * 7 / 10, baseWidth * 2, baseHeight * 2);
    setSize (juce::roundToInt ((float) baseWidth * scale), juce::roundToInt ((float) baseHeight * scale));

    startTimerHz (30);
}

OniriqueTunerEditor::~OniriqueTunerEditor()
{
    stopTimer();
    licenseOverlay.reset();
    content.reset();
    setLookAndFeel (nullptr);
}

void OniriqueTunerEditor::paint (juce::Graphics& g)
{
    g.fillAll (palette::night);
}

void OniriqueTunerEditor::resized()
{
    if (content == nullptr)
        return;

    const float scale = (float) getWidth() / (float) baseWidth;
    content->setTransform (juce::AffineTransform::scale (scale));
    content->setBounds (0, 0, baseWidth, baseHeight);

    if (licenseOverlay != nullptr)
    {
        // Un bouton de l'en-tête, contre LIVE / HQ, à la hauteur de LATENCY et ON ; sa place suit son texte.
        const int width = licenseOverlay->pillWidth();
        licenseOverlay->fit (getLocalBounds(), scale, { 572 - width, 18, width, 30 });
        content->placeHardTune (572 - width - 10);
    }
    else
    {
        content->placeHardTune (572);
    }
    tuner.setEditorScale (scale);
}

void OniriqueTunerEditor::timerCallback()
{
    refreshDisplays (1.0 / 30.0);
}

void OniriqueTunerEditor::refreshDisplays (double seconds)
{
    if (content != nullptr)
        content->refresh (seconds);
}
