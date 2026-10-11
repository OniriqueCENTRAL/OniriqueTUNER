// Outil de développement : charge le fichier .vst3 comme le ferait un DAW
// (hôte VST3 de JUCE), puis vérifie ce qu'un hôte attend de lui.
//
//   OniriqueTunerHostTest "chemin/vers/ONIRIQUE TUNER.vst3"

#include <juce_audio_processors/juce_audio_processors.h>

#include <cmath>
#include <iostream>
#include <random>

namespace
{
    int failures = 0;
    double voicePhase = 0.0;

    void check (bool ok, const juce::String& what)
    {
        std::cout << (ok ? "  ok     " : "  ECHEC  ") << what << std::endl;
        if (! ok)
            ++failures;
    }

    juce::AudioProcessorParameter* find (juce::AudioPluginInstance& plugin, const juce::String& name)
    {
        for (auto* parameter : plugin.getParameters())
            if (parameter->getName (64) == name)
                return parameter;

        return nullptr;
    }

    void pump (int milliseconds)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (milliseconds);
    }

    /** Envoie un bruit (que le plugin doit laisser intact) et mesure de
        combien d'échantillons il ressort retardé. */
    int measureDelay (juce::AudioPluginInstance& plugin, int blockSize)
    {
        std::mt19937 rng (5);
        std::normal_distribution<float> gauss (0.0f, 0.1f);

        const int total = 32768;
        std::vector<float> in ((size_t) total), out ((size_t) total);
        for (auto& v : in)
            v = gauss (rng);

        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;
        for (int pos = 0; pos + blockSize <= total; pos += blockSize)
        {
            for (int i = 0; i < blockSize; ++i)
            {
                buffer.setSample (0, i, in[(size_t) (pos + i)]);
                buffer.setSample (1, i, in[(size_t) (pos + i)]);
            }

            plugin.processBlock (buffer, midi);

            for (int i = 0; i < blockSize; ++i)
                out[(size_t) (pos + i)] = buffer.getSample (0, i);
        }

        int best = -1;
        double bestError = 1.0e30;
        for (int delay = 0; delay < 8192; ++delay)
        {
            double error = 0.0;
            for (int i = 16000; i < 16400; ++i)
                error += std::abs ((double) out[(size_t) i] - (double) in[(size_t) (i - delay)]);

            if (error < bestError)
            {
                bestError = error;
                best = delay;
            }
        }

        return bestError < 1.0e-3 ? best : -1;
    }
}

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::cout << "usage : OniriqueTunerHostTest <fichier.vst3>" << std::endl;
        return 1;
    }

    juce::ScopedJuceInitialiser_GUI gui;

    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> found;
    format.findAllTypesForFile (found, argv[1]);

    std::cout << "Analyse de " << argv[1] << std::endl;
    check (found.size() == 1, "le fichier contient un plugin VST3");
    if (found.isEmpty())
        return 1;

    const auto& description = *found[0];
    std::cout << "  nom \"" << description.name << "\", editeur \"" << description.manufacturerName << "\", categorie \""
              << description.category << "\", version " << description.version << std::endl;
    check (description.name == "ONIRIQUE TUNER", "il s'appelle ONIRIQUE TUNER");
    check (! description.isInstrument, "il est declare comme effet");

    const double sampleRate = 48000.0;
    const int blockSize = 256;

    juce::String error;
    std::unique_ptr<juce::AudioPluginInstance> plugin (format.createInstanceFromDescription (description, sampleRate, blockSize, error));
    check (plugin != nullptr, "l'hote arrive a l'instancier" + (error.isEmpty() ? juce::String() : " (" + error + ")"));
    if (plugin == nullptr)
        return 1;

    plugin->setPlayConfigDetails (2, 2, sampleRate, blockSize);
    plugin->prepareToPlay (sampleRate, blockSize);
    check (plugin->getTotalNumInputChannels() == 2 && plugin->getTotalNumOutputChannels() == 2, "entree et sortie stereo");

    std::cout << "  " << plugin->getParameters().size() << " parametres :";
    for (auto* parameter : plugin->getParameters())
        std::cout << " " << parameter->getName (64) << "=" << parameter->getCurrentValueAsText() << ";";
    std::cout << std::endl;

    auto* mode = find (*plugin, "Mode");
    auto* retune = find (*plugin, "Retune Speed");
    auto* key = find (*plugin, "Key");
    check (mode != nullptr && retune != nullptr && key != nullptr, "les reglages principaux sont exposes a l'hote");
    check (plugin->getBypassParameter() != nullptr, "le bypass de l'hote est relie au plugin");
    if (mode == nullptr || retune == nullptr || key == nullptr)
        return 1;

    // --- LIVE : pas de latence, et le son non voisé traverse sans retard
    pump (300);
    check (plugin->getLatencySamples() == 0, "LIVE : latence declaree nulle");
    check (measureDelay (*plugin, blockSize) == 0, "LIVE : retard mesure nul");

    // --- HQ : la latence déclarée doit être exactement le retard réel
    mode->setValueNotifyingHost (1.0f);
    pump (400);
    const int declared = plugin->getLatencySamples();
    const int measured = measureDelay (*plugin, blockSize);
    std::cout << "  HQ : latence declaree " << declared << " echantillons (" << juce::String (1000.0 * declared / sampleRate, 1)
              << " ms), retard mesure " << measured << std::endl;
    check (declared > 0, "HQ : une latence est declaree a l'hote");
    check (declared == measured, "HQ : la latence declaree est le retard reel (compensation exacte)");

    // --- État : ce qui est enregistré dans le projet revient à l'identique
    retune->setValueNotifyingHost (0.0f);
    key->setValueNotifyingHost (key->getValueForText ("F#"));
    pump (200);
    juce::MemoryBlock saved;
    plugin->getStateInformation (saved);

    retune->setValueNotifyingHost (1.0f);
    key->setValueNotifyingHost (0.0f);
    mode->setValueNotifyingHost (0.0f);
    pump (200);

    plugin->setStateInformation (saved.getData(), (int) saved.getSize());
    pump (300);
    std::cout << "  apres rechargement : Mode=" << mode->getCurrentValueAsText() << ", Key=" << key->getCurrentValueAsText()
              << ", Retune=" << retune->getCurrentValueAsText() << std::endl;
    check (mode->getCurrentValueAsText() == "HQ" && key->getCurrentValueAsText() == "F#" && retune->getValue() < 0.001f,
           "l'etat enregistre est restaure");
    check (plugin->getLatencySamples() == declared, "la latence suit l'etat restaure");

    // --- Vocodeur : la voix devient des notes, qui sortent aussi en MIDI
    {
        auto* vocoder = find (*plugin, "Vocoder");
        auto* scale = find (*plugin, "Scale");
        check (vocoder != nullptr && find (*plugin, "Vocoder Voicing") != nullptr && find (*plugin, "Vocoder Blend") != nullptr,
               "les reglages du vocodeur sont exposes a l'hote");
        check (plugin->producesMidi(), "le plugin declare une sortie MIDI");

        if (vocoder != nullptr && scale != nullptr)
        {
            mode->setValueNotifyingHost (0.0f);
            key->setValueNotifyingHost (0.0f);
            scale->setValueNotifyingHost (0.0f);
            vocoder->setValueNotifyingHost (1.0f);
            pump (300);

            juce::String played;
            int opened = 0, closed = 0;
            bool finite = true;
            double energy = 0.0;

            const auto sing = [&] (double seconds, bool silent)
            {
                juce::AudioBuffer<float> buffer (2, blockSize);
                juce::MidiBuffer midi;
                const int blocks = (int) (seconds * sampleRate / blockSize);
                for (int b = 0; b < blocks; ++b)
                {
                    for (int i = 0; i < blockSize; ++i)
                    {
                        float sample = 0.0f;
                        if (! silent)
                        {
                            const double sung = 220.0 * std::pow (2.0, 35.0 / 1200.0);
                            voicePhase += 2.0 * juce::MathConstants<double>::pi * sung / sampleRate;
                            for (int h = 1; h <= 20; ++h)
                                sample += (float) (0.25 * std::sin ((double) h * voicePhase) / (double) h);
                        }

                        buffer.setSample (0, i, sample);
                        buffer.setSample (1, i, sample);
                    }

                    midi.clear();
                    plugin->processBlock (buffer, midi);

                    for (const auto metadata : midi)
                    {
                        const auto message = metadata.getMessage();
                        if (message.isNoteOn())
                        {
                            ++opened;
                            played << juce::MidiMessage::getMidiNoteName (message.getNoteNumber(), true, true, 4) << " ";   // do central = do4
                        }
                        else if (message.isNoteOff())
                        {
                            ++closed;
                        }
                    }

                    for (int i = 0; i < blockSize; ++i)
                    {
                        finite = finite && std::isfinite (buffer.getSample (0, i));
                        energy += (double) buffer.getSample (0, i) * buffer.getSample (0, i);
                    }
                }
            };

            sing (1.5, false);
            sing (0.5, true);

            std::cout << "  vocodeur : notes MIDI emises -> " << played << "(" << opened << " ouvertes, " << closed << " fermees)" << std::endl;
            check (opened >= 1 && played.startsWith ("A3"), "une voix chantee en la3 produit la note MIDI la3");
            check (opened == closed, "chaque note MIDI ouverte est refermee");
            check (finite && energy > 1.0, "le vocodeur produit du son");

            vocoder->setValueNotifyingHost (0.0f);
            pump (100);
        }
    }

    // --- Maltraitance : l'hôte automatise tout en même temps, change de
    //     format, sauvegarde et recharge, ouvre et ferme l'interface
    {
        juce::Random random (2026);
        bool finite = true;
        int rounds = 0;

        for (const auto& setup : { std::pair<double, int> { 44100.0, 512 }, std::pair<double, int> { 96000.0, 64 }, std::pair<double, int> { 48000.0, 1024 } })
        {
            plugin->releaseResources();
            plugin->setPlayConfigDetails (2, 2, setup.first, setup.second);
            plugin->prepareToPlay (setup.first, setup.second);

            juce::AudioBuffer<float> buffer (2, setup.second);
            juce::MidiBuffer midi;
            std::unique_ptr<juce::AudioProcessorEditor> editor;
            juce::MemoryBlock snapshot;

            for (int b = 0; b < 400; ++b, ++rounds)
            {
                for (auto* parameter : plugin->getParameters())
                    if (random.nextInt (6) == 0)
                        parameter->setValueNotifyingHost (random.nextFloat());

                if (b % 97 == 20)
                    editor.reset (plugin->createEditorIfNeeded());
                if (b % 97 == 60)
                    editor.reset();
                if (b % 131 == 40)
                    plugin->getStateInformation (snapshot);
                if (b % 131 == 90 && snapshot.getSize() > 0)
                    plugin->setStateInformation (snapshot.getData(), (int) snapshot.getSize());
                if (b % 50 == 0)
                    pump (20);

                for (int i = 0; i < setup.second; ++i)
                {
                    voicePhase += 2.0 * juce::MathConstants<double>::pi * (170.0 + 60.0 * std::sin (0.002 * (double) rounds)) / setup.first;
                    float sample = 0.0f;
                    for (int h = 1; h <= 12; ++h)
                        sample += (float) (0.2 * std::sin ((double) h * voicePhase) / (double) h);

                    buffer.setSample (0, i, sample);
                    buffer.setSample (1, i, sample * 0.8f);
                }

                midi.clear();
                plugin->processBlock (buffer, midi);

                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < setup.second; ++i)
                        finite = finite && std::isfinite (buffer.getSample (ch, i)) && std::abs (buffer.getSample (ch, i)) < 16.0f;
            }

            editor.reset();
        }

        std::cout << "  maltraitance : " << rounds << " blocs a 44,1 / 96 / 48 kHz, tous les reglages automatises au hasard, interface ouverte et fermee, etat recharge" << std::endl;
        check (finite, "la sortie reste saine quoi que fasse l'hote");
    }

    // --- Interface
    if (plugin->hasEditor())
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor (plugin->createEditorIfNeeded());
        check (editor != nullptr, "l'interface s'ouvre");
        if (editor != nullptr)
        {
            pump (300);
            std::cout << "  interface : " << editor->getWidth() << " x " << editor->getHeight() << std::endl;
            check (editor->getWidth() >= 700 && editor->getHeight() >= 462, "elle a une taille correcte");
        }
    }
    else
    {
        check (false, "le plugin declare une interface");
    }

    plugin->releaseResources();
    plugin.reset();

    std::cout << (failures == 0 ? "\nLe plugin se comporte correctement dans un hote VST3." : "\nDes verifications ont echoue.") << std::endl;
    return failures == 0 ? 0 : 1;
}
