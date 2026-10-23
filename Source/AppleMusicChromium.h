#pragma once

#include <JuceHeader.h>

#if JUCE_MAC
class AppleMusicChromiumBrowser final : public juce::NSViewComponent
{
public:
    AppleMusicChromiumBrowser();
    ~AppleMusicChromiumBrowser() override;

    bool loadUrl(const juce::String& url);
    bool isBrowserCreated() const;
    void pumpMessageLoop();
    void pauseMedia();
    void resized() override;

private:
    struct Impl;
    std::shared_ptr<Impl> impl;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AppleMusicChromiumBrowser)
};

bool initializeAppleMusicChromium(int argc, char* argv[], juce::String& error);
void shutdownAppleMusicChromium();
juce::String getAppleMusicChromiumStartupError();
juce::String getAppleMusicChromiumLogPath();
#endif
