#include "PluginEditor.h"

#include "GUI/Controls.h"
#include "GUI/PitchViews.h"
#include "Parameters.h"
#include "GUI/OniriqueWindowSize.h"

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

    /** VIBRATO, dans l'en-tête à côté de HARD TUNE : la LED dit si le vibrato
        créé est en marche, le clic déroule son menu de réglages. */
    class VibratoButton final : public juce::Button
    {
    public:
        explicit VibratoButton (std::atomic<float>* onValue) : juce::Button ("VIBRATO"), on (onValue)
        {
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
            setTooltip ("Vibrato : ajoute un vibrato a la voix accordee, note par note. Clic pour ses reglages.");
        }

        void setCompact (bool shouldBeCompact) { if (shouldBeCompact != compact) { compact = shouldBeCompact; repaint(); } }
        void setOpen (bool isOpen) { if (isOpen != open) { open = isOpen; repaint(); } }
        void refresh() { const bool lit = on->load() > 0.5f; if (lit != shownLit) { shownLit = lit; repaint(); } }

        void paintButton (juce::Graphics& g, bool highlighted, bool) override
        {
            const bool lit = on->load() > 0.5f;
            const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
            const float radius = bounds.getHeight() * 0.5f;

            g.setColour (juce::Colours::black.withAlpha (open ? 0.45f : 0.3f));
            g.fillRoundedRectangle (bounds, radius);
            g.setColour (lit ? palette::ember.withAlpha (0.6f) : ((highlighted || open) ? palette::rose.withAlpha (0.3f) : palette::line()));
            g.drawRoundedRectangle (bounds, radius, 1.0f);

            const auto font = fonts->monoBoldFont (11.5f, 0.08f);
            const juce::String caption = compact ? "VIB" : "VIBRATO";
            const float textWidth = juce::GlyphArrangement::getStringWidth (font, caption);
            const float ledSize = 8.0f, gap = 7.0f, chevron = 7.0f;
            const float startX = bounds.getCentreX() - 0.5f * (ledSize + gap + textWidth + gap + chevron);

            drawLed (g, { startX + ledSize * 0.5f, bounds.getCentreY() }, ledSize, lit);

            const auto colour = (lit || highlighted || open) ? palette::mist : palette::haze;
            g.setColour (colour);
            g.setFont (font);
            g.drawText (caption, juce::Rectangle<float> (startX + ledSize + gap, bounds.getY(), textWidth + 4.0f, bounds.getHeight()),
                        juce::Justification::centredLeft, false);

            // petite flèche : vers le bas, vers le haut quand le menu est ouvert
            const float cx = startX + ledSize + gap + textWidth + gap + chevron * 0.5f, cy = bounds.getCentreY();
            const float d = open ? -1.0f : 1.0f;
            juce::Path arrow;
            arrow.startNewSubPath (cx - 3.5f, cy - 1.8f * d);
            arrow.lineTo (cx, cy + 1.8f * d);
            arrow.lineTo (cx + 3.5f, cy - 1.8f * d);
            g.strokePath (arrow, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

    private:
        Fonts fonts;
        std::atomic<float>* on;
        bool compact = false, open = false, shownLit = false;
    };

    /** Le menu déroulé par VIBRATO : en marche / arrêt, forme, puis vitesse,
        amplitude, délai avant le début du vibrato, montée et variation d'une
        note à l'autre (le « Create Vibrato » d'Auto-Tune). */
    class VibratoPanel final : public juce::Component
    {
    public:
        explicit VibratoPanel (juce::AudioProcessorValueTreeState& state)
            : power (*state.getParameter (ids::vibOn), "ON", "OFF"),
              shape (*state.getParameter (ids::vibShape), { "SINE", "TRIANGLE", "SQUARE", "SAW" }),
              rate (state, ids::vibRate, "Rate"),
              depth (state, ids::vibDepth, "Depth"),
              delay (state, ids::vibDelay, "Delay"),
              onset (state, ids::vibOnset, "Onset"),
              variation (state, ids::vibVariation, "Variation")
        {
            for (auto* child : std::initializer_list<juce::Component*> { &power, &shape, &rate, &depth, &delay, &onset, &variation })
                addAndMakeVisible (child);
            setSize (452, 222);
        }

        void paint (juce::Graphics& g) override
        {
            const auto card = getLocalBounds().toFloat().reduced (6.0f);
            juce::Path outline;
            outline.addRoundedRectangle (card, 16.0f);
            juce::DropShadow (juce::Colours::black.withAlpha (0.7f), 16, { 0, 6 }).drawForPath (g, outline);

            juce::ColourGradient fill (palette::bloodDeep, 0.0f, card.getY(), palette::night, 0.0f, card.getBottom(), false);
            g.setGradientFill (fill);
            g.fillPath (outline);
            g.setColour (palette::ember.withAlpha (0.35f));
            g.strokePath (outline, juce::PathStrokeType (1.0f));

            drawEyebrow (g, *fonts, "Vibrato", card.getX() + 18.0f, card.getY() + 24.0f);
            g.setColour (palette::haze);
            g.setFont (fonts->monoFont (9.5f, 0.06f));
            g.drawText ("ADDED TO THE TUNED VOICE, NOTE BY NOTE", juce::Rectangle<float> (card.getX() + 18.0f, card.getY() + 30.0f, 300.0f, 16.0f),
                        juce::Justification::centredLeft, false);
        }

        void resized() override
        {
            const auto card = getLocalBounds().reduced (6);
            power.setBounds (card.getRight() - 18 - 74, card.getY() + 14, 74, 28);
            shape.setBounds (card.getX() + 16, card.getY() + 56, card.getWidth() - 32, 32);

            const int top = card.getY() + 100, width = (card.getWidth() - 24) / 5;
            int x = card.getX() + 12;
            for (auto* knob : { &rate, &depth, &delay, &onset, &variation })
            {
                knob->setBounds (x, top, width, card.getBottom() - top - 6);
                x += width;
            }
        }

    private:
        Fonts fonts;
        LedButton power;
        ChoiceSwitch shape;
        Knob rate, depth, delay, onset, variation;
    };

    /** Couvre l'interface quand le menu VIBRATO est ouvert : un clic à côté le referme. */
    class ClickCatcher final : public juce::Component
    {
    public:
        std::function<void()> onClick;
        void mouseDown (const juce::MouseEvent&) override { if (onClick) onClick(); }
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
          mix (p.apvts, ids::mix, "Mix"),
          vibratoButton (p.apvts.getRawParameterValue (ids::vibOn)),
          vibratoPanel (p.apvts)
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

        addAndMakeVisible (vibratoButton);
        addChildComponent (catcher);
        addChildComponent (vibratoPanel);
        vibratoButton.onClick = [this] { showVibrato (! vibratoPanel.isVisible()); };
        catcher.onClick = [this] { showVibrato (false); };

        // Fin du nom ONIRIQUE Tuner dans l'en-tête : les boutons ne vont pas plus à gauche.
        brandRight = 84.0f + juce::GlyphArrangement::getStringWidth (fonts->displayFont (18.0f, 0.16f), "ONIRIQUE") + 14.0f
                   + juce::GlyphArrangement::getStringWidth (fonts->dreamFont (38.0f), "Tuner");

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

        vibratoButton.refresh();

        // Hard Tune impose la note, plate, et saute d'une note à l'autre d'un
        // coup : Retune Speed, Flex-Tune, Humanize et Natural Vibrato n'ont plus
        // rien à régler (le vibrato créé, lui, s'ajoute toujours).
        const bool natural = ! tuner.isHardTuneOn();
        if (natural != flex.isEnabled())
        {
            for (auto* knob : { &retune, &flex, &humanize, &vibrato })
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

    /** HARD TUNE puis VIBRATO se rangent dans l'en-tête, à gauche de la pastille
        de licence dont la largeur suit son texte ; VIBRATO se raccourcit s'il
        manque de place devant le nom. */
    void placeHardTune (int right)
    {
        hardTuneButton.setBounds (right - 112, 18, 112, 30);

        int width = 108;
        int x = right - 112 - 8 - width;
        const bool compact = (float) x < brandRight + 12.0f;
        if (compact)
        {
            width = 76;
            x = right - 112 - 8 - width;
        }
        vibratoButton.setCompact (compact);
        vibratoButton.setBounds (x, 18, width, 30);
        if (vibratoPanel.isVisible())
            placeVibratoPanel();
    }

private:
    void showVibrato (bool show)
    {
        catcher.setBounds (getLocalBounds());
        catcher.setVisible (show);
        vibratoPanel.setVisible (show);
        vibratoButton.setOpen (show);
        if (show)
        {
            placeVibratoPanel();
            catcher.toFront (false);
            vibratoPanel.toFront (false);
        }
    }

    void placeVibratoPanel()
    {
        const int x = juce::jlimit (8, baseWidth - 8 - vibratoPanel.getWidth(), vibratoButton.getX() - 6);
        vibratoPanel.setTopLeftPosition (x, vibratoButton.getBottom() + 2);
    }

    void layout()
    {
        // En-tête
        power.setBounds (906, 18, 74, 30);
        latency.setBounds (768, 18, 128, 30);
        modeSwitch.setBounds (620, 16, 140, 34);

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
    VibratoButton vibratoButton;
    ClickCatcher catcher;
    VibratoPanel vibratoPanel;
    float brandRight = 300.0f;
};

//==============================================================================
OniriqueTunerEditor::OniriqueTunerEditor (OniriqueTunerProcessor& p)
    : AudioProcessorEditor (&p),
      tuner (p)
{
    setLookAndFeel (&lookAndFeel);

    content = std::make_unique<Content> (p);
    addAndMakeVisible (*content);

   #if ! ONIRIQUE_TEST_BUILD   // la version d'essai locale n'a pas de licence
    licenseOverlay = std::make_unique<onirique::license::Overlay> ("ONIRIQUE TUNER", p.license);
    addAndMakeVisible (*licenseOverlay);
   #endif

    // taille d'ouverture : la règle commune des plugins ONIRIQUE (GUI/OniriqueWindowSize.h)
    const int w = onirique::window::openingSize (p.apvts.state, "ONIRIQUE TUNER", { baseWidth, baseHeight },
                                                 { baseWidth * 7 / 10, baseHeight * 7 / 10 }, { baseWidth * 2, baseHeight * 2 }).x;
    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio ((double) baseWidth / (double) baseHeight);
    setResizeLimits (baseWidth * 7 / 10, baseHeight * 7 / 10, baseWidth * 2, baseHeight * 2);
    setSize (w, juce::roundToInt ((float) w * baseHeight / baseWidth));
    sizeRestored = true;

    startTimerHz (30);
}

OniriqueTunerEditor::~OniriqueTunerEditor()
{
    stopTimer();
    onirique::window::rememberOnThisComputer ("ONIRIQUE TUNER", getWidth(), getHeight());
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
        licenseOverlay->fit (getLocalBounds(), scale, { 610 - width, 18, width, 30 });
        content->placeHardTune (610 - width - 10);
    }
    else
    {
        content->placeHardTune (610);
    }
    tuner.setEditorScale (scale);
    if (sizeRestored) onirique::window::remember (tuner.apvts.state, getWidth(), getHeight());   // taille gardée avec le projet
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
