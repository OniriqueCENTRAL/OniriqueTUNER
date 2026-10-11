#include "Parameters.h"

#include "DSP/Scales.h"
#include "DSP/TuneEngine.h"
#include "DSP/Vocoder.h"

namespace onirique::params
{

juce::String noteToggleId (int pitchClass)
{
    return "note" + juce::String (pitchClass);
}

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using Float = juce::AudioParameterFloat;
    using Choice = juce::AudioParameterChoice;
    using Bool = juce::AudioParameterBool;
    using Range = juce::NormalisableRange<float>;
    using Attributes = juce::AudioParameterFloatAttributes;

    const auto withText = [] (std::function<juce::String (float)> text)
    {
        return Attributes().withStringFromValueFunction ([text = std::move (text)] (float value, int) { return text (value); });
    };

    const auto percent = withText ([] (float v) { return juce::String (juce::roundToInt (v)) + " %"; });
    const auto signedText = [] (float v, int decimals, const char* unit)
    {
        return (v > 0.0f ? "+" : "") + juce::String (v, decimals) + unit;
    };

    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<Choice> (juce::ParameterID { id::mode, 1 }, "Mode", juce::StringArray { "LIVE", "HQ" }, 0));

    juce::StringArray inputs;
    for (const auto* name : tune::TuneEngine::inputTypeNames)
        inputs.add (name);
    layout.add (std::make_unique<Choice> (juce::ParameterID { id::input, 1 }, "Input Type", inputs, 1));

    juce::StringArray keys;
    for (const auto* name : tune::noteNames)
        keys.add (name);
    layout.add (std::make_unique<Choice> (juce::ParameterID { id::key, 1 }, "Key", keys, 0));

    juce::StringArray scaleNames;
    for (const auto& scale : tune::scales)
        scaleNames.add (scale.name);
    layout.add (std::make_unique<Choice> (juce::ParameterID { id::scale, 1 }, "Scale", scaleNames, 0));

    for (int note = 0; note < 12; ++note)
        layout.add (std::make_unique<Bool> (juce::ParameterID { noteToggleId (note), 1 },
                                            juce::String ("Toggle ") + tune::noteNames[note], false));

    layout.add (std::make_unique<Float> (juce::ParameterID { id::retune, 1 }, "Retune Speed",
                                         Range (0.0f, 400.0f, 0.1f, 0.45f), 20.0f,
                                         withText ([] (float v) { return juce::String (v, v < 10.0f ? 1 : 0) + " ms"; })));

    layout.add (std::make_unique<Float> (juce::ParameterID { id::flex, 1 }, "Flex-Tune", Range (0.0f, 100.0f, 0.1f), 0.0f, percent));
    layout.add (std::make_unique<Float> (juce::ParameterID { id::humanize, 1 }, "Humanize", Range (0.0f, 100.0f, 0.1f), 0.0f, percent));

    layout.add (std::make_unique<Float> (juce::ParameterID { id::vibrato, 1 }, "Natural Vibrato",
                                         Range (-12.0f, 12.0f, 0.1f), 0.0f,
                                         withText ([signedText] (float v) { return signedText (v, 1, ""); })));

    layout.add (std::make_unique<Bool> (juce::ParameterID { id::formant, 1 }, "Formant", false));

    layout.add (std::make_unique<Float> (juce::ParameterID { id::throat, 1 }, "Throat Length",
                                         Range (60.0f, 160.0f, 0.5f), 100.0f, percent));

    layout.add (std::make_unique<Float> (juce::ParameterID { id::transpose, 1 }, "Transpose",
                                         Range (-12.0f, 12.0f, 1.0f), 0.0f,
                                         withText ([signedText] (float v) { return signedText (v, 0, " st"); })));

    layout.add (std::make_unique<Float> (juce::ParameterID { id::detune, 1 }, "Detune",
                                         Range (-100.0f, 100.0f, 0.1f), 0.0f,
                                         withText ([] (float v) { return juce::String (440.0 * std::pow (2.0, (double) v / 1200.0), 1) + " Hz"; })));

    layout.add (std::make_unique<Float> (juce::ParameterID { id::tracking, 1 }, "Tracking", Range (0.0f, 100.0f, 0.1f), 50.0f, percent));
    layout.add (std::make_unique<Float> (juce::ParameterID { id::mix, 1 }, "Mix", Range (0.0f, 100.0f, 0.1f), 100.0f, percent));
    layout.add (std::make_unique<Bool> (juce::ParameterID { id::bypass, 1 }, "Bypass", false));

    layout.add (std::make_unique<Bool> (juce::ParameterID { id::vocoder, 1 }, "Vocoder", false));

    juce::StringArray voicings;
    for (const auto* name : tune::Vocoder::voicingNames)
        voicings.add (name);
    layout.add (std::make_unique<Choice> (juce::ParameterID { id::voicing, 1 }, "Vocoder Voicing", voicings, 0));

    layout.add (std::make_unique<Float> (juce::ParameterID { id::blend, 1 }, "Vocoder Blend", Range (0.0f, 100.0f, 0.1f), 100.0f, percent));

    return layout;
}

} // namespace onirique::params
