#include "ProEngine.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <random>
#include <thread>
#if JUCE_WINDOWS
#include <psapi.h>
#include <windows.h>
#endif
using namespace bte;
using Clock = std::chrono::steady_clock;
namespace
{
struct Options
{
    int tracks = 64, notes = 2048, cycles = 10, edits = 500, threads = 4;
    double seconds = 60, recordSeconds = 30;
    juce::File vst, report;
    bool keep = false;
};
double elapsed(Clock::time_point start)
{
    return std::chrono::duration<double>(Clock::now() - start).count();
}
double rssMiB()
{
#if JUCE_WINDOWS
    PROCESS_MEMORY_COUNTERS m{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &m, sizeof(m)))
        return m.WorkingSetSize / 1048576.;
#elif JUCE_LINUX
    std::ifstream file("/proc/self/status");
    std::string line;
    while (std::getline(file, line))
        if (line.rfind("VmRSS:", 0) == 0)
            return std::stod(line.substr(6)) / 1024.;
#endif
    return -1;
}
int handles()
{
#if JUCE_WINDOWS
    DWORD n = 0;
    if (GetProcessHandleCount(GetCurrentProcess(), &n))
        return static_cast<int>(n);
#elif JUCE_LINUX
    return juce::File("/proc/self/fd").findChildFiles(juce::File::findFilesAndDirectories, false).size();
#endif
    return -1;
}
void pump(int ms = 30)
{
    juce::MessageManager::getInstance()->runDispatchLoopUntil(ms);
}
void ok(juce::Result r)
{
    if (r.failed())
        throw std::runtime_error(r.getErrorMessage().toStdString());
}
std::unique_ptr<juce::AudioFormatReader> read(const juce::File& f)
{
    juce::AudioFormatManager m;
    m.registerBasicFormats();
    return std::unique_ptr<juce::AudioFormatReader>(m.createReaderFor(f));
}
struct Stats
{
    std::vector<double> callbacks;
    juce::int64 nonfinite = 0, overruns = 0, silent = 0, frames = 0;
    double peak = 0, wallSeconds = 0, deadlineMs = 0;
    juce::var json(double rate, int block)
    {
        std::sort(callbacks.begin(), callbacks.end());
        auto quantile = [&](double q)
        { return callbacks.empty() ? 0 : callbacks[static_cast<size_t>(q * (callbacks.size() - 1))]; };
        auto* o = new juce::DynamicObject;
        o->setProperty("sample_rate", rate);
        o->setProperty("block_size", block);
        o->setProperty("callbacks", static_cast<juce::int64>(callbacks.size()));
        o->setProperty("audio_seconds", frames / rate);
        o->setProperty("wall_seconds", wallSeconds);
        o->setProperty("deadline_ms", deadlineMs);
        o->setProperty("p50_ms", quantile(.5));
        o->setProperty("p95_ms", quantile(.95));
        o->setProperty("p99_ms", quantile(.99));
        o->setProperty("max_ms", quantile(1));
        o->setProperty("deadline_misses", overruns);
        o->setProperty("deadline_miss_percent", callbacks.empty() ? 0 : 100. * overruns / callbacks.size());
        o->setProperty("compute_budget_pass_1_percent",
                       callbacks.size() > 0 && 100. * overruns / callbacks.size() <= 1.0);
        o->setProperty("nonfinite_samples", nonfinite);
        o->setProperty("silent_blocks", silent);
        o->setProperty("peak", peak);
        return juce::var(o);
    }
};
class Stress
{
  public:
    Options o;
    juce::var report{new juce::DynamicObject};
    juce::Array<juce::var> phases, playback, resources;
    int checks = 0;
    double peakRss = 0;
    Clock::time_point started = Clock::now();
    explicit Stress(Options options) : o(std::move(options))
    {
        auto& d = *report.getDynamicObject();
        d.setProperty("started_utc", juce::Time::getCurrentTime().toISO8601(true));
        d.setProperty("os", juce::SystemStats::getOperatingSystemName());
        d.setProperty("cpu", juce::SystemStats::getCpuModel());
        d.setProperty("logical_cpus_visible", juce::SystemStats::getNumCpus());
        d.setProperty("audio_cpu_budget", o.threads);
#ifdef NDEBUG
        d.setProperty("build", "Release");
#else
        d.setProperty("build", "Debug");
#endif
        d.setProperty("source_revision", BTE_STRESS_REVISION);
        d.setProperty("source_dirty_at_configure", BTE_STRESS_DIRTY);
        d.setProperty("physical_hardware_test", false);
        d.setProperty("cpu_mute_guard_disabled", true);
        d.setProperty("callback_measurement",
                      "Wall-clock paced generated buffers; io.processBlock only; first 0.5s excluded from "
                      "timing. Deadlines are compute-time overruns, not measured device xruns.");
        d.setProperty("seed", 513);
        d.setProperty("audio_tracks", o.tracks);
        d.setProperty("midi_tracks", 8);
        d.setProperty("midi_notes", o.notes * 8);
        d.setProperty("requested_playback_seconds", o.seconds);
        d.setProperty("requested_record_seconds", o.recordSeconds);
        d.setProperty("requested_reload_cycles", o.cycles);
        d.setProperty("requested_edit_operations", o.edits);
        sample("start");
    }
    void sample(const juce::String& phase)
    {
        double mem = rssMiB();
        peakRss = std::max(peakRss, mem);
        auto* x = new juce::DynamicObject;
        x->setProperty("phase", phase);
        x->setProperty("elapsed_seconds", elapsed(started));
        x->setProperty("rss_mib", mem);
        x->setProperty("os_handle_count", handles());
        resources.add(juce::var(x));
    }
    void require(bool pass, const juce::String& name)
    {
        ++checks;
        if (!pass)
            throw std::runtime_error(name.toStdString());
    }
    template <typename Fn> void phase(const juce::String& name, Fn fn)
    {
        std::cout << "BEGIN " << name << std::endl;
        auto at = Clock::now();
        report.getDynamicObject()->setProperty("active_phase", name);
        write(false, {}, false);
        fn();
        auto* p = new juce::DynamicObject;
        p->setProperty("name", name);
        p->setProperty("seconds", elapsed(at));
        phases.add(juce::var(p));
        sample(name);
        write(false, {}, false);
        std::cout << "DONE " << name << " (" << elapsed(at) << " s, " << rssMiB() << " MiB)" << std::endl;
    }
    void write(bool success, const juce::String& failure = {}, bool complete = true)
    {
        auto& d = *report.getDynamicObject();
        d.setProperty("status", complete ? (success ? "passed" : "failed") : "running");
        d.setProperty("correctness_pass", complete ? juce::var(success) : juce::var());
        d.setProperty("failure", failure);
        d.setProperty("checks", checks);
        d.setProperty("total_seconds", elapsed(started));
        d.setProperty("sampled_peak_rss_mib", peakRss);
        d.setProperty("phases", juce::var(phases));
        d.setProperty("playback", juce::var(playback));
        d.setProperty("resources", juce::var(resources));
        bool deadlines = true;
        for (const auto& profile : playback)
            deadlines = deadlines && static_cast<bool>(profile["compute_budget_pass_1_percent"]);
        d.setProperty("all_profiles_compute_budget_pass_1_percent", !playback.isEmpty() && deadlines);
        d.setProperty("deadline_acceptance",
                      "Internal diagnostic target: at most 1% measured calls exceed one buffer period. "
                      "Separate from correctness and physical-driver acceptance.");
        o.report.getParentDirectory().createDirectory();
        juce::TemporaryFile temporary(o.report);
        if (!temporary.getFile().replaceWithText(juce::JSON::toString(report, true)) ||
            !temporary.overwriteTargetFileWithTemporary())
            throw std::runtime_error("Cannot write stress report");
    }
    Stats callbacks(Workstation& ws, double rate, int block, double seconds, bool recording = false)
    {
        auto& io = ws.engine.getDeviceManager().getHostedAudioDeviceInterface();
        Stats stats;
        stats.deadlineMs = 1000 * block / rate;
        int blocks = static_cast<int>(std::ceil(seconds * rate / block));
        stats.callbacks.reserve(static_cast<size_t>(blocks));
        std::atomic<bool> done = false;
        std::exception_ptr failure;
        auto at = Clock::now();
        std::jthread audio(
            [&]
            {
                try
                {
                    juce::AudioBuffer<float> buffer(2, block);
                    juce::MidiBuffer midi;
                    auto next = Clock::now();
                    for (int b = 0; b < blocks; ++b)
                    {
                        for (int n = 0; n < block; ++n)
                        {
                            float v = .05f * std::sin((static_cast<double>(b) * block + n) * 375 *
                                                      juce::MathConstants<double>::twoPi / rate);
                            buffer.setSample(0, n, v);
                            buffer.setSample(1, n, v);
                        }
                        midi.clear();
                        auto begin = Clock::now();
                        io.processBlock(buffer, midi);
                        double ms = elapsed(begin) * 1000;
                        // Ignore the first half second when graphs/files are warming up.
                        if (b * block >= rate * .5)
                        {
                            stats.callbacks.push_back(ms);
                            if (ms > stats.deadlineMs)
                                ++stats.overruns;
                        }
                        float peak = 0;
                        for (int ch = 0; ch < 2; ++ch)
                            for (int n = 0; n < block; ++n)
                            {
                                auto v = buffer.getSample(ch, n);
                                if (!std::isfinite(v))
                                    ++stats.nonfinite;
                                else
                                    peak = std::max(peak, std::abs(v));
                            }
                        stats.peak = std::max(stats.peak, static_cast<double>(peak));
                        if (peak < 1e-7f)
                            ++stats.silent;
                        stats.frames += block;
                        next += std::chrono::duration_cast<Clock::duration>(
                            std::chrono::duration<double>(block / rate));
                        if (next < Clock::now())
                            next = Clock::now();
                        std::this_thread::sleep_until(next);
                    }
                }
                catch (...)
                {
                    failure = std::current_exception();
                }
                done.store(true, std::memory_order_release);
            });
        int iterations = 0;
        while (!done.load(std::memory_order_acquire))
        {
            pump(15);
            if (++iterations % 30 == 0)
            {
                peakRss = std::max(peakRss, rssMiB());
                if (!recording)
                {
                    int t = (iterations / 30) % o.tracks;
                    ws.begin("Live fader move");
                    ws.fader(t)->setVolumeDb(
                        static_cast<float>(-20 * std::log10(o.tracks) - (iterations % 3)));
                }
            }
        }
        audio.join();
        if (failure)
            std::rethrow_exception(failure);
        stats.wallSeconds = elapsed(at);
        require(stats.nonfinite == 0, "finite audio callback samples");
        if (!recording)
            require(stats.peak > 1e-5, "live playback produces audio");
        return stats;
    }
    void run()
    {
        auto root = juce::File::getSpecialLocation(juce::File::tempDirectory)
                        .getNonexistentChildFile("bte-stress", "", false);
        root.createDirectory();
        report.getDynamicObject()->setProperty("artifact_directory", root.getFullPathName());
        try
        {
            {
                Workstation ws(false, root.getChildFile("Sessions"));
                static_cast<HostBehaviour&>(ws.engine.getEngineBehaviour()).audioCpuLimit = o.threads;
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
                ws.edit->setCountInMode(te::Edit::CountIn::none);
                ws.setBpm(120);
                const int midiStart = o.tracks, busStart = o.tracks + 8, groups = (o.tracks + 7) / 8,
                          rootBus = busStart + groups;
                int vstCount = std::min(32, o.tracks / 2);
                auto source = root.getChildFile("source.wav");
                phase("build_large_session",
                      [&]
                      {
                          juce::WavAudioFormat wav;
                          std::unique_ptr<juce::OutputStream> stream = source.createOutputStream();
                          auto writer = wav.createWriterFor(stream, juce::AudioFormatWriterOptions{}
                                                                        .withSampleRate(48000)
                                                                        .withNumChannels(2)
                                                                        .withBitsPerSample(24));
                          require(writer != nullptr, "fixture writer");
                          juce::AudioBuffer<float> data(2, 96000);
                          for (int n = 0; n < 96000; ++n)
                          {
                              auto v = static_cast<float>(
                                  .04 * std::sin(n * 375 * juce::MathConstants<double>::twoPi / 48000));
                              data.setSample(0, n, v);
                              data.setSample(1, n, v);
                          }
                          require(writer->writeFromAudioSampleBuffer(data, 0, 96000), "fixture data");
                          writer.reset();
                          for (int i = 0; i < o.tracks; ++i)
                          {
                              ok(ws.importAudio(source));
                              auto* clip = ws.track(i)->getClips()[0];
                              for (int j = 0; j < 3; ++j)
                              {
                                  clip = ws.duplicate(clip);
                                  require(clip != nullptr, "duplicate dense region");
                              }
                              ws.fader(i)->setVolumeDb(static_cast<float>(-20 * std::log10(o.tracks)));
                              require(ws.addEffect(i, te::EqualiserPlugin::xmlTypeName) != nullptr,
                                      "insert EQ");
                              if (i < vstCount)
                                  ok(ws.loadVst(i, o.vst));
                              if (i % 8 == 0)
                                  pump(2);
                          }
                          for (int i = 0; i < 8; ++i)
                          {
                              ws.addTrack("Dense MIDI " + juce::String(i + 1), true);
                              auto* c = ws.addMidiClip(midiStart + i, 0, 16);
                              require(c != nullptr, "dense MIDI region");
                              for (int n = 0; n < o.notes; ++n)
                                  c->getSequence().addNote(
                                      48 + n % 24, te::BeatPosition::fromBeats(16. * n / o.notes),
                                      te::BeatDuration::fromBeats(.02), 80 + n % 24, 0, nullptr);
                              ws.track(midiStart + i)->setMute(true);
                              ws.fader(midiStart + i)->setVolumeDb(-36);
                          }
                          for (int g = 0; g < groups; ++g)
                              ws.addTrack("Group " + juce::String(g + 1), false, true);
                          ws.addTrack("Mix Bus", false, true);
                          for (int t = 0; t < o.tracks; ++t)
                              ok(ws.route(t, busStart + t / 8));
                          for (int g = 0; g < groups; ++g)
                              ok(ws.route(busStart + g, rootBus));
                          require(ws.route(rootBus, 0).failed(), "large nested routing feedback rejected");
                          ws.edit->dispatchPendingUpdatesSynchronously();
                          pump();
                          auto& d = *report.getDynamicObject();
                          int plugins = 0, clips = 0, notes = 0;
                          for (auto* t : ws.tracks())
                          {
                              plugins += t->pluginList.size();
                              clips += t->getClips().size();
                              for (auto* c : t->getClips())
                                  if (auto* m = dynamic_cast<te::MidiClip*>(c))
                                      notes += m->getSequence().getNotes().size();
                          }
                          d.setProperty("total_tracks", ws.tracks().size());
                          d.setProperty("audio_regions", o.tracks * 4);
                          d.setProperty("total_regions", clips);
                          d.setProperty("plugin_instances_including_utilities", plugins);
                          d.setProperty("test_vst3_instances", vstCount);
                          require(notes == o.notes * 8, "dense MIDI note count");
                      });
                phase("large_mix_render",
                      [&]
                      {
                          ExportSettings s;
                          s.tailSeconds = 0;
                          auto f = root.getChildFile("large-mix.wav");
                          ok(ws.exportAudio(f, s));
                          auto r = read(f);
                          require(r && r->lengthInSamples == 384000, "large render length");
                          juce::AudioBuffer<float> data(2, 24000);
                          r->read(&data, 0, 24000, 48000, true, true);
                          float expected = .04f * (1.f + static_cast<float>(vstCount) / o.tracks);
                          require(std::abs(data.getMagnitude(0, 24000) - expected) < .002f,
                                  "large bus and VST mix has expected decoded amplitude");
                          report.getDynamicObject()->setProperty("render_expected_peak", expected);
                          report.getDynamicObject()->setProperty("render_measured_peak",
                                                                 data.getMagnitude(0, 24000));
                      });
                phase("sends_sidechains_feedback",
                      [&]
                      {
                          for (int r = 0; r < 4; ++r)
                          {
                              int dest = ws.tracks().size();
                              ws.addTrack("Stress return " + juce::String(r + 1), false, true);
                              require(ws.addEffect(dest, r < 2 ? te::ReverbPlugin::xmlTypeName
                                                               : te::DelayPlugin::xmlTypeName) != nullptr,
                                      "return effect inserted");
                              ws.fader(dest)->setVolumeDb(-18);
                              ok(ws.route(dest, rootBus));
                              for (int source = r; source < o.tracks; source += 16)
                                  ok(ws.send(source, dest, -18));
                              require(ws.route(rootBus, dest).failed(), "return output feedback rejected");
                              require(ws.send(rootBus, dest).failed(), "return send feedback rejected");
                          }
                          for (int g = 0; g < groups; ++g)
                          {
                              ok(ws.loadVst(busStart + g, o.vst));
                              ok(ws.sidechain(g * 8, busStart + g, 0));
                              require(ws.sidechain(rootBus, busStart + g, 0).failed(),
                                      "sidechain feedback rejected");
                          }
                          auto& d = *report.getDynamicObject();
                          int plugins = 0;
                          for (auto* t : ws.tracks())
                              plugins += t->pluginList.size();
                          d.setProperty("total_tracks", ws.tracks().size());
                          d.setProperty("plugin_instances_including_utilities", plugins);
                          d.setProperty("test_vst3_instances", vstCount + groups);
                          d.setProperty("aux_returns", 4);
                          d.setProperty("sidechain_routes", groups);
                      });
                phase("random_edit_undo",
                      [&]
                      {
                          std::mt19937 random(513);
                          for (int n = 0; n < o.edits; ++n)
                          {
                              int t = static_cast<int>(random() % static_cast<unsigned>(o.tracks));
                              int action = n % 6;
                              if (action == 0)
                              {
                                  bool before = ws.mono(t);
                                  ws.setMono(t, !before);
                                  ws.undo();
                                  require(ws.mono(t) == before, "mono undo under load");
                              }
                              if (action == 1)
                              {
                                  int size = ws.track(t)->getClips().size();
                                  require(ws.duplicate(ws.track(t)->getClips()[0]) != nullptr,
                                          "duplicate under load");
                                  ws.undo();
                                  require(ws.track(t)->getClips().size() == size,
                                          "duplicate undo under load");
                              }
                              if (action == 2)
                              {
                                  auto* m =
                                      dynamic_cast<te::MidiClip*>(ws.track(midiStart + n % 8)->getClips()[0]);
                                  int pitch = m->getSequence().getNotes()[0]->getNoteNumber();
                                  ok(ws.transpose(m, 1));
                                  ws.undo();
                                  require(m->getSequence().getNotes()[0]->getNoteNumber() == pitch,
                                          "dense MIDI transpose undo");
                              }
                              if (action == 3)
                              {
                                  int before = ws.track(t)->pluginList.size();
                                  auto p = ws.addEffect(t, te::CompressorPlugin::xmlTypeName);
                                  require(p != nullptr, "insert under load");
                                  p = nullptr;
                                  ws.undo();
                                  require(ws.track(t)->pluginList.size() == before, "insert undo under load");
                              }
                              if (action == 4)
                              {
                                  auto* before = ws.track(t)->getOutput().getDestinationTrack();
                                  ok(ws.route(t, rootBus));
                                  ws.undo();
                                  require(ws.track(t)->getOutput().getDestinationTrack() == before,
                                          "route undo under load");
                              }
                              if (action == 5)
                              {
                                  auto* m =
                                      dynamic_cast<te::MidiClip*>(ws.track(midiStart + n % 8)->getClips()[0]);
                                  double before = m->getSequence().getNotes()[10]->getStartBeat().inBeats();
                                  ok(ws.humanize(m, 10, 5, 513 + n));
                                  ws.undo();
                                  require(std::abs(m->getSequence().getNotes()[10]->getStartBeat().inBeats() -
                                                   before) < 1e-8,
                                          "dense MIDI humanize undo");
                              }
                              if (n % 25 == 0)
                                  pump(2);
                          }
                      });
                phase("save_reload_recovery_cycles",
                      [&]
                      {
                          ok(ws.save());
                          auto file = ws.projectFile;
                          auto tracks = ws.tracks().size();
                          for (int n = 0; n < o.cycles; ++n)
                          {
                              ws.begin("Cycle name");
                              auto name = "Cycle " + juce::String(n);
                              ws.track(0)->setName(name);
                              ok(ws.save());
                              ok(ws.open(file));
                              pump(250); // Allow asynchronous post-load initialization to settle.
                              require(ws.tracks().size() == tracks && ws.track(0)->getName() == name,
                                      "reload preserves large session");
                              require(ws.track(0)->getOutput().getDestinationTrack() == ws.track(busStart),
                                      "reload preserves bus route");
                              auto* m = dynamic_cast<te::MidiClip*>(ws.track(midiStart)->getClips()[0]);
                              require(m && m->getSequence().getNotes().size() == o.notes,
                                      "reload preserves dense MIDI");
                              require(ws.missingMedia().empty(), "reload resolves media");
                              sample("reload_" + juce::String(n));
                          }
                          report.getDynamicObject()->setProperty("session_bytes", file.getSize());
                          ws.begin("Recovery edit");
                          ws.track(0)->setName("Recovery stress");
                          ws.autosave();
                          ok(ws.recover());
                          require(ws.track(0)->getName() == "Recovery stress", "large session recovery");
                      });
                phase("live_callback_matrix",
                      [&]
                      {
                          for (int i = 0; i < 8; ++i)
                              ws.track(midiStart + i)->setMute(false);
                          const double rates[]{44100, 48000, 48000, 96000};
                          const int blocks[]{64, 128, 512, 256};
                          for (int i = 0; i < 4; ++i)
                          {
                              ws.stop();
                              io.prepareToPlay(rates[i], blocks[i]);
                              dm.dispatchPendingUpdates();
                              ok(ws.transportSettings(0, 16, true, false, 0));
                              ws.seek(0);
                              ws.togglePlay();
                              pump(200);
                              auto stats = callbacks(ws, rates[i], blocks[i], o.seconds / 4);
                              auto result = stats.json(rates[i], blocks[i]);
                              playback.add(result);
                              std::cout << "PROFILE " << juce::JSON::toString(result, true) << std::endl;
                              ws.stop();
                              pump();
                          }
                      });
                phase("multitrack_disk_recording",
                      [&]
                      {
                          ws.newSession(root.getChildFile("Recording/Session.bte"));
                          io.prepareToPlay(48000, 256);
                          dm.dispatchPendingUpdates();
                          ws.edit->setCountInMode(te::Edit::CountIn::none);
                          for (int t = 0; t < 8; ++t)
                              ws.addTrack("Recorded " + juce::String(t));
                          ws.configureInputs();
                          pump();
                          int device = -1;
                          auto devs = ws.edit->getAllInputDevices();
                          for (int n = 0; n < devs.size(); ++n)
                              if (devs[n]->getInputDevice().getDeviceType() == te::InputDevice::waveDevice)
                              {
                                  device = n;
                                  break;
                              }
                          require(device >= 0, "recording source available");
                          for (int t = 0; t < 8; ++t)
                              ok(ws.assignInput(t, device, true));
                          ok(ws.record());
                          pump(100);
                          auto stats = callbacks(ws, 48000, 256, o.recordSeconds, true);
                          report.getDynamicObject()->setProperty("recording_callback",
                                                                 stats.json(48000, 256));
                          ws.stop();
                          pump(300);
                          for (int t = 0; t < 8; ++t)
                          {
                              require(ws.track(t)->getClips().size() == 1, "recording creates eight takes");
                              auto* c = dynamic_cast<te::WaveAudioClip*>(ws.track(t)->getClips()[0]);
                              require(c != nullptr, "recording is audio");
                              auto r = read(c->getOriginalFile());
                              require(r && r->lengthInSamples >=
                                               static_cast<juce::int64>((o.recordSeconds - .2) * 48000),
                                      "recording duration preserved");
                              juce::AudioBuffer<float> data(2, 4096);
                              r->read(&data, 0, 4096, std::max<juce::int64>(0, r->lengthInSamples - 8192),
                                      true, true);
                              require(data.getMagnitude(0, 4096) > .03f, "recorded tail contains input");
                          }
                          ok(ws.save());
                          auto file = ws.projectFile;
                          ok(ws.open(file));
                          pump(250); // Allow asynchronous post-load initialization to settle.
                          require(ws.tracks().size() == 8 && ws.track(7)->getClips().size() == 1,
                                  "eight takes survive reload");
                      });
                phase("aux_bus_capacity",
                      [&]
                      {
                          ws.newSession(root.getChildFile("AuxLimit/Session.bte"));
                          for (int i = 0; i < 4; ++i)
                              ws.addTrack("Source " + juce::String(i));
                          for (int i = 0; i < 33; ++i)
                              ws.addTrack("Aux " + juce::String(i + 1), false, true);
                          for (int i = 0; i < 32; ++i)
                          {
                              int source = i % 4;
                              int before = ws.track(source)->pluginList.size();
                              ok(ws.send(source, 4 + i));
                              require(ws.track(source)->pluginList.size() == before + 1,
                                      "successful send actually appears in chain");
                          }
                          int before = ws.track(0)->pluginList.size();
                          require(ws.send(0, 36).failed(), "33rd auxiliary bus rejected");
                          require(ws.track(0)->pluginList.size() == before &&
                                      static_cast<int>(ws.edit->state["BTE_nextAux"]) == 32,
                                  "aux capacity failure preserves routing");
                          require(ws.route(4, 0).failed(), "aux capacity routing remains cycle safe");
                          int limit = ws.engine.getEngineBehaviour().getEditLimits().maxPluginsOnTrack;
                          while (ws.track(0)->pluginList.size() < limit)
                              require(ws.addEffect(0, te::EqualiserPlugin::xmlTypeName) != nullptr,
                                      "fill channel to capacity");
                          require(ws.addEffect(0, te::CompressorPlugin::xmlTypeName) == nullptr,
                                  "full channel refuses stock effect");
                          require(ws.loadVst(0, o.vst).failed(), "full channel refuses VST3");
                          require(ws.send(0, 4).failed(), "full channel refuses send");
                          require(ws.track(0)->pluginList.size() == limit,
                                  "capacity failure preserves chain");
                      });
                phase("session_teardown",
                      [&]
                      {
                          ws.edit.reset();
                          dm.deviceManager.closeAudioDevice();
                          dm.removeHostedAudioDeviceInterface();
                          pump();
                      });
            }
            sample("engine_destroyed");
            report.getDynamicObject()->setProperty("artifacts_retained", o.keep);
            if (!o.keep)
                root.deleteRecursively();
            write(true);
            std::cout << "PASS " << checks
                      << " stress integrity checks; callback deadlines are reported separately\nReport: "
                      << o.report.getFullPathName() << std::endl;
        }
        catch (const std::exception& e)
        {
            report.getDynamicObject()->setProperty("artifacts_retained", true);
            sample("failure");
            write(false, e.what());
            throw;
        }
    }
};
} // namespace
int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    try
    {
        Options o;
        for (int i = 1; i < argc; ++i)
        {
            juce::String key(argv[i]);
            if (key == "--keep-session")
            {
                o.keep = true;
                continue;
            }
            if (i + 1 >= argc)
                throw std::runtime_error("Option needs a value");
            juce::String value(argv[++i]);
            if (key == "--vst")
                o.vst = juce::File(value);
            else if (key == "--report")
                o.report = juce::File(value);
            else if (key == "--tracks")
                o.tracks = value.getIntValue();
            else if (key == "--notes")
                o.notes = value.getIntValue();
            else if (key == "--cycles")
                o.cycles = value.getIntValue();
            else if (key == "--edits")
                o.edits = value.getIntValue();
            else if (key == "--threads")
                o.threads = value.getIntValue();
            else if (key == "--seconds")
                o.seconds = value.getDoubleValue();
            else if (key == "--record-seconds")
                o.recordSeconds = value.getDoubleValue();
            else
                throw std::runtime_error("Unknown option: " + key.toStdString());
        }
        if (o.report == juce::File{} || !o.vst.exists() || o.tracks < 8 || o.tracks > 256 || o.notes < 16 ||
            o.notes > 8192 || o.cycles < 1 || o.cycles > 100 || o.edits < 1 || o.edits > 10000 ||
            o.threads < 1 || o.threads > 64 || !std::isfinite(o.seconds) || o.seconds < 4 ||
            o.seconds > 3600 || !std::isfinite(o.recordSeconds) || o.recordSeconds < 1 ||
            o.recordSeconds > 600)
            throw std::runtime_error(
                "Use --vst <fixture> --report <json>; tracks 8..256, notes 16..8192, seconds 4..3600, "
                "record-seconds 1..600, threads 1..64, cycles 1..100, edits 1..10000");
        Stress stress(o);
        stress.run();
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "STRESS FAILURE: " << e.what() << std::endl;
        return 1;
    }
}
