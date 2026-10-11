#pragma once

#include <cstdint>

namespace onirique::tune
{

struct CorrectorParams
{
    float retuneMs = 20.0f;      // 0 = correction instantanée
    float flex = 0.0f;           // 0..1 : ne corrige qu'à l'approche de la note
    float humanize = 0.0f;       // 0..1 : relâche la correction sur les notes tenues
    float vibrato = 0.0f;        // -1..1 : atténue ou accentue le vibrato naturel
    float transpose = 0.0f;      // demi-tons
    double a4 = 440.0;           // diapason
    std::uint16_t noteMask = 0x0fff;
};

/** Décide, trame après trame, de la hauteur à faire entendre : note cible
    dans la gamme, vitesse de recalage, Flex-Tune, Humanize et vibrato. */
class PitchCorrector
{
public:
    struct Output
    {
        bool voiced = false;
        float inMidi = 0.0f;
        float outMidi = 0.0f;

        /** 1 = la sortie doit valoir exactement outMidi (correction
            instantanée, voix "robot") ; 0 = la correction est une simple
            transposition douce et la voix garde ses micro-variations. */
        float pin = 0.0f;

        /** Note visée dans la gamme, transposition comprise. */
        float noteMidi = 0.0f;
    };

    void reset() noexcept;
    Output process (bool voiced, double frequencyHz, double dt, const CorrectorParams&) noexcept;

    static int nearestNote (double midi, std::uint16_t mask) noexcept;

private:
    bool active = false;
    double silentFor = 1.0e9;
    int target = 0;
    double held = 0.0;
    double correction = 0.0;
    double slowPitch = 0.0;
    double vibrato = 0.0;
    float lastIn = 0.0f;
    float lastOut = 0.0f;
    float lastNote = 0.0f;
};

} // namespace onirique::tune
