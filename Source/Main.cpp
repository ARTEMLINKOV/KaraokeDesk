#include <JuceHeader.h>
#include "MainComponent.h"
#include "AppleMusicChromium.h"

#if JUCE_MAC
extern "C" void configureKaraokeWindowForSpaces(void* nativeWindow);
#endif

class KaraokeDeskApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "KaraokeDesk"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise(const juce::String&) override
    {
        const auto arguments = getCommandLineParameterArray();
        if (arguments.size() == 3 && arguments[0] == "--karaokedesk-scan-plugin")
        {
            KaraokeAudioEngine::scanPluginInHelper(arguments[1], arguments[2]);
            quit();
            return;
        }

        mainWindow.reset(new MainWindow(getApplicationName()));
    }

    void shutdown() override
    {
        mainWindow = nullptr;
#if JUCE_MAC
        shutdownAppleMusicChromium();
#endif
    }
    void systemRequestedQuit() override { quit(); }

    class MainWindow final : public juce::DocumentWindow
    {
    public:
        explicit MainWindow(const juce::String& name)
            : DocumentWindow(name, juce::Colour(0xff10131a), DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar(true);
            setContentOwned(new MainComponent(), true);
            setResizable(true, true);
            setResizeLimits(900, 700, 4096, 4096);
            setAlwaysOnTop(true);
            centreWithSize(1440, 900);
            setVisible(true);
#if JUCE_MAC
            if (auto* peer = getPeer())
                configureKaraokeWindowForSpaces(peer->getNativeHandle());
#endif
        }
        void closeButtonPressed() override { JUCEApplication::getInstance()->systemRequestedQuit(); }
    };

private:
    std::unique_ptr<MainWindow> mainWindow;
};

static juce::JUCEApplicationBase* createKaraokeDeskApplication()
{
    return new KaraokeDeskApplication();
}

int main(int argc, char* argv[])
{
    juce::JUCEApplicationBase::createInstance = &createKaraokeDeskApplication;

#if JUCE_MAC
    const bool isPluginScanHelper = argc > 1
        && juce::String(argv[1]) == "--karaokedesk-scan-plugin";
    if (!isPluginScanHelper)
    {
        juce::String error;
        initializeAppleMusicChromium(argc, argv, error);
    }
#endif

    return juce::JUCEApplicationBase::main(argc, (const char**)argv);
}
