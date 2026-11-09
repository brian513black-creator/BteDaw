#include "ProEngine.h"
#include <random>
namespace bte
{
namespace
{
juce::Result invalid(const juce::String& message)
{
    return juce::Result::fail(message);
}
bool finite(double value, double low, double high)
{
    return std::isfinite(value) && value >= low && value <= high;
}
bool sameFile(const juce::File& a, const juce::File& b)
{
#if JUCE_WINDOWS
    return a.getFullPathName().equalsIgnoreCase(b.getFullPathName());
#else
    return a == b;
#endif
}
} // namespace
juce::Result Workstation::copyClip(te::Clip* clip, bool cut)
{
    if (!clip || &clip->edit != edit.get())
        return invalid("Select a region in the current session first");
    auto state = clip->state.createCopy();
    if (auto* audio = dynamic_cast<te::WaveAudioClip*>(clip))
    {
        auto source = audio->getOriginalFile();
        if (!source.existsAsFile())
            return invalid("Relink the missing source before copying this region");
        state.setProperty(te::IDs::source, source.getFullPathName(), nullptr);
    }
    clipboard = state;
    if (cut)
    {
        begin("Cut region");
        clip->removeFromParent();
    }
    return juce::Result::ok();
}
te::Clip* Workstation::pasteClip(int index, double time)
{
    auto* destination = track(index);
    if (!destination || !clipboard.isValid() || !finite(time, 0, 86400))
        return nullptr;
    auto state = clipboard.createCopy();
    auto source = state.getProperty(te::IDs::source).toString();
    if (source.isNotEmpty() && juce::File::isAbsolutePath(source))
    {
        juce::File original(source);
        if (!original.existsAsFile())
        {
            status = "The copied region's source is missing";
            return nullptr;
        }
        auto media = projectFile.getParentDirectory().getChildFile("Media");
        if (media.createDirectory().failed())
            return nullptr;
        auto copy = media.getNonexistentChildFile(original.getFileNameWithoutExtension(),
                                                  original.getFileExtension(), false);
        if (!original.copyFileTo(copy))
            return nullptr;
        state.setProperty(te::IDs::source, copy.getRelativePathFrom(projectFile.getParentDirectory()),
                          nullptr);
    }
    begin("Paste region");
    auto copy = te::ClipCopy::fromClipboardState(state, false).withNewItemID(*edit);
    auto* clip = te::insertClipCopy(*destination, copy);
    if (clip)
        clip->setStart(te::TimePosition::fromSeconds(time), false, true);
    return clip;
}
juce::Result Workstation::moveClip(te::Clip* clip, int destination, double time)
{
    auto* t = track(destination);
    if (!clip || &clip->edit != edit.get() || !t || !finite(time, 0, 86400))
        return invalid("Select a valid region and destination");
    begin("Move region to channel");
    if (clip->getTrack() != t && !clip->moveTo(*t))
        return invalid("This region cannot be moved to that channel");
    clip->setStart(te::TimePosition::fromSeconds(time), false, true);
    return juce::Result::ok();
}
juce::Result Workstation::editRegion(te::Clip* clip, const RegionSettings& settings)
{
    if (!clip || &clip->edit != edit.get())
        return invalid("Select a region first");
    if (settings.name.trim().isEmpty() || !finite(settings.gainDb, -60, 24) ||
        !finite(settings.pitchSemitones, -48, 48) || !finite(settings.fadeInSeconds, 0, 600) ||
        !finite(settings.fadeOutSeconds, 0, 600) || !finite(settings.lengthBeats, 0, 100000))
        return invalid("Region values are outside their supported ranges");
    auto* audio = dynamic_cast<te::AudioClipBase*>(clip);
    double length = clip->getPosition().getLength().inSeconds();
    if (settings.lengthBeats > 0)
    {
        double start = clip->getPosition().getStart().inSeconds();
        length = seconds(beats(start) + settings.lengthBeats) - start;
    }
    if (length < 0.01)
        return invalid("A region must be at least 10 ms long");
    if (audio && (settings.fadeInSeconds > length || settings.fadeOutSeconds > length))
        return invalid("Fade lengths cannot exceed the region length");
    double current = clip->getPosition().getLength().inSeconds();
    if (audio && (current <= 0 || length / current <= 0.01 ||
                  audio->getSpeedRatio() * current / length < te::ClipConstants::speedRatioMin ||
                  audio->getSpeedRatio() * current / length > te::ClipConstants::speedRatioMax))
        return invalid("The requested stretch exceeds the audio speed range");
    begin("Region properties");
    clip->setName(settings.name.trim().substring(0, 128));
    if (audio)
    {
        audio->setTimeStretchMode(te::TimeStretcher::soundtouchBetter);
        audio->setGainDB(settings.gainDb);
        audio->setPitchChange(settings.pitchSemitones);
        audio->setIsReversed(settings.reversed);
    }
    if (settings.lengthBeats > 0)
    {
        double current = clip->getPosition().getLength().inSeconds();
        if (std::abs(current - length) > 1e-8)
        {
            if (audio)
                audio->setAutoTempo(false);
            if (audio)
                clip->rescale(clip->getPosition().getStart(), length / current);
            else
                clip->setEnd(clip->getPosition().getStart() + te::TimeDuration::fromSeconds(length), true);
        }
    }
    if (audio)
    {
        audio->setFadeIn(te::TimeDuration::fromSeconds(settings.fadeInSeconds));
        audio->setFadeOut(te::TimeDuration::fromSeconds(settings.fadeOutSeconds));
    }
    return juce::Result::ok();
}
juce::Result Workstation::quantize(te::MidiClip* clip, const QuantizeSettings& options)
{
    if (!clip || &clip->edit != edit.get())
        return invalid("Select a MIDI region");
    if (!finite(options.gridBeats, 0.0625, 4) || !finite(options.strength, 0, 1) ||
        !finite(options.swing, 0, 0.5))
        return invalid("Invalid quantize settings");
    if (options.strength == 0)
        return juce::Result::ok();
    auto snap = [&](double beat)
    {
        auto cell = static_cast<int64_t>(std::llround(beat / options.gridBeats));
        return cell * options.gridBeats + (cell % 2 ? options.gridBeats * options.swing : 0);
    };
    begin("Quantize MIDI");
    for (auto* note : clip->getSequence().getNotes())
    {
        double start = note->getStartBeat().inBeats(), end = note->getEndBeat().inBeats();
        double next = std::max(0.0, start + (snap(start) - start) * options.strength);
        double length = note->getLengthBeats().inBeats();
        if (options.noteEnds)
            length = std::max(0.01, end + (snap(end) - end) * options.strength - next);
        note->setStartAndLength(te::BeatPosition::fromBeats(next), te::BeatDuration::fromBeats(length),
                                &edit->getUndoManager());
    }
    return juce::Result::ok();
}
juce::Result Workstation::humanize(te::MidiClip* clip, double timingMs, int range, uint32_t seed)
{
    if (!clip || &clip->edit != edit.get())
        return invalid("Select a MIDI region");
    if (!finite(timingMs, 0, 100) || range < 0 || range > 50)
        return invalid("Humanize is limited to 100 ms and 50 velocity steps");
    begin("Humanize MIDI");
    std::mt19937 random(seed);
    std::uniform_real_distribution<double> timing(-timingMs, timingMs);
    std::uniform_int_distribution<int> velocity(-range, range);
    double clipStart = clip->getPosition().getStart().inSeconds();
    double clipBeat = beats(clipStart);
    for (auto* note : clip->getSequence().getNotes())
    {
        double at = seconds(clipBeat + note->getStartBeat().inBeats()) + timing(random) / 1000;
        double start = std::max(0.0, beats(std::max(clipStart, at)) - clipBeat);
        note->setStartAndLength(te::BeatPosition::fromBeats(start), note->getLengthBeats(),
                                &edit->getUndoManager());
        note->setVelocity(std::clamp(note->getVelocity() + velocity(random), 1, 127),
                          &edit->getUndoManager());
    }
    return juce::Result::ok();
}
juce::Result Workstation::transpose(te::MidiClip* clip, int semitones)
{
    if (!clip || &clip->edit != edit.get() || semitones < -48 || semitones > 48)
        return invalid("Choose a MIDI region and a transpose amount within four octaves");
    begin("Transpose MIDI");
    for (auto* n : clip->getSequence().getNotes())
        n->setNoteNumber(std::clamp(n->getNoteNumber() + semitones, 0, 127), &edit->getUndoManager());
    return juce::Result::ok();
}
juce::Result Workstation::setVelocities(te::MidiClip* clip, int velocity)
{
    if (!clip || &clip->edit != edit.get() || velocity < 1 || velocity > 127)
        return invalid("Choose a MIDI region and velocity from 1 to 127");
    begin("MIDI velocity");
    for (auto* n : clip->getSequence().getNotes())
        n->setVelocity(velocity, &edit->getUndoManager());
    return juce::Result::ok();
}
std::vector<juce::File> Workstation::missingMedia() const
{
    std::vector<juce::File> result;
    for (auto* t : tracks())
        for (auto* c : t->getClips())
            if (auto* wave = dynamic_cast<te::WaveAudioClip*>(c))
            {
                auto file = wave->getOriginalFile();
                if (file.existsAsFile())
                    continue;
                if (std::none_of(result.begin(), result.end(),
                                 [&](const auto& f) { return sameFile(f, file); }))
                    result.push_back(file);
            }
    return result;
}
juce::Result Workstation::relink(const juce::File& missing, const juce::File& replacement)
{
    te::AudioFile audio(engine, replacement);
    if (!audio.isValid())
        return invalid("The replacement is not a readable audio file");
    std::vector<te::WaveAudioClip*> clips;
    for (auto* t : tracks())
        for (auto* c : t->getClips())
            if (auto* wave = dynamic_cast<te::WaveAudioClip*>(c);
                wave && sameFile(wave->getOriginalFile(), missing))
                clips.push_back(wave);
    if (clips.empty())
        return invalid("That media reference no longer belongs to this session");
    auto media = projectFile.getParentDirectory().getChildFile("Media");
    if (media.createDirectory().failed())
        return invalid("Cannot create session media folder");
    auto owned = media.getNonexistentChildFile(replacement.getFileNameWithoutExtension(),
                                               replacement.getFileExtension(), false);
    if (!replacement.copyFileTo(owned))
        return invalid("Could not copy replacement into this session");
    begin("Relink media");
    for (auto* c : clips)
        c->getSourceFileReference().setToFile(owned, te::SourceFileReference::PathStyle::alwaysRelative,
                                              false);
    status = juce::String(clips.size()) + " regions relinked to " + owned.getFileName();
    return juce::Result::ok();
}
juce::Result Workstation::findMedia(const juce::File& folder)
{
    if (!folder.isDirectory())
        return invalid("Choose a folder to search");
    auto files = folder.findChildFiles(juce::File::findFiles, true);
    int fixed = 0, ambiguous = 0;
    for (auto& missing : missingMedia())
    {
        juce::Array<juce::File> candidates;
        for (auto& file : files)
            if (file.getFileName().equalsIgnoreCase(missing.getFileName()))
                candidates.add(file);
        if (candidates.size() > 1)
        {
            ++ambiguous;
            continue;
        }
        if (candidates.size() == 1)
        {
            auto result = relink(missing, candidates[0]);
            if (result.wasOk())
                ++fixed;
        }
    }
    status = juce::String(fixed) + " sources relinked; " + juce::String(ambiguous) +
             " ambiguous names left for manual selection";
    return juce::Result::ok();
}
te::MarkerClip::Ptr Workstation::addMarker(const juce::String& name, double beat)
{
    if (!finite(beat, 0, 1000000) || name.trim().isEmpty())
        return {};
    begin("Add location marker");
    auto& manager = edit->getMarkerManager();
    auto marker =
        manager.createMarker(manager.getNextUniqueID(), te::TimePosition::fromSeconds(seconds(beat)),
                             te::TimeDuration::fromSeconds(0), te::Clip::syncBarsBeats, nullptr);
    if (marker)
        marker->setName(name.trim().substring(0, 128));
    return marker;
}
void Workstation::seekMarker(bool next)
{
    auto& m = edit->getMarkerManager();
    auto* marker = next ? m.getNextMarker(te::TimePosition::fromSeconds(position() + 0.001))
                        : m.getPrevMarker(te::TimePosition::fromSeconds(std::max(0.0, position() - 0.001)));
    if (marker)
        seek(marker->getPosition().getStart().inSeconds());
}
juce::Result Workstation::transportSettings(double start, double end, bool loop, bool punch, int count)
{
    if (!finite(start, 0, 1000000) || !finite(end, 0, 1000000) || end <= start || count < 0 || count > 2)
        return invalid("Enter an increasing loop range and a count-in of zero, one or two bars");
    begin("Transport and recording options");
    edit->getTransport().setLoopRange(
        {te::TimePosition::fromSeconds(seconds(start)), te::TimePosition::fromSeconds(seconds(end))});
    edit->getTransport().looping = loop;
    edit->recordingPunchInOut = punch;
    edit->setCountInMode(count == 0   ? te::Edit::CountIn::none
                         : count == 1 ? te::Edit::CountIn::oneBar
                                      : te::Edit::CountIn::twoBar);
    return juce::Result::ok();
}
juce::Result Workstation::reorderEffect(int index, int which, int direction)
{
    auto* t = track(index);
    auto effects = inserts(index);
    int other = which + (direction < 0 ? -1 : 1);
    if (!t || !juce::isPositiveAndBelow(which, static_cast<int>(effects.size())) ||
        !juce::isPositiveAndBelow(other, static_cast<int>(effects.size())))
        return invalid("No insert in that direction");
    auto* p = effects[static_cast<size_t>(which)];
    auto* target = effects[static_cast<size_t>(other)];
    auto parent = p->state.getParent();
    if (parent != target->state.getParent())
        return invalid("Inserts are not in the same chain");
    auto faderIndex = parent.indexOf(fader(index)->state);
    if (p->isSynth() || target->isSynth() ||
        ((parent.indexOf(p->state) < faderIndex) != (parent.indexOf(target->state) < faderIndex)))
        return invalid("Instruments stay first and sends stay after the fader");
    begin("Reorder insert");
    parent.moveChild(parent.indexOf(p->state), parent.indexOf(target->state), &edit->getUndoManager());
    return juce::Result::ok();
}
} // namespace bte
