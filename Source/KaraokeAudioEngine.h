#pragma once
#include <JuceHeader.h>
#include <vector>
#include <atomic>

class KaraokeAudioEngine final : public juce::AudioIODeviceCallback
{
public:
    KaraokeAudioEngine();
    ~KaraokeAudioEngine() override;
    void start();
    void stop();
    juce::AudioDeviceManager& getDeviceManager() { return deviceManager; }
    juce::String getStatus() const;
    juce::StringArray getAvailablePluginNames() const;
    bool addPluginToChannel(int channel, const juce::String& pluginName, juce::String& error);
    void clearChannel(int channel);
    void setChannelGain(int channel, float gain);
    void setMasterGain(float gain);
    float getChannelPeak(int channel) const;
    float getMasterPeak() const;
    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
        int numInputChannels, float* const* outputChannelData, int numOutputChannels,
        int numSamples, const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;
private:
    struct PluginSlot { juce::String name; std::unique_ptr<juce::AudioPluginInstance> instance; };
    juce::AudioDeviceManager deviceManager;
    juce::AudioPluginFormatManager formats;
    juce::KnownPluginList knownPlugins;
    juce::OwnedArray<juce::PluginDescription> descriptions;
    std::vector<PluginSlot> chains[2];
    mutable juce::CriticalSection pluginLock;
    double sampleRate = 48000.0;
    int blockSize = 512;
    int processingCapacity = 8192;
    std::atomic<float> channelGain[2] { 1.0f, 1.0f };
    std::atomic<float> masterGain { 0.7f };
    std::atomic<float> channelPeak[2] { 0.0f, 0.0f };
    std::atomic<float> masterPeak { 0.0f };
    juce::AudioBuffer<float> processingBuffers[2];
    juce::String status = "Not started";
    juce::MidiBuffer midiBuffer;
    void scanPlugins();
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KaraokeAudioEngine)
};
