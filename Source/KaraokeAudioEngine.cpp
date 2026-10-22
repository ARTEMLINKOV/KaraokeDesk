#include "KaraokeAudioEngine.h"
#include <cmath>

KaraokeAudioEngine::KaraokeAudioEngine()
{
    // Register only plugin formats enabled by this target's build configuration.
#if JUCE_PLUGINHOST_VST3
    formats.addFormat(std::make_unique<juce::VST3PluginFormat>());
#endif
#if JUCE_PLUGINHOST_AU
    formats.addFormat(std::make_unique<juce::AudioUnitPluginFormat>());
#endif
    // Plugin discovery is deferred until the user starts it from Settings.
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

    for (const auto& description : knownPlugins.getTypes())
        descriptions.add(new juce::PluginDescription(description));
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

    auto* device = deviceManager.getCurrentAudioDevice();
    const double rate = device != nullptr ? device->getCurrentSampleRate() : sampleRate;
    const int block = device != nullptr
        ? juce::jmax(1, device->getCurrentBufferSizeSamples())
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

    // Allocate buffers before the real-time callback starts. Larger callbacks
    // are processed in chunks, so the audio thread never needs to resize them.
    processingCapacity = juce::jmax(blockSize, 8192);
    for (auto& buffer : processingBuffers)
        buffer.setSize(2, processingCapacity, false, true, true);

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

    const int availableInputs = juce::jmin(2, numInputChannels);
    for (int channel = 0; channel < 2; ++channel)
    {
        float peak = 0.0f;
        if (channel < availableInputs && inputChannelData[channel] != nullptr)
        {
            const auto range = juce::FloatVectorOperations::findMinAndMax(
                inputChannelData[channel], numSamples);
            peak = juce::jmax(std::abs(range.getStart()), std::abs(range.getEnd()));
        }
        channelPeak[channel].store(peak);
    }

    // The audio callback never waits for the UI thread. While a plugin chain
    // is being edited, this block passes through without effects.
    const bool havePluginLock = pluginLock.tryEnter();
    const float master = masterGain.load();

    int offset = 0;
    while (offset < numSamples)
    {
        const int chunk = juce::jmin(processingCapacity, numSamples - offset);

        for (int inputChannel = 0; inputChannel < 2; ++inputChannel)
        {
            auto& buffer = processingBuffers[inputChannel];
            buffer.setSize(2, chunk, false, false, true);

            const bool hasInput = inputChannel < numInputChannels
                && inputChannelData[inputChannel] != nullptr;

            if (hasInput)
                buffer.copyFrom(0, 0, inputChannelData[inputChannel] + offset, chunk);
            else
                buffer.clear(0, 0, chunk);

            // Host mono microphones through stereo effects by duplicating input.
            juce::FloatVectorOperations::copy(
                buffer.getWritePointer(1), buffer.getReadPointer(0), chunk);
            buffer.applyGain(channelGain[inputChannel].load());

            if (havePluginLock)
            {
                for (auto& slot : chains[inputChannel])
                {
                    if (slot.instance != nullptr)
                    {
                        midiBuffer.clear();
                        slot.instance->processBlock(buffer, midiBuffer);
                    }
                }
            }

            // Mix both microphone chains into the stereo master bus. A mono
            // output receives a safe average of the processed stereo signal.
            if (numOutputChannels == 1 && outputChannelData[0] != nullptr)
            {
                auto* destination = outputChannelData[0] + offset;
                const auto* left = buffer.getReadPointer(0);
                const auto* right = buffer.getReadPointer(1);
                for (int sample = 0; sample < chunk; ++sample)
                    destination[sample] += 0.5f * (left[sample] + right[sample]) * master;
            }
            else
            {
                if (numOutputChannels > 0 && outputChannelData[0] != nullptr)
                    juce::FloatVectorOperations::addWithMultiply(
                        outputChannelData[0] + offset, buffer.getReadPointer(0), master, chunk);

                if (numOutputChannels > 1 && outputChannelData[1] != nullptr)
                    juce::FloatVectorOperations::addWithMultiply(
                        outputChannelData[1] + offset, buffer.getReadPointer(1), master, chunk);
            }
        }

        offset += chunk;
    }

    if (havePluginLock)
        pluginLock.exit();

    float outputPeak = 0.0f;
    for (int channel = 0; channel < numOutputChannels; ++channel)
    {
        if (outputChannelData[channel] != nullptr)
        {
            const auto range = juce::FloatVectorOperations::findMinAndMax(
                outputChannelData[channel], numSamples);
            outputPeak = juce::jmax(outputPeak,
                juce::jmax(std::abs(range.getStart()), std::abs(range.getEnd())));
        }
    }
    masterPeak.store(outputPeak);
}
