#pragma once
#include <tracktion_engine/tracktion_engine.h>
namespace bte
{
class InputTrim final : public tracktion::Plugin
{
  public:
    inline static const char* xmlTypeName = "bteInputTrim";
    explicit InputTrim(tracktion::PluginCreationInfo info) : Plugin(info)
    {
        db.referTo(state, "trimDb", getUndoManager(), 0.0f);
        parameter = addParam("trimDb", "Input trim (dB)", {-60.0f, 24.0f});
        parameter->attachToCurrentValue(db);
    }
    ~InputTrim() override
    {
        notifyListenersOfDeletion();
        parameter->detachFromCurrentValue();
    }
    juce::String getName() const override
    {
        return "BTE Input Trim";
    }
    juce::String getPluginType() override
    {
        return xmlTypeName;
    }
    juce::String getSelectableDescription() override
    {
        return getName();
    }
    BusLayout getBusses() const override
    {
        return BusLayout::singlePassThrough();
    }
    int getNumOutputChannelsGivenInputs(int inputs) override
    {
        return inputs;
    }
    void setVolumeDb(float value)
    {
        parameter->setParameter(std::clamp(value, -60.0f, 24.0f), juce::sendNotification);
    }
    float getVolumeDb() const
    {
        return parameter->getCurrentValue();
    }
    void initialise(const tracktion::PluginInitialisationInfo& info) override
    {
        smoother.reset(info.sampleRate, 0.01);
        smoother.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(db.get()));
    }
    void deinitialise() override {}
    void applyToBuffer(const tracktion::PluginRenderContext& context) override
    {
        if (!context.destBuffer)
            return;
        smoother.setTargetValue(juce::Decibels::decibelsToGain(parameter->getCurrentValue()));
        for (int i = 0; i < context.bufferNumSamples; ++i)
        {
            float gain = smoother.getNextValue();
            for (int c = 0; c < context.destBuffer->getNumChannels(); ++c)
                context.destBuffer->getWritePointer(c)[context.bufferStartSample + i] *= gain;
        }
    }
    void restorePluginStateFromValueTree(const juce::ValueTree& v) override
    {
        tracktion::copyPropertiesToCachedValues(v, db);
        parameter->updateFromAttachedValue();
    }

  private:
    juce::CachedValue<float> db;
    tracktion::AutomatableParameter::Ptr parameter;
    juce::SmoothedValue<float> smoother;
};
} // namespace bte
