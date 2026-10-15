#include "PitchCorrector.h"

#include <algorithm>
#include <cmath>

namespace onirique::tune
{

namespace
{
    constexpr double hangSeconds = 0.06;        // trou de voisement toléré sans repartir de zéro
    constexpr double twoPi = 6.283185307179586;

    // Hard Tune : la note suit la voix instantanée ; un peu d'hystérésis
    // évite de papillonner entre deux notes sans effacer les sauts typiques.
    constexpr double hardHysteresis = 0.12;

    // Accordage naturel
    constexpr double quickHz = 25.0;            // lissage léger de la hauteur mesurée
    constexpr double centreSlowHz = 2.2;        // centre de la note : ôte un vibrato de 4 à 8 Hz
    constexpr double centreFastHz = 25.0;       // ... mais rattrape vite un vrai changement de note
    constexpr double fastFrom = 0.7, fastTo = 1.4;           // demi-tons d'écart : au-delà, ce n'est plus du vibrato
    constexpr double freezeFrom = 20.0, freezeTo = 60.0;      // demi-tons / s : la voix glisse, la correction se fige
    constexpr double settleFrom = 0.10, settleTo = 0.30;      // s : une note devient "tenue"
    constexpr double settleMs = 15.0;
    constexpr double shiftFrom = 0.15;          // demi-tons : écart au centre qui compte pour un déplacement
    constexpr double shiftAfter = 0.11, shiftSure = 0.18;     // s : un vibrato change de côté avant
    constexpr double shiftHz = 12.0;            // le centre rejoint alors la voix à cette vitesse...
    constexpr double caughtUp = 0.04;           // ... jusqu'à être tout près d'elle
    constexpr double restingSpeed = 2.0;        // demi-tons / s : la voix ne bouge plus...
    constexpr double restAfter = 0.025, restSure = 0.045;       // ... depuis assez longtemps pour ne pas être un vibrato
    constexpr double naturalHysteresis = 0.15;
    constexpr double confirmSeconds = 0.02;     // une nouvelle note doit se confirmer
    constexpr double leapSemitones = 1.5;       // ... sauf un saut franc
    constexpr double edgeMargin = 0.0;         // demi-tons avant le milieu de deux notes : la correction s'efface
    constexpr double naturalTauMs = 8.0;       // même à Retune 0, un changement de note se fait en glissant
    constexpr double humanizeMs = 150.0;
    constexpr double expressionFrom = 0.3, expressionTo = 0.8;   // demi-tons autour du centre : gardés tels quels, puis ramenés

    double smoothStep (double lo, double hi, double x) noexcept
    {
        if (x <= lo) return 0.0;
        if (x >= hi) return 1.0;
        const double t = (x - lo) / (hi - lo);
        return t * t * (3.0 - 2.0 * t);
    }

    /** Coefficient d'un filtre à un pôle, par constante de temps en ms. */
    double follow (double dt, double tauMs) noexcept
    {
        return tauMs < 0.05 ? 1.0 : 1.0 - std::exp (-dt * 1000.0 / tauMs);
    }

    /** Coefficient d'un filtre à un pôle, par fréquence de coupure. */
    double followHz (double dt, double hz) noexcept
    {
        return 1.0 - std::exp (-twoPi * hz * dt);
    }

    /** Le centre suit lentement tant que l'écart ressemble à du vibrato, et
        accélère quand la voix est clairement partie ailleurs. */
    double centreRate (double deviation) noexcept
    {
        const double x = smoothStep (fastFrom, fastTo, std::abs (deviation));
        return centreSlowHz * std::pow (centreFastHz / centreSlowHz, x);
    }

    bool inMask (int note, std::uint16_t mask) noexcept
    {
        return (mask & (1u << (((note % 12) + 12) % 12))) != 0;
    }

    bool allowed (int note, std::uint16_t mask) noexcept
    {
        return (mask & 0x0fff) == 0 || inMask (note, mask);
    }

    /** Moitié de l'écart entre une note et sa voisine dans la gamme, du côté
        où se trouve la voix : c'est là que la voix est à mi-chemin. */
    double halfGap (int note, double voice, std::uint16_t mask) noexcept
    {
        const int direction = voice >= (double) note ? 1 : -1;
        for (int step = 1; step <= 12; ++step)
            if (allowed (note + direction * step, mask))
                return 0.5 * (double) step;

        return 6.0;
    }

    /** Flex-Tune : au-delà d'une certaine distance à la note, la voix reste libre. */
    double flexWeight (double distance, float flex) noexcept
    {
        if (flex <= 0.001f)
            return 1.0;

        const double f = flex > 1.0f ? 1.0 : (double) flex;
        const double inner = std::pow (1.0 - f, 1.5) + 0.06 * f;
        const double outer = inner * 1.5 + 0.1;
        return 1.0 - smoothStep (inner, outer, std::abs (distance));
    }
}

void PitchCorrector::reset() noexcept
{
    active = false;
    wasHard = false;
    silentFor = 1.0e9;
    target = 0;
    held = pending = 0.0;
    quick = centreA = centre = motion = 0.0;
    shifted = resting = shift = sustain = steady = 0.0;
    historyIndex = historyCount = 0;
    catching = false;
    side = 1;
    smoothA = correction = glide = 0.0;
}

int PitchCorrector::nearestNote (double midi, std::uint16_t mask) noexcept
{
    const int base = (int) std::floor (midi + 0.5);
    if ((mask & 0x0fff) == 0)
        return base;

    int best = base;
    double bestDistance = 1.0e9;
    for (int note = base - 6; note <= base + 6; ++note)
    {
        if (! inMask (note, mask))
            continue;

        const double distance = std::abs (midi - (double) note);
        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = note;
        }
    }

    return best;
}

PitchCorrector::Output PitchCorrector::process (bool voiced, double frequencyHz, double dt, const CorrectorParams& p) noexcept
{
    if (! voiced || frequencyHz <= 0.0)
    {
        silentFor += dt;
        if (silentFor > hangSeconds)
            active = false;

        return { false, lastIn, lastOut, 0.0f, lastNote };
    }

    const double pitch = 69.0 + 12.0 * std::log2 (frequencyHz / p.a4);
    const double wanted = pitch + (double) p.transpose;

    if (! active)
    {
        // Nouvelle phrase : on repart de la voix telle qu'elle arrive.
        active = true;
        target = nearestNote (wanted, p.noteMask);
        held = pending = 0.0;
        quick = centreA = centre = wanted;
        motion = shifted = resting = shift = sustain = steady = 0.0;
        historyIndex = historyCount = 0;
        catching = false;
        side = 1;
        smoothA = correction = 0.0;
        glide = (double) target;
        wasHard = p.hard;
    }

    silentFor = 0.0;

    quick += followHz (dt, quickHz) * (wanted - quick);

    // Vitesse de la voix sur une vingtaine de millisecondes : assez court
    // pour suivre un vibrato, assez long pour ignorer la gigue du détecteur.
    const double past = history[(size_t) historyIndex];
    history[(size_t) historyIndex] = quick;
    historyIndex = (historyIndex + 1) % (int) history.size();
    historyCount = std::min (historyCount + 1, (int) history.size());
    motion = historyCount == (int) history.size() ? std::abs (quick - past) / (dt * (double) history.size()) : 0.0;

    // La voix reste du même côté de son centre plus longtemps qu'un demi-cycle
    // de vibrato : elle s'est déplacée, le centre doit la rejoindre.
    const double away = quick - centre;
    if (std::abs (away) > shiftFrom && (away > 0.0) == (side > 0))
        shifted += dt;
    else
        shifted = 0.0;

    // Ou bien elle s'est posée ailleurs : immobile, loin du centre (un
    // vibrato ne s'arrête jamais assez longtemps à l'un de ses sommets).
    if (std::abs (away) > shiftFrom && motion < restingSpeed)
        resting += dt;
    else
        resting = 0.0;

    side = away > 0.0 ? 1 : -1;
    const double detected = std::max (smoothStep (shiftAfter, shiftSure, shifted), smoothStep (restAfter, restSure, resting));

    // Une fois le déplacement reconnu, le centre court après la voix jusqu'à
    // l'avoir rattrapée : un centre resté un peu en retard fausserait la note.
    if (detected >= 1.0)
        catching = true;
    else if (std::abs (away) < caughtUp)
        catching = false;

    shift = catching ? 1.0 : detected;

    // Centre de la note : deux filtres en cascade dont la vitesse s'adapte à
    // l'écart (lents sur un vibrato, rapides sur un changement de note).
    const double boost = centreSlowHz + shift * (shiftHz - centreSlowHz);
    centreA += followHz (dt, std::max (boost, centreRate (wanted - centreA))) * (wanted - centreA);
    centre += followHz (dt, std::max (boost, centreRate (centreA - centre))) * (centreA - centre);

    // Changement de mode en pleine note : on reprend là où en est l'autre.
    if (p.hard != wasHard)
    {
        if (p.hard)
            glide = wanted + correction;
        else
            smoothA = correction = glide - wanted;

        wasHard = p.hard;
    }

    return p.hard ? hard (pitch, wanted, dt, p) : natural (pitch, wanted, dt, p);
}

PitchCorrector::Output PitchCorrector::hard (double pitch, double wanted, double dt, const CorrectorParams& p) noexcept
{
    const int candidate = nearestNote (wanted, p.noteMask);
    if (candidate != target
        && (! allowed (target, p.noteMask)
            || std::abs (wanted - (double) candidate) + hardHysteresis < std::abs (wanted - (double) target)))
    {
        target = candidate;
        held = 0.0;
    }

    held += dt;
    pending = 0.0;

    // La sortie est la note elle-même : plate, et le saut d'une note à
    // l'autre ne dure que ce que Retune Speed lui accorde.
    glide += follow (dt, (double) p.retuneMs / 4.0) * ((double) target - glide);

    // Garde l'accordage naturel prêt à reprendre sans saut.
    smoothA = correction = glide - wanted;

    return finish (pitch, glide, 1.0f);
}

PitchCorrector::Output PitchCorrector::natural (double pitch, double wanted, double dt, const CorrectorParams& p) noexcept
{
    // La voix est-elle partie franchement loin de son centre ? Alors le centre
    // est en retard sur elle : on ne s'y fie plus le temps qu'il la rejoigne.
    const double far = smoothStep (fastFrom, fastTo, std::abs (quick - centre));
    if (far > 0.5 || shift > 0.5)
        steady = 0.0;
    steady += dt;

    // La note se juge sur le centre dès qu'il est établi (un vibrato, même
    // large, ne la fait pas basculer) ; au début d'une phrase ou d'un
    // déplacement, sur la voix à peine lissée.
    const double onCentre = smoothStep (settleFrom, settleTo, steady) * (1.0 - far);
    const double judged = onCentre * centre + (1.0 - onCentre) * quick;

    const int candidate = nearestNote (judged, p.noteMask);
    bool switched = false;
    if (candidate != target)
    {
        const double fromTarget = std::abs (judged - (double) target);
        if (! allowed (target, p.noteMask))
        {
            switched = true;
        }
        else if (std::abs (judged - (double) candidate) + naturalHysteresis < fromTarget)
        {
            pending += dt;
            switched = pending >= confirmSeconds || fromTarget > leapSemitones;
        }
        else
        {
            pending = 0.0;
        }
    }
    else
    {
        pending = 0.0;
    }

    if (switched)
    {
        target = candidate;
        held = pending = 0.0;
    }

    held += dt;

    // Confiance dans le centre : nulle à l'attaque d'une note ou quand la voix
    // vient de se déplacer, entière sur une note tenue.
    const double trust = smoothStep (settleFrom, settleTo, held) * (1.0 - shift) * (1.0 - far);
    sustain += follow (dt, settleMs) * (trust - sustain);

    // Ce que la correction vise : l'écart instantané quand le centre n'est pas
    // encore connu (la note tombe juste tout de suite), l'écart du centre sur
    // une note tenue (vibrato et micro-variations restent ceux du chanteur).
    // Un écart qui dépasse un vibrato (la voix part vers une note voisine
    // puis revient) est ramené lui aussi, d'autant plus qu'il est grand.
    const double instant = (double) target - wanted;
    const double centred = (double) target - centre;
    const double keep = sustain * (1.0 - smoothStep (expressionFrom, expressionTo, std::abs (wanted - centre)));
    const double aim = (1.0 - keep) * instant + keep * centred;
    const double toNote = (double) target - judged;

    // Une voix posée à mi-chemin entre deux notes n'est pas tirée vers l'une
    // ou l'autre : la correction s'y efface, sans saut quand la note bascule.
    const double half = halfGap (target, judged, p.noteMask);
    const double edge = 1.0 - smoothStep (half - edgeMargin, half + naturalHysteresis, std::abs (toNote));
    const double weight = edge * flexWeight (toNote, p.flex);

    // Recalage à deux pôles : un arrondi au départ et à l'arrivée, jamais
    // d'angle ; ce qui bouge plus vite que lui (inflexions, attaques) reste
    // celui du chanteur. Humanize le ralentit sur les notes tenues. Pendant
    // un glissé rapide la correction se fige : le glissé garde sa forme, sans
    // marches d'escalier, et la correction reprend dès que la voix se pose.
    const double tauMs = naturalTauMs + 0.3 * (double) p.retuneMs + (double) p.humanize * sustain * humanizeMs;
    const double alpha = follow (dt, 0.5 * tauMs) * (1.0 - smoothStep (freezeFrom, freezeTo, motion));
    smoothA += alpha * (weight * aim - smoothA);
    correction += alpha * (smoothA - correction);

    // Natural Vibrato : ce qui bouge autour du centre, atténué ou accentué.
    const double expression = wanted - centre;
    const double vibrato = (double) p.vibrato * keep * weight * expression;

    return finish (pitch, wanted + correction + vibrato, 0.0f);
}

PitchCorrector::Output PitchCorrector::finish (double pitch, double outMidi, float pin) noexcept
{
    lastIn = (float) pitch;
    lastOut = (float) outMidi;
    lastNote = (float) target;
    return { true, lastIn, lastOut, pin, lastNote };
}

} // namespace onirique::tune
