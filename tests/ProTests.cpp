#include "ProEngine.h"
#include <iostream>
#include <stdexcept>
using namespace bte;
int count = 0;
void check(bool v, const char* name)
{
    ++count;
    if (!v)
        throw std::runtime_error(name);
    std::cout << "PASS " << name << std::endl;
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
float readPeak(const juce::File& f)
{
    juce::AudioFormatManager m;
    m.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r(m.createReaderFor(f));
    check(r && r->lengthInSamples > 0, "render contains decoded samples");
    juce::AudioBuffer<float> b(2, static_cast<int>(r->lengthInSamples));
    r->read(&b, 0, b.getNumSamples(), 0, true, true);
    return b.getMagnitude(0, b.getNumSamples());
}
int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    auto root = juce::File::getSpecialLocation(juce::File::tempDirectory)
                    .getNonexistentChildFile("bte-workstation-test", "", false);
    root.createDirectory();
    try
    {
        auto source = root.getChildFile("original.wav");
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream = source.createOutputStream();
        auto writer = wav.createWriterFor(
            stream,
            juce::AudioFormatWriterOptions{}.withSampleRate(48000).withNumChannels(2).withBitsPerSample(24));
        check(writer != nullptr, "create audio fixture");
        juce::AudioBuffer<float> audio(2, 48000);
        for (int i = 0; i < audio.getNumSamples(); ++i)
        {
            float value =
                0.1f * std::sin(static_cast<float>(i) * 440.0f * juce::MathConstants<float>::twoPi / 48000);
            audio.setSample(0, i, value);
            audio.setSample(1, i, value);
        }
        writer->writeFromAudioSampleBuffer(audio, 0, audio.getNumSamples());
        writer.reset();
        {
            Workstation ws(false, root.getChildFile("Sessions"));
            auto& dm = ws.engine.getDeviceManager();
            auto& io = dm.getHostedAudioDeviceInterface();
            te::HostedAudioDeviceInterface::Parameters p;
            p.sampleRate = 48000;
            p.blockSize = 512;
            p.inputChannels = 2;
            p.outputChannels = 2;
            io.initialise(p);
            io.prepareToPlay(48000, 512);
            dm.dispatchPendingUpdates();
            dm.setCpuLimitBeforeMuting(1000);
            ws.configureInputs();
            ok(ws.importAudio(source));
            check(ws.tracks().size() == 1 && ws.track(0)->getClips().size() == 1,
                  "audio import creates a real clip");
            auto* wave = dynamic_cast<te::WaveAudioClip*>(ws.track(0)->getClips()[0]);
            check(wave && wave->getOriginalFile() != source && wave->getOriginalFile().existsAsFile(),
                  "media copied into session");
            ws.addTrack("Drum Bus", false, true);
            ok(ws.route(0, 1));
            check(ws.track(0)->getOutput().getDestinationTrack() == ws.track(1), "audio route reaches bus");
            check(ws.route(1, 0).failed(), "feedback route refused");
            ws.fader(1)->setVolumeDb(-6);
            pump();
            auto render = root.getChildFile("mix.wav");
            ok(ws.exportWav(render));
            float peak = readPeak(render);
            check(std::abs(peak - 0.1f * dbToGain(-6)) < 0.003f, "bus mix rendered at expected gain");
            ws.begin("Trim boost");
            ws.trim(0)->setVolumeDb(24);
            check(std::abs(ws.trim(0)->getVolumeDb() - 24) < 0.001, "input trim supports 24 dB boost");
            auto trimRender = root.getChildFile("trim-boost.wav");
            ok(ws.exportWav(trimRender));
            check(std::abs(readPeak(trimRender) - 0.1f * dbToGain(18)) < 0.005f,
                  "input trim gain applied in rendered audio");
            ws.trim(0)->setVolumeDb(0);
            auto stems = root.getChildFile("Stems");
            ok(ws.exportStems(stems));
            check(stems.findChildFiles(juce::File::findFiles, false, "*.wav").size() == 2,
                  "individual audio and bus stems exported");
            auto* clip = ws.track(0)->getClips()[0];
            ws.seek(0.5);
            ok(ws.split(clip));
            check(ws.track(0)->getClips().size() == 2, "split region");
            ws.undo();
            check(ws.track(0)->getClips().size() == 1, "split undo");
            auto* copy = ws.duplicate(ws.track(0)->getClips()[0]);
            check(copy && ws.track(0)->getClips().size() == 2, "duplicate region");
            ws.undo();
            check(ws.track(0)->getClips().size() == 1, "duplicate undo");
            ws.setMono(0, true);
            check(ws.mono(0), "mono mode stored");
            ws.undo();
            check(!ws.mono(0), "mono mode undo");
            ok(ws.autoLevel());
            check(ws.trim(0)->getVolumeDb() > 4 && ws.trim(0)->getVolumeDb() < 6,
                  "RMS gain staging changes trim");
            ws.undo();
            check(std::abs(ws.trim(0)->getVolumeDb()) < 0.001, "RMS gain staging undo");
            ws.automationPoint(0, 0, -12);
            ws.automationPoint(0, 0.9, 0);
            check(ws.fader(0)->volParam->getCurve().getNumPoints() == 2, "volume automation points");
            auto automationRender = root.getChildFile("automation.wav");
            ok(ws.exportWav(automationRender));
            {
                juce::AudioFormatManager formats;
                formats.registerBasicFormats();
                std::unique_ptr<juce::AudioFormatReader> r(formats.createReaderFor(automationRender));
                check(r != nullptr, "automation render readable");
                juce::AudioBuffer<float> head(2, 4800), tail(2, 4800);
                r->read(&head, 0, 4800, 2400, true, true);
                r->read(&tail, 0, 4800, 38400, true, true);
                check(tail.getRMSLevel(0, 0, 4800) > head.getRMSLevel(0, 0, 4800) * 1.5f,
                      "volume automation changes rendered signal");
            }
            ws.fader(0)->volParam->getCurve().clear(&ws.edit->getUndoManager());
            ws.addTrack("Reverb Return", false, true);
            ok(ws.send(0, 2, -12));
            check(ws.inserts(0).size() == 1 && ws.inserts(2).size() == 1,
                  "auxiliary send and return created");
            check(ws.send(2, 0).failed(), "auxiliary feedback loop refused");
            check(ws.route(2, 0).failed(), "output route cannot close an auxiliary feedback loop");
            check(ws.addEffect(2, te::ReverbPlugin::xmlTypeName) != nullptr,
                  "stock reverb available without third-party plugins");
            ws.addTrack("4OSC", true);
            ws.addChordProgression(3);
            check(ws.track(3)->getClips().size() == 1 &&
                      dynamic_cast<te::MidiClip*>(ws.track(3)->getClips()[0])->getSequence().getNumNotes() ==
                          12,
                  "built-in instrument and editable chord MIDI");
            ws.track(0)->setMute(true);
            ws.track(1)->setMute(true);
            ws.track(2)->setMute(true);
            pump();
            auto synthRender = root.getChildFile("synth.wav");
            ok(ws.exportWav(synthRender));
            check(readPeak(synthRender) > 0.0001, "stock synth produces rendered audio");
            ws.track(0)->setMute(false);
            ws.track(1)->setMute(false);
            ws.track(2)->setMute(false);
            ok(ws.plan("Turn track 1 down 3 dB"));
            check(ws.fader(0)->getVolumeDb() == 0, "assistant preview does not mutate");
            ok(ws.applyPlan());
            check(std::abs(ws.fader(0)->getVolumeDb() + 3) < 0.001, "assistant applies gain");
            ws.undo();
            check(std::abs(ws.fader(0)->getVolumeDb()) < 0.001, "assistant undo");
            check(argc == 2, "VST3 fixture path provided");
            ok(ws.loadVst(0, juce::File(argv[1])));
            pump();
            auto inserts = ws.inserts(0);
            int external = -1;
            for (int i = 0; i < static_cast<int>(inserts.size()); ++i)
                if (dynamic_cast<te::ExternalPlugin*>(inserts[static_cast<size_t>(i)]))
                    external = i;
            check(external >= 0, "real VST3 instance in insert chain");
            ok(ws.sidechain(3, 0, external));
            check(inserts[static_cast<size_t>(external)]->getSidechainSourceID() == ws.track(3)->itemID,
                  "VST3 sidechain routing stored");
            ok(ws.save());
            auto session = ws.projectFile;
            auto portable = root.getChildFile("Portable/Song.bte");
            ok(ws.save(portable));
            check(portable.existsAsFile(), "portable session copy saved");
            ws.begin("Autosave gain");
            ws.fader(0)->setVolumeDb(-9);
            ws.autosave();
            check(te::EditFileOperations::getTempVersionOfEditFile(session).existsAsFile(),
                  "recovery snapshot written");
            ok(ws.open(session));
            check(ws.tracks().size() == 4 && ws.isBus(1), "session tracks and buses restored");
            check(ws.track(0)->getOutput().getDestinationTrack() == ws.track(1), "bus route restored");
            check(ws.inserts(0).size() >= 2, "insert chain restored");
            auto invalid = root.getChildFile("bad.bte");
            invalid.replaceWithText("{}");
            check(ws.open(invalid).failed() && ws.tracks().size() == 4,
                  "invalid session preserves current edit");
            ok(ws.open(portable));
            auto* copiedWave = dynamic_cast<te::WaveAudioClip*>(ws.track(0)->getClips()[0]);
            check(copiedWave && copiedWave->getOriginalFile().existsAsFile() &&
                      copiedWave->getOriginalFile().isAChildOf(portable.getParentDirectory()),
                  "portable copy resolves copied media");
            ws.newSession(root.getChildFile("VSTProcessing/Session.bte"));
            ok(ws.importAudio(source));
            ok(ws.importAudio(source));
            ws.track(1)->getOutput().setOutputToNone();
            ok(ws.loadVst(0, juce::File(argv[1])));
            pump();
            auto vstRender = root.getChildFile("vst-main.wav");
            ok(ws.exportWav(vstRender));
            check(std::abs(readPeak(vstRender) - 0.2f) < 0.004f,
                  "new engine renders actual VST3 main processing");
            ok(ws.sidechain(1, 0, 0));
            auto scRender = root.getChildFile("vst-sidechain.wav");
            ok(ws.exportWav(scRender));
            check(std::abs(readPeak(scRender) - 0.05f) < 0.004f,
                  "new engine renders actual VST3 auxiliary sidechain processing");
            ws.newSession(root.getChildFile("Drums/Session.bte"));
            int drums = ws.createDrumGroove();
            check(ws.track(drums)->getClips().size() == 1, "factory drum groove creates a MIDI region");
            pump();
            auto drumRender = root.getChildFile("drums.wav");
            ok(ws.exportWav(drumRender));
            check(readPeak(drumRender) > 0.01,
                  "built-in percussion synthesizer renders without sample downloads");
            ws.setMono(-1, true);
            check(ws.mono(-1), "master mono sum enabled");
            ws.undo();
            check(!ws.mono(-1), "master mono sum undo");
            ok(ws.createDemo());
            check(ws.tracks().size() == 6, "out-of-box demo includes synths, drums, return and music bus");
            auto demo = root.getChildFile("demo.wav");
            ok(ws.exportWav(demo));
            auto demoPeak = readPeak(demo);
            check(demoPeak > 0.01f && demoPeak < 1.0f, "factory demo renders an audible mix with headroom");
            // Exercise actual disk recording through Tracktion's hosted device callback.
            for (uint32_t group : {1u, 2u})
            {
                ws.newSession(root.getChildFile("Recording" + juce::String(group) + "/Take.bte"));
                ws.addTrack("Recorded input");
                dm.setAllWaveInputsToNumChannels(group);
                dm.dispatchPendingUpdates();
                ws.configureInputs();
                pump();
                int waveInput = -1;
                auto devs = ws.edit->getAllInputDevices();
                for (int i = 0; i < devs.size(); ++i)
                    if (devs[i]->getInputDevice().getDeviceType() == te::InputDevice::waveDevice)
                    {
                        waveInput = i;
                        break;
                    }
                check(waveInput >= 0, "hosted recording input available");
                ok(ws.assignInput(0, waveInput, true));
                ok(ws.record());
                pump(50);
                juce::AudioBuffer<float> block(2, 512);
                juce::MidiBuffer midi;
                for (int b = 0; b < 110; ++b)
                {
                    for (int i = 0; i < 512; ++i)
                    {
                        float value = 0.15f * std::sin(static_cast<float>(b * 512 + i) * 220 *
                                                       juce::MathConstants<float>::twoPi / 48000);
                        block.setSample(0, i, value);
                        block.setSample(1, i, value);
                    }
                    io.processBlock(block, midi);
                    if (b % 10 == 0)
                        pump(2);
                }
                ws.stop();
                pump(150);
                check(!ws.track(0)->getClips().isEmpty(), "recording creates a timeline clip");
                auto* take = dynamic_cast<te::WaveAudioClip*>(ws.track(0)->getClips()[0]);
                check(take && take->getOriginalFile().existsAsFile() &&
                          take->getOriginalFile().getSize() > 1000,
                      "recording writes real audio to disk");
                {
                    juce::AudioFormatManager formats;
                    formats.registerBasicFormats();
                    std::unique_ptr<juce::AudioFormatReader> reader(
                        formats.createReaderFor(take->getOriginalFile()));
                    check(reader && reader->numChannels == group,
                          "recorded file matches selected mono/stereo input grouping");
                }
                check(readPeak(take->getOriginalFile()) > 0.05f,
                      "recorded audio contains the injected signal");
            }
            ws.edit.reset();
            dm.deviceManager.closeAudioDevice();
            dm.removeHostedAudioDeviceInterface();
        }
        root.deleteRecursively();
        std::cout << count << " workstation checks passed\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "FAIL: " << e.what() << "\nArtifacts: " << root.getFullPathName() << std::endl;
        return 1;
    }
}
