#pragma once
#include <JuceHeader.h>
#include <vector>
#include <atomic>
#include <cstdint>
#include <functional>

class KaraokeAudioEngine final : public juce::AudioIODeviceCallback
{
public:
    KaraokeAudioEngine();
    ~KaraokeAudioEngine() override;
    void start();
    void stop();
    void saveSettings();
    juce::FileSearchPath getPluginSearchPaths() const;
    void setPluginSearchPaths(const juce::FileSearchPath& paths);
    juce::Colour getAccentColour() const;
    void setAccentColour(juce::Colour colour);
    juce::AudioDeviceManager& getDeviceManager() { return deviceManager; }
    juce::String getStatus() const;
    juce::StringArray getAvailablePluginNames() const;
    int scanPlugins(const juce::FileSearchPath& selectedPaths,
                    std::function<void(const juce::String&, float)> progress,
                    std::function<bool()> shouldSkipCurrent);
    static void scanPluginInHelper(const juce::String& pluginPath,
                                   const juce::String& resultPath);
    juce::AudioProcessorEditor* getPluginEditor(int channel, int slot);
    bool addPluginToChannel(int channel, const juce::String& pluginName, juce::String& error);
    bool setPluginInChannelSlot(int channel, int slot, const juce::String& pluginName, juce::String& error);
    void clearChannel(int channel);
    void setChannelGain(int channel, float gain);
    void setMasterGain(float gain);
    void setChannelInput(int channel, int inputIndex);
    void setLoopbackInputChannels(int leftInputIndex, int rightInputIndex);
    void setActiveBrowserChannel(int channel);
    bool hasLoopbackInput() const;
    void setChannelMuted(int channel, bool muted);
    void setChannelSolo(int channel, bool solo);
    void setAppleMusicVocalReduction(bool enabled);
    void pushYoutubeAudio(const float* const* inputChannelData,
                          int numInputChannels, int numSamples);
    void pushAppleMusicAudio(const float* const* inputChannelData,
                             int numInputChannels, int numSamples);
    bool startRecording(const juce::File& directory, bool mic1, bool mic2,
                        bool master, juce::String& error);
    void stopRecording();
    bool isRecording() const;
    juce::File getRecordingDirectory() const;
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
    juce::OwnedArray<juce::PluginDescription> descriptions;
    std::vector<PluginSlot> chains[5];
    mutable juce::CriticalSection pluginLock;
    juce::CriticalSection formatLock;
    juce::CriticalSection recordingLock;
    double sampleRate = 48000.0;
    int blockSize = 512;
    int processingCapacity = 8192;
    bool audioCallbackRegistered = false;
    std::atomic<float> channelGain[5] { 0.8f, 0.8f, 0.8f, 0.8f, 0.65f };
    std::atomic<float> masterGain { 0.65f };
    std::atomic<int> channelInput[2] { 0, 1 };
    std::atomic<int> loopbackInputChannels[2] { -1, -1 };
    std::atomic<int> activeBrowserChannel { 2 };
    std::atomic<bool> channelMuted[5] { false, false, false, false, false };
    std::atomic<bool> channelSolo[5] { false, false, false, false, false };
    std::atomic<float> channelPeak[5] { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    std::atomic<float> masterPeak { 0.0f };
    std::atomic<bool> appleMusicVocalReduction { false };
    juce::TimeSliceThread recordingThread { "KaraokeDesk recording writer" };
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> microphoneRecorders[2];
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> masterRecorder;
    std::atomic<bool> recording { false };
    juce::File recordingDirectory;
    juce::AudioBuffer<float> processingBuffers[5];
    juce::AbstractFifo youtubeFifo { 48000 * 10 };
    juce::AudioBuffer<float> youtubeFifoBuffer { 2, 48000 * 10 };
    juce::AbstractFifo appleMusicFifo { 48000 * 10 };
    juce::AudioBuffer<float> appleMusicFifoBuffer { 2, 48000 * 10 };
    juce::File settingsFile;
    juce::File pluginCacheFile;
    juce::FileSearchPath pluginSearchPaths;
    juce::Colour accentColour { 0xff2584c7 };
    juce::String status = "Not started";
    juce::MidiBuffer midiBuffer;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KaraokeAudioEngine)
};
