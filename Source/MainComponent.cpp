#include "MainComponent.h"
#include <cmath>
#include <functional>
#include <utility>

namespace
{
    class PluginScanJob final : public juce::ThreadPoolJob
    {
    public:
        PluginScanJob(KaraokeAudioEngine& audioEngine, juce::FileSearchPath paths,
                      std::function<void(const juce::String&, float)> progress,
                      std::function<bool()> shouldSkip,
                      std::function<void(int)> completion)
            : ThreadPoolJob("KaraokeDesk plugin scan"), engine(audioEngine),
              searchPaths(std::move(paths)), onProgress(std::move(progress)),
              shouldSkipCurrent(std::move(shouldSkip)),
              onComplete(std::move(completion)) {}
        JobStatus runJob() override
        {
            const int count = engine.scanPlugins(searchPaths,
                [callback = onProgress](const juce::String& item, float progress)
                {
                    juce::MessageManager::callAsync([callback, item, progress]
                    {
                        if (callback)
                            callback(item, progress);
                    });
                }, shouldSkipCurrent);
            juce::MessageManager::callAsync([callback = std::move(onComplete), count]
            {
                if (callback)
                    callback(count);
            });
            return jobHasFinished;
        }
    private:
        KaraokeAudioEngine& engine;
        juce::FileSearchPath searchPaths;
        std::function<void(const juce::String&, float)> onProgress;
        std::function<bool()> shouldSkipCurrent;
        std::function<void(int)> onComplete;
    };

    const juce::Colour bg(0xff0b0e14), panel(0xff151a24), panel2(0xff1d2431),
        text(0xfff3f5fb), muted(0xff9aa6ba);
    juce::Colour accent(0xff2584c7);
    juce::Colour cyan(0xff37d6c5);
    const juce::Font uiFont(float size, bool bold = false)
    {
        return juce::Font(juce::FontOptions(size, bold ? juce::Font::bold : juce::Font::plain));
    }

    const juce::Colour faderGreen(0xff8ddb27);

    const juce::String utf8(const char* sourceText) { return juce::String::fromUTF8(sourceText); }

    void drawFaderScale(juce::Graphics& g, const juce::Slider& slider)
    {
        const auto bounds = slider.getBounds();
        const int marks[] = { 10, 6, 0, -6, -12, -24, -36, -48, -60 };

        g.setFont(uiFont(9.0f));
        g.setColour(muted);
        for (const int db : marks)
        {
            const int y = bounds.getY() + juce::roundToInt(
                slider.getPositionOfValue(static_cast<double>(db)));
            const auto label = db > 0 ? "+" + juce::String(db) : juce::String(db);
            g.drawText(label, bounds.getX() - 34, y - 6, 28, 12, juce::Justification::centredRight);
            g.drawHorizontalLine(y, static_cast<float>(bounds.getX() + 18),
                                 static_cast<float>(bounds.getX() + 29));
        }
    }

    class RecordingOptionsComponent final : public juce::Component
    {
    public:
        RecordingOptionsComponent()
        {
            mic1.setButtonText(juce::String::fromUTF8("Микрофон 1"));
            mic2.setButtonText(juce::String::fromUTF8("Микрофон 2"));
            master.setButtonText(juce::String::fromUTF8("Мастер"));
            youtube.setButtonText(juce::String::fromUTF8("YouTube / системный звук"));
            for (auto* button : { &mic1, &mic2, &master, &youtube })
            {
                button->setToggleState(true, juce::dontSendNotification);
                addAndMakeVisible(*button);
            }
            setSize(340, 140);
        }

        void resized() override
        {
            auto area = getLocalBounds().reduced(8, 4);
            for (auto* button : { &mic1, &mic2, &master, &youtube })
            {
                button->setBounds(area.removeFromTop(32));
                area.removeFromTop(2);
            }
        }

        juce::ToggleButton mic1, mic2, master, youtube;
    };

}

class MainComponent::LookAndFeel final : public juce::LookAndFeel_V4
{
public:
    juce::Font getTextButtonFont(juce::TextButton& button, int buttonHeight) override
    {
        const bool settings = button.getButtonText() == juce::String::fromUTF8("⚙");
        return juce::Font(juce::FontOptions(
            settings ? juce::jmax(25.0f, buttonHeight * 0.72f)
                     : juce::jmax(18.0f, buttonHeight * 0.56f),
            juce::Font::bold));
    }

    void drawButtonText(juce::Graphics& g, juce::TextButton& button,
                        bool highlighted, bool down) override
    {
        juce::ignoreUnused(highlighted, down);
        g.setColour(button.findColour(button.getToggleState()
            ? juce::TextButton::textColourOnId : juce::TextButton::textColourOffId));
        g.setFont(getTextButtonFont(button, button.getHeight()));
        g.drawFittedText(button.getButtonText(), button.getLocalBounds().reduced(2),
                         juce::Justification::centred, 1);
    }

    void drawButtonBackground(juce::Graphics& g, juce::Button& button,
                              const juce::Colour& backgroundColour,
                              bool highlighted, bool down) override
    {
        auto colour = backgroundColour;
        if (down || button.getToggleState())
            colour = button.findColour(juce::TextButton::buttonOnColourId);
        else if (highlighted)
            colour = colour.brighter(0.15f);
        g.setColour(colour);
        g.fillRoundedRectangle(button.getLocalBounds().toFloat().reduced(1.0f), 9.0f);
    }

    void drawComboBox(juce::Graphics& g, int width, int height, bool,
                      int, int, int, int, juce::ComboBox& box) override
    {
        const auto bounds = juce::Rectangle<float>(0.0f, 0.0f, (float) width, (float) height)
                                .reduced(1.0f);
        g.setColour(box.findColour(juce::ComboBox::backgroundColourId));
        g.fillRoundedRectangle(bounds, 7.0f);
        g.setColour(box.findColour(juce::ComboBox::outlineColourId).withAlpha(0.65f));
        g.drawRoundedRectangle(bounds, 7.0f, 1.0f);
        g.setColour(box.findColour(juce::ComboBox::arrowColourId));
        juce::Path arrow;
        arrow.addTriangle((float) width - 19.0f, (float) height * 0.42f,
                          (float) width - 9.0f, (float) height * 0.42f,
                          (float) width - 14.0f, (float) height * 0.62f);
        g.fillPath(arrow);
    }

    void fillTextEditorBackground(juce::Graphics& g, int width, int height,
                                  juce::TextEditor& editor) override
    {
        g.setColour(editor.findColour(juce::TextEditor::backgroundColourId));
        g.fillRoundedRectangle(0.0f, 0.0f, (float) width, (float) height, 8.0f);
    }

    void drawTextEditorOutline(juce::Graphics& g, int width, int height,
                               juce::TextEditor& editor) override
    {
        g.setColour(editor.findColour(juce::TextEditor::outlineColourId));
        g.drawRoundedRectangle(0.5f, 0.5f, (float) width - 1.0f, (float) height - 1.0f,
                               8.0f, 1.0f);
    }

    void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, float minSliderPos, float maxSliderPos,
                          juce::Slider::SliderStyle style, juce::Slider& slider) override
    {
        if (style != juce::Slider::LinearVertical)
        {
            juce::LookAndFeel_V4::drawLinearSlider(g, x, y, width, height, sliderPos,
                                                    minSliderPos, maxSliderPos, style, slider);
            return;
        }

        const float top = static_cast<float>(y) + 1.0f;
        const float bottom = static_cast<float>(y + height) - 1.0f;
        const float trackX = static_cast<float>(x) + static_cast<float>(width) * 0.5f - 9.0f;
        const float trackHeight = juce::jmax(1.0f, bottom - top);
        g.setColour(juce::Colour(0xff080b0a));
        g.fillRoundedRectangle(trackX, top, 18.0f, trackHeight, 2.0f);
        g.setColour(faderGreen);
        g.drawRoundedRectangle(trackX, top, 18.0f, trackHeight, 2.0f, 1.4f);

        const float level = juce::jlimit(0.0f, 1.0f,
            (float) slider.getProperties().getWithDefault("meterLevel", 0.0));
        const float meterHeight = trackHeight * level;
        g.setColour(level >= 0.9f ? juce::Colours::red
                                 : level >= 0.65f ? juce::Colours::yellow : faderGreen);
        if (meterHeight > 0.0f)
            g.fillRect(trackX + 3.0f, bottom - meterHeight, 12.0f, meterHeight);

        const float thumbY = juce::jlimit(top, bottom, sliderPos);
        g.setColour(juce::Colour(0xff14200b));
        g.fillRoundedRectangle(trackX - 2.0f, thumbY - 9.0f, 22.0f, 18.0f, 3.0f);
        g.setColour(faderGreen);
        g.fillRoundedRectangle(trackX - 3.0f, thumbY - 10.0f, 22.0f, 18.0f, 3.0f);
        g.setColour(juce::Colour(0xffd3ff9e));
        g.drawRoundedRectangle(trackX - 3.0f, thumbY - 10.0f, 22.0f, 18.0f, 3.0f, 1.0f);
    }
};

class MainComponent::PluginEditorWindow final : public juce::DocumentWindow
{
public:
    explicit PluginEditorWindow(juce::AudioProcessorEditor& editor)
        : DocumentWindow(editor.getAudioProcessor()->getName(), panel,
                         juce::DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar(true);
        setContentNonOwned(&editor, true);
        setResizable(true, false);
        setAlwaysOnTop(true);
        centreWithSize(juce::jmax(320, editor.getWidth()),
                       juce::jmax(240, editor.getHeight()));
        setVisible(true);
        toFront(true);
    }

    void closeButtonPressed() override
    {
        setVisible(false);
    }
};

class MainComponent::WebPopupWindow final : public juce::DocumentWindow
{
public:
    WebPopupWindow()
        : DocumentWindow("KaraokeDesk", panel, juce::DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar(true);
        setResizable(true, true);
        setAlwaysOnTop(true);
    }

    void navigateTo(const juce::String& targetUrl)
    {
        if (auto* existingBrowser = dynamic_cast<KaraokeWebBrowserComponent*>(getContentComponent()))
            existingBrowser->goToURL(targetUrl);
        else
        {
            auto newBrowser = std::make_unique<KaraokeWebBrowserComponent>();
            setContentOwned(newBrowser.get(), true);
            newBrowser->goToURL(targetUrl);
            newBrowser.release();
        }

        if (!isVisible())
            centreWithSize(800, 620);
        setVisible(true);
        toFront(true);
    }

    void closeButtonPressed() override
    {
        setVisible(false);
    }
};

MainComponent::MainComponent()
    : outputDeviceSelector(engine.getDeviceManager(), 0, 0, 1, 2, false, false, false, true)
{
    setSize(1440, 900);
    accent = engine.getAccentColour();
    cyan = accent.brighter(0.48f);
    customLookAndFeel = std::make_unique<LookAndFeel>();
    setLookAndFeel(customLookAndFeel.get());

    title.setText("KARAOKE DESK", juce::dontSendNotification);
    title.setFont(uiFont(25.0f, true));
    title.setColour(juce::Label::textColourId, text);
    addAndMakeVisible(title);

    url.setTextToShowWhenEmpty(utf8("Вставь ссылку на YouTube-караоке…"), muted);
    url.setColour(juce::TextEditor::backgroundColourId, panel2);
    url.setColour(juce::TextEditor::textColourId, text);
    url.setColour(juce::TextEditor::outlineColourId, panel2);
    url.setFont(uiFont(14.0f));
    addAndMakeVisible(url);

    for (auto* b : { &openButton, &mixerToggle, &settingsToggle, &scanPluginsButton,
                     &skipPluginButton, &addPluginDirectoryButton, &recordButton,
                     &monitorButton, &stopButton,
                     &muteButtons[0], &muteButtons[1], &muteButtons[2], &muteButtons[3],
                     &soloButtons[0], &soloButtons[1], &soloButtons[2], &soloButtons[3] })
    {
        styleIconButton(*b, {});
        addAndMakeVisible(*b);
    }
    openButton.setTooltip(utf8("Открыть видео"));
    openButton.onClick = [this] { openYoutube(); };
    openButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xffd92323));
    settingsToggle.onClick = [this]
    {
        settingsVisible = !settingsVisible;
        settingsToggle.setTooltip(settingsVisible ? utf8("Скрыть настройки") : utf8("Настройки"));
        resized(); repaint();
    };
    settingsToggle.setTooltip(utf8("Настройки"));
    scanPluginsButton.onClick = [this] { scanPluginsInBackground(); };
    scanPluginsButton.setTooltip(utf8("Сканировать только выбранные папки плагинов"));
    skipPluginButton.onClick = [this] { skipCurrentPluginScan(); };
    skipPluginButton.setTooltip(utf8("Остановить сканирование текущего плагина и перейти к следующему"));
    addPluginDirectoryButton.onClick = [this] { choosePluginDirectory(); };
    addPluginDirectoryButton.setTooltip(utf8("Добавить папку с плагинами"));
    mixerToggle.setTooltip(utf8("Показать/скрыть микшер"));
    monitorButton.setClickingTogglesState(true);
    monitorButton.setTooltip(utf8("Включить мониторинг микрофонов"));
    monitorButton.onClick = [this]
    {
        if (monitorButton.getToggleState())
        {
            engine.start();
            status.setText(engine.getStatus(), juce::dontSendNotification);
            if (engine.getDeviceManager().getCurrentAudioDevice() != nullptr)
            {
                if (!engine.hasLoopbackInput() && youtubePageOpened)
                    showLoopbackSetupWarning();
            }
        }
        else
        {
            if (!recordingRunning)
                engine.stop();
            if (!recordingRunning || !youtubeTrackRecording)
            {
                youtubeCapture.stop();
                youtubeCaptureRequested = false;
            }
            setYoutubeVolume();
            status.setText(engine.getStatus(), juce::dontSendNotification);
        }
    };
    stopButton.setTooltip(utf8("Остановить аудио"));
    stopButton.onClick = [this]
    {
        if (recordingRunning)
            stopRecording();
        monitorButton.setToggleState(false, juce::dontSendNotification);
        youtubeCapture.stop();
        youtubeCaptureRequested = false;
        setYoutubeVolume();
        engine.stop();
        status.setText(engine.getStatus(), juce::dontSendNotification);
    };
    recordButton.setTooltip(utf8("Выбрать каналы и начать запись"));
    recordButton.onClick = [this]
    {
        if (recordingRunning)
            stopRecording();
        else
            showRecordingOptions();
    };
    pluginScanStatus.setText(utf8("Поиск плагинов ещё не запускался"), juce::dontSendNotification);
    pluginScanStatus.setColour(juce::Label::textColourId, muted);
    pluginScanStatus.setFont(uiFont(12.0f));
    addAndMakeVisible(pluginScanStatus);
    scanPluginsButton.setVisible(false);
    skipPluginButton.setVisible(false);
    pluginScanStatus.setVisible(false);
    settingsContent.addAndMakeVisible(outputDeviceSelector);
    outputDeviceSelector.setVisible(false);
    settingsViewport.setViewedComponent(&settingsContent, false);
    settingsViewport.setScrollBarsShown(true, false);
    settingsViewport.setScrollBarThickness(10);
    settingsViewport.setVisible(false);
    addAndMakeVisible(settingsViewport);
    mixerToggle.onClick = [this]
    {
        mixerVisible = !mixerVisible;
        mixerToggle.setTooltip(mixerVisible ? utf8("Скрыть микшер") : utf8("Показать микшер"));
        resized();
        repaint();
    };

    status.setText(utf8("Аудио остановлено"), juce::dontSendNotification);
    status.setColour(juce::Label::textColourId, muted);
    status.setFont(uiFont(12.0f));
    addAndMakeVisible(status);

    auto setupLabel = [this](juce::Label& l, const juce::String& value, float size)
    {
        l.setText(value, juce::dontSendNotification);
        l.setColour(juce::Label::textColourId, text);
        l.setFont(uiFont(size, true));
        if (&l == &outputDeviceTitle)
            settingsContent.addAndMakeVisible(l);
        else
            addAndMakeVisible(l);
    };
    setupLabel(outputDeviceTitle, utf8("ОСНОВНОЙ ВЫХОД"), 13.0f);
    setupLabel(ch1Title, utf8("МИКРОФОН 1"), 15.0f);
    setupLabel(ch2Title, utf8("МИКРОФОН 2"), 15.0f);
    setupLabel(masterTitle, utf8("МАСТЕР"), 15.0f);
    setupLabel(youtubeTitle, utf8("YOUTUBE"), 15.0f);
    setupLabel(inputDeviceTitle, utf8("ВХОД"), 13.0f);
    setupLabel(sampleRateTitle, utf8("ЧАСТОТА"), 11.0f);
    setupLabel(bufferSizeTitle, utf8("БУФЕР / ЗАДЕРЖКА"), 11.0f);
    setupLabel(themeColourTitle, utf8("ЦВЕТ ИНТЕРФЕЙСА"), 11.0f);
    recordingResult.setColour(juce::Label::textColourId, cyan);
    recordingResult.setFont(uiFont(12.0f, true));
    addAndMakeVisible(recordingResult);
    themeColourSelector.setCurrentColour(accent, juce::dontSendNotification);
    themeColourSelector.setColour(juce::ColourSelector::backgroundColourId, panel);
    themeColourSelector.addChangeListener(this);
    addAndMakeVisible(themeColourSelector);
    inputDevice.setColour(juce::ComboBox::backgroundColourId, panel2);
    inputDevice.setColour(juce::ComboBox::textColourId, text);
    inputDevice.setColour(juce::ComboBox::outlineColourId, muted);
    inputDevice.onChange = [this] { selectInputDevice(); };
    addAndMakeVisible(inputDevice);
    for (auto* combo : { &sampleRateSelector, &bufferSizeSelector })
    {
        combo->setColour(juce::ComboBox::backgroundColourId, panel2);
        combo->setColour(juce::ComboBox::textColourId, text);
        combo->setColour(juce::ComboBox::outlineColourId, muted);
        addAndMakeVisible(*combo);
    }
    sampleRateSelector.onChange = [this] { selectSampleRate(); };
    bufferSizeSelector.onChange = [this] { selectBufferSize(); };
    outputDeviceSelector.setItemHeight(22);
    for (int channel = 0; channel < 2; ++channel)
    {
        auto& selector = inputChannelSelectors[channel];
        selector.setColour(juce::ComboBox::backgroundColourId, panel2);
        selector.setColour(juce::ComboBox::textColourId, text);
        selector.setColour(juce::ComboBox::outlineColourId, muted);
        selector.setTextWhenNothingSelected(utf8("Input"));
        selector.onChange = [this, channel] { selectInputChannel(channel); };
        addAndMakeVisible(selector);
    }

    for (int channel = 0; channel < 4; ++channel)
    {
        muteButtons[channel].setClickingTogglesState(true);
        muteButtons[channel].setTooltip(utf8("Mute"));
        muteButtons[channel].onClick = [this, channel]
        {
            engine.setChannelMuted(channel, muteButtons[channel].getToggleState());
            muteButtons[channel].setColour(
                juce::TextButton::buttonOnColourId,
                muteButtons[channel].getToggleState() ? juce::Colours::red : cyan);
            setYoutubeVolume();
        };
        soloButtons[channel].setClickingTogglesState(true);
        soloButtons[channel].setTooltip(utf8("Solo"));
        soloButtons[channel].onClick = [this, channel]
        {
            engine.setChannelSolo(channel, soloButtons[channel].getToggleState());
            soloButtons[channel].setColour(
                juce::TextButton::buttonOnColourId,
                soloButtons[channel].getToggleState() ? juce::Colours::orange : cyan);
            setYoutubeVolume();
        };
    }

    styleSlider(gain1, juce::Decibels::gainToDecibels(0.8), -60.0, 10.0);
    styleSlider(gain2, juce::Decibels::gainToDecibels(0.8), -60.0, 10.0);
    styleSlider(master, juce::Decibels::gainToDecibels(0.65), -60.0, 10.0);
    styleSlider(youtubeVolume, juce::Decibels::gainToDecibels(0.8), -60.0, 10.0);
    gain1.onValueChange = [this] { engine.setChannelGain(0, juce::Decibels::decibelsToGain((float) gain1.getValue())); };
    gain2.onValueChange = [this] { engine.setChannelGain(1, juce::Decibels::decibelsToGain((float) gain2.getValue())); };
    master.onValueChange = [this] { engine.setMasterGain(juce::Decibels::decibelsToGain((float) master.getValue())); };
    youtubeVolume.onValueChange = [this]
    {
        engine.setChannelGain(2, juce::Decibels::decibelsToGain(
            static_cast<float>(youtubeVolume.getValue())));
        setYoutubeVolume();
    };

    pluginNames = engine.getAvailablePluginNames();
    for (int channel = 0; channel < 4; ++channel)
    {
        for (int slot = 0; slot < 4; ++slot)
        {
            auto& combo = pluginSlots[channel][slot];
            combo.setColour(juce::ComboBox::backgroundColourId, panel2);
            combo.setColour(juce::ComboBox::textColourId, text);
            combo.setColour(juce::ComboBox::outlineColourId, muted);
            combo.setTextWhenNothingSelected(utf8("— пустой слот —"));
            combo.onChange = [this, channel, slot] { setPluginSlot(channel, slot); };
            combo.onPluginClick = [this, channel, slot] { showPluginEditor(channel, slot); };
            combo.setTooltip(utf8("Нажмите на название загруженного плагина, чтобы открыть его окно. Стрелка справа меняет плагин."));
            addAndMakeVisible(combo);
        }
    }
    engine.getDeviceManager().addChangeListener(this);
    customPluginPaths = engine.getPluginSearchPaths();
    pluginScanStatus.setText(utf8("Выбрано папок: ")
        + juce::String(customPluginPaths.getNumPaths())
        + utf8("; из кэша загружено плагинов: ") + juce::String(pluginNames.size())
        + utf8("; системные папки не сканируются"), juce::dontSendNotification);
    updatePluginLists();
    refreshInputDevices();
    refreshAudioDeviceOptions();

    addAndMakeVisible(youtube);
    youtube.setVisible(false);
    youtube.onNewWindow = [this](const juce::String& newURL) { showWebPopup(newURL); };
    startTimerHz(30);
}

MainComponent::~MainComponent()
{
    stopTimer();
    webPopupWindow.reset();
    for (auto& channelWindows : pluginEditorWindows)
        for (auto& window : channelWindows)
            window.reset();
    youtubeCapture.stop();
    themeColourSelector.removeChangeListener(this);
    engine.getDeviceManager().removeChangeListener(this);
    pluginScanPool.removeAllJobs(true, -1);
    engine.saveSettings();
    engine.stop();
    settingsViewport.setViewedComponent(nullptr, false);
    setLookAndFeel(nullptr);
}

void MainComponent::styleButton(juce::TextButton& b)
{
    b.setColour(juce::TextButton::buttonColourId, accent);
    b.setColour(juce::TextButton::textColourOffId, text);
    b.setColour(juce::TextButton::buttonOnColourId, cyan);
}

void MainComponent::styleIconButton(juce::TextButton& button, const juce::String& tooltip)
{
    styleButton(button);
    button.setTooltip(tooltip);
}

void MainComponent::applyAccentColour(juce::Colour colour)
{
    accent = colour;
    cyan = colour.brighter(0.48f);
    engine.setAccentColour(colour);

    for (auto* button : { &openButton, &mixerToggle, &settingsToggle, &scanPluginsButton,
                          &addPluginDirectoryButton, &recordButton, &monitorButton, &stopButton })
        styleButton(*button);
    openButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xffd92323));
    for (auto& button : muteButtons)
        button.setColour(juce::TextButton::buttonOnColourId,
                         button.getToggleState() ? juce::Colours::red : cyan);
    for (auto& button : soloButtons)
        button.setColour(juce::TextButton::buttonOnColourId,
                         button.getToggleState() ? juce::Colours::orange : cyan);
    for (auto* slider : { &gain1, &gain2, &master, &youtubeVolume })
        slider->setColour(juce::Slider::thumbColourId, cyan);
    themeColourSelector.setCurrentColour(colour, juce::dontSendNotification);
    recordingResult.setColour(juce::Label::textColourId, cyan);
    repaint();
}

void MainComponent::styleSlider(
    juce::Slider& s,
    double initialValue,
    double minimum,
    double maximum)
{
    s.setSliderStyle(juce::Slider::LinearVertical);
    s.setSliderSnapsToMousePosition(true);
    s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 64, 20);
    s.setRange(minimum, maximum, &s == &youtubeVolume ? 0.01 : 1.0);
    s.setSkewFactorFromMidPoint(-6.0);
    s.setValue(initialValue, juce::dontSendNotification);
    if (&s != &youtubeVolume)
    {
        s.setTextValueSuffix(" dB");
        s.setNumDecimalPlacesToDisplay(0);
    }
    s.setColour(juce::Slider::trackColourId, panel2);
    s.setColour(juce::Slider::thumbColourId, cyan);
    s.setColour(juce::Slider::textBoxTextColourId, text);
    s.setColour(juce::Slider::textBoxBackgroundColourId, panel2);
    s.setColour(juce::Slider::textBoxOutlineColourId, panel2);
    addAndMakeVisible(s);
}

void MainComponent::choosePluginDirectory()
{
    pluginDirectoryChooser = std::make_unique<juce::FileChooser>(
        utf8("Выбери папку VST3 или AU"), juce::File::getSpecialLocation(juce::File::userHomeDirectory));
    pluginDirectoryChooser->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
        [safeThis = juce::Component::SafePointer<MainComponent>(this)](const juce::FileChooser& chooser)
        {
            if (safeThis == nullptr)
                return;

            const auto directory = chooser.getResult();
            if (directory.isDirectory()
                && safeThis->customPluginPaths.addIfNotAlreadyThere(directory))
            {
                safeThis->engine.setPluginSearchPaths(safeThis->customPluginPaths);
                safeThis->pluginScanStatus.setText(
                    utf8("Добавлена папка: ") + directory.getFullPathName(),
                    juce::dontSendNotification);
            }
        });
}

void MainComponent::showRecordingOptions()
{
    RecordingOptionsComponent options;
    juce::AlertWindow dialog(
        utf8("Запись исполнения"),
        utf8("Выбери дорожки. Каждый источник сохранится в отдельный файл."),
        juce::MessageBoxIconType::NoIcon);
    dialog.addCustomComponent(&options);
    dialog.addButton(utf8("Начать запись"), 1, juce::KeyPress(juce::KeyPress::returnKey));
    dialog.addButton(utf8("Отмена"), 0, juce::KeyPress(juce::KeyPress::escapeKey));
    if (dialog.runModalLoop() != 1)
        return;

    const bool recordMic1 = options.mic1.getToggleState();
    const bool recordMic2 = options.mic2.getToggleState();
    const bool recordMaster = options.master.getToggleState();
    const bool recordYoutube = options.youtube.getToggleState();
    if (!recordMic1 && !recordMic2 && !recordMaster && !recordYoutube)
    {
        recordingResult.setText(utf8("Выбери хотя бы один источник."),
                                juce::dontSendNotification);
        return;
    }

    if (recordMic1 || recordMic2 || recordMaster || recordYoutube)
    {
        engine.start();
        if (engine.getDeviceManager().getCurrentAudioDevice() == nullptr)
        {
            status.setText(engine.getStatus(), juce::dontSendNotification);
            return;
        }
    }

    const auto timestamp = juce::Time::getCurrentTime().formatted("%Y-%m-%d_%H-%M-%S")
        + "_" + juce::String(juce::Time::currentTimeMillis());
    const auto directory = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
        .getChildFile("KaraokeDesk Recordings")
        .getChildFile(timestamp);
    if (!directory.createDirectory())
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::MessageBoxIconType::WarningIcon,
            utf8("Не удалось создать папку записи"), directory.getFullPathName());
        return;
    }
    juce::String error;
    if ((recordMic1 || recordMic2 || recordMaster)
        && !engine.startRecording(directory, recordMic1, recordMic2, recordMaster, error))
    {
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
            utf8("Не удалось начать запись"), error);
        return;
    }

    if (recordYoutube)
    {
        const auto youtubeFile = directory.getChildFile("YouTube_" + timestamp + ".caf");
        youtubeCapture.stop();
        startYoutubeMixerCapture(youtubeFile);
        youtubeTrackRecording = true;
    }

    currentRecordingDirectory = directory;
    recordingRunning = true;
    recordButton.setButtonText(utf8("■"));
    recordButton.setTooltip(utf8("Остановить запись"));
    recordingResult.setText(utf8("Запись в: ") + directory.getFullPathName(),
                            juce::dontSendNotification);
}

void MainComponent::stopRecording()
{
    if (youtubeTrackRecording)
    {
        youtubeCapture.stop();
        youtubeCaptureRequested = false;
        youtubeTrackRecording = false;
        if (monitorButton.getToggleState())
            startYoutubeMixerCapture({});
    }
    engine.stopRecording();
    recordingRunning = false;
    recordButton.setButtonText(utf8("●"));
    recordButton.setTooltip(utf8("Выбрать каналы и начать запись"));
    recordingResult.setText(utf8("Запись сохранена в: ")
        + currentRecordingDirectory.getFullPathName(), juce::dontSendNotification);
    if (!monitorButton.getToggleState())
        engine.stop();
    setYoutubeVolume();
}

void MainComponent::refreshInputDevices()
{
    auto& manager = engine.getDeviceManager();
    auto* type = manager.getCurrentDeviceTypeObject();
    if (type == nullptr)
    {
        auto& availableTypes = manager.getAvailableDeviceTypes();
        if (availableTypes.isEmpty())
            return;

        manager.setCurrentAudioDeviceType(availableTypes[0]->getTypeName(), false);
        type = manager.getCurrentDeviceTypeObject();
        if (type == nullptr)
            return;
    }

    type->scanForDevices();
    const auto names = type->getDeviceNames(true);
    const auto signature = type->getTypeName() + "\n" + names.joinIntoString("\n");
    if (signature != inputDeviceListSignature)
    {
        inputDeviceListSignature = signature;
        inputDevice.clear(juce::dontSendNotification);
        for (int i = 0; i < names.size(); ++i)
            inputDevice.addItem(names[i], i + 1);
    }

    auto setup = manager.getAudioDeviceSetup();
    const int selectedIndex = names.indexOf(setup.inputDeviceName);
    const int inputIndex = names.isEmpty()
        ? -1
        : selectedIndex >= 0 ? selectedIndex
                             : juce::jlimit(0, names.size() - 1, type->getDefaultDeviceIndex(true));

    if (inputIndex >= 0 && inputIndex < names.size())
    {
        inputDevice.setSelectedId(inputIndex + 1, juce::dontSendNotification);

        if (selectedIndex < 0)
        {
            setup.inputDeviceName = names[inputIndex];
            const auto error = manager.setAudioDeviceSetup(setup, true);
            if (error.isNotEmpty())
                juce::AlertWindow::showMessageBoxAsync(
                    juce::MessageBoxIconType::WarningIcon,
                    utf8("Не удалось включить входные каналы"),
                    error);
        }
    }
    else if (names.isEmpty())
        inputDevice.setText(utf8("Нет входных устройств"), juce::dontSendNotification);

    if (inputIndex >= 0 && setup.useDefaultInputChannels)
        configureAllInputChannels();

    refreshInputChannels();
}

void MainComponent::configureAllInputChannels()
{
    auto& manager = engine.getDeviceManager();
    auto setup = manager.getAudioDeviceSetup();
    auto* type = manager.getCurrentDeviceTypeObject();
    if (type == nullptr || setup.inputDeviceName.isEmpty())
        return;

    std::unique_ptr<juce::AudioIODevice> probe(
        type->createDevice(setup.outputDeviceName, setup.inputDeviceName));
    if (probe == nullptr)
        return;

    const int inputCount = probe->getInputChannelNames().size();
    if (inputCount <= 0)
        return;

    setup.inputChannels.clear();
    setup.inputChannels.setRange(0, inputCount, true);
    setup.useDefaultInputChannels = false;
    const auto error = manager.setAudioDeviceSetup(setup, true);
    if (error.isNotEmpty())
        juce::AlertWindow::showMessageBoxAsync(
            juce::MessageBoxIconType::WarningIcon,
            utf8("Не удалось включить входные каналы"),
            error);
}

void MainComponent::selectInputDevice()
{
    const int selectedId = inputDevice.getSelectedId();
    if (selectedId <= 0)
        return;

    auto setup = engine.getDeviceManager().getAudioDeviceSetup();
    setup.inputDeviceName = inputDevice.getText();
    auto* type = engine.getDeviceManager().getCurrentDeviceTypeObject();
    if (type != nullptr)
    {
        std::unique_ptr<juce::AudioIODevice> probe(
            type->createDevice(setup.outputDeviceName, setup.inputDeviceName));
        if (probe != nullptr)
        {
            const int inputCount = probe->getInputChannelNames().size();
            setup.inputChannels.clear();
            setup.inputChannels.setRange(0, inputCount, true);
            setup.useDefaultInputChannels = false;
        }
    }
    const auto error = engine.getDeviceManager().setAudioDeviceSetup(setup, true);
    if (error.isNotEmpty())
        juce::AlertWindow::showMessageBoxAsync(
            juce::MessageBoxIconType::WarningIcon,
            utf8("Не удалось выбрать входное устройство"),
            error);
}

void MainComponent::refreshInputChannels()
{
    auto* device = engine.getDeviceManager().getCurrentAudioDevice();
    if (device == nullptr)
        return;

    const auto names = device->getInputChannelNames();
    const bool hadLoopbackInput = engine.hasLoopbackInput();
    int loopbackLeft = -1;
    int loopbackRight = -1;
    for (int input = 0; input + 1 < names.size(); ++input)
    {
        if (names[input].containsIgnoreCase("BlackHole")
            && names[input + 1].containsIgnoreCase("BlackHole"))
        {
            loopbackLeft = input;
            loopbackRight = input + 1;
            break;
        }
    }
    engine.setLoopbackInputChannels(loopbackLeft, loopbackRight);
    const bool hasLoopbackInput = engine.hasLoopbackInput();
    inputDevice.setTooltip(hasLoopbackInput
        ? utf8("BlackHole найден: звук YouTube поступает в микшер через агрегированное устройство.")
        : utf8("Для маршрутизации звука YouTube через микшер добавьте BlackHole 2ch и EVO4 в агрегированное входное устройство."));
    if (hasLoopbackInput != hadLoopbackInput)
    {
        if (hasLoopbackInput)
        {
            youtubeCapture.stop();
            youtubeCaptureRequested = false;
        }
        else if (monitorButton.getToggleState())
            showLoopbackSetupWarning();
    }

    const auto signature = device->getName() + "\n" + names.joinIntoString("\n");
    if (signature != inputChannelListSignature)
    {
        inputChannelListSignature = signature;
        for (int channel = 0; channel < 2; ++channel)
        {
            auto& selector = inputChannelSelectors[channel];
            selector.clear(juce::dontSendNotification);
            for (int input = 0; input < names.size(); ++input)
                selector.addItem(names[input], input + 1);
            if (names.isEmpty())
                selector.setText(utf8("Нет входов"), juce::dontSendNotification);
            else
                selector.setSelectedId(juce::jmin(channel + 1, names.size()),
                                       juce::dontSendNotification);
        }
    }

    for (int channel = 0; channel < 2 && channel < names.size(); ++channel)
    {
        if (inputChannelSelectors[channel].getSelectedId() <= 0)
            inputChannelSelectors[channel].setSelectedId(channel + 1, juce::dontSendNotification);
        engine.setChannelInput(channel, inputChannelSelectors[channel].getSelectedId() - 1);
    }
}

void MainComponent::refreshAudioDeviceOptions()
{
    availableSampleRates.clear();
    availableBufferSizes.clear();
    sampleRateSelector.clear(juce::dontSendNotification);
    bufferSizeSelector.clear(juce::dontSendNotification);

    auto* device = engine.getDeviceManager().getCurrentAudioDevice();
    if (device == nullptr)
    {
        sampleRateSelector.setText(utf8("Нет устройства"), juce::dontSendNotification);
        bufferSizeSelector.setText(utf8("Нет устройства"), juce::dontSendNotification);
        return;
    }

    availableSampleRates = device->getAvailableSampleRates();
    availableBufferSizes = device->getAvailableBufferSizes();
    for (int index = 0; index < availableSampleRates.size(); ++index)
    {
        const auto rate = availableSampleRates.getReference(index);
        sampleRateSelector.addItem(juce::String(rate / 1000.0, 1) + " kHz",
                                   index + 1);
        if (std::abs(rate - device->getCurrentSampleRate()) < 1.0)
            sampleRateSelector.setSelectedId(index + 1, juce::dontSendNotification);
    }

    for (int index = 0; index < availableBufferSizes.size(); ++index)
    {
        const int samples = availableBufferSizes.getReference(index);
        const int latencyMs = juce::roundToInt(
            2000.0 * static_cast<double>(samples) / device->getCurrentSampleRate());
        bufferSizeSelector.addItem(juce::String(samples) + " samples (~"
                                       + juce::String(latencyMs) + " ms round-trip)",
                                   index + 1);
        if (samples == device->getCurrentBufferSizeSamples())
            bufferSizeSelector.setSelectedId(index + 1, juce::dontSendNotification);
    }

    sampleRateSelector.setEnabled(availableSampleRates.size() > 1);
    bufferSizeSelector.setEnabled(availableBufferSizes.size() > 1);
}

void MainComponent::selectSampleRate()
{
    const int index = sampleRateSelector.getSelectedId() - 1;
    if (index < 0 || index >= availableSampleRates.size())
        return;

    auto setup = engine.getDeviceManager().getAudioDeviceSetup();
    setup.sampleRate = availableSampleRates.getReference(index);
    const auto error = engine.getDeviceManager().setAudioDeviceSetup(setup, true);
    if (error.isNotEmpty())
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
            utf8("Не удалось изменить частоту дискретизации"), error);
    else
        engine.saveSettings();
}

void MainComponent::selectBufferSize()
{
    const int index = bufferSizeSelector.getSelectedId() - 1;
    if (index < 0 || index >= availableBufferSizes.size())
        return;

    auto setup = engine.getDeviceManager().getAudioDeviceSetup();
    setup.bufferSize = availableBufferSizes.getReference(index);
    const auto error = engine.getDeviceManager().setAudioDeviceSetup(setup, true);
    if (error.isNotEmpty())
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
            utf8("Не удалось изменить размер аудиобуфера"), error);
    else
        engine.saveSettings();
}

void MainComponent::selectInputChannel(int channel)
{
    if (channel >= 0 && channel < 2 && inputChannelSelectors[channel].getSelectedId() > 0)
        engine.setChannelInput(channel, inputChannelSelectors[channel].getSelectedId() - 1);
}

void MainComponent::updatePluginLists()
{
    for (int channel = 0; channel < 4; ++channel)
    {
        for (int slot = 0; slot < 4; ++slot)
        {
            auto& combo = pluginSlots[channel][slot];
            combo.clear(juce::dontSendNotification);
            for (int i = 0; i < pluginNames.size(); ++i)
                combo.addItem(pluginNames[i], i + 1);
            if (!pluginNames.isEmpty())
                combo.addItem(utf8("Очистить слот"), pluginNames.size() + 1);

            const int pluginIndex = pluginNames.indexOf(pluginSlotNames[channel][slot]);
            combo.setSelectedId(pluginIndex >= 0 ? pluginIndex + 1 : 0,
                                juce::dontSendNotification);
        }
    }
}

void MainComponent::openYoutube()
{
    auto s = url.getText().trim();
    if (s.isNotEmpty())
    {
        if (!s.startsWithIgnoreCase("https://") && !s.startsWithIgnoreCase("http://"))
            s = "https://" + s;
        youtubePageOpened = true;
        youtube.setVisible(true);
        youtube.goToURL(s);
        if (monitorButton.getToggleState() && !engine.hasLoopbackInput())
            showLoopbackSetupWarning();
    }
}

void MainComponent::showWebPopup(const juce::String& newURL)
{
    if (!newURL.startsWithIgnoreCase("https://"))
        return;

    if (webPopupWindow == nullptr)
        webPopupWindow = std::make_unique<WebPopupWindow>();
    webPopupWindow->navigateTo(newURL);
}

void MainComponent::setYoutubeVolume()
{
    if (engine.hasLoopbackInput())
    {
        youtube.evaluateJavascript(
            "document.querySelectorAll('video').forEach(v=>{v.volume=1;v.muted=false;});");
        return;
    }

    const auto gain = juce::Decibels::decibelsToGain(
        static_cast<float>(youtubeVolume.getValue()));
    bool anySourceSolo = false;
    for (int channel = 0; channel < 3; ++channel)
        anySourceSolo = anySourceSolo || soloButtons[channel].getToggleState();
    const bool shouldMuteVideo = (!engine.hasLoopbackInput()
        && (youtubeCaptureRequested || youtubeCapture.isCapturing()))
        || muteButtons[2].getToggleState()
        || (anySourceSolo && !soloButtons[2].getToggleState());
    const auto volume = juce::String(juce::jlimit(0.0f, 1.0f, gain), 2);
    youtube.evaluateJavascript("(()=>{document.querySelectorAll('video').forEach(v=>{v.volume="
        + volume + ";v.muted=(" + juce::String(shouldMuteVideo ? "true" : "false")
        + "||" + volume + "===0);});})();");
}

void MainComponent::startYoutubeMixerCapture(const juce::File& destination)
{
    if (engine.hasLoopbackInput() && destination.getFullPathName().isEmpty())
        return;

    auto* device = engine.getDeviceManager().getCurrentAudioDevice();
    if (device == nullptr)
        return;

    const auto sampleRate = device->getCurrentSampleRate();
    youtubeCaptureRequested = true;
    youtubeCapture.start(destination, sampleRate,
        [safeThis = juce::Component::SafePointer<MainComponent>(this)](
            const float* const* channels, int channelCount, int samples)
        {
            if (safeThis != nullptr && !safeThis->engine.hasLoopbackInput())
                safeThis->engine.pushYoutubeAudio(channels, channelCount, samples);
        },
        [safeThis = juce::Component::SafePointer<MainComponent>(this)](const juce::String& message)
        {
            if (message.isEmpty())
                return;
            juce::MessageManager::callAsync([safeThis, message]
            {
                if (safeThis == nullptr)
                    return;
                safeThis->youtubeCapture.stop();
                safeThis->youtubeCaptureRequested = false;
                safeThis->setYoutubeVolume();
                safeThis->status.setText(utf8("Захват YouTube: ") + message,
                                         juce::dontSendNotification);
                const auto lowerMessage = message.toLowerCase();
                const auto detail = lowerMessage.contains("denied")
                    ? utf8("macOS запретила запись аудио. Откройте System Settings > Privacy & Security > Screen & System Audio Recording, разрешите KaraokeDesk и перезапустите приложение.")
                    : message;
                juce::AlertWindow::showMessageBoxAsync(
                    juce::MessageBoxIconType::WarningIcon,
                    utf8("Не удалось запустить аудиоканал YouTube"), detail);
            });
        });
    setYoutubeVolume();
}

void MainComponent::showLoopbackSetupWarning()
{
    status.setText(
        utf8("Добавьте BlackHole 2ch во входной Aggregate Device для микширования YouTube."),
        juce::dontSendNotification);
    if (loopbackWarningShown)
        return;
    loopbackWarningShown = true;
    juce::AlertWindow::showMessageBoxAsync(
        juce::MessageBoxIconType::WarningIcon,
        utf8("Не настроен аудиомаршрут YouTube"),
        utf8("Чтобы фейдеры обрабатывали звук YouTube, установите BlackHole 2ch, "
             "добавьте его вместе с EVO4 в Aggregate Device, выберите агрегат как вход KaraokeDesk, "
             "а BlackHole — как системный вывод macOS."));
}

void MainComponent::scanPluginsInBackground()
{
    if (pluginScanRunning)
        return;

    if (customPluginPaths.getNumPaths() == 0)
    {
        pluginScanStatus.setText(utf8("Сначала добавьте папку с плагинами."),
                                 juce::dontSendNotification);
        return;
    }

    pluginScanRunning = true;
    skipPluginRequested.store(false);
    scanPluginsButton.setEnabled(false);
    skipPluginButton.setVisible(settingsVisible);
    skipPluginButton.setEnabled(true);
    skipPluginButton.setButtonText(utf8("⏭"));
    pluginScanStatus.setText(utf8("Сканируются только выбранные папки…"),
                             juce::dontSendNotification);
    juce::Component::SafePointer<MainComponent> safeThis(this);
    pluginScanPool.addJob(new PluginScanJob(
        engine, customPluginPaths,
        [safeThis](const juce::String& item, float progress)
        {
            if (safeThis == nullptr)
                return;
            const auto percent = juce::roundToInt(progress * 100.0f);
            safeThis->pluginScanStatus.setText(
                item.isEmpty()
                    ? utf8("Завершение сканирования…")
                    : item.startsWith(utf8("Пропущен: "))
                        ? item
                        : item.startsWith(utf8("Уже проверен: "))
                        ? utf8("Из кэша: ") + juce::File(item.fromFirstOccurrenceOf(": ", false, false)).getFileName()
                        : item.startsWith(utf8("Ошибка сканирования: "))
                        ? item
                        : utf8("Сканируется (") + juce::String(percent) + utf8("%): ")
                            + juce::File(item).getFileName(),
                juce::dontSendNotification);
            if (!item.isEmpty() && !item.startsWith(utf8("Пропущен: "))
                && !item.startsWith(utf8("Ошибка сканирования: ")))
            {
                safeThis->skipPluginButton.setEnabled(true);
                safeThis->skipPluginButton.setButtonText(utf8("⏭"));
            }
        },
        [safeThis]
        {
            return safeThis != nullptr && safeThis->skipPluginRequested.exchange(false);
        },
        [safeThis](int count)
        {
            if (safeThis == nullptr)
                return;

            safeThis->pluginNames = safeThis->engine.getAvailablePluginNames();
            safeThis->updatePluginLists();
            safeThis->pluginScanRunning = false;
            safeThis->scanPluginsButton.setEnabled(true);
            safeThis->skipPluginButton.setVisible(false);
            safeThis->pluginScanStatus.setText(
                utf8("Сканирование завершено. Найдено плагинов: ") + juce::String(count),
                juce::dontSendNotification);
        }), true);
}

void MainComponent::skipCurrentPluginScan()
{
    if (!pluginScanRunning)
        return;
    skipPluginRequested.store(true);
    skipPluginButton.setEnabled(false);
    skipPluginButton.setButtonText(utf8("…"));
    pluginScanStatus.setText(
        utf8("Останавливаю текущий плагин…"),
        juce::dontSendNotification);
}

void MainComponent::setPluginSlot(int channel, int slot)
{
    const int id = pluginSlots[channel][slot].getSelectedId();
    if (id <= 0)
        return;

    pluginEditorWindows[channel][slot].reset();
    if (id == pluginNames.size() + 1)
    {
        juce::String error;
        if (engine.setPluginInChannelSlot(channel, slot, {}, error))
        {
            pluginSlotNames[channel][slot].clear();
            pluginSlots[channel][slot].setSelectedId(0, juce::dontSendNotification);
        }
        return;
    }

    juce::String error;
    if (!engine.setPluginInChannelSlot(channel, slot, pluginNames[id - 1], error))
    {
        pluginSlots[channel][slot].setSelectedId(
            pluginNames.indexOf(pluginSlotNames[channel][slot]) + 1,
            juce::dontSendNotification);
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
            utf8("Не удалось загрузить эффект"), error);
        return;
    }

    pluginSlotNames[channel][slot] = pluginNames[id - 1];
}

void MainComponent::showPluginEditor(int channel, int slot)
{
    if (channel < 0 || channel >= 4 || slot < 0 || slot >= 4)
        return;

    auto& window = pluginEditorWindows[channel][slot];
    if (window == nullptr)
    {
        auto* editor = engine.getPluginEditor(channel, slot);
        if (editor == nullptr)
        {
            juce::AlertWindow::showMessageBoxAsync(
                juce::MessageBoxIconType::InfoIcon,
                utf8("Окно плагина недоступно"),
                utf8("Сначала добавьте плагин в этот слот. Некоторые плагины не предоставляют окно настроек."));
            return;
        }
        window = std::make_unique<PluginEditorWindow>(*editor);
    }

    window->setVisible(true);
    window->toFront(true);
}

void MainComponent::timerCallback()
{
    status.setText(engine.getStatus(), juce::dontSendNotification);
    const auto meterProportion = [](float peak)
    {
        if (peak <= 0.000001f)
            return 0.0f;
        const auto db = juce::Decibels::gainToDecibels(peak);
        return juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 70.0f);
    };
    const auto now = juce::Time::getMillisecondCounterHiRes();
    const auto elapsed = lastMeterUpdateMs > 0.0
        ? juce::jlimit(0.001, 0.2, (now - lastMeterUpdateMs) / 1000.0)
        : 1.0 / 30.0;
    lastMeterUpdateMs = now;
    const auto updateMeter = [this, elapsed, &meterProportion](int channel, juce::Slider& slider)
    {
        const auto target = meterProportion(engine.getChannelPeak(channel));
        const auto timeConstant = target > smoothedMeterLevels[channel] ? 0.035 : 0.24;
        const auto smoothing = 1.0 - std::exp(-elapsed / timeConstant);
        smoothedMeterLevels[channel] += static_cast<float>(
            (target - smoothedMeterLevels[channel]) * smoothing);
        slider.getProperties().set("meterLevel", smoothedMeterLevels[channel]);
        slider.repaint();
    };
    for (int channel = 0; channel < 4; ++channel)
        updateMeter(channel, *channelFaders[channel]);
    repaint();
}

void MainComponent::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    if (source == &themeColourSelector)
    {
        if (themeColourSelector.getCurrentColour() != accent)
            applyAccentColour(themeColourSelector.getCurrentColour());
        return;
    }
    refreshInputDevices();
    refreshAudioDeviceOptions();
    engine.saveSettings();
}

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(bg);
    auto bounds = getLocalBounds();

    const int contentTop = settingsVisible ? 254 : 64;
    const int mixerHeight = mixerVisible
        ? juce::jlimit(280, 380, bounds.getHeight() - contentTop - 140)
        : 0;
    const int videoTop = contentTop + (mixerVisible ? mixerHeight + 12 : 0);
    const int videoHeight = juce::jmax(0, bounds.getHeight() - videoTop - 12);
    g.setColour(panel);
    g.fillRoundedRectangle(12.0f, 4.0f, (float) bounds.getWidth() - 24.0f, 54.0f, 13.0f);
    if (settingsVisible)
    {
        g.setColour(panel);
        g.fillRoundedRectangle(12.0f, 62.0f, (float) bounds.getWidth() - 24.0f, 180.0f, 13.0f);
    }
    g.setColour(juce::Colour(0xff070a10));
    g.fillRoundedRectangle(16.0f, (float) videoTop, (float) bounds.getWidth() - 32.0f,
                           (float) videoHeight, 14.0f);
    if (mixerVisible)
    {
        const int mixerY = contentTop;
        g.setColour(panel);
        g.fillRoundedRectangle(16.0f, (float) mixerY, (float) bounds.getWidth() - 32.0f,
                               (float) mixerHeight, 16.0f);
        g.setColour(panel2);
        const int innerX = 28, gap = 12;
        const int stripW = (bounds.getWidth() - 56 - gap * 3) / 4;
        for (int i = 0; i < 4; ++i)
            g.fillRoundedRectangle((float) (innerX + i * (stripW + gap)), (float) (mixerY + 12),
                                   (float) stripW, (float) mixerHeight - 24.0f, 10.0f);

        for (auto* fader : channelFaders)
            drawFaderScale(g, *fader);
    }
}

void MainComponent::resized()
{
    const int w = getWidth(), h = getHeight();
    const int buttonSize = 44;
    title.setBounds(22, 7, 174, 30);
    status.setBounds(22, 34, 174, 17);
    url.setBounds(205, 8, w - 605, 30);
    recordingResult.setBounds(205, 36, w - 605, 17);

    openButton.setBounds(w - 380, 7, buttonSize, buttonSize);
    mixerToggle.setBounds(w - 330, 7, buttonSize, buttonSize);
    recordButton.setBounds(w - 280, 7, buttonSize, buttonSize);
    monitorButton.setBounds(w - 230, 7, buttonSize, buttonSize);
    stopButton.setBounds(w - 180, 7, buttonSize, buttonSize);
    settingsToggle.setBounds(w - 74, 4, 54, 50);

    const int contentTop = settingsVisible ? 254 : 64;
    settingsViewport.setBounds(12, 62, 470, 180);
    settingsContent.setSize(470, 420);
    outputDeviceTitle.setBounds(18, 4, 180, 18);
    outputDeviceSelector.setBounds(12, 22, 446, 360);
    sampleRateTitle.setBounds(500, 66, 140, 18);
    sampleRateSelector.setBounds(500, 86, 140, 32);
    bufferSizeTitle.setBounds(652, 66, 260, 18);
    bufferSizeSelector.setBounds(652, 86, juce::jmin(260, w - 664), 32);
    addPluginDirectoryButton.setBounds(520, 130, 40, 36);
    scanPluginsButton.setBounds(568, 130, 40, 36);
    skipPluginButton.setBounds(616, 130, 40, 36);
    pluginScanStatus.setBounds(664, 130, juce::jmax(0, juce::jmin(300, w - 678)), 36);
    themeColourTitle.setBounds(w - 370, 66, 340, 18);
    themeColourSelector.setBounds(w - 370, 84, 340, 145);

    const int mixerHeight = mixerVisible
        ? juce::jlimit(280, 380, h - contentTop - 140)
        : 0;
    const int videoTop = contentTop + (mixerVisible ? mixerHeight + 12 : 0);
    const int videoHeight = juce::jmax(0, h - videoTop - 12);
    youtube.setBounds(24, videoTop + 8, w - 48, juce::jmax(0, videoHeight - 16));
    const int mixerY = contentTop;
    const int innerX = 28, gap = 12;
    const int stripW = (w - 56 - gap * 3) / 4;
    inputDeviceTitle.setBounds(innerX + 12, mixerY + 10, 62, 28);
    inputDevice.setBounds(innerX + 80, mixerY + 8, 340, 32);
    const int top = mixerY + 48;
    const int sliderY = mixerY + 132;
    const int sliderH = juce::jmax(120, mixerHeight - 158);
    const int strip1 = innerX;
    const int strip2 = innerX + stripW + gap;
    const int strip3 = innerX + (stripW + gap) * 2;
    const int strip4 = innerX + (stripW + gap) * 3;

    ch1Title.setBounds(strip1 + 12, top, stripW - 24, 24);
    inputChannelSelectors[0].setBounds(strip1 + 12, top + 25, stripW - 24, 28);
    gain1.setBounds(strip1 + 36, sliderY, 80, sliderH);
    muteButtons[0].setBounds(strip1 + 12, top + 58, 34, 28);
    soloButtons[0].setBounds(strip1 + 52, top + 58, 34, 28);
    for (int slot = 0; slot < 4; ++slot)
        pluginSlots[0][slot].setBounds(strip1 + 120, sliderY + 8 + slot * 38,
                                       stripW - 132, 30);

    ch2Title.setBounds(strip2 + 12, top, stripW - 24, 24);
    inputChannelSelectors[1].setBounds(strip2 + 12, top + 25, stripW - 24, 28);
    gain2.setBounds(strip2 + 36, sliderY, 80, sliderH);
    muteButtons[1].setBounds(strip2 + 12, top + 58, 34, 28);
    soloButtons[1].setBounds(strip2 + 52, top + 58, 34, 28);
    for (int slot = 0; slot < 4; ++slot)
        pluginSlots[1][slot].setBounds(strip2 + 120, sliderY + 8 + slot * 38,
                                       stripW - 132, 30);

    youtubeTitle.setBounds(strip3 + 12, top, stripW - 24, 24);
    muteButtons[2].setBounds(strip3 + 12, top + 26, 34, 28);
    soloButtons[2].setBounds(strip3 + 52, top + 26, 34, 28);
    youtubeVolume.setBounds(strip3 + 36, sliderY, 80, sliderH);
    for (int slot = 0; slot < 4; ++slot)
        pluginSlots[2][slot].setBounds(strip3 + 120, sliderY + 8 + slot * 38,
                                       stripW - 132, 30);

    masterTitle.setBounds(strip4 + 12, top, stripW - 24, 24);
    muteButtons[3].setBounds(strip4 + 12, top + 26, 34, 28);
    soloButtons[3].setBounds(strip4 + 52, top + 26, 34, 28);
    master.setBounds(strip4 + 36, sliderY, 80, sliderH);
    for (int slot = 0; slot < 4; ++slot)
        pluginSlots[3][slot].setBounds(strip4 + 120, sliderY + 8 + slot * 38,
                                       stripW - 132, 30);

    scanPluginsButton.setVisible(settingsVisible);
    skipPluginButton.setVisible(settingsVisible && pluginScanRunning);
    pluginScanStatus.setVisible(settingsVisible);
    settingsViewport.setVisible(settingsVisible);
    outputDeviceTitle.setVisible(true);
    outputDeviceSelector.setVisible(true);
    sampleRateTitle.setVisible(settingsVisible);
    sampleRateSelector.setVisible(settingsVisible);
    bufferSizeTitle.setVisible(settingsVisible);
    bufferSizeSelector.setVisible(settingsVisible);
    themeColourTitle.setVisible(settingsVisible);
    themeColourSelector.setVisible(settingsVisible);
    addPluginDirectoryButton.setVisible(settingsVisible);
    const bool show = mixerVisible;
    inputDeviceTitle.setVisible(show);
    inputDevice.setVisible(show);
    recordButton.setVisible(true);
    for (auto& selector : inputChannelSelectors)
        selector.setVisible(show);
    for (auto& button : muteButtons)
        button.setVisible(show);
    for (auto& button : soloButtons)
        button.setVisible(show);
    juce::Component* mixerControls[] = {
        &ch1Title, &gain1,
        &ch2Title, &gain2,
        &masterTitle, &master, &youtubeTitle, &youtubeVolume
    };
    for (auto* c : mixerControls)
        c->setVisible(show);
    for (auto& channelSlots : pluginSlots)
        for (auto& slot : channelSlots)
            slot.setVisible(show);
}
