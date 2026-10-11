#include "PluginProcessor.h"

#include "DSP/Scales.h"
#include "Parameters.h"
#include "PluginEditor.h"

namespace ids = onirique::params::id;
using onirique::tune::TuneEngine;

OniriqueTunerProcessor::OniriqueTunerProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "ONIRIQUE_TUNER", onirique::params::createLayout())
{
    mode = apvts.getRawParameterValue (ids::mode);
    input = apvts.getRawParameterValue (ids::input);
    key = apvts.getRawParameterValue (ids::key);
    scale = apvts.getRawParameterValue (ids::scale);
    retune = apvts.getRawParameterValue (ids::retune);
    flex = apvts.getRawParameterValue (ids::flex);
    humanize = apvts.getRawParameterValue (ids::humanize);
    vibrato = apvts.getRawParameterValue (ids::vibrato);
    formant = apvts.getRawParameterValue (ids::formant);
    throat = apvts.getRawParameterValue (ids::throat);
    transpose = apvts.getRawParameterValue (ids::transpose);
    detune = apvts.getRawParameterValue (ids::detune);
    tracking = apvts.getRawParameterValue (ids::tracking);
    mix = apvts.getRawParameterValue (ids::mix);
    bypass = apvts.getRawParameterValue (ids::bypass);
    vocoder = apvts.getRawParameterValue (ids::vocoder);
    voicing = apvts.getRawParameterValue (ids::voicing);
    blend = apvts.getRawParameterValue (ids::blend);
    bypassParam = apvts.getParameter (ids::bypass);

    for (int note = 0; note < 12; ++note)
        noteToggles[(size_t) note] = apvts.getRawParameterValue (onirique::params::noteToggleId (note));

    apvts.addParameterListener (ids::key, this);
    apvts.addParameterListener (ids::scale, this);

    // La latence et les interrupteurs de notes ne se règlent que depuis le
    // thread de messages : ce minuteur y veille, jamais le thread audio.
    refreshLicense();
    startTimerHz (15);
}

OniriqueTunerProcessor::~OniriqueTunerProcessor()
{
    stopTimer();
    apvts.removeParameterListener (ids::key, this);
    apvts.removeParameterListener (ids::scale, this);
}

//==============================================================================
bool OniriqueTunerProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    return layouts.getMainInputChannelSet() == out;
}

void OniriqueTunerProcessor::prepareToPlay (double sampleRate, int)
{
    // Les réglages d'abord : le moteur démarre directement dans le bon mode.
    engine.setParams (readParams());
    engine.prepare (sampleRate);
    setLatencySamples (engine.getLatencySamples());

    // Beaucoup de DAW rappellent prepareToPlay juste avant un export : la licence est relue à ce moment-là.
    refreshLicense();
}

onirique::license::Status OniriqueTunerProcessor::getLicense() const
{
    const juce::ScopedLock lock (licenseLock);
    return licenseStatus;
}

void OniriqueTunerProcessor::refreshLicense()
{
    const auto status = onirique::license::read ("tuner");
    demoNoise.setState (status.state);

    const juce::ScopedLock lock (licenseLock);
    licenseStatus = status;
}

void OniriqueTunerProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    midi.clear();

    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    if (buffer.getNumChannels() == 0 || buffer.getNumSamples() == 0)
        return;

    engine.setParams (readParams());
    engine.process (buffer.getArrayOfWritePointers(),
                    juce::jmin (TuneEngine::maxChannels, buffer.getNumChannels()),
                    buffer.getNumSamples());

    // Vocodeur actif : les notes tirées de la voix sortent aussi en MIDI.
    const auto* events = engine.getNoteEvents();
    for (int i = 0; i < engine.getNumNoteEvents(); ++i)
    {
        const auto& e = events[i];
        midi.addEvent (e.on ? juce::MidiMessage::noteOn (1, e.note, (juce::uint8) e.velocity)
                            : juce::MidiMessage::noteOff (1, e.note),
                       juce::jlimit (0, buffer.getNumSamples() - 1, e.sampleOffset));
    }

    // Mode démo (plugin non acheté, ou essai gratuit) : voir License/DemoNoise.h.
    if (demoNoise.process (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), buffer.getNumSamples(),
                           getSampleRate(), isNonRealtime()))
        midi.clear();
}

//==============================================================================
std::uint16_t OniriqueTunerProcessor::getNoteMask() const noexcept
{
    const int scaleIndex = juce::jlimit (0, onirique::tune::numScales - 1, juce::roundToInt (scale->load()));
    const int keyIndex = juce::jlimit (0, 11, juce::roundToInt (key->load()));
    auto mask = onirique::tune::pitchClassMask (onirique::tune::scales[scaleIndex].mask, keyIndex);

    for (int note = 0; note < 12; ++note)
        if (noteToggles[(size_t) note]->load() > 0.5f)
            mask = (std::uint16_t) (mask ^ (1u << note));

    return mask;
}

onirique::tune::EngineParams OniriqueTunerProcessor::readParams() const noexcept
{
    onirique::tune::EngineParams p;
    p.hq = mode->load() > 0.5f;
    p.inputType = juce::roundToInt (input->load());
    p.noteMask = getNoteMask();
    p.retuneMs = retune->load();
    p.flex = flex->load() * 0.01f;
    p.humanize = humanize->load() * 0.01f;
    p.vibrato = vibrato->load() / 12.0f;
    p.formant = formant->load() > 0.5f;
    p.throat = throat->load() * 0.01f;
    p.transpose = transpose->load();
    p.detuneCents = detune->load();
    p.tracking = tracking->load() * 0.01f;
    p.mix = mix->load() * 0.01f;
    p.bypass = bypass->load() > 0.5f;

    p.vocoder = vocoder->load() > 0.5f;
    p.vocoderVoicing = juce::roundToInt (voicing->load());
    p.vocoderBlend = blend->load() * 0.01f;

    // Les accords du vocodeur se construisent dans la gamme choisie. Une
    // gamme sans tonalité (chromatique, par tons...) n'en dit rien : on prend
    // alors la gamme majeure de la tonique affichée.
    int count = 0;
    for (int note = 0; note < 12; ++note)
        count += (p.noteMask >> note) & 1;

    p.harmonyMask = count >= 5 && count <= 8
                      ? p.noteMask
                      : onirique::tune::pitchClassMask (onirique::tune::scales[1].mask, juce::jlimit (0, 11, juce::roundToInt (key->load())));
    return p;
}

int OniriqueTunerProcessor::wantedLatency() const noexcept
{
    const double sampleRate = getSampleRate();
    if (sampleRate <= 0.0)
        return 0;

    return TuneEngine::latencyFor (sampleRate, mode->load() > 0.5f, juce::roundToInt (input->load()));
}

double OniriqueTunerProcessor::getLatencyMs() const noexcept
{
    const double sampleRate = getSampleRate();
    return sampleRate > 0.0 ? 1000.0 * (double) wantedLatency() / sampleRate : 0.0;
}

//==============================================================================
void OniriqueTunerProcessor::parameterChanged (const juce::String&, float)
{
    // Nouvelle tonalité ou nouvelle gamme : les notes retouchées à la main
    // n'ont plus de sens, on repart de la gamme telle quelle. Sauf quand
    // c'est un projet qui se recharge : ses notes enregistrées font foi.
    if (! restoringState.load())
        clearTogglesPending.store (true);
}

void OniriqueTunerProcessor::timerCallback()
{
    if (clearTogglesPending.exchange (false))
    {
        for (int note = 0; note < 12; ++note)
        {
            if (auto* param = apvts.getParameter (onirique::params::noteToggleId (note)))
            {
                if (param->getValue() > 0.5f)
                {
                    param->beginChangeGesture();
                    param->setValueNotifyingHost (0.0f);
                    param->endChangeGesture();
                }
            }
        }
    }

    const int latency = wantedLatency();
    if (latency != getLatencySamples())
        setLatencySamples (latency);

    // Toutes les cinq secondes : une activation faite dans ONIRIQUE CENTRAL est prise en compte sans recharger le plugin.
    if (++licenseTicks >= 75)
    {
        licenseTicks = 0;
        refreshLicense();
    }
}

//==============================================================================
void OniriqueTunerProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("editorScale", (double) editorScale.load(), nullptr);

    if (const auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void OniriqueTunerProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (const auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (xml->hasTagName (apvts.state.getType()))
        {
            const auto state = juce::ValueTree::fromXml (*xml);
            editorScale.store (juce::jlimit (0.7f, 2.0f, (float) (double) state.getProperty ("editorScale", 1.0)));

            restoringState.store (true);
            apvts.replaceState (state);
            restoringState.store (false);
        }
    }
}

juce::AudioProcessorEditor* OniriqueTunerProcessor::createEditor()
{
    return new OniriqueTunerEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new OniriqueTunerProcessor();
}
