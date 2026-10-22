#include "KaraokeAudioEngine.h"

KaraokeAudioEngine::KaraokeAudioEngine()
{
    formats.addDefaultFormats();
}

KaraokeAudioEngine::~KaraokeAudioEngine()
{
    stop();
}

void KaraokeAudioEngine::start()
{
    auto error = deviceManager.initialise(
        2,
        2,
        nullptr,
        true
    );

    if (error.isNotEmpty())
    {
        status = "Ошибка аудиоустройства: " + error;
        juce::Logger::writeToLog(status);
        return;
    }

    deviceManager.addAudioCallback(this);
    scanPlugins();
    status = "Аудио запущено";
}

void KaraokeAudioEngine::stop()
{
    deviceManager.removeAudioCallback(this);
    deviceManager.closeAudioDevice();
    status = "Аудио остановлено";
}

juce::String KaraokeAudioEngine::getStatus() const
{
    return status;
}

void KaraokeAudioEngine::scanPlugins()
{
    juce::ScopedLock lock(pluginLock);

    knownPlugins.clear();
    descriptions.clear();

    formats.addDefaultFormats();

    for (int i = 0; i < formats.getNumFormats(); ++i)
    {
        auto* format = formats.getFormat(i);

        if (format == nullptr)
            continue;

        juce::OwnedArray<juce::PluginDescription> found;

        format->findAllTypesForFile(
            found,
            format->getDefaultLocationsToSearch().getFile()
        );

        for (auto* description : found)
        {
            if (description != nullptr)
            {
                knownPlugins.addType(*description);
                descriptions.add(new juce::PluginDescription(*description));
            }
        }
    }
}

juce::StringArray KaraokeAudioEngine::getAvailablePluginNames() const
{
    juce::StringArray names;

    juce::ScopedLock lock(pluginLock);

    for (auto* description : descriptions)
    {
        if (description != nullptr)
            names.addIfNotAlreadyThere(description->name);
    }

    names.sort(true);
    return names;
}

bool KaraokeAudioEngine::addPluginToChannel(
    int channel,
    const juce::String& pluginName,
    juce::String& error)
{
    if (channel < 0 || channel >= 2)
    {
        error = "Недопустимый номер канала.";
        return false;
    }

    juce::PluginDescription selectedDescription;
    bool found = false;

    {
        juce::ScopedLock lock(pluginLock);

        for (auto* description : descriptions)
        {
            if (description != nullptr && description->name == pluginName)
            {
                selectedDescription = *description;
                found = true;
                break;
            }
        }
    }

    if (!found)
    {
        error = "Плагин не найден: " + pluginName;
        return false;
    }

    juce::String creationError;

    auto instance = formats.createPluginInstance(
        selectedDescription,
        sampleRate,
        blockSize,
        creationError
    );

    if (instance == nullptr)
    {
        error = creationError.isNotEmpty()
            ? creationError
            : "Не удалось загрузить плагин.";
        return false;
    }

    instance->setPlayConfigDetails(2, 2, sampleRate, blockSize);
    instance->prepareToPlay(sampleRate, blockSize);

    {
        juce::ScopedLock lock(pluginLock);

        PluginSlot slot;
        slot.name = pluginName;
        slot.instance = std::move(instance);
        chains[channel].push_back(std::move(slot));
    }

    error.clear();
    return true;
}

void KaraokeAudioEngine::clearChannel(int channel)
{
    if (channel < 0 || channel >= 2)
        return;

    juce::ScopedLock lock(pluginLock);
    chains[channel].clear();
}

void KaraokeAudioEngine::setChannelGain(int channel, float gain)
{
    if (channel < 0 || channel >= 2)
        return;

    channelGain[channel] = juce::jlimit(0.0f, 2.0f, gain);
}

void KaraokeAudioEngine::setMasterGain(float gain)
{
    masterGain = juce::jlimit(0.0f, 2.0f, gain);
}

float KaraokeAudioEngine::getChannelPeak(int channel) const
{
    if (channel < 0 || channel >= 2)
        return 0.0f;

    return channelPeak[channel].load();
}

float KaraokeAudioEngine::getMasterPeak() const
{
    return masterPeak.load();
}

void KaraokeAudioEngine::audioDeviceAboutToStart(juce::AudioIODevice* device)
{
    if (device != nullptr)
    {
        sampleRate = device->getCurrentSampleRate();
        blockSize = device->getCurrentBufferSizeSamples();
    }

    juce::ScopedLock lock(pluginLock);

    for (auto& chain : chains)
    {
        for (auto& slot : chain)
        {
            if (slot.instance != nullptr)
            {
                slot.instance->setPlayConfigDetails(2, 2, sampleRate, blockSize);
                slot.instance->prepareToPlay(sampleRate, blockSize);
            }
        }
    }
}

void KaraokeAudioEngine::audioDeviceStopped()
{
    juce::ScopedLock lock(pluginLock);

    for (auto& chain : chains)
    {
        for (auto& slot : chain)
        {
            if (slot.instance != nullptr)
                slot.instance->releaseResources();
        }
    }
}

void KaraokeAudioEngine::audioDeviceIOCallbackWithContext(
    const float* const* inputChannelData,
    int numInputChannels,
    float* const* outputChannelData,
    int numOutputChannels,
    int numSamples,
    const juce::AudioIODeviceCallbackContext& context)
{
    juce::ignoreUnused(context);

    juce::AudioBuffer<float> buffer(
        const_cast<float**>(inputChannelData),
        juce::jmax(1, numInputChannels),
        numSamples
    );

    buffer.applyGain(masterGain);

    float peak = 0.0f;

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        peak = juce::jmax(peak, buffer.getMagnitude(channel, 0, numSamples));

    masterPeak.store(peak);

    for (int channel = 0; channel < numOutputChannels; ++channel)
    {
        if (outputChannelData[channel] == nullptr)
            continue;

        if (channel < numInputChannels && inputChannelData[channel] != nullptr)
            juce::FloatVectorOperations::copy(
                outputChannelData[channel],
                inputChannelData[channel],
                numSamples
            );
        else
            juce::FloatVectorOperations::clear(outputChannelData[channel], numSamples);
    }
}
