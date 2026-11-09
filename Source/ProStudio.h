#pragma once
#include "ProEngine.h"
namespace bte
{
class ChannelStrip;
class ProStudio final : public juce::Component,
                        public juce::FileDragAndDropTarget,
                        private juce::Timer,
                        private juce::ScrollBar::Listener
{
  public:
    ProStudio();
    ~ProStudio() override;
    Workstation session;
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed(const juce::KeyPress&) override;
    bool isInterestedInFileDrag(const juce::StringArray&) override
    {
        return true;
    }
    void filesDropped(const juce::StringArray&, int, int) override;
    void requestClose(std::function<void()>);
    void command(int);
    void showPlugin(int);
    void refresh(bool controls = true);
    void report(juce::Result);
    int selectedTrack = 0;

  private:
    juce::LookAndFeel_V4 look;
    juce::TooltipWindow tips{this, 700};
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<juce::DocumentWindow> pluginWindow;
    juce::Component::SafePointer<juce::DialogWindow> audioWindow, assistantWindow;
    juce::Component::SafePointer<juce::DialogWindow> regionWindow, midiWindow, exportWindow, transportWindow;
    juce::OwnedArray<juce::TextButton> buttons;
    juce::OwnedArray<ChannelStrip> channels;
    std::unique_ptr<ChannelStrip> masterStrip;
    std::unique_ptr<juce::Component> inspectorContent;
    juce::Viewport inspectorViewport;
    juce::Label name, tempo, message;
    juce::ComboBox input, output, mode, sidechain, snap;
    juce::Slider trim, pan;
    juce::Slider velocity;
    juce::TextButton regionTools{"Region"}, midiTools{"MIDI"}, transportTools{"Transport"};
    juce::ValueTree selectedNoteState;
    juce::ScrollBar horizontal{false};
    juce::TextButton addInsert{"+ Insert"}, vst{"Browse VST3"}, addSend{"+ Send"},
        connectSidechain{"Connect SC"};
    te::Clip::Ptr selectedClip;
    std::map<juce::String, std::unique_ptr<te::SmartThumbnail>> thumbs;
    int scrollTrack = 0, mixScroll = 0, lastTrackCount = -1;
    int selectedInsert = -1, cable = -1, dragging = 0;
    juce::Point<float> cableEnd;
    double firstBeat = 0, pixelsPerBeat = 34, dragStart = 0, dragEnd = 0, dragOffset = 0;
    double mouseStartBeat = 0;
    te::MidiNote* draggedNote = nullptr;
    int noteBase = 48, noteVelocity = 96;
    double noteStart = 0, noteLength = 1;
    bool piano = false, automation = false, snapEnabled = true;
    int ticks = 0;
    juce::Rectangle<int> arrange, ruler, console, browser, pianoGrid;
    void timerCallback() override;
    void scrollBarMoved(juce::ScrollBar*, double start) override;
    void clearSessionViews();
    void refreshChannels();
    void select(int, te::Clip* = nullptr);
    void choose(int);
    void showAssistant();
    void showAudio();
    void popupMenu(int);
    void insertMenu();
    int trackAt(int) const;
    te::Clip* clipAt(juce::Point<int>) const;
    juce::Rectangle<int> row(int) const;
    juce::Rectangle<int> clipBounds(te::Clip&) const;
    float beatX(double) const;
    double xBeat(float) const;
    double snapped(double) const;
    void drawTimeline(juce::Graphics&);
    void drawPiano(juce::Graphics&);
    void pianoMouse(const juce::MouseEvent&);
    void updateInputMenu();
    void updateRoutes();
    void workflowMenu(int);
    void showRegionEditor();
    void showMidiEditor(int mode = 1);
    void showExportDialog();
    void showTransportDialog();
    void showRelinkMenu();
    void showMarkerDialog(int markerID = -1);
};
} // namespace bte
