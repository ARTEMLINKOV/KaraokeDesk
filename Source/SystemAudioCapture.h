#pragma once

#include <JuceHeader.h>
#include <functional>
#include <memory>

class SystemAudioCapture final
{
public:
    using Completion = std::function<void(const juce::String&)>;
    using AudioCallback = std::function<void(const float* const*, int, int)>;

    SystemAudioCapture();
    ~SystemAudioCapture();

    void start(const juce::File& destination, double sampleRate,
               AudioCallback audioCallback, Completion completion,
               const juce::StringArray& targetAppBundleIdentifiers = {},
               const juce::StringArray& excludedAppBundleIdentifiers = {});
    void stop();
    bool isCapturing() const;

private:
    struct Impl;
    std::shared_ptr<Impl> implementation;
};
