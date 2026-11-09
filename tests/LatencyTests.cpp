#include "ProEngine.h"
#include <array>
#include <deque>
#include <iostream>
#include <stdexcept>
using namespace bte;
namespace
{
int checks = 0;
void check(bool pass, const char* label)
{
    ++checks;
    if (!pass)
        throw std::runtime_error(label);
    std::cout << "PASS " << label << std::endl;
}
void ok(juce::Result r)
{
    if (r.failed())
        throw std::runtime_error(r.getErrorMessage().toStdString());
}
void pump(int ms = 100)
{
    juce::MessageManager::getInstance()->runDispatchLoopUntil(ms);
}
class LatencyFixture final : public te::Plugin
{
  public:
    inline static const char* xmlTypeName = "bteLatencyFixture";
    explicit LatencyFixture(te::PluginCreationInfo i) : Plugin(i) {}
    ~LatencyFixture() override
    {
        notifyListenersOfDeletion();
    }
    juce::String getName() const override
    {
        return "128-sample test delay";
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
        return BusLayout::singleStereoInOut();
    }
    int getNumOutputChannelsGivenInputs(int) override
    {
        return 2;
    }
    double getLatencySeconds() override
    {
        return 128. / rate;
    }
    void initialise(const te::PluginInitialisationInfo& i) override
    {
        rate = i.sampleRate;
        for (auto& c : delay)
            c.fill(0);
        position = 0;
    }
    void deinitialise() override {}
    void applyToBuffer(const te::PluginRenderContext& rc) override
    {
        if (!rc.destBuffer)
            return;
        for (int n = 0; n < rc.bufferNumSamples; ++n)
        {
            for (int c = 0; c < std::min(2, rc.destBuffer->getNumChannels()); ++c)
            {
                auto* data = rc.destBuffer->getWritePointer(c) + rc.bufferStartSample;
                float in = data[n];
                data[n] = delay[static_cast<size_t>(c)][position];
                delay[static_cast<size_t>(c)][position] = in;
            }
            position = (position + 1) % 128;
        }
    }

  private:
    double rate = 48000;
    std::array<std::array<float, 128>, 2> delay{};
    size_t position = 0;
};
struct Case
{
    double rate;
    int block, input, output;
    bool graph = false, calibrate = false, disableCompensation = false;
};
} // namespace
int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    auto root = juce::File::getSpecialLocation(juce::File::tempDirectory)
                    .getNonexistentChildFile("bte-latency", "", false);
    root.createDirectory();
    try
    {
        const Case cases[]{{48000, 64, 0, 0},
                           {44100, 64, 64, 96},
                           {48000, 64, 128, 192},
                           {48000, 128, 256, 128},
                           {96000, 256, 96, 160},
                           {48000, 128, 128, 192, true},
                           {48000, 128, 128, 192, false, true},
                           {48000, 128, 128, 192, false, false, true}};
        int repeats = 1;
        if (argc != 1)
        {
            if (argc != 3 || juce::String(argv[1]) != "--repeat")
                throw std::runtime_error("Use --repeat 1..100");
            repeats = juce::String(argv[2]).getIntValue();
            if (repeats < 1 || repeats > 100)
                throw std::runtime_error("Repeat count must be 1..100");
        }
        for (int repeat = 0; repeat < repeats; ++repeat)
            for (size_t ci = 0; ci < std::size(cases); ++ci)
            {
                auto test = cases[ci];
                Workstation ws(false, root.getChildFile(juce::String(repeat) + "-" +
                                                        juce::String(static_cast<int>(ci))));
                static_cast<HostBehaviour&>(ws.engine.getEngineBehaviour()).audioCpuLimit = 1;
                ws.engine.getPluginManager().createBuiltInType<LatencyFixture>();
                auto& dm = ws.engine.getDeviceManager();
                auto& io = dm.getHostedAudioDeviceInterface();
                te::HostedAudioDeviceInterface::Parameters p;
                p.sampleRate = test.rate;
                p.blockSize = test.block;
                p.inputChannels = p.outputChannels = 2;
                p.inputLatencyNumSamples = test.input;
                p.outputLatencyNumSamples = test.output;
                io.initialise(p);
                io.prepareToPlay(test.rate, test.block);
                dm.dispatchPendingUpdates();
                dm.setCpuLimitBeforeMuting(1000);
                ws.configureInputs();
                ws.edit->setCountInMode(te::Edit::CountIn::none);
                auto source = root.getChildFile("reference-" + juce::String(static_cast<int>(ci)) + ".wav");
                juce::WavAudioFormat wav;
                std::unique_ptr<juce::OutputStream> stream = source.createOutputStream();
                auto writer = wav.createWriterFor(stream, juce::AudioFormatWriterOptions{}
                                                              .withSampleRate(test.rate)
                                                              .withNumChannels(1)
                                                              .withBitsPerSample(24));
                check(writer != nullptr, "reference impulse writer");
                juce::AudioBuffer<float> referenceAudio(1, static_cast<int>(test.rate * .7));
                referenceAudio.clear();
                referenceAudio.setSample(0, static_cast<int>(std::llround(test.rate * .25)), .75f);
                writer->writeFromAudioSampleBuffer(referenceAudio, 0, referenceAudio.getNumSamples());
                writer.reset();
                ok(ws.importAudio(source));
                ws.addTrack("Recording");
                if (test.graph)
                {
                    auto plugin = ws.edit->getPluginCache().createNewPlugin(LatencyFixture::xmlTypeName, {});
                    check(plugin != nullptr, "latency DSP fixture created");
                    ws.edit->getMasterPluginList().insertPlugin(plugin, 0, nullptr);
                }
                ws.configureInputs();
                pump(300);
                ws.edit->getTransport().ensureContextAllocated();
                ws.edit->dispatchPendingUpdatesSynchronously();
                pump();
                auto latency = ws.latency();
                check(latency.available && latency.sampleRate == test.rate &&
                          latency.bufferSamples == test.block,
                      "device rate and buffer readout");
                check(latency.inputSamples == test.input && latency.outputSamples == test.output,
                      "reported input/output latency readout");
                if (test.graph)
                    check(latency.graphKnown && latency.graphSamples >= 128, "graph reports real DSP delay");
                int input = -1;
                auto devs = ws.edit->getAllInputDevices();
                for (int n = 0; n < devs.size(); ++n)
                    if (devs[n]->getInputDevice().getDeviceType() == te::InputDevice::waveDevice)
                    {
                        input = n;
                        break;
                    }
                check(input >= 0, "recording input available");
                ok(ws.assignInput(1, input, true));
                devs[input]->getInputDevice().setMonitorMode(te::InputDevice::MonitorMode::off);
                auto id = devs[input]->getInputDevice().getDeviceID();
                int residual = test.calibrate ? 17 : 0;
                double correction =
                    test.disableCompensation
                        ? -1000. * (test.input + test.output + latency.graphSamples) / test.rate
                        : 1000. * residual / test.rate;
                ok(ws.setRecordingCorrection(id, correction));
                check(!ws.recordingSources().empty(), "recording correction source enumerated");
                check(ws.setRecordingCorrection(id, 501).failed(), "invalid recording correction refused");
                ws.seek(0);
                ok(ws.record());
                check(ws.setRecordingCorrection(id, 1).failed(), "alignment cannot change during recording");
                check(ws.useSmallBuffer().failed(), "buffer cannot change during recording");
                int reference = static_cast<int>(std::llround(test.rate * .25));
                // Model a causal wire: callback input receives a previous output block.
                // Hosted-device processors add the configured driver input/output delay.
                std::deque<std::array<float, 2>> loopback(static_cast<size_t>(test.block + residual), {0, 0});
                int frames = static_cast<int>(test.rate * .65),
                    blocks = (frames + test.block - 1) / test.block;
                juce::AudioBuffer<float> block(2, test.block);
                juce::MidiBuffer midi;
                for (int b = 0; b < blocks; ++b)
                {
                    for (int n = 0; n < test.block; ++n)
                    {
                        auto inputSample = loopback.front();
                        loopback.pop_front();
                        for (int ch = 0; ch < 2; ++ch)
                            block.setSample(ch, n, inputSample[static_cast<size_t>(ch)]);
                    }
                    io.processBlock(block, midi);
                    for (int n = 0; n < test.block; ++n)
                        loopback.push_back({block.getSample(0, n), block.getSample(1, n)});
                    if (b % 16 == 0)
                        pump(1);
                }
                ws.stop();
                pump(200);
                check(ws.track(1)->getClips().size() == 1, "recorded impulse clip created");
                auto* clip = dynamic_cast<te::WaveAudioClip*>(ws.track(1)->getClips()[0]);
                check(clip != nullptr, "recorded impulse is audio");
                juce::AudioFormatManager formats;
                formats.registerBasicFormats();
                std::unique_ptr<juce::AudioFormatReader> reader(
                    formats.createReaderFor(clip->getOriginalFile()));
                check(reader != nullptr, "recorded impulse file decodes");
                juce::AudioBuffer<float> data(1, static_cast<int>(reader->lengthInSamples));
                reader->read(&data, 0, data.getNumSamples(), 0, true, false);
                int peak = 0;
                for (int n = 1; n < data.getNumSamples(); ++n)
                    if (std::abs(data.getSample(0, n)) > std::abs(data.getSample(0, peak)))
                        peak = n;
                check(std::abs(data.getSample(0, peak)) > .7f, "injected impulse survives recording");
                auto position = clip->getPosition();
                int timeline = static_cast<int>(std::llround(
                    position.getStart().inSeconds() * test.rate +
                    (peak - position.getOffset().inSeconds() * test.rate) / clip->getSpeedRatio()));
                int error = timeline - reference;
                int expected = test.disableCompensation ? test.input + test.output + latency.graphSamples : 0;
                std::cout << "ALIGNMENT repeat=" << repeat << " rate=" << test.rate << " block=" << test.block
                          << " input=" << test.input << " output=" << test.output
                          << " graph=" << latency.graphSamples << " correction_ms=" << correction
                          << " measured_error_samples=" << error << " expected=" << expected << std::endl;
                check(std::abs(error - expected) <= 1,
                      "recording alignment within one sample of expected result");
                reader.reset();
                ok(ws.setRecordingCorrection(id, 0));
                ok(ws.useSmallBuffer());
                auto small = ws.latency();
                check(small.available && small.sampleRate == test.rate && small.bufferSamples <= 256,
                      "small buffer setup preserves sample rate and uses supported size");
                ws.edit.reset();
                dm.deviceManager.closeAudioDevice();
                dm.removeHostedAudioDeviceInterface();
            }
        root.deleteRecursively();
        std::cout << checks << " recording latency checks passed\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "FAIL: " << e.what() << "\nArtifacts: " << root.getFullPathName() << std::endl;
        return 1;
    }
}
