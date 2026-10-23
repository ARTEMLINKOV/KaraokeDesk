#include "KaraokeAudioEngine.h"
#include <cmath>

namespace
{
    void addEnabledPluginFormats(juce::AudioPluginFormatManager& manager)
    {
#if JUCE_PLUGINHOST_VST3
        manager.addFormat(std::make_unique<juce::VST3PluginFormat>());
#endif
#if JUCE_PLUGINHOST_AU
        manager.addFormat(std::make_unique<juce::AudioUnitPluginFormat>());
#endif
    }

    juce::StringArray findPluginBundles(const juce::FileSearchPath& selectedPaths,
                                        const juce::String& extension,
                                        const std::function<void(const juce::String&, float)>& progress)
    {
        juce::StringArray candidates;
        juce::Array<juce::File> pendingDirectories;

        for (int index = 0; index < selectedPaths.getNumPaths(); ++index)
        {
            const auto path = selectedPaths[index];
            if (path.isDirectory())
            {
                if (path.getFileExtension().equalsIgnoreCase(extension))
                    candidates.addIfNotAlreadyThere(path.getFullPathName());
                else
                {
                    pendingDirectories.add(path);
                    if (progress)
                        progress(path.getFullPathName(), 0.0f);
                }
            }
            else if (path.getFileExtension().equalsIgnoreCase(extension))
                candidates.addIfNotAlreadyThere(path.getFullPathName());
        }

        juce::StringArray visitedDirectories;
        while (!pendingDirectories.isEmpty())
        {
            const auto directory = pendingDirectories.removeAndReturn(pendingDirectories.size() - 1);
            const auto fullPath = directory.getFullPathName();
            if (visitedDirectories.contains(fullPath))
                continue;
            visitedDirectories.add(fullPath);
            if (progress)
                progress(fullPath, 0.0f);

            for (const auto& child : directory.findChildFiles(
                     juce::File::findDirectories, false, "*", juce::File::FollowSymlinks::no))
            {
                if (child.getFileExtension().equalsIgnoreCase(extension))
                    candidates.addIfNotAlreadyThere(child.getFullPathName());
                else
                    pendingDirectories.add(child);
            }
        }

        candidates.sort(true);
        return candidates;
    }
}

KaraokeAudioEngine::KaraokeAudioEngine()
{
    settingsFile = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("KaraokeDesk")
        .getChildFile("settings.xml");
    pluginCacheFile = settingsFile.getSiblingFile("plugins.xml");
    const auto savedSettings = juce::XmlDocument::parse(settingsFile);
    if (savedSettings != nullptr && savedSettings->hasTagName("KaraokeDeskSettings"))
    {
        if (auto* audioState = savedSettings->getChildByName("DEVICESETUP"))
            deviceManager.initialise(2, 2, audioState, true);

        if (auto* directories = savedSettings->getChildByName("PluginDirectories"))
        {
            for (auto* directory = directories->getFirstChildElement();
                 directory != nullptr; directory = directory->getNextElement())
            {
                if (directory->hasTagName("Directory"))
                {
                    const juce::File folder(directory->getStringAttribute("path"));
                    if (folder.isDirectory())
                        pluginSearchPaths.add(folder);
                }
            }

            accentColour = juce::Colour::fromString(
                savedSettings->getStringAttribute("accentColour", "ff2584c7"));
        }
    }

    addEnabledPluginFormats(formats);
    if (auto cachedXml = juce::XmlDocument::parse(pluginCacheFile))
    {
        juce::KnownPluginList cachedPlugins;
        cachedPlugins.recreateFromXml(*cachedXml);
        for (const auto& description : cachedPlugins.getTypes())
        {
            const juce::File pluginFile(description.fileOrIdentifier);
            if (pluginFile.exists() && pluginSearchPaths.isFileInPath(pluginFile, true))
                descriptions.add(new juce::PluginDescription(description));
        }
    }
    recordingThread.startThread();
    // Plugin descriptions are restored from cache; plugin binaries are loaded on demand.
}

void KaraokeAudioEngine::saveSettings()
{
    auto settings = std::make_unique<juce::XmlElement>("KaraokeDeskSettings");
    settings->setAttribute("accentColour", accentColour.toString());
    if (auto audioState = deviceManager.createStateXml())
        settings->addChildElement(audioState.release());

    auto* directories = settings->createNewChildElement("PluginDirectories");
    for (int i = 0; i < pluginSearchPaths.getNumPaths(); ++i)
        directories->createNewChildElement("Directory")
            ->setAttribute("path", pluginSearchPaths[i].getFullPathName());

    settingsFile.getParentDirectory().createDirectory();
    if (!settings->writeTo(settingsFile))
        juce::Logger::writeToLog("Failed to save KaraokeDesk settings to " + settingsFile.getFullPathName());
}

juce::FileSearchPath KaraokeAudioEngine::getPluginSearchPaths() const
{
    return pluginSearchPaths;
}

void KaraokeAudioEngine::setPluginSearchPaths(const juce::FileSearchPath& paths)
{
    pluginSearchPaths = paths;
    saveSettings();
}

juce::Colour KaraokeAudioEngine::getAccentColour() const
{
    return accentColour;
}

void KaraokeAudioEngine::setAccentColour(juce::Colour colour)
{
    accentColour = colour;
    saveSettings();
}

KaraokeAudioEngine::~KaraokeAudioEngine()
{
    stopRecording();
    stop();
    recordingThread.stopThread(2000);
}

void KaraokeAudioEngine::start()
{
    auto setup = deviceManager.getAudioDeviceSetup();
    if (setup.outputDeviceName.isEmpty())
    {
        auto* type = deviceManager.getCurrentDeviceTypeObject();
        if (type == nullptr)
        {
            auto& availableTypes = deviceManager.getAvailableDeviceTypes();
            if (!availableTypes.isEmpty())
            {
                deviceManager.setCurrentAudioDeviceType(availableTypes[0]->getTypeName(), false);
                type = deviceManager.getCurrentDeviceTypeObject();
            }
        }

        if (type != nullptr)
        {
            type->scanForDevices();
            const auto outputNames = type->getDeviceNames(false);
            if (!outputNames.isEmpty())
            {
                const int outputIndex = juce::jlimit(
                    0, outputNames.size() - 1, type->getDefaultDeviceIndex(false));
                setup.outputDeviceName = outputNames[outputIndex];
                setup.useDefaultOutputChannels = true;
                const auto error = deviceManager.setAudioDeviceSetup(setup, true);
                if (error.isNotEmpty())
                {
                    status = juce::String::fromUTF8("Ошибка основного выхода: ") + error;
                    juce::Logger::writeToLog(status);
                    return;
                }
                saveSettings();
            }
        }
    }

    if (deviceManager.getCurrentAudioDevice() == nullptr)
    {
        auto error = deviceManager.initialise(2, 2, nullptr, true);

        if (error.isNotEmpty())
        {
            status = juce::String::fromUTF8("Ошибка аудиоустройства: ") + error;
            juce::Logger::writeToLog(status);
            return;
        }
    }

    if (!audioCallbackRegistered)
    {
        deviceManager.addAudioCallback(this);
        audioCallbackRegistered = true;
    }
    status = juce::String::fromUTF8("Аудио запущено");
}

void KaraokeAudioEngine::stop()
{
    if (audioCallbackRegistered)
    {
        deviceManager.removeAudioCallback(this);
        audioCallbackRegistered = false;
    }
    deviceManager.closeAudioDevice();
    status = juce::String::fromUTF8("Аудио остановлено");
}

juce::String KaraokeAudioEngine::getStatus() const
{
    return status;
}

int KaraokeAudioEngine::scanPlugins(
    const juce::FileSearchPath& selectedPaths,
    std::function<void(const juce::String&, float)> progress,
    std::function<bool()> shouldSkipCurrent)
{
    juce::KnownPluginList cachedPlugins;
    {
        const juce::ScopedLock lock(pluginLock);
        for (auto* description : descriptions)
            if (description != nullptr)
                cachedPlugins.addType(*description);
    }

    juce::StringArray candidatesByFormat[2];
    juce::AudioPluginFormat* scanFormats[2] {};
    int scanFormatCount = 0;

    for (int i = 0; i < formats.getNumFormats(); ++i)
    {
        auto* format = formats.getFormat(i);
        if (format == nullptr)
            continue;

        const auto formatName = format->getName();
        juce::StringArray candidates;
        if (formatName.equalsIgnoreCase("AudioUnit"))
            candidates = findPluginBundles(selectedPaths, ".component", progress);
        else if (formatName.equalsIgnoreCase("VST3"))
            candidates = findPluginBundles(selectedPaths, ".vst3", progress);
        else
        {
            candidates = format->searchPathsForPlugins(selectedPaths, true, false);
        }

        auto& formatCandidates = candidatesByFormat[scanFormatCount];
        for (const auto& candidate : candidates)
            if (format->fileMightContainThisPluginType(candidate))
                formatCandidates.addIfNotAlreadyThere(candidate);
        scanFormats[scanFormatCount++] = format;
    }

    int totalCandidates = 0;
    for (int i = 0; i < scanFormatCount; ++i)
        totalCandidates += candidatesByFormat[i].size();

    int scanned = 0;
    for (int formatIndex = 0; formatIndex < scanFormatCount; ++formatIndex)
    {
        for (const auto& candidate : candidatesByFormat[formatIndex])
        {
            auto* format = scanFormats[formatIndex];
            if (format != nullptr && cachedPlugins.isListingUpToDate(candidate, *format))
            {
                if (progress)
                    progress(juce::String::fromUTF8("Уже проверен: ") + candidate,
                        totalCandidates > 0
                            ? static_cast<float>(scanned + 1) / static_cast<float>(totalCandidates)
                            : 1.0f);
                ++scanned;
                continue;
            }

            if (progress)
                progress(candidate, totalCandidates > 0
                    ? static_cast<float>(scanned) / static_cast<float>(totalCandidates)
                    : 1.0f);

            if (shouldSkipCurrent && shouldSkipCurrent())
            {
                if (progress)
                    progress(juce::String::fromUTF8("Пропущен: ") + candidate,
                        totalCandidates > 0
                            ? static_cast<float>(scanned + 1) / static_cast<float>(totalCandidates)
                            : 1.0f);
                ++scanned;
                continue;
            }

            const auto resultFile = juce::File::getSpecialLocation(
                juce::File::tempDirectory).getChildFile(
                    "KaraokeDeskPluginScan-" + juce::Uuid().toString() + ".xml");
            juce::ChildProcess scanner;
            juce::StringArray arguments {
                juce::File::getSpecialLocation(juce::File::currentExecutableFile).getFullPathName(),
                "--karaokedesk-scan-plugin",
                candidate,
                resultFile.getFullPathName()
            };
            bool skipped = false;
            if (scanner.start(arguments, 0))
            {
                while (!scanner.waitForProcessToFinish(20))
                {
                    if (shouldSkipCurrent && shouldSkipCurrent() && scanner.kill())
                    {
                        scanner.waitForProcessToFinish(2000);
                        skipped = true;
                        break;
                    }
                }
            }

            if (skipped)
            {
                if (progress)
                    progress(juce::String::fromUTF8("Пропущен: ") + candidate,
                        totalCandidates > 0
                            ? static_cast<float>(scanned + 1) / static_cast<float>(totalCandidates)
                            : 1.0f);
            }
            else if (auto xml = juce::XmlDocument::parse(resultFile))
            {
                juce::KnownPluginList resultList;
                resultList.recreateFromXml(*xml);
                for (const auto& description : resultList.getTypes())
                    cachedPlugins.addType(description);
            }
            else if (progress)
                progress(juce::String::fromUTF8("Ошибка сканирования: ") + candidate,
                         totalCandidates > 0
                             ? static_cast<float>(scanned + 1) / static_cast<float>(totalCandidates)
                             : 1.0f);
            resultFile.deleteFile();
            ++scanned;
        }
    }
    juce::OwnedArray<juce::PluginDescription> discoveredDescriptions;
    for (const auto& description : cachedPlugins.getTypes())
    {
        const juce::File pluginFile(description.fileOrIdentifier);
        if (pluginFile.exists() && selectedPaths.isFileInPath(pluginFile, true))
            discoveredDescriptions.add(new juce::PluginDescription(description));
    }

    {
        juce::ScopedLock lock(pluginLock);
        descriptions.clear();
        for (auto* description : discoveredDescriptions)
            if (description != nullptr)
                descriptions.add(new juce::PluginDescription(*description));
    }

    if (auto xml = cachedPlugins.createXml())
    {
        pluginCacheFile.getParentDirectory().createDirectory();
        if (!xml->writeTo(pluginCacheFile))
            juce::Logger::writeToLog("Failed to save scanned plugin cache to "
                                     + pluginCacheFile.getFullPathName());
    }

    if (progress)
        progress({}, 1.0f);

    return discoveredDescriptions.size();
}

void KaraokeAudioEngine::scanPluginInHelper(
    const juce::String& pluginPath, const juce::String& resultPath)
{
    juce::AudioPluginFormatManager pluginFormats;
    addEnabledPluginFormats(pluginFormats);

    juce::KnownPluginList pluginList;
    juce::OwnedArray<juce::PluginDescription> foundDescriptions;
    for (int index = 0; index < pluginFormats.getNumFormats(); ++index)
    {
        auto* format = pluginFormats.getFormat(index);
        if (format != nullptr && format->fileMightContainThisPluginType(pluginPath))
        {
            pluginList.scanAndAddFile(pluginPath, false, foundDescriptions, *format);
            break;
        }
    }

    for (auto* description : foundDescriptions)
        if (description != nullptr)
            pluginList.addType(*description);

    if (auto xml = pluginList.createXml())
        xml->writeTo(juce::File(resultPath));
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
    if (channel < 0 || channel >= 5)
    {
        error = juce::String::fromUTF8("Недопустимый номер канала.");
        return false;
    }

    int slot = 0;
    {
        juce::ScopedLock lock(pluginLock);
        while (slot < 4 && slot < static_cast<int>(chains[channel].size())
               && chains[channel][static_cast<size_t>(slot)].instance != nullptr)
            ++slot;
    }

    if (slot >= 4)
    {
        error = juce::String::fromUTF8("Все слоты эффектов заняты.");
        return false;
    }

    return setPluginInChannelSlot(channel, slot, pluginName, error);
}

bool KaraokeAudioEngine::setPluginInChannelSlot(
    int channel,
    int slot,
    const juce::String& pluginName,
    juce::String& error)
{
    if (channel < 0 || channel >= 5 || slot < 0 || slot >= 4)
    {
        error = juce::String::fromUTF8("Недопустимый слот эффекта.");
        return false;
    }

    if (pluginName.isEmpty())
    {
        juce::ScopedLock lock(pluginLock);
        if (slot < static_cast<int>(chains[channel].size()))
            chains[channel][static_cast<size_t>(slot)] = PluginSlot {};
        error.clear();
        return true;
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
        error = juce::String::fromUTF8("Плагин не найден: ") + pluginName;
        return false;
    }

    auto* device = deviceManager.getCurrentAudioDevice();
    const double rate = device != nullptr ? device->getCurrentSampleRate() : sampleRate;
    const int block = device != nullptr
        ? juce::jmax(1, device->getCurrentBufferSizeSamples())
        : blockSize;

    juce::String creationError;
    std::unique_ptr<juce::AudioPluginInstance> instance;
    {
        juce::ScopedLock lock(formatLock);
        instance = formats.createPluginInstance(selectedDescription, rate, block, creationError);
    }

    if (instance == nullptr)
    {
        error = creationError.isNotEmpty()
            ? creationError
            : juce::String::fromUTF8("Не удалось загрузить плагин.");
        return false;
    }

    instance->setPlayConfigDetails(2, 2, rate, block);
    instance->prepareToPlay(rate, block);

    juce::ScopedLock lock(pluginLock);
    auto& chain = chains[channel];
    if (static_cast<int>(chain.size()) <= slot)
        chain.resize(static_cast<size_t>(slot + 1));

    auto& selectedSlot = chain[static_cast<size_t>(slot)];
    selectedSlot.name = pluginName;
    selectedSlot.instance = std::move(instance);

    error.clear();
    return true;
}

juce::AudioProcessorEditor* KaraokeAudioEngine::getPluginEditor(int channel, int slot)
{
    if (channel < 0 || channel >= 5 || slot < 0 || slot >= 4)
        return nullptr;

    const juce::ScopedLock lock(pluginLock);
    auto& chain = chains[channel];
    if (slot >= static_cast<int>(chain.size()))
        return nullptr;

    auto* instance = chain[static_cast<size_t>(slot)].instance.get();
    if (instance == nullptr || !instance->hasEditor())
        return nullptr;
    return instance->createEditorAndMakeActive();
}

void KaraokeAudioEngine::clearChannel(int channel)
{
    if (channel < 0 || channel >= 5)
        return;

    juce::ScopedLock lock(pluginLock);
    chains[channel].clear();
}

void KaraokeAudioEngine::setChannelGain(int channel, float gain)
{
    if (channel < 0 || channel >= 5)
        return;

    channelGain[channel].store(juce::jlimit(0.0f, juce::Decibels::decibelsToGain(10.0f), gain));
}

void KaraokeAudioEngine::setMasterGain(float gain)
{
    const auto limited = juce::jlimit(0.0f, juce::Decibels::decibelsToGain(10.0f), gain);
    masterGain.store(limited);
    channelGain[4].store(limited);
}

void KaraokeAudioEngine::setChannelInput(int channel, int inputIndex)
{
    if (channel >= 0 && channel < 2 && inputIndex >= 0)
        channelInput[channel].store(inputIndex);
}

void KaraokeAudioEngine::setLoopbackInputChannels(int leftInputIndex, int rightInputIndex)
{
    const bool valid = leftInputIndex >= 0 && rightInputIndex >= 0
        && leftInputIndex != rightInputIndex;
    loopbackInputChannels[0].store(valid ? leftInputIndex : -1);
    loopbackInputChannels[1].store(valid ? rightInputIndex : -1);
}

void KaraokeAudioEngine::setActiveBrowserChannel(int channel)
{
    if (channel == 2 || channel == 3)
        activeBrowserChannel.store(channel);
}

bool KaraokeAudioEngine::hasLoopbackInput() const
{
    return loopbackInputChannels[0].load() >= 0
        && loopbackInputChannels[1].load() >= 0;
}

void KaraokeAudioEngine::setChannelMuted(int channel, bool muted)
{
    if (channel >= 0 && channel < 5)
        channelMuted[channel].store(muted);
}

void KaraokeAudioEngine::setChannelSolo(int channel, bool solo)
{
    if (channel >= 0 && channel < 5)
        channelSolo[channel].store(solo);
}

void KaraokeAudioEngine::setAppleMusicVocalReduction(bool enabled)
{
    appleMusicVocalReduction.store(enabled);
}

void KaraokeAudioEngine::pushYoutubeAudio(
    const float* const* inputChannelData, int numInputChannels, int numSamples)
{
    if (inputChannelData == nullptr || numInputChannels <= 0 || numSamples <= 0)
        return;

    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    youtubeFifo.prepareToWrite(numSamples, start1, size1, start2, size2);
    const auto copySegment = [&](int destinationStart, int segmentLength, int sourceOffset)
    {
        if (segmentLength <= 0)
            return;
        const int leftInput = inputChannelData[0] != nullptr ? 0 : -1;
        const int rightInput = numInputChannels > 1 && inputChannelData[1] != nullptr ? 1 : leftInput;
        if (leftInput < 0)
            youtubeFifoBuffer.clear(0, destinationStart, segmentLength);
        else
            youtubeFifoBuffer.copyFrom(0, destinationStart,
                inputChannelData[leftInput] + sourceOffset, segmentLength);
        if (rightInput < 0)
            youtubeFifoBuffer.clear(1, destinationStart, segmentLength);
        else
            youtubeFifoBuffer.copyFrom(1, destinationStart,
                inputChannelData[rightInput] + sourceOffset, segmentLength);
    };

    copySegment(start1, size1, 0);
    copySegment(start2, size2, size1);
    youtubeFifo.finishedWrite(size1 + size2);
}

void KaraokeAudioEngine::pushAppleMusicAudio(
    const float* const* inputChannelData, int numInputChannels, int numSamples)
{
    if (inputChannelData == nullptr || numInputChannels <= 0 || numSamples <= 0)
        return;

    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    appleMusicFifo.prepareToWrite(numSamples, start1, size1, start2, size2);
    const auto copySegment = [&](int destinationStart, int segmentLength, int sourceOffset)
    {
        if (segmentLength <= 0)
            return;

        const int rightInput = numInputChannels > 1 && inputChannelData[1] != nullptr ? 1 : 0;
        if (inputChannelData[0] != nullptr)
            appleMusicFifoBuffer.copyFrom(0, destinationStart,
                inputChannelData[0] + sourceOffset, segmentLength);
        else
            appleMusicFifoBuffer.clear(0, destinationStart, segmentLength);

        if (inputChannelData[rightInput] != nullptr)
            appleMusicFifoBuffer.copyFrom(1, destinationStart,
                inputChannelData[rightInput] + sourceOffset, segmentLength);
        else
            appleMusicFifoBuffer.clear(1, destinationStart, segmentLength);
    };

    copySegment(start1, size1, 0);
    copySegment(start2, size2, size1);
    appleMusicFifo.finishedWrite(size1 + size2);
}

bool KaraokeAudioEngine::startRecording(
    const juce::File& directory, bool mic1, bool mic2, bool recordMaster, juce::String& error)
{
    if (!mic1 && !mic2 && !recordMaster)
    {
        error = juce::String::fromUTF8("Выберите хотя бы один канал для записи.");
        return false;
    }

    if (recording.load())
    {
        error = juce::String::fromUTF8("Запись уже выполняется.");
        return false;
    }

    if (!directory.createDirectory())
    {
        error = juce::String::fromUTF8("Не удалось создать папку записи: ")
            + directory.getFullPathName();
        return false;
    }

    auto* device = deviceManager.getCurrentAudioDevice();
    if (device == nullptr)
    {
        error = juce::String::fromUTF8("Сначала запустите аудиоустройство.");
        return false;
    }

    juce::WavAudioFormat wavFormat;
    const auto sampleRateForFiles = device->getCurrentSampleRate();
    const auto timestamp = juce::Time::getCurrentTime().formatted("%Y-%m-%d_%H-%M-%S");
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> newMicrophoneRecorders[2];
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> newMasterRecorder;

    auto createWriter = [&](const juce::String& name, unsigned int channels)
        -> std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter>
    {
        const auto file = directory.getChildFile(name + "_" + timestamp + ".wav");
        std::unique_ptr<juce::OutputStream> output = file.createOutputStream();
        if (output == nullptr)
        {
            error = juce::String::fromUTF8("Не удалось создать файл: ") + file.getFullPathName();
            return {};
        }

        auto writer = wavFormat.createWriterFor(output,
            juce::AudioFormatWriterOptions()
                .withSampleRate(sampleRateForFiles)
                .withNumChannels(static_cast<int>(channels))
                .withBitsPerSample(24));
        if (writer == nullptr)
        {
            error = juce::String::fromUTF8("Не удалось подготовить WAV-файл: ")
                + file.getFullPathName();
            return {};
        }
        return std::make_unique<juce::AudioFormatWriter::ThreadedWriter>(
            writer.release(), recordingThread,
            juce::jmax(1, juce::roundToInt(sampleRateForFiles * 3.0)));
    };

    if (mic1 && !(newMicrophoneRecorders[0] = createWriter("Mic_1", 2)))
        return false;
    if (mic2 && !(newMicrophoneRecorders[1] = createWriter("Mic_2", 2)))
        return false;
    if (recordMaster && !(newMasterRecorder = createWriter("Master", 2)))
        return false;

    {
        juce::ScopedLock lock(recordingLock);
        for (int channel = 0; channel < 2; ++channel)
            microphoneRecorders[channel] = std::move(newMicrophoneRecorders[channel]);
        masterRecorder = std::move(newMasterRecorder);
        recordingDirectory = directory;
        recording.store(true);
    }

    error.clear();
    return true;
}

void KaraokeAudioEngine::stopRecording()
{
    recording.store(false);
    juce::ScopedLock lock(recordingLock);
    for (auto& recorder : microphoneRecorders)
        recorder.reset();
    masterRecorder.reset();
}

bool KaraokeAudioEngine::isRecording() const
{
    return recording.load();
}

juce::File KaraokeAudioEngine::getRecordingDirectory() const
{
    return recordingDirectory;
}

float KaraokeAudioEngine::getChannelPeak(int channel) const
{
    if (channel < 0 || channel >= 5)
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
    for (auto& peak : channelPeak)
        peak.store(0.0f);
    masterPeak.store(0.0f);

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

    const int inputIndices[2] = {
        channelInput[0].load(),
        channelInput[1].load()
    };
    const bool sourceSoloActive = channelSolo[0].load() || channelSolo[1].load()
        || channelSolo[2].load() || channelSolo[3].load();

    // The audio callback never waits for the UI thread. While a plugin chain
    // is being edited, this block passes through without effects.
    const bool havePluginLock = pluginLock.tryEnter();
    const bool haveRecordingLock = recording.load() && recordingLock.tryEnter();
    int offset = 0;
    while (offset < numSamples)
    {
        const int chunk = juce::jmin(juce::jmin(blockSize, processingCapacity),
                                     numSamples - offset);
        for (auto& buffer : processingBuffers)
            buffer.setSize(2, chunk, false, false, true);
        const float meterDecay = std::exp(-static_cast<float>(chunk)
            / static_cast<float>(juce::jmax(1.0, sampleRate * 0.24)));
        auto& masterBuffer = processingBuffers[4];
        masterBuffer.clear(0, 0, chunk);
        masterBuffer.clear(1, 0, chunk);

        for (int sourceChannel = 0; sourceChannel < 4; ++sourceChannel)
        {
            auto& buffer = processingBuffers[sourceChannel];

            if (sourceChannel < 2)
            {
                const int inputIndex = inputIndices[sourceChannel];
                const bool hasInput = inputIndex >= 0 && inputIndex < numInputChannels
                    && inputChannelData[inputIndex] != nullptr;
                if (hasInput)
                    buffer.copyFrom(0, 0, inputChannelData[inputIndex] + offset, chunk);
                else
                    buffer.clear(0, 0, chunk);
                juce::FloatVectorOperations::copy(
                    buffer.getWritePointer(1), buffer.getReadPointer(0), chunk);
            }
            else if (hasLoopbackInput())
            {
                const int leftInput = loopbackInputChannels[0].load();
                const int rightInput = loopbackInputChannels[1].load();
                const bool routeToThisChannel = activeBrowserChannel.load() == sourceChannel;
                const bool hasLoopback = routeToThisChannel
                    && leftInput < numInputChannels && rightInput < numInputChannels
                    && inputChannelData[leftInput] != nullptr
                    && inputChannelData[rightInput] != nullptr;
                if (hasLoopback)
                {
                    buffer.copyFrom(0, 0, inputChannelData[leftInput] + offset, chunk);
                    buffer.copyFrom(1, 0, inputChannelData[rightInput] + offset, chunk);
                }
                else
                    buffer.clear();
            }
            else
            {
                buffer.clear();
                int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
                auto& fifo = sourceChannel == 2 ? youtubeFifo : appleMusicFifo;
                auto& fifoBuffer = sourceChannel == 2 ? youtubeFifoBuffer : appleMusicFifoBuffer;
                fifo.prepareToRead(chunk, start1, size1, start2, size2);
                if (size1 > 0)
                    buffer.copyFrom(0, 0, fifoBuffer, 0, start1, size1);
                if (size2 > 0)
                    buffer.copyFrom(0, size1, fifoBuffer, 0, start2, size2);
                if (size1 > 0)
                    buffer.copyFrom(1, 0, fifoBuffer, 1, start1, size1);
                if (size2 > 0)
                    buffer.copyFrom(1, size1, fifoBuffer, 1, start2, size2);
                fifo.finishedRead(size1 + size2);

                if (sourceChannel == 3 && appleMusicVocalReduction.load())
                {
                    auto* left = buffer.getWritePointer(0);
                    auto* right = buffer.getWritePointer(1);
                    for (int sample = 0; sample < chunk; ++sample)
                    {
                        const auto side = 0.5f * (left[sample] - right[sample]);
                        left[sample] = side;
                        right[sample] = -side;
                    }
                }
            }

            const bool muted = channelMuted[sourceChannel].load()
                || (sourceSoloActive && !channelSolo[sourceChannel].load());
            if (muted)
                buffer.clear();
            else
                buffer.applyGain(channelGain[sourceChannel].load());

            if (havePluginLock)
            {
                for (auto& slot : chains[sourceChannel])
                {
                    if (slot.instance != nullptr)
                    {
                        midiBuffer.clear();
                        slot.instance->processBlock(buffer, midiBuffer);
                    }
                }
            }

            const auto rangeLeft = juce::FloatVectorOperations::findMinAndMax(
                buffer.getReadPointer(0), chunk);
            const auto rangeRight = juce::FloatVectorOperations::findMinAndMax(
                buffer.getReadPointer(1), chunk);
            const auto blockPeak = juce::jmax(
                juce::jmax(std::abs(rangeLeft.getStart()), std::abs(rangeLeft.getEnd())),
                juce::jmax(std::abs(rangeRight.getStart()), std::abs(rangeRight.getEnd())));
            const auto previousPeak = channelPeak[sourceChannel].load();
            channelPeak[sourceChannel].store(juce::jmax(blockPeak, previousPeak * meterDecay));

            if (haveRecordingLock && sourceChannel < 2
                && microphoneRecorders[sourceChannel] != nullptr)
            {
                const float* recordedChannels[] = {
                    buffer.getReadPointer(0), buffer.getReadPointer(1)
                };
                microphoneRecorders[sourceChannel]->write(recordedChannels, chunk);
            }

            juce::FloatVectorOperations::add(
                masterBuffer.getWritePointer(0), buffer.getReadPointer(0), chunk);
            juce::FloatVectorOperations::add(
                masterBuffer.getWritePointer(1), buffer.getReadPointer(1), chunk);
        }

        const bool masterMuted = channelMuted[4].load();
        if (masterMuted)
            masterBuffer.clear();
        if (havePluginLock)
        {
            for (auto& slot : chains[4])
            {
                if (slot.instance != nullptr)
                {
                    midiBuffer.clear();
                    slot.instance->processBlock(masterBuffer, midiBuffer);
                }
            }
        }
        masterBuffer.applyGain(channelGain[4].load());

        const auto masterLeft = juce::FloatVectorOperations::findMinAndMax(
            masterBuffer.getReadPointer(0), chunk);
        const auto masterRight = juce::FloatVectorOperations::findMinAndMax(
            masterBuffer.getReadPointer(1), chunk);
        const auto masterChannelPeak = juce::jmax(
            juce::jmax(std::abs(masterLeft.getStart()), std::abs(masterLeft.getEnd())),
            juce::jmax(std::abs(masterRight.getStart()), std::abs(masterRight.getEnd())));
        const auto previousMasterPeak = masterPeak.load();
        const auto masterMeterPeak = juce::jmax(masterChannelPeak,
                                                previousMasterPeak * meterDecay);
        channelPeak[4].store(masterMeterPeak);
        masterPeak.store(masterMeterPeak);

        if (haveRecordingLock && masterRecorder != nullptr)
        {
            const float* recordedChannels[] = {
                masterBuffer.getReadPointer(0), masterBuffer.getReadPointer(1)
            };
            masterRecorder->write(recordedChannels, chunk);
        }

        if (numOutputChannels == 1 && outputChannelData[0] != nullptr)
        {
            const auto* left = masterBuffer.getReadPointer(0);
            const auto* right = masterBuffer.getReadPointer(1);
            auto* destination = outputChannelData[0] + offset;
            for (int sample = 0; sample < chunk; ++sample)
                destination[sample] = 0.5f * (left[sample] + right[sample]);
        }
        else
        {
            if (numOutputChannels > 0 && outputChannelData[0] != nullptr)
                juce::FloatVectorOperations::copy(
                    outputChannelData[0] + offset, masterBuffer.getReadPointer(0), chunk);
            if (numOutputChannels > 1 && outputChannelData[1] != nullptr)
                juce::FloatVectorOperations::copy(
                    outputChannelData[1] + offset, masterBuffer.getReadPointer(1), chunk);
        }

        offset += chunk;
    }

    if (haveRecordingLock)
        recordingLock.exit();
    if (havePluginLock)
        pluginLock.exit();

}
