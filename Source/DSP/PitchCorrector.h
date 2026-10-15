#pragma once

#include <array>
#include <cstdint>

namespace onirique::tune
{

struct CorrectorParams
{
    float retuneMs = 20.0f;      // vitesse de recalage (0 = immédiat)
    float flex = 0.0f;           // 0..1 : ne corrige qu'à l'approche de la note
    float humanize = 0.0f;       // 0..1 : relâche la correction sur les notes tenues
    float vibrato = 0.0f;        // -1..1 : atténue ou accentue le vibrato naturel
    float transpose = 0.0f;      // demi-tons
    double a4 = 440.0;           // diapason
    std::uint16_t noteMask = 0x0fff;
    bool hard = false;           // Hard Tune : notes plates et sauts francs, l'effet robot
};

/** Décide, trame après trame, de la hauteur à faire entendre.

    Deux façons d'accorder :

    - Naturelle (par défaut). La hauteur chantée est séparée en deux : son
      centre (la note que vise le chanteur, suivie lentement) et ce qui bouge
      autour (vibrato, micro-variations, attaques). Sur une note tenue, seul
      le centre est ramené sur la note : le vibrato et le grain de la voix
      restent intacts. À l'attaque d'une note, avant que ce centre soit connu,
      c'est l'écart instantané qui est corrigé, à la vitesse de recalage ; le
      passage de l'un à l'autre se fait en douceur, comme un vibrato qui
      s'installe. Les glissés d'une note à l'autre passent sans être corrigés.

    - Hard Tune. La sortie est exactement la note de la gamme, plate, et saute
      d'une note à l'autre sans transition : l'effet robot. Retune Speed règle
      seulement la netteté des sauts. */
class PitchCorrector
{
public:
    struct Output
    {
        bool voiced = false;
        float inMidi = 0.0f;
        float outMidi = 0.0f;

        /** 1 = la sortie doit valoir exactement outMidi (Hard Tune) ; 0 = la
            correction est une simple transposition et la voix garde ses
            micro-variations. */
        float pin = 0.0f;

        /** Note visée dans la gamme, transposition comprise. */
        float noteMidi = 0.0f;
    };

    void reset() noexcept;
    Output process (bool voiced, double frequencyHz, double dt, const CorrectorParams&) noexcept;

    static int nearestNote (double midi, std::uint16_t mask) noexcept;

private:
    Output natural (double pitch, double wanted, double dt, const CorrectorParams&) noexcept;
    Output hard (double pitch, double wanted, double dt, const CorrectorParams&) noexcept;
    Output finish (double pitch, double outMidi, float pin) noexcept;

    bool active = false;
    bool wasHard = false;
    double silentFor = 1.0e9;
    int target = 0;
    double held = 0.0;           // depuis quand la note visée n'a pas changé
    double pending = 0.0;        // depuis quand une autre note se présente

    double quick = 0.0;          // hauteur à peine lissée (gigue du détecteur ôtée)
    double centreA = 0.0, centre = 0.0;   // centre de la note : vibrato ôté
    std::array<double, 8> history {};    // dernières valeurs de `quick`
    int historyIndex = 0, historyCount = 0;
    double motion = 0.0;         // vitesse de la voix, en demi-tons par seconde
    double shifted = 0.0, resting = 0.0, shift = 0.0;    // la voix s'est déplacée par rapport au centre
    int side = 1;
    bool catching = false;
    double steady = 0.0;         // depuis quand le centre suit la voix sans retard
    double sustain = 0.0;        // confiance dans le centre (note tenue)
    double smoothA = 0.0, correction = 0.0;
    double glide = 0.0;          // Hard Tune : note jouée, avec son saut

    float lastIn = 0.0f;
    float lastOut = 0.0f;
    float lastNote = 0.0f;
};

} // namespace onirique::tune
