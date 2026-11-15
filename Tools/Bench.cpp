// Outil de développement : banc d'essai d'un correcteur de hauteur, en boîte noire.
// Il charge n'importe quel .vst3 comme un DAW, ouvre son interface dans une fenêtre
// (sur le deuxième écran s'il y en a un, sans prendre le focus), lui fait chanter des
// signaux de synthèse et mesure ce qui ressort : vitesse de correction, dépassement,
// vibrato laissé passer, points de bascule d'une note à l'autre, latence.
// Seule la sortie audio est observée : rien n'est lu dans le binaire du plugin.
//
//   OniriqueTunerBench <fichier.vst3> [dossier du rapport]

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <random>

namespace
{
constexpr double rate = 48000.0;
constexpr int block = 256;
constexpr double twoPi = juce::MathConstants<double>::twoPi;

double midiToHz (double m) { return 440.0 * std::pow (2.0, (m - 69.0) / 12.0); }
double hzToMidi (double f) { return 69.0 + 12.0 * std::log2 (f / 440.0); }

/** Voix de synthèse : 30 harmoniques sous deux formants fixes (700 et 1 200 Hz), un souffle léger. */
std::vector<float> sing (const std::vector<double>& midi)
{
    std::vector<float> out (midi.size());
    std::mt19937 rng (7);
    std::normal_distribution<float> noise (0.0f, 0.004f);
    double phase = 0.0;
    for (size_t i = 0; i < midi.size(); ++i)
    {
        if (midi[i] <= 0.0) { phase = 0.0; continue; }
        const double f0 = midiToHz (midi[i]);
        phase = std::fmod (phase + twoPi * f0 / rate, twoPi * 1000.0);
        double s = 0.0;
        for (int h = 1; h <= 30 && h * f0 < 0.45 * rate; ++h)
        {
            const double f = h * f0;
            const double env = std::exp (-std::pow (std::log2 (f / 700.0) / 0.5, 2.0)) + 0.6 * std::exp (-std::pow (std::log2 (f / 1200.0) / 0.4, 2.0)) + 0.05;
            s += env * std::sin (h * phase) / std::sqrt ((double) h);
        }
        out[i] = (float) (0.12 * s) + noise (rng);
    }
    // fondus de 10 ms à chaque début et fin de son
    const int fade = (int) (0.01 * rate);
    for (size_t i = 0; i < midi.size(); ++i)
    {
        if (midi[i] <= 0.0) continue;
        int distance = fade;
        for (int k = 1; k < fade; ++k)
            if ((i < (size_t) k || midi[i - (size_t) k] <= 0.0) || (i + (size_t) k >= midi.size() || midi[i + (size_t) k] <= 0.0)) { distance = k; break; }
        out[i] *= (float) distance / (float) fade;
    }
    return out;
}

struct Curve { std::vector<double> t, midi; };   // midi < 0 : pas de hauteur

/** YIN hors ligne, fenêtre 1 024, pas de 2,5 ms, 70 Hz – 1 kHz. */
Curve track (const std::vector<float>& x)
{
    Curve c;
    const int W = 1024, tMin = (int) (rate / 1000.0), tMax = (int) (rate / 70.0), hop = 120;
    std::vector<double> d ((size_t) tMax + 2);
    for (int start = 0; start + W + tMax + 1 < (int) x.size(); start += hop)
    {
        double energy = 0.0;
        for (int i = 0; i < W; ++i) energy += (double) x[(size_t) (start + i)] * x[(size_t) (start + i)];
        double pitch = -1.0;
        if (energy / W > 1.0e-5)
        {
            double running = 0.0;
            d[0] = 1.0;
            for (int tau = 1; tau <= tMax; ++tau)
            {
                double sum = 0.0;
                for (int i = 0; i < W; ++i)
                {
                    const double diff = (double) x[(size_t) (start + i)] - x[(size_t) (start + i + tau)];
                    sum += diff * diff;
                }
                running += sum;
                d[(size_t) tau] = running > 0.0 ? sum * tau / running : 1.0;
            }
            int best = -1;
            for (int tau = tMin; tau < tMax; ++tau)
                if (d[(size_t) tau] < 0.15)
                {
                    while (tau + 1 < tMax && d[(size_t) tau + 1] < d[(size_t) tau]) ++tau;
                    best = tau;
                    break;
                }
            if (best > 0)
            {
                const double a = d[(size_t) best - 1], b = d[(size_t) best], cc = d[(size_t) best + 1];
                const double shift = (a - 2.0 * b + cc) != 0.0 ? 0.5 * (a - cc) / (a - 2.0 * b + cc) : 0.0;
                pitch = hzToMidi (rate / (best + shift));
            }
        }
        c.t.push_back ((start + W / 2 + 0.0) / rate);
        c.midi.push_back (pitch);
    }
    return c;
}

double valueAt (const Curve& c, double t)
{
    for (size_t i = 0; i + 1 < c.t.size(); ++i)
        if (c.t[i + 1] >= t) return c.midi[i];
    return -1.0;
}

double median (std::vector<double> v)
{
    v.erase (std::remove_if (v.begin(), v.end(), [] (double a) { return a < 0.0; }), v.end());
    if (v.empty()) return -1.0;
    std::nth_element (v.begin(), v.begin() + (long) v.size() / 2, v.end());
    return v[v.size() / 2];
}

std::vector<double> slice (const Curve& c, double from, double to)
{
    std::vector<double> v;
    for (size_t i = 0; i < c.t.size(); ++i)
        if (c.t[i] >= from && c.t[i] < to) v.push_back (c.midi[i]);
    return v;
}

juce::AudioProcessorParameter* findParam (juce::AudioPluginInstance& p, std::initializer_list<const char*> names)
{
    for (const auto* wanted : names)
        for (auto* q : p.getParameters())
            if (q->getName (64).trim().equalsIgnoreCase (wanted)) return q;
    return nullptr;
}

void pump (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }

// ---------------------------------------------------------------------------
struct Plot : juce::Component
{
    struct Panel { juce::String title; double from = 0, to = 1, low = 55, high = 62; std::vector<std::pair<Curve, juce::Colour>> curves; };
    std::vector<Panel> panels;
    juce::StringArray legend;

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff111015));
        if (panels.empty()) { g.setColour (juce::Colours::white); g.drawText ("Mesure en cours...", getLocalBounds(), juce::Justification::centred); return; }
        auto area = getLocalBounds().reduced (12);
        auto legendArea = area.removeFromBottom (18 * legend.size() + 6);
        const int h = area.getHeight() / (int) panels.size();
        for (auto& p : panels)
        {
            auto r = area.removeFromTop (h).reduced (0, 6);
            g.setColour (juce::Colours::white.withAlpha (0.8f));
            g.drawText (p.title, r.removeFromTop (16), juce::Justification::left);
            g.setColour (juce::Colour (0xff1c1a22)); g.fillRect (r);
            for (int n = (int) std::ceil (p.low); n <= (int) p.high; ++n)
            {
                const float y = (float) juce::jmap ((double) n, p.low, p.high, (double) r.getBottom(), (double) r.getY());
                g.setColour (juce::Colours::white.withAlpha (0.12f)); g.drawHorizontalLine ((int) y, (float) r.getX(), (float) r.getRight());
                g.setColour (juce::Colours::white.withAlpha (0.4f));
                g.drawText (juce::MidiMessage::getMidiNoteName (n, true, true, 4), r.getX() + 2, (int) y - 12, 40, 12, juce::Justification::left);
            }
            for (auto& [c, colour] : p.curves)
            {
                juce::Path path; bool pen = false;
                for (size_t i = 0; i < c.t.size(); ++i)
                {
                    if (c.t[i] < p.from || c.t[i] > p.to || c.midi[i] < 0) { pen = false; continue; }
                    const float x = (float) juce::jmap (c.t[i], p.from, p.to, (double) r.getX(), (double) r.getRight());
                    const float y = (float) juce::jmap (juce::jlimit (p.low, p.high, c.midi[i]), p.low, p.high, (double) r.getBottom(), (double) r.getY());
                    if (pen) path.lineTo (x, y); else path.startNewSubPath (x, y);
                    pen = true;
                }
                g.setColour (colour); g.strokePath (path, juce::PathStrokeType (1.5f));
            }
        }
        for (auto& line : legend)
        {
            g.setColour (juce::Colours::white.withAlpha (0.85f));
            g.drawText (line, legendArea.removeFromTop (18), juce::Justification::left);
        }
    }
};

struct Bench : juce::Component
{
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    Plot plot;
    void resized() override
    {
        auto r = getLocalBounds();
        if (editor) editor->setTopLeftPosition (0, 0), r.removeFromLeft (editor->getWidth());
        plot.setBounds (r);
    }
};

struct Window : juce::DocumentWindow
{
    bool closed = false;
    Window (const juce::String& name) : DocumentWindow (name, juce::Colours::black, allButtons) {}
    void closeButtonPressed() override { closed = true; }
};
} // namespace

int main (int argc, char** argv)
{
    if (argc < 2) { std::cout << "usage : OniriqueTunerBench <fichier.vst3> [dossier du rapport]" << std::endl; return 1; }
    juce::ScopedJuceInitialiser_GUI gui;

    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> found;
    format.findAllTypesForFile (found, juce::String::fromUTF8 (argv[1]));
    if (found.isEmpty()) { std::cout << "Aucun plugin VST3 dans ce fichier." << std::endl; return 1; }

    juce::String error;
    std::unique_ptr<juce::AudioPluginInstance> plugin (format.createInstanceFromDescription (*found[0], rate, block, error));
    if (plugin == nullptr) { std::cout << "Impossible de l'instancier : " << error << std::endl; return 1; }
    plugin->setPlayConfigDetails (2, 2, rate, block);
    plugin->prepareToPlay (rate, block);

    juce::String report;
    const auto say = [&] (const juce::String& line) { report << line << "\n"; std::cout << line.toRawUTF8() << std::endl; };
    say ("Banc d'essai : " + found[0]->name + " (" + found[0]->manufacturerName + ", version " + found[0]->version + ")");
    say ("Reglages exposes a l'hote (" + juce::String (plugin->getParameters().size()) + ") :");
    for (auto* q : plugin->getParameters())
        say ("  " + q->getName (64) + " = " + q->getCurrentValueAsText() + "  [defaut " + q->getText (q->getDefaultValue(), 64) + (q->getNumSteps() < 200 ? ", " + juce::String (q->getNumSteps()) + " pas" : juce::String()) + "]");

    // --- fenêtre : interface du plugin + courbes, sur le deuxième écran, sans focus
    Window window ("Banc d'essai - " + found[0]->name);
    Bench bench;
    if (plugin->hasEditor()) { bench.editor.reset (plugin->createEditorIfNeeded()); if (bench.editor) bench.addAndMakeVisible (*bench.editor); }
    bench.addAndMakeVisible (bench.plot);
    const int ew = bench.editor ? bench.editor->getWidth() : 0, eh = bench.editor ? bench.editor->getHeight() : 0;
    bench.setSize (ew + 720, juce::jmax (eh, 720));
    window.setContentNonOwned (&bench, true);
    window.setUsingNativeTitleBar (true);
    window.setResizable (true, false);
    const auto& displays = juce::Desktop::getInstance().getDisplays();
    auto area = displays.getPrimaryDisplay()->userArea;
    for (auto& d : displays.displays) if (! d.isMain) { area = d.userArea; break; }
    window.setBounds (window.getBounds().withPosition (area.getPosition()).constrainedWithin (area));
    window.addToDesktop();      // SW_SHOWNA : visible sans être activée
    window.setVisible (true);
    pump (800);

    // --- réglages repérés
    auto* speed = findParam (*plugin, { "Retune Speed", "Speed", "Retune", "Correction Speed" });
    auto* humanize = findParam (*plugin, { "Humanize" });
    auto* hard = findParam (*plugin, { "Hard Tune" });
    auto* mix = findParam (*plugin, { "Mix", "Dry/Wet" });
    auto* key = findParam (*plugin, { "Key", "Root", "Root Note" });
    auto* scale = findParam (*plugin, { "Scale", "Scale Type" });
    if (key) key->setValueNotifyingHost (key->getValueForText ("C"));
    if (scale) scale->setValueNotifyingHost (scale->getValueForText ("Major"));
    if (humanize) humanize->setValueNotifyingHost (0.0f);
    if (hard) hard->setValueNotifyingHost (0.0f);
    if (mix) mix->setValueNotifyingHost (1.0f);
    say ("Reglage de vitesse : " + (speed ? speed->getName (64) : juce::String ("introuvable")) + "  |  Key : " + (key ? key->getCurrentValueAsText() : juce::String ("-"))
         + "  |  Scale : " + (scale ? scale->getCurrentValueAsText() : juce::String ("-")) + "  |  Humanize : " + (humanize ? humanize->getCurrentValueAsText() : juce::String ("-")));
    pump (200);

    const auto run = [&] (const std::vector<float>& in)
    {
        plugin->reset();
        std::vector<float> out (in.size(), 0.0f);
        juce::AudioBuffer<float> buf (2, block);
        juce::MidiBuffer midi;
        for (size_t pos = 0; pos + block <= in.size(); pos += block)
        {
            for (int i = 0; i < block; ++i) { buf.setSample (0, i, in[pos + (size_t) i]); buf.setSample (1, i, in[pos + (size_t) i]); }
            midi.clear();
            plugin->processBlock (buf, midi);
            for (int i = 0; i < block; ++i) out[pos + (size_t) i] = 0.5f * (buf.getSample (0, i) + buf.getSample (1, i));
        }
        return out;
    };

    // --- signaux (en secondes) ; gamme de do majeur : la3 = 57, do4 = 60
    const auto build = [] (double seconds, auto&& f) { std::vector<double> m ((size_t) (seconds * rate)); for (size_t i = 0; i < m.size(); ++i) m[i] = f (i / rate); return m; };
    const double jumpAt = 1.0;
    const auto stepIn = build (2.4, [&] (double t) { return t < 0.3 ? 0.0 : t < jumpAt ? 57.40 : 59.65; });        // la3 +40 ct puis do4 -35 ct
    const auto vibIn  = build (2.4, [] (double t) { return t < 0.3 ? 0.0 : 57.0 + 0.40 * std::sin (twoPi * 5.5 * (t - 0.3)) * std::min (1.0, (t - 0.3) / 0.3); });
    const auto glideIn = build (2.6, [] (double t) { return t < 0.3 ? 0.0 : t < 0.6 ? 57.0 : t < 2.1 ? 57.0 + 3.0 * (t - 0.6) / 1.5 : 60.0; });
    // notes brèves (ornements) vers si3 : 40, 80 et 150 ms, au milieu d'un la3 tenu
    const double flickAt[] = { 0.8, 1.3, 1.8 }, flickLen[] = { 0.04, 0.08, 0.15 };
    const auto flickIn = build (2.4, [&] (double t) { if (t < 0.3) return 0.0; for (int k = 0; k < 3; ++k) if (t >= flickAt[k] && t < flickAt[k] + flickLen[k]) return 59.0; return 57.0; });
    const auto stepSig = sing (stepIn), vibSig = sing (vibIn), glideSig = sing (glideIn), flickSig = sing (flickIn);
    const auto stepRef = track (stepSig), vibRef = track (vibSig), glideRef = track (glideSig);

    // latence déclarée et retard de l'attaque (premier instant où la sortie a une hauteur)
    const int latency = plugin->getLatencySamples();
    say ("Latence declaree : " + juce::String (latency) + " echantillons (" + juce::String (1000.0 * latency / rate, 1) + " ms)");
    double lat = latency / rate;

    std::vector<float> settings;
    if (speed) settings = { 0.0f, 0.15f, 0.3f, 0.5f, 0.7f, 0.85f, 1.0f }; else settings = { -1.0f };
    const juce::Colour palette[] = { juce::Colour (0xffff5a36), juce::Colour (0xffffa62b), juce::Colour (0xffe6e14a), juce::Colour (0xff6fdc6f),
                                     juce::Colour (0xff3fc6e0), juce::Colour (0xff6a8cff), juce::Colour (0xffc77dff) };
    Plot::Panel stepPanel { "Saut la3 +40 ct -> do4 -35 ct (gris : entree)", 0.25, 2.3, 56.5, 60.5, { { stepRef, juce::Colours::grey } } };
    Plot::Panel vibPanel { "Vibrato 5,5 Hz +/-40 ct sur la3", 0.25, 2.3, 56.2, 57.8, { { vibRef, juce::Colours::grey } } };
    Plot::Panel glidePanel { "Glisse lente la3 -> do4 en 1,5 s", 0.25, 2.5, 56.5, 60.5, { { glideRef, juce::Colours::grey } } };

    say ("");
    say ("reglage                  | note tenue la3+40 | arrivee sur do4 (<5 ct) | depassement | erreur finale | vibrato garde | bascules glisse (ct / milieu) | notes breves 40/80/150 ms suivies");
    const double vibInDepth = [&] { auto v = slice (vibRef, 1.0, 2.2); double m = median (v), s = 0; int n = 0; for (auto a : v) if (a > 0) s += (a - m) * (a - m), ++n; return n ? std::sqrt (s / n) : 0.0; }();

    const auto measure = [&] (const juce::String& label, juce::Colour colour, bool draw)
    {
        run (std::vector<float> ((size_t) block * 8, 0.0f));   // un hôte n'apprend une nouvelle latence qu'après un traitement
        pump (300);
        lat = plugin->getLatencySamples() / rate;   // décaler la sortie de la latence déclarée
        const auto shifted = [&] (Curve c) { for (auto& t : c.t) t -= lat; return c; };
        const auto stepOut = shifted (track (run (stepSig)));
        const auto vibOut = shifted (track (run (vibSig)));
        const auto glideOut = shifted (track (run (glideSig)));
        const auto flickOut = shifted (track (run (flickSig)));

        const double held = median (slice (stepOut, 0.7, jumpAt));
        double arrive = -1.0, over = 0.0;
        for (size_t i = 0; i < stepOut.t.size(); ++i)
        {
            if (stepOut.t[i] < jumpAt || stepOut.midi[i] < 0) continue;
            over = std::max (over, (stepOut.midi[i] - 60.0) * 100.0);
            if (arrive < 0 && std::abs (stepOut.midi[i] - 60.0) < 0.05) arrive = (stepOut.t[i] - jumpAt) * 1000.0;
        }
        const double final_ = median (slice (stepOut, 2.0, 2.3));
        double t63 = -1.0;   // 63 % du chemin entre la note d'avant (59,25 en sortie) et do4
        for (size_t i = 0; i < stepOut.t.size(); ++i)
            if (stepOut.t[i] >= jumpAt && stepOut.midi[i] >= 59.72 && t63 < 0) t63 = (stepOut.t[i] - jumpAt) * 1000.0;
        juce::String flicks;   // part de chaque note brève reproduite en sortie (100 % = suivie, 0 % = gommée)
        for (int k = 0; k < 3; ++k)
        {
            double top = 57.0;
            for (auto m : slice (flickOut, flickAt[k], flickAt[k] + flickLen[k] + 0.08)) top = std::max (top, m);
            flicks << juce::String (50.0 * (top - 57.0), 0) << "% ";
        }

        auto v = slice (vibOut, 1.0, 2.2); const double vm = median (v); double vs = 0; int vn = 0;
        for (auto a : v) if (a > 0) vs += (a - vm) * (a - vm), ++vn;
        const double vibKept = vn && vibInDepth > 0 ? 100.0 * std::sqrt (vs / vn) / vibInDepth : -1.0;

        // bascules : l'instant où la note de sortie (arrondie) change, comparé à la note d'entrée
        juce::String switches;
        int lastNote = -1;
        for (size_t i = 0; i < glideOut.t.size(); ++i)
        {
            if (glideOut.midi[i] < 0) continue;
            const int n = (int) std::lround (glideOut.midi[i]);
            if (lastNote >= 0 && n != lastNote && std::abs (n - lastNote) == 1)
            {
                const double inNow = valueAt (glideRef, glideOut.t[i]);
                switches << juce::String ((inNow - (std::min (n, lastNote) + 0.5)) * 100.0, 0) << " ";
            }
            lastNote = n;
        }

        say (label.paddedRight (' ', 24) + " | " + juce::String ((held - 57.0) * 100.0, 1).paddedLeft (' ', 9) + " ct      | "
             + (arrive >= 0 ? juce::String (arrive, 0) + " ms" : juce::String ("jamais")).paddedLeft (' ', 12) + " (63 % : " + juce::String (t63, 0) + " ms) | "
             + juce::String (over, 1).paddedLeft (' ', 7) + " ct  | " + juce::String ((final_ - 60.0) * 100.0, 1).paddedLeft (' ', 7) + " ct    | "
             + juce::String (vibKept, 0).paddedLeft (' ', 6) + " %      | " + switches.paddedRight (' ', 18) + " | " + flicks);

        if (draw)
        {
            stepPanel.curves.push_back ({ stepOut, colour });
            vibPanel.curves.push_back ({ vibOut, colour });
            glidePanel.curves.push_back ({ glideOut, colour });
            bench.plot.legend.add (label);
            bench.plot.panels = { stepPanel, vibPanel, glidePanel };
            bench.plot.repaint();
        }
        pump (30);
    };

    for (size_t k = 0; k < settings.size(); ++k)
    {
        if (settings[k] >= 0.0f) speed->setValueNotifyingHost (settings[k]);
        pump (60);
        measure ("vitesse " + (settings[k] >= 0.0f ? speed->getCurrentValueAsText() : juce::String ("(fixe)")), palette[k % 7], true);
    }
    say ("(note tenue : ecart restant apres 0,4 s sur une note fausse de 40 ct ; 0 = corrigee. Vibrato garde : 100 % = intact, 0 % = aplati.)");

    // --- les autres réglages qui changent le caractère de la correction, vitesse par défaut
    if (speed) speed->setValueNotifyingHost (speed->getDefaultValue());
    for (const auto* name : { "Humanize", "Flex-Tune", "Natural Vibrato", "Sustain", "Amount", "Note Stabilizer", "Low Latency", "Mode", "Hard Tune" })
    {
        auto* q = findParam (*plugin, { name });
        if (q == nullptr) continue;
        say ("");
        say (juce::String (name) + " (vitesse " + (speed ? speed->getCurrentValueAsText() : juce::String ("-")) + ") :");
        const int steps = q->getNumSteps() < 8 ? q->getNumSteps() : 3;
        for (int i = 0; i < steps; ++i)
        {
            q->setValueNotifyingHost (steps > 1 ? (float) i / (float) (steps - 1) : 0.0f);
            pump (300);   // le temps qu'une latence nouvelle soit annoncée
            const int l = plugin->getLatencySamples();
            measure ("  " + q->getCurrentValueAsText() + " (lat. " + juce::String (1000.0 * l / rate, 1) + " ms)", {}, false);
        }
        q->setValueNotifyingHost (q->getDefaultValue());
        pump (300);
    }

    if (argc > 2)
    {
        const auto dir = juce::File::getCurrentWorkingDirectory().getChildFile (juce::String::fromUTF8 (argv[2]));
        dir.createDirectory();
        const auto base = juce::File::createLegalFileName (found[0]->name);
        dir.getChildFile (base + ".txt").replaceWithText (report);
        juce::Image shot (juce::Image::RGB, bench.plot.getWidth(), bench.plot.getHeight(), true);
        { juce::Graphics g (shot); bench.plot.paintEntireComponent (g, true); }
        juce::FileOutputStream png (dir.getChildFile (base + ".png"));
        if (png.openedOk()) { png.setPosition (0); png.truncate(); juce::PNGImageFormat().writeImageToStream (shot, png); }
        std::cout << "Rapport ecrit dans " << dir.getFullPathName() << std::endl;
    }

    std::cout << "Fenetre ouverte : fermez-la pour quitter." << std::endl;
    while (! window.closed && ! juce::MessageManager::getInstance()->hasStopMessageBeenSent()) pump (50);

    bench.editor.reset();
    window.clearContentComponent();
    plugin->releaseResources();
    plugin.reset();
    return 0;
}
