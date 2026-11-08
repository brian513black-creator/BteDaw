#include <juce_audio_processors/juce_audio_processors.h>
// A real VST3 fixture: double the main input, or halve it when the sidechain has signal.
class TestEffect final : public juce::AudioProcessor
{
  public:
    TestEffect()
        : AudioProcessor(BusesProperties()
                             .withInput("Input", juce::AudioChannelSet::stereo(), true)
                             .withInput("Sidechain", juce::AudioChannelSet::stereo(), true)
                             .withOutput("Output", juce::AudioChannelSet::stereo(), true))
    {
    }
    const juce::String getName() const override
    {
        return "BTE Test Effect";
    }
    void prepareToPlay(double, int) override {}
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        auto main = getBusBuffer(buffer, false, 0);
        auto side = getBusBuffer(buffer, true, 1);
        bool active = side.getNumChannels() > 0 && side.getMagnitude(0, side.getNumSamples()) > 0.01f;
        main.applyGain(active ? 0.5f : 2.0f);
    }
    bool isBusesLayoutSupported(const BusesLayout& l) const override
    {
        return l.getMainInputChannelSet() == juce::AudioChannelSet::stereo() &&
               l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo() && l.inputBuses.size() == 2 &&
               l.inputBuses[1].size() <= 2;
    }
    bool acceptsMidi() const override
    {
        return false;
    }
    bool producesMidi() const override
    {
        return false;
    }
    double getTailLengthSeconds() const override
    {
        return 0;
    }
    bool hasEditor() const override
    {
        return false;
    }
    juce::AudioProcessorEditor* createEditor() override
    {
        return nullptr;
    }
    int getNumPrograms() override
    {
        return 1;
    }
    int getCurrentProgram() override
    {
        return 0;
    }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override
    {
        return "Default";
    }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock& block) override
    {
        block.append("BTE", 3);
    }
    void setStateInformation(const void*, int) override {}
};
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TestEffect;
}
