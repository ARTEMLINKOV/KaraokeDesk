#import <AVFAudio/AVFAudio.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>
#import <CoreMedia/CoreMedia.h>

#include "SystemAudioCapture.h"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <memory>
#include <mutex>
#include <vector>

@interface KaraokeDeskAudioOutput : NSObject <SCStreamOutput>
{
@public
    NSURL* destinationURL;
    AVAudioFile* audioFile;
    std::function<void(juce::String)> errorCallback;
    SystemAudioCapture::AudioCallback audioCallback;
    std::atomic<bool> hasReportedError;
}
- (instancetype)initWithDestination:(NSURL*)url
                      audioCallback:(SystemAudioCapture::AudioCallback)audioCallback
                       errorCallback:(std::function<void(juce::String)>)errorCallback;
@end

@implementation KaraokeDeskAudioOutput

- (instancetype)initWithDestination:(NSURL*)url
                      audioCallback:(SystemAudioCapture::AudioCallback)captureCallback
                       errorCallback:(std::function<void(juce::String)>)completionCallback
{
    self = [super init];
    if (self != nil)
    {
        destinationURL = [url retain];
        audioCallback = std::move(captureCallback);
        errorCallback = std::move(completionCallback);
        hasReportedError.store(false);
    }
    return self;
}

- (void)dealloc
{
    [audioFile release];
    [destinationURL release];
    [super dealloc];
}

- (void)stream:(SCStream*)stream
 didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
        ofType:(SCStreamOutputType)type
{
    juce::ignoreUnused(stream);
    if (type != SCStreamOutputTypeAudio || !CMSampleBufferDataIsReady(sampleBuffer))
        return;

    const auto* audioDescription = CMAudioFormatDescriptionGetStreamBasicDescription(
        static_cast<CMAudioFormatDescriptionRef>(CMSampleBufferGetFormatDescription(sampleBuffer)));
    if (audioDescription == nullptr)
        return;

    const auto frameCount = static_cast<AVAudioFrameCount>(CMSampleBufferGetNumSamples(sampleBuffer));
    AVAudioFormat* format = [[AVAudioFormat alloc] initWithStreamDescription:audioDescription];
    AVAudioPCMBuffer* audioBuffer = [[AVAudioPCMBuffer alloc] initWithPCMFormat:format
                                                             frameCapacity:frameCount];
    if (audioBuffer == nil)
    {
        [format release];
        return;
    }

    size_t requiredSize = 0;
    CMBlockBufferRef retainedBlockBuffer = nullptr;
    CMSampleBufferGetAudioBufferListWithRetainedBlockBuffer(
        sampleBuffer, &requiredSize, nullptr, 0, kCFAllocatorDefault, kCFAllocatorDefault,
        kCMSampleBufferFlag_AudioBufferList_Assure16ByteAlignment, nullptr);
    OSStatus result = requiredSize > 0 ? noErr : kCMSampleBufferError_ArrayTooSmall;

    if (requiredSize > 0)
    {
        auto* sourceList = static_cast<AudioBufferList*>(calloc(1, requiredSize));
        if (sourceList != nullptr)
            result = CMSampleBufferGetAudioBufferListWithRetainedBlockBuffer(
                sampleBuffer, &requiredSize, sourceList, requiredSize, kCFAllocatorDefault,
                kCFAllocatorDefault, kCMSampleBufferFlag_AudioBufferList_Assure16ByteAlignment,
                &retainedBlockBuffer);
        else
            result = kCMSampleBufferError_AllocationFailed;

        if (result == noErr && sourceList != nullptr)
        {
            auto* destinationList = audioBuffer.audioBufferList;
            const auto count = std::min(sourceList->mNumberBuffers, destinationList->mNumberBuffers);
            for (UInt32 index = 0; index < count; ++index)
            {
                const auto bytes = std::min(sourceList->mBuffers[index].mDataByteSize,
                                            destinationList->mBuffers[index].mDataByteSize);
                if (sourceList->mBuffers[index].mData != nullptr
                    && destinationList->mBuffers[index].mData != nullptr)
                    std::memcpy(destinationList->mBuffers[index].mData,
                                sourceList->mBuffers[index].mData, bytes);
            }
            audioBuffer.frameLength = frameCount;

            if (audioCallback && audioDescription->mBitsPerChannel > 0)
            {
                std::vector<float> left(frameCount), right(frameCount);
                const auto channelCount = juce::jmax<UInt32>(1, audioDescription->mChannelsPerFrame);
                const auto isInterleaved = (audioDescription->mFormatFlags
                    & kAudioFormatFlagIsNonInterleaved) == 0;
                const auto isFloat = (audioDescription->mFormatFlags & kAudioFormatFlagIsFloat) != 0;
                const auto isSigned = (audioDescription->mFormatFlags
                    & kAudioFormatFlagIsSignedInteger) != 0;
                const auto bitsPerSample = audioDescription->mBitsPerChannel;
                const auto sampleBytes = (bitsPerSample + 7) / 8;
                const auto readSample = [=](const char* data) -> float
                {
                    if (data == nullptr)
                        return 0.0f;
                    if (isFloat && bitsPerSample == 32)
                    {
                        float value = 0.0f;
                        std::memcpy(&value, data, sizeof(value));
                        return value;
                    }
                    if (isFloat && bitsPerSample == 64)
                    {
                        double value = 0.0;
                        std::memcpy(&value, data, sizeof(value));
                        return static_cast<float>(value);
                    }
                    if (isSigned && bitsPerSample == 16)
                    {
                        int16_t value = 0;
                        std::memcpy(&value, data, sizeof(value));
                        return static_cast<float>(value) / 32768.0f;
                    }
                    if (isSigned && bitsPerSample == 24)
                    {
                        int32_t value = static_cast<uint8_t>(data[0])
                            | (static_cast<int32_t>(static_cast<uint8_t>(data[1])) << 8)
                            | (static_cast<int32_t>(static_cast<uint8_t>(data[2])) << 16);
                        if ((value & 0x00800000) != 0)
                            value |= static_cast<int32_t>(0xff000000);
                        return static_cast<float>(value) / 8388608.0f;
                    }
                    if (isSigned && bitsPerSample == 32)
                    {
                        int32_t value = 0;
                        std::memcpy(&value, data, sizeof(value));
                        return static_cast<float>(static_cast<double>(value) / 2147483648.0);
                    }
                    return 0.0f;
                };

                for (AVAudioFrameCount frame = 0; frame < frameCount; ++frame)
                {
                    for (UInt32 channel = 0; channel < 2; ++channel)
                    {
                        const auto sourceChannel = juce::jmin(channel, channelCount - 1);
                        const UInt32 bufferIndex = isInterleaved ? 0 : sourceChannel;
                        if (bufferIndex >= sourceList->mNumberBuffers)
                            continue;
                        const auto& sourceBuffer = sourceList->mBuffers[bufferIndex];
                        const UInt32 offset = isInterleaved
                            ? (frame * channelCount + sourceChannel) * sampleBytes
                            : frame * sampleBytes;
                        const float value = sourceBuffer.mData != nullptr
                            ? readSample(static_cast<const char*>(sourceBuffer.mData) + offset)
                            : 0.0f;
                        (channel == 0 ? left : right)[frame] = value;
                    }
                }
                const float* channels[] = { left.data(), right.data() };
                audioCallback(channels, 2, static_cast<int>(frameCount));
            }
        }
        free(sourceList);
    }

    if (retainedBlockBuffer != nullptr)
        CFRelease(retainedBlockBuffer);

    if (result != noErr || requiredSize == 0)
    {
        [audioBuffer release];
        [format release];
        return;
    }

    if (audioFile == nil && destinationURL != nil)
    {
        NSError* error = nil;
        audioFile = [[AVAudioFile alloc] initForWriting:destinationURL
                                               settings:audioBuffer.format.settings
                                          commonFormat:audioBuffer.format.commonFormat
                                           interleaved:audioBuffer.format.isInterleaved
                                                 error:&error];
        if (audioFile == nil)
        {
            if (errorCallback && !hasReportedError.exchange(true))
                errorCallback(error.localizedDescription != nil
                    ? juce::String::fromUTF8(error.localizedDescription.UTF8String)
                    : juce::String::fromUTF8("Не удалось создать файл системной записи."));
            [audioBuffer release];
            [format release];
            return;
        }
    }

    if (audioFile != nil)
    {
        NSError* error = nil;
        if (![audioFile writeFromBuffer:audioBuffer error:&error] && errorCallback
            && !hasReportedError.exchange(true))
            errorCallback(error.localizedDescription != nil
                ? juce::String::fromUTF8(error.localizedDescription.UTF8String)
                : juce::String::fromUTF8("Ошибка записи системного аудио."));
    }

    [audioBuffer release];
    [format release];
}

@end

struct SystemAudioCapture::Impl : std::enable_shared_from_this<SystemAudioCapture::Impl>
{
    ~Impl() { stop(); }

    void start(const juce::File& destination, double sampleRate,
               SystemAudioCapture::AudioCallback audioCallback,
               SystemAudioCapture::Completion completion,
               const juce::StringArray& targetAppBundleIdentifiers,
               const juce::StringArray& excludedAppBundleIdentifiers)
    {
        stop();
        stopping.store(false);
        auto self = shared_from_this();
        const auto destinationPath = destination.getFullPathName();
        NSString* pathString = destinationPath.isNotEmpty()
            ? [NSString stringWithUTF8String:destinationPath.toRawUTF8()] : nil;
        if (destinationPath.isNotEmpty() && pathString == nil)
        {
            if (completion)
                completion(juce::String::fromUTF8("Некорректный путь для системной записи."));
            return;
        }
        NSURL* url = pathString != nil ? [NSURL fileURLWithPath:pathString] : nil;
        NSMutableArray<NSString*>* targetBundleIds = [NSMutableArray array];
        for (const auto& bundleId : targetAppBundleIdentifiers)
            if (auto* value = [NSString stringWithUTF8String:bundleId.toRawUTF8()])
                [targetBundleIds addObject:value];
        NSMutableArray<NSString*>* excludedBundleIds = [NSMutableArray array];
        for (const auto& bundleId : excludedAppBundleIdentifiers)
            if (auto* value = [NSString stringWithUTF8String:bundleId.toRawUTF8()])
                [excludedBundleIds addObject:value];

        void (^contentCompletion)(SCShareableContent*, NSError*) =
            ^(SCShareableContent* content, NSError* error)
        {
            if (self->stopping.load())
                return;
            if (error != nil || content.displays.count == 0)
            {
                completion(error != nil
                    ? juce::String::fromUTF8(error.localizedDescription.UTF8String)
                    : juce::String::fromUTF8("Не найден экран для захвата системного аудио."));
                return;
            }

            SCDisplay* display = content.displays.firstObject;
            SCContentFilter* filter = nil;
            if (targetBundleIds.count > 0)
            {
                NSMutableArray<SCRunningApplication*>* targetApps = [NSMutableArray array];
                for (SCRunningApplication* app in content.applications)
                    if ([targetBundleIds containsObject:app.bundleIdentifier])
                        [targetApps addObject:app];

                if (targetApps.count > 0)
                    filter = [[SCContentFilter alloc] initWithDisplay:display
                                                 includingApplications:targetApps
                                                      exceptingWindows:@[]];
                else
                {
                    completion(juce::String::fromUTF8("Не найдено ни одно приложение для захвата: ")
                        + targetAppBundleIdentifiers.joinIntoString(", "));
                    return;
                }
            }
            else if (excludedBundleIds.count > 0)
            {
                NSMutableArray<SCRunningApplication*>* excludedApps = [NSMutableArray array];
                for (SCRunningApplication* app in content.applications)
                    if ([excludedBundleIds containsObject:app.bundleIdentifier])
                        [excludedApps addObject:app];
                filter = [[SCContentFilter alloc] initWithDisplay:display
                                             excludingApplications:excludedApps
                                                   exceptingWindows:@[]];
            }
            else
                filter = [[SCContentFilter alloc] initWithDisplay:display
                                                  excludingWindows:@[]];
            SCStreamConfiguration* configuration = [[SCStreamConfiguration alloc] init];
            configuration.capturesAudio = YES;
            configuration.sampleRate = static_cast<NSInteger>(sampleRate);
            configuration.channelCount = 2;
            configuration.excludesCurrentProcessAudio = YES;
            configuration.width = 2;
            configuration.height = 2;

            auto outputCallback = [self, completion](juce::String message)
            {
                if (self->stopping.load())
                    return;
                completion(message);
            };
            auto* streamOutput = [[KaraokeDeskAudioOutput alloc] initWithDestination:url
                                                                     audioCallback:std::move(audioCallback)
                                                                        errorCallback:std::move(outputCallback)];
            SCStream* newStream = [[SCStream alloc] initWithFilter:filter
                                                     configuration:configuration
                                                          delegate:nil];
            NSError* setupError = nil;
            const bool added = [newStream addStreamOutput:streamOutput
                                                     type:SCStreamOutputTypeAudio
                                       sampleHandlerQueue:dispatch_get_global_queue(
                                           QOS_CLASS_USER_INITIATED, 0)
                                                    error:&setupError];
            [filter release];
            [configuration release];
            if (!added)
            {
                [newStream release];
                [streamOutput release];
                completion(setupError != nil
                    ? juce::String::fromUTF8(setupError.localizedDescription.UTF8String)
                    : juce::String::fromUTF8("Не удалось подключить системный аудиозахват."));
                return;
            }

            {
                const std::lock_guard<std::mutex> lock(self->mutex);
                if (self->stopping.load())
                {
                    [newStream release];
                    [streamOutput release];
                    return;
                }
                self->stream = newStream;
                self->output = streamOutput;
            }

            [newStream startCaptureWithCompletionHandler:^(NSError* startError)
            {
                if (self->stopping.load())
                    return;
                completion(startError != nil
                    ? juce::String::fromUTF8(startError.localizedDescription.UTF8String)
                    : juce::String());
            }];
        };

        [SCShareableContent getShareableContentExcludingDesktopWindows:NO
                                                  onScreenWindowsOnly:NO
                                                    completionHandler:contentCompletion];
    }

    void stop()
    {
        stopping.store(true);
        SCStream* oldStream = nil;
        KaraokeDeskAudioOutput* oldOutput = nil;
        {
            const std::lock_guard<std::mutex> lock(mutex);
            oldStream = stream;
            oldOutput = output;
            stream = nil;
            output = nil;
        }
        if (oldStream != nil)
        {
            [oldStream stopCaptureWithCompletionHandler:^(__unused NSError* error) {}];
            [oldStream release];
        }
        [oldOutput release];
    }

    bool isCapturing() const
    {
        const std::lock_guard<std::mutex> lock(mutex);
        return stream != nil && !stopping.load();
    }

    mutable std::mutex mutex;
    SCStream* stream = nil;
    KaraokeDeskAudioOutput* output = nil;
    std::atomic<bool> stopping { true };
};

SystemAudioCapture::SystemAudioCapture()
    : implementation(std::make_shared<Impl>())
{
}

SystemAudioCapture::~SystemAudioCapture()
{
    stop();
}

void SystemAudioCapture::start(const juce::File& destination, double sampleRate,
                               AudioCallback audioCallback, Completion completion,
                               const juce::StringArray& targetAppBundleIdentifiers,
                               const juce::StringArray& excludedAppBundleIdentifiers)
{
    implementation->start(destination, sampleRate, std::move(audioCallback),
                          std::move(completion), targetAppBundleIdentifiers,
                          excludedAppBundleIdentifiers);
}

void SystemAudioCapture::stop()
{
    implementation->stop();
}

bool SystemAudioCapture::isCapturing() const
{
    return implementation->isCapturing();
}
