#include "ProStudio.h"
class BteApplication final : public juce::JUCEApplication
{
  public:
    const juce::String getApplicationName() override
    {
        return "BTE Studio";
    }
    const juce::String getApplicationVersion() override
    {
        return "0.2.0";
    }
    void initialise(const juce::String& commandLine) override
    {
        if (tracktion::PluginManager::startChildProcessPluginScan(commandLine))
            return;
        window = std::make_unique<Window>();
        auto* studio = static_cast<bte::ProStudio*>(window->getContentComponent());
        if (commandLine.contains("--demo"))
        {
            studio->report(studio->session.createDemo());
            studio->refresh();
        }
        juce::StringArray paths;
        for (auto arg : juce::StringArray::fromTokens(commandLine, true))
        {
            auto path = arg.unquoted();
            if (juce::File::isAbsolutePath(path) && juce::File(path).existsAsFile())
                paths.add(path);
        }
        if (!paths.isEmpty())
            studio->filesDropped(paths, 0, 0);
        if (commandLine.contains("--smoke-test"))
            juce::Timer::callAfterDelay(3000, [this] { quit(); });
    }
    void shutdown() override
    {
        window.reset();
    }
    void systemRequestedQuit() override
    {
        if (window)
            static_cast<bte::ProStudio*>(window->getContentComponent())->requestClose([this] { quit(); });
        else
            quit();
    }

  private:
    struct Window final : juce::DocumentWindow
    {
        Window() : DocumentWindow("BTE Studio", juce::Colour(0xff181a1e), allButtons)
        {
            setUsingNativeTitleBar(true);
            setContentOwned(new bte::ProStudio, true);
            setResizable(true, false);
            setResizeLimits(1280, 700, 3840, 2160);
            centreWithSize(1600, 1000);
            setVisible(true);
        }
        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }
    };
    std::unique_ptr<Window> window;
};
START_JUCE_APPLICATION(BteApplication)
