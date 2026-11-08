#include "ProEngine.h"
#include "DrumMachine.h"
namespace bte
{
namespace
{
class HostUI : public te::UIBehaviour
{
  public:
    bool cancelled = false;
    void runTaskWithProgressBar(te::ThreadPoolJobWithProgress& task) override
    {
        struct RenderProgress : juce::ThreadWithProgressWindow
        {
            te::ThreadPoolJobWithProgress& job;
            explicit RenderProgress(te::ThreadPoolJobWithProgress& t)
                : ThreadWithProgressWindow(t.getJobName(), true, true), job(t)
            {
            }
            void run() override
            {
                while (!threadShouldExit())
                {
                    if (job.runJob() == juce::ThreadPoolJob::jobHasFinished)
                        return;
                    setProgress(job.getCurrentTaskProgress());
                }
                job.signalJobShouldExit();
            }
        } progress(task);
        cancelled = !progress.runThread();
    }
    void showWarningAlert(const juce::String& title, const juce::String& message) override
    {
        juce::Logger::writeToLog(title + ": " + message);
    }
};
juce::Result fail(const juce::String& s)
{
    return juce::Result::fail(s);
}
te::TimePosition tp(double s)
{
    return te::TimePosition::fromSeconds(s);
}
te::TimeDuration td(double s)
{
    return te::TimeDuration::fromSeconds(s);
}
} // namespace
Workstation::Workstation(bool audio, juce::File root)
    : engine(audio ? "BTE Studio" : "BTE Studio Tests", std::make_unique<HostUI>(),
             std::make_unique<HostBehaviour>(audio))
{
    workspace = root == juce::File{} ? juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                                           .getChildFile("BTE Studio/Sessions")
                                     : root;
    workspace.createDirectory();
    engine.getPluginManager().createBuiltInType<DrumMachine>();
    engine.getPluginManager().createBuiltInType<InputTrim>();
    newSession(workspace.getNonexistentChildFile("Untitled", "", false).getChildFile("Session.bte"));
}
Workstation::~Workstation()
{
    if (edit)
    {
        stop();
        autosave();
        edit.reset();
    }
}
juce::Array<te::AudioTrack*> Workstation::tracks() const
{
    return te::getAudioTracks(*edit);
}
te::AudioTrack* Workstation::track(int index) const
{
    auto list = tracks();
    return juce::isPositiveAndBelow(index, list.size()) ? list[index] : nullptr;
}
void Workstation::begin(const juce::String& text)
{
    edit->getUndoManager().beginNewTransaction(text);
}
void Workstation::newSession(const juce::File& file)
{
    if (beforeSessionChange)
        beforeSessionChange();
    if (edit)
    {
        stop();
        autosave();
    }
    file.getParentDirectory().createDirectory();
    auto staged = te::createEmptyEdit(engine, file);
    if (!staged)
    {
        status = "Cannot create session";
        return;
    }
    edit = std::move(staged);
    projectFile = file;
    for (auto* t : te::getAudioTracks(*edit))
        edit->deleteTrack(t);
    edit->getMasterVolumePlugin()->setVolumeDb(0);
    auto masterMode = edit->getPluginCache().createNewPlugin(te::PatchBayPlugin::xmlTypeName, {});
    masterMode->state.setProperty("BTE_role", "channelMode", nullptr);
    edit->getMasterPluginList().insertPlugin(masterMode, 0, nullptr);
    auto meter = edit->getPluginCache().createNewPlugin(te::LevelMeterPlugin::xmlTypeName, {});
    edit->getMasterPluginList().insertPlugin(meter, -1, nullptr);
    edit->clickTrackEnabled = false;
    edit->playInStopEnabled = true;
    edit->setTempDirectory(file.getParentDirectory().getChildFile("Cache"));
    edit->getTransport().setLoopRange({tp(0), tp(8)});
    pending = juce::var{};
    pendingSummary = {};
    te::EditFileOperations(*edit).writeToFile(file, false);
    edit->resetChangedStatus();
    status = "Session created";
    if (engine.getDeviceManager().deviceManager.getCurrentAudioDevice())
        configureInputs();
}
void Workstation::installStrip(te::AudioTrack& t)
{
    auto patch = edit->getPluginCache().createNewPlugin(te::PatchBayPlugin::xmlTypeName, {});
    patch->state.setProperty("BTE_role", "channelMode", nullptr);
    t.pluginList.insertPlugin(patch, 0, nullptr);
    auto trim = edit->getPluginCache().createNewPlugin(InputTrim::xmlTypeName, {});
    trim->state.setProperty("BTE_role", "trim", nullptr);
    t.pluginList.insertPlugin(trim, 1, nullptr);
}
te::AudioTrack* Workstation::addTrack(const juce::String& name, bool instrument, bool bus)
{
    begin("Add channel");
    auto t = edit->insertNewAudioTrack(te::TrackInsertPoint::getEndOfTracks(*edit), nullptr);
    if (!t)
        return nullptr;
    t->setName(name);
    t->state.setProperty("BTE_bus", bus, &edit->getUndoManager());
    installStrip(*t);
    if (instrument)
    {
        t->state.setProperty("BTE_instrument", true, &edit->getUndoManager());
        auto synth = edit->getPluginCache().createNewPlugin(te::FourOscPlugin::xmlTypeName, {});
        t->pluginList.insertPlugin(synth, 0, nullptr);
    }
    return t.get();
}
te::Plugin::Ptr Workstation::utility(int index, const juce::String& role) const
{
    if (index == -1)
        for (auto* p : edit->getMasterPluginList())
            if (p->state["BTE_role"].toString() == role)
                return p;
    if (auto* t = track(index))
        for (auto* p : t->pluginList)
            if (p->state["BTE_role"].toString() == role)
                return p;
    return {};
}
te::VolumeAndPanPlugin* Workstation::fader(int index) const
{
    if (index == -1)
        return edit->getMasterVolumePlugin().get();
    if (auto* t = track(index))
        for (auto* p : t->pluginList)
            if (auto* v = dynamic_cast<te::VolumeAndPanPlugin*>(p); v && !v->state.hasProperty("BTE_role"))
                return v;
    return nullptr;
}
InputTrim* Workstation::trim(int index) const
{
    return dynamic_cast<InputTrim*>(utility(index, "trim").get());
}
void Workstation::setMono(int index, bool enabled)
{
    if (auto* patch = dynamic_cast<te::PatchBayPlugin*>(utility(index, "channelMode").get()))
    {
        begin("Channel mode");
        for (int i = 0; i < 2; ++i)
            for (int j = 0; j < 2; ++j)
                patch->breakConnection(i, j);
        for (int i = 0; i < 2; ++i)
            for (int j = 0; j < 2; ++j)
                if (enabled || i == j)
                    patch->makeConnection(i, j, enabled ? -6.0206f : 0.0f, &edit->getUndoManager());
        (index == -1 ? edit->state : track(index)->state)
            .setProperty("BTE_mono", enabled, &edit->getUndoManager());
    }
}
bool Workstation::mono(int i) const
{
    if (i == -1)
        return static_cast<bool>(edit->state["BTE_mono"]);
    auto* t = track(i);
    return t && static_cast<bool>(t->state["BTE_mono"]);
}
juce::Result Workstation::importAudio(const juce::File& file, int index, double at)
{
    te::AudioFile audio(engine, file);
    if (!audio.isValid() || audio.getLength() <= 0)
        return fail("Cannot decode " + file.getFileName());
    begin("Import audio");
    auto media = projectFile.getParentDirectory().getChildFile("Media");
    if (media.createDirectory().failed())
        return fail("Cannot create project media folder");
    auto copy =
        media.getNonexistentChildFile(file.getFileNameWithoutExtension(), file.getFileExtension(), false);
    if (!file.copyFileTo(copy))
        return fail("Cannot copy media into the project");
    auto* t = track(index);
    if (!t)
        t = addTrack(file.getFileNameWithoutExtension());
    auto c = t->insertWaveClip(file.getFileNameWithoutExtension(), copy,
                               {{tp(at), tp(at + audio.getLength())}, {}}, false);
    if (!c)
        return fail("Could not create audio clip");
    c->setFadeIn(td(0.005));
    c->setFadeOut(td(0.005));
    return juce::Result::ok();
}
te::MidiClip* Workstation::addMidiClip(int index, double startBeat, double length)
{
    auto* t = track(index);
    if (!t)
        return nullptr;
    begin("Create MIDI region");
    return t
        ->insertMIDIClip("MIDI region", {tp(seconds(startBeat)), tp(seconds(startBeat + length))}, nullptr)
        .get();
}
juce::Result Workstation::importMidi(const juce::File& file)
{
    auto stream = file.createInputStream();
    juce::MidiFile midi;
    if (!stream || !midi.readFrom(*stream))
        return fail("Invalid MIDI file");
    begin("Import MIDI");
    int n = 0;
    for (int i = 0; i < midi.getNumTracks(); ++i)
    {
        auto sequence = *midi.getTrack(i);
        if (sequence.getNumEvents() == 0)
            continue;
        bool notes = false;
        for (int j = 0; j < sequence.getNumEvents(); ++j)
            notes = notes || sequence.getEventPointer(j)->message.isNoteOn();
        if (!notes)
            continue;
        // MidiList accepts seconds when given an Edit. Convert ticks via the MIDI file's tempo map first.
        if (n == 0)
            midi.convertTimestampTicksToSeconds();
        sequence = *midi.getTrack(i);
        addTrack(file.getFileNameWithoutExtension() + " " + juce::String(++n), true);
        int idx = tracks().size() - 1;
        auto* c = addMidiClip(idx, 0, std::max(16.0, beats(sequence.getEndTime()) + 4));
        c->getSequence().importMidiSequence(sequence, edit.get(), tp(0), &edit->getUndoManager());
    }
    return n ? juce::Result::ok() : fail("MIDI file has no note tracks");
}
juce::Result Workstation::save(const juce::File& dest)
{
    if (edit->getTransport().isRecording())
        return fail("Stop recording before saving to another file");
    auto file = dest == juce::File{} ? projectFile : dest;
    edit->flushState();
    if (!file.getParentDirectory().createDirectory())
        return fail("Cannot create project folder");
    juce::TemporaryFile temp(file);
    if (file.getParentDirectory() != projectFile.getParentDirectory())
    {
        // Save a portable copy without relocating the live recording session or mutating its media refs.
        auto state = edit->state.createCopy();
        auto media = file.getParentDirectory().getChildFile("Media");
        if (media.createDirectory().failed())
            return fail("Cannot create copied session's media folder");
        for (auto* t : tracks())
            for (auto* clip : t->getClips())
                if (auto* wave = dynamic_cast<te::WaveAudioClip*>(clip))
                {
                    auto original = wave->getOriginalFile();
                    if (!original.existsAsFile())
                        return fail("Missing media: " + original.getFileName());
                    auto copy = media.getNonexistentChildFile(original.getFileNameWithoutExtension(),
                                                              original.getFileExtension(), false);
                    if (!original.copyFileTo(copy))
                        return fail("Could not copy media");
                    std::function<void(juce::ValueTree)> rewrite = [&](juce::ValueTree node)
                    {
                        if (node[te::IDs::id] == wave->state[te::IDs::id] &&
                            node.getType() == wave->state.getType())
                            node.setProperty(te::IDs::source,
                                             copy.getRelativePathFrom(file.getParentDirectory()), nullptr);
                        for (auto child : node)
                            rewrite(child);
                    };
                    rewrite(state);
                }
        auto xml = state.createXml();
        if (!xml || !xml->writeTo(temp.getFile()) || !temp.overwriteTargetFileWithTemporary())
            return fail("Could not save session copy");
    }
    else if (!te::EditFileOperations(*edit).writeToFile(temp.getFile(), false) ||
             !temp.overwriteTargetFileWithTemporary())
        return fail("Could not save session");
    // A saved copy does not relocate this live recording session.
    if (file == projectFile)
        edit->resetChangedStatus();
    return juce::Result::ok();
}
juce::Result Workstation::open(const juce::File& file)
{
    if (file.getSize() > 64 * 1024 * 1024)
        return fail("Session is too large");
    auto xml = juce::parseXML(file);
    if (!xml || !xml->hasTagName("EDIT"))
        return fail("Not a valid BTE/Tracktion session");
    if (beforeSessionChange)
        beforeSessionChange();
    stop();
    autosave();
    auto staged = te::loadEditFromFile(engine, file);
    if (!staged)
        return fail("Session could not be loaded");
    edit = std::move(staged);
    projectFile = file;
    edit->playInStopEnabled = true;
    pending = juce::var{};
    pendingSummary = {};
    return juce::Result::ok();
}
void Workstation::autosave()
{
    if (edit)
        edit->getUndoManager().dispatchPendingMessages();
    if (edit && edit->hasChangedSinceSaved() && !edit->getTransport().isRecording())
        te::EditFileOperations(*edit).saveTempVersion(true);
}
juce::Result Workstation::recover()
{
    auto file = te::EditFileOperations::getTempVersionOfEditFile(projectFile);
    if (!file.existsAsFile())
        return fail("No recovery snapshot for this session");
    auto main = projectFile;
    auto result = open(file);
    if (result.wasOk())
    {
        edit->editFileRetriever = [main] { return main; };
        projectFile = main;
    }
    return result;
}
void Workstation::undo()
{
    edit->undo();
    edit->dispatchPendingUpdatesSynchronously();
    for (auto* p : edit->getAllAutomatableParams(true))
        p->updateFromAttachedValue();
}
void Workstation::redo()
{
    edit->redo();
    edit->dispatchPendingUpdatesSynchronously();
    for (auto* p : edit->getAllAutomatableParams(true))
        p->updateFromAttachedValue();
}
void Workstation::togglePlay()
{
    auto& t = edit->getTransport();
    if (t.isPlaying())
        t.stop(false, false);
    else
        t.play(false);
}
void Workstation::stop()
{
    edit->getTransport().stop(false, false);
}
juce::Result Workstation::record()
{
    if (edit->getTransport().isRecording())
    {
        stop();
        save();
        return juce::Result::ok();
    }
    bool ready = false;
    for (auto* t : tracks())
        for (auto* d : edit->getAllInputDevices())
            ready = ready || (te::isOnTargetTrack(*d, *t, 0) && d->isRecordingEnabled(t->itemID));
    if (!ready)
        return fail("Choose an input and arm a track before recording");
    if (!audioDevices().getCurrentAudioDevice())
        return fail("Select an audio device first");
    edit->getTransport().record(false);
    return juce::Result::ok();
}
void Workstation::configureInputs()
{
    auto& dm = engine.getDeviceManager();
    for (int i = 0; i < dm.getNumWaveInDevices(); ++i)
    {
        auto* in = dm.getWaveInDevice(i);
        if (!in->isEnabled())
        {
            in->setMonitorMode(te::InputDevice::MonitorMode::off);
            in->setEnabled(true);
        }
    }
    for (auto in : dm.getMidiInDevices())
        if (!in->isEnabled())
        {
            in->setMonitorMode(te::InputDevice::MonitorMode::off);
            in->setEnabled(true);
        }
    edit->getTransport().ensureContextAllocated();
}
juce::StringArray Workstation::inputs() const
{
    juce::StringArray result;
    for (auto* d : edit->getAllInputDevices())
        result.add(d->getInputDevice().getName());
    return result;
}
juce::Result Workstation::assignInput(int index, int device, bool arm)
{
    begin("Assign recording input");
    auto* t = track(index);
    if (!t)
        return fail("No channel selected");
    auto devices = edit->getAllInputDevices();
    for (auto* d : devices)
        if (te::isOnTargetTrack(*d, *t, 0))
        {
            d->setRecordingEnabled(t->itemID, false);
            auto r = d->removeTarget(t->itemID, &edit->getUndoManager());
            juce::ignoreUnused(r);
        }
    if (device < 0)
        return juce::Result::ok();
    if (!juce::isPositiveAndBelow(device, devices.size()))
        return fail("Input device is unavailable");
    auto* d = devices[device];
    auto r = d->setTarget(t->itemID, false, &edit->getUndoManager(), 0);
    if (!r)
        return fail(r.error());
    d->setRecordingEnabled(t->itemID, arm);
    return juce::Result::ok();
}
bool Workstation::armed(int index) const
{
    if (auto* t = track(index))
        for (auto* d : edit->getAllInputDevices())
            if (te::isOnTargetTrack(*d, *t, 0) && d->isRecordingEnabled(t->itemID))
                return true;
    return false;
}
double Workstation::bpm() const
{
    return edit->tempoSequence.getTempo(0)->getBpm();
}
void Workstation::setBpm(double value)
{
    begin("Set tempo");
    edit->tempoSequence.getTempo(0)->setBpm(std::clamp(value, 30.0, 300.0));
}
double Workstation::seconds(double b) const
{
    return edit->tempoSequence.toTime(te::BeatPosition::fromBeats(b)).inSeconds();
}
double Workstation::beats(double s) const
{
    return edit->tempoSequence.toBeats(tp(s)).inBeats();
}
double Workstation::position() const
{
    return edit->getTransport().getPosition().inSeconds();
}
void Workstation::seek(double s)
{
    edit->getTransport().setPosition(tp(std::max(0.0, s)));
}
void Workstation::setLoop(double first, double last)
{
    begin("Loop range");
    edit->getTransport().setLoopRange({tp(seconds(first)), tp(seconds(std::max(first + 1, last)))});
}
bool Workstation::wouldFeedback(int from, int to) const
{
    auto channels = tracks();
    if (!juce::isPositiveAndBelow(from, channels.size()) || !juce::isPositiveAndBelow(to, channels.size()))
        return false;
    std::vector<std::vector<int>> edges(static_cast<size_t>(channels.size()));
    for (int i = 0; i < channels.size(); ++i)
    {
        int out = channels.indexOf(channels[i]->getOutput().getDestinationTrack());
        if (out >= 0)
            edges[static_cast<size_t>(i)].push_back(out);
        for (auto* p : channels[i]->pluginList)
        {
            auto sc = p->getSidechainSourceID();
            if (sc.isValid())
                for (int j = 0; j < channels.size(); ++j)
                    if (channels[j]->itemID == sc)
                        edges[static_cast<size_t>(j)].push_back(i);
            if (auto* send = dynamic_cast<te::AuxSendPlugin*>(p))
                for (int j = 0; j < channels.size(); ++j)
                    for (auto* target : channels[j]->pluginList)
                        if (auto* ret = dynamic_cast<te::AuxReturnPlugin*>(target);
                            ret && ret->busNumber.get() == send->getBusNumber())
                            edges[static_cast<size_t>(i)].push_back(j);
        }
    }
    std::vector<bool> visited(static_cast<size_t>(channels.size()));
    std::vector<int> pending{to};
    while (!pending.empty())
    {
        int node = pending.back();
        pending.pop_back();
        if (node == from)
            return true;
        if (visited[static_cast<size_t>(node)])
            continue;
        visited[static_cast<size_t>(node)] = true;
        for (int next : edges[static_cast<size_t>(node)])
            pending.push_back(next);
    }
    return false;
}
juce::Result Workstation::route(int from, int to)
{
    auto* s = track(from);
    auto* d = track(to);
    if (!s)
        return fail("Select a source channel");
    if (d && wouldFeedback(from, to))
        return fail("This route would create feedback");
    begin("Route output");
    if (d)
        s->getOutput().setOutputToTrack(d);
    else
        s->getOutput().setOutputToDefaultDevice(false);
    return juce::Result::ok();
}
juce::Result Workstation::send(int from, int to, float db)
{
    auto* s = track(from);
    auto* d = track(to);
    if (!s || !d || wouldFeedback(from, to))
        return fail("Choose a different destination without feedback");
    begin("Create post-fader send");
    int bus = static_cast<int>(d->state.getProperty("BTE_aux", -1));
    if (bus < 0)
    {
        bus = static_cast<int>(edit->state.getProperty("BTE_nextAux", 0));
        if (bus >= 32)
            return fail("32 auxiliary buses already in use");
        edit->state.setProperty("BTE_nextAux", bus + 1, &edit->getUndoManager());
        d->state.setProperty("BTE_aux", bus, &edit->getUndoManager());
        auto p = edit->getPluginCache().createNewPlugin(te::AuxReturnPlugin::xmlTypeName, {});
        dynamic_cast<te::AuxReturnPlugin*>(p.get())->busNumber = bus;
        d->pluginList.insertPlugin(p, 0, nullptr);
    }
    auto p = edit->getPluginCache().createNewPlugin(te::AuxSendPlugin::xmlTypeName, {});
    auto* aux = dynamic_cast<te::AuxSendPlugin*>(p.get());
    aux->busNumber = bus;
    aux->setGainDb(db);
    s->pluginList.insertPlugin(p, s->pluginList.size() - 1, nullptr);
    return juce::Result::ok();
}
juce::Result Workstation::sidechain(int source, int dest, int insert)
{
    auto list = inserts(dest);
    auto* s = track(source);
    if (!s || wouldFeedback(source, dest) || !juce::isPositiveAndBelow(insert, static_cast<int>(list.size())))
        return fail("Select a source and a target insert");
    auto* p = list[static_cast<size_t>(insert)];
    if (!p->canSidechain())
        return fail("This insert has no sidechain input");
    begin("Connect sidechain");
    p->setSidechainSourceID(s->itemID);
    p->guessSidechainRouting();
    return juce::Result::ok();
}
const juce::StringArray& Workstation::effectNames()
{
    static juce::StringArray names{"4OSC synthesizer", "4-band EQ",       "Compressor",
                                   "Reverb",           "Delay",           "Chorus",
                                   "Phaser",           "Low-pass filter", "BTE Drum Machine"};
    return names;
}
const juce::StringArray& Workstation::effectTypes()
{
    static juce::StringArray types{
        te::FourOscPlugin::xmlTypeName, te::EqualiserPlugin::xmlTypeName, te::CompressorPlugin::xmlTypeName,
        te::ReverbPlugin::xmlTypeName,  te::DelayPlugin::xmlTypeName,     te::ChorusPlugin::xmlTypeName,
        te::PhaserPlugin::xmlTypeName,  te::LowPassPlugin::xmlTypeName,   DrumMachine::xmlTypeName};
    return types;
}
te::Plugin::Ptr Workstation::addEffect(int index, const juce::String& type)
{
    auto* t = track(index);
    if (!t)
        return {};
    begin("Insert effect");
    auto p = edit->getPluginCache().createNewPlugin(type, {});
    if (p)
        t->pluginList.insertPlugin(
            p,
            (type == te::FourOscPlugin::xmlTypeName || type == DrumMachine::xmlTypeName)
                ? 0
                : std::max(0, t->pluginList.size() - 2),
            nullptr);
    return p;
}
juce::Result Workstation::loadVst(int index, const juce::File& file)
{
    auto* t = track(index);
    if (!t)
        return fail("Select a channel first");
    auto& manager = engine.getPluginManager();
    juce::OwnedArray<juce::PluginDescription> found;
    for (auto* format : manager.pluginFormatManager.getFormats())
        if (format->getName() == "VST3")
            format->findAllTypesForFile(found, file.getFullPathName());
    if (found.isEmpty())
        return fail("No compatible 64-bit VST3 class in this bundle");
    // Every discovered class is kept in the browser; loading this bundle picks its first class.
    for (auto* d : found)
        manager.knownPluginList.addType(*d);
    begin("Load VST3");
    auto p = edit->getPluginCache().createNewPlugin(te::ExternalPlugin::xmlTypeName, *found[0]);
    if (!p)
        return fail("Plugin could not be instantiated");
    t->pluginList.insertPlugin(p, found[0]->isInstrument ? 0 : std::max(0, t->pluginList.size() - 2),
                               nullptr);
    return juce::Result::ok();
}
std::vector<te::Plugin*> Workstation::inserts(int index) const
{
    std::vector<te::Plugin*> result;
    if (auto* t = track(index))
        for (auto* p : t->pluginList)
            if (!p->state.hasProperty("BTE_role") && !dynamic_cast<te::VolumeAndPanPlugin*>(p) &&
                !dynamic_cast<te::LevelMeterPlugin*>(p))
                result.push_back(p);
    return result;
}
void Workstation::removeEffect(int index, int which)
{
    auto list = inserts(index);
    if (juce::isPositiveAndBelow(which, static_cast<int>(list.size())))
    {
        begin("Remove insert");
        list[static_cast<size_t>(which)]->deleteFromParent();
    }
}
void Workstation::setParameter(int index, int insert, int param, float value)
{
    auto list = inserts(index);
    if (!juce::isPositiveAndBelow(insert, static_cast<int>(list.size())))
        return;
    auto params = list[static_cast<size_t>(insert)]->getAutomatableParameters();
    if (juce::isPositiveAndBelow(param, params.size()))
        params[param]->setNormalisedParameter(std::clamp(value, 0.0f, 1.0f), juce::sendNotification);
}
void Workstation::automationPoint(int index, double time, float db)
{
    if (auto* f = fader(index))
    {
        begin("Write volume automation");
        f->volParam->getCurve().addPoint(tp(time), te::decibelsToVolumeFaderPosition(db), 0,
                                         &edit->getUndoManager());
        edit->getAutomationRecordManager().setReadingAutomation(true);
    }
}
juce::Result Workstation::autoLevel()
{
    begin("RMS level match");
    int count = 0;
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    for (int i = 0; i < tracks().size(); ++i)
        if (auto* t = track(i); t && !isBus(i))
        {
            double sum = 0;
            juce::int64 frames = 0;
            float peak = 0;
            for (auto* clip : t->getClips())
                if (auto* wave = dynamic_cast<te::WaveAudioClip*>(clip))
                {
                    std::unique_ptr<juce::AudioFormatReader> reader(
                        formats.createReaderFor(wave->getOriginalFile()));
                    if (!reader)
                        continue;
                    juce::AudioBuffer<float> b(2, 16384);
                    auto pos = wave->getPosition();
                    auto offset = static_cast<juce::int64>(pos.getOffset().inSeconds() * reader->sampleRate);
                    auto length = static_cast<juce::int64>(pos.getLength().inSeconds() * reader->sampleRate);
                    length = std::min(length, reader->lengthInSamples - offset);
                    for (juce::int64 n = 0; n < length; n += 16384)
                    {
                        int size = static_cast<int>(std::min<juce::int64>(16384, length - n));
                        if (!reader->read(&b, 0, size, offset + n, true, true))
                            break;
                        for (int j = 0; j < size; ++j)
                        {
                            float l = b.getSample(0, j), r = reader->numChannels > 1 ? b.getSample(1, j) : l;
                            if (mono(i))
                                l = r = (l + r) * 0.5f;
                            if (!std::isfinite(l) || !std::isfinite(r))
                                continue;
                            sum += (static_cast<double>(l) * l + static_cast<double>(r) * r) * 0.5;
                            peak = std::max(peak, std::max(std::abs(l), std::abs(r)));
                            ++frames;
                        }
                    }
                }
            if (frames > 0 && sum / frames > 1e-12 && peak > 0)
                if (auto* p = trim(i))
                {
                    p->setVolumeDb(
                        std::clamp(std::min(-18.0f - 10 * std::log10(static_cast<float>(sum / frames)),
                                            -1.0f - 20 * std::log10(peak)),
                                   -60.0f, 24.0f));
                    ++count;
                }
        }
    status = juce::String(count) + " channels matched toward -18 dBFS RMS; -1 dBFS source peak ceiling";
    return juce::Result::ok();
}
juce::Result Workstation::split(te::Clip* c)
{
    if (!c)
        return fail("Select a clip first");
    auto at = edit->getTransport().getPosition();
    if (!c->getPosition().time.contains(at))
        return fail("Place the playhead inside the selected clip");
    if (auto* t = dynamic_cast<te::ClipTrack*>(c->getTrack()))
    {
        begin("Split region");
        t->splitClip(*c, at);
        return juce::Result::ok();
    }
    return fail("Clip cannot be split");
}
te::Clip* Workstation::duplicate(te::Clip* c)
{
    if (!c)
        return nullptr;
    begin("Duplicate region");
    auto state = c->state.createCopy();
    state.removeProperty(te::IDs::id, nullptr);
    auto* copy = dynamic_cast<te::ClipTrack*>(c->getTrack())->insertClipWithState(state);
    if (copy)
        copy->setStart(c->getPosition().getEnd(), false, true);
    return copy;
}
void Workstation::deleteClip(te::Clip* c)
{
    if (c)
    {
        begin("Delete region");
        c->removeFromParent();
    }
}
void Workstation::addChordProgression(int index, int bars)
{
    auto* c = addMidiClip(index, 0, bars * 4);
    if (!c)
        return;
    c->setName("Chord progression / C - Am - F - G");
    const int roots[]{48, 45, 41, 43};
    for (int bar = 0; bar < bars; ++bar)
        for (int voice = 0; voice < 3; ++voice)
        {
            int pitch = roots[bar % 4] + (voice == 0 ? 0 : voice == 1 ? (bar % 4 == 1 ? 3 : 4) : 7);
            c->getSequence().addNote(pitch, te::BeatPosition::fromBeats(bar * 4),
                                     te::BeatDuration::fromBeats(3.8), 88, 0, &edit->getUndoManager());
        }
}
int Workstation::createDrumGroove(int bars)
{
    addTrack("BTE Drums");
    int index = tracks().size() - 1;
    addEffect(index, DrumMachine::xmlTypeName);
    auto* c = addMidiClip(index, 0, bars * 4);
    c->setName("Drum groove / Kick - Snare - Hats");
    for (int bar = 0; bar < bars; ++bar)
        for (int step = 0; step < 8; ++step)
        {
            auto beat = te::BeatPosition::fromBeats(bar * 4 + step * 0.5);
            auto length = te::BeatDuration::fromBeats(0.2);
            auto& seq = c->getSequence();
            auto* undo = &edit->getUndoManager();
            seq.addNote(42, beat, length, step % 2 == 0 ? 86 : 62, 0, undo);
            if (step == 0 || step == 4)
                seq.addNote(36, beat, length, 108, 0, undo);
            if (step == 2 || step == 6)
                seq.addNote(38, beat, length, 100, 0, undo);
        }
    return index;
}
juce::Result Workstation::createDemo()
{
    newSession(workspace.getNonexistentChildFile("BTE Example", "", false).getChildFile("Session.bte"));
    setBpm(110);
    addTrack("01 Keys", true);
    addChordProgression(0);
    fader(0)->setVolumeDb(-12);
    addTrack("02 Bass", true);
    auto* bass = addMidiClip(1, 0, 16);
    bass->setName("Bass / C - A - F - G");
    const int roots[]{36, 33, 29, 31};
    for (int bar = 0; bar < 4; ++bar)
        for (int n = 0; n < 4; ++n)
            bass->getSequence().addNote(roots[bar], te::BeatPosition::fromBeats(bar * 4 + n),
                                        te::BeatDuration::fromBeats(0.8), 94, 0, &edit->getUndoManager());
    fader(1)->setVolumeDb(-16);
    addTrack("03 Arpeggio", true);
    auto* arp = addMidiClip(2, 0, 16);
    arp->setName("Arpeggio / Eighth notes");
    for (int i = 0; i < 32; ++i)
    {
        int degrees[]{0, 4, 7, 12};
        arp->getSequence().addNote(roots[i / 8] + 24 + degrees[i % 4], te::BeatPosition::fromBeats(i * 0.5),
                                   te::BeatDuration::fromBeats(0.35), i % 2 ? 68 : 88, 0,
                                   &edit->getUndoManager());
    }
    fader(2)->setVolumeDb(-20);
    createDrumGroove();
    fader(3)->setVolumeDb(-8);
    addTrack("05 Reverb Return", false, true);
    addEffect(4, te::ReverbPlugin::xmlTypeName);
    send(0, 4, -18);
    addTrack("06 Music Bus", false, true);
    route(0, 5);
    route(1, 5);
    route(2, 5);
    fader(5)->setVolumeDb(-4);
    addEffect(5, te::EqualiserPlugin::xmlTypeName);
    setLoop(0, 16);
    edit->getTransport().looping = true;
    edit->getMasterVolumePlugin()->setVolumeDb(-1);
    seek(0);
    return save();
}
juce::Result Workstation::exportWav(const juce::File& file, bool normalise, bool loopOnly, int stem)
{
    stop();
    if (edit->getLength().inSeconds() <= 0)
        return fail("Session has no clips to render");
    edit->flushState();
    te::Edit::Options options{engine, edit->state.createCopy(), {}};
    options.role = te::Edit::forRendering;
    options.numAudioTracks = 0;
    options.editFileRetriever = [file = projectFile] { return file; };
    auto snapshot = te::Edit::createEdit(options);
    if (!snapshot)
        return fail("Could not prepare offline session");
    auto* renderEdit = snapshot.get();
    juce::Array<te::EditItemID> silentSources;
    auto renderChannels = te::getAudioTracks(*renderEdit);
    if (stem >= 0)
    {
        if (!juce::isPositiveAndBelow(stem, renderChannels.size()))
            return fail("Stem channel no longer exists");
        auto* target = renderChannels[stem];
        // Route and mute only an offline clone. Live routing, mute state and undo history stay intact.
        for (auto* t : renderChannels)
        {
            t->setSolo(false);
            if (t != target && !t->getOutput().feedsInto(target))
                t->setMute(true);
        }
        target->getOutput().setOutputToDefaultDevice(false);
    }
    else
    {
        // Tracktion's generic renderer sums disconnected tracks too. Keep their sidechain/send
        // processing alive but explicitly silence their direct output, matching device playback.
        for (auto* t : renderChannels)
            if (t->getOutput().outputsToNone())
            {
                t->setMute(true);
                silentSources.add(t->itemID);
            }
    }
    te::Renderer::Parameters p(*renderEdit);
    juce::WavAudioFormat format;
    juce::TemporaryFile temp(file);
    p.destFile = temp.getFile();
    p.audioFormat = &format;
    p.bitDepth = 24;
    p.sampleRateForAudio = 48000;
    p.usePlugins = true;
    p.useMasterPlugins = stem < 0;
    p.time = loopOnly ? edit->getTransport().getLoopRange()
                      : te::TimeRange{tp(0), tp(edit->getLength().inSeconds() + 2)};
    p.tracksToDo = te::toBitSet(te::getAllTracks(*renderEdit));
    p.shouldNormaliseByLUFS = normalise;
    p.normaliseToLevelDb = -14;
    p.limitTruePeak = true;
    p.truePeakCeilingDb = -1;
    p.canRenderInMono = false;
    p.tracksToProcessWhileMuted = silentSources;
    if (stem >= 0)
    {
        auto channels = te::getAudioTracks(*renderEdit);
        auto sources = te::findStemSourceTracks(*renderEdit, {channels[stem]->itemID});
        p.tracksToProcessWhileMuted = sources;
        p.checkNodesForAudio = false;
    }
    renderEdit->dispatchPendingUpdatesSynchronously();
    auto rendered = te::Renderer::renderToFile("Render BTE Studio mix", p);
    auto* ui = dynamic_cast<HostUI*>(&engine.getUIBehaviour());
    if (ui && ui->cancelled)
        return fail("Render cancelled. The previous output file was preserved.");
    return rendered.existsAsFile() && rendered.getSize() > 44 && temp.overwriteTargetFileWithTemporary()
               ? juce::Result::ok()
               : fail("Render failed; check output folder and plugin errors");
}
juce::Result Workstation::exportStems(const juce::File& folder)
{
    if (folder.createDirectory().failed())
        return fail("Cannot create stem folder");
    for (int i = 0; i < tracks().size(); ++i)
    {
        auto r = exportWav(folder.getChildFile(juce::File::createLegalFileName(track(i)->getName()) + "-" +
                                               juce::String(i + 1) + ".wav"),
                           false, false, i);
        if (r.failed())
            return r;
    }
    return juce::Result::ok();
}
juce::Result Workstation::plan(const juce::String& text)
{
    pending = juce::var{};
    pendingSummary = {};
    if (auto action = propose(text.toStdString(), tracks().size()))
    {
        auto* o = new juce::DynamicObject;
        o->setProperty("kind", static_cast<int>(action->kind));
        o->setProperty("track", action->track);
        o->setProperty("value", action->value);
        pending = juce::var(o);
        pendingSummary = action->summary;
        return juce::Result::ok();
    }
    if (text.trim().equalsIgnoreCase("create drum groove"))
    {
        auto* o = new juce::DynamicObject;
        o->setProperty("drums", true);
        pending = juce::var(o);
        pendingSummary = "Create a self-contained BTE drum instrument and four-bar MIDI groove";
        return juce::Result::ok();
    }
    if (text.trim().equalsIgnoreCase("create chord progression"))
    {
        auto* o = new juce::DynamicObject;
        o->setProperty("chords", true);
        pending = juce::var(o);
        pendingSummary = "Create a 4OSC instrument with a four-bar C-Am-F-G progression";
        return juce::Result::ok();
    }
    return fail(
        "Supported: tempo, track gain, mute/solo, or 'create chord progression'. No changes applied.");
}
juce::Result Workstation::applyPlan()
{
    if (pending.isVoid())
        return fail("Preview a command first");
    if (static_cast<bool>(pending["drums"]))
        createDrumGroove();
    else if (static_cast<bool>(pending["chords"]))
    {
        addTrack("4OSC Keys", true);
        addChordProgression(tracks().size() - 1);
    }
    else
    {
        auto kind = static_cast<ActionKind>(static_cast<int>(pending["kind"]));
        int index = static_cast<int>(pending["track"]);
        float value = static_cast<float>(pending["value"]);
        begin("Assistant edit");
        if (kind == ActionKind::tempo)
            setBpm(value);
        else if (auto* t = track(index))
        {
            if (kind == ActionKind::gainDelta)
            {
                if (auto* f = fader(index))
                    f->setVolumeDb(std::clamp(f->getVolumeDb() + value, -60.0f, 6.0f));
            }
            if (kind == ActionKind::mute)
                t->setMute(value != 0);
            if (kind == ActionKind::solo)
                t->setSolo(value != 0);
        }
    }
    pending = juce::var{};
    return juce::Result::ok();
}
} // namespace bte
