// Outil de développement : instancie le plugin sans DAW, lui fait chanter
// une voix de synthèse, puis enregistre l'interface dans un fichier PNG.
//
//   OniriqueTunerSnapshot <sortie.png> [echelle] [hq] [formant] [hard] [silence]

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "Parameters.h"

#include <cmath>
#include <iostream>

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

namespace
{
    void setParameter (juce::AudioProcessorValueTreeState& state, const char* id, float value)
    {
        if (auto* parameter = state.getParameter (id))
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
    }
}

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::cout << "usage : OniriqueTunerSnapshot <sortie.png> [echelle] [hq] [formant] [vocoder] [hard] [silence]" << std::endl;
        return 1;
    }

    const float scale = argc > 2 ? juce::String (argv[2]).getFloatValue() : 1.0f;
    juce::StringArray options;
    for (int i = 3; i < argc; ++i)
        options.add (argv[i]);

    juce::ScopedJuceInitialiser_GUI gui;

    const double sampleRate = 48000.0;
    const int blockSize = 256;

    std::unique_ptr<juce::AudioProcessor> plugin (createPluginFilter());
    auto* tuner = dynamic_cast<OniriqueTunerProcessor*> (plugin.get());
    if (tuner == nullptr)
        return 2;

    namespace ids = onirique::params::id;
    setParameter (tuner->apvts, ids::key, 0.0f);       // do
    setParameter (tuner->apvts, ids::scale, 2.0f);     // mineur
    setParameter (tuner->apvts, ids::retune, 0.0f);
    setParameter (tuner->apvts, ids::mode, options.contains ("hq") ? 1.0f : 0.0f);
    setParameter (tuner->apvts, ids::formant, options.contains ("formant") ? 1.0f : 0.0f);
    setParameter (tuner->apvts, ids::vocoder, options.contains ("vocoder") ? 1.0f : 0.0f);
    setParameter (tuner->apvts, ids::hardTune, options.contains ("hard") ? 1.0f : 0.0f);
    setParameter (tuner->apvts, ids::voicing, options.contains ("vocoder") ? 3.0f : 0.0f);

    plugin->setPlayConfigDetails (2, 2, sampleRate, blockSize);
    plugin->prepareToPlay (sampleRate, blockSize);

    std::unique_ptr<juce::AudioProcessorEditor> editor (plugin->createEditor());
    auto* view = dynamic_cast<OniriqueTunerEditor*> (editor.get());
    if (view == nullptr)
        return 3;

    // Une phrase chantée un peu faux : la hauteur glisse d'une note à l'autre
    // avec du vibrato, entrecoupée de respirations.
    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;
    double phase = 0.0;
    const double melody[] = { 55.3, 58.2, 60.4, 62.8, 63.2, 60.3, 58.4, 55.2, 63.4, 67.3, 65.2, 62.6 };
    const bool silent = options.contains ("silence");
    const int totalBlocks = (int) (5.0 * sampleRate / blockSize);
    int untilRefresh = 0;

    for (int block = 0; block < totalBlocks; ++block)
    {
        for (int i = 0; i < blockSize; ++i)
        {
            const double t = ((double) block * blockSize + i) / sampleRate;
            const int step = juce::jmin (11, (int) (t / 0.42));
            const double local = t - step * 0.42;
            const double previous = melody[juce::jmax (0, step - 1)];
            const double glide = juce::jlimit (0.0, 1.0, local / 0.07);
            const double note = previous + (melody[step] - previous) * (0.5 - 0.5 * std::cos (juce::MathConstants<double>::pi * glide))
                                + 0.22 * std::sin (2.0 * juce::MathConstants<double>::pi * 5.6 * t);
            const double frequency = 440.0 * std::pow (2.0, (note - 69.0) / 12.0);
            phase += 2.0 * juce::MathConstants<double>::pi * frequency / sampleRate;

            double x = 0.0;
            for (int h = 1; h <= 24; ++h)
                x += std::sin ((double) h * phase) / (double) h * (1.0 + 2.5 * std::exp (-0.5 * std::pow (((double) h * frequency - 800.0) / 260.0, 2.0)));

            const bool breathing = local > 0.36 && step % 4 == 3;
            const float sample = silent || breathing ? 0.0f : (float) (0.12 * x);
            buffer.setSample (0, i, sample);
            buffer.setSample (1, i, sample);
        }

        plugin->processBlock (buffer, midi);

        if (++untilRefresh >= 6)
        {
            untilRefresh = 0;
            view->refreshDisplays (6.0 * blockSize / sampleRate);
        }
    }

    editor->setSize (juce::roundToInt (OniriqueTunerEditor::baseWidth * scale), juce::roundToInt (OniriqueTunerEditor::baseHeight * scale));
    const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);

    juce::File file = juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]);
    file.deleteFile();
    juce::FileOutputStream stream (file);
    if (! stream.openedOk() || ! juce::PNGImageFormat().writeImageToStream (image, stream))
    {
        std::cout << "impossible d'ecrire " << file.getFullPathName() << std::endl;
        return 4;
    }

    std::cout << "ecrit : " << file.getFullPathName() << " (" << image.getWidth() << " x " << image.getHeight() << ")" << std::endl;
    editor.reset();
    return 0;
}
