#pragma once
#include <JuceHeader.h>
#include "KaraokeAudioEngine.h"

class MainComponent final : public juce::Component, private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    KaraokeAudioEngine engine;
    juce::ThreadPool pluginScanPool { 1 };
    juce::WebBrowserComponent youtube;
    juce::TextEditor url;

    juce::TextButton openButton { "ОТКРЫТЬ ВИДЕО" };
    juce::TextButton mixerToggle { "ПОКАЗАТЬ МИКШЕР" };
    juce::TextButton settingsToggle { "НАСТРОЙКИ" };
    juce::TextButton scanPluginsButton { "СКАНИРОВАТЬ VST / AU" };
    juce::Label pluginScanStatus;
    juce::TextButton startButton { "ВКЛЮЧИТЬ МИКРОФОНЫ" };
    juce::TextButton stopButton { "ОСТАНОВИТЬ ЗВУК" };
    juce::TextButton add1 { "+ ЭФФЕКТ" }, add2 { "+ ЭФФЕКТ" };
    juce::TextButton clear1 { "ОЧИСТИТЬ" }, clear2 { "ОЧИСТИТЬ" };

    juce::Label title, status, ch1Title, ch2Title, masterTitle, youtubeTitle;
    juce::Label meter1, meter2, meterMaster;
    juce::Slider gain1, gain2, master, youtubeVolume;
    juce::ComboBox plugin1, plugin2;
    juce::StringArray pluginNames;
    bool mixerVisible = false;
    bool settingsVisible = false;
    bool pluginScanRunning = false;

    void timerCallback() override;
    void openYoutube();
    void addPlugin(int channel);
    void scanPluginsInBackground();
    void styleButton(juce::TextButton&);
    void styleSlider(juce::Slider&, double initialValue);
    void setYoutubeVolume();
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
