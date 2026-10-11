//==============================================================================
//  Banc d'essai de la licence (outil de développement, non livré).
//
//    OniriqueLicenseTest machine
//        affiche l'empreinte de ce PC, telle que le plugin la calcule
//
//    OniriqueLicenseTest verify <fichier.lic> <plugin> <machine> <clé publique base64> <maintenant en ms>
//        affiche l'état que le plugin déduirait de ce fichier
//
//    OniriqueLicenseTest export <plugin.vst3>
//        charge le plugin, lui fait traiter une sinusoïde en temps réel puis en export,
//        et dit si la sortie est du son ou du bruit blanc
//
//    OniriqueLicenseTest shot <plugin.vst3> <image.png>
//        charge le plugin et enregistre une image de son interface
//
//    OniriqueLicenseTest noise
//        contrôle quand le mode démo remplace le son par du bruit
//==============================================================================
#include "../Source/License/DemoNoise.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <cmath>
#include <cstring>
#include <iostream>

#if JUCE_WINDOWS
 #include <windows.h>
#endif
#include <vector>

namespace
{

const char* name (onirique::license::State state)
{
    switch (state)
    {
        case onirique::license::State::licensed: return "licensed";
        case onirique::license::State::trial:    return "trial";
        default:                                 return "unlicensed";
    }
}

struct Measure
{
    double rms = 0.0;
    double correlation = 0.0; // entre un échantillon et le suivant : proche de 1 pour un son, de 0 pour du bruit blanc
};

/** Fait traiter une sinusoïde au plugin et mesure ce qui en sort. */
Measure run (juce::AudioPluginInstance& plugin, bool offline)
{
    const double sampleRate = 48000.0;
    const int blockSize = 512, blocks = 240;

    plugin.setNonRealtime (offline);
    plugin.prepareToPlay (sampleRate, blockSize);

    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;
    double phase = 0.0, sum = 0.0, cross = 0.0;
    float previous = 0.0f;
    long counted = 0;

    for (int block = 0; block < blocks; ++block)
    {
        for (int i = 0; i < blockSize; ++i)
        {
            const float sample = 0.3f * (float) std::sin (phase);
            phase += juce::MathConstants<double>::twoPi * 220.0 / sampleRate;
            buffer.setSample (0, i, sample);
            buffer.setSample (1, i, sample);
        }

        midi.clear();
        plugin.processBlock (buffer, midi);

        if (block < 40) // le temps que le plugin se stabilise
            continue;

        for (int i = 0; i < blockSize; ++i)
        {
            const float out = buffer.getSample (0, i);
            sum += (double) out * out;
            cross += (double) out * previous;
            previous = out;
            ++counted;
        }
    }

    plugin.releaseResources();
    return { std::sqrt (sum / (double) counted), sum > 0.0 ? cross / sum : 0.0 };
}

#if JUCE_WINDOWS
/** Relit une zone de l'écran (en pixels logiques). */
juce::Image grabScreen (juce::Rectangle<int> area)
{
    const auto scale = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->scale;
    const auto px = (area.toDouble() * scale).toNearestInt();
    juce::Image image (juce::Image::RGB, px.getWidth(), px.getHeight(), false);
    HDC screen = GetDC (nullptr);
    juce::Image::BitmapData data (image, juce::Image::BitmapData::writeOnly);
    HDC memory = CreateCompatibleDC (screen);
    HBITMAP bitmap = CreateCompatibleBitmap (screen, px.getWidth(), px.getHeight());
    SelectObject (memory, bitmap);
    BitBlt (memory, 0, 0, px.getWidth(), px.getHeight(), screen, px.getX(), px.getY(), SRCCOPY);
    BITMAPINFO info {};
    info.bmiHeader.biSize = sizeof (BITMAPINFOHEADER);
    info.bmiHeader.biWidth = px.getWidth();
    info.bmiHeader.biHeight = -px.getHeight();
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    std::vector<juce::uint8> pixels ((size_t) px.getWidth() * (size_t) px.getHeight() * 4);
    GetDIBits (memory, bitmap, 0, (UINT) px.getHeight(), pixels.data(), &info, DIB_RGB_COLORS);
    for (int y = 0; y < px.getHeight(); ++y)
        for (int x = 0; x < px.getWidth(); ++x)
            std::memcpy (data.getPixelPointer (x, y), pixels.data() + ((size_t) y * (size_t) px.getWidth() + (size_t) x) * 4, 3);
    DeleteObject (bitmap);
    DeleteDC (memory);
    ReleaseDC (nullptr, screen);
    return image;
}
#else
juce::Image grabScreen (juce::Rectangle<int>) { return {}; }
#endif

} // namespace

int main (int argc, char** argv)
{
    const juce::String mode (argc > 1 ? argv[1] : "");

    if (mode == "noise")
    {
        using onirique::license::DemoNoise;
        using onirique::license::State;

        // Une seconde par bloc : rend les secondes pendant lesquelles le son a été remplacé.
        const auto noisySeconds = [] (State state, bool offline)
        {
            DemoNoise noise;
            noise.setState (state);
            std::vector<float> block (1000);
            float* channels[] = { block.data() };
            int count = 0;
            for (int second = 0; second < 60; ++second)
                count += noise.process (channels, 1, 1000, 1000.0, offline) ? 1 : 0;
            return count;
        };

        const bool ok = noisySeconds (State::licensed, false) == 0 && noisySeconds (State::licensed, true) == 0
                     && noisySeconds (State::trial, false) == 0 && noisySeconds (State::trial, true) == 60
                     && noisySeconds (State::unlicensed, false) == 4 && noisySeconds (State::unlicensed, true) == 60;
        std::cout << (ok ? "bruit du mode demo : conforme" : "bruit du mode demo : ECHEC") << std::endl;
        return ok ? 0 : 1;
    }

    if (mode == "machine")
    {
        std::cout << onirique::license::machineId() << std::endl;
        return 0;
    }

    if (mode == "verify" && argc == 7)
    {
        juce::MemoryBlock key;
        {
            juce::MemoryOutputStream stream (key, false);
            juce::Base64::convertFromBase64 (stream, argv[5]);
        }
        if (key.getSize() != 32)
        {
            std::cout << "cle publique invalide" << std::endl;
            return 2;
        }

        const auto status = onirique::license::verify (juce::File::getCurrentWorkingDirectory().getChildFile (juce::String::fromUTF8 (argv[2])),
                                                       argv[3], argv[4], (const unsigned char*) key.getData(),
                                                       juce::String (argv[6]).getLargeIntValue());
        std::cout << name (status.state) << " exportPropre=" << (status.exportsClean() ? "oui" : "non")
                  << " email=" << status.email << " expiresAt=" << status.expiresAt << std::endl;
        return 0;
    }

    if ((mode == "export" && argc == 3) || (mode == "shot" && argc == 4))
    {
        juce::ScopedJuceInitialiser_GUI gui;
        juce::VST3PluginFormat format;
        juce::OwnedArray<juce::PluginDescription> found;
        format.findAllTypesForFile (found, juce::File::getCurrentWorkingDirectory().getChildFile (juce::String::fromUTF8 (argv[2])).getFullPathName());
        if (found.isEmpty())
        {
            std::cout << "aucun plugin dans ce fichier" << std::endl;
            return 1;
        }

        juce::String error;
        std::unique_ptr<juce::AudioPluginInstance> plugin (format.createInstanceFromDescription (*found[0], 48000.0, 512, error));
        if (plugin == nullptr)
        {
            std::cout << "chargement impossible : " << error << std::endl;
            return 1;
        }

        plugin->setPlayConfigDetails (2, 2, 48000.0, 512);
        std::cout << found[0]->name << " " << found[0]->version << std::endl;

        if (mode == "shot")
        {
            std::unique_ptr<juce::AudioProcessorEditor> editor (plugin->createEditorIfNeeded());
            if (editor == nullptr)
                return 1;

            // L'interface d'un VST3 est une fenêtre du système : elle est affichée, puis relue à l'écran.
            editor->addToDesktop (0);
            editor->setTopLeftPosition (0, 0);
            editor->setAlwaysOnTop (true);
            editor->setVisible (true);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (1500);
            const auto image = grabScreen (editor->getScreenBounds());
            const auto file = juce::File::getCurrentWorkingDirectory().getChildFile (juce::String::fromUTF8 (argv[3]));
            file.deleteFile();
            juce::FileOutputStream out (file);
            return juce::PNGImageFormat().writeImageToStream (image, out) ? 0 : 1;
        }

        const auto live = run (*plugin, false);
        const auto bounce = run (*plugin, true);
        const auto describe = [] (const Measure& m) { return m.correlation > 0.9 ? "son" : std::abs (m.correlation) < 0.1 ? "BRUIT BLANC" : "indetermine"; };

        std::cout << "temps reel : rms=" << live.rms << " correlation=" << live.correlation << " -> " << describe (live) << std::endl;
        std::cout << "export     : rms=" << bounce.rms << " correlation=" << bounce.correlation << " -> " << describe (bounce) << std::endl;
        return 0;
    }

    std::cout << "usage : OniriqueLicenseTest noise | shot <plugin.vst3> <image.png> | machine | verify <lic> <plugin> <machine> <cle> <maintenant> | export <plugin.vst3>" << std::endl;
    return 2;
}
