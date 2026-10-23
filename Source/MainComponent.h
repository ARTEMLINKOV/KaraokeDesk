#pragma once
#include <JuceHeader.h>
#include "KaraokeAudioEngine.h"
#include "SystemAudioCapture.h"
#include "AppleMusicChromium.h"

class PluginSlotComboBox final : public juce::ComboBox
{
public:
    std::function<void()> onPluginClick;

    void mouseDown(const juce::MouseEvent& event) override
    {
        if (getSelectedId() > 0 && event.x < getWidth() - 24 && onPluginClick)
        {
            onPluginClick();
            return;
        }

        juce::ComboBox::mouseDown(event);
    }
};

class KaraokeWebBrowserComponent final : public juce::WebBrowserComponent
{
public:
    std::function<void(const juce::String&)> onNewWindow;

    KaraokeWebBrowserComponent()
        : juce::WebBrowserComponent(
            juce::WebBrowserComponent::Options {}.withKeepPageLoadedWhenBrowserIsHidden())
    {
    }

    void newWindowAttemptingToLoad(const juce::String& newURL) override
    {
        if (onNewWindow && newURL.startsWithIgnoreCase("https://"))
            onNewWindow(newURL);
    }
};

class MainComponent final : public juce::Component,
                            private juce::Timer,
                            private juce::ChangeListener
{
public:
    MainComponent();
    ~MainComponent() override;
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    class LookAndFeel;
    class PluginEditorWindow;
    class WebPopupWindow;
    KaraokeAudioEngine engine;
    juce::AudioDeviceSelectorComponent outputDeviceSelector;
    juce::Component settingsContent;
    juce::Viewport settingsViewport;
    SystemAudioCapture youtubeCapture;
    SystemAudioCapture appleMusicCapture;
    juce::ThreadPool pluginScanPool { 1 };
    std::unique_ptr<LookAndFeel> customLookAndFeel;
    KaraokeWebBrowserComponent youtube;
    AppleMusicChromiumBrowser appleMusicWeb;
    std::unique_ptr<WebPopupWindow> webPopupWindow;
    juce::TextEditor url;

    juce::TextButton openButton { juce::String::fromUTF8("▶") };
    juce::TextButton appleMusicButton { juce::String::fromUTF8("♫") };
    juce::TextButton mixerToggle { juce::String::fromUTF8("≡") };
    juce::TextButton settingsToggle { juce::String::fromUTF8("⚙") };
    juce::TextButton scanPluginsButton { juce::String::fromUTF8("↻") };
    juce::TextButton skipPluginButton { juce::String::fromUTF8("⏭") };
    juce::TextButton addPluginDirectoryButton { juce::String::fromUTF8("+") };
    juce::TextButton recordButton { juce::String::fromUTF8("●") };
    juce::Label pluginScanStatus;
    juce::TextButton monitorButton { juce::String::fromUTF8("●") };
    juce::TextButton stopButton { juce::String::fromUTF8("■") };
    juce::TextButton muteButtons[5] {
        juce::TextButton { juce::String::fromUTF8("M") },
        juce::TextButton { juce::String::fromUTF8("M") },
        juce::TextButton { juce::String::fromUTF8("M") },
        juce::TextButton { juce::String::fromUTF8("M") },
        juce::TextButton { juce::String::fromUTF8("M") }
    };
    juce::TextButton soloButtons[5] {
        juce::TextButton { juce::String::fromUTF8("S") },
        juce::TextButton { juce::String::fromUTF8("S") },
        juce::TextButton { juce::String::fromUTF8("S") },
        juce::TextButton { juce::String::fromUTF8("S") },
        juce::TextButton { juce::String::fromUTF8("S") }
    };
    juce::TextButton vocalReductionButton { juce::String::fromUTF8("Ø") };

    juce::Label title, status, outputDeviceTitle, inputDeviceTitle;
    juce::Label ch1Title, ch2Title, masterTitle, youtubeTitle, appleMusicTitle, recordingResult;
    juce::Label sampleRateTitle, bufferSizeTitle, themeColourTitle;
    juce::Slider gain1, gain2, master, youtubeVolume, appleMusicVolume;
    juce::Slider* channelFaders[5] {
        &gain1, &gain2, &youtubeVolume, &appleMusicVolume, &master
    };
    juce::ColourSelector themeColourSelector {
        juce::ColourSelector::showColourAtTop | juce::ColourSelector::showSliders
            | juce::ColourSelector::showColourspace,
        8, 8
    };
    juce::ComboBox inputDevice, inputChannelSelectors[2], sampleRateSelector, bufferSizeSelector;
    juce::Array<double> availableSampleRates;
    juce::Array<int> availableBufferSizes;
    PluginSlotComboBox pluginSlots[5][4];
    std::unique_ptr<PluginEditorWindow> pluginEditorWindows[5][4];
    juce::StringArray pluginNames;
    juce::FileSearchPath customPluginPaths;
    juce::String pluginSlotNames[5][4];
    juce::String inputDeviceListSignature;
    juce::String inputChannelListSignature;
    juce::File currentRecordingDirectory;
    bool youtubeTrackRecording = false;
    bool youtubeCaptureRequested = false;
    bool appleMusicTrackRecording = false;
    bool appleMusicCaptureRequested = false;
    bool appleMusicPageOpened = false;
    bool appleMusicOpenPending = false;
    double appleMusicOpenStartedMs = 0.0;
    juce::String appleMusicStatus;
    bool youtubePageOpened = false;
    int activeBrowserChannel = 2;
    std::atomic<bool> skipPluginRequested { false };
    float smoothedMeterLevels[5] {};
    double lastMeterUpdateMs = 0.0;
    std::unique_ptr<juce::FileChooser> pluginDirectoryChooser;
    bool mixerVisible = true;
    bool settingsVisible = false;
    bool pluginScanRunning = false;
    bool recordingRunning = false;
    bool loopbackWarningShown = false;

    void timerCallback() override;
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void openYoutube();
    void showWebPopup(const juce::String& url);
    void setPluginSlot(int channel, int slot);
    void showPluginEditor(int channel, int slot);
    void scanPluginsInBackground();
    void skipCurrentPluginScan();
    void startYoutubeMixerCapture(const juce::File& destination);
    void startAppleMusicCapture(const juce::File& destination);
    void setAppleMusicCaptureError(const juce::String& message);
    void showLoopbackSetupWarning();
    void openAppleMusic();
    void choosePluginDirectory();
    void refreshInputDevices();
    void configureAllInputChannels();
    void selectInputDevice();
    void selectInputChannel(int channel);
    void refreshInputChannels();
    void refreshAudioDeviceOptions();
    void selectSampleRate();
    void selectBufferSize();
    void updatePluginLists();
    void showRecordingOptions();
    void stopRecording();
    void applyAccentColour(juce::Colour colour);
    void styleIconButton(juce::TextButton&, const juce::String& tooltip);
    void styleButton(juce::TextButton&);
    void styleSlider(juce::Slider&, double initialValue, double minimum = 0.0, double maximum = 1.0);
    void setYoutubeVolume();
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
