#include "MainComponent.h"

namespace
{
    const juce::Colour bg(0xff0b0e14), panel(0xff151a24), panel2(0xff1d2431),
        accent(0xff6d5dfc), cyan(0xff37d6c5), text(0xfff3f5fb), muted(0xff9aa6ba);
}

MainComponent::MainComponent()
{
    setSize(1440, 900);
    title.setText("KARAOKE DESK", juce::dontSendNotification);
    title.setFont(juce::Font(juce::FontOptions(26.0f, juce::Font::bold)));
    title.setColour(juce::Label::textColourId, text);
    addAndMakeVisible(title);

    url.setTextToShowWhenEmpty("Вставь ссылку на YouTube-караоке…" , muted);
    url.setColour(juce::TextEditor::backgroundColourId, panel2);
    url.setColour(juce::TextEditor::textColourId, text);
    url.setColour(juce::TextEditor::outlineColourId, panel2);
    addAndMakeVisible(url);
    styleButton(openButton); addAndMakeVisible(openButton);
    openButton.onClick = [this] { openYoutube(); };

    youtube.setVisible(true);
    addAndMakeVisible(youtube);

    styleButton(startButton); styleButton(stopButton);
    addAndMakeVisible(startButton); addAndMakeVisible(stopButton);
    startButton.onClick = [this] { engine.start(); status.setText(engine.getStatus(), juce::dontSendNotification); };
    stopButton.onClick = [this] { engine.stop(); status.setText(engine.getStatus(), juce::dontSendNotification); };

    status.setText("Готово · При необходимости выбери EVO 4 в настройках звука macOS", juce::dontSendNotification);
    status.setColour(juce::Label::textColourId, muted);
    status.setFont(juce::Font(juce::FontOptions(12.0f)));
    addAndMakeVisible(status);

    auto setupLabel = [this](juce::Label& l, const juce::String& t, float size)
    {
        l.setText(t, juce::dontSendNotification);
        l.setColour(juce::Label::textColourId, text);
        l.setFont(juce::Font(juce::FontOptions(size, juce::Font::bold)));
        addAndMakeVisible(l);
    };
    setupLabel(ch1Title, "МИКРОФОН 1 / ВОКАЛ", 16.0f);
    setupLabel(ch2Title, "МИКРОФОН 2 / ВОКАЛ", 16.0f);
    setupLabel(masterTitle, "МАСТЕР-ВЫХОД", 16.0f);
    setupLabel(pluginsTitle, "ЭФФЕКТЫ VST3 / AU", 12.0f);

    for (auto* s : { &gain1, &gain2, &master })
    {
        s->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        s->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 72, 20);
        s->setRange(0.0, 1.0, 0.01);
        s->setColour(juce::Slider::rotarySliderFillColourId, accent);
        s->setColour(juce::Slider::thumbColourId, cyan);
        s->setColour(juce::Slider::textBoxTextColourId, text);
        s->setColour(juce::Slider::textBoxBackgroundColourId, panel2);
        addAndMakeVisible(*s);
    }
    gain1.setValue(0.8); gain2.setValue(0.8); master.setValue(0.65);
    gain1.onValueChange = [this] { engine.setChannelGain(0, (float) gain1.getValue()); };
    gain2.onValueChange = [this] { engine.setChannelGain(1, (float) gain2.getValue()); };
    master.onValueChange = [this] { engine.setMasterGain((float) master.getValue()); };

    pluginNames = engine.getAvailablePluginNames();
    for (int i = 0; i < pluginNames.size(); ++i) { plugin1.addItem(pluginNames[i], i + 1); plugin2.addItem(pluginNames[i], i + 1); }
    addAndMakeVisible(plugin1); addAndMakeVisible(plugin2);
    styleButton(add1); styleButton(add2); styleButton(clear1); styleButton(clear2);
    addAndMakeVisible(add1); addAndMakeVisible(add2); addAndMakeVisible(clear1); addAndMakeVisible(clear2);
    add1.onClick = [this] { addPlugin(0); };
    add2.onClick = [this] { addPlugin(1); };
    clear1.onClick = [this] { engine.clearChannel(0); };
    clear2.onClick = [this] { engine.clearChannel(1); };

    for (auto* l : { &meter1, &meter2, &meterMaster })
    {
        l->setColour(juce::Label::textColourId, cyan);
        l->setFont(juce::Font(juce::FontOptions(11.0f)));
        addAndMakeVisible(*l);
    }
    startTimerHz(10);
}

MainComponent::~MainComponent() { engine.stop(); }

void MainComponent::styleButton(juce::TextButton& b)
{
    b.setColour(juce::TextButton::buttonColourId, accent);
    b.setColour(juce::TextButton::textColourOffId, text);
    b.setColour(juce::TextButton::buttonOnColourId, cyan);
}

void MainComponent::openYoutube()
{
    auto s = url.getText().trim();
    if (s.isEmpty()) return;
    if (!s.startsWithIgnoreCase("https://") && !s.startsWithIgnoreCase("http://"))
        s = "https://" + s;
    // JUCE WebBrowserComponent uses the system web view. YouTube may require login/cookies,
    // and some playback paths can be blocked by the site's embedding policy.
    youtube.goToURL(s);
}

void MainComponent::addPlugin(int channel)
{
    const int id = channel == 0 ? plugin1.getSelectedId() : plugin2.getSelectedId();
    if (id <= 0 || id > pluginNames.size())
    {
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, "Выбери эффект", "Установи совместимый AU или VST3-плагин и перезапусти приложение.");
        return;
    }
    juce::String error;
    if (!engine.addPluginToChannel(channel, pluginNames[id - 1], error))
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Не удалось загрузить плагин", error);
}

void MainComponent::timerCallback()
{
    status.setText(engine.getStatus(), juce::dontSendNotification);
    meter1.setText("ВХОД 1  " + juce::String(engine.getChannelPeak(0), 3), juce::dontSendNotification);
    meter2.setText("ВХОД 2  " + juce::String(engine.getChannelPeak(1), 3), juce::dontSendNotification);
    meterMaster.setText("ВЫХОД  " + juce::String(engine.getMasterPeak(), 3), juce::dontSendNotification);
}

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(bg);
    auto r = getLocalBounds();
    g.setColour(panel);
    g.fillRoundedRectangle(18.0f, 72.0f, (float) getWidth() - 36.0f, 58.0f, 14.0f);
    g.fillRoundedRectangle(18.0f, 144.0f, (float) getWidth() * 0.62f - 24.0f, (float) getHeight() - 166.0f, 16.0f);
    g.fillRoundedRectangle((float) getWidth() * 0.64f, 144.0f, (float) getWidth() * 0.36f - 28.0f, (float) getHeight() - 166.0f, 16.0f);
    g.setColour(panel2);
    g.fillRoundedRectangle((float) getWidth() * 0.64f + 16.0f, 174.0f, (float) getWidth() * 0.36f - 60.0f, 210.0f, 12.0f);
    g.fillRoundedRectangle((float) getWidth() * 0.64f + 16.0f, 400.0f, (float) getWidth() * 0.36f - 60.0f, 210.0f, 12.0f);
    g.setColour(accent);
    g.fillRoundedRectangle(18.0f, 72.0f, 5.0f, 58.0f, 2.0f);
    g.setColour(muted);
    g.setFont(12.0f);
    g.drawText("ВИДЕО КАРАОКЕ С YOUTUBE", 34, 150, 360, 20, juce::Justification::left);
    g.drawText("МИКШЕР МИКРОФОНОВ", (int)(getWidth() * 0.64f + 18), 150, 280, 20, juce::Justification::left);
}

void MainComponent::resized()
{
    auto w = getWidth(), h = getHeight();
    title.setBounds(24, 18, 340, 42);
    url.setBounds(34, 84, w - 330, 34);
    openButton.setBounds(w - 278, 83, 150, 36);
    status.setBounds(w - 122, 24, 100, 28);
    startButton.setBounds(w - 122, 56, 100, 28);
    stopButton.setBounds(w - 122, 87, 100, 28);

    auto leftX = 28, leftW = (int)(w * 0.62f) - 42;
    youtube.setBounds(leftX, 174, leftW, h - 204);

    auto rx = (int)(w * 0.64f) + 20;
    ch1Title.setBounds(rx + 16, 180, 230, 26);
    gain1.setBounds(rx + 10, 205, 115, 145);
    plugin1.setBounds(rx + 135, 225, w - rx - 170, 30);
    add1.setBounds(rx + 135, 265, 105, 30);
    clear1.setBounds(rx + 248, 265, 100, 30);
    meter1.setBounds(rx + 135, 305, 240, 24);

    ch2Title.setBounds(rx + 16, 406, 230, 26);
    gain2.setBounds(rx + 10, 430, 115, 145);
    plugin2.setBounds(rx + 135, 450, w - rx - 170, 30);
    add2.setBounds(rx + 135, 490, 105, 30);
    clear2.setBounds(rx + 248, 490, 100, 30);
    meter2.setBounds(rx + 135, 530, 240, 24);

    masterTitle.setBounds(rx + 16, 628, 220, 24);
    master.setBounds(rx + 10, 652, 110, 135);
    meterMaster.setBounds(rx + 135, 690, 240, 24);
    pluginsTitle.setBounds(rx + 135, 650, 200, 20);
}
