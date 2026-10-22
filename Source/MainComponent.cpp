#include "MainComponent.h"
#include <functional>
#include <utility>

namespace
{
    class PluginScanJob final : public juce::ThreadPoolJob
    {
    public:
        PluginScanJob(KaraokeAudioEngine& audioEngine, std::function<void()> completion)
            : ThreadPoolJob("KaraokeDesk plugin scan"), engine(audioEngine), onComplete(std::move(completion)) {}
        JobStatus runJob() override
        {
            engine.scanPlugins();
            juce::MessageManager::callAsync([callback = std::move(onComplete)] { if (callback) callback(); });
            return jobHasFinished;
        }
    private:
        KaraokeAudioEngine& engine;
        std::function<void()> onComplete;
    };

    const juce::Colour bg(0xff0b0e14), panel(0xff151a24), panel2(0xff1d2431),
        accent(0xff6d5dfc), cyan(0xff37d6c5), text(0xfff3f5fb), muted(0xff9aa6ba);
    const juce::Font uiFont(float size, bool bold = false)
    {
        return juce::Font(juce::FontOptions(size, bold ? juce::Font::bold : juce::Font::plain)
                              .withName("Arial"));
    }
}

MainComponent::MainComponent()
{
    setSize(1440, 900);

    title.setText("KARAOKE DESK", juce::dontSendNotification);
    title.setFont(uiFont(25.0f, true));
    title.setColour(juce::Label::textColourId, text);
    addAndMakeVisible(title);

    url.setTextToShowWhenEmpty("Вставь ссылку на YouTube-караоке…", muted);
    url.setColour(juce::TextEditor::backgroundColourId, panel2);
    url.setColour(juce::TextEditor::textColourId, text);
    url.setColour(juce::TextEditor::outlineColourId, panel2);
    url.setFont(uiFont(14.0f));
    addAndMakeVisible(url);

    for (auto* b : { &openButton, &mixerToggle, &settingsToggle, &scanPluginsButton, &startButton, &stopButton,
                     &add1, &add2, &clear1, &clear2 })
    {
        styleButton(*b);
        addAndMakeVisible(*b);
    }
    openButton.onClick = [this] { openYoutube(); };
    settingsToggle.onClick = [this]
    {
        settingsVisible = !settingsVisible;
        settingsToggle.setButtonText(settingsVisible ? "СКРЫТЬ НАСТРОЙКИ" : "НАСТРОЙКИ");
        resized(); repaint();
    };
    scanPluginsButton.onClick = [this] { scanPluginsInBackground(); };
    pluginScanStatus.setText("Поиск плагинов ещё не запускался", juce::dontSendNotification);
    pluginScanStatus.setColour(juce::Label::textColourId, muted);
    pluginScanStatus.setFont(uiFont(12.0f));
    addAndMakeVisible(pluginScanStatus);
    scanPluginsButton.setVisible(false);
    pluginScanStatus.setVisible(false);
    mixerToggle.onClick = [this]
    {
        mixerVisible = !mixerVisible;
        mixerToggle.setButtonText(mixerVisible ? "СКРЫТЬ МИКШЕР" : "ПОКАЗАТЬ МИКШЕР");
        resized();
        repaint();
    };
    startButton.onClick = [this]
    {
        engine.start();
        status.setText(engine.getStatus(), juce::dontSendNotification);
    };
    stopButton.onClick = [this]
    {
        engine.stop();
        status.setText(engine.getStatus(), juce::dontSendNotification);
    };

    status.setText("Аудио остановлено", juce::dontSendNotification);
    status.setColour(juce::Label::textColourId, muted);
    status.setFont(uiFont(12.0f));
    addAndMakeVisible(status);

    auto setupLabel = [this](juce::Label& l, const juce::String& value, float size)
    {
        l.setText(value, juce::dontSendNotification);
        l.setColour(juce::Label::textColourId, text);
        l.setFont(uiFont(size, true));
        addAndMakeVisible(l);
    };
    setupLabel(ch1Title, "МИКРОФОН 1", 15.0f);
    setupLabel(ch2Title, "МИКРОФОН 2", 15.0f);
    setupLabel(masterTitle, "МАСТЕР", 15.0f);
    setupLabel(youtubeTitle, "ГРОМКОСТЬ YOUTUBE", 15.0f);

    styleSlider(gain1, 0.8);
    styleSlider(gain2, 0.8);
    styleSlider(master, 0.65);
    styleSlider(youtubeVolume, 0.8);
    gain1.onValueChange = [this] { engine.setChannelGain(0, (float) gain1.getValue()); };
    gain2.onValueChange = [this] { engine.setChannelGain(1, (float) gain2.getValue()); };
    master.onValueChange = [this] { engine.setMasterGain((float) master.getValue()); };
    youtubeVolume.onValueChange = [this] { setYoutubeVolume(); };

    pluginNames = engine.getAvailablePluginNames();
    for (int i = 0; i < pluginNames.size(); ++i)
    {
        plugin1.addItem(pluginNames[i], i + 1);
        plugin2.addItem(pluginNames[i], i + 1);
    }
    for (auto* combo : { &plugin1, &plugin2 })
    {
        combo->setColour(juce::ComboBox::backgroundColourId, panel2);
        combo->setColour(juce::ComboBox::textColourId, text);
        combo->setColour(juce::ComboBox::outlineColourId, muted);
        addAndMakeVisible(*combo);
    }
    add1.onClick = [this] { addPlugin(0); };
    add2.onClick = [this] { addPlugin(1); };
    clear1.onClick = [this] { engine.clearChannel(0); };
    clear2.onClick = [this] { engine.clearChannel(1); };

    for (auto* l : { &meter1, &meter2, &meterMaster })
    {
        l->setColour(juce::Label::textColourId, cyan);
        l->setFont(uiFont(11.0f));
        addAndMakeVisible(*l);
    }

    youtube.setVisible(true);
    addAndMakeVisible(youtube);
    startTimerHz(4);
}

MainComponent::~MainComponent() { stopTimer(); pluginScanPool.removeAllJobs(true, -1); engine.stop(); }

void MainComponent::styleButton(juce::TextButton& b)
{
    b.setColour(juce::TextButton::buttonColourId, accent);
    b.setColour(juce::TextButton::textColourOffId, text);
    b.setColour(juce::TextButton::buttonOnColourId, cyan);
}

void MainComponent::styleSlider(juce::Slider& s, double initialValue)
{
    s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 64, 20);
    s.setRange(0.0, 1.0, 0.01);
    s.setValue(initialValue, juce::dontSendNotification);
    s.setColour(juce::Slider::rotarySliderFillColourId, accent);
    s.setColour(juce::Slider::thumbColourId, cyan);
    s.setColour(juce::Slider::textBoxTextColourId, text);
    s.setColour(juce::Slider::textBoxBackgroundColourId, panel2);
    s.setColour(juce::Slider::textBoxOutlineColourId, panel2);
    addAndMakeVisible(s);
}

void MainComponent::openYoutube()
{
    auto s = url.getText().trim();
    if (s.isEmpty()) return;
    if (!s.startsWithIgnoreCase("https://") && !s.startsWithIgnoreCase("http://"))
        s = "https://" + s;
    youtube.goToURL(s);
}

void MainComponent::setYoutubeVolume()
{
    const auto volume = juce::String(youtubeVolume.getValue(), 2);
    // Adjust the HTML5 video element inside the embedded YouTube page, when available.
    // The volume control only affects YouTube; microphone/master levels remain independent.
    youtube.evaluateJavascript("(()=>{const v=document.querySelector('video');if(v){v.volume=" + volume + ";}})();");
}

void MainComponent::scanPluginsInBackground()
{
    if (pluginScanRunning) return;
    pluginScanRunning = true;
    scanPluginsButton.setEnabled(false);
    pluginScanStatus.setText("Идёт поиск VST3 / AU…", juce::dontSendNotification);
    juce::Component::SafePointer<MainComponent> safeThis(this);
    pluginScanPool.addJob(new PluginScanJob(engine, [safeThis]
    {
        if (safeThis == nullptr) return;
        safeThis->pluginNames = safeThis->engine.getAvailablePluginNames();
        safeThis->plugin1.clear(juce::dontSendNotification);
        safeThis->plugin2.clear(juce::dontSendNotification);
        for (int i = 0; i < safeThis->pluginNames.size(); ++i)
        {
            safeThis->plugin1.addItem(safeThis->pluginNames[i], i + 1);
            safeThis->plugin2.addItem(safeThis->pluginNames[i], i + 1);
        }
        safeThis->pluginScanRunning = false;
        safeThis->scanPluginsButton.setEnabled(true);
        safeThis->pluginScanStatus.setText("Найдено плагинов: " + juce::String(safeThis->pluginNames.size()), juce::dontSendNotification);
    }), true);
}

void MainComponent::addPlugin(int channel)
{
    const int id = channel == 0 ? plugin1.getSelectedId() : plugin2.getSelectedId();
    if (id <= 0 || id > pluginNames.size())
    {
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
            "Эффект не выбран", "Открой «Настройки» и нажми «Сканировать VST / AU», чтобы найти установленные плагины.");
        return;
    }
    juce::String error;
    if (!engine.addPluginToChannel(channel, pluginNames[id - 1], error))
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
            "Не удалось загрузить эффект", error);
}

void MainComponent::timerCallback()
{
    status.setText(engine.getStatus(), juce::dontSendNotification);
    meter1.setText("ВХОД  " + juce::String(engine.getChannelPeak(0), 3), juce::dontSendNotification);
    meter2.setText("ВХОД  " + juce::String(engine.getChannelPeak(1), 3), juce::dontSendNotification);
    meterMaster.setText("ВЫХОД  " + juce::String(engine.getMasterPeak(), 3), juce::dontSendNotification);
}

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(bg);
    auto bounds = getLocalBounds();
    g.setColour(panel);
    g.fillRoundedRectangle(16.0f, 68.0f, (float) bounds.getWidth() - 32.0f, 58.0f, 12.0f);
    g.setColour(accent);
    g.fillRoundedRectangle(16.0f, 68.0f, 5.0f, 58.0f, 2.0f);

    const int contentTop = settingsVisible ? 196 : 140;
    const int mixerHeight = mixerVisible ? 270 : 0;
    const int videoHeight = juce::jmax(120, bounds.getHeight() - contentTop - mixerHeight - 18);
    g.setColour(panel);
    g.fillRoundedRectangle(16.0f, (float) contentTop, (float) bounds.getWidth() - 32.0f,
                           (float) videoHeight, 14.0f);
    if (mixerVisible)
    {
        const int mixerY = bounds.getHeight() - mixerHeight - 8;
        g.setColour(panel);
        g.fillRoundedRectangle(16.0f, (float) mixerY, (float) bounds.getWidth() - 32.0f,
                               (float) mixerHeight, 14.0f);
        g.setColour(panel2);
        const int innerX = 28, gap = 12;
        const int stripW = (bounds.getWidth() - 56 - gap * 3) / 4;
        for (int i = 0; i < 4; ++i)
            g.fillRoundedRectangle((float) (innerX + i * (stripW + gap)), (float) (mixerY + 12),
                                   (float) stripW, (float) mixerHeight - 24.0f, 10.0f);
    }
}

void MainComponent::resized()
{
    const int w = getWidth(), h = getHeight();
    title.setBounds(22, 12, 300, 42);
    status.setBounds(w - 430, 14, 210, 24);
    startButton.setBounds(w - 214, 8, 190, 30);
    stopButton.setBounds(w - 214, 42, 190, 28);

    url.setBounds(30, 80, w - 610, 34);
    openButton.setBounds(w - 570, 79, 145, 36);
    mixerToggle.setBounds(w - 415, 79, 185, 36);
    settingsToggle.setBounds(w - 220, 79, 190, 36);
    scanPluginsButton.setBounds(30, 132, 210, 32);
    pluginScanStatus.setBounds(252, 132, w - 280, 32);

    const int settingsHeight = settingsVisible ? 56 : 0;
    const int contentTop = 148 + settingsHeight;
    const int mixerHeight = mixerVisible ? 270 : 0;
    const int videoHeight = juce::jmax(120, h - contentTop - mixerHeight - 22);
    youtube.setBounds(24, contentTop + 8, w - 48, videoHeight - 16);

    const int mixerY = h - mixerHeight - 8;
    const int innerX = 28, gap = 12;
    const int stripW = (w - 56 - gap * 3) / 4;
    const int top = mixerY + 18;
    const int sliderY = mixerY + 52;
    const int sliderH = 145;
    const int strip1 = innerX;
    const int strip2 = innerX + stripW + gap;
    const int strip3 = innerX + (stripW + gap) * 2;
    const int strip4 = innerX + (stripW + gap) * 3;

    ch1Title.setBounds(strip1 + 12, top, stripW - 24, 24);
    gain1.setBounds(strip1 + 12, sliderY, 92, sliderH);
    plugin1.setBounds(strip1 + 112, sliderY + 12, stripW - 128, 28);
    add1.setBounds(strip1 + 112, sliderY + 50, (stripW - 136) / 2, 28);
    clear1.setBounds(strip1 + 120 + (stripW - 136) / 2, sliderY + 50, (stripW - 136) / 2, 28);
    meter1.setBounds(strip1 + 112, sliderY + 88, stripW - 128, 22);

    ch2Title.setBounds(strip2 + 12, top, stripW - 24, 24);
    gain2.setBounds(strip2 + 12, sliderY, 92, sliderH);
    plugin2.setBounds(strip2 + 112, sliderY + 12, stripW - 128, 28);
    add2.setBounds(strip2 + 112, sliderY + 50, (stripW - 136) / 2, 28);
    clear2.setBounds(strip2 + 120 + (stripW - 136) / 2, sliderY + 50, (stripW - 136) / 2, 28);
    meter2.setBounds(strip2 + 112, sliderY + 88, stripW - 128, 22);

    masterTitle.setBounds(strip3 + 12, top, stripW - 24, 24);
    master.setBounds(strip3 + (stripW - 100) / 2, sliderY, 100, sliderH);
    meterMaster.setBounds(strip3 + 16, sliderY + 118, stripW - 32, 22);

    youtubeTitle.setBounds(strip4 + 12, top, stripW - 24, 24);
    youtubeVolume.setBounds(strip4 + (stripW - 100) / 2, sliderY, 100, sliderH);

    scanPluginsButton.setVisible(settingsVisible);
    pluginScanStatus.setVisible(settingsVisible);
    const bool show = mixerVisible;
    juce::Component* mixerControls[] = {
        &ch1Title, &gain1, &plugin1, &add1, &clear1, &meter1,
        &ch2Title, &gain2, &plugin2, &add2, &clear2, &meter2,
        &masterTitle, &master, &meterMaster, &youtubeTitle, &youtubeVolume
    };
    for (auto* c : mixerControls)
        c->setVisible(show);
}
