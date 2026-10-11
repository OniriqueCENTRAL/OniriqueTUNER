#include "PitchCorrector.h"

#include <cmath>

namespace onirique::tune
{

namespace
{
    constexpr double hangSeconds = 0.06;       // trou de voisement toléré sans repartir de zéro
    constexpr double noteHysteresis = 0.12;    // demi-tons : évite de papillonner entre deux notes
    constexpr double twoPi = 6.283185307179586;

    double smoothStep (double lo, double hi, double x) noexcept
    {
        if (x <= lo) return 0.0;
        if (x >= hi) return 1.0;
        const double t = (x - lo) / (hi - lo);
        return t * t * (3.0 - 2.0 * t);
    }

    bool inMask (int note, std::uint16_t mask) noexcept
    {
        return (mask & (1u << (((note % 12) + 12) % 12))) != 0;
    }
}

void PitchCorrector::reset() noexcept
{
    active = false;
    silentFor = 1.0e9;
    target = 0;
    held = 0.0;
    correction = 0.0;
    slowPitch = 0.0;
    vibrato = 0.0;
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
        // Nouvelle phrase : la correction part de zéro et rejoint la note à
        // la vitesse de recalage, comme une attaque chantée.
        active = true;
        correction = 0.0;
        target = nearestNote (wanted, p.noteMask);
        held = 0.0;
        slowPitch = pitch;
        vibrato = 0.0;
    }

    silentFor = 0.0;

    const int candidate = nearestNote (wanted, p.noteMask);
    if (candidate != target)
    {
        const bool targetStillAllowed = (p.noteMask & 0x0fff) == 0 || inMask (target, p.noteMask);
        if (! targetStillAllowed
            || std::abs (wanted - (double) candidate) + noteHysteresis < std::abs (wanted - (double) target))
        {
            target = candidate;
            held = 0.0;
        }
    }

    held += dt;

    const double distance = (double) target - wanted;

    // Flex-Tune : au-delà d'une certaine distance, la voix reste libre.
    double weight = 1.0;
    if (p.flex > 0.001f)
    {
        const double flex = p.flex > 1.0f ? 1.0 : (double) p.flex;
        const double inner = std::pow (1.0 - flex, 1.5) + 0.06 * flex;
        const double outer = inner * 1.5 + 0.1;
        weight = 1.0 - smoothStep (inner, outer, std::abs (distance));
    }

    // Humanize : une note tenue est recalée plus doucement qu'une note brève.
    const double sustain = smoothStep (0.08, 0.35, held);
    const double tauMs = (double) p.retuneMs / 2.5 + (double) p.humanize * sustain * 110.0;
    const double alpha = tauMs < 0.05 ? 1.0 : 1.0 - std::exp (-dt * 1000.0 / tauMs);
    correction += alpha * (weight * distance - correction);

    // Vibrato naturel : écart entre la hauteur et sa moyenne lente.
    slowPitch += (1.0 - std::exp (-twoPi * 2.5 * dt)) * (pitch - slowPitch);
    double wobble = pitch - slowPitch;
    if (std::abs (wobble) > 1.0)
    {
        slowPitch = pitch;      // changement de note, pas du vibrato
        wobble = 0.0;
    }
    vibrato += (1.0 - std::exp (-twoPi * 14.0 * dt)) * (wobble - vibrato);

    const double shift = (double) p.transpose + correction + (double) p.vibrato * vibrato;

    // Tant que la correction réagit plus vite que le détecteur ne lisse la
    // voix, on impose la hauteur de sortie ; au-delà, on se contente de
    // transposer.
    const double pin = weight * (tauMs >= 8.0 ? 0.0 : 1.0 - tauMs / 8.0);

    lastIn = (float) pitch;
    lastOut = (float) (pitch + shift);
    lastNote = (float) target;
    return { true, lastIn, lastOut, (float) pin, lastNote };
}

} // namespace onirique::tune
