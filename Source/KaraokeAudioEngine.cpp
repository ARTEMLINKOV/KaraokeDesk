
#include "KaraokeAudioEngine.h"

KaraokeAudioEngine::KaraokeAudioEngine()
{
    // Регистрируем форматы плагинов, поддерживаемые JUCE.
    // VST3 доступен, если соответствующий модуль включён в сборку.
    formats.addFormat(std::make_unique<juce::AudioUnitPluginFormat>());
    formats.addFormat(std::make_unique<juce::VST3PluginFormat>());
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

    auto error = deviceManager.initialise(
        2,      // Входы: два микрофона
        2,      // Выходы: стерео
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
    knownPlugins.clear();

    for (int i = 0; i < formats.getNumFormats(); ++i)
    {
        auto* format = formats.getFormat(i);
        if (format == nullptr)
            continue;

        juce::FileSearchPath searchPath =
            format->getDefaultLocationsToSearch();

        juce::StringArray locations;
        searchPath.getNumPaths();

        for (int p = 0; p < searchPath.getNumPaths(); ++p)
        {
            auto directory = searchPath[p];
            if (directory.isDirectory())
                locations.add(directory.getFullPathName());
        }

        juce::PluginDirectoryScanner scanner(
            knownPlugins,
            *format,
            searchPath,
            true,
            juce::File(),
            false
        );

        juce::String pluginBeingScanned;
        while (scanner.scanNextFile(true, pluginBeingScanned))
        {
        }
    }
}
