#include "PluginEditor.h"

#include "GUI/Controls.h"
#include "GUI/Instrument.h"
#include "GUI/PitchViews.h"
#include "Parameters.h"
#include "GUI/OniriqueWindowSize.h"

namespace ids = onirique::params::id;
using namespace onirique::ui;

namespace
{
    /** Petit affichage de la latence déclarée. */
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
            const auto bounds = getLocalBounds().toFloat().reduced (2.0f);
            glassBack (g, bounds, 4.0f);
            g.setColour (pal::rose.withAlpha (0.4f));
            g.setFont (fonts->monoFont (8.5f, 0.18f));
            g.drawText ("LATENCY", bounds.withTrimmedLeft (10.0f), juce::Justification::centredLeft, false);
            g.setColour (pal::ember.brighter (0.15f));
            g.setFont (fonts->monoBoldFont (12.0f, 0.02f));
            g.drawText (text, bounds.withTrimmedRight (10.0f), juce::Justification::centredRight, false);
            glassFront (g, bounds, 4.0f);
        }

    private:
        Fonts fonts;
        juce::String text;
    };

    /** VIBRATO, dans l'en-tête à côté de HARD TUNE : le témoin dit si le vibrato créé est en marche, le clic
        déroule son panneau de réglages. */
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

        void paintButton (juce::Graphics& g, bool highlighted, bool down) override
        {
            const bool lit = on->load() > 0.5f;
            const auto key = getLocalBounds().toFloat().reduced (2.0f);
            keyCap (g, key, down || open, highlighted);

            const auto font = fonts->bodyFont (10.5f, 0.2f);
            const juce::String caption = compact ? "VIB" : "VIBRATO";
            const float textWidth = juce::GlyphArrangement::getStringWidth (font, caption);
            const float startX = key.getCentreX() - 0.5f * (6.0f + 9.0f + textWidth + 8.0f + 7.0f);

            led (g, { startX + 3.0f, key.getCentreY() }, lit ? 1.0f : 0.0f);
            g.setColour (lit || open ? pal::bone : (highlighted ? pal::ink : pal::inkDim));
            g.setFont (font);
            g.drawText (caption, juce::Rectangle<float> (startX + 15.0f, key.getY(), textWidth + 4.0f, key.getHeight()), juce::Justification::centredLeft, false);

            // petite flèche : vers le bas, vers le haut quand le panneau est ouvert
            const float cx = startX + 15.0f + textWidth + 8.0f + 3.5f, cy = key.getCentreY();
            const float d = open ? -1.0f : 1.0f;
            juce::Path arrow;
            arrow.startNewSubPath (cx - 3.5f, cy - 1.8f * d);
            arrow.lineTo (cx, cy + 1.8f * d);
            arrow.lineTo (cx + 3.5f, cy - 1.8f * d);
            g.setColour (pal::ink.withAlpha (0.6f));
            g.strokePath (arrow, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

    private:
        Fonts fonts;
        std::atomic<float>* on;
        bool compact = false, open = false, shownLit = false;
    };

    /** Le panneau déroulé par VIBRATO : en marche / arrêt, forme, puis vitesse, amplitude, délai avant le début
        du vibrato, montée et variation d'une note à l'autre (le « Create Vibrato » d'Auto-Tune). */
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
            brushed = brushedMetal();
            setSize (452, 236);
        }

        void paint (juce::Graphics& g) override
        {
            // une plaque de métal posée par-dessus la façade, avec son ombre
            const auto plate = getLocalBounds().toFloat().reduced (8.0f);
            juce::Path outline;
            outline.addRoundedRectangle (plate, 6.0f);
            juce::DropShadow (juce::Colours::black.withAlpha (0.8f), 18, { 0, 8 }).drawForPath (g, outline);
            {
                juce::Graphics::ScopedSaveState save (g);
                g.reduceClipRegion (outline);
                drawFaceplate (g, plate, brushed);
            }
            g.setColour (juce::Colours::black);
            g.strokePath (outline, juce::PathStrokeType (1.0f));
            g.setColour (juce::Colours::white.withAlpha (0.08f));
            g.drawLine (plate.getX() + 6.0f, plate.getY() + 1.0f, plate.getRight() - 6.0f, plate.getY() + 1.0f, 1.0f);

            sectionTitle (g, "VIBRATO", plate.getX() + 16.0f, plate.getY() + 22.0f, plate.getRight() - 104.0f);
            g.setColour (pal::inkDim);
            g.setFont (fonts->monoFont (9.0f, 0.06f));
            g.drawText ("ADDED TO THE TUNED VOICE, NOTE BY NOTE", juce::Rectangle<float> (plate.getX() + 16.0f, plate.getY() + 32.0f, 300.0f, 14.0f),
                        juce::Justification::centredLeft, false);
        }

        void resized() override
        {
            const auto plate = getLocalBounds().reduced (8);
            power.setBounds (plate.getRight() - 16 - 76, plate.getY() + 10, 76, 30);
            shape.setBounds (plate.getX() + 14, plate.getY() + 54, plate.getWidth() - 28, 30);

            const int top = plate.getY() + 98, width = (plate.getWidth() - 24) / 5;
            int x = plate.getX() + 12;
            for (auto* knob : { &rate, &depth, &delay, &onset, &variation })
            {
                knob->setBounds (x, top, width, plate.getBottom() - top - 8);
                x += width;
            }
        }

    private:
        Fonts fonts;
        juce::Image brushed;
        LedButton power;
        ChoiceSwitch shape;
        Knob rate, depth, delay, onset, variation;
    };

    /** Couvre l'interface quand le panneau VIBRATO est ouvert : un clic à côté le referme. */
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
          throat (p.apvts, ids::throat, "Throat"),
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
        brushed = brushedMetal();

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

        // Fin du nom ONIRIQUE Tuner dans l'en-tête : les touches ne vont pas plus à gauche.
        brandRight = brandX + juce::GlyphArrangement::getStringWidth (fonts->displayFont (15.0f, 0.14f), "ONIRIQUE") + 10.0f
                   + juce::GlyphArrangement::getStringWidth (fonts->dreamFont (33.0f), "Tuner");

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
        // La façade ne change jamais : on la dessine une fois, à la résolution
        // réelle de l'écran, et on la recolle ensuite.
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
        hardTuneButton.setBounds (right - 116, 17, 116, 32);

        int width = 112;
        int x = right - 116 - 8 - width;
        const bool compact = (float) x < brandRight + 12.0f;
        if (compact)
        {
            width = 80;
            x = right - 116 - 8 - width;
        }
        vibratoButton.setCompact (compact);
        vibratoButton.setBounds (x, 17, width, 32);
        if (vibratoPanel.isVisible())
            placeVibratoPanel();
    }

private:
    static constexpr float brandX = 58.0f;

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
        const int x = juce::jlimit (4, baseWidth - 4 - vibratoPanel.getWidth(), vibratoButton.getX() - 8);
        vibratoPanel.setTopLeftPosition (x, vibratoButton.getBottom() - 2);
    }

    void layout()
    {
        // En-tête
        power.setBounds (906, 17, 76, 32);
        latency.setBounds (768, 17, 130, 32);
        modeSwitch.setBounds (620, 17, 140, 32);

        // SOURCE
        inputBox.setBounds (34, 110, 236, 32);
        keyBox.setBounds (34, 180, 84, 32);
        scaleBox.setBounds (126, 180, 144, 32);
        notes.setBounds (34, 250, 236, 84);

        // Centre
        dial.setBounds (362, 72, 276, 276);

        // TIMBRE
        formantButton.setBounds (730, 106, 236, 32);
        throat.setBounds (728, 146, 78, 98);
        transpose.setBounds (809, 146, 78, 98);
        detune.setBounds (890, 146, 78, 98);

        // VOCODER
        vocoderButton.setBounds (730, 268, 120, 32);
        voicingBox.setBounds (858, 268, 108, 32);
        blend.setBounds (732, 310, 234, 26);

        trace.setBounds (18, 356, 964, 112);

        // Rangée du bas
        tracking.setBounds (30, 500, 108, 120);
        retune.setBounds (166, 482, 150, 156);
        flex.setBounds (342, 482, 150, 156);
        humanize.setBounds (508, 482, 150, 156);
        vibrato.setBounds (684, 482, 150, 156);
        mix.setBounds (862, 500, 108, 120);
    }

    void drawBackground (juce::Graphics& g) const
    {
        const auto bounds = juce::Rectangle<float> ((float) baseWidth, (float) baseHeight);
        drawFaceplate (g, bounds, brushed);

        // la marque : le sigil, ONIRIQUE sérigraphié, « Tuner » en gothique couleur braise
        g.setOpacity (1.0f);
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (sigil, juce::Rectangle<float> (18.0f, 17.0f, 30.0f, 30.0f), juce::RectanglePlacement::centred);
        const auto brandFont = fonts->displayFont (15.0f, 0.14f);
        g.setColour (pal::ink.withAlpha (0.92f));
        g.setFont (brandFont);
        g.drawText ("ONIRIQUE", juce::Rectangle<float> (brandX, 18.0f, 220.0f, 30.0f), juce::Justification::centredLeft, false);
        g.setColour (pal::ember);
        g.setFont (fonts->dreamFont (33.0f));
        g.drawText ("Tuner", juce::Rectangle<float> (brandX + juce::GlyphArrangement::getStringWidth (brandFont, "ONIRIQUE") + 10.0f, 10.0f, 140.0f, 40.0f),
                    juce::Justification::centredLeft, false);

        groove (g, 0.0f, 64.0f, bounds.getRight(), 64.0f);

        // SOURCE, à gauche
        sectionTitle (g, "INPUT TYPE", 34.0f, 92.0f, 270.0f);
        sectionTitle (g, "KEY", 34.0f, 164.0f, 118.0f);
        sectionTitle (g, "SCALE", 126.0f, 164.0f, 270.0f);
        sectionTitle (g, "NOTES", 34.0f, 234.0f, 270.0f);
        groove (g, 300.0f, 76.0f, 300.0f, 340.0f);

        // TIMBRE et VOCODER, à droite
        groove (g, 700.0f, 76.0f, 700.0f, 340.0f);
        sectionTitle (g, "TIMBRE", 730.0f, 92.0f, 966.0f);
        sectionTitle (g, "VOCODER", 730.0f, 254.0f, 966.0f);

        groove (g, 18.0f, 476.0f, 982.0f, 476.0f);
    }

    OniriqueTunerProcessor& tuner;
    Fonts fonts;
    juce::Image sigil, brushed, background;
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
    g.fillAll (pal::plateBottom);
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
