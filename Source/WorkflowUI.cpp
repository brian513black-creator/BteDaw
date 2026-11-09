#include "ProStudio.h"
#include <cstdlib>
namespace bte
{
namespace
{
const juce::Colour panel{0xff22252b}, ink{0xffe1e5eb};
class Form final : public juce::Component
{
  public:
    juce::TextButton apply{"Apply"};
    juce::Label feedback;
    std::unique_ptr<juce::FileChooser> chooser;
    std::function<void(Form&)> action;
    Form()
    {
        addAndMakeVisible(apply);
        addAndMakeVisible(feedback);
        feedback.setColour(juce::Label::textColourId, ink);
        feedback.setJustificationType(juce::Justification::topLeft);
        apply.onClick = [this]
        {
            if (action)
                action(*this);
        };
    }
    void text(const std::string& key, const juce::String& title, const juce::String& initial)
    {
        auto* edit = new juce::TextEditor;
        edit->setText(initial);
        fields[key] = edit;
        row(title, edit);
    }
    void number(const std::string& key, const juce::String& title, double initial)
    {
        text(key, title, juce::String(initial, 3));
        fields[key]->setInputRestrictions(16, "-0123456789.");
    }
    void choice(const std::string& key, const juce::String& title, const juce::StringArray& options,
                int selected)
    {
        auto* box = new juce::ComboBox;
        box->addItemList(options, 1);
        box->setSelectedId(selected);
        choices[key] = box;
        row(title, box);
    }
    void toggle(const std::string& key, const juce::String& title, bool checked)
    {
        auto* box = new juce::ToggleButton(title);
        box->setToggleState(checked, juce::dontSendNotification);
        toggles[key] = box;
        row({}, box);
    }
    juce::String value(const std::string& key) const
    {
        return fields.at(key)->getText();
    }
    bool numbers(std::initializer_list<std::pair<const char*, double*>> targets)
    {
        for (auto item : targets)
        {
            auto text = value(item.first).trim().toStdString();
            char* end = nullptr;
            double v = std::strtod(text.c_str(), &end);
            if (text.empty() || end != text.c_str() + text.size() || !std::isfinite(v))
            {
                result(juce::Result::fail("Enter a valid number for " + juce::String(item.first)));
                return false;
            }
            *item.second = v;
        }
        return true;
    }
    int selected(const std::string& key) const
    {
        return choices.at(key)->getSelectedId();
    }
    bool checked(const std::string& key) const
    {
        return toggles.at(key)->getToggleState();
    }
    void result(juce::Result r)
    {
        feedback.setText(r.wasOk() ? "Applied. Ctrl+Z undoes an edit." : r.getErrorMessage(),
                         juce::dontSendNotification);
    }
    void info(const juce::String& s)
    {
        feedback.setText(s, juce::dontSendNotification);
    }
    void paint(juce::Graphics& g) override
    {
        g.fillAll(panel);
    }

  private:
    juce::OwnedArray<juce::Component> controls;
    std::map<std::string, juce::TextEditor*> fields;
    std::map<std::string, juce::ComboBox*> choices;
    std::map<std::string, juce::ToggleButton*> toggles;
    int rows = 0;
    void row(const juce::String& title, juce::Component* component)
    {
        auto* label = new juce::Label;
        label->setText(title, juce::dontSendNotification);
        label->setBounds(16, 12 + rows * 36, 222, 28);
        controls.add(label);
        addAndMakeVisible(label);
        component->setBounds(240, 12 + rows * 36, 300, 28);
        controls.add(component);
        addAndMakeVisible(component);
        ++rows;
        feedback.setBounds(16, rows * 36 + 22, 524, 46);
        apply.setBounds(356, rows * 36 + 74, 184, 30);
        setSize(560, rows * 36 + 120);
    }
};
juce::DialogWindow* launch(Form* form, const juce::String& title)
{
    juce::DialogWindow::LaunchOptions o;
    o.dialogTitle = title;
    o.dialogBackgroundColour = panel;
    o.useNativeTitleBar = true;
    o.content.setOwned(form);
    return o.launchAsync();
}
} // namespace
void ProStudio::showRegionEditor()
{
    if (!selectedClip)
    {
        message.setText("Select an audio or MIDI region first.", juce::dontSendNotification);
        return;
    }
    if (regionWindow)
    {
        delete regionWindow.getComponent();
    }
    auto* f = new Form;
    auto clip = selectedClip;
    f->text("name", "Region name", clip->getName());
    f->number("length", "Length in beats",
              session.beats(clip->getPosition().getEnd().inSeconds()) -
                  session.beats(clip->getPosition().getStart().inSeconds()));
    auto* audio = dynamic_cast<te::AudioClipBase*>(clip.get());
    if (audio)
    {
        f->number("gain", "Region gain (dB)", audio->getGainDB());
        f->number("pitch", "Transpose (semitones)", audio->getPitchChange());
        f->number("fadeIn", "Fade in (ms)", audio->getFadeIn().inSeconds() * 1000);
        f->number("fadeOut", "Fade out (ms)", audio->getFadeOut().inSeconds() * 1000);
        f->toggle("reverse", "Reverse source", audio->getIsReversed());
    }
    f->info("Non-destructive edits. Changing audio length uses SoundTouch time stretch.");
    f->action = [safe = juce::Component::SafePointer<ProStudio>(this), clip, audio,
                 originalLength = f->value("length")](Form& form) mutable
    {
        if (!safe)
            return;
        RegionSettings settings;
        settings.name = form.value("name");
        double length = 0, gain = 0, pitch = 0, fadeIn = 0, fadeOut = 0;
        if (!form.numbers({{"length", &length}}))
            return;
        if (length <= 0)
        {
            form.result(juce::Result::fail("Region length must be greater than zero"));
            return;
        }
        settings.lengthBeats = form.value("length") == originalLength ? 0 : length;
        if (audio)
        {
            if (!form.numbers(
                    {{"gain", &gain}, {"pitch", &pitch}, {"fadeIn", &fadeIn}, {"fadeOut", &fadeOut}}))
                return;
            settings.gainDb = static_cast<float>(gain);
            settings.pitchSemitones = static_cast<float>(pitch);
            settings.fadeInSeconds = fadeIn / 1000;
            settings.fadeOutSeconds = fadeOut / 1000;
            settings.reversed = form.checked("reverse");
        }
        auto r = safe->session.editRegion(clip.get(), settings);
        form.result(r);
        safe->report(r);
        if (r.wasOk())
            originalLength = form.value("length");
        safe->thumbs.clear();
        safe->refresh();
    };
    regionWindow = launch(f, "Region properties / Gain, fades, pitch and stretch");
}
void ProStudio::showMidiEditor(int mode)
{
    auto* midi = dynamic_cast<te::MidiClip*>(selectedClip.get());
    if (!midi)
    {
        message.setText("Select a MIDI region first.", juce::dontSendNotification);
        return;
    }
    if (midiWindow)
        delete midiWindow.getComponent();
    auto* f = new Form;
    te::Clip::Ptr clip = midi;
    if (mode == 1)
    {
        f->choice("grid", "Grid",
                  {"1/16 note (quarter beat)", "1/8 note (half beat)", "Quarter note (one beat)",
                   "Half note (two beats)"},
                  1);
        f->number("strength", "Strength (%)", 100);
        f->number("swing", "Swing delay (%)", 0);
        f->toggle("ends", "Quantize note ends too", false);
        f->apply.setButtonText("Quantize");
    }
    if (mode == 2)
    {
        f->number("timing", "Timing variation (+/- ms)", 12);
        f->number("velocity", "Velocity variation (+/-)", 8);
        f->apply.setButtonText("Humanize");
    }
    if (mode == 3)
    {
        f->number("pitch", "Transpose semitones", 12);
        f->apply.setButtonText("Transpose");
    }
    if (mode == 4)
    {
        f->number("velocity", "Set every note velocity (1..127)", 96);
        f->apply.setButtonText("Set velocities");
    }
    f->info("Applies to all notes in the selected region. Each application is undoable.");
    f->action = [safe = juce::Component::SafePointer<ProStudio>(this), clip, mode](Form& form)
    {
        if (!safe)
            return;
        auto* c = dynamic_cast<te::MidiClip*>(clip.get());
        juce::Result r = juce::Result::fail("Invalid MIDI operation");
        if (mode == 1)
        {
            double strength = 0, swing = 0;
            if (!form.numbers({{"strength", &strength}, {"swing", &swing}}))
                return;
            const double grids[]{0.25, 0.5, 1, 2};
            r = safe->session.quantize(c,
                                       {grids[form.selected("grid") - 1], static_cast<float>(strength / 100),
                                        static_cast<float>(swing / 100), form.checked("ends")});
        }
        if (mode == 2)
        {
            double timing = 0, range = 0;
            if (!form.numbers({{"timing", &timing}, {"velocity", &range}}))
                return;
            if (range < 0 || range > 50 || range != std::round(range))
            {
                form.result(juce::Result::fail("Use an integer velocity variation from 0..50"));
                return;
            }
            r = safe->session.humanize(c, timing, static_cast<int>(range),
                                       static_cast<uint32_t>(juce::Random::getSystemRandom().nextInt()));
        }
        if (mode == 3)
        {
            double pitch = 0;
            if (!form.numbers({{"pitch", &pitch}}))
                return;
            if (pitch < -48 || pitch > 48 || pitch != std::round(pitch))
            {
                form.result(juce::Result::fail("Use an integer transpose within four octaves"));
                return;
            }
            r = safe->session.transpose(c, static_cast<int>(pitch));
        }
        if (mode == 4)
        {
            double v = 0;
            if (!form.numbers({{"velocity", &v}}))
                return;
            if (v < 1 || v > 127)
            {
                form.result(juce::Result::fail("Velocity must be 1..127"));
                return;
            }
            r = safe->session.setVelocities(c, static_cast<int>(v));
        }
        form.result(r);
        safe->report(r);
        safe->refresh();
    };
    midiWindow = launch(f, "MIDI region tools");
}
void ProStudio::showExportDialog()
{
    if (exportWindow)
    {
        exportWindow->toFront(true);
        return;
    }
    auto* f = new Form;
    f->choice("format", "File format", {"WAV", "FLAC", "MIDI (.mid)"}, 1);
    f->choice("rate", "Sample rate", {"44100", "48000", "88200", "96000", "192000"}, 2);
    f->choice("depth", "Bit depth", {"16", "24", "32 (WAV only)"}, 2);
    f->choice("range", "Time range", {"Entire session", "Loop / punch range"}, 1);
    f->number("tail", "Extra tail (seconds)", 2);
    f->toggle("mono", "Mono output", false);
    f->toggle("normalise", "Normalize audio by loudness", false);
    f->number("target", "Loudness target (LUFS)", -14);
    f->apply.setButtonText("Choose file and export");
    f->info("MIDI exports notes and tempo; audio-only settings do not change MIDI data.");
    f->action = [safe = juce::Component::SafePointer<ProStudio>(this)](Form& form)
    {
        if (!safe)
            return;
        ExportSettings settings;
        settings.format = static_cast<ExportSettings::Format>(form.selected("format") - 1);
        const int rates[]{44100, 48000, 88200, 96000, 192000}, depths[]{16, 24, 32};
        settings.sampleRate = rates[form.selected("rate") - 1];
        settings.bitDepth = depths[form.selected("depth") - 1];
        settings.mono = form.checked("mono");
        settings.normalise = form.checked("normalise");
        settings.loopOnly = form.selected("range") == 2;
        double tail = 0, target = 0;
        if (!form.numbers({{"tail", &tail}, {"target", &target}}))
            return;
        settings.tailSeconds = tail;
        settings.targetLufs = static_cast<float>(target);
        if (settings.format == ExportSettings::flac && settings.bitDepth == 32)
        {
            form.result(juce::Result::fail("Choose 16 or 24 bits for FLAC"));
            return;
        }
        auto extension = settings.format == ExportSettings::wav    ? "wav"
                         : settings.format == ExportSettings::flac ? "flac"
                                                                   : "mid";
        form.chooser = std::make_unique<juce::FileChooser>(
            "Export session", safe->session.projectFile.getSiblingFile("Mixdown." + juce::String(extension)),
            "*." + juce::String(extension));
        form.chooser->launchAsync(
            juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles |
                juce::FileBrowserComponent::warnAboutOverwriting,
            [safe, owner = juce::Component::SafePointer<Form>(&form), settings,
             extension = juce::String(extension)](const juce::FileChooser& chooser)
            {
                if (!safe || !owner)
                    return;
                auto file = chooser.getResult();
                if (file == juce::File{})
                    return;
                if (!file.getFileExtension().equalsIgnoreCase("." + extension))
                {
                    owner->info("Choose a filename ending in ." + extension +
                                " so overwrite confirmation checks the actual output.");
                    return;
                }
                auto r = safe->session.exportAudio(file, settings);
                if (!safe || !owner)
                    return;
                owner->info(r.wasOk() ? "Export finished: " + file.getFullPathName() : r.getErrorMessage());
                safe->report(r);
            });
    };
    exportWindow = launch(f, "Export / Format, range, sample rate and loudness");
}
void ProStudio::showTransportDialog()
{
    if (transportWindow)
    {
        transportWindow->toFront(true);
        return;
    }
    auto* f = new Form;
    auto range = session.edit->getTransport().getLoopRange();
    f->number("start", "Range start (beat, zero based)", session.beats(range.getStart().inSeconds()));
    f->number("end", "Range end (beat)", session.beats(range.getEnd().inSeconds()));
    f->toggle("loop", "Loop playback", session.edit->getTransport().looping);
    f->toggle("punch", "Record only inside this range", session.edit->recordingPunchInOut);
    int count = session.edit->getCountInMode() == te::Edit::CountIn::twoBar   ? 3
                : session.edit->getCountInMode() == te::Edit::CountIn::oneBar ? 2
                                                                              : 1;
    f->choice("count", "Recording count-in", {"None", "One bar", "Two bars"}, count);
    f->info("The same visible ruler range is used for looping, punch recording and range export.");
    f->action = [safe = juce::Component::SafePointer<ProStudio>(this)](Form& form)
    {
        if (!safe)
            return;
        double start = 0, end = 0;
        if (!form.numbers({{"start", &start}, {"end", &end}}))
            return;
        auto r = safe->session.transportSettings(start, end, form.checked("loop"), form.checked("punch"),
                                                 form.selected("count") - 1);
        form.result(r);
        safe->report(r);
        safe->refresh();
    };
    transportWindow = launch(f, "Transport / Loop, punch and count-in");
}
void ProStudio::showMarkerDialog(int markerID)
{
    if (transportWindow)
        delete transportWindow.getComponent();
    auto* existing = session.edit->getMarkerManager().getMarkerByID(markerID);
    auto* f = new Form;
    f->text("name", "Marker name",
            existing ? existing->getName()
                     : "Marker " + juce::String(session.edit->getMarkerManager().getMarkers().size() + 1));
    f->number("beat", "Position (beat, zero based)",
              existing ? session.beats(existing->getPosition().getStart().inSeconds())
                       : session.beats(session.position()));
    f->action = [safe = juce::Component::SafePointer<ProStudio>(this), markerID](Form& form) mutable
    {
        if (!safe)
            return;
        double beat = 0;
        if (!form.numbers({{"beat", &beat}}))
            return;
        if (beat < 0 || beat > 1000000 || form.value("name").trim().isEmpty())
        {
            form.result(juce::Result::fail("Choose a marker name and a nonnegative beat"));
            return;
        }
        if (auto* marker = safe->session.edit->getMarkerManager().getMarkerByID(markerID))
        {
            safe->session.begin("Edit marker");
            marker->setName(form.value("name").trim().substring(0, 128));
            marker->setStart(te::TimePosition::fromSeconds(safe->session.seconds(beat)), false, true);
        }
        else
        {
            auto added = safe->session.addMarker(form.value("name"), beat);
            if (!added)
            {
                form.result(juce::Result::fail("Could not add marker"));
                return;
            }
            markerID = static_cast<te::MarkerClip*>(added.get())->getMarkerID();
        }
        form.result(juce::Result::ok());
        safe->refresh();
    };
    transportWindow = launch(f, "Location marker");
}
void ProStudio::showRelinkMenu()
{
    auto missing = session.missingMedia();
    if (missing.empty())
    {
        message.setText("All referenced audio files are present.", juce::dontSendNotification);
        return;
    }
    juce::PopupMenu menu;
    for (int i = 0; i < static_cast<int>(missing.size()); ++i)
        menu.addItem(i + 1, missing[static_cast<size_t>(i)].getFullPathName());
    menu.addSeparator();
    menu.addItem(100000, "Search a folder by filename (skip ambiguous matches)");
    menu.showMenuAsync(
        juce::PopupMenu::Options{},
        [safe = juce::Component::SafePointer<ProStudio>(this), missing,
         sessionFile = session.projectFile](int id)
        {
            if (!safe || id == 0)
                return;
            bool folder = id == 100000;
            if (!folder && !juce::isPositiveAndBelow(id - 1, static_cast<int>(missing.size())))
                return;
            safe->chooser = std::make_unique<juce::FileChooser>(
                folder ? "Find missing audio in a folder" : "Choose replacement audio",
                sessionFile.getParentDirectory(), folder ? "" : "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
            safe->chooser->launchAsync(
                juce::FileBrowserComponent::openMode |
                    (folder ? juce::FileBrowserComponent::canSelectDirectories
                            : juce::FileBrowserComponent::canSelectFiles),
                [safe, missing, id, folder, sessionFile](const juce::FileChooser& chooser)
                {
                    if (!safe || safe->session.projectFile != sessionFile)
                        return;
                    auto file = chooser.getResult();
                    if (file == juce::File{})
                        return;
                    safe->report(folder ? safe->session.findMedia(file)
                                        : safe->session.relink(missing[static_cast<size_t>(id - 1)], file));
                    safe->thumbs.clear();
                    safe->refresh();
                });
        });
}
void ProStudio::workflowMenu(int kind)
{
    juce::PopupMenu menu;
    std::map<int, std::pair<int, int>> markerActions;
    if (kind == 104)
    {
        menu.addItem(1, "Region properties / Gain, fades, pitch, reverse, stretch");
        menu.addSeparator();
        menu.addItem(2, "Copy / Ctrl+C");
        menu.addItem(3, "Cut / Ctrl+X");
        menu.addItem(4, "Paste at playhead / Ctrl+V");
        menu.addItem(5, "Duplicate");
        menu.addItem(6, "Delete");
    }
    if (kind == 105)
    {
        menu.addItem(11, "Quantize / Strength and swing...");
        menu.addItem(12, "Humanize timing and velocity...");
        menu.addItem(13, "Transpose...");
        menu.addItem(14, "Set note velocities...");
    }
    if (kind == 106)
    {
        menu.addItem(21, "Loop, punch and count-in settings...");
        menu.addItem(22, "Add marker at playhead...");
        menu.addItem(23, "Next marker");
        menu.addItem(24, "Previous marker");
        auto markers = session.edit->getMarkerManager().getMarkers();
        for (auto* marker : markers)
        {
            juce::PopupMenu sub;
            for (int action = 0; action < 3; ++action)
            {
                int menuID = 100 + static_cast<int>(markerActions.size());
                markerActions[menuID] = {marker->getMarkerID(), action};
                sub.addItem(menuID, action == 0   ? "Jump here"
                                    : action == 1 ? "Rename / move..."
                                                  : "Delete marker");
            }
            menu.addSubMenu(marker->getName(), sub);
        }
    }
    menu.showMenuAsync(
        juce::PopupMenu::Options{},
        [safe = juce::Component::SafePointer<ProStudio>(this), markerActions](int id)
        {
            if (!safe || id == 0)
                return;
            if (id == 1)
                safe->showRegionEditor();
            if (id == 2)
                safe->report(safe->session.copyClip(safe->selectedClip.get()));
            if (id == 3)
            {
                auto r = safe->session.copyClip(safe->selectedClip.get(), true);
                safe->report(r);
                if (r.wasOk())
                    safe->selectedClip = nullptr;
            }
            if (id == 4)
            {
                auto* c = safe->session.pasteClip(safe->selectedTrack, safe->session.position());
                if (c)
                    safe->selectedClip = c;
                else
                    safe->message.setText("Copy a region and select a destination channel before pasting.",
                                          juce::dontSendNotification);
            }
            if (id == 5)
                safe->command(13);
            if (id == 6)
                safe->command(14);
            if (id >= 11 && id <= 14)
                safe->showMidiEditor(id - 10);
            if (id == 21)
                safe->showTransportDialog();
            if (id == 22)
                safe->showMarkerDialog();
            if (id == 23)
                safe->session.seekMarker(true);
            if (id == 24)
                safe->session.seekMarker(false);
            if (auto action = markerActions.find(id); action != markerActions.end())
            {
                int markerID = action->second.first, operation = action->second.second;
                if (auto* m = safe->session.edit->getMarkerManager().getMarkerByID(markerID))
                {
                    if (operation == 0)
                        safe->session.seek(m->getPosition().getStart().inSeconds());
                    if (operation == 1)
                        safe->showMarkerDialog(markerID);
                    if (operation == 2)
                    {
                        safe->session.begin("Delete marker");
                        m->removeFromParent();
                    }
                }
            }
            safe->refresh();
        });
}
} // namespace bte
