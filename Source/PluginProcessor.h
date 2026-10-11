#pragma once

#include "DSP/TuneEngine.h"
#include "License/DemoNoise.h"

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

    /** Latence déclarée à l'hôte avec les réglages actuels, en millisecondes. */
    double getLatencyMs() const noexcept;

    /** Le vocodeur est-il en marche ? (pour l'affichage) */
    bool isVocoderOn() const noexcept { return vocoder->load() > 0.5f; }

    /** Taille de l'interface (1 = taille de base), mémorisée avec le projet. */
    float getEditorScale() const noexcept { return editorScale.load(); }
    void setEditorScale (float newScale) noexcept { editorScale.store (newScale); }

    /** État de la licence sur ce PC (acheté, essai, démo). À lire depuis l'interface. */
    onirique::license::Status getLicense() const;

    /** Relit le fichier de licence écrit par ONIRIQUE CENTRAL. Jamais depuis le thread audio. */
    void refreshLicense();

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
    std::array<std::atomic<float>*, 12> noteToggles {};
    juce::RangedAudioParameter* bypassParam = nullptr;

    std::atomic<bool> clearTogglesPending { false };
    std::atomic<bool> restoringState { false };
    std::atomic<float> editorScale { 1.0f };

    // Licence : l'état complet pour l'interface, et le bruit du mode démo pour le thread audio.
    juce::CriticalSection licenseLock;
    onirique::license::Status licenseStatus;
    onirique::license::DemoNoise demoNoise;
    int licenseTicks = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OniriqueTunerProcessor)
};
