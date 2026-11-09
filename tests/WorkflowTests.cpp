#include "ProEngine.h"
#include <iostream>
#include <stdexcept>
using namespace bte;
namespace
{
int count = 0;
void check(bool pass, const char* name)
{
    if (!pass)
        throw std::runtime_error(name);
    ++count;
    std::cout << "PASS " << name << std::endl;
}
void ok(juce::Result r)
{
    if (r.failed())
        throw std::runtime_error(r.getErrorMessage().toStdString());
}
void pump()
{
    juce::MessageManager::getInstance()->runDispatchLoopUntil(80);
}
void near(double a, double b, const char* name, double tolerance = 1e-5)
{
    check(std::abs(a - b) < tolerance, name);
}
std::unique_ptr<juce::AudioFormatReader> reader(const juce::File& file)
{
    juce::AudioFormatManager m;
    m.registerBasicFormats();
    return std::unique_ptr<juce::AudioFormatReader>(m.createReaderFor(file));
}
} // namespace
int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    auto root = juce::File::getSpecialLocation(juce::File::tempDirectory)
                    .getNonexistentChildFile("bte-workflow-test", "", false);
    root.createDirectory();
    try
    {
        auto source = root.getChildFile("signal.wav");
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream = source.createOutputStream();
        auto writer = wav.createWriterFor(
            stream,
            juce::AudioFormatWriterOptions{}.withSampleRate(48000).withNumChannels(2).withBitsPerSample(24));
        check(writer != nullptr, "fixture writer");
        juce::AudioBuffer<float> samples(2, 48000);
        for (int n = 0; n < 48000; ++n)
        {
            float v =
                (n < 24000 ? 0.1f : 0.2f) * std::sin(n * 440.0 * juce::MathConstants<double>::twoPi / 48000);
            samples.setSample(0, n, v);
            samples.setSample(1, n, v);
        }
        check(writer->writeFromAudioSampleBuffer(samples, 0, 48000), "fixture samples");
        writer.reset();
        {
            Workstation ws(false, root.getChildFile("Sessions"));
            auto& dm = ws.engine.getDeviceManager();
            auto& io = dm.getHostedAudioDeviceInterface();
            te::HostedAudioDeviceInterface::Parameters hp;
            hp.sampleRate = 48000;
            hp.blockSize = 512;
            hp.inputChannels = hp.outputChannels = 2;
            io.initialise(hp);
            io.prepareToPlay(48000, 512);
            dm.dispatchPendingUpdates();
            dm.setCpuLimitBeforeMuting(1000);
            ws.configureInputs();
            ws.setBpm(120);
            ok(ws.importAudio(source));
            ws.addTrack("Destination");
            te::Clip::Ptr wave = ws.track(0)->getClips()[0];
            auto id = wave->itemID;
            ok(ws.copyClip(wave.get()));
            auto* pasted = dynamic_cast<te::WaveAudioClip*>(ws.pasteClip(1, 1.5));
            check(pasted && pasted->itemID != id, "paste creates unique clip ID");
            near(pasted->getPosition().getStart().inSeconds(), 1.5, "paste at playhead");
            check(pasted->getOriginalFile().existsAsFile() && pasted->getOriginalFile() != source,
                  "paste owns source media");
            ws.undo();
            check(ws.track(1)->getClips().isEmpty(), "paste undo");
            ok(ws.moveClip(wave.get(), 1, 0.25));
            check(wave->getTrack() == ws.track(1), "cross-track move");
            ws.undo();
            check(wave->getTrack() == ws.track(0), "cross-track move undo");
            ok(ws.copyClip(wave.get(), true));
            check(ws.track(0)->getClips().isEmpty(), "cut removes region");
            ws.undo();
            check(ws.track(0)->getClips().size() == 1, "cut undo");
            auto* audio = dynamic_cast<te::WaveAudioClip*>(wave.get());
            RegionSettings region;
            region.name = "Edited region";
            region.gainDb = -6;
            region.fadeInSeconds = .1;
            region.fadeOutSeconds = .15;
            ok(ws.editRegion(audio, region));
            near(audio->getGainDB(), -6, "region gain stored");
            near(audio->getFadeIn().inSeconds(), .1, "fade in stored");
            near(audio->getFadeOut().inSeconds(), .15, "fade out stored");
            ExportSettings exportSettings;
            exportSettings.tailSeconds = 0;
            auto gained = root.getChildFile("gain.wav");
            ok(ws.exportAudio(gained, exportSettings));
            auto r = reader(gained);
            check(r && r->lengthInSamples == 48000, "region render length");
            juce::AudioBuffer<float> rendered(2, 48000);
            r->read(&rendered, 0, 48000, 0, true, true);
            near(rendered.getMagnitude(0, 24000, 12000), .2 * std::pow(10., -6. / 20.),
                 "region gain affects rendered samples", .002);
            check(rendered.getMagnitude(0, 0, 240) < .01, "fade in affects rendered samples");
            r.reset();
            ws.undo();
            near(audio->getGainDB(), 0, "region edit undo");
            region.gainDb = 0;
            region.pitchSemitones = 12;
            region.lengthBeats = 4;
            region.reversed = true;
            ok(ws.editRegion(audio, region));
            near(audio->getPosition().getLength().inSeconds(), 2, "audio time stretch length");
            near(audio->getSpeedRatio(), .5, "audio stretch speed");
            check(audio->getIsReversed() && audio->getPitchChange() == 12, "reverse and pitch stored");
            auto stretched = root.getChildFile("stretched.wav");
            ok(ws.exportAudio(stretched, exportSettings));
            r = reader(stretched);
            check(r && r->lengthInSamples == 96000, "stretched audio renders requested duration");
            juce::AudioBuffer<float> pitched(2, 96000);
            r->read(&pitched, 0, 96000, 0, true, true);
            r.reset();
            check(pitched.getMagnitude(0, 12000, 12000) > .17f &&
                      pitched.getMagnitude(0, 72000, 12000) < .12f,
                  "reverse swaps source amplitude halves");
            int crossings = 0;
            for (int n = 24001; n < 48000; ++n)
                if (pitched.getSample(0, n - 1) <= 0 && pitched.getSample(0, n) > 0)
                    ++crossings;
            check(crossings >= 430 && crossings <= 450, "octave transpose renders 880 Hz while stretching");
            ws.undo();
            near(audio->getSpeedRatio(), 1, "stretch undo");
            region.lengthBeats = .001;
            check(ws.editRegion(audio, region).failed(), "invalid region edit rejected");
            RegionSettings extreme;
            extreme.name = "Extreme stretch";
            extreme.lengthBeats = 100;
            ok(ws.editRegion(audio, extreme));
            extreme.lengthBeats = .1;
            check(ws.editRegion(audio, extreme).failed(),
                  "unsupported stretch factor rejected without silently ignoring length");
            ws.undo();
            auto* midi = ws.addMidiClip(1, 0, 8);
            auto& seq = midi->getSequence();
            auto* note = seq.addNote(60, te::BeatPosition::fromBeats(.13), te::BeatDuration::fromBeats(.4),
                                     96, 0, nullptr);
            auto* high = seq.addNote(125, te::BeatPosition::fromBeats(.62), te::BeatDuration::fromBeats(.2),
                                     120, 0, nullptr);
            QuantizeSettings q;
            q.gridBeats = .25;
            ok(ws.quantize(midi, q));
            near(note->getStartBeat().inBeats(), .25, "quantize start");
            near(note->getLengthBeats().inBeats(), .4, "quantize preserves duration");
            ws.undo();
            near(note->getStartBeat().inBeats(), .13, "quantize undo");
            q.strength = .5;
            ok(ws.quantize(midi, q));
            near(note->getStartBeat().inBeats(), .19, "partial quantize strength");
            ws.undo();
            q.strength = 1;
            q.swing = .5;
            ok(ws.quantize(midi, q));
            near(note->getStartBeat().inBeats(), .375, "swing shifts odd subdivision");
            ws.undo();
            q.swing = 0;
            q.noteEnds = true;
            ok(ws.quantize(midi, q));
            near(note->getEndBeat().inBeats(), .5, "quantize note ends");
            ws.undo();
            q.strength = 0;
            ok(ws.quantize(midi, q));
            near(note->getStartBeat().inBeats(), .13, "zero strength unchanged");
            ok(ws.humanize(midi, 12, 8, 42));
            double human = note->getStartBeat().inBeats();
            int vel = note->getVelocity();
            check(std::abs(ws.seconds(human) - ws.seconds(.13)) <= .012001 && vel >= 88 && vel <= 104,
                  "humanize bounded timing and velocity");
            ws.undo();
            ok(ws.humanize(midi, 12, 8, 42));
            near(note->getStartBeat().inBeats(), human, "humanize repeatable seed");
            check(note->getVelocity() == vel, "humanize repeatable velocity");
            ws.undo();
            ok(ws.transpose(midi, 12));
            check(note->getNoteNumber() == 72 && high->getNoteNumber() == 127, "transpose clamps MIDI pitch");
            ws.undo();
            check(note->getNoteNumber() == 60, "transpose undo");
            ok(ws.setVelocities(midi, 54));
            check(note->getVelocity() == 54 && high->getVelocity() == 54, "bulk velocities");
            ws.undo();
            check(note->getVelocity() == 96, "velocity undo");
            check(ws.setVelocities(midi, 0).failed(), "invalid velocity rejected");
            auto* midiCopy = dynamic_cast<te::MidiClip*>(ws.duplicate(midi));
            check(midiCopy && midiCopy->itemID != midi->itemID &&
                      midiCopy->getSequence().getNotes().size() == 2,
                  "MIDI duplicate preserves notes");
            ws.undo();
            auto marker = ws.addMarker("Verse", 2);
            auto marker2 = ws.addMarker("Chorus", 6);
            check(marker && marker2 && marker->getName() == "Verse", "named markers");
            ws.seek(0);
            ws.seekMarker(true);
            near(ws.position(), 1, "next marker navigation");
            ws.seek(4);
            ws.seekMarker(false);
            near(ws.position(), 3, "previous marker navigation");
            ok(ws.transportSettings(1, 3, true, true, 2));
            check(ws.edit->recordingPunchInOut && ws.edit->getCountInMode() == te::Edit::CountIn::twoBar,
                  "punch and count-in enabled");
            near(ws.edit->getTransport().getLoopRange().getStart().inSeconds(), .5, "range start");
            check(ws.transportSettings(3, 1, false, false, 0).failed(), "invalid transport range rejected");
            auto eq = ws.addEffect(0, te::EqualiserPlugin::xmlTypeName);
            auto comp = ws.addEffect(0, te::CompressorPlugin::xmlTypeName);
            check(ws.inserts(0)[0] == eq.get(), "initial insert order");
            ok(ws.reorderEffect(0, 0, 1));
            check(ws.inserts(0)[0] == comp.get(), "insert reorder");
            ws.undo();
            check(ws.inserts(0)[0] == eq.get(), "insert reorder undo");
            ws.addTrack("FX Bus", false, true);
            ok(ws.send(0, 2));
            check(ws.reorderEffect(0, 1, 1).failed(), "reorder protects post-fader sends");
            ws.addEffect(1, te::FourOscPlugin::xmlTypeName);
            check(ws.reorderEffect(1, 0, 1).failed(), "reorder protects instrument position");
            exportSettings.format = ExportSettings::flac;
            exportSettings.sampleRate = 44100;
            exportSettings.bitDepth = 16;
            exportSettings.mono = true;
            exportSettings.loopOnly = true;
            auto flac = root.getChildFile("range.flac");
            ok(ws.exportAudio(flac, exportSettings));
            r = reader(flac);
            check(r && r->sampleRate == 44100 && r->numChannels == 1 && r->bitsPerSample == 16,
                  "FLAC export rate depth channels");
            check(std::abs(r->lengthInSamples - 44100) <= 1, "range export duration");
            r.reset();
            exportSettings.format = ExportSettings::midi;
            exportSettings.loopOnly = false;
            auto mid = root.getChildFile("notes.mid");
            ok(ws.exportAudio(mid, exportSettings));
            auto input = mid.createInputStream();
            juce::MidiFile file;
            check(input && file.readFrom(*input), "MIDI export parses");
            int notes = 0;
            bool tempo = false;
            for (int t = 0; t < file.getNumTracks(); ++t)
                for (int e = 0; e < file.getTrack(t)->getNumEvents(); ++e)
                {
                    auto m = file.getTrack(t)->getEventPointer(e)->message;
                    if (m.isNoteOn())
                        ++notes;
                    if (m.isTempoMetaEvent())
                        tempo = true;
                }
            check(notes == 2 && tempo, "MIDI export notes and tempo");
            input.reset();
            exportSettings.format = ExportSettings::flac;
            exportSettings.bitDepth = 32;
            auto oldSize = flac.getSize();
            check(ws.exportAudio(flac, exportSettings).failed() && flac.getSize() == oldSize,
                  "invalid export preserves output");
            auto missing = root.getChildFile("unavailable/signal.wav");
            ws.begin("Missing fixture");
            audio->getSourceFileReference().setToFile(
                missing, te::SourceFileReference::PathStyle::alwaysAbsolute, false);
            pump();
            check(ws.missingMedia().size() == 1, "missing media detected");
            ok(ws.relink(missing, source));
            check(ws.missingMedia().empty() && audio->getOriginalFile().existsAsFile() &&
                      audio->getOriginalFile() != source,
                  "relink copies owned source");
            ws.undo();
            check(ws.missingMedia().size() == 1, "relink undo");
            auto search = root.getChildFile("Search");
            search.getChildFile("A").createDirectory();
            search.getChildFile("B").createDirectory();
            source.copyFileTo(search.getChildFile("A/signal.wav"));
            source.copyFileTo(search.getChildFile("B/signal.wav"));
            ok(ws.findMedia(search));
            check(ws.missingMedia().size() == 1, "ambiguous filename search skipped");
            search.getChildFile("B/signal.wav").deleteFile();
            ok(ws.findMedia(search));
            check(ws.missingMedia().empty(), "unique filename search relinks");
            ok(ws.save());
            auto saved = ws.projectFile;
            wave = nullptr;
            marker = nullptr;
            marker2 = nullptr;
            eq = nullptr;
            comp = nullptr;
            ok(ws.open(saved));
            check(ws.missingMedia().empty(), "relinked source survives reload");
            check(ws.edit->getMarkerManager().getMarkers().size() == 2, "markers survive reload");
            check(ws.edit->recordingPunchInOut && ws.edit->getCountInMode() == te::Edit::CountIn::twoBar,
                  "punch survives reload and count-in preference remains");
            ws.newSession(root.getChildFile("New/new.btedaw"));
            ws.addTrack("Pasted audio");
            auto* portable = dynamic_cast<te::WaveAudioClip*>(ws.pasteClip(0, 0));
            check(portable && portable->getOriginalFile().getParentDirectory() ==
                                  ws.projectFile.getParentDirectory().getChildFile("Media"),
                  "cross-session clipboard owns media");
            ws.newSession(root.getChildFile("Punch/take.bte"));
            ws.setBpm(120);
            ws.addTrack("Punch recording");
            ws.configureInputs();
            pump();
            auto devs = ws.edit->getAllInputDevices();
            int waveInput = -1;
            for (int n = 0; n < devs.size(); ++n)
                if (devs[n]->getInputDevice().getDeviceType() == te::InputDevice::waveDevice)
                {
                    waveInput = n;
                    break;
                }
            check(waveInput >= 0, "punch input available");
            ok(ws.assignInput(0, waveInput, true));
            ok(ws.transportSettings(.5, 1.5, false, true, 0));
            ws.seek(0);
            ok(ws.record());
            pump();
            juce::AudioBuffer<float> block(2, 512);
            juce::MidiBuffer messages;
            for (int b = 0; b < 110; ++b)
            {
                for (int n = 0; n < 512; ++n)
                {
                    float v =
                        .1f * std::sin((b * 512 + n) * 220.0 * juce::MathConstants<double>::twoPi / 48000);
                    block.setSample(0, n, v);
                    block.setSample(1, n, v);
                }
                io.processBlock(block, messages);
                if (b % 10 == 0)
                    pump();
            }
            ws.stop();
            pump();
            check(!ws.track(0)->getClips().isEmpty(), "punch recording creates clip");
            auto* take = ws.track(0)->getClips()[0];
            near(take->getPosition().getStart().inSeconds(), .25, "punch clip starts at range", .025);
            near(take->getPosition().getEnd().inSeconds(), .75, "punch clip ends at range", .025);
            auto* recorded = dynamic_cast<te::WaveAudioClip*>(take);
            r = reader(recorded->getOriginalFile());
            check(r && r->lengthInSamples > 1000, "punch writes readable audio");
            r.reset();
            ws.edit.reset();
            dm.deviceManager.closeAudioDevice();
            dm.removeHostedAudioDeviceInterface();
        }
        root.deleteRecursively();
        std::cout << count << " workflow checks passed\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "FAIL after " << count << ": " << e.what() << "\nArtifacts: " << root.getFullPathName()
                  << std::endl;
        return 1;
    }
}
