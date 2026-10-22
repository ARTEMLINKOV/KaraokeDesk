
#include "KaraokeAudioEngine.h"

KaraokeAudioEngine::KaraokeAudioEngine()
{
    formatManager.addDefaultFormats();
}

KaraokeAudioEngine::~KaraokeAudioEngine()
{
    shutdownAudio();
}

void KaraokeAudioEngine::initialise()
{
    auto error = deviceManager.initialise(
        2,      // два входных канала для микрофонов
        2,      // два выходных канала
        nullptr,
        true
    );

    if (error.isNotEmpty())
        juce::Logger::writeToLog("Ошибка аудиоустройства: " + error);
}

void KaraokeAudioEngine::shutdownAudio()
{
    deviceManager.closeAudioDevice();
}

juce::AudioDeviceManager& KaraokeAudioEngine::getDeviceManager()
{
    return deviceManager;
}

juce::AudioPluginFormatManager& KaraokeAudioEngine::getFormatManager()
{
    return formatManager;
}
