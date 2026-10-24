#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace onirique::params
{

namespace id
{
    inline constexpr const char* mode      = "mode";        // LIVE / HQ
    inline constexpr const char* input     = "input";       // registre de la voix
    inline constexpr const char* key       = "key";
    inline constexpr const char* scale     = "scale";
    inline constexpr const char* retune    = "retune";
    inline constexpr const char* hardTune  = "hardtune";    // effet robot
    inline constexpr const char* flex      = "flex";
    inline constexpr const char* humanize  = "humanize";
    inline constexpr const char* vibrato   = "vibrato";
    inline constexpr const char* formant   = "formant";
    inline constexpr const char* throat    = "throat";
    inline constexpr const char* transpose = "transpose";
    inline constexpr const char* detune    = "detune";
    inline constexpr const char* tracking  = "tracking";
    inline constexpr const char* mix       = "mix";
    inline constexpr const char* bypass    = "bypass";
    inline constexpr const char* vocoder   = "vocoder";     // vocodeur piloté par la voix
    inline constexpr const char* voicing   = "vocVoicing";  // ce que joue son synthé
    inline constexpr const char* blend     = "vocBlend";    // voix accordée <-> vocodeur

    // Vibrato créé (bouton VIBRATO de l'en-tête et son menu)
    inline constexpr const char* vibOn        = "vibOn";
    inline constexpr const char* vibShape     = "vibShape";
    inline constexpr const char* vibRate      = "vibRate";
    inline constexpr const char* vibDepth     = "vibDepth";
    inline constexpr const char* vibDelay     = "vibDelay";
    inline constexpr const char* vibOnset     = "vibOnset";
    inline constexpr const char* vibVariation = "vibVariation";
    inline constexpr const char* vibAmplitude = "vibAmplitude";   // le volume suit le vibrato créé
    inline constexpr const char* vibTimbre    = "vibTimbre";      // l'éclat suit le vibrato créé
    inline constexpr const char* vibThreshold = "vibThreshold";   // seuil de vibrato : Wide / Narrow
}

/** Identifiant de l'interrupteur d'une note (0 = do ... 11 = si). Il inverse
    l'état que la gamme choisie donne à cette note : on peut ainsi retirer une
    note de la gamme ou en ajouter une. */
juce::String noteToggleId (int pitchClass);

/** Identifiant de « note ignorée » (0 = do ... 11 = si) : une voix dont c'est la note la plus proche passe sans
    correction. */
juce::String noteSkipId (int pitchClass);

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

} // namespace onirique::params
