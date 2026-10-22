#include "KaraokeAudioEngine.h"
#include <cmath>

KaraokeAudioEngine::KaraokeAudioEngine()
{
    // JUCE registers only the plugin formats enabled by the build configuration.
    // CMake enables VST3 everywhere and Audio Units on macOS.
    formats.addDefaultFormats();
    scanPlugins();
}

KaraokeAudioEngine::~KaraokeAudioEngine()
{
    stop();
}

void KaraokeAudioEngine::start()
{
    if (deviceManager.getCurrentAudioDevice() != nullptr)
    {
        status = "Аудио запущено";
        return;
    }

    auto error = deviceManager.initialise(2, 2, nullptr, true);

    if (error.isNotEmpty())
    {
        status = "Ошибка аудиоустройства: " + error;
        juce::Logger::writeToLog(status);
        return;
    }

    deviceManager.addAudioCallback(this);
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

    for (int i = 0; i < formats.getNumFormats(); ++i)
    {
        auto* format = formats.getFormat(i);
        if (format == nullptr)
            continue;

        juce::PluginDirectoryScanner scanner(
            knownPlugins,
            *format,
            format->getDefaultLocationsToSearch(),
            true,
            juce::File(),
            false);

        juce::String pluginBeingScanned;
        while (scanner.scanNextFile(true, pluginBeingScanned))
        {
        }
    }

    for (int i = 0; i < knownPlugins.getNumTypes(); ++i)
        descriptions.add(new juce::PluginDescription(*knownPlugins.getType(i)));
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

    const double rate = deviceManager.getCurrentAudioDevice() != nullptr
        ? deviceManager.getCurrentAudioDevice()->getCurrentSampleRate()
        : sampleRate;
    const int block = deviceManager.getCurrentAudioDevice() != nullptr
        ? deviceManager.getCurrentAudioDevice()->getCurrentBufferSizeSamples()
        : blockSize;

    juce::String creationError;
    auto instance = formats.createPluginInstance(
        selectedDescription, rate, block, creationError);

    if (instance == nullptr)
    {
        error = creationError.isNotEmpty()
            ? creationError
            : "Не удалось загрузить плагин.";
        return false;
    }

    instance->setPlayConfigDetails(2, 2, rate, block);
    instance->prepareToPlay(rate, block);

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

    channelGain[channel].store(juce::jlimit(0.0f, 2.0f, gain));
}

void KaraokeAudioEngine::setMasterGain(float gain)
{
    masterGain.store(juce::jlimit(0.0f, 2.0f, gain));
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
        blockSize = juce::jmax(1, device->getCurrentBufferSizeSamples());
    }

    // Allocate audio buffers before the real-time callback starts.
    processingCapacity = juce::jmax(blockSize, 8192);
    for (auto& buffer : processingBuffers)
        buffer.setSize(2, capacity, false, true, true);

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

    for (int channel = 0; channel < numOutputChannels; ++channel)
        if (outputChannelData[channel] != nullptr)
            juce::FloatVectorOperations::clear(outputChannelData[channel], numSamples);

    if (numSamples <= 0)
        return;

    float inputPeaks[2] { 0.0f, 0.0f };
    for (int channel = 0; channel < juce::jmin(2, numInputChannels); ++channel)
    {
        if (inputChannelData[channel] != nullptr)
            const auto range = juce::FloatVectorOperations::findMinAndMax(
                inputChannelData[channel], numSamples);
            inputPeaks[channel] = juce::jmax(std::abs(range.getStart()),
                                             std::abs(range.getEnd()));
        channelPeak[channel].store(inputPeaks[channel]);
    }
    for (int channel = juce::jmin(2, numInputChannels); channel < 2; ++channel)
        channelPeak[channel].store(0.0f);

    // Never wait on the UI thread from the audio callback. If a plugin is being
    // added/removed, pass the signal through cleanly for this block.
    const bool havePluginLock = pluginLock.tryEnter();
    const float master = masterGain.load();
    float outputPeak = 0.0f;

    int offset = 0;
    while (offset < numSamples)
    {
        const int chunk = juce::jmin(processingCapacity, numSamples - offset);

        for (int channel = 0; channel < 2; ++channel)
        {
            auto& buffer = processingBuffers[channel];
            // Change the active sample count without reallocating the preallocated storage.
            buffer.setSize(2, chunk, false, false, true);
            const bool hasInput = channel < numInputChannels
                && inputChannelData[channel] != nullptr;

            if (hasInput)
                buffer.copyFrom(0, 0, inputChannelData[channel] + offset, chunk);
            else
                buffer.clear(0, 0, chunk);

            // Each mic has an independent effect chain. Duplicate mono input to
            // a stereo plugin buffer so stereo-only VST3/AU effects can be hosted.
            juce::FloatVectorOperations::copy(
                buffer.getWritePointer(1), buffer.getReadPointer(0), chunk);
            buffer.applyGain(channelGain[channel].load());

            if (havePluginLock)
            {
                for (auto& slot : chains[channel])
                {
                    if (slot.instance != nullptr)
                    {
                        midiBuffer.clear();
                        slot.instance->processBlock(buffer, midiBuffer);
                    }
                }
            }

            if (channel < numOutputChannels && outputChannelData[channel] != nullptr)
            {
                auto* destination = outputChannelData[channel] + offset;
                const auto* source = buffer.getReadPointer(0);
                juce::FloatVectorOperations::copy(destination, source, chunk);
                juce::FloatVectorOperations::multiply(destination, master, chunk);
                const auto range = juce::FloatVectorOperations::findMinAndMax(destination, chunk);
                outputPeak = juce::jmax(outputPeak,
                    juce::jmax(std::abs(range.getStart()), std::abs(range.getEnd())));
            }
        }

        offset += chunk;
    }

    if (havePluginLock)
        pluginLock.exit();

    masterPeak.store(outputPeak);
}
