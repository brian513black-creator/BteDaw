#pragma once
#include "Core.h"
#include "InputTrim.h"
#include <tracktion_engine/tracktion_engine.h>
namespace bte
{
namespace te = tracktion;
struct HostBehaviour : te::EngineBehaviour
{
    explicit HostBehaviour(bool audio) : enableAudio(audio) {}
    bool autoInitialiseDeviceManager() override
    {
        return enableAudio;
    }
    bool enableAudio;
};
class Workstation
{
  public:
    explicit Workstation(bool audio = true, juce::File root = {});
    ~Workstation();
    te::Engine engine;
    std::unique_ptr<te::Edit> edit;
    juce::File projectFile;
    juce::String status;
    std::function<void()> beforeSessionChange;
    juce::Array<te::AudioTrack*> tracks() const;
    te::AudioTrack* track(int) const;
    te::AudioTrack* addTrack(const juce::String& name, bool instrument = false, bool bus = false);
    juce::Result importAudio(const juce::File&, int track = -1, double seconds = 0);
    juce::Result importMidi(const juce::File&);
    te::MidiClip* addMidiClip(int, double startBeat, double lengthBeats = 16);
    void newSession(const juce::File&);
    juce::Result save(const juce::File& = {});
    juce::Result open(const juce::File&);
    juce::Result recover();
    juce::Result exportWav(const juce::File&, bool normalise = false, bool loopOnly = false, int stem = -1);
    juce::Result exportStems(const juce::File& folder);
    void autosave();
    void begin(const juce::String&);
    void undo();
    void redo();
    void togglePlay();
    void stop();
    juce::Result record();
    juce::StringArray inputs() const;
    void configureInputs();
    juce::Result assignInput(int track, int device, bool armed);
    bool armed(int) const;
    double bpm() const;
    void setBpm(double);
    double seconds(double beats) const;
    double beats(double seconds) const;
    double position() const;
    void seek(double);
    void setLoop(double startBeat, double endBeat);
    te::VolumeAndPanPlugin* fader(int) const;
    InputTrim* trim(int) const;
    void setMono(int, bool);
    bool mono(int) const;
    juce::Result route(int source, int dest);
    juce::Result send(int source, int dest, float db = -12);
    juce::Result sidechain(int source, int dest, int plugin);
    te::Plugin::Ptr addEffect(int track, const juce::String& type);
    juce::Result loadVst(int track, const juce::File&);
    std::vector<te::Plugin*> inserts(int track) const;
    void removeEffect(int track, int insert);
    void setParameter(int track, int insert, int parameter, float normalised);
    void automationPoint(int track, double atSeconds, float db);
    juce::Result autoLevel();
    juce::Result split(te::Clip*);
    te::Clip* duplicate(te::Clip*);
    void deleteClip(te::Clip*);
    void addChordProgression(int track, int bars = 4);
    int createDrumGroove(int bars = 4);
    juce::Result createDemo();
    juce::Result plan(const juce::String& command);
    juce::Result applyPlan();
    juce::String pendingSummary;
    juce::AudioDeviceManager& audioDevices()
    {
        return engine.getDeviceManager().deviceManager;
    }
    bool isBus(int i) const
    {
        auto* t = track(i);
        return t && static_cast<bool>(t->state.getProperty("BTE_bus", false));
    }
    static const juce::StringArray& effectNames();
    static const juce::StringArray& effectTypes();

  private:
    juce::File workspace;
    juce::var pending;
    void installStrip(te::AudioTrack&);
    bool wouldFeedback(int source, int destination) const;
    te::Plugin::Ptr utility(int, const juce::String& role) const;
};
} // namespace bte
