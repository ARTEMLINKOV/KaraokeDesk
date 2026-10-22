#include "KaraokeAudioEngine.h"

KaraokeAudioEngine::KaraokeAudioEngine()
{
    formats.addFormat(new juce::AudioUnitPluginFormat());
    formats.addFormat(new juce::VST3PluginFormat());
    scanPlugins();
}

KaraokeAudioEngine::~KaraokeAudioEngine() { stop(); }

void KaraokeAudioEngine::scanPlugins()
{
    knownPlugins.clear();
    for (int f = 0; f < formats.getNumFormats(); ++f)
    {
        auto* format = formats.getFormat(f);
        juce::FileSearchPath paths;
        if (format->getName().containsIgnoreCase("AudioUnit"))
        {
            paths.add(juce::File("/Library/Audio/Plug-Ins/Components"));
            paths.add(juce::File(juce::File::getSpecialLocation(juce::File::userHomeDirectory)
                .getChildFile("Library/Audio/Plug-Ins/Components")));
        }
        else
        {
            paths.add(juce::File("/Library/Audio/Plug-Ins/VST3"));
            paths.add(juce::File(juce::File::getSpecialLocation(juce::File::userHomeDirectory)
                .getChildFile("Library/Audio/Plug-Ins/VST3")));
        }
        juce::PluginDirectoryScanner scanner(knownPlugins, *format, paths, true, juce::File(), false);
        juce::String name;
        while (scanner.scanNextFile(true, name)) {}
    }
    descriptions.clear();
    for (const auto& d : knownPlugins.getTypes())
        descriptions.add(new juce::PluginDescription(d));
}

void KaraokeAudioEngine::start()
{
    auto error = deviceManager.initialise(2, 2, nullptr, true);
    if (error.isNotEmpty()) { status = error; return; }
    deviceManager.addAudioCallback(this);
    status = "Audio active";
}

void KaraokeAudioEngine::stop()
{
    deviceManager.removeAudioCallback(this);
    deviceManager.closeAudioDevice();
    status = "Stopped";
}

juce::String KaraokeAudioEngine::getStatus() const
{
    if (auto* dev = deviceManager.getCurrentAudioDevice())
        return status + " · " + dev->getName() + " · " + juce::String(dev->getCurrentSampleRate(), 0) + " Hz";
    return status;
}

juce::StringArray KaraokeAudioEngine::getAvailablePluginNames() const
{
    juce::StringArray result;
    for (auto* d : descriptions)
        result.add(d->name + " [" + d->pluginFormatName + "]");
    result.sort(true);
    return result;
}

bool KaraokeAudioEngine::addPluginToChannel(int channel, const juce::String& pluginName, juce::String& error)
{
    if (channel < 0 || channel > 1) { error = "Invalid channel"; return false; }
    const auto shortName = pluginName.upToFirstOccurrenceOf(" [", false, false);
    const juce::PluginDescription* chosen = nullptr;
    for (auto* d : descriptions)
        if (d->name == shortName) { chosen = d; break; }
    if (chosen == nullptr) { error = "Plugin not found. Rescan/restart after installing AU or VST3."; return false; }

    juce::String creationError;
    std::unique_ptr<juce::AudioPluginInstance> plugin(formats.createPluginInstance(*chosen, sampleRate, blockSize, creationError));
    if (!plugin) { error = creationError.isNotEmpty() ? creationError : "Could not instantiate plugin."; return false; }

    const juce::ScopedLock sl(pluginLock);
    plugin->setPlayConfigDetails(2, 2, sampleRate, blockSize);
    plugin->prepareToPlay(sampleRate, blockSize);
    chains[channel].push_back({ chosen->name, std::move(plugin) });
    return true;
}

void KaraokeAudioEngine::clearChannel(int channel)
{
    if (channel < 0 || channel > 1) return;
    const juce::ScopedLock sl(pluginLock);
    for (auto& p : chains[channel]) if (p.instance) p.instance->releaseResources();
    chains[channel].clear();
}

void KaraokeAudioEngine::setChannelGain(int channel, float gain)
{
    if (channel >= 0 && channel < 2) channelGain[channel] = juce::jlimit(0.0f, 1.5f, gain);
}
void KaraokeAudioEngine::setMasterGain(float gain) { masterGain = juce::jlimit(0.0f, 1.0f, gain); }
float KaraokeAudioEngine::getChannelPeak(int channel) const { return channelPeak[channel].load(); }
float KaraokeAudioEngine::getMasterPeak() const { return masterPeak.load(); }

void KaraokeAudioEngine::audioDeviceAboutToStart(juce::AudioIODevice* device)
{
    if (device != nullptr) { sampleRate = device->getCurrentSampleRate(); blockSize = device->getCurrentBufferSizeSamples(); }
    const juce::ScopedLock sl(pluginLock);
    for (auto& chain : chains)
        for (auto& p : chain)
            if (p.instance) { p.instance->releaseResources(); p.instance->prepareToPlay(sampleRate, blockSize); }
}
void KaraokeAudioEngine::audioDeviceStopped()
{
    const juce::ScopedLock sl(pluginLock);
    for (auto& chain : chains) for (auto& p : chain) if (p.instance) p.instance->releaseResources();
}

void KaraokeAudioEngine::audioDeviceIOCallbackWithContext(const float* const* in, int numIn,
    float* const* out, int numOut, int n, const juce::AudioIODeviceCallbackContext&)
{
    juce::AudioBuffer<float> mixed(out, numOut, n);
    mixed.clear();
    juce::AudioBuffer<float> temp(2, n);
    float peaks[2] = { 0.0f, 0.0f };

    const juce::ScopedTryLock sl(pluginLock);
    if (!sl.isLocked()) return;

    for (int ch = 0; ch < 2; ++ch)
    {
        temp.clear();
        if (ch < numIn && in[ch] != nullptr)
            temp.copyFrom(0, 0, in[ch], n);
        // duplicate mono mic to stereo for conventional vocal plugins
        temp.copyFrom(1, 0, temp, 0, 0, n);
        for (auto& p : chains[ch])
            if (p.instance) p.instance->processBlock(temp, midiBuffer);
        temp.applyGain(channelGain[ch]);
        peaks[ch] = temp.getMagnitude(0, 0, n);
        if (numOut > 0) mixed.addFrom(0, 0, temp, 0, 0, n);
        if (numOut > 1) mixed.addFrom(1, 0, temp, 1, 0, n);
    }
    mixed.applyGain(masterGain);
    float blockPeak = 0.0f;
    for (int c = 0; c < numOut; ++c)
        blockPeak = juce::jmax(blockPeak, mixed.getMagnitude(c, 0, n));
    masterPeak.store(blockPeak);
    channelPeak[0].store(peaks[0]); channelPeak[1].store(peaks[1]);
}
