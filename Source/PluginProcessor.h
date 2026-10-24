#pragma once

#include "DSP/TuneEngine.h"
#include "License/LicenseGuard.h"

#include <juce_audio_processors/juce_audio_processors.h>

class OniriqueTunerProcessor final : public juce::AudioProcessor,
                                     private juce::AudioProcessorValueTreeState::Listener,
                                     private juce::Timer
{
public:
    OniriqueTunerProcessor();
    ~OniriqueTunerProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override { engine.reset(); }
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParam; }

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return true; }      // les notes déduites de la voix
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    //==============================================================================
    juce::AudioProcessorValueTreeState apvts;

    /** Mesures de hauteur destinées à l'interface (à appeler depuis elle seule). */
    bool popDisplayFrame (onirique::tune::DisplayFrame& frame) noexcept { return engine.popDisplayFrame (frame); }

    /** Notes autorisées : gamme et tonalité choisies, puis notes inversées à la main. */
    std::uint16_t getNoteMask() const noexcept;

    /** Notes ignorées (la voix qui en est le plus près passe sans correction). */
    std::uint16_t getSkipMask() const noexcept;

    /** Flex-Tune, de 0 à 1 (pour dessiner sa zone de correction). */
    float getFlex() const noexcept { return flex->load() * 0.01f; }

    /** Latence déclarée à l'hôte avec les réglages actuels, en millisecondes. */
    double getLatencyMs() const noexcept;

    /** Le vocodeur est-il en marche ? (pour l'affichage) */
    bool isVocoderOn() const noexcept { return vocoder->load() > 0.5f; }
    bool isHardTuneOn() const noexcept { return hardTune->load() > 0.5f; }
    bool isCreatedVibratoOn() const noexcept { return vibOn->load() > 0.5f; }

    /** Taille de l'interface (1 = taille de base), mémorisée avec le projet. */
    float getEditorScale() const noexcept { return editorScale.load(); }
    void setEditorScale (float newScale) noexcept { editorScale.store (newScale); }

    /** Licence ONIRIQUE (acheté, essai, démo) : voir License/LicenseGuard.h. */
    onirique::license::Guard license { "tuner" };

private:
    onirique::tune::EngineParams readParams() const noexcept;
    int wantedLatency() const noexcept;

    void parameterChanged (const juce::String& parameterId, float newValue) override;
    void timerCallback() override;

    onirique::tune::TuneEngine engine;

    std::atomic<float>* mode = nullptr;
    std::atomic<float>* input = nullptr;
    std::atomic<float>* key = nullptr;
    std::atomic<float>* scale = nullptr;
    std::atomic<float>* retune = nullptr;
    std::atomic<float>* hardTune = nullptr;
    std::atomic<float>* flex = nullptr;
    std::atomic<float>* humanize = nullptr;
    std::atomic<float>* vibrato = nullptr;
    std::atomic<float>* formant = nullptr;
    std::atomic<float>* throat = nullptr;
    std::atomic<float>* transpose = nullptr;
    std::atomic<float>* detune = nullptr;
    std::atomic<float>* tracking = nullptr;
    std::atomic<float>* mix = nullptr;
    std::atomic<float>* bypass = nullptr;
    std::atomic<float>* vocoder = nullptr;
    std::atomic<float>* voicing = nullptr;
    std::atomic<float>* blend = nullptr;
    std::atomic<float>* vibOn = nullptr;
    std::atomic<float>* vibShape = nullptr;
    std::atomic<float>* vibRate = nullptr;
    std::atomic<float>* vibDepth = nullptr;
    std::atomic<float>* vibDelay = nullptr;
    std::atomic<float>* vibOnset = nullptr;
    std::atomic<float>* vibVariation = nullptr;
    std::atomic<float>* vibAmplitude = nullptr;
    std::atomic<float>* vibTimbre = nullptr;
    std::atomic<float>* vibThreshold = nullptr;
    std::array<std::atomic<float>*, 12> noteToggles {}, noteSkips {};
    juce::RangedAudioParameter* bypassParam = nullptr;

    std::atomic<bool> clearTogglesPending { false };
    std::atomic<bool> restoringState { false };
    std::atomic<float> editorScale { 1.0f };


    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OniriqueTunerProcessor)
};
