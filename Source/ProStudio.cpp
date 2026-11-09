#include "ProStudio.h"
namespace bte
{
namespace
{
const juce::Colour bg{0xff181a1e}, panel{0xff22252b}, dark{0xff121417}, line{0xff343941}, ink{0xffe1e5eb},
    dim{0xff919ba9}, blue{0xff63a8e6}, green{0xff6cc39a}, red{0xffe46c6c};
juce::Colour trackColour(int i)
{
    const juce::uint32 values[]{0xff729fcf, 0xff71bba7, 0xffb795d0, 0xffcf9f6d, 0xffd07d99, 0xff8fb86c};
    return juce::Colour(values[static_cast<size_t>(std::max(0, i)) % 6]);
}
void label(juce::Graphics& g, const juce::String& s, juce::Rectangle<int> r, float size = 12,
           juce::Colour colour = ink, int align = juce::Justification::centredLeft)
{
    g.setFont(juce::FontOptions(size));
    g.setColour(colour);
    g.drawText(s, r, align, true);
}
void meter(juce::Graphics& g, juce::Rectangle<int> r, float db)
{
    g.setColour(dark);
    g.fillRect(r);
    int h = static_cast<int>(std::clamp((db + 60) / 60.0f, 0.0f, 1.0f) * r.getHeight());
    g.setColour(db >= 0 ? red : db > -6 ? juce::Colour(0xffd7c274) : green);
    g.fillRect(r.withTop(r.getBottom() - h));
    g.setColour(panel);
    for (int y = r.getY(); y < r.getBottom(); y += 4)
        g.drawHorizontalLine(y, static_cast<float>(r.getX()), static_cast<float>(r.getRight()));
}
struct InspectorSurface : juce::Component
{
    std::function<void(juce::Graphics&)> draw;
    std::function<void(const juce::MouseEvent&)> down, doubleClick;
    void paint(juce::Graphics& g) override
    {
        if (draw)
            draw(g);
    }
    void mouseDown(const juce::MouseEvent& e) override
    {
        if (down)
            down(e);
    }
    void mouseDoubleClick(const juce::MouseEvent& e) override
    {
        if (doubleClick)
            doubleClick(e);
    }
};
struct EditorWindow : juce::DocumentWindow
{
    te::Plugin::Ptr hold;
    EditorWindow(te::Plugin::Ptr p, juce::Component* content)
        : DocumentWindow(p->getName(), panel, closeButton), hold(p)
    {
        setUsingNativeTitleBar(true);
        setContentOwned(content, true);
        setResizable(true, false);
        centreWithSize(getWidth(), getHeight());
        setVisible(true);
    }
    void closeButtonPressed() override
    {
        setVisible(false);
    }
    ~EditorWindow() override
    {
        clearContentComponent();
    }
};
class ParameterPanel : public juce::Component
{
    te::Plugin::Ptr plugin;
    juce::Viewport viewport;
    juce::Component content;
    juce::OwnedArray<juce::Slider> sliders;
    juce::OwnedArray<juce::Label> labels;

  public:
    explicit ParameterPanel(te::Plugin::Ptr p) : plugin(p)
    {
        addAndMakeVisible(viewport);
        viewport.setViewedComponent(&content, false);
        auto params = p->getAutomatableParameters();
        for (auto* param : params)
        {
            auto* l = labels.add(new juce::Label);
            l->setText(param->getParameterName(), juce::dontSendNotification);
            content.addAndMakeVisible(l);
            auto* s = sliders.add(new juce::Slider);
            s->setRange(0, 1, 0.0001);
            s->setValue(param->getCurrentNormalisedValue());
            s->setTextBoxStyle(juce::Slider::TextBoxRight, false, 100, 24);
            s->textFromValueFunction = [param](double v)
            { return param->valueToString(param->valueRange.convertFrom0to1(static_cast<float>(v))); };
            s->onDragStart = [p] { p->edit.getUndoManager().beginNewTransaction("Edit plugin parameter"); };
            s->onValueChange = [s, param]
            { param->setNormalisedParameter(static_cast<float>(s->getValue()), juce::sendNotification); };
            content.addAndMakeVisible(s);
        }
        content.setSize(620, std::max(100, params.size() * 42));
        for (int i = 0; i < sliders.size(); ++i)
        {
            labels[i]->setBounds(12, i * 42 + 6, 200, 30);
            sliders[i]->setBounds(215, i * 42 + 6, 380, 30);
        }
        setSize(640, std::min(700, std::max(180, params.size() * 42 + 12)));
    }
    void resized() override
    {
        viewport.setBounds(getLocalBounds());
    }
    void paint(juce::Graphics& g) override
    {
        g.fillAll(panel);
        if (sliders.isEmpty())
            label(g, "No automatable parameters exposed.", getLocalBounds().reduced(20));
    }
};
} // namespace
class ChannelStrip : public juce::Component
{
    ProStudio& studio;
    int index;
    te::LevelMeterPlugin* measured = nullptr;
    te::Plugin::Ptr measuredLifetime;
    te::LevelMeasurer::Client client;
    juce::Slider gain, pan;
    juce::TextButton mute{"M"}, solo{"S"}, arm{"R"}, masterMono{"MONO"};

  public:
    float left = -100, right = -100;
    ChannelStrip(ProStudio& s, int i) : studio(s), index(i)
    {
        for (auto* c : std::initializer_list<juce::Component*>{&gain, &pan, &mute, &solo, &arm})
            addAndMakeVisible(c);
        if (index < 0)
        {
            addAndMakeVisible(masterMono);
            masterMono.onClick = [this] { studio.session.setMono(-1, !studio.session.mono(-1)); };
        }
        gain.setSliderStyle(juce::Slider::LinearVertical);
        gain.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 66, 18);
        gain.setRange(-60, 6, 0.1);
        gain.setTextValueSuffix(" dB");
        gain.setDoubleClickReturnValue(true, 0);
        pan.setSliderStyle(juce::Slider::LinearHorizontal);
        pan.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        pan.setRange(-1, 1, 0.01);
        pan.setDoubleClickReturnValue(true, 0);
        gain.onDragStart = [this] { studio.session.begin("Channel gain"); };
        gain.onValueChange = [this]
        {
            if (auto* f = studio.session.fader(index))
                f->setVolumeDb(static_cast<float>(gain.getValue()));
        };
        pan.onDragStart = [this] { studio.session.begin("Channel pan"); };
        pan.onValueChange = [this]
        {
            if (auto* f = studio.session.fader(index))
                f->setPan(static_cast<float>(pan.getValue()));
        };
        mute.onClick = [this]
        {
            if (auto* t = studio.session.track(index))
            {
                studio.session.begin("Mute channel");
                t->setMute(!t->isMuted(false));
            }
        };
        solo.onClick = [this]
        {
            if (auto* t = studio.session.track(index))
            {
                studio.session.begin("Solo channel");
                t->setSolo(!t->isSolo(false));
            }
        };
        arm.onClick = [this]
        {
            studio.selectedTrack = index;
            studio.command(25);
        };
        if (auto* t = studio.session.track(index))
            measured = t->getLevelMeterPlugin();
        if (index < 0)
        {
            for (auto* p : studio.session.edit->getMasterPluginList())
                if (auto* m = dynamic_cast<te::LevelMeterPlugin*>(p))
                    measured = m;
            if (!measured)
            {
                auto p = studio.session.edit->getPluginCache().createNewPlugin(
                    te::LevelMeterPlugin::xmlTypeName, {});
                studio.session.edit->getMasterPluginList().insertPlugin(p, -1, nullptr);
                measured = dynamic_cast<te::LevelMeterPlugin*>(p.get());
            }
        }
        if (measured)
        {
            measuredLifetime = measured;
            measured->measurer.addClient(client);
        }
        if (index < 0)
        {
            mute.setVisible(false);
            solo.setVisible(false);
            arm.setVisible(false);
        }
        tick();
    }
    ~ChannelStrip() override
    {
        if (measured)
            measured->measurer.removeClient(client);
    }
    void tick()
    {
        if (auto* f = studio.session.fader(index))
        {
            if (!gain.isMouseButtonDown())
                gain.setValue(f->getVolumeDb(), juce::dontSendNotification);
            if (!pan.isMouseButtonDown())
                pan.setValue(f->getPan(), juce::dontSendNotification);
        }
        if (auto* t = studio.session.track(index))
        {
            mute.setToggleState(t->isMuted(false), juce::dontSendNotification);
            solo.setToggleState(t->isSolo(false), juce::dontSendNotification);
            arm.setToggleState(studio.session.armed(index), juce::dontSendNotification);
        }
        if (index < 0)
            masterMono.setToggleState(studio.session.mono(-1), juce::dontSendNotification);
        if (measured)
        {
            left = client.getAndClearAudioLevel(0).dB;
            right = client.getAndClearAudioLevel(1).dB;
        }
        repaint();
    }
    void resized() override
    {
        masterMono.setBounds(10, 43, 72, 18);
        pan.setBounds(10, 65, 72, 16);
        gain.setBounds(8, 88, 60, getHeight() - 130);
        mute.setBounds(7, getHeight() - 30, 25, 20);
        solo.setBounds(35, getHeight() - 30, 25, 20);
        arm.setBounds(63, getHeight() - 30, 25, 20);
    }
    void paint(juce::Graphics& g) override
    {
        auto* t = studio.session.track(index);
        auto colour = index < 0 ? green : trackColour(index);
        g.fillAll(studio.selectedTrack == index ? juce::Colour(0xff30363f) : panel);
        g.setColour(line);
        g.drawRect(getLocalBounds());
        g.setColour(colour);
        g.fillRect(1, 1, getWidth() - 2, 3);
        label(g, index < 0 ? "STEREO OUT" : t->getName(), {6, 10, getWidth() - 12, 20}, 11, ink);
        label(g, index < 0 ? "MASTER" : t->getOutput().getDescriptiveOutputName(),
              {6, 33, getWidth() - 12, 17}, 9, dim);
        auto inserts = studio.session.inserts(index);
        if (index >= 0)
            label(g, juce::String(inserts.size()) + " inserts", {7, 49, 85, 14}, 9, dim);
        meter(g, {72, 92, 6, getHeight() - 142}, left);
        meter(g, {81, 92, 6, getHeight() - 142}, right);
    }
    void mouseDown(const juce::MouseEvent&) override
    {
        if (index >= 0)
        {
            studio.selectedTrack = index;
            studio.refresh();
        }
    }
};
ProStudio::ProStudio()
{
    look.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff323740));
    look.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff446d8d));
    look.setColour(juce::TextButton::textColourOffId, ink);
    look.setColour(juce::Slider::thumbColourId, ink);
    look.setColour(juce::Slider::trackColourId, blue);
    look.setColour(juce::ComboBox::backgroundColourId, dark);
    look.setColour(juce::TextEditor::backgroundColourId, dark);
    look.setColour(juce::TextEditor::textColourId, ink);
    setLookAndFeel(&look);
    const std::vector<std::pair<const char*, int>> actions{
        {"Project", 100},  {"Edit", 101},       {"Track", 102}, {"Mix", 103},        {"Audio", 24},
        {"Assistant", 26}, {"|<", 1},           {"Stop", 2},    {"Play", 3},         {"REC", 4},
        {"Loop", 5},       {"Click", 6},        {"+ Audio", 7}, {"+ Instrument", 8}, {"+ Bus", 9},
        {"Import", 10},    {"MIDI Region", 11}, {"Split", 12},  {"Duplicate", 13},   {"Delete", 14},
        {"Undo", 15},      {"Redo", 16},        {"-", 17},      {"+", 18},           {"Mixer", 19},
        {"Piano", 20},     {"Automation", 21}};
    for (auto a : actions)
    {
        auto* b = buttons.add(new juce::TextButton(a.first));
        b->setComponentID(juce::String(a.second));
        b->onClick = [this, id = a.second] { command(id); };
        addAndMakeVisible(b);
    }
    for (auto* c : std::initializer_list<juce::Component*>{&name, &tempo, &message, &input, &output, &mode,
                                                           &sidechain, &trim, &pan, &addInsert, &vst,
                                                           &addSend, &connectSidechain, &snap, &horizontal})
        addAndMakeVisible(c);
    auto surface = std::make_unique<InspectorSurface>();
    surface->draw = [this](juce::Graphics& g)
    {
        g.fillAll(panel);
        g.addTransform(juce::AffineTransform::translation(0, -134));
        label(g, "CHANNEL", {14, 135, 180, 17}, 10, dim);
        for (auto field :
             std::initializer_list<std::pair<const char*, int>>{{"INPUT / RECORD SOURCE", 196},
                                                                {"CHANNEL FORMAT", 252},
                                                                {"INPUT TRIM", 310},
                                                                {"PAN / BALANCE", 364},
                                                                {"OUTPUT", 422},
                                                                {"SIDECHAIN TO SELECTED INSERT", 518},
                                                                {"INSERT CHAIN", 615}})
            label(g, field.first, {14, field.second, 184, 18}, 9, dim);
        auto effects = session.inserts(selectedTrack);
        for (int i = 0; i < static_cast<int>(effects.size()); ++i)
        {
            auto r = juce::Rectangle<int>(14, 676 + i * 26, 184, 23);
            g.setColour(i == selectedInsert ? juce::Colour(0xff3a536b) : dark);
            g.fillRoundedRectangle(r.toFloat(), 3);
            label(g, juce::String(i + 1) + "  " + effects[static_cast<size_t>(i)]->getName(), r.reduced(7, 0),
                  10, effects[static_cast<size_t>(i)]->isEnabled() ? ink : dim);
        }
    };
    surface->down = [this](const juce::MouseEvent& e)
    {
        mouseDown(
            e.getEventRelativeTo(this).withNewPosition(juce::Point<float>{e.position.x, e.position.y + 134}));
    };
    surface->doubleClick = [this](const juce::MouseEvent& e)
    {
        mouseDoubleClick(
            e.getEventRelativeTo(this).withNewPosition(juce::Point<float>{e.position.x, e.position.y + 134}));
    };
    inspectorContent = std::move(surface);
    for (auto* c :
         std::initializer_list<juce::Component*>{&name, &input, &output, &mode, &sidechain, &trim, &pan,
                                                 &addInsert, &vst, &addSend, &connectSidechain})
        inspectorContent->addAndMakeVisible(c);
    inspectorContent->setSize(198, 800);
    inspectorViewport.setViewedComponent(inspectorContent.get(), false);
    inspectorViewport.setScrollBarsShown(true, false);
    inspectorViewport.setScrollBarThickness(8);
    addAndMakeVisible(inspectorViewport);
    name.setEditable(false, true, false);
    name.onTextChange = [this]
    {
        if (auto* t = session.track(selectedTrack))
        {
            session.begin("Rename channel");
            t->setName(name.getText());
        }
    };
    tempo.setEditable(false, true, false);
    tempo.setColour(juce::Label::textColourId, ink);
    tempo.setJustificationType(juce::Justification::centred);
    tempo.onTextChange = [this]
    {
        session.setBpm(tempo.getText().getDoubleValue());
        refresh(false);
    };
    message.setColour(juce::Label::textColourId, dim);
    trim.setRange(-60, 24, 0.1);
    trim.setTextValueSuffix(" dB");
    trim.setTextBoxStyle(juce::Slider::TextBoxRight, false, 62, 20);
    pan.setRange(-1, 1, 0.01);
    pan.setTextBoxStyle(juce::Slider::TextBoxRight, false, 62, 20);
    trim.onDragStart = [this] { session.begin("Input trim"); };
    trim.onValueChange = [this]
    {
        if (auto* p = session.trim(selectedTrack))
            p->setVolumeDb(static_cast<float>(trim.getValue()));
    };
    pan.onDragStart = [this] { session.begin("Pan"); };
    pan.onValueChange = [this]
    {
        if (auto* p = session.fader(selectedTrack))
            p->setPan(static_cast<float>(pan.getValue()));
    };
    mode.addItem("Stereo", 1);
    mode.addItem("Mono fold", 2);
    mode.onChange = [this] { session.setMono(selectedTrack, mode.getSelectedId() == 2); };
    input.onChange = [this]
    { report(session.assignInput(selectedTrack, input.getSelectedId() - 2, session.armed(selectedTrack))); };
    output.onChange = [this] { report(session.route(selectedTrack, output.getSelectedId() - 2)); };
    addInsert.onClick = [this] { insertMenu(); };
    vst.onClick = [this] { choose(6); };
    addSend.onClick = [this]
    {
        juce::PopupMenu m;
        auto list = session.tracks();
        for (int i = 0; i < list.size(); ++i)
            if (i != selectedTrack)
                m.addItem(i + 1, list[i]->getName());
        m.showMenuAsync(juce::PopupMenu::Options{}.withTargetComponent(&addSend),
                        [safe = juce::Component::SafePointer<ProStudio>(this)](int id)
                        {
                            if (safe && id > 0)
                            {
                                safe->report(safe->session.send(safe->selectedTrack, id - 1));
                                safe->refresh();
                            }
                        });
    };
    connectSidechain.onClick = [this]
    { report(session.sidechain(sidechain.getSelectedId() - 2, selectedTrack, selectedInsert)); };
    snap.addItem("1/4 beat", 1);
    snap.addItem("1 beat", 2);
    snap.addItem("1 bar", 3);
    snap.addItem("Snap off", 4);
    snap.setSelectedId(2);
    snap.setTooltip("Grid quantization for clip/note edits");
    for (auto* button : {&regionTools, &midiTools, &transportTools})
        addAndMakeVisible(button);
    regionTools.onClick = [this] { workflowMenu(104); };
    midiTools.onClick = [this] { workflowMenu(105); };
    transportTools.onClick = [this] { workflowMenu(106); };
    addAndMakeVisible(velocity);
    velocity.setRange(1, 127, 1);
    velocity.setValue(96);
    velocity.setTextBoxStyle(juce::Slider::TextBoxRight, false, 42, 20);
    velocity.setTooltip("Selected note velocity / Default velocity for newly drawn notes");
    velocity.onDragStart = [this] { session.begin("Edit note velocity"); };
    velocity.onValueChange = [this]
    {
        noteVelocity = static_cast<int>(velocity.getValue());
        if (auto* midi = dynamic_cast<te::MidiClip*>(selectedClip.get()))
            if (auto* note = midi->getSequence().getNoteFor(selectedNoteState))
                note->setVelocity(noteVelocity, &session.edit->getUndoManager());
        repaint();
    };
    horizontal.addListener(this);
    session.beforeSessionChange = [this] { clearSessionViews(); };
    session.edit->getTransport().ensureContextAllocated();
    message.setText("Ready / Add an audio or instrument track. Ctrl+S saves. F1 opens shortcuts.",
                    juce::dontSendNotification);
    setWantsKeyboardFocus(true);
    setSize(1600, 1000);
    refresh();
    startTimerHz(30);
}
ProStudio::~ProStudio()
{
    stopTimer();
    session.beforeSessionChange = nullptr;
    clearSessionViews();
    if (audioWindow)
        delete audioWindow.getComponent();
    if (assistantWindow)
        delete assistantWindow.getComponent();
    setLookAndFeel(nullptr);
}
void ProStudio::clearSessionViews()
{
    for (auto* window : {&regionWindow, &midiWindow, &exportWindow, &transportWindow})
        if (*window)
            delete window->getComponent();
    selectedNoteState = juce::ValueTree{};
    selectedClip = nullptr;
    draggedNote = nullptr;
    pluginWindow.reset();
    channels.clear();
    masterStrip.reset();
    thumbs.clear();
    lastTrackCount = -1;
}
void ProStudio::refreshChannels()
{
    auto n = session.tracks().size();
    if (n == lastTrackCount)
        return;
    channels.clear();
    for (int i = 0; i < n; ++i)
    {
        auto* c = channels.add(new ChannelStrip(*this, i));
        addAndMakeVisible(c);
    }
    masterStrip = std::make_unique<ChannelStrip>(*this, -1);
    addAndMakeVisible(*masterStrip);
    lastTrackCount = n;
    resized();
}
void ProStudio::refresh(bool controls)
{
    if (inspectorContent)
    {
        inspectorContent->setSize(
            198, std::max(800, 550 + static_cast<int>(session.inserts(selectedTrack).size()) * 26));
        inspectorContent->repaint();
    }
    auto list = session.tracks();
    selectedTrack = std::clamp(selectedTrack, 0, std::max(0, list.size() - 1));
    refreshChannels();
    if (selectedClip && (!selectedClip->state.getParent().isValid()))
        selectedClip = nullptr;
    if (controls)
    {
        auto* t = session.track(selectedTrack);
        name.setText(t ? t->getName() : "No channel selected", juce::dontSendNotification);
        tempo.setText(juce::String(session.bpm(), 1), juce::dontSendNotification);
        trim.setEnabled(t != nullptr);
        pan.setEnabled(t != nullptr);
        mode.setEnabled(t != nullptr);
        if (auto* f = session.fader(selectedTrack))
            pan.setValue(f->getPan(), juce::dontSendNotification);
        if (auto* f = session.trim(selectedTrack))
            trim.setValue(f->getVolumeDb(), juce::dontSendNotification);
        mode.setSelectedId(session.mono(selectedTrack) ? 2 : 1, juce::dontSendNotification);
        updateInputMenu();
        updateRoutes();
    }
    horizontal.setRangeLimits(0, std::max(128.0, session.beats(session.edit->getLength().inSeconds()) + 32));
    horizontal.setCurrentRange(firstBeat, std::max(1.0, (arrange.getWidth() - 240) / pixelsPerBeat),
                               juce::dontSendNotification);
    repaint();
}
void ProStudio::scrollBarMoved(juce::ScrollBar*, double start)
{
    firstBeat = start;
    repaint();
}
void ProStudio::timerCallback()
{
    if (++ticks % 900 == 0)
        session.autosave();
    if (ticks % 30 == 0 && !audioWindow)
        updateInputMenu();
    if (session.tracks().size() != lastTrackCount)
        refresh();
    for (auto* c : channels)
        c->tick();
    if (masterStrip)
        masterStrip->tick();
    for (auto* b : buttons)
    {
        int id = b->getComponentID().getIntValue();
        if (id == 3)
            b->setToggleState(session.edit->getTransport().isPlaying(), juce::dontSendNotification);
        if (id == 4)
            b->setToggleState(session.edit->getTransport().isRecording(), juce::dontSendNotification);
        if (id == 5)
            b->setToggleState(session.edit->getTransport().looping, juce::dontSendNotification);
        if (id == 6)
            b->setToggleState(session.edit->clickTrackEnabled, juce::dontSendNotification);
    }
    repaint();
}
void ProStudio::updateInputMenu()
{
    session.configureInputs();
    input.clear(juce::dontSendNotification);
    input.addItem("No input", 1);
    int id = 2, selected = 1;
    auto devices = session.edit->getAllInputDevices();
    auto* t = session.track(selectedTrack);
    for (auto* d : devices)
    {
        input.addItem(d->getInputDevice().getName(), id);
        if (t && te::isOnTargetTrack(*d, *t, 0))
            selected = id;
        ++id;
    }
    input.setSelectedId(selected, juce::dontSendNotification);
}
void ProStudio::updateRoutes()
{
    output.clear(juce::dontSendNotification);
    sidechain.clear(juce::dontSendNotification);
    output.addItem("Stereo Out", 1);
    sidechain.addItem("Select source", 1);
    int selected = 1;
    auto* t = session.track(selectedTrack);
    auto list = session.tracks();
    for (int i = 0; i < list.size(); ++i)
        if (i != selectedTrack)
        {
            output.addItem(list[i]->getName(), i + 2);
            sidechain.addItem(list[i]->getName(), i + 2);
            if (t && t->getOutput().getDestinationTrack() == list[i])
                selected = i + 2;
        }
    output.setSelectedId(selected, juce::dontSendNotification);
    sidechain.setSelectedId(1, juce::dontSendNotification);
}
void ProStudio::select(int index, te::Clip* clip)
{
    selectedTrack = index;
    selectedClip = clip;
    selectedNoteState = juce::ValueTree{};
    draggedNote = nullptr;
    if (auto* midi = dynamic_cast<te::MidiClip*>(clip))
        noteBase = std::clamp(midi->getSequence().getNoteNumberRange().getStart() - 2, 0, 104);
    selectedInsert = -1;
    refresh();
}
void ProStudio::resized()
{
    int mx = 8;
    for (int i = 0; i < 6; ++i)
    {
        buttons[i]->setBounds(mx, 5, i == 5 ? 84 : 64, 22);
        mx += i == 5 ? 88 : 68;
    }
    int tx = 232;
    for (int i = 6; i < 12; ++i)
    {
        buttons[i]->setBounds(tx, 43, i == 9 ? 58 : 52, 29);
        tx += i == 9 ? 64 : 58;
    }
    regionTools.setBounds(444, 5, 64, 22);
    midiTools.setBounds(512, 5, 64, 22);
    transportTools.setBounds(580, 5, 88, 22);
    tempo.setBounds(760, 37, 78, 25);
    bool wide = getWidth() >= 1450;
    int x = 222;
    for (int i = 12; i < buttons.size(); ++i)
    {
        int id = buttons[i]->getComponentID().getIntValue();
        bool visible = wide || !(id >= 13 && id <= 16);
        buttons[i]->setVisible(visible);
        if (!visible)
            continue;
        int width = i == 13 ? 95 : i == 16 ? 90 : (id == 17 || id == 18) ? 32 : id == 21 ? 82 : 60;
        buttons[i]->setBounds(x, 98, width, 24);
        x += width + 4;
    }
    snap.setBounds(getWidth() - 150, 98, 136, 24);
    int consoleHeight = std::clamp(getHeight() / 3, 220, 288);
    console = {214, getHeight() - consoleHeight - 38, getWidth() - 222, consoleHeight};
    int arrangeWidth = getWidth() - (wide ? 440 : 222);
    ruler = {454, 140, arrangeWidth - 240, 30};
    arrange = {214, 170, arrangeWidth, console.getY() - 192};
    browser = wide ? juce::Rectangle<int>(getWidth() - 222, 134, 214, console.getY() - 144)
                   : juce::Rectangle<int>{};
    inspectorViewport.setBounds(0, 134, 206, getHeight() - 166);
    name.setBounds(12, 23, 184, 26);
    input.setBounds(14, 85, 184, 24);
    mode.setBounds(14, 141, 184, 24);
    trim.setBounds(10, 199, 188, 24);
    pan.setBounds(10, 253, 188, 24);
    output.setBounds(14, 311, 184, 24);
    addSend.setBounds(14, 345, 184, 23);
    sidechain.setBounds(14, 407, 184, 24);
    connectSidechain.setBounds(14, 440, 184, 23);
    addInsert.setBounds(14, 506, 88, 25);
    vst.setBounds(108, 506, 90, 25);
    message.setBounds(8, getHeight() - 28, getWidth() - 16, 22);
    horizontal.setBounds(454, arrange.getBottom() + 4, arrange.getWidth() - 240, 12);
    int visible = (console.getWidth() - 110) / 94;
    for (int i = 0; i < channels.size(); ++i)
    {
        bool show = !piano && i >= mixScroll && i < mixScroll + visible;
        channels[i]->setVisible(show);
        channels[i]->setBounds(console.getX() + (i - mixScroll) * 94, console.getY() + 25, 94,
                               console.getHeight() - 25);
    }
    if (masterStrip)
    {
        masterStrip->setVisible(!piano);
        masterStrip->setBounds(getWidth() - 102, console.getY() + 25, 94, console.getHeight() - 25);
    }
    pianoGrid = {console.getX() + 56, console.getY() + 27, console.getWidth() - 66, console.getHeight() - 36};
    velocity.setBounds(getWidth() - 230, console.getY() + 2, 210, 20);
    velocity.setVisible(piano);
    refresh(false);
}
float ProStudio::beatX(double beat) const
{
    return static_cast<float>(arrange.getX() + 240 + (beat - firstBeat) * pixelsPerBeat);
}
double ProStudio::xBeat(float x) const
{
    return firstBeat + (x - arrange.getX() - 240) / pixelsPerBeat;
}
double ProStudio::snapped(double beat) const
{
    double step = snap.getSelectedId() == 1 ? 0.25 : snap.getSelectedId() == 3 ? 4 : 1;
    return snap.getSelectedId() == 4 ? std::max(0.0, beat) : std::max(0.0, std::round(beat / step) * step);
}
juce::Rectangle<int> ProStudio::row(int index) const
{
    return {arrange.getX(), arrange.getY() + (index - scrollTrack) * 72, arrange.getWidth(), 72};
}
int ProStudio::trackAt(int y) const
{
    int i = scrollTrack + (y - arrange.getY()) / 72;
    return y >= arrange.getY() && y < arrange.getBottom() &&
                   juce::isPositiveAndBelow(i, session.tracks().size())
               ? i
               : -1;
}
juce::Rectangle<int> ProStudio::clipBounds(te::Clip& c) const
{
    int idx = session.tracks().indexOf(dynamic_cast<te::AudioTrack*>(c.getTrack()));
    auto p = c.getPosition();
    int x = static_cast<int>(beatX(session.beats(p.getStart().inSeconds()))),
        end = static_cast<int>(beatX(session.beats(p.getEnd().inSeconds())));
    auto r = row(idx);
    return {x, r.getY() + 5, std::max(6, end - x), r.getHeight() - 10};
}
te::Clip* ProStudio::clipAt(juce::Point<int> p) const
{
    auto* t = session.track(trackAt(p.y));
    if (t && p.x >= arrange.getX() + 240)
        for (auto* c : t->getClips())
            if (clipBounds(*c).contains(p))
                return c;
    return nullptr;
}
void ProStudio::paint(juce::Graphics& g)
{
    g.fillAll(bg);
    g.setColour(panel);
    g.fillRect(0, 0, getWidth(), 30);
    g.fillRect(0, 92, getWidth(), 38);
    g.fillRect(0, 134, 206, getHeight() - 166);
    g.fillRect(browser);
    g.fillRect(console);
    g.setColour(line);
    g.drawHorizontalLine(90, 0, static_cast<float>(getWidth()));
    g.drawVerticalLine(206, 132, static_cast<float>(getHeight() - 32));
    label(g, "BTE STUDIO", {16, 42, 190, 25}, 21, ink);
    label(g, "TRACKTION ENGINE / 64-BIT", {16, 69, 190, 16}, 9, dim);
    auto at = session.position();
    double beat = session.beats(at);
    int bars = static_cast<int>(beat / 4) + 1;
    label(g,
          juce::String(bars).paddedLeft('0', 3) + " : " + juce::String(static_cast<int>(beat) % 4 + 1) +
              " : " + juce::String(static_cast<int>(beat * 960) % 960).paddedLeft('0', 3),
          {620, 39, 136, 24}, 20, blue);
    label(g, "BAR      BEAT      TICK", {620, 65, 136, 15}, 9, dim);
    label(g, "BPM / 4:4", {769, 65, 90, 16}, 9, dim);
    auto& dm = session.engine.getDeviceManager();
    if (getWidth() >= 1450)
    {
        label(g,
              juce::String(dm.getSampleRate() / 1000, 1) + " kHz  /  " + juce::String(dm.getBlockSize()) +
                  " samples",
              {875, 40, 220, 22}, 12, dim);
        label(g, juce::String(at, 3) + " s", {875, 63, 200, 18}, 11, dim);
    }
    label(g,
          session.projectFile.getParentDirectory().getFileName() +
              (session.edit->hasChangedSinceSaved() ? " *" : ""),
          {getWidth() - 330, 40, 310, 22}, 13, ink, juce::Justification::centredRight);
    label(g,
          session.edit->getTransport().isRecording() ? "RECORDING"
          : session.edit->getTransport().isPlaying() ? "PLAYING"
                                                     : "STOPPED",
          {getWidth() - 250, 66, 230, 17}, 10, session.edit->getTransport().isRecording() ? red : dim,
          juce::Justification::centredRight);
    if (!browser.isEmpty())
    {
        label(g, "LIBRARY", browser.reduced(12).withHeight(20), 10, dim);
        label(g, "BUILT-IN INSTRUMENTS", {browser.getX() + 12, 170, 190, 18}, 9, dim);
        label(g, "4OSC / Polyphonic synth", {browser.getX() + 12, 195, 190, 26}, 12, blue);
        label(g, "BTE Drum Machine", {browser.getX() + 12, 219, 190, 26}, 12, blue);
        label(g, "BUILT-IN EFFECTS", {browser.getX() + 12, 253, 190, 20}, 9, dim);
        for (int i = 1; i < Workstation::effectNames().size() - 1; ++i)
            label(g, Workstation::effectNames()[i], {browser.getX() + 12, 283 + (i - 1) * 30, 190, 27}, 12,
                  ink);
        label(g, "Double-click an item to insert", {browser.getX() + 12, 493, 190, 20}, 10, dim);
        label(g, "VST3 effects and instruments", {browser.getX() + 12, 520, 190, 20}, 10, dim);
        label(g, "load from the channel inspector", {browser.getX() + 12, 539, 190, 20}, 10, dim);
    }
    drawTimeline(g);
    label(g,
          piano ? "PIANO ROLL / Select note to edit velocity"
                : "MIXER / Post-fader meters / Scroll to reveal channels",
          {console.getX() + 10, console.getY() + 3, console.getWidth() - (piano ? 240 : 20), 20}, 10, dim);
    if (piano)
    {
        label(g, "Velocity", {getWidth() - 288, console.getY() + 3, 56, 20}, 10, dim);
        drawPiano(g);
    }
    if (cable >= 0)
    {
        auto r = row(cable);
        juce::Path p;
        p.startNewSubPath(static_cast<float>(r.getX() + 225), static_cast<float>(r.getY() + 48));
        p.cubicTo(static_cast<float>(r.getX() + 320), static_cast<float>(r.getY() + 48), cableEnd.x + 60,
                  cableEnd.y, cableEnd.x, cableEnd.y);
        g.setColour(blue);
        g.strokePath(p, juce::PathStrokeType(2.5f));
    }
}
void ProStudio::drawTimeline(juce::Graphics& g)
{
    g.setColour(dark);
    g.fillRect(arrange);
    g.fillRect(ruler);
    label(g, "TRACKS / " + juce::String(session.tracks().size()), {arrange.getX() + 12, 141, 220, 24}, 10,
          dim);
    juce::Graphics::ScopedSaveState state(g);
    g.reduceClipRegion(arrange.withTop(ruler.getY()));
    auto list = session.tracks();
    for (int i = 0; i < list.size(); ++i)
    {
        auto r = row(i);
        if (r.getBottom() < arrange.getY() || r.getY() >= arrange.getBottom())
            continue;
        g.setColour(i == selectedTrack ? juce::Colour(0xff2f3742) : panel);
        g.fillRect(r.withWidth(238).reduced(0, 1));
        g.setColour(trackColour(i));
        g.fillRect(r.getX(), r.getY() + 2, 3, 68);
        label(g, juce::String(i + 1).paddedLeft('0', 2) + "  " + list[i]->getName(),
              {r.getX() + 10, r.getY() + 6, 190, 20}, 12);
        label(g, list[i]->getOutput().getDescriptiveOutputName(), {r.getX() + 10, r.getY() + 46, 188, 16}, 9,
              dim);
        for (int j = 0; j < 4; ++j)
        {
            bool monitor = false;
            for (auto* dev : session.edit->getAllInputDevices())
                if (te::isOnTargetTrack(*dev, *list[i], 0))
                    monitor = dev->getInputDevice().getMonitorMode() != te::InputDevice::MonitorMode::off;
            bool on = j == 0   ? list[i]->isMuted(false)
                      : j == 1 ? list[i]->isSolo(false)
                      : j == 2 ? session.armed(i)
                               : monitor;
            auto b = juce::Rectangle<int>(r.getX() + 10 + j * 30, r.getY() + 28, 24, 18);
            g.setColour(on ? (j == 2 ? red : blue) : line);
            g.fillRoundedRectangle(b.toFloat(), 3);
            label(g,
                  j == 0   ? "M"
                  : j == 1 ? "S"
                  : j == 2 ? "R"
                           : "I",
                  b, 10, ink, juce::Justification::centred);
        }
        g.setColour(trackColour(i));
        g.drawEllipse(static_cast<float>(r.getX() + 220), static_cast<float>(r.getY() + 42), 10, 10, 1.8f);
        if (i < channels.size())
        {
            meter(g, {r.getX() + 205, r.getY() + 6, 4, 54}, channels[i]->left);
            meter(g, {r.getX() + 211, r.getY() + 6, 4, 54}, channels[i]->right);
        }
        g.setColour(line);
        g.drawHorizontalLine(r.getBottom() - 1, static_cast<float>(arrange.getX()),
                             static_cast<float>(arrange.getRight()));
    }
    {
        juce::Graphics::ScopedSaveState timeline(g);
        g.reduceClipRegion(arrange.withTrimmedLeft(240).withTop(ruler.getY()));
        int first = static_cast<int>(firstBeat),
            last = static_cast<int>(xBeat(static_cast<float>(arrange.getRight()))) + 1;
        for (int b = first; b <= last; ++b)
        {
            int x = static_cast<int>(beatX(b));
            g.setColour(b % 4 == 0 ? juce::Colour(0xff39414d) : juce::Colour(0xff252a31));
            g.drawVerticalLine(x, static_cast<float>(arrange.getY()),
                               static_cast<float>(arrange.getBottom()));
            if (b % 4 == 0)
                label(g, juce::String(b / 4 + 1), {x + 5, ruler.getY() + 7, 60, 11}, 9, dim);
        }
        auto loop = session.edit->getTransport().getLoopRange();
        int lx = static_cast<int>(beatX(session.beats(loop.getStart().inSeconds()))),
            rx = static_cast<int>(beatX(session.beats(loop.getEnd().inSeconds())));
        g.setColour(session.edit->recordingPunchInOut      ? red
                    : session.edit->getTransport().looping ? blue
                                                           : line);
        g.fillRect(lx, ruler.getY() + 2, std::max(0, rx - lx), 5);
        for (auto* marker : session.edit->getMarkerManager().getMarkers())
        {
            int mx = static_cast<int>(beatX(session.beats(marker->getPosition().getStart().inSeconds())));
            g.setColour(blue);
            g.fillRect(mx, ruler.getY() + 18, 3, 12);
            label(g, marker->getName(), {mx + 5, ruler.getY() + 18, 90, 12}, 9, blue);
        }
        for (int i = 0; i < list.size(); ++i)
        {
            auto r = row(i);
            if (r.getY() < arrange.getY() - 72 || r.getY() >= arrange.getBottom())
                continue;
            for (auto* c : list[i]->getClips())
            {
                auto bounds = clipBounds(*c);
                auto visible = bounds.getIntersection(arrange.withTrimmedLeft(240));
                if (visible.isEmpty())
                    continue;
                auto colour = trackColour(i);
                g.setColour(colour.withAlpha(c == selectedClip.get() ? 0.66f : 0.36f));
                g.fillRoundedRectangle(bounds.toFloat(), 3);
                g.setColour(c == selectedClip.get() ? ink : colour);
                g.drawRoundedRectangle(bounds.toFloat(), 3, c == selectedClip.get() ? 1.5f : 0.7f);
                label(g, c->getName(), bounds.reduced(7, 0).withHeight(18), 10, ink);
                if (auto* audio = dynamic_cast<te::AudioClipBase*>(c))
                {
                    g.setColour(ink);
                    g.fillRect(bounds.getX() + 4, bounds.getY() + 4, 5, 5);
                    g.fillRect(bounds.getRight() - 9, bounds.getY() + 4, 5, 5);
                    double duration = c->getPosition().getLength().inSeconds();
                    float in = static_cast<float>(audio->getFadeIn().inSeconds() / duration *
                                                  bounds.getWidth()),
                          out = static_cast<float>(audio->getFadeOut().inSeconds() / duration *
                                                   bounds.getWidth());
                    g.setColour(ink.withAlpha(0.5f));
                    g.drawLine(static_cast<float>(bounds.getX()), static_cast<float>(bounds.getBottom()),
                               static_cast<float>(bounds.getX()) + in, static_cast<float>(bounds.getY()), 1);
                    g.drawLine(static_cast<float>(bounds.getRight()) - out, static_cast<float>(bounds.getY()),
                               static_cast<float>(bounds.getRight()), static_cast<float>(bounds.getBottom()),
                               1);
                }
                juce::Graphics::ScopedSaveState clipState(g);
                g.reduceClipRegion(visible.withTrimmedTop(18));
                if (auto* wave = dynamic_cast<te::WaveAudioClip*>(c))
                {
                    if (!wave->getOriginalFile().existsAsFile())
                    {
                        label(g, "MISSING AUDIO / Project > Relink", bounds.withTrimmedTop(22).reduced(6), 11,
                              red);
                        continue;
                    }
                    auto file = wave->getPlaybackFile().getFile();
                    auto key = file.getFullPathName();
                    auto& thumb = thumbs[key];
                    if (!thumb)
                        thumb = std::make_unique<te::SmartThumbnail>(
                            session.engine, te::AudioFile(session.engine, file), *this, session.edit.get());
                    auto p = c->getPosition();
                    g.setColour(colour.brighter(0.45f));
                    thumb->drawChannels(g, bounds.withTrimmedTop(20).reduced(2),
                                        {te::TimePosition::fromSeconds(wave->usesTimeStretchedProxy()
                                                                           ? 0
                                                                           : p.getOffset().inSeconds() *
                                                                                 wave->getSpeedRatio()),
                                         te::TimePosition::fromSeconds(
                                             wave->usesTimeStretchedProxy()
                                                 ? p.getLength().inSeconds()
                                                 : (p.getOffset().inSeconds() + p.getLength().inSeconds()) *
                                                       wave->getSpeedRatio())},
                                        0.8f);
                }
                else if (auto* midi = dynamic_cast<te::MidiClip*>(c))
                {
                    double startBeat = session.beats(c->getPosition().getStart().inSeconds());
                    for (auto* note : midi->getSequence().getNotes())
                    {
                        float nx = beatX(startBeat + note->getStartBeat().inBeats());
                        float nw = static_cast<float>(note->getLengthBeats().inBeats() * pixelsPerBeat);
                        float ny =
                            static_cast<float>(bounds.getBottom() - 6) - (note->getNoteNumber() - 36) * 0.6f;
                        g.setColour(colour.brighter(0.8f));
                        g.fillRect(nx, ny, std::max(2.0f, nw), 2.4f);
                    }
                }
            }
            if (automation)
            {
                if (auto* f = session.fader(i))
                {
                    auto& curve = f->volParam->getCurve();
                    juce::Path path;
                    bool started = false;
                    for (int n = 0; n < curve.getNumPoints(); ++n)
                    {
                        auto point = curve.getPoint(n);
                        double sec = te::toTime(point.time, session.edit->tempoSequence).inSeconds();
                        float x = beatX(session.beats(sec));
                        float db = te::volumeFaderPositionToDB(point.value);
                        float y = static_cast<float>(r.getBottom()) -
                                  std::clamp((db + 60) / 66.0f, 0.0f, 1.0f) * r.getHeight();
                        if (started)
                            path.lineTo(x, y);
                        else
                            path.startNewSubPath(x, y);
                        started = true;
                        g.setColour(juce::Colours::yellow);
                        g.fillEllipse(x - 3, y - 3, 6, 6);
                    }
                    g.setColour(juce::Colours::yellow);
                    g.strokePath(path, juce::PathStrokeType(1.5f));
                }
            }
        }
        int px = static_cast<int>(beatX(session.beats(session.position())));
        g.setColour(ink);
        g.drawVerticalLine(px, static_cast<float>(ruler.getY() + 8), static_cast<float>(arrange.getBottom()));
        if (list.isEmpty())
            label(g, "Create a track or drop audio / MIDI files into the arrangement.",
                  arrange.withTrimmedLeft(260).reduced(20).withHeight(40), 13, dim);
    }
}
void ProStudio::drawPiano(juce::Graphics& g)
{
    g.setColour(dark);
    g.fillRect(pianoGrid);
    auto* midi = dynamic_cast<te::MidiClip*>(selectedClip.get());
    if (!midi)
    {
        label(g, "Select a MIDI region. Add an instrument track and choose MIDI Region to create one.",
              pianoGrid.reduced(20).withHeight(40), 13, dim);
        return;
    }
    double length = session.beats(midi->getPosition().getEnd().inSeconds()) -
                    session.beats(midi->getPosition().getStart().inSeconds());
    float unit = static_cast<float>(pianoGrid.getWidth() / std::max(16.0, length));
    float height = pianoGrid.getHeight() / 24.0f;
    for (int n = 0; n < 24; ++n)
    {
        int pitch = noteBase + n, pc = pitch % 12;
        bool black = pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
        int y = pianoGrid.getBottom() - static_cast<int>((n + 1) * height);
        g.setColour(black ? juce::Colour(0xff1b2026) : juce::Colour(0xff252b33));
        g.fillRect(pianoGrid.getX(), y, pianoGrid.getWidth(), static_cast<int>(height));
        g.setColour(black ? dark : juce::Colour(0xffbec4cc));
        g.fillRect(pianoGrid.getX() - 48, y, 46, static_cast<int>(height) - 1);
        if (pc == 0)
            label(g, juce::MidiMessage::getMidiNoteName(pitch, true, true, 4),
                  {pianoGrid.getX() - 44, y, 40, static_cast<int>(height)}, 9, black ? ink : dark);
    }
    for (int b = 0; b <= static_cast<int>(std::max(16.0, length)); ++b)
    {
        g.setColour(b % 4 == 0 ? line : juce::Colour(0xff30343b));
        g.drawVerticalLine(pianoGrid.getX() + static_cast<int>(b * unit),
                           static_cast<float>(pianoGrid.getY()), static_cast<float>(pianoGrid.getBottom()));
    }
    for (auto* note : midi->getSequence().getNotes())
    {
        float x = pianoGrid.getX() + static_cast<float>(note->getStartBeat().inBeats()) * unit;
        float y = pianoGrid.getBottom() - (note->getNoteNumber() - noteBase + 1) * height;
        g.setColour(trackColour(selectedTrack).withAlpha(0.4f + note->getVelocity() / 220.0f));
        g.fillRoundedRectangle(
            x, y + 1, std::max(3.0f, static_cast<float>(note->getLengthBeats().inBeats()) * unit - 1),
            height - 2, 2);
        if (note->state == selectedNoteState)
        {
            g.setColour(ink);
            g.drawRoundedRectangle(
                x, y + 1, std::max(3.0f, static_cast<float>(note->getLengthBeats().inBeats()) * unit - 1),
                height - 2, 2, 1);
        }
    }
}
void ProStudio::pianoMouse(const juce::MouseEvent& e)
{
    auto* c = dynamic_cast<te::MidiClip*>(selectedClip.get());
    if (!c || !pianoGrid.contains(e.getPosition()))
        return;
    double length = session.beats(c->getPosition().getEnd().inSeconds()) -
                    session.beats(c->getPosition().getStart().inSeconds());
    double unit = pianoGrid.getWidth() / std::max(16.0, length);
    double beat = (e.x - pianoGrid.getX()) / unit;
    int pitch = std::clamp(
        noteBase + static_cast<int>((pianoGrid.getBottom() - e.y) / (pianoGrid.getHeight() / 24.0)), 0, 127);
    session.begin("Edit MIDI note");
    draggedNote = nullptr;
    for (auto* n : c->getSequence().getNotes())
        if (n->getNoteNumber() == pitch && beat >= n->getStartBeat().inBeats() &&
            beat < n->getEndBeat().inBeats())
        {
            if (e.mods.isRightButtonDown())
            {
                selectedNoteState = juce::ValueTree{};
                c->getSequence().removeNote(*n, &session.edit->getUndoManager());
                repaint();
                return;
            }
            draggedNote = n;
            break;
        }
    if (!draggedNote)
        draggedNote =
            c->getSequence().addNote(pitch, te::BeatPosition::fromBeats(snapped(beat)),
                                     te::BeatDuration::fromBeats(snap.getSelectedId() == 1 ? 0.25 : 1),
                                     noteVelocity, 0, &session.edit->getUndoManager());
    if (draggedNote)
    {
        selectedNoteState = draggedNote->state;
        velocity.setValue(draggedNote->getVelocity(), juce::dontSendNotification);
        noteStart = draggedNote->getStartBeat().inBeats();
        noteLength = draggedNote->getLengthBeats().inBeats();
        mouseStartBeat = beat;
        dragging = e.mods.isShiftDown() ? 5 : 4;
    }
    repaint();
}
void ProStudio::mouseDown(const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    if (piano && console.contains(e.getPosition()))
    {
        pianoMouse(e);
        return;
    }
    if (e.x >= 14 && e.x < 198 && e.y >= 676)
    {
        int i = (e.y - 676) / 26;
        if (i < static_cast<int>(session.inserts(selectedTrack).size()))
        {
            selectedInsert = i;
            repaint();
            if (e.mods.isRightButtonDown())
            {
                juce::PopupMenu m;
                m.addItem(1, "Open editor");
                m.addItem(2, "Bypass / enable");
                m.addItem(3, "Remove");
                m.addItem(4, "Move insert up");
                m.addItem(5, "Move insert down");
                m.showMenuAsync(juce::PopupMenu::Options{},
                                [safe = juce::Component::SafePointer<ProStudio>(this), i](int id)
                                {
                                    if (!safe)
                                        return;
                                    auto list = safe->session.inserts(safe->selectedTrack);
                                    if (!juce::isPositiveAndBelow(i, static_cast<int>(list.size())))
                                        return;
                                    if (id == 1)
                                        safe->showPlugin(i);
                                    if (id == 2)
                                        list[static_cast<size_t>(i)]->setEnabled(
                                            !list[static_cast<size_t>(i)]->isEnabled());
                                    if (id == 3)
                                    {
                                        safe->pluginWindow.reset();
                                        safe->session.removeEffect(safe->selectedTrack, i);
                                    }
                                    if (id == 4 || id == 5)
                                    {
                                        safe->pluginWindow.reset();
                                        safe->report(safe->session.reorderEffect(safe->selectedTrack, i,
                                                                                 id == 4 ? -1 : 1));
                                    }
                                    safe->refresh();
                                });
            }
        }
        return;
    }
    if (ruler.contains(e.getPosition()))
    {
        if (e.y >= ruler.getY() + 18)
        {
            for (auto* marker : session.edit->getMarkerManager().getMarkers())
                if (std::abs(beatX(session.beats(marker->getPosition().getStart().inSeconds())) - e.x) < 9)
                {
                    session.seek(marker->getPosition().getStart().inSeconds());
                    repaint();
                    return;
                }
        }
        double beat = snapped(xBeat(static_cast<float>(e.x)));
        if (e.mods.isShiftDown())
            session.setLoop(beat, beat + 16);
        else
            session.seek(session.seconds(beat));
        repaint();
        return;
    }
    if (!arrange.contains(e.getPosition()))
        return;
    int idx = trackAt(e.y);
    if (idx < 0)
        return;
    auto r = row(idx);
    int x = e.x - r.getX(), y = e.y - r.getY();
    if (x < 240)
    {
        select(idx);
        if (x >= 10 && x < 130 && y >= 28 && y <= 46)
        {
            auto* t = session.track(idx);
            session.begin("Channel state");
            if (x < 38)
                t->setMute(!t->isMuted(false));
            else if (x < 68)
                t->setSolo(!t->isSolo(false));
            else if (x < 98)
                command(25);
            else
            {
                for (auto* dev : session.edit->getAllInputDevices())
                    if (te::isOnTargetTrack(*dev, *t, 0))
                    {
                        auto& d = dev->getInputDevice();
                        d.setMonitorMode(d.getMonitorMode() == te::InputDevice::MonitorMode::off
                                             ? te::InputDevice::MonitorMode::automatic
                                             : te::InputDevice::MonitorMode::off);
                    }
            }
        }
        else if (x >= 217 && y > 34)
        {
            cable = idx;
            cableEnd = e.position;
        }
        return;
    }
    if (automation && e.mods.isAltDown())
    {
        float db = static_cast<float>(r.getBottom() - e.y) / r.getHeight() * 66 - 60;
        session.automationPoint(idx, session.seconds(snapped(xBeat(static_cast<float>(e.x)))), db);
        repaint();
        return;
    }
    auto* c = clipAt(e.getPosition());
    select(idx, c);
    if (c)
    {
        auto p = c->getPosition();
        dragStart = p.getStart().inSeconds();
        dragEnd = p.getEnd().inSeconds();
        dragOffset = p.getOffset().inSeconds();
        mouseStartBeat = xBeat(static_cast<float>(e.x));
        auto bounds = clipBounds(*c);
        dragging = e.x > bounds.getRight() - 8 ? 2 : e.x < bounds.getX() + 8 ? 3 : 1;
        if (dynamic_cast<te::AudioClipBase*>(c) && e.y < bounds.getY() + 14)
        {
            if (e.x < bounds.getX() + 14)
                dragging = 6;
            else if (e.x > bounds.getRight() - 14)
                dragging = 7;
        }
        session.begin(dragging >= 6 ? "Region fade" : "Move / trim region");
    }
}
void ProStudio::mouseDrag(const juce::MouseEvent& e)
{
    if (cable >= 0)
    {
        cableEnd = e.position;
        repaint();
        return;
    }
    if (dragging >= 4 && draggedNote)
    {
        auto* c = dynamic_cast<te::MidiClip*>(selectedClip.get());
        if (!c)
            return;
        double length = session.beats(c->getPosition().getEnd().inSeconds()) -
                        session.beats(c->getPosition().getStart().inSeconds());
        double unit = pianoGrid.getWidth() / std::max(16.0, length);
        double delta = (e.x - pianoGrid.getX()) / unit - mouseStartBeat;
        if (dragging == 5)
            draggedNote->setStartAndLength(
                te::BeatPosition::fromBeats(noteStart),
                te::BeatDuration::fromBeats(std::max(0.25, snapped(noteLength + delta))),
                &session.edit->getUndoManager());
        else
        {
            int pitch = std::clamp(
                noteBase + static_cast<int>((pianoGrid.getBottom() - e.y) / (pianoGrid.getHeight() / 24.0)),
                0, 127);
            draggedNote->setNoteNumber(pitch, &session.edit->getUndoManager());
            draggedNote->setStartAndLength(te::BeatPosition::fromBeats(snapped(noteStart + delta)),
                                           te::BeatDuration::fromBeats(noteLength),
                                           &session.edit->getUndoManager());
        }
        repaint();
        return;
    }
    if (!selectedClip || dragging == 0)
        return;
    double delta = xBeat(static_cast<float>(e.x)) - mouseStartBeat;
    if (dragging == 6 || dragging == 7)
    {
        if (auto* audio = dynamic_cast<te::AudioClipBase*>(selectedClip.get()))
        {
            double time = session.seconds(xBeat(static_cast<float>(e.x)));
            double fade =
                std::clamp(dragging == 6 ? time - dragStart : dragEnd - time, 0.0, dragEnd - dragStart);
            if (dragging == 6)
                audio->setFadeIn(te::TimeDuration::fromSeconds(fade));
            else
                audio->setFadeOut(te::TimeDuration::fromSeconds(fade));
        }
        repaint();
        return;
    }
    if (dragging == 1)
        selectedClip->setStart(
            te::TimePosition::fromSeconds(session.seconds(snapped(session.beats(dragStart) + delta))), false,
            true);
    if (dragging == 2)
        selectedClip->setEnd(te::TimePosition::fromSeconds(std::max(
                                 dragStart + 0.05, session.seconds(snapped(session.beats(dragEnd) + delta)))),
                             false);
    if (dragging == 3)
        selectedClip->setStart(te::TimePosition::fromSeconds(
                                   std::clamp(session.seconds(snapped(session.beats(dragStart) + delta)),
                                              std::max(0.0, dragStart - dragOffset), dragEnd - 0.05)),
                               true, false);
    repaint();
}
void ProStudio::mouseUp(const juce::MouseEvent& e)
{
    if (cable >= 0)
    {
        int dest = trackAt(e.y);
        if (arrange.contains(e.getPosition()) && dest >= 0)
            report(session.route(cable, dest));
        else if (console.contains(e.getPosition()) && e.x > getWidth() - 110)
            report(session.route(cable, -1));
        else
            message.setText("Route unchanged. Drop on a channel or Stereo Out.", juce::dontSendNotification);
        cable = -1;
        refresh();
    }
    if (dragging == 1 && selectedClip && arrange.contains(e.getPosition()))
    {
        int destination = trackAt(e.y);
        auto* target = session.track(destination);
        if (target && selectedClip->getTrack() != target && selectedClip->moveTo(*target))
        {
            selectedTrack = destination;
            refresh();
        }
    }
    dragging = 0;
    draggedNote = nullptr;
}
void ProStudio::mouseDoubleClick(const juce::MouseEvent& e)
{
    if (ruler.contains(e.getPosition()) && e.y >= ruler.getY() + 18)
    {
        for (auto* marker : session.edit->getMarkerManager().getMarkers())
            if (std::abs(beatX(session.beats(marker->getPosition().getStart().inSeconds())) - e.x) < 9)
            {
                showMarkerDialog(marker->getMarkerID());
                return;
            }
    }
    if (browser.contains(e.getPosition()))
    {
        int effect = e.y < 216 ? 0 : e.y < 250 ? 8 : 1 + (e.y - 283) / 30;
        if (juce::isPositiveAndBelow(effect, Workstation::effectTypes().size()))
        {
            if (!session.track(selectedTrack))
            {
                session.addTrack(effect == 0 ? "4OSC" : "Audio", effect == 0);
                selectedTrack = session.tracks().size() - 1;
                if (effect == 0)
                {
                    refresh();
                    return;
                }
            }
            if (!session.addEffect(selectedTrack, Workstation::effectTypes()[effect]))
                message.setText(session.status, juce::dontSendNotification);
            refresh();
        }
        return;
    }
    if (e.x >= 14 && e.x < 198 && e.y >= 676)
    {
        showPlugin((e.y - 676) / 26);
        return;
    }
    if (auto* c = clipAt(e.getPosition()))
    {
        select(trackAt(e.y), c);
        if (dynamic_cast<te::MidiClip*>(c))
        {
            piano = true;
            resized();
        }
        return;
    }
    if (arrange.contains(e.getPosition()) && e.x >= arrange.getX() + 240)
    {
        int i = trackAt(e.y);
        if (i >= 0)
        {
            auto* c = session.addMidiClip(i, snapped(xBeat(static_cast<float>(e.x))), 4);
            select(i, c);
            piano = true;
            resized();
        }
    }
}
void ProStudio::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (piano && pianoGrid.contains(e.getPosition()))
    {
        noteBase = std::clamp(noteBase + (w.deltaY > 0 ? 2 : -2), 0, 104);
        repaint();
        return;
    }
    if (console.contains(e.getPosition()))
    {
        mixScroll = std::clamp(mixScroll + (w.deltaY < 0 ? 1 : -1), 0, std::max(0, channels.size() - 1));
        resized();
        return;
    }
    if (e.mods.isCtrlDown())
    {
        pixelsPerBeat = std::clamp(pixelsPerBeat * (w.deltaY > 0 ? 1.2 : 0.8), 8.0, 256.0);
        refresh(false);
        return;
    }
    if (e.mods.isShiftDown())
    {
        firstBeat = std::max(0.0, firstBeat + (w.deltaY < 0 ? 4 : -4));
        refresh(false);
        return;
    }
    scrollTrack = std::clamp(scrollTrack + (w.deltaY < 0 ? 1 : -1), 0,
                             std::max(0, session.tracks().size() - std::max(1, arrange.getHeight() / 72)));
    repaint();
}
void ProStudio::report(juce::Result result)
{
    message.setText(result.wasOk() ? (session.status.isNotEmpty() ? session.status : "Done")
                                   : result.getErrorMessage(),
                    juce::dontSendNotification);
}
void ProStudio::command(int id)
{
    if (id >= 100)
    {
        popupMenu(id);
        return;
    }
    if (id == 1)
    {
        session.stop();
        session.seek(0);
    }
    if (id == 2)
        session.stop();
    if (id == 3)
        session.togglePlay();
    if (id == 4)
        report(session.record());
    if (id == 5)
        session.edit->getTransport().looping = !session.edit->getTransport().looping;
    if (id == 6)
        session.edit->clickTrackEnabled = !session.edit->clickTrackEnabled;
    if (id >= 7 && id <= 9)
    {
        auto name = id == 7 ? "Audio " : id == 8 ? "Instrument " : "Bus ";
        session.addTrack(juce::String(name) + juce::String(session.tracks().size() + 1), id == 8, id == 9);
        selectedTrack = session.tracks().size() - 1;
    }
    if (id == 10)
        choose(0);
    if (id == 11)
    {
        if (!session.track(selectedTrack))
            session.addTrack("4OSC", true);
        selectedClip = session.addMidiClip(selectedTrack, snapped(session.beats(session.position())));
        piano = true;
        resized();
    }
    if (id == 12)
        report(session.split(selectedClip.get()));
    if (id == 13)
    {
        if (auto* c = session.duplicate(selectedClip.get()))
            selectedClip = c;
    }
    if (id == 14)
    {
        session.deleteClip(selectedClip.get());
        selectedClip = nullptr;
    }
    if (id == 15)
    {
        dragging = 0;
        draggedNote = nullptr;
        pluginWindow.reset();
        session.undo();
    }
    if (id == 16)
    {
        dragging = 0;
        draggedNote = nullptr;
        pluginWindow.reset();
        session.redo();
    }
    if (id == 17)
        pixelsPerBeat = std::max(8.0, pixelsPerBeat / 1.3);
    if (id == 18)
        pixelsPerBeat = std::min(256.0, pixelsPerBeat * 1.3);
    if (id == 19)
    {
        piano = false;
        resized();
    }
    if (id == 20)
    {
        piano = true;
        resized();
    }
    if (id == 21)
    {
        automation = !automation;
        message.setText("Automation: Alt-click a track lane to add a volume point. Undo removes it.",
                        juce::dontSendNotification);
    }
    if (id == 24)
        showAudio();
    if (id == 25)
    {
        bool on = !session.armed(selectedTrack);
        auto device = input.getSelectedId() - 2;
        if (device < 0)
        {
            message.setText("Choose an input in the inspector before arming.", juce::dontSendNotification);
        }
        else
            report(session.assignInput(selectedTrack, device, on));
    }
    if (id == 26)
        showAssistant();
    refresh();
}
void ProStudio::popupMenu(int menu)
{
    juce::PopupMenu m;
    if (menu == 100)
    {
        m.addItem(201, "New session...");
        m.addItem(217, "Create playable built-in demo session");
        m.addItem(202, "Open session...");
        m.addItem(203, "Save", true);
        m.addItem(204, "Save copy as...");
        m.addItem(205, "Recover last autosave");
        m.addItem(219, "Find and relink missing audio...");
        m.addSeparator();
        m.addItem(206, "Export stereo WAV / 24-bit / 48 kHz...");
        m.addItem(207, "Export loudness-normalized WAV / -14 LUFS...");
        m.addItem(208, "Export individual channel stems...");
        m.addItem(218, "Export options / WAV, FLAC, MIDI...");
    }
    if (menu == 101)
    {
        m.addItem(15, "Undo / Ctrl+Z");
        m.addItem(16, "Redo / Ctrl+Shift+Z");
        m.addItem(12, "Split at playhead / S");
        m.addItem(13, "Duplicate region / Ctrl+D");
        m.addItem(14, "Delete selected region");
        m.addItem(209, "Delete selected track");
        m.addItem(210, "Fade selected audio region edges / 20 ms");
    }
    if (menu == 102)
    {
        m.addItem(7, "Add audio channel");
        m.addItem(8, "Add instrument channel / 4OSC");
        m.addItem(9, "Add bus channel");
        m.addItem(11, "New MIDI region");
        m.addItem(211, "Create 4-bar chord progression");
        m.addItem(215, "Create BTE drum instrument and groove");
    }
    if (menu == 103)
    {
        m.addItem(212, "Auto level match / -18 dBFS RMS");
        m.addItem(213, "Set loop to selected region");
        m.addItem(214, "Clear volume automation on selected track");
    }
    m.showMenuAsync(juce::PopupMenu::Options{},
                    [safe = juce::Component::SafePointer<ProStudio>(this)](int id)
                    {
                        if (!safe || id == 0)
                            return;
                        if (id < 200)
                        {
                            safe->command(id);
                            return;
                        }
                        auto& s = safe->session;
                        if (id == 217)
                        {
                            safe->requestClose(
                                [safe]
                                {
                                    if (safe)
                                    {
                                        safe->report(safe->session.createDemo());
                                        safe->refresh();
                                    }
                                });
                        }
                        if (id == 218)
                            safe->showExportDialog();
                        if (id == 219)
                            safe->showRelinkMenu();
                        if (id == 201)
                            safe->choose(1);
                        if (id == 202)
                            safe->choose(2);
                        if (id == 203)
                            safe->report(s.save());
                        if (id == 204)
                            safe->choose(3);
                        if (id == 205)
                            safe->report(s.recover());
                        if (id == 206)
                            safe->choose(4);
                        if (id == 207)
                            safe->choose(5);
                        if (id == 208)
                            safe->choose(7);
                        if (id == 209)
                        {
                            safe->pluginWindow.reset();
                            safe->selectedClip = nullptr;
                            safe->channels.clear();
                            safe->lastTrackCount = -1;
                            if (auto* t = s.track(safe->selectedTrack))
                            {
                                s.begin("Delete track");
                                s.edit->deleteTrack(t);
                            }
                        }
                        if (id == 210)
                            if (auto* c = dynamic_cast<te::WaveAudioClip*>(safe->selectedClip.get()))
                            {
                                s.begin("Fade region");
                                c->setFadeIn(te::TimeDuration::fromSeconds(0.02));
                                c->setFadeOut(te::TimeDuration::fromSeconds(0.02));
                            }
                        if (id == 211)
                        {
                            if (!s.track(safe->selectedTrack))
                            {
                                s.addTrack("4OSC", true);
                                safe->selectedTrack = s.tracks().size() - 1;
                            }
                            s.addChordProgression(safe->selectedTrack);
                        }
                        if (id == 215)
                            safe->selectedTrack = s.createDrumGroove();
                        if (id == 212)
                            safe->report(s.autoLevel());
                        if (id == 213 && safe->selectedClip)
                            s.edit->getTransport().setLoopRange(safe->selectedClip->getPosition().time);
                        if (id == 214)
                            if (auto* f = s.fader(safe->selectedTrack))
                            {
                                s.begin("Clear automation");
                                f->volParam->getCurve().clear(&s.edit->getUndoManager());
                            }
                        safe->refresh();
                    });
}
void ProStudio::insertMenu()
{
    juce::PopupMenu m;
    for (int i = 0; i < Workstation::effectNames().size(); ++i)
        m.addItem(i + 1, Workstation::effectNames()[i]);
    m.showMenuAsync(
        juce::PopupMenu::Options{}.withTargetComponent(&addInsert),
        [safe = juce::Component::SafePointer<ProStudio>(this)](int id)
        {
            if (safe && id > 0)
            {
                if (!safe->session.addEffect(safe->selectedTrack, Workstation::effectTypes()[id - 1]))
                    safe->message.setText(safe->session.status, juce::dontSendNotification);
                safe->refresh();
            }
        });
}
void ProStudio::showPlugin(int which)
{
    auto list = session.inserts(selectedTrack);
    if (!juce::isPositiveAndBelow(which, static_cast<int>(list.size())))
        return;
    pluginWindow.reset();
    te::Plugin::Ptr p = list[static_cast<size_t>(which)];
    juce::Component* content = nullptr;
    if (auto* ext = dynamic_cast<te::ExternalPlugin*>(p.get()))
        if (auto* instance = ext->getAudioPluginInstance())
        {
            content = instance->createEditorIfNeeded();
            if (!content)
                content = new juce::GenericAudioProcessorEditor(*instance);
        }
    if (!content)
        content = new ParameterPanel(p);
    pluginWindow = std::make_unique<EditorWindow>(p, content);
}
void ProStudio::showAudio()
{
    if (audioWindow)
    {
        audioWindow->toFront(true);
        return;
    }
    session.stop();
    auto* c =
        new juce::AudioDeviceSelectorComponent(session.audioDevices(), 0, 64, 0, 64, true, true, true, false);
    struct AudioPanel : juce::Component
    {
        ~AudioPanel() override
        {
            deleteAllChildren();
        }
    };
    auto* panelContent = new AudioPanel;
    panelContent->setSize(660, 540);
    c->setBounds(0, 40, 660, 500);
    panelContent->addAndMakeVisible(c);
    for (int channels : {1, 2})
    {
        auto* button =
            new juce::TextButton(channels == 1 ? "Inputs: separate mono channels" : "Inputs: stereo pairs");
        button->setBounds(channels == 1 ? 12 : 336, 6, 312, 28);
        panelContent->addAndMakeVisible(button);
        button->onClick = [this, channels]
        {
            auto& dm = session.engine.getDeviceManager();
            dm.setAllWaveInputsToNumChannels(static_cast<uint32_t>(channels));
            dm.dispatchPendingUpdates();
            session.configureInputs();
            updateInputMenu();
            message.setText("Input grouping changed. Reselect each track's recording source.",
                            juce::dontSendNotification);
        };
    }
    juce::DialogWindow::LaunchOptions o;
    o.dialogTitle = "Windows audio and MIDI / WASAPI";
    o.dialogBackgroundColour = panel;
    o.useNativeTitleBar = true;
    o.content.setOwned(panelContent);
    audioWindow = o.launchAsync();
}
void ProStudio::choose(int mode)
{
    if ((mode == 1 || mode == 2) && session.edit->hasChangedSinceSaved())
    {
        requestClose(
            [safe = juce::Component::SafePointer<ProStudio>(this), mode]
            {
                if (safe)
                {
                    safe->session.edit->resetChangedStatus();
                    safe->choose(mode);
                }
            });
        return;
    }
    auto filter = mode == 0                ? "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3;*.mid;*.midi"
                  : mode == 6              ? "*.vst3"
                  : mode == 4 || mode == 5 ? "*.wav"
                                           : "*.bte;*.tracktionedit";
    chooser =
        std::make_unique<juce::FileChooser>(mode == 6 ? "Choose a trusted 64-bit VST3 bundle" : "BTE Studio",
                                            session.projectFile.getParentDirectory(), filter);
    int flags = mode == 1 || mode == 3 || mode == 4 || mode == 5
                    ? juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting
                    : juce::FileBrowserComponent::openMode;
    flags |= mode == 7 ? juce::FileBrowserComponent::canSelectDirectories
                       : juce::FileBrowserComponent::canSelectFiles;
    if (mode == 6)
        flags |= juce::FileBrowserComponent::canSelectDirectories;
    if (mode == 0)
        flags |= juce::FileBrowserComponent::canSelectMultipleItems;
    chooser->launchAsync(
        flags,
        [safe = juce::Component::SafePointer<ProStudio>(this), mode](const juce::FileChooser& c)
        {
            if (!safe)
                return;
            auto f = c.getResult();
            if (f == juce::File{})
                return;
            if (mode == 0)
            {
                juce::StringArray paths;
                for (auto& a : c.getResults())
                    paths.add(a.getFullPathName());
                safe->filesDropped(paths, 0, 0);
            }
            if (mode == 1)
                safe->session.newSession(f.withFileExtension("bte"));
            if (mode == 2)
                safe->report(safe->session.open(f));
            if (mode == 3)
                safe->report(safe->session.save(f.withFileExtension("bte")));
            if (mode == 4 || mode == 5)
                safe->report(safe->session.exportWav(f.withFileExtension("wav"), mode == 5));
            if (mode == 6)
            {
                safe->pluginWindow.reset();
                safe->report(safe->session.loadVst(safe->selectedTrack, f));
            }
            if (mode == 7)
                safe->report(safe->session.exportStems(f));
            safe->refresh();
        });
}
void ProStudio::filesDropped(const juce::StringArray& files, int x, int y)
{
    for (auto& path : files)
    {
        juce::File f(path);
        auto ext = f.getFileExtension().toLowerCase();
        if (ext == ".mid" || ext == ".midi")
            report(session.importMidi(f));
        else if (ext == ".bte" || ext == ".tracktionedit")
            report(session.open(f));
        else
            report(session.importAudio(
                f, arrange.contains(x, y) ? trackAt(y) : -1,
                arrange.contains(x, y) ? session.seconds(snapped(xBeat(static_cast<float>(x)))) : 0));
    }
    refresh();
}
void ProStudio::requestClose(std::function<void()> done)
{
    session.edit->getUndoManager().dispatchPendingMessages();
    if (!session.edit->hasChangedSinceSaved())
    {
        done();
        return;
    }
    juce::AlertWindow::showYesNoCancelBox(
        juce::MessageBoxIconType::QuestionIcon, "Save session?", "Save changes before continuing?", "Save",
        "Discard", "Cancel", this,
        juce::ModalCallbackFunction::create(
            [safe = juce::Component::SafePointer<ProStudio>(this), done](int result)
            {
                if (!safe)
                    return;
                if (result == 1)
                {
                    auto r = safe->session.save();
                    safe->report(r);
                    if (r.wasOk())
                        done();
                }
                if (result == 2)
                    done();
            }));
}
void ProStudio::showAssistant()
{
    if (assistantWindow)
    {
        assistantWindow->toFront(true);
        return;
    }
    auto* content = new juce::Component;
    content->setSize(580, 260);
    auto* prompt = new juce::TextEditor;
    prompt->setBounds(18, 18, 544, 74);
    prompt->setMultiLine(true);
    prompt->setTextToShowWhenEmpty("Set tempo to 128 / Turn track 2 down 3 dB / Create chord progression",
                                   dim);
    content->addAndMakeVisible(prompt);
    auto* result = new juce::Label;
    result->setBounds(18, 104, 544, 78);
    content->addAndMakeVisible(result);
    auto* preview = new juce::TextButton("Preview command");
    preview->setBounds(18, 204, 260, 32);
    content->addAndMakeVisible(preview);
    auto* apply = new juce::TextButton("Apply edit");
    apply->setBounds(302, 204, 260, 32);
    apply->setEnabled(false);
    content->addAndMakeVisible(apply);
    preview->onClick = [this, prompt, result, apply]
    {
        auto r = session.plan(prompt->getText());
        result->setText(r.wasOk() ? session.pendingSummary : r.getErrorMessage(), juce::dontSendNotification);
        apply->setEnabled(r.wasOk());
    };
    apply->onClick = [this, apply, result]
    {
        report(session.applyPlan());
        apply->setEnabled(false);
        result->setText("Applied. Ctrl+Z undoes the edit.", juce::dontSendNotification);
        refresh();
    };
    prompt->onTextChange = [apply] { apply->setEnabled(false); };
    // Own the controls as children through a custom component destructor.
    struct Owned : juce::Component
    {
        ~Owned() override
        {
            deleteAllChildren();
        }
    };
    auto* owner = new Owned;
    owner->setSize(content->getWidth(), content->getHeight());
    while (content->getNumChildComponents())
    {
        auto* child = content->getChildComponent(0);
        content->removeChildComponent(child);
        owner->addAndMakeVisible(child);
    }
    delete content;
    juce::DialogWindow::LaunchOptions o;
    o.dialogTitle = "Offline assistant / Review before applying";
    o.dialogBackgroundColour = panel;
    o.useNativeTitleBar = true;
    o.content.setOwned(owner);
    assistantWindow = o.launchAsync();
}
bool ProStudio::keyPressed(const juce::KeyPress& key)
{
    auto mods = key.getModifiers();
    int code = key.getKeyCode();
    if (code >= 'a' && code <= 'z')
        code += 'A' - 'a';
    if (code == juce::KeyPress::spaceKey)
    {
        command(3);
        return true;
    }
    if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey)
    {
        command(14);
        return true;
    }
    if (mods.isCommandDown() && code == 'C')
    {
        report(session.copyClip(selectedClip.get()));
        return true;
    }
    if (mods.isCommandDown() && code == 'X')
    {
        dragging = 0;
        draggedNote = nullptr;
        auto r = session.copyClip(selectedClip.get(), true);
        report(r);
        if (r.wasOk())
            selectedClip = nullptr;
        refresh();
        return true;
    }
    if (mods.isCommandDown() && code == 'V')
    {
        if (auto* clip = session.pasteClip(selectedTrack, session.position()))
            select(selectedTrack, clip);
        else
            message.setText("Copy a region before pasting into a channel.", juce::dontSendNotification);
        return true;
    }
    if (mods.isCommandDown() && code == 'M')
    {
        showMarkerDialog();
        return true;
    }
    if (mods.isCommandDown() && mods.isShiftDown() && code == 'E')
    {
        showExportDialog();
        return true;
    }
    if (code == 'Q' && !mods.isCommandDown())
    {
        showMidiEditor(1);
        return true;
    }
    if (mods.isCommandDown())
    {
        if (code == 'S')
        {
            report(session.save());
            return true;
        }
        if (code == 'Z')
        {
            command(mods.isShiftDown() ? 16 : 15);
            return true;
        }
        if (code == 'D')
        {
            command(13);
            return true;
        }
        if (code == 'O')
        {
            choose(2);
            return true;
        }
    }
    if (code == 'S')
    {
        command(12);
        return true;
    }
    if (code == 'R')
    {
        command(4);
        return true;
    }
    if (code == 'L')
    {
        command(5);
        return true;
    }
    if (code == 'M')
    {
        command(19);
        return true;
    }
    if (code == 'P')
    {
        command(20);
        return true;
    }
    if (code == juce::KeyPress::F1Key)
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::MessageBoxIconType::InfoIcon, "Shortcuts",
            "Ctrl+C/X/V: copy/cut/paste region\nCtrl+M: marker\nQ: quantize\nCtrl+Shift+E: export "
            "options\nSpace: play/stop\nR: record\nL: loop\nS: split clip at playhead\nCtrl+D: "
            "duplicate\nDelete: "
            "delete clip\nCtrl+S: save\nCtrl+Z / Ctrl+Shift+Z: undo/redo\nShift-click ruler: set four-bar "
            "loop\nCtrl+wheel: zoom\nShift+wheel: scroll timeline\nAlt-click track lane: volume automation "
            "point\nPiano roll: click to draw, drag to move, Shift-drag to resize, right-click to remove");
        return true;
    }
    return false;
}
} // namespace bte
