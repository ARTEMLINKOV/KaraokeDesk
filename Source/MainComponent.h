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
    juce::WebBrowserComponent youtube;
    juce::TextEditor url;
    juce::TextButton openButton { "OPEN VIDEO" }, startButton { "START AUDIO" }, stopButton { "STOP" };
    juce::Label title, status, ch1Title, ch2Title, masterTitle, pluginsTitle;
    juce::Slider gain1, gain2, master;
    juce::ComboBox plugin1, plugin2;
    juce::TextButton add1 { "+ ADD FX" }, add2 { "+ ADD FX" }, clear1 { "CLEAR FX" }, clear2 { "CLEAR FX" };
    juce::Label meter1, meter2, meterMaster;
    juce::StringArray pluginNames;
    void timerCallback() override;
    void openYoutube();
    void addPlugin(int channel);
    void styleButton(juce::TextButton&);
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
