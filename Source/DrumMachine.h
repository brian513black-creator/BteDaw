#pragma once
#include <array>
#include <tracktion_engine/tracktion_engine.h>
namespace bte
{
// Original percussion synthesizer: no samples, downloads, licensing server, or sound pack required.
class DrumMachine final : public tracktion::Plugin
{
  public:
    inline static const char* xmlTypeName = "bteDrumMachine";
    explicit DrumMachine(tracktion::PluginCreationInfo info) : Plugin(info)
    {
        level.referTo(state, "level", getUndoManager(), 0.7f);
        levelParameter = addParam("level", "Output level", {0.0f, 1.0f});
        levelParameter->attachToCurrentValue(level);
    }
    ~DrumMachine() override
    {
        notifyListenersOfDeletion();
        levelParameter->detachFromCurrentValue();
    }
    juce::String getName() const override
    {
        return "BTE Drum Machine";
    }
    juce::String getPluginType() override
    {
        return xmlTypeName;
    }
    juce::String getSelectableDescription() override
    {
        return getName();
    }
    int getNumOutputChannelsGivenInputs(int) override
    {
        return 2;
    }
    BusLayout getBusses() const override
    {
        return BusLayout::singleStereoInOut();
    }
    bool takesMidiInput() override
    {
        return true;
    }
    bool isSynth() override
    {
        return true;
    }
    bool producesAudioWhenNoAudioInput() override
    {
        return true;
    }
    double getTailLength() const override
    {
        return 2;
    }
    void initialise(const tracktion::PluginInitialisationInfo& info) override
    {
        sampleRate = info.sampleRate;
        outputGain.reset(sampleRate, 0.01);
        outputGain.setCurrentAndTargetValue(levelParameter->getCurrentValue());
        reset();
    }
    void deinitialise() override
    {
        reset();
    }
    void reset() override
    {
        for (auto& v : voices)
            v.active = false;
    }
    void midiPanic() override
    {
        reset();
    }
    void restorePluginStateFromValueTree(const juce::ValueTree& v) override
    {
        tracktion::copyPropertiesToCachedValues(v, level);
        levelParameter->updateFromAttachedValue();
    }
    void applyToBuffer(const tracktion::PluginRenderContext& context) override
    {
        if (!context.destBuffer)
            return;
        outputGain.setTargetValue(levelParameter->getCurrentValue());
        auto* events = context.bufferForMidiMessages;
        if (events && events->isAllNotesOff)
            reset();
        int event = 0;
        for (int n = 0; n < context.bufferNumSamples; ++n)
        {
            int frame = context.bufferStartSample + n;
            if (events)
                while (event < events->size())
                {
                    const auto& message = (*events)[event];
                    int at = juce::jlimit(context.bufferStartSample,
                                          context.bufferStartSample + context.bufferNumSamples - 1,
                                          juce::roundToInt(message.getTimeStamp() * sampleRate));
                    if (at > frame)
                        break;
                    if (message.isNoteOn())
                        trigger(message.getNoteNumber(), message.getFloatVelocity());
                    ++event;
                }
            float l = 0, r = 0;
            for (auto& voice : voices)
                if (voice.active)
                {
                    double t = voice.age / sampleRate;
                    float noise = random();
                    float high = (noise - voice.previousNoise) * 0.5f;
                    voice.previousNoise = noise;
                    double freq = voice.note == 36 || voice.note == 35 ? 45 + 110 * std::exp(-40 * t)
                                  : voice.note == 45                   ? 110
                                  : voice.note == 47                   ? 160
                                                                       : 180;
                    voice.phase += freq / sampleRate;
                    float tone =
                        std::sin(static_cast<float>(voice.phase * juce::MathConstants<double>::twoPi));
                    float value = 0;
                    if (voice.note == 35 || voice.note == 36)
                        value = tone * static_cast<float>(std::exp(-9 * t)) +
                                high * static_cast<float>(std::exp(-180 * t)) * 0.2f;
                    else if (voice.note == 38 || voice.note == 40)
                        value = (high * 0.8f + tone * 0.25f) * static_cast<float>(std::exp(-15 * t));
                    else if (voice.note == 39)
                    {
                        double burst = std::fmod(t, 0.012);
                        value = high * static_cast<float>(std::exp(-t * 18)) *
                                static_cast<float>(std::exp(-burst * 110));
                    }
                    else if (voice.note == 42 || voice.note == 44)
                        value = high * static_cast<float>(std::exp(-65 * t)) * 0.45f;
                    else if (voice.note == 46)
                        value = high * static_cast<float>(std::exp(-9 * t)) * 0.38f;
                    else if (voice.note == 49 || voice.note == 51)
                        value = high * static_cast<float>(std::exp(-3.8 * t)) * 0.45f;
                    else
                        value = tone * static_cast<float>(std::exp(-13 * t)) * 0.65f;
                    value *= voice.velocity * 0.55f;
                    float balance = (voice.note == 42 || voice.note == 46) ? 0.18f : 0;
                    l += value * (1 - balance);
                    r += value * (1 + balance);
                    if (++voice.age > static_cast<int>(sampleRate * 2.5))
                        voice.active = false;
                }
            float output = outputGain.getNextValue();
            context.destBuffer->setSample(0, frame, std::tanh(l) * output);
            if (context.destBuffer->getNumChannels() > 1)
                context.destBuffer->setSample(1, frame, std::tanh(r) * output);
        }
        if (events)
            events->clear();
    }

  private:
    struct Voice
    {
        bool active = false;
        int note = 36, age = 0;
        double phase = 0;
        float velocity = 1, previousNoise = 0;
    };
    std::array<Voice, 24> voices;
    juce::CachedValue<float> level;
    tracktion::AutomatableParameter::Ptr levelParameter;
    juce::SmoothedValue<float> outputGain;
    double sampleRate = 48000;
    uint32_t noiseState = 0x31415926;
    float random()
    {
        noiseState ^= noiseState << 13;
        noiseState ^= noiseState >> 17;
        noiseState ^= noiseState << 5;
        return static_cast<float>(noiseState) / 2147483648.0f - 1;
    }
    void trigger(int note, float velocity)
    {
        if (note < 35 || note > 81)
            return;
        auto* selected = &voices[0];
        for (auto& voice : voices)
        {
            if (!voice.active)
            {
                selected = &voice;
                break;
            }
            if (voice.age > selected->age)
                selected = &voice;
        }
        *selected = {true, note, 0, 0, velocity, 0};
        if (note == 42)
            for (auto& voice : voices)
                if (voice.note == 46)
                    voice.active = false;
    }
};
} // namespace bte
