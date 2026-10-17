// Banc de test du moteur ONIRIQUE TUNER, sans JUCE.
//
//   engine_test                    -> tous les essais sur des voix de synthèse
//   engine_test <groupe>           -> un seul groupe : justesse, charge,
//                                     robustesse, voix, vocodeur, pannes
//   engine_test <dossier> a.wav…   -> traite ces fichiers et écrit le résultat
//                                     (LIVE, HQ, vocodeur) dans <dossier>
//
// Les voix de synthèse ont une hauteur et un timbre connus : on peut donc
// vérifier objectivement la justesse, la propreté (rapport harmoniques/bruit)
// et le respect des formants de ce qui sort du moteur.

#include "DSP/Fft.h"
#include "DSP/Scales.h"
#include "DSP/TuneEngine.h"
#include "voice_model.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <limits>
#include <new>
#include <random>
#include <string>
#include <vector>

using namespace onirique::tune;

//==============================================================================
//  Toute allocation de mémoire du programme passe par ce compteur : l'essai
//  "pannes" vérifie qu'il ne bouge pas pendant le traitement audio.
//==============================================================================
static std::atomic<long long> allocationCount { 0 };

void* operator new (std::size_t size)
{
    ++allocationCount;
    if (void* p = std::malloc (size != 0 ? size : 1))
        return p;

    throw std::bad_alloc();
}

void* operator new[] (std::size_t size)
{
    ++allocationCount;
    if (void* p = std::malloc (size != 0 ? size : 1))
        return p;

    throw std::bad_alloc();
}

void operator delete (void* p) noexcept              { std::free (p); }
void operator delete[] (void* p) noexcept            { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }

namespace
{
constexpr double pi = 3.14159265358979323846;

//==============================================================================
//  Fichiers WAV
//==============================================================================
struct Audio
{
    int sampleRate = 48000;
    std::vector<std::vector<float>> channels;

    int length() const { return channels.empty() ? 0 : (int) channels[0].size(); }
};

bool readWav (const std::string& path, Audio& audio)
{
    FILE* f = std::fopen (path.c_str(), "rb");
    if (f == nullptr)
        return false;

    std::fseek (f, 0, SEEK_END);
    const long size = std::ftell (f);
    std::fseek (f, 0, SEEK_SET);
    std::vector<std::uint8_t> bytes ((size_t) size);
    const size_t got = std::fread (bytes.data(), 1, bytes.size(), f);
    std::fclose (f);

    if (got != bytes.size() || size < 44 || std::memcmp (bytes.data(), "RIFF", 4) != 0 || std::memcmp (bytes.data() + 8, "WAVE", 4) != 0)
        return false;

    const auto u16 = [&] (size_t o) { return (std::uint32_t) bytes[o] | ((std::uint32_t) bytes[o + 1] << 8); };
    const auto u32 = [&] (size_t o) { return u16 (o) | (u16 (o + 2) << 16); };

    int format = 0, numChannels = 0, bits = 0;
    size_t dataOffset = 0, dataSize = 0;

    for (size_t o = 12; o + 8 <= bytes.size();)
    {
        const size_t chunkSize = u32 (o + 4);
        if (std::memcmp (bytes.data() + o, "fmt ", 4) == 0)
        {
            format = (int) u16 (o + 8);
            numChannels = (int) u16 (o + 10);
            audio.sampleRate = (int) u32 (o + 12);
            bits = (int) u16 (o + 22);
            if (format == 0xfffe && chunkSize >= 26)
                format = (int) u16 (o + 32);     // sous-format du WAVE_FORMAT_EXTENSIBLE
        }
        else if (std::memcmp (bytes.data() + o, "data", 4) == 0)
        {
            dataOffset = o + 8;
            dataSize = std::min (chunkSize, bytes.size() - dataOffset);
            break;
        }

        o += 8 + chunkSize + (chunkSize & 1);
    }

    if (dataOffset == 0 || numChannels < 1 || (format != 1 && format != 3))
        return false;

    const int bytesPerSample = bits / 8;
    const size_t frames = dataSize / (size_t) (bytesPerSample * numChannels);
    audio.channels.assign ((size_t) numChannels, std::vector<float> (frames));

    for (size_t i = 0; i < frames; ++i)
    {
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const std::uint8_t* p = bytes.data() + dataOffset + (i * (size_t) numChannels + (size_t) ch) * (size_t) bytesPerSample;
            float v = 0.0f;
            if (format == 3 && bits == 32)
            {
                std::memcpy (&v, p, 4);
            }
            else if (bits == 16)
            {
                v = (float) (std::int16_t) (p[0] | (p[1] << 8)) / 32768.0f;
            }
            else if (bits == 24)
            {
                std::int32_t s = p[0] | (p[1] << 8) | (p[2] << 16);
                if ((s & 0x800000) != 0)
                    s |= ~0xffffff;
                v = (float) s / 8388608.0f;
            }
            else if (bits == 32)
            {
                std::int32_t s;
                std::memcpy (&s, p, 4);
                v = (float) ((double) s / 2147483648.0);
            }

            audio.channels[(size_t) ch][i] = v;
        }
    }

    return true;
}

bool writeWav (const std::string& path, const Audio& audio)
{
    FILE* f = std::fopen (path.c_str(), "wb");
    if (f == nullptr)
        return false;

    const std::uint32_t numChannels = (std::uint32_t) audio.channels.size();
    const std::uint32_t frames = (std::uint32_t) audio.length();
    const std::uint32_t dataSize = frames * numChannels * 3;

    const auto put32 = [f] (std::uint32_t v) { std::fwrite (&v, 4, 1, f); };
    const auto put16 = [f] (std::uint16_t v) { std::fwrite (&v, 2, 1, f); };

    std::fwrite ("RIFF", 1, 4, f);
    put32 (36 + dataSize);
    std::fwrite ("WAVEfmt ", 1, 8, f);
    put32 (16);
    put16 (1);
    put16 ((std::uint16_t) numChannels);
    put32 ((std::uint32_t) audio.sampleRate);
    put32 ((std::uint32_t) audio.sampleRate * numChannels * 3);
    put16 ((std::uint16_t) (numChannels * 3));
    put16 (24);
    std::fwrite ("data", 1, 4, f);
    put32 (dataSize);

    std::vector<std::uint8_t> out ((size_t) dataSize);
    size_t o = 0;
    for (std::uint32_t i = 0; i < frames; ++i)
    {
        for (std::uint32_t ch = 0; ch < numChannels; ++ch)
        {
            const double v = std::max (-1.0, std::min (1.0, (double) audio.channels[ch][i]));
            const auto s = (std::int32_t) std::lround (v * 8388607.0);
            out[o++] = (std::uint8_t) (s & 0xff);
            out[o++] = (std::uint8_t) ((s >> 8) & 0xff);
            out[o++] = (std::uint8_t) ((s >> 16) & 0xff);
        }
    }

    std::fwrite (out.data(), 1, out.size(), f);
    std::fclose (f);
    return true;
}

//==============================================================================
//  Voix de synthèse : train d'impulsions à bande limitée + résonateurs
//==============================================================================
struct Resonator
{
    double b0, b1, b2, a1, a2;
    double z1 = 0.0, z2 = 0.0;

    Resonator (double fs, double frequency, double q, double gainDb)
    {
        // Filtre en cloche (RBJ)
        const double a = std::pow (10.0, gainDb / 40.0);
        const double w0 = 2.0 * pi * frequency / fs;
        const double alpha = std::sin (w0) / (2.0 * q);
        const double a0 = 1.0 + alpha / a;
        b0 = (1.0 + alpha * a) / a0;
        b1 = -2.0 * std::cos (w0) / a0;
        b2 = (1.0 - alpha * a) / a0;
        a1 = b1;
        a2 = (1.0 - alpha / a) / a0;
    }

    double process (double x)
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    double magnitude (double fs, double frequency) const
    {
        const double w = 2.0 * pi * frequency / fs;
        const double cr = std::cos (w), ci = -std::sin (w);
        const double c2r = std::cos (2.0 * w), c2i = -std::sin (2.0 * w);
        const double nr = b0 + b1 * cr + b2 * c2r, ni = b1 * ci + b2 * c2i;
        const double dr = 1.0 + a1 * cr + a2 * c2r, di = a1 * ci + a2 * c2i;
        return std::sqrt ((nr * nr + ni * ni) / (dr * dr + di * di));
    }
};

struct VoiceModel
{
    double fs = 48000.0;
    std::vector<Resonator> formants;

    explicit VoiceModel (double sampleRate) : fs (sampleRate)
    {
        // Voyelle proche d'un "a"
        formants.emplace_back (fs, 700.0, 6.0, 18.0);
        formants.emplace_back (fs, 1220.0, 9.0, 15.0);
        formants.emplace_back (fs, 2600.0, 12.0, 12.0);
        formants.emplace_back (fs, 3500.0, 12.0, 8.0);
    }

    /** Amplitude d'un partiel à la fréquence donnée (source x conduit vocal). */
    double envelope (double frequency) const
    {
        double v = std::pow (frequency / 100.0, -1.6);      // pente de la source glottique
        for (const auto& r : formants)
            v *= r.magnitude (fs, frequency);
        return v;
    }
};

std::vector<float> synthVoice (const VoiceModel& model, double seconds, const std::function<double (double)>& f0, double noise = 0.0005)
{
    const double fs = model.fs;
    const int n = (int) (seconds * fs);
    std::vector<float> out ((size_t) n);
    auto formants = model.formants;

    std::mt19937 rng (1234);
    std::normal_distribution<double> gauss (0.0, 1.0);

    double phase = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double t = (double) i / fs;
        const double f = f0 (t);
        phase += 2.0 * pi * f / fs;
        if (phase > 2.0 * pi)
            phase -= 2.0 * pi;

        const int harmonics = std::max (1, (int) std::floor (std::min (0.45 * fs, 11000.0) / f));
        double x = 0.0;
        for (int k = 1; k <= harmonics; ++k)
            x += std::pow ((double) k * f / 100.0, -1.6) * std::cos ((double) k * phase);

        for (auto& r : formants)
            x = r.process (x);

        const double fade = std::min (1.0, std::min (t / 0.02, (seconds - t) / 0.02));
        out[(size_t) i] = (float) (x * fade + noise * gauss (rng));
    }

    float peak = 0.0f;
    for (float v : out)
        peak = std::max (peak, std::abs (v));
    for (float& v : out)
        v *= 0.5f / std::max (peak, 1.0e-9f);

    return out;
}

//==============================================================================
//  Mesures
//==============================================================================

/** Fréquence fondamentale d'un extrait, par corrélation normalisée autour de
    la valeur attendue (recherche à ± 12 %), avec interpolation parabolique. */
double measureF0 (const float* x, int n, double fs, double expected)
{
    const double t0 = fs / expected;
    const int lo = (int) std::floor (t0 * 0.88);
    const int hi = (int) std::ceil (t0 * 1.12);
    const int w = n - hi - 1;
    if (w < 16)
        return 0.0;

    std::vector<double> score ((size_t) (hi - lo + 1));
    double xx = 0.0;
    for (int j = 0; j < w; ++j)
        xx += (double) x[j] * (double) x[j];

    int best = 0;
    for (int lag = lo; lag <= hi; ++lag)
    {
        double xy = 0.0, yy = 0.0;
        for (int j = 0; j < w; ++j)
        {
            xy += (double) x[j] * (double) x[j + lag];
            yy += (double) x[j + lag] * (double) x[j + lag];
        }

        score[(size_t) (lag - lo)] = xy / std::sqrt (xx * yy + 1.0e-30);
        if (score[(size_t) (lag - lo)] > score[(size_t) best])
            best = lag - lo;
    }

    double lag = (double) (lo + best);
    if (best > 0 && best < hi - lo)
    {
        const double a = score[(size_t) best - 1], b = score[(size_t) best], c = score[(size_t) best + 1];
        const double curve = a - 2.0 * b + c;
        if (curve < 0.0)
            lag += 0.5 * (a - c) / curve;
    }

    return fs / lag;
}

double cents (double f, double reference)
{
    return 1200.0 * std::log2 (f / reference);
}

struct PitchStats
{
    double meanCents = 0.0;      // écart moyen par rapport à la référence
    double spreadCents = 0.0;    // écart-type
    double worstCents = 0.0;
    int count = 0;
};

/** Suit la hauteur par fenêtres de 40 ms et la compare à une référence. */
PitchStats pitchStats (const std::vector<float>& x, double fs, int from, int to, const std::function<double (double)>& reference)
{
    PitchStats stats;
    const int window = (int) (0.04 * fs);
    const int hop = (int) (0.01 * fs);
    double sum = 0.0, sumSq = 0.0;

    for (int start = from; start + window < to; start += hop)
    {
        const double ref = reference (((double) start + 0.5 * (double) window) / fs);
        const double f = measureF0 (x.data() + start, window, fs, ref);
        if (f <= 0.0)
            continue;

        const double c = cents (f, ref);
        sum += c;
        sumSq += c * c;
        stats.worstCents = std::max (stats.worstCents, std::abs (c));
        ++stats.count;
    }

    if (stats.count > 0)
    {
        stats.meanCents = sum / stats.count;
        stats.spreadCents = std::sqrt (std::max (0.0, sumSq / stats.count - stats.meanCents * stats.meanCents));
    }

    return stats;
}

struct Spectrum
{
    int size = 0;
    double fs = 48000.0;
    std::vector<double> power;

    Spectrum (const float* x, int order, double sampleRate) : size (1 << order), fs (sampleRate)
    {
        Fft fft (order);
        std::vector<float> re ((size_t) size), im ((size_t) size, 0.0f);
        for (int i = 0; i < size; ++i)
        {
            // Blackman-Harris 4 termes : lobes secondaires à -92 dB
            const double a = 2.0 * pi * (double) i / (double) (size - 1);
            const double w = 0.35875 - 0.48829 * std::cos (a) + 0.14128 * std::cos (2.0 * a) - 0.01168 * std::cos (3.0 * a);
            re[(size_t) i] = (float) ((double) x[i] * w);
        }

        fft.forward (re.data(), im.data());
        power.resize ((size_t) size / 2);
        for (int k = 0; k < size / 2; ++k)
            power[(size_t) k] = (double) re[(size_t) k] * re[(size_t) k] + (double) im[(size_t) k] * im[(size_t) k];
    }

    double bandPower (double frequency, int halfWidth) const
    {
        const int centre = (int) std::lround (frequency * size / fs);
        double sum = 0.0;
        for (int k = std::max (1, centre - halfWidth); k <= std::min (size / 2 - 1, centre + halfWidth); ++k)
            sum += power[(size_t) k];
        return sum;
    }
};

struct Timbre
{
    double hnrDb = 0.0;            // harmoniques / tout le reste, jusqu'à 8 kHz
    double envelopeErrorDb = 0.0;  // écart RMS entre partiels mesurés et attendus
};

/** Analyse un extrait stationnaire de fondamentale connue. `expected` donne
    l'amplitude attendue d'un partiel à une fréquence donnée. */
Timbre analyseTimbre (const float* x, double fs, double f0, const std::function<double (double)>& expected)
{
    const int order = 15;
    const Spectrum spectrum (x, order, fs);
    const double top = 8000.0;

    double harmonic = 0.0, total = 0.0;
    for (int k = (int) (60.0 * spectrum.size / fs); k < (int) (top * spectrum.size / fs); ++k)
        total += spectrum.power[(size_t) k];

    std::vector<double> errors;
    for (int h = 1; (double) h * f0 < top; ++h)
    {
        const double p = spectrum.bandPower ((double) h * f0, 6);
        harmonic += p;

        if ((double) h * f0 < 5000.0)
            errors.push_back (10.0 * std::log10 (p + 1.0e-30) - 20.0 * std::log10 (expected ((double) h * f0)));
    }

    Timbre t;
    t.hnrDb = 10.0 * std::log10 (harmonic / std::max (1.0e-30, total - harmonic));

    double mean = 0.0;
    for (double e : errors)
        mean += e;
    mean /= (double) std::max<size_t> (1, errors.size());

    double sumSq = 0.0;
    for (double e : errors)
        sumSq += (e - mean) * (e - mean);
    t.envelopeErrorDb = std::sqrt (sumSq / (double) std::max<size_t> (1, errors.size()));
    return t;
}

//==============================================================================
//  Passage dans le moteur
//==============================================================================
std::vector<float> run (const std::vector<float>& input, double fs, const EngineParams& params, int blockSize = 128, double* secondsSpent = nullptr)
{
    TuneEngine engine;
    engine.setParams (params);
    engine.prepare (fs);

    std::vector<float> out = input;
    out.resize (input.size() + (size_t) engine.getLatencySamples(), 0.0f);

    const auto begin = std::chrono::steady_clock::now();
    for (size_t pos = 0; pos < out.size(); pos += (size_t) blockSize)
    {
        const int n = (int) std::min ((size_t) blockSize, out.size() - pos);
        float* channel = out.data() + pos;
        engine.process (&channel, 1, n);
    }
    const auto end = std::chrono::steady_clock::now();

    if (secondsSpent != nullptr)
        *secondsSpent = std::chrono::duration<double> (end - begin).count();

    // On retire la latence déclarée : la sortie est alignée sur l'entrée.
    out.erase (out.begin(), out.begin() + engine.getLatencySamples());
    return out;
}

const char* modeName (bool hq, bool formant)
{
    return hq ? (formant ? "HQ   formant " : "HQ   classic ")
              : (formant ? "LIVE formant " : "LIVE classic ");
}

int failures = 0;

void check (bool ok, const char* what)
{
    if (! ok)
    {
        ++failures;
        std::printf ("      !! ECHEC : %s\n", what);
    }
}

//==============================================================================
//  Essais
//==============================================================================
void testTransparency (double fs)
{
    std::printf ("\n[1] Transparence : un bruit (non voisé) doit ressortir intact\n");

    std::mt19937 rng (7);
    std::normal_distribution<float> gauss (0.0f, 0.1f);
    std::vector<float> noise ((size_t) (fs * 1.5));
    for (auto& v : noise)
        v = gauss (rng);

    for (bool hq : { false, true })
    {
        EngineParams p;
        p.hq = hq;
        p.retuneMs = 0.0f;
        const auto out = run (noise, fs, p);

        double worst = 0.0;
        for (size_t i = 0; i < noise.size(); ++i)
            worst = std::max (worst, (double) std::abs (out[i] - noise[i]));

        std::printf ("    %s  ecart max = %.2e  (latence %d echantillons)\n", hq ? "HQ  " : "LIVE", worst,
                     TuneEngine::latencyFor (fs, hq, p.inputType));
        check (worst < 1.0e-4, "le bruit doit traverser le moteur sans modification");
    }
}

void testHardTune (double fs)
{
    std::printf ("\n[2] Hard Tune : voix 35 cents trop haute, gamme chromatique, Retune 0\n");
    std::printf ("    mode            justesse (cents)        proprete   timbre\n");

    const VoiceModel model (fs);
    const double target = 220.0;
    const double sung = target * std::pow (2.0, 35.0 / 1200.0);
    const auto voice = synthVoice (model, 3.0, [sung] (double) { return sung; });

    for (bool hq : { false, true })
    {
        for (bool formant : { false, true })
        {
            EngineParams p;
            p.hq = hq;
            p.formant = formant;
            p.retuneMs = 0.0f;
            p.hardTune = true;
            const auto out = run (voice, fs, p);

            const auto stats = pitchStats (out, fs, (int) (0.5 * fs), (int) (2.8 * fs), [target] (double) { return target; });
            const double ratio = target / sung;
            const auto timbre = analyseTimbre (out.data() + (int) (0.8 * fs), fs, target,
                                               [&] (double f) { return model.envelope (formant ? f : f / ratio); });

            std::printf ("    %s  moy %+6.2f  ect %5.2f  max %5.2f   HNR %5.1f dB   env %4.2f dB\n", modeName (hq, formant),
                         stats.meanCents, stats.spreadCents, stats.worstCents, timbre.hnrDb, timbre.envelopeErrorDb);
            check (std::abs (stats.meanCents) < 1.0 && stats.worstCents < 3.0, "la sortie doit etre sur la note");
            check (timbre.hnrDb > 30.0, "la sortie doit rester propre");
        }
    }

    const auto reference = analyseTimbre (voice.data() + (int) (0.8 * fs), fs, sung, [&] (double f) { return model.envelope (f); });
    std::printf ("    (voix d'origine : HNR %.1f dB, env %.2f dB)\n", reference.hnrDb, reference.envelopeErrorDb);
}

void testScaleAndTranspose (double fs)
{
    std::printf ("\n[3] Grands ecarts : gamme de do majeur et transposition\n");
    std::printf ("    reglage                      mode            justesse (cents)        proprete   timbre\n");

    const VoiceModel model (fs);

    struct Case
    {
        const char* name;
        double sungMidi;
        double expectedMidi;
        int transpose;
        std::uint16_t mask;
    };

    const std::uint16_t cMajor = pitchClassMask (scales[1].mask, 0);
    const Case cases[] = {
        { "do# +10c -> re (do majeur)", 61.10, 62.0, 0, cMajor },
        { "la -30c, transpose +7     ", 56.70, 64.0, 7, 0x0fff },
        { "la +20c, transpose -5     ", 57.20, 52.0, -5, 0x0fff },
        { "mi4 juste, transpose +12  ", 64.00, 76.0, 12, 0x0fff },
    };

    for (const auto& c : cases)
    {
        const double sung = 440.0 * std::pow (2.0, (c.sungMidi - 69.0) / 12.0);
        const double target = 440.0 * std::pow (2.0, (c.expectedMidi - 69.0) / 12.0);
        const auto voice = synthVoice (model, 3.0, [sung] (double) { return sung; });

        for (bool hq : { false, true })
        {
            for (bool formant : { false, true })
            {
                EngineParams p;
                p.hq = hq;
                p.formant = formant;
                p.retuneMs = 0.0f;
                p.noteMask = c.mask;
                p.transpose = (float) c.transpose;
                const auto out = run (voice, fs, p);

                const auto stats = pitchStats (out, fs, (int) (0.5 * fs), (int) (2.8 * fs), [target] (double) { return target; });
                const double ratio = target / sung;
                const auto timbre = analyseTimbre (out.data() + (int) (0.8 * fs), fs, target,
                                                   [&] (double f) { return model.envelope (formant ? f : f / ratio); });

                std::printf ("    %s  %s  moy %+6.2f  ect %5.2f  max %5.2f   HNR %5.1f dB   env %4.2f dB\n", c.name, modeName (hq, formant),
                             stats.meanCents, stats.spreadCents, stats.worstCents, timbre.hnrDb, timbre.envelopeErrorDb);
                check (std::abs (stats.meanCents) < 1.5, "la sortie doit etre sur la note");
            }
        }
    }
}

void testVibrato (double fs)
{
    std::printf ("\n[4] Vibrato : la3 30 cents trop bas, avec +/- 40 cents a 5,5 Hz\n");

    const VoiceModel model (fs);
    const double centre = 220.0;
    const double flatBy = std::pow (2.0, -30.0 / 1200.0);
    const auto sung = [=] (double t) { return centre * flatBy * std::pow (2.0, 40.0 / 1200.0 * std::sin (2.0 * pi * 5.5 * t)); };
    const auto voice = synthVoice (model, 4.0, sung);

    const auto flat = [centre] (double) { return centre; };
    const auto inputStats = pitchStats (voice, fs, (int) (1.0 * fs), (int) (3.8 * fs), flat);
    std::printf ("    voix d'origine                         moyenne %+6.2f cents, ecart-type %5.2f cents\n", inputStats.meanCents, inputStats.spreadCents);

    const auto runWith = [&] (bool hq, bool hard, float retune, float vibrato)
    {
        EngineParams p;
        p.hq = hq;
        p.hardTune = hard;
        p.retuneMs = retune;
        p.vibrato = vibrato;
        return pitchStats (run (voice, fs, p), fs, (int) (1.0 * fs), (int) (3.8 * fs), flat);
    };

    for (bool hq : { false, true })
    {
        // Accordage classique, comme Auto-Tune : la note est toujours recentrée ;
        // à Retune 0 le vibrato est effacé, plus Retune est lent plus il passe.
        for (float retune : { 0.0f, 20.0f, 400.0f })
        {
            const auto stats = runWith (hq, false, retune, 0.0f);
            std::printf ("    %s classique Retune %3.0f ms        moyenne %+6.2f cents, ecart-type %5.2f cents\n", hq ? "HQ  " : "LIVE",
                         (double) retune, stats.meanCents, stats.spreadCents);

            check (std::abs (stats.meanCents) < 3.0, "l'accordage doit recentrer la note");
            if (retune == 0.0f)
                check (stats.spreadCents < (hq ? 2.0 : 4.0), "Retune 0 doit effacer le vibrato");
            if (retune == 20.0f)
                check (stats.spreadCents < 0.6 * inputStats.spreadCents, "Retune 20 doit nettement resserrer le vibrato");
            if (retune == 400.0f)
                check (stats.spreadCents > 0.8 * inputStats.spreadCents, "Retune 400 doit laisser passer le vibrato");
        }

        // Hard Tune : la note est plate. (Sur une voix fausse dont le vibrato
        // franchit le milieu de deux notes, il saute de l'une à l'autre, comme
        // Auto-Tune à 0 ms : on le juge ici sur un vibrato centré.)
        const auto centred = synthVoice (model, 4.0, [=] (double t) { return centre * std::pow (2.0, 40.0 / 1200.0 * std::sin (2.0 * pi * 5.5 * t)); });
        EngineParams hp;
        hp.hq = hq;
        hp.hardTune = true;
        hp.retuneMs = 0.0f;
        const auto hard = pitchStats (run (centred, fs, hp), fs, (int) (1.0 * fs), (int) (3.8 * fs), flat);
        std::printf ("    %s Hard Tune                     moyenne %+6.2f cents, ecart-type %5.2f cents\n", hq ? "HQ  " : "LIVE",
                     hard.meanCents, hard.spreadCents);
        check (hard.spreadCents < (hq ? 1.0 : 3.5) && std::abs (hard.meanCents) < 1.0, "Hard Tune doit aplatir la note");
    }

    // Natural Vibrato (Retune lent, qui laisse passer le vibrato) : -12 l'efface, +12 l'accentue.
    const auto less = runWith (true, false, 400.0f, -1.0f);
    const auto more = runWith (true, false, 400.0f, 1.0f);
    std::printf ("    HQ naturel, Natural Vibrato -12        moyenne %+6.2f cents, ecart-type %5.2f cents\n", less.meanCents, less.spreadCents);
    std::printf ("    HQ naturel, Natural Vibrato +12        moyenne %+6.2f cents, ecart-type %5.2f cents\n", more.meanCents, more.spreadCents);
    check (less.spreadCents < 0.35 * inputStats.spreadCents, "Natural Vibrato -12 doit effacer le vibrato");
    check (more.spreadCents > 1.5 * inputStats.spreadCents, "Natural Vibrato +12 doit accentuer le vibrato");
}

/** Hard Tune, comme Auto-Tune à Retune 0 : une voix qui glisse d'une octave sort
    en marches nettes, chaque demi-ton plat, sans glissé entre deux. */
void testHardSteps (double fs)
{
    std::printf ("\n[4b] Hard Tune : glissade la3 -> la4 en 2 s, gamme chromatique\n");

    const VoiceModel model (fs);
    const double seconds = 2.4;
    const auto midiAt = [] (double t) { return 57.0 + 12.0 * std::clamp ((t - 0.2) / 2.0, 0.0, 1.0); };
    const auto voice = synthVoice (model, seconds, [&] (double t) { return 440.0 * std::pow (2.0, (midiAt (t) - 69.0) / 12.0); });

    for (int pass = 0; pass < 4; ++pass)
    {
        const bool hq = (pass & 1) != 0, hard = pass < 2;
        EngineParams p;
        p.hq = hq;
        p.hardTune = hard;
        p.retuneMs = hard ? 20.0f : 60.0f;
        const auto out = run (voice, fs, p);

        // Fenêtres de 15 ms : sur une marche, la sortie est à moins de 5 cents d'un demi-ton.
        int onStep = 0, total = 0;
        const int window = (int) (0.015 * fs), hop = (int) (0.005 * fs);
        for (int start = (int) (0.3 * fs); start + window < (int) (2.1 * fs); start += hop)
        {
            const double guess = 440.0 * std::pow (2.0, (midiAt (((double) start + 0.5 * window) / fs) - 69.0) / 12.0);
            const double f = measureF0 (out.data() + start, window, fs, guess);
            if (f <= 0.0)
                continue;
            const double midi = 69.0 + 12.0 * std::log2 (f / 440.0);
            if (std::abs (midi - std::round (midi)) * 100.0 < 5.0)
                ++onStep;
            ++total;
        }

        std::printf ("    %s %-22s %d / %d fenetres sur un demi-ton exact\n", hq ? "HQ  " : "LIVE", hard ? "Hard Tune" : "classique Retune 60 ms", onStep, total);
        if (hard)
            check (onStep >= total * 85 / 100, "Hard Tune doit sortir en marches nettes");
        else
            check (onStep < total * 70 / 100, "l'accordage classique lent doit glisser d'une note a l'autre");
    }
}

/** Transitions, accordage naturel : quand la voix passe d'une note à l'autre en
    glissant, la sortie doit glisser comme elle, au même moment et avec la même
    forme, simplement recalée sur la gamme ; pas tenir l'ancienne note puis
    sauter sur la nouvelle (ce qui sonne robot). */
void testTransitions (double fs)
{
    std::printf ("\n[4d] Transitions entre notes, accordage naturel, Retune 20 ms\n");
    std::printf ("    passage                                   mode   ecart au glisse ideal   retard   vitesse max   note d'arrivee\n");

    const VoiceModel model (fs);

    struct Move
    {
        const char* name;
        double from, to, glideMs, overshoot, vibrato;
    };

    const Move moves[] = {
        { "la3+20c -> do4-15c, glisse de 90 ms     ", 57.20, 59.85, 90.0, 0.0, 0.0 },
        { "do4-10c -> la3+25c, glisse de 70 ms     ", 59.90, 57.25, 70.0, 0.0, 0.0 },
        { "la3 -> mi4+10c, saut de 50 ms, depasse  ", 57.05, 64.10, 50.0, 0.35, 0.0 },
        { "la3+30c -> si3-20c, glisse de 200 ms    ", 57.30, 58.80, 200.0, 0.0, 0.0 },
        { "la3 -> do4, glisse de 80 ms, vibrato    ", 57.15, 60.10, 80.0, 0.0, 0.25 },
    };

    const double hold = 0.7;
    double worstDeviation = 0.0, worstDelay = 0.0;

    for (const auto& m : moves)
    {
        const double start = hold, end = hold + m.glideMs / 1000.0;
        const auto sungAt = [&] (double t)
        {
            double midi;
            if (t < start)
                midi = m.from;
            else if (t < end)
                midi = m.from + (m.to - m.from) * (0.5 - 0.5 * std::cos (pi * (t - start) / (end - start)));
            else
                midi = m.to + m.overshoot * std::exp (-(t - end) / 0.03) * std::sin (pi * std::min (1.0, (t - end) / 0.03));

            return midi + m.vibrato * std::sin (2.0 * pi * 5.5 * t);
        };

        // Ce qu'on attend : la voix elle-même, décalée de la correction de la
        // première note puis de celle de la seconde, au fil de son glissé.
        const double fromNote = std::round (m.from), toNote = std::round (m.to);
        const auto idealAt = [&] (double t)
        {
            const double sung = sungAt (t) - m.vibrato * std::sin (2.0 * pi * 5.5 * t);
            const double progress = std::clamp ((sung - m.from) / (m.to - m.from), 0.0, 1.0);
            return sungAt (t) + (fromNote - m.from) + progress * ((toNote - m.to) - (fromNote - m.from));
        };

        const auto voice = synthVoice (model, end + hold, [&] (double t) { return 440.0 * std::pow (2.0, (sungAt (t) - 69.0) / 12.0); });

        for (bool hq : { false, true })
        {
            EngineParams p;
            p.hq = hq;
            p.retuneMs = 20.0f;
            const auto out = run (voice, fs, p);

            // Hauteur de sortie toutes les 2,5 ms, sur des fenêtres de 12 ms
            const int window = (int) (0.012 * fs), hop = (int) (0.0025 * fs);
            double sumSq = 0.0, maxOut = 0.0, maxIdeal = 0.0, previousOut = 0.0, previousIdeal = 0.0;
            double outCross = -1.0, idealCross = -1.0, landed = 0.0;
            int count = 0, landedCount = 0;
            const double midpoint = 0.5 * (fromNote + toNote);

            for (int s = (int) ((start - 0.05) * fs); s + window < (int) ((end + 0.5) * fs); s += hop)
            {
                const double t = ((double) s + 0.5 * window) / fs;
                const double ideal = idealAt (t);
                const double f = measureF0 (out.data() + s, window, fs, 440.0 * std::pow (2.0, (ideal - 69.0) / 12.0));
                if (f <= 0.0)
                    continue;

                const double got = 69.0 + 12.0 * std::log2 (f / 440.0);

                if (t < end + 0.12)
                {
                    sumSq += (got - ideal) * (got - ideal);
                    ++count;
                }
                else if (t > end + 0.15)
                {
                    landed += got - toNote - m.vibrato * std::sin (2.0 * pi * 5.5 * t);
                    ++landedCount;
                }

                if (count > 1)
                {
                    maxOut = std::max (maxOut, std::abs (got - previousOut) / 0.0025);
                    maxIdeal = std::max (maxIdeal, std::abs (ideal - previousIdeal) / 0.0025);
                }

                if (outCross < 0.0 && (got - midpoint) * (toNote - fromNote) > 0.0)
                    outCross = t;
                if (idealCross < 0.0 && (ideal - midpoint) * (toNote - fromNote) > 0.0)
                    idealCross = t;

                previousOut = got;
                previousIdeal = ideal;
            }

            const double deviation = 100.0 * std::sqrt (sumSq / std::max (1, count));
            const double delay = 1000.0 * (outCross - idealCross);
            std::printf ("    %s %s       %5.1f cents         %+5.1f ms     x%4.1f        %+5.1f cents\n", m.name, hq ? "HQ  " : "LIVE",
                         deviation, delay, maxOut / std::max (1.0e-9, maxIdeal), 100.0 * landed / std::max (1, landedCount));

            // En temps réel, la note d'arrivée n'est connue qu'une fois que la
            // voix freine : sur un glissé lent, le passage se fait dans sa
            // seconde moitié. Le retard se juge donc à la durée du glissé.
            worstDeviation = std::max (worstDeviation, deviation);
            worstDelay = std::max (worstDelay, std::abs (delay) / std::max (1.0, 0.2 * m.glideMs / 12.0));
            check (std::abs (100.0 * landed / std::max (1, landedCount)) < 6.0, "la note d'arrivee doit etre juste");
        }
    }

    // Avant le suivi des glissés (1.1.4) : 35 a 70 cents d'ecart, 15 a 52 ms de retard.
    std::printf ("    pire ecart au glisse ideal %.1f cents, pire retard %.1f ms (ramene a un glisse de 60 ms)\n", worstDeviation, worstDelay);
    check (worstDeviation < 32.0, "la sortie doit suivre le glisse de la voix, sans tenir puis sauter");
    check (worstDelay < 12.0, "la transition doit arriver au moment ou la voix la chante (a 20 % du glisse pres)");
}

/** Vibrato créé : une note tenue bien juste reçoit le vibrato choisi, après son délai. */
void testCreatedVibrato (double fs)
{
    std::printf ("\n[4c] Vibrato cree : la3 juste et sans vibrato, 5,5 Hz, +/- 40 cents\n");

    const VoiceModel model (fs);
    const double centre = 220.0;
    const auto voice = synthVoice (model, 3.5, [centre] (double) { return centre; });
    const auto flat = [centre] (double) { return centre; };

    for (bool hard : { false, true })
    {
        for (bool delayed : { false, true })
        {
            EngineParams p;
            p.hq = true;
            p.hardTune = hard;
            p.retuneMs = 20.0f;
            p.createdVibrato.on = true;
            p.createdVibrato.rateHz = 5.5f;
            p.createdVibrato.depthCents = 40.0f;
            p.createdVibrato.delayMs = delayed ? 1500.0f : 0.0f;
            p.createdVibrato.onsetMs = 0.0f;
            p.createdVibrato.variation = 0.0f;
            const auto out = run (voice, fs, p);

            const auto early = pitchStats (out, fs, (int) (0.3 * fs), (int) (1.2 * fs), flat);
            const auto late = pitchStats (out, fs, (int) (2.0 * fs), (int) (3.3 * fs), flat);
            std::printf ("    %-9s delai %4d ms   debut : ecart-type %5.2f cents   fin : moyenne %+6.2f, ecart-type %5.2f cents\n",
                         hard ? "Hard Tune" : "classique", delayed ? 1500 : 0, early.spreadCents, late.meanCents, late.spreadCents);

            // Sinus de +/- 40 cents : ecart-type 28 cents.
            check (late.spreadCents > 22.0 && late.spreadCents < 34.0, "le vibrato cree doit avoir l'amplitude choisie");
            check (std::abs (late.meanCents) < 4.0, "le vibrato cree doit rester centre sur la note");
            if (delayed)
                check (early.spreadCents < 3.0, "avant son delai, le vibrato cree ne doit pas commencer");
        }
    }
}

void testMelody (double fs)
{
    std::printf ("\n[5] Melodie glissee : la hauteur passe d'une note a l'autre en 60 ms\n");

    const VoiceModel model (fs);
    const double notes[] = { 57.25, 59.80, 60.30, 62.15, 59.70, 57.10 };
    const double noteLength = 0.45, glide = 0.06;

    const auto midiAt = [&] (double t)
    {
        const int index = std::min (5, (int) (t / noteLength));
        const double local = t - index * noteLength;
        if (index > 0 && local < glide)
        {
            const double x = local / glide;
            return notes[index - 1] + (notes[index] - notes[index - 1]) * (0.5 - 0.5 * std::cos (pi * x));
        }
        return notes[index];
    };

    const auto voice = synthVoice (model, 6 * noteLength, [&] (double t) { return 440.0 * std::pow (2.0, (midiAt (t) - 69.0) / 12.0); });
    const std::uint16_t aMinor = pitchClassMask (scales[2].mask, 9);

    for (int pass = 0; pass < 4; ++pass)
    {
        const bool hq = (pass & 1) != 0, hard = pass >= 2;
        EngineParams p;
        p.hq = hq;
        p.hardTune = hard;
        p.retuneMs = hard ? 0.0f : 20.0f;
        p.noteMask = aMinor;
        const auto out = run (voice, fs, p);

        // Hors transitions, chaque fenêtre doit tomber sur une note de la gamme.
        int onNote = 0, total = 0;
        double worst = 0.0;
        const int window = (int) (0.03 * fs), hop = (int) (0.01 * fs);
        for (int start = (int) (0.1 * fs); start + window < (int) out.size() - (int) (0.05 * fs); start += hop)
        {
            const double t = ((double) start + 0.5 * window) / fs;
            const double local = std::fmod (t, noteLength);
            if (local < glide + 0.05 || local > noteLength - 0.03)
                continue;

            const int index = std::min (5, (int) (t / noteLength));
            const int expected = PitchCorrector::nearestNote (notes[index], aMinor);
            const double expectedHz = 440.0 * std::pow (2.0, (expected - 69.0) / 12.0);
            const double f = measureF0 (out.data() + start, window, fs, expectedHz);
            const double error = std::abs (cents (f, expectedHz));
            worst = std::max (worst, error);
            if (error < 5.0)
                ++onNote;
            ++total;
        }

        std::printf ("    %s %-9s  %d / %d fenetres a moins de 5 cents de la note (pire ecart %.1f cents)\n", hq ? "HQ  " : "LIVE",
                     hard ? "Hard Tune" : "naturel", onNote, total, worst);
        check (onNote >= total * 95 / 100, "les notes tenues doivent etre justes");
    }
}

void testSampleRates()
{
    std::printf ("\n[6] Frequences d'echantillonnage et registres\n");

    for (double fs : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
    {
        const VoiceModel model (fs);
        const double target = 261.6256;
        const double sung = target * std::pow (2.0, -30.0 / 1200.0);
        const auto voice = synthVoice (model, 1.6, [sung] (double) { return sung; });

        for (bool hq : { false, true })
        {
            EngineParams p;
            p.hq = hq;
            p.retuneMs = 0.0f;
            const auto out = run (voice, fs, p);
            const auto stats = pitchStats (out, fs, (int) (0.5 * fs), (int) (1.5 * fs), [target] (double) { return target; });
            std::printf ("    %6.0f Hz %s  moy %+5.2f cents  max %4.2f   latence %5.1f ms\n", fs, hq ? "HQ  " : "LIVE",
                         stats.meanCents, stats.worstCents, 1000.0 * TuneEngine::latencyFor (fs, hq, p.inputType) / fs);
            check (std::abs (stats.meanCents) < 1.5, "justesse a cette frequence d'echantillonnage");
        }
    }

    const double fs = 48000.0;
    const VoiceModel model (fs);
    const struct { int type; double midi; } registers[] = { { 0, 72.3 }, { 1, 57.3 }, { 2, 43.3 }, { 3, 81.3 }, { 4, 33.3 } };
    for (const auto& r : registers)
    {
        const double sung = 440.0 * std::pow (2.0, (r.midi - 69.0) / 12.0);
        const double target = 440.0 * std::pow (2.0, (std::floor (r.midi + 0.5) - 69.0) / 12.0);
        const auto voice = synthVoice (model, 2.0, [sung] (double) { return sung; });

        for (bool hq : { false, true })
        {
            EngineParams p;
            p.hq = hq;
            p.inputType = r.type;
            p.retuneMs = 0.0f;
            const auto out = run (voice, fs, p);
            const auto stats = pitchStats (out, fs, (int) (0.7 * fs), (int) (1.9 * fs), [target] (double) { return target; });
            std::printf ("    %-13s %7.1f Hz %s  moy %+5.2f cents  max %4.2f   latence %5.1f ms\n", TuneEngine::inputTypeNames[r.type], sung,
                         hq ? "HQ  " : "LIVE", stats.meanCents, stats.worstCents, 1000.0 * TuneEngine::latencyFor (fs, hq, r.type) / fs);
            check (std::abs (stats.meanCents) < 2.0, "justesse dans ce registre");
        }
    }
}

void testCpu (double fs)
{
    std::printf ("\n[7] Charge processeur (mono, blocs de 64 echantillons)\n");

    const VoiceModel model (fs);
    const auto voice = synthVoice (model, 20.0, [] (double t) { return 180.0 * std::pow (2.0, 0.3 * std::sin (2.0 * pi * 0.4 * t)); });

    for (bool hq : { false, true })
    {
        for (bool formant : { false, true })
        {
            EngineParams p;
            p.hq = hq;
            p.formant = formant;
            p.retuneMs = 10.0f;
            double spent = 0.0;
            run (voice, fs, p, 64, &spent);
            std::printf ("    %s  %.2f %% d'un coeur\n", modeName (hq, formant), 100.0 * spent / 20.0);
        }
    }
}

void testAbuse()
{
    std::printf ("\n[8] Maltraitance : reglages et tailles de bloc aleatoires, changements de mode en route\n");

    std::mt19937 rng (99);
    const auto uniform = [&rng] (float lo, float hi) { return std::uniform_real_distribution<float> (lo, hi) (rng); };

    for (double fs : { 44100.0, 96000.0 })
    {
        const VoiceModel model (fs);
        auto left = synthVoice (model, 12.0, [] (double t) { return 150.0 * std::pow (2.0, 1.2 * std::sin (2.0 * pi * 0.7 * t) + 0.05 * std::sin (2.0 * pi * 6.0 * t)); }, 0.01);
        auto right = left;
        for (size_t i = 0; i < right.size(); ++i)
            right[i] = 0.8f * left[i] + 0.02f * uniform (-1.0f, 1.0f);

        // Des silences et des bouffées de bruit au milieu de la voix
        for (size_t i = 0; i < left.size(); ++i)
        {
            const double t = (double) i / fs;
            if (std::fmod (t, 3.0) > 2.6)
                left[i] = right[i] = 0.0f;
            else if (std::fmod (t, 3.0) > 2.3)
                left[i] = right[i] = 0.2f * uniform (-1.0f, 1.0f);
        }

        TuneEngine engine;
        engine.prepare (fs);

        float peak = 0.0f;
        bool finite = true;
        size_t pos = 0;
        int blocks = 0;

        while (pos < left.size())
        {
            EngineParams p;
            p.hq = (blocks / 37) % 2 == 1;
            p.inputType = (blocks / 211) % TuneEngine::numInputTypes;
            p.noteMask = (std::uint16_t) (rng() & 0x0fff);
            p.retuneMs = uniform (0.0f, 1.0f) < 0.5f ? 0.0f : uniform (0.0f, 400.0f);
            p.flex = uniform (0.0f, 1.0f);
            p.humanize = uniform (0.0f, 1.0f);
            p.vibrato = uniform (-1.0f, 1.0f);
            p.formant = (blocks / 53) % 2 == 1;
            p.throat = uniform (0.6f, 1.6f);
            p.transpose = (float) (int) uniform (-12.0f, 12.9f);
            p.detuneCents = uniform (-100.0f, 100.0f);
            p.tracking = uniform (0.0f, 1.0f);
            p.mix = uniform (0.0f, 1.0f);
            p.bypass = (blocks / 97) % 5 == 4;
            p.vocoder = (blocks / 41) % 3 == 1;
            p.vocoderVoicing = (int) (rng() % 4);
            p.vocoderBlend = uniform (0.0f, 1.0f);
            p.harmonyMask = (std::uint16_t) (rng() & 0x0fff);
            engine.setParams (p);

            const int sizes[] = { 1, 7, 32, 64, 256, 480, 1024, 4096 };
            const int n = (int) std::min ((size_t) sizes[rng() % 8], left.size() - pos);
            float* channels[2] = { left.data() + pos, right.data() + pos };
            engine.process (channels, 2, n);

            for (int i = 0; i < n; ++i)
            {
                finite = finite && std::isfinite (channels[0][i]) && std::isfinite (channels[1][i]);
                peak = std::max (peak, std::max (std::abs (channels[0][i]), std::abs (channels[1][i])));
            }

            pos += (size_t) n;
            ++blocks;
        }

        std::printf ("    %6.0f Hz : %d blocs, crete de sortie %.2f, %s\n", fs, blocks, (double) peak, finite ? "sortie saine" : "valeurs non finies");
        check (finite, "la sortie ne doit contenir que des nombres finis");
        check (peak < 2.0f, "la sortie ne doit pas s'emballer");
    }
}

//==============================================================================
//  Voix réalistes, avec vérité terrain
//==============================================================================
std::vector<voicemodel::Reading> listen (const voicemodel::Take& take, const EngineParams& params)
{
    TuneEngine engine;
    engine.setParams (params);
    engine.prepare (take.fs);

    std::vector<float> buffer (take.audio);
    buffer.resize (buffer.size() + (size_t) engine.getLatencySamples(), 0.0f);

    std::vector<voicemodel::Reading> readings;
    for (size_t pos = 0; pos < buffer.size(); pos += 256)
    {
        float* channel = buffer.data() + pos;
        engine.process (&channel, 1, (int) std::min<size_t> (256, buffer.size() - pos));

        DisplayFrame frame;
        while (engine.popDisplayFrame (frame))
            readings.push_back ({ frame.time, frame.voiced, (double) frame.inMidi });
    }

    return readings;
}

void testRealVoices (double fs)
{
    std::printf ("\n[9] Voix realistes (jitter, souffle, voyelles, consonnes, silences) : le detecteur face a la verite\n");
    std::printf ("    voix                         mode   notes reconnues   fausses alertes   hauteur fausse   dont octave   erreur fine\n");

    struct Condition
    {
        const char* name;
        voicemodel::Options options;
        int inputType;
        double minRecall, maxGross;
    };

    const auto make = [] (bool female, double breath, double noiseDb, double jitter, unsigned seed)
    {
        voicemodel::Options o;
        o.female = female;
        o.breath = breath;
        o.noiseDb = noiseDb;
        o.jitter = jitter;
        o.seed = seed;
        return o;
    };

    const Condition conditions[] = {
        { "homme, voix claire        ", make (false, 0.10, -70.0, 0.006, 11), 1, 95.0, 1.5 },
        { "femme, voix claire        ", make (true,  0.10, -70.0, 0.006, 12), 1, 95.0, 1.5 },
        { "homme, voix soufflee      ", make (false, 0.70, -70.0, 0.006, 13), 1, 90.0, 3.0 },
        { "femme, voix soufflee      ", make (true,  0.70, -70.0, 0.006, 14), 1, 90.0, 3.0 },
        // Une voix rauque varie réellement de +/- 40 cents d'un cycle à l'autre : la limite est plus large.
        { "homme, voix rauque        ", make (false, 0.30, -70.0, 0.025, 15), 1, 85.0, 6.0 },
        { "homme, bruit de fond -40dB", make (false, 0.15, -40.0, 0.006, 16), 1, 90.0, 3.0 },
        { "femme, bruit de fond -40dB", make (true,  0.15, -40.0, 0.006, 17), 1, 90.0, 3.0 },
        { "homme en registre grave   ", make (false, 0.15, -70.0, 0.006, 18), 2, 93.0, 2.0 },
        { "femme en registre soprano ", make (true,  0.15, -70.0, 0.006, 19), 0, 93.0, 2.0 },
    };

    double totalRecall = 0.0, totalAlarm = 0.0, totalGross = 0.0, totalFine = 0.0;
    int count = 0;

    for (const auto& c : conditions)
    {
        const auto take = voicemodel::sing (fs, 20.0, c.options);

        for (bool hq : { false, true })
        {
            EngineParams p;
            p.hq = hq;
            p.inputType = c.inputType;
            const auto score = voicemodel::judge (take, listen (take, p));

            std::printf ("    %s   %s   %6.1f %%          %5.2f %%          %5.2f %%         %5.2f %%      %4.1f cents   (trop haut %.2f %%)\n", c.name, hq ? "HQ  " : "LIVE",
                         score.recall, score.falseAlarm, score.gross, score.octave, score.fineCents, score.tooHigh);

            check (score.recall >= c.minRecall, "les notes tenues doivent etre reconnues");
            check (score.gross <= c.maxGross, "la hauteur detectee doit etre la bonne");
            check (score.falseAlarm <= 3.0, "consonnes et silences ne doivent pas etre pris pour des notes");

            totalRecall += score.recall;
            totalAlarm += score.falseAlarm;
            totalGross += score.gross;
            totalFine += score.fineCents;
            ++count;
        }
    }

    std::printf ("    moyenne                               %6.1f %%          %5.2f %%          %5.2f %%                      %4.1f cents\n",
                 totalRecall / count, totalAlarm / count, totalGross / count, totalFine / count);
}

//==============================================================================
//  Pannes : tout ce qu'un hôte ou un utilisateur peut infliger au moteur
//==============================================================================
void testFaults (double fs)
{
    std::printf ("\n[11] Pannes : entrees invalides, changements en route, memoire\n");

    const VoiceModel model (fs);
    const double sung = 220.0 * std::pow (2.0, 35.0 / 1200.0);
    const auto clean = synthVoice (model, 4.0, [sung] (double) { return sung; });
    const auto onNote = [] (double) { return 220.0; };

    // --- a. Valeurs impossibles dans le son entrant (NaN, infini, démesuré)
    {
        auto dirty = clean;
        std::mt19937 rng (21);
        const float poison[] = { std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
                                 -std::numeric_limits<float>::infinity(), 1.0e30f, -1.0e30f };
        for (int k = 0; k < 500; ++k)
            dirty[(size_t) (fs * 1.0) + rng() % (size_t) (fs * 1.0)] = poison[rng() % 5];

        bool allFinite = true, allRecovered = true;
        float peak = 0.0f;
        for (bool hq : { false, true })
        {
            for (bool vocoder : { false, true })
            {
                EngineParams p;
                p.hq = hq;
                p.retuneMs = 0.0f;
                p.vocoder = vocoder;
                const auto out = run (dirty, fs, p);

                for (float v : out)
                {
                    allFinite = allFinite && std::isfinite (v);
                    peak = std::max (peak, std::abs (v));
                }

                // Une seconde après la fin des dégâts, la correction doit être redevenue normale.
                const auto stats = pitchStats (out, fs, (int) (2.8 * fs), (int) (3.8 * fs), onNote);
                allRecovered = allRecovered && std::abs (stats.meanCents) < 1.5 && stats.count > 50;
            }
        }

        std::printf ("    500 valeurs impossibles en entree : sortie %s, crete %.1f, correction %s ensuite\n",
                     allFinite ? "saine" : "CORROMPUE", (double) peak, allRecovered ? "normale" : "PERDUE");
        check (allFinite, "aucune valeur non finie ne doit sortir du moteur");
        check (peak < 12.0f, "la sortie ne doit pas s'emballer");
        check (allRecovered, "le moteur doit se remettre d'une entree invalide");
    }

    // --- b. Tout changer pendant que ça chante : ni clic, ni note bloquée
    {
        const auto biggestStep = [] (const std::vector<float>& x)
        {
            float worst = 0.0f;
            for (size_t i = 1; i < x.size(); ++i)
                worst = std::max (worst, std::abs (x[i] - x[i - 1]));
            return worst;
        };

        // Référence : la pente la plus raide du signal lui-même, voix accordée
        // ou vocodeur (ses dents de scie sont naturellement plus abruptes).
        EngineParams p;
        p.retuneMs = 0.0f;
        p.vocoder = true;
        const float vocoded = biggestStep (run (clean, fs, p));
        p.vocoder = false;
        const float reference = std::max (vocoded, biggestStep (run (clean, fs, p)));

        TuneEngine engine;
        engine.setParams (p);
        engine.prepare (fs);

        std::vector<float> out (clean);
        int notesOn = 0, notesOff = 0, block = 0;
        for (size_t pos = 0; pos < out.size(); pos += 64, ++block)
        {
            // Toutes les 80 ms environ, un changement différent
            if (block % 60 == 20) p.hq = ! p.hq;
            if (block % 60 == 40) p.inputType = (p.inputType + 1) % 3;
            if (block % 45 == 10) p.vocoder = ! p.vocoder;
            if (block % 70 == 35) p.formant = ! p.formant;
            if (block % 90 == 50) p.bypass = ! p.bypass;
            engine.setParams (p);

            float* channel = out.data() + pos;
            engine.process (&channel, 1, (int) std::min<size_t> (64, out.size() - pos));

            for (int e = 0; e < engine.getNumNoteEvents(); ++e)
                (engine.getNoteEvents()[e].on ? notesOn : notesOff)++;
        }

        // On coupe le vocodeur : la dernière note doit se refermer.
        p.vocoder = false;
        p.bypass = false;
        engine.setParams (p);
        std::vector<float> silence ((size_t) (0.3 * fs), 0.0f);
        float* channel = silence.data();
        engine.process (&channel, 1, (int) silence.size());
        for (int e = 0; e < engine.getNumNoteEvents(); ++e)
            (engine.getNoteEvents()[e].on ? notesOn : notesOff)++;

        const float worst = biggestStep (out);
        std::printf ("    mode, registre, vocodeur, formant et bypass changes en route : plus grand saut %.3f (voix seule %.3f), notes %d ouvertes / %d fermees\n",
                     (double) worst, (double) reference, notesOn, notesOff);
        check (worst < 1.6f * reference, "changer un reglage ne doit pas faire de clic");

        // En cas d'échec, dire où : le numéro de bloc désigne le réglage fautif.
        if (worst >= 1.6f * reference)
            for (size_t i = 1; i < out.size(); ++i)
                if (std::abs (out[i] - out[i - 1]) > 1.3f * reference)
                    std::printf ("        saut %.3f a l'echantillon %d (bloc %d)\n", (double) std::abs (out[i] - out[i - 1]), (int) i, (int) (i / 64));
        check (notesOn == notesOff, "chaque note ouverte doit etre refermee");
    }

    // --- c. Fréquences d'échantillonnage extrêmes ou absurdes, blocs d'un échantillon
    {
        bool sane = true;
        std::string report;
        for (double rate : { 8000.0, 11025.0, 22050.0, 32000.0, 176400.0, 352800.0, 384000.0 })
        {
            const VoiceModel local (rate);
            const auto voice = synthVoice (local, 1.6, [sung] (double) { return sung; });
            EngineParams p;
            p.retuneMs = 0.0f;
            p.hq = rate > 20000.0;
            p.vocoder = rate == 32000.0;
            const auto out = run (voice, rate, p, rate < 20000.0 ? 1 : 37);

            bool finite = true;
            for (float v : out)
                finite = finite && std::isfinite (v);

            double centsOff = 0.0;
            if (! p.vocoder)
                centsOff = pitchStats (out, rate, (int) (0.6 * rate), (int) (1.5 * rate), onNote).meanCents;

            sane = sane && finite && std::abs (centsOff) < 3.0;
            char line[64];
            std::snprintf (line, sizeof (line), "%.0f:%+.1f ", rate, centsOff);
            report += line;
        }

        for (double rate : { 0.0, -48000.0, 1.0e12, std::numeric_limits<double>::quiet_NaN() })
        {
            TuneEngine engine;
            engine.prepare (rate);
            std::vector<float> buffer (clean.begin(), clean.begin() + 4096);
            float* channel = buffer.data();
            engine.process (&channel, 1, 4096);
            for (float v : buffer)
                sane = sane && std::isfinite (v);
        }

        std::printf ("    de 8 a 384 kHz (ecart en cents) : %s; frequences absurdes : pas de plantage\n", report.c_str());
        check (sane, "le moteur doit rester juste et sain a toute frequence d'echantillonnage");
    }

    // --- d. Appels limites
    {
        TuneEngine engine;
        std::vector<float> a (512, 0.1f), b (512, 0.1f);
        float* channels[2] = { a.data(), b.data() };

        engine.process (channels, 2, 512);          // avant prepare()
        engine.reset();
        engine.prepare (fs);
        engine.process (channels, 2, 0);
        engine.process (channels, 2, -5);
        engine.process (nullptr, 2, 512);
        engine.process (channels, 0, 512);
        engine.process (channels, 7, 512);
        engine.reset();
        engine.process (channels, 2, 512);

        bool finite = true;
        for (float v : a)
            finite = finite && std::isfinite (v);
        std::printf ("    appels limites (avant prepare, bloc vide, pointeur nul, trop de canaux, reset) : %s\n", finite ? "sans incident" : "SORTIE CORROMPUE");
        check (finite, "les appels limites ne doivent rien casser");
    }

    // --- e. Mémoire : rien ne doit être alloué pendant le traitement audio
    {
        TuneEngine engine;
        EngineParams p;
        engine.setParams (p);
        engine.prepare (fs);

        std::vector<float> left (clean), right (clean);
        const long long before = allocationCount.load();

        int block = 0;
        for (size_t pos = 0; pos + 128 <= left.size(); pos += 128, ++block)
        {
            p.hq = (block / 40) % 2 == 1;
            p.vocoder = (block / 25) % 2 == 1;
            p.vocoderVoicing = block % 4;
            p.formant = (block / 33) % 2 == 1;
            p.retuneMs = (float) (block % 50);
            p.inputType = (block / 60) % TuneEngine::numInputTypes;
            engine.setParams (p);

            float* channels[2] = { left.data() + pos, right.data() + pos };
            engine.process (channels, 2, 128);

            DisplayFrame frame;
            while (engine.popDisplayFrame (frame)) {}
        }

        const long long allocated = allocationCount.load() - before;
        std::printf ("    allocations memoire pendant 4 s de traitement avec changements de reglages : %lld\n", allocated);
        check (allocated == 0, "le traitement audio ne doit jamais allouer de memoire");
    }

    // --- f. Repisse du casque : une note juste mais très faible n'est pas la voix
    {
        auto take = synthVoice (model, 1.5, [sung] (double) { return sung; });
        const auto bleed = synthVoice (model, 1.5, [] (double) { return 261.6256; }, 0.0);
        const float bleedGain = std::pow (10.0f, -45.0f / 20.0f);
        for (float v : bleed)
            take.push_back (v * bleedGain);

        EngineParams p;
        p.retuneMs = 0.0f;
        p.transpose = 3.0f;         // si la repisse était traitée, elle serait transposée
        p.vocoder = false;

        TuneEngine engine;
        engine.setParams (p);
        engine.prepare (fs);

        std::vector<float> out (take);
        int voicedInBleed = 0;
        for (size_t pos = 0; pos < out.size(); pos += 256)
        {
            float* channel = out.data() + pos;
            engine.process (&channel, 1, (int) std::min<size_t> (256, out.size() - pos));

            DisplayFrame frame;
            while (engine.popDisplayFrame (frame))
                if (frame.voiced && frame.time > 1.8)
                    ++voicedInBleed;
        }

        double worst = 0.0;
        for (size_t i = (size_t) (2.0 * fs); i < out.size(); ++i)
            worst = std::max (worst, (double) std::abs (out[i] - take[i]));

        std::printf ("    repisse a -45 dB sous la voix : %d mesure(s) prise(s) pour une note, ecart avec l'entree %.1e\n", voicedInBleed, worst);
        check (voicedInBleed == 0 && worst < 1.0e-5, "la repisse du casque doit etre laissee intacte");
    }
}

//==============================================================================
//  Vocodeur
//==============================================================================
struct VocoderRun
{
    std::vector<float> audio;
    std::vector<NoteEvent> notes;       // sampleOffset = position absolue dans la prise
};

VocoderRun runVocoder (const std::vector<float>& input, double fs, const EngineParams& params, int blockSize = 128)
{
    TuneEngine engine;
    engine.setParams (params);
    engine.prepare (fs);

    VocoderRun result;
    result.audio = input;
    result.audio.resize (input.size() + (size_t) engine.getLatencySamples(), 0.0f);

    for (size_t pos = 0; pos < result.audio.size(); pos += (size_t) blockSize)
    {
        const int n = (int) std::min ((size_t) blockSize, result.audio.size() - pos);
        float* channel = result.audio.data() + pos;
        engine.process (&channel, 1, n);

        for (int e = 0; e < engine.getNumNoteEvents(); ++e)
        {
            NoteEvent event = engine.getNoteEvents()[e];
            event.sampleOffset += (int) pos - engine.getLatencySamples();
            result.notes.push_back (event);
        }
    }

    result.audio.erase (result.audio.begin(), result.audio.begin() + engine.getLatencySamples());
    return result;
}

double rmsDb (const float* x, int n)
{
    double sum = 0.0;
    for (int i = 0; i < n; ++i)
        sum += (double) x[i] * (double) x[i];
    return 10.0 * std::log10 (sum / std::max (1, n) + 1.0e-20);
}

void testVocoder (double fs)
{
    std::printf ("\n[10] Vocodeur : la voix devient des notes, les notes pilotent le synthe\n");

    const VoiceModel model (fs);

    // --- a. La porteuse joue la note de la gamme, pas la note chantée
    {
        const double sung = 220.0 * std::pow (2.0, 35.0 / 1200.0);
        auto voice = synthVoice (model, 3.0, [sung] (double) { return sung; });
        voice.resize (voice.size() + (size_t) (0.6 * fs), 0.0f);

        for (bool hq : { false, true })
        {
            EngineParams p;
            p.hq = hq;
            p.vocoder = true;
            const auto run = runVocoder (voice, fs, p);

            const auto stats = pitchStats (run.audio, fs, (int) (0.5 * fs), (int) (2.8 * fs), [] (double) { return 220.0; });
            const double level = rmsDb (run.audio.data() + (int) (1.0 * fs), (int) fs) - rmsDb (voice.data() + (int) (1.0 * fs), (int) fs);
            const double tail = rmsDb (run.audio.data() + (int) (3.35 * fs), (int) (0.2 * fs));

            // Le timbre : niveaux par bande d'un tiers d'octave, voix contre vocodeur
            const Spectrum in (voice.data() + (int) (0.8 * fs), 15, fs), out (run.audio.data() + (int) (0.8 * fs), 15, fs);
            std::vector<double> a, b;
            for (double f = 200.0; f < 7000.0; f *= 1.26)
            {
                double pa = 0.0, pb = 0.0;
                for (int k = (int) (f * in.size / fs); k < (int) (f * 1.26 * in.size / fs); ++k)
                {
                    pa += in.power[(size_t) k];
                    pb += out.power[(size_t) k];
                }
                a.push_back (10.0 * std::log10 (pa + 1.0e-30));
                b.push_back (10.0 * std::log10 (pb + 1.0e-30));
            }

            // On compare là où la voix a quelque chose à dire : une bande
            // vide entre deux partiels d'une voix de synthèse ne dit rien.
            const double loudestBand = *std::max_element (a.begin(), a.end());
            std::vector<double> va, vb;
            for (size_t i = 0; i < a.size(); ++i)
            {
                if (a[i] > loudestBand - 30.0)
                {
                    va.push_back (a[i]);
                    vb.push_back (b[i]);
                }
            }

            double ma = 0.0, mb = 0.0;
            for (size_t i = 0; i < va.size(); ++i)
            {
                ma += va[i] / (double) va.size();
                mb += vb[i] / (double) vb.size();
            }

            double saa = 0.0, sbb = 0.0, sab = 0.0;
            for (size_t i = 0; i < va.size(); ++i)
            {
                saa += (va[i] - ma) * (va[i] - ma);
                sbb += (vb[i] - mb) * (vb[i] - mb);
                sab += (va[i] - ma) * (vb[i] - mb);
            }
            const double likeness = sab / std::sqrt (saa * sbb + 1.0e-30);

            std::printf ("    %s  note jouee : %+.2f cents du la3 (pire %.2f)   niveau %+.1f dB   timbre suivi a %.0f %%   silence apres la voix : %.0f dB\n",
                         hq ? "HQ  " : "LIVE", stats.meanCents, stats.worstCents, level, 100.0 * likeness, tail);
            // (les deux dents de scie de la porteuse sont écartées de 7 cents de part et d'autre de la note)
            check (std::abs (stats.meanCents) < 1.5 && stats.worstCents < 8.0, "la porteuse doit jouer la note de la gamme");
            check (std::abs (level) < 4.0, "le vocodeur doit sortir au niveau de la voix");
            check (likeness > 0.9, "le timbre du vocodeur doit suivre celui de la voix");
            check (tail < -70.0, "le vocodeur doit se taire quand la voix s'arrete");

            float voicePeak = 0.0f, vocoderPeak = 0.0f;
            for (float v : voice)
                voicePeak = std::max (voicePeak, std::abs (v));
            for (float v : run.audio)
                vocoderPeak = std::max (vocoderPeak, std::abs (v));
            check (vocoderPeak <= 1.13f * voicePeak, "le vocodeur ne doit jamais depasser les cretes de la voix");
        }
    }

    // --- b. Les consonnes passent (porteuse de bruit)
    {
        std::mt19937 rng (3);
        std::normal_distribution<float> gauss (0.0f, 0.15f);
        std::vector<float> burst ((size_t) (0.6 * fs), 0.0f);
        float low = 0.0f;
        for (int i = (int) (0.2 * fs); i < (int) (0.3 * fs); ++i)
        {
            const float white = gauss (rng);
            low += 0.35f * (white - low);
            burst[(size_t) i] = white - low;        // sifflante
        }

        EngineParams p;
        p.vocoder = true;
        const auto run = runVocoder (burst, fs, p);
        const double difference = rmsDb (run.audio.data() + (int) (0.21 * fs), (int) (0.08 * fs)) - rmsDb (burst.data() + (int) (0.21 * fs), (int) (0.08 * fs));
        std::printf ("    consonne sifflante : ressort a %+.1f dB de son niveau d'origine\n", difference);
        check (std::abs (difference) < 6.0, "les consonnes doivent rester audibles dans le vocodeur");
    }

    // --- c. Accord pris dans la gamme : do chante en do mineur -> do, mi bemol, sol
    {
        const double sung = 261.6256 * std::pow (2.0, 20.0 / 1200.0);
        const auto voice = synthVoice (model, 3.0, [sung] (double) { return sung; });

        EngineParams p;
        p.vocoder = true;
        p.vocoderVoicing = 3;
        p.noteMask = p.harmonyMask = pitchClassMask (scales[2].mask, 0);
        const auto run = runVocoder (voice, fs, p);

        const Spectrum spectrum (run.audio.data() + (int) (0.8 * fs), 15, fs);
        const auto peak = [&] (double f) { return 10.0 * std::log10 (spectrum.bandPower (f, 4) + 1.0e-30); };
        const double root = peak (261.6256), minorThird = peak (311.127), majorThird = peak (329.628), fifth = peak (391.995);
        std::printf ("    accord en do mineur : do %.0f dB, mi bemol %.0f dB, sol %.0f dB (mi naturel, hors gamme : %.0f dB)\n", root, minorThird, fifth, majorThird);
        check (minorThird > root - 20.0 && fifth > root - 20.0, "l'accord doit contenir la tierce et la quinte de la gamme");
        check (majorThird < minorThird - 15.0, "l'accord ne doit pas contenir de note hors gamme");
    }

    // --- d. Les notes produites (ce qui part en MIDI)
    {
        const double notes[] = { 57.25, 59.80, 60.30, 62.15, 59.70, 57.10 };
        const double noteLength = 0.45, glide = 0.06;
        const auto midiAt = [&] (double t)
        {
            const int index = std::min (5, (int) (t / noteLength));
            const double local = t - index * noteLength;
            if (index > 0 && local < glide)
                return notes[index - 1] + (notes[index] - notes[index - 1]) * (0.5 - 0.5 * std::cos (pi * local / glide));
            return notes[index];
        };

        auto voice = synthVoice (model, 6 * noteLength, [&] (double t) { return 440.0 * std::pow (2.0, (midiAt (t) - 69.0) / 12.0); });
        voice.resize (voice.size() + (size_t) (0.5 * fs), 0.0f);

        for (bool hq : { false, true })
        {
            EngineParams p;
            p.hq = hq;
            p.vocoder = true;
            p.retuneMs = 0.0f;
            p.noteMask = p.harmonyMask = pitchClassMask (scales[2].mask, 9);     // la mineur
            const auto run = runVocoder (voice, fs, p, 256);

            std::string sequence;
            int sounding = -1, stuck = 0, wrongOff = 0, longNotes = 0;
            double lastOn = 0.0;
            std::vector<int> held;
            for (const auto& e : run.notes)
            {
                const double t = (double) e.sampleOffset / fs;
                if (e.on)
                {
                    if (sounding >= 0)
                        ++stuck;                 // une note commence alors que la précédente n'est pas finie
                    sounding = e.note;
                    lastOn = t;
                }
                else
                {
                    if (e.note != sounding)
                        ++wrongOff;
                    if (t - lastOn > 0.15)
                    {
                        ++longNotes;
                        held.push_back (e.note);
                        sequence += std::string (noteNames[e.note % 12]) + std::to_string (e.note / 12 - 1) + " ";
                    }
                    sounding = -1;
                }
            }

            std::printf ("    %s  notes tenues : %s (%d evenements, %d note(s) restee(s) ouverte(s))\n", hq ? "HQ  " : "LIVE",
                         sequence.c_str(), (int) run.notes.size(), (sounding >= 0 ? 1 : 0) + stuck);
            check (sounding < 0 && stuck == 0 && wrongOff == 0, "chaque note commencee doit se terminer");
            check (held == std::vector<int> ({ 57, 60, 62, 60, 57 }), "les notes tenues doivent etre celles de la melodie, dans la gamme");
        }
    }

    // --- e. Charge
    {
        const auto voice = synthVoice (model, 10.0, [] (double t) { return 180.0 * std::pow (2.0, 0.3 * std::sin (2.0 * pi * 0.4 * t)); });
        EngineParams p;
        p.vocoder = true;
        p.vocoderVoicing = 3;
        double spent = 0.0;
        run (voice, fs, p, 64, &spent);

        // La même chose en stéréo, comme sur une piste de DAW
        TuneEngine engine;
        engine.setParams (p);
        engine.prepare (fs);
        std::vector<float> left (voice), right (voice);
        const auto begin = std::chrono::steady_clock::now();
        for (size_t pos = 0; pos + 64 <= left.size(); pos += 64)
        {
            float* channels[2] = { left.data() + pos, right.data() + pos };
            engine.process (channels, 2, 64);
        }
        const double stereoSpent = std::chrono::duration<double> (std::chrono::steady_clock::now() - begin).count();

        const int from = (int) (2.0 * fs), span = (int) (4.0 * fs);
        const double stereoLevel = rmsDb (left.data() + from, span) - rmsDb (voice.data() + from, span);

        std::printf ("    charge avec vocodeur (accord) : %.2f %% d'un coeur en mono, %.2f %% en stereo (niveau d'un cote : %+.1f dB)\n",
                     100.0 * spent / 10.0, 100.0 * stereoSpent / 10.0, stereoLevel);
        check (std::abs (stereoLevel) < 4.0, "en stereo aussi le vocodeur doit sortir au niveau de la voix");
    }
}

// Gamme mineure des fichiers traités (--minor=Bb), do mineur par défaut.
std::uint16_t fileScale = pitchClassMask (scales[2].mask, 0);

void processFile (const std::string& path, const std::string& outDir)
{
    Audio audio;
    if (! readWav (path, audio))
    {
        std::printf ("    impossible de lire %s\n", path.c_str());
        return;
    }

    for (size_t ch = 0; ch < audio.channels.size(); ++ch)
    {
        float channelPeak = 0.0f;
        for (float v : audio.channels[ch])
            channelPeak = std::max (channelPeak, std::abs (v));
        std::printf ("    canal %d : crete %.4f sur %.1f s\n", (int) ch, (double) channelPeak, (double) audio.length() / audio.sampleRate);
    }

    // On garde les 45 secondes les plus chantées : inutile de traiter du silence.
    {
        const int second = audio.sampleRate;
        const int seconds = audio.length() / second;
        std::vector<int> active ((size_t) seconds + 1, 0);
        for (int sec = 0; sec < seconds; ++sec)
        {
            double sum = 0.0;
            for (int i = 0; i < second; ++i)
            {
                double v = 0.0;
                for (const auto& ch : audio.channels)
                    v += (double) ch[(size_t) (sec * second + i)];
                sum += v * v;
            }

            active[(size_t) sec + 1] = active[(size_t) sec] + (std::sqrt (sum / second) > 0.004 ? 1 : 0);
        }

        const int span = std::min (45, seconds);
        int bestStart = 0, bestCount = -1;
        for (int startSec = 0; startSec + span <= seconds; ++startSec)
        {
            const int count = active[(size_t) (startSec + span)] - active[(size_t) startSec];
            if (count > bestCount)
            {
                bestCount = count;
                bestStart = startSec;
            }
        }

        for (auto& ch : audio.channels)
        {
            ch.erase (ch.begin(), ch.begin() + bestStart * second);
            ch.resize ((size_t) (span * second));
        }

        std::printf ("    extrait : %d s a partir de %d s (%d s chantees)\n", span, bestStart, bestCount);
    }

    const int keep = audio.length();

    std::string name = path.substr (path.find_last_of ("/\\") + 1);
    name = name.substr (0, name.find_last_of ('.'));
    std::printf ("    %s : %d Hz, %d canal(aux)\n", name.c_str(), audio.sampleRate, (int) audio.channels.size());

    const std::uint16_t cMinor = fileScale;

    struct Variant
    {
        const char* suffix;
        bool hq;
        float retune;
        bool formant;
        bool vocoder;
        int voicing;
        bool hard = false;
    };

    const Variant variants[] = {
        { "sans correction", false, 0.0f, false, false, 0 },
        { "naturel HQ retune20",  true,  20.0f, false, false, 0 },
        { "naturel LIVE retune20", false, 20.0f, false, false, 0 },
        { "naturel HQ retune0",   true,  0.0f,  false, false, 0 },
        { "HARD TUNE HQ",        true,  0.0f,  false, false, 0, true },
        { "HARD TUNE LIVE",      false, 0.0f,  false, false, 0, true },
    };

    for (const auto& v : variants)
    {
        TuneEngine engine;
        const int numCh = std::min ((int) audio.channels.size(), TuneEngine::maxChannels);

        EngineParams p;
        p.hq = v.hq;
        p.retuneMs = v.retune;
        p.formant = v.formant;
        p.hardTune = v.hard;
        p.vocoder = v.vocoder;
        p.vocoderVoicing = v.voicing;
        p.noteMask = cMinor;
        p.harmonyMask = cMinor;
        p.bypass = &v == &variants[0];      // le premier passage sert de référence
        engine.setParams (p);
        engine.prepare (audio.sampleRate);

        Audio out;
        out.sampleRate = audio.sampleRate;
        out.channels.assign ((size_t) numCh, std::vector<float> ((size_t) keep + (size_t) engine.getLatencySamples(), 0.0f));
        for (int ch = 0; ch < numCh; ++ch)
            std::copy (audio.channels[(size_t) ch].begin(), audio.channels[(size_t) ch].end(), out.channels[(size_t) ch].begin());

        // Courbe de hauteur du premier réglage, pour l'examiner à part.
        FILE* trace = &v == &variants[0] ? std::fopen ((outDir + "/" + name + " - hauteur.csv").c_str(), "w") : nullptr;
        if (trace != nullptr)
            std::fprintf (trace, "mesure,voise,entree,sortie,clarte,niveau\n");

        int voiced = 0, frames = 0, jumps = 0;
        double offNote = 0.0;
        bool finite = true, previousVoiced = false;
        float peak = 0.0f, previousMidi = 0.0f, lowest = 200.0f, highest = 0.0f;

        const int total = out.length();
        const auto begin = std::chrono::steady_clock::now();
        for (int pos = 0; pos < total; pos += 256)
        {
            const int n = std::min (256, total - pos);
            float* pointers[TuneEngine::maxChannels] = {};
            for (int ch = 0; ch < numCh; ++ch)
                pointers[ch] = out.channels[(size_t) ch].data() + pos;

            engine.process (pointers, numCh, n);

            DisplayFrame frame;
            while (engine.popDisplayFrame (frame))
            {
                if (trace != nullptr)
                    std::fprintf (trace, "%d,%d,%.3f,%.3f,%.3f,%.5f\n", frames, frame.voiced ? 1 : 0, (double) frame.inMidi, (double) frame.outMidi,
                                  (double) frame.clarity, (double) frame.level);

                ++frames;
                if (frame.voiced)
                {
                    ++voiced;
                    offNote += std::abs (frame.outMidi - (float) PitchCorrector::nearestNote (frame.outMidi, cMinor));
                    lowest = std::min (lowest, frame.inMidi);
                    highest = std::max (highest, frame.inMidi);
                    if (previousVoiced && std::abs (frame.inMidi - previousMidi) > 5.0f)
                        ++jumps;
                    previousMidi = frame.inMidi;
                }

                previousVoiced = frame.voiced;
            }
        }
        const double spent = std::chrono::duration<double> (std::chrono::steady_clock::now() - begin).count();

        if (trace != nullptr)
            std::fclose (trace);

        for (auto& ch : out.channels)
        {
            ch.erase (ch.begin(), ch.begin() + engine.getLatencySamples());
            for (float s : ch)
            {
                finite = finite && std::isfinite (s);
                peak = std::max (peak, std::abs (s));
            }
        }

        // Clics : une pointe de dérivée seconde très supérieure à ce que
        // contient l'entrée au même endroit (à 15 ms près).
        int clicks = 0;
        double worstClick = 0.0, worstClickAt = 0.0;
        {
            const auto& in = audio.channels[0];
            const auto& o = out.channels[0];
            const int win = audio.sampleRate / 200;
            const int count = keep / win;
            std::vector<float> inPeak ((size_t) count, 0.0f), outPeak ((size_t) count, 0.0f);
            for (int w = 0; w < count; ++w)
            {
                for (int i = std::max (2, w * win); i < (w + 1) * win; ++i)
                {
                    inPeak[(size_t) w] = std::max (inPeak[(size_t) w], std::abs (in[(size_t) i] - 2.0f * in[(size_t) i - 1] + in[(size_t) i - 2]));
                    outPeak[(size_t) w] = std::max (outPeak[(size_t) w], std::abs (o[(size_t) i] - 2.0f * o[(size_t) i - 1] + o[(size_t) i - 2]));
                }
            }

            for (int w = 3; w + 3 < count; ++w)
            {
                float around = 0.0f;
                for (int k = -3; k <= 3; ++k)
                    around = std::max (around, inPeak[(size_t) (w + k)]);

                const double excess = (double) outPeak[(size_t) w] / ((double) around + 0.002);
                if (excess > 3.0)
                    ++clicks;
                if (excess > worstClick)
                {
                    worstClick = excess;
                    worstClickAt = (double) w * win / audio.sampleRate;
                }
            }
        }

        // Justesse réellement obtenue : on réécoute la sortie avec un second
        // détecteur, indépendant du premier, là où la voix est bien présente.
        double outOff = 0.0, outWorst = 0.0;
        int outCount = 0, outOnNote = 0;
        {
            TuneEngine listener;
            listener.prepare (audio.sampleRate);
            std::vector<float> mono (out.channels[0]);
            std::vector<float> offsets;

            for (size_t pos = 0; pos < mono.size(); pos += 256)
            {
                float* channel = mono.data() + pos;
                listener.process (&channel, 1, (int) std::min<size_t> (256, mono.size() - pos));

                DisplayFrame frame;
                while (listener.popDisplayFrame (frame))
                {
                    if (frame.voiced && frame.level > 0.02f && frame.clarity > 0.9f)
                    {
                        const float off = 100.0f * std::abs (frame.inMidi - (float) PitchCorrector::nearestNote (frame.inMidi, cMinor));
                        offsets.push_back (off);
                        outOff += off;
                        if (off < 10.0f)
                            ++outOnNote;
                    }
                }
            }

            outCount = (int) offsets.size();
            if (! offsets.empty())
            {
                std::sort (offsets.begin(), offsets.end());
                outWorst = offsets[offsets.size() * 9 / 10];
            }
        }

        std::printf ("      %-20s sortie reecoutee : ecart moyen a la gamme %.1f cents, 90 %% des mesures sous %.1f cents, %d %% a moins de 10 cents\n", v.suffix,
                     outOff / std::max (1, outCount), outWorst, 100 * outOnNote / std::max (1, outCount));

        if (! p.bypass)
            writeWav (outDir + "/" + name + " - " + v.suffix + ".wav", out);
        std::printf ("      %-20s voise %4.1f %%  tessiture %.0f-%.0f  sauts %d  ecart gamme %4.1f c  clics %d (pire x%.1f a %.2f s)  crete %.2f  cpu %.1f %%\n", v.suffix,
                     100.0 * voiced / std::max (1, frames), (double) lowest, (double) highest, jumps,
                     100.0 * offNote / std::max (1, voiced), clicks, worstClick, worstClickAt, (double) peak,
                     100.0 * spent / ((double) keep / audio.sampleRate));
        check (finite, "la sortie ne doit contenir que des nombres finis");
    }
}
} // namespace

int main (int argc, char** argv)
{
    const double fs = 48000.0;

    std::printf ("ONIRIQUE TUNER - banc de test du moteur\n");

    if (argc > 2)
    {
        std::printf ("\nFichiers\n");
        for (int i = 2; i < argc; ++i)
        {
            const std::string arg = argv[i];
            if (arg.rfind ("--minor=", 0) == 0)
            {
                const std::string name = arg.substr (8);
                static const char* names[] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
                static const char* sharps[] = { "C", "Db", "D", "D#", "E", "F", "Gb", "G", "G#", "A", "A#", "B" };
                for (int k = 0; k < 12; ++k)
                    if (name == names[k] || name == sharps[k])
                        fileScale = pitchClassMask (scales[2].mask, k);
            }
            else
            {
                processFile (arg, argv[1]);
            }
        }
    }
    else
    {
        // Sans argument : tout. Avec un mot : seulement ce groupe d'essais.
        const std::string only = argc > 1 ? argv[1] : "";
        const auto wanted = [&only] (const char* group) { return only.empty() || only == group; };

        if (wanted ("justesse"))
        {
            testTransparency (fs);
            testHardTune (fs);
            testScaleAndTranspose (fs);
            testVibrato (fs);
            testHardSteps (fs);
            testCreatedVibrato (fs);
            testMelody (fs);
            testSampleRates();
        }

        if (wanted ("justesse") || wanted ("transitions"))
            testTransitions (fs);

        if (wanted ("charge"))
            testCpu (fs);

        if (wanted ("robustesse"))
            testAbuse();

        if (wanted ("voix"))
            testRealVoices (fs);

        if (wanted ("vocodeur"))
            testVocoder (fs);

        if (wanted ("pannes"))
            testFaults (fs);
    }

    std::printf ("\n%s\n", failures == 0 ? "Tous les essais passent." : "Des essais ont echoue.");
    return failures == 0 ? 0 : 1;
}
