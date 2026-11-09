#include "ProEngine.h"
namespace bte
{
AudioLatency Workstation::latency() const
{
    AudioLatency result;
    if (auto* d = engine.getDeviceManager().deviceManager.getCurrentAudioDevice())
    {
        result.available = true;
        result.device = d->getName();
        result.driver = d->getTypeName();
        result.sampleRate = d->getCurrentSampleRate();
        result.bufferSamples = d->getCurrentBufferSizeSamples();
        result.inputSamples = d->getInputLatencyInSamples();
        result.outputSamples = d->getOutputLatencyInSamples();
    }
    if (edit)
        if (auto* context = edit->getCurrentPlaybackContext())
        {
            result.graphKnown = true;
            result.graphSamples = context->getLatencySamples();
        }
    return result;
}
juce::Result Workstation::useSmallBuffer()
{
    if (edit && edit->getTransport().isRecording())
        return juce::Result::fail("Stop recording before changing the audio buffer");
    auto& dm = audioDevices();
    auto* device = dm.getCurrentAudioDevice();
    if (!device)
        return juce::Result::fail(
            "Choose your audio device first. For Apollo, select ASIO and the Universal Audio driver.");
    auto sizes = device->getAvailableBufferSizes();
    sizes.sort();
    int chosen = 0;
    // Prefer 64 frames, then the smallest supported size above it. Never request an unsupported size.
    for (int size : sizes)
        if (size >= 64 && size <= 256)
        {
            chosen = size;
            break;
        }
    if (chosen == 0)
        for (int i = sizes.size(); --i >= 0;)
            if (sizes[i] > 0 && sizes[i] < 64)
            {
                chosen = sizes[i];
                break;
            }
    if (chosen == 0)
        return juce::Result::fail("This driver offers no buffer of 256 frames or less. Open its control "
                                  "panel or choose a native ASIO driver.");
    stop();
    juce::AudioDeviceManager::AudioDeviceSetup setup;
    dm.getAudioDeviceSetup(setup);
    setup.bufferSize = chosen;
    auto error = dm.setAudioDeviceSetup(setup, true);
    if (error.isNotEmpty())
        return juce::Result::fail(error);
    engine.getDeviceManager().dispatchPendingUpdates();
    configureInputs();
    auto now = latency();
    if (!now.available)
        return juce::Result::fail("The device did not restart; reopen Audio settings");
    status = "Requested " + juce::String(chosen) + " frames; driver active at " +
             juce::String(now.bufferSamples) + ". Increase the buffer if you hear crackles.";
    return juce::Result::ok();
}
std::vector<RecordingSource> Workstation::recordingSources() const
{
    std::vector<RecordingSource> result;
    for (auto* d : engine.getDeviceManager().getWaveInputDevices())
        if (!d->isTrackDevice() && d->isEnabled())
            result.push_back({d->getDeviceID(), d->getName(), d->getRecordAdjustmentMs()});
    return result;
}
juce::Result Workstation::setRecordingCorrection(const juce::String& id, double value)
{
    if (!std::isfinite(value) || value < -500 || value > 500)
        return juce::Result::fail("Recording correction must be between -500 and +500 ms");
    if (edit && edit->getTransport().isRecording())
        return juce::Result::fail("Stop recording before changing recording alignment");
    for (auto* d : engine.getDeviceManager().getWaveInputDevices())
        if (!d->isTrackDevice() && d->getDeviceID() == id)
        {
            d->setRecordAdjustmentMs(value);
            status = "Recording alignment correction set. Monitoring delay is unchanged.";
            return juce::Result::ok();
        }
    return juce::Result::fail("That recording source is no longer available");
}
} // namespace bte
