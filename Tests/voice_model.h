#pragma once

// Voix de synthèse "réaliste" pour mettre le détecteur à l'épreuve, avec sa
// vérité terrain. Contrairement aux voix parfaites des autres essais, celle-ci
// a ce qui rend une vraie prise difficile : hauteur approximative, glissés et
// attaques par en dessous, vibrato, irrégularité d'un cycle à l'autre (jitter
// et shimmer), souffle, voyelles qui changent, consonnes, silences, bruit de
// fond. On sait à chaque échantillon s'il y a une note et laquelle.

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

namespace voicemodel
{

constexpr double pi = 3.14159265358979323846;

struct Options
{
    bool female = false;
    double breath = 0.15;       // 0 = voix pure, 1 = très soufflée
    double noiseDb = -70.0;     // bruit de fond, en dB pleine échelle
    double jitter = 0.006;      // irrégularité de période d'un cycle à l'autre
    double shimmer = 0.04;      // irrégularité d'amplitude d'un cycle à l'autre
    unsigned seed = 1;
};

struct Take
{
    double fs = 48000.0;
    std::vector<float> audio;
    std::vector<float> f0;      // fréquence chantée en Hz, 0 = pas de note
};

/** Filtre en cloche dont on peut déplacer la fréquence en cours de route. */
struct Bell
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0, z1 = 0.0, z2 = 0.0;

    void set (double fs, double frequency, double q, double gainDb)
    {
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
};

inline Take sing (double fs, double seconds, const Options& options)
{
    std::mt19937 rng (options.seed);
    std::normal_distribution<double> gauss (0.0, 1.0);
    const auto uniform = [&rng] (double lo, double hi) { return std::uniform_real_distribution<double> (lo, hi) (rng); };

    struct Segment
    {
        enum Kind { note, consonant, silence } kind = silence;
        double start = 0.0, length = 0.0, midi = 60.0;
        double vibratoDepth = 0.0, vibratoRate = 5.5;
        int vowel = 0;
        bool legato = false;
        double previousMidi = 60.0;
    };

    const double lowNote = options.female ? 57.0 : 45.0;
    const double highNote = options.female ? 77.0 : 65.0;

    std::vector<Segment> script;
    double cursor = 0.15, lastMidi = 0.5 * (lowNote + highNote);
    bool lastWasNote = false;

    while (cursor < seconds)
    {
        Segment s;
        s.start = cursor;
        const double dice = uniform (0.0, 1.0);

        if (dice < 0.7 || ! lastWasNote)
        {
            s.kind = Segment::note;
            s.length = uniform (0.14, 0.7);
            s.previousMidi = lastMidi;
            double target = std::round (lastMidi) + std::round (uniform (-5.0, 5.0));
            target = std::max (lowNote, std::min (highNote, target));
            s.midi = target + uniform (-0.45, 0.45);          // chanté à côté de la note
            s.legato = lastWasNote && uniform (0.0, 1.0) < 0.6;
            s.vowel = (int) uniform (0.0, 4.999);
            s.vibratoDepth = s.length > 0.35 ? uniform (0.1, 0.4) : 0.0;
            s.vibratoRate = uniform (5.0, 6.5);
            lastMidi = s.midi;
            lastWasNote = true;
        }
        else if (dice < 0.85)
        {
            s.kind = Segment::consonant;
            s.length = uniform (0.03, 0.09);
            lastWasNote = false;
        }
        else
        {
            s.kind = Segment::silence;
            s.length = uniform (0.12, 0.45);
            lastWasNote = false;
        }

        cursor += s.length;
        script.push_back (s);
    }

    // a, e, i, o, u
    static const double vowels[5][3] = {
        { 700.0, 1220.0, 2600.0 }, { 480.0, 1900.0, 2550.0 }, { 300.0, 2300.0, 3000.0 },
        { 500.0, 900.0, 2500.0 },  { 320.0, 800.0, 2300.0 },
    };

    Take take;
    take.fs = fs;
    const int n = (int) (seconds * fs);
    take.audio.assign ((size_t) n, 0.0f);
    take.f0.assign ((size_t) n, 0.0f);

    Bell formants[4];
    double formantNow[3] = { 600.0, 1400.0, 2600.0 };
    formants[3].set (fs, 3500.0, 12.0, 8.0);

    double phase = 0.0, cycleJitter = 1.0, cycleShimmer = 1.0, drift = 0.0;
    double hissState = 0.0;
    size_t index = 0;

    for (int i = 0; i < n; ++i)
    {
        const double t = (double) i / fs;
        while (index + 1 < script.size() && t >= script[index].start + script[index].length)
            ++index;

        const Segment& s = script[index];
        const double local = t - s.start;
        if (local < 0.0 || local >= s.length)
            continue;

        if (s.kind == Segment::note)
        {
            if (i % 32 == 0)
            {
                for (int k = 0; k < 3; ++k)
                {
                    formantNow[k] += (vowels[s.vowel][k] - formantNow[k]) * (1.0 - std::exp (-32.0 / (0.06 * fs)));
                    formants[k].set (fs, formantNow[k], k == 0 ? 6.0 : (k == 1 ? 9.0 : 12.0), k == 0 ? 18.0 : (k == 1 ? 15.0 : 12.0));
                }

                drift += 0.002 * gauss (rng) - 0.01 * drift;      // dérive lente, en demi-tons
            }

            double pitch = s.midi + drift;
            if (s.legato)
                pitch += (s.previousMidi - s.midi) * (0.5 + 0.5 * std::cos (pi * std::min (1.0, local / 0.07)));
            else
                pitch -= 0.8 * std::exp (-local / 0.03);          // attaque par en dessous

            if (s.vibratoDepth > 0.0 && local > 0.18)
                pitch += s.vibratoDepth * std::min (1.0, (local - 0.18) / 0.1) * std::sin (2.0 * pi * s.vibratoRate * (local - 0.18));

            const double f = 440.0 * std::pow (2.0, (pitch - 69.0) / 12.0);
            const double envelope = std::min (1.0, std::min (local / 0.015, (s.length - local) / 0.03));

            phase += f * cycleJitter / fs;
            if (phase >= 1.0)
            {
                phase -= 1.0;
                cycleJitter = 1.0 + options.jitter * gauss (rng);
                cycleShimmer = 1.0 + options.shimmer * gauss (rng);
            }

            // Source glottique : les partiels décroissent avec leur rang (et non
            // avec leur fréquence), si bien qu'une note aiguë n'est pas plus
            // faible qu'une grave. Le souffle s'y ajoute, modulé par le cycle.
            const int harmonics = std::max (1, (int) std::floor (std::min (0.45 * fs, 8000.0) / f));
            double x = 0.0;
            for (int k = 1; k <= harmonics; ++k)
                x += std::pow ((double) k, -1.6) * std::cos (2.0 * pi * (double) k * phase);

            x = x * cycleShimmer + options.breath * 0.9 * gauss (rng) * (0.5 + 0.5 * std::cos (2.0 * pi * phase));

            for (auto& formant : formants)
                x = formant.process (x);

            take.audio[(size_t) i] = (float) (x * envelope);
            if (envelope > 0.25)
                take.f0[(size_t) i] = (float) f;
        }
        else if (s.kind == Segment::consonant)
        {
            // Sifflante : bruit passe-haut
            const double white = gauss (rng);
            hissState += 0.35 * (white - hissState);
            const double envelope = std::min (1.0, std::min (local / 0.005, (s.length - local) / 0.01));
            take.audio[(size_t) i] = (float) ((white - hissState) * 0.6 * envelope);
        }
    }

    float peak = 0.0f;
    for (float v : take.audio)
        peak = std::max (peak, std::abs (v));

    const double background = std::pow (10.0, options.noiseDb / 20.0);
    for (float& v : take.audio)
        v = (float) ((double) v * 0.5 / std::max (1.0e-9, (double) peak) + background * gauss (rng));

    return take;
}

/** Bilan d'un détecteur face à la vérité terrain. */
struct Score
{
    double recall = 0.0;        // notes tenues reconnues comme telles (%)
    double falseAlarm = 0.0;    // consonnes et silences pris pour une note (%)
    double gross = 0.0;         // hauteur fausse de plus de 50 cents (%)
    double octave = 0.0;        // dont erreurs d'octave ou de quinte (%)
    double tooHigh = 0.0;       // dont note détectée trop haute (%)
    double fineCents = 0.0;     // erreur moyenne quand la note est la bonne
    int frames = 0;
};

struct Reading
{
    double seconds = 0.0;
    bool voiced = false;
    double midi = 0.0;
};

inline Score judge (const Take& take, const std::vector<Reading>& readings)
{
    const int n = (int) take.f0.size();
    const int margin = (int) (0.02 * take.fs);

    int notes = 0, heard = 0, settled = 0, quiet = 0, alarms = 0, gross = 0, octave = 0, tooHigh = 0, fineCount = 0;
    double fine = 0.0;

    for (const auto& r : readings)
    {
        const int i = (int) std::lround (r.seconds * take.fs);
        if (i < margin || i >= n - margin)
            continue;

        // On ne juge que loin des transitions : note établie ou vrai silence.
        bool allVoiced = true, allQuiet = true;
        for (int k = i - margin; k <= i + margin; k += margin / 4)
        {
            allVoiced = allVoiced && take.f0[(size_t) k] > 0.0f;
            allQuiet = allQuiet && take.f0[(size_t) k] <= 0.0f;
        }

        if (allVoiced)
        {
            ++notes;
            if (r.voiced)
            {
                ++heard;

                // Pendant un glissé la "bonne" hauteur change d'une mesure à
                // l'autre : on ne note la justesse que sur une note posée.
                const int step = (int) (0.01 * take.fs);
                const double before = 12.0 * std::log2 ((double) take.f0[(size_t) (i - step)] / (double) take.f0[(size_t) i]);
                const double after = 12.0 * std::log2 ((double) take.f0[(size_t) (i + step)] / (double) take.f0[(size_t) i]);
                if (std::abs (before) > 0.3 || std::abs (after) > 0.3)
                    continue;

                ++settled;
                const double truth = 69.0 + 12.0 * std::log2 ((double) take.f0[(size_t) i] / 440.0);
                const double cents = 100.0 * (r.midi - truth);
                if (std::abs (cents) > 50.0)
                {
                    ++gross;
                    if (cents > 0.0)
                        ++tooHigh;
                    for (double interval : { 1200.0, 1902.0, 2400.0 })
                        if (std::abs (std::abs (cents) - interval) < 80.0)
                            ++octave;
                }
                else
                {
                    fine += std::abs (cents);
                    ++fineCount;
                }
            }
        }
        else if (allQuiet)
        {
            ++quiet;
            if (r.voiced)
                ++alarms;
        }
    }

    Score score;
    score.frames = notes;
    score.recall = 100.0 * heard / std::max (1, notes);
    score.falseAlarm = 100.0 * alarms / std::max (1, quiet);
    score.gross = 100.0 * gross / std::max (1, settled);
    score.octave = 100.0 * octave / std::max (1, settled);
    score.tooHigh = 100.0 * tooHigh / std::max (1, settled);
    score.fineCents = fine / std::max (1, fineCount);
    return score;
}

} // namespace voicemodel
