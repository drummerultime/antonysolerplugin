#include "PluginEditor.h"

namespace
{
const juce::Colour ink(0xff10191a), panel(0xff172426), panelEdge(0xff43504d);
const juce::Colour ivory(0xffefe2c1), brass(0xffc9a66b), red(0xffbf4935), teal(0xff69908b);
const std::array<const char*, 26> parameterIds {
    "sensitivity", "impact", "tune", "decay", "grainSize", "stretch", "pitch", "reverse",
    "density", "scatter", "stutterChance", "stutterTime", "material", "crush",
    "feedback", "reverb", "tone", "filterEnv", "filterDecay", "resonance", "drive", "motion", "motionRate", "mix", "width", "output"
};
const std::array<const char*, 26> parameterNames {
    "SENSITIVITY", "IMPACT", "TUNE", "DECAY", "GRAIN", "STRETCH", "PITCH", "REVERSE",
    "DENSITY", "SCATTER", "STUTTER CHANCE", "STUTTER TIME", "MATERIAL", "CRUSH",
    "FEEDBACK", "SPACE", "CUTOFF", "ENV AMOUNT", "ENV DECAY", "RESONANCE", "DRIVE", "MOTION", "RATE", "MIX", "WIDTH", "OUTPUT"
};

void styleCombo(juce::ComboBox& combo)
{
    combo.setColour(juce::ComboBox::backgroundColourId, panel);
    combo.setColour(juce::ComboBox::textColourId, ivory);
    combo.setColour(juce::ComboBox::outlineColourId, brass.withAlpha(0.55f));
    combo.setColour(juce::ComboBox::arrowColourId, brass);
}
}

MetropolisAudioProcessorEditor::MetropolisAudioProcessorEditor(MetropolisAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    title.setText("METROPOLIS", juce::dontSendNotification);
    title.setFont(juce::Font(juce::FontOptions(34.0f, juce::Font::bold)));
    title.setColour(juce::Label::textColourId, ivory);
    addAndMakeVisible(title);
    subtitle.setText("PERCUSSION DESIGN SYSTEM  /  1927", juce::dontSendNotification);
    subtitle.setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));
    subtitle.setColour(juce::Label::textColourId, brass);
    addAndMakeVisible(subtitle);

    engineBox.addItemList({ "IMPACT", "RESONATOR", "REVERSE", "GRAIN", "STUTTER" }, 1);
    filterModeBox.addItemList({ "LOW PASS", "BAND PASS", "HIGH PASS" }, 1);
    sceneBox.addItem("CHOOSE A SCENE", 1);
    sceneBox.addItem("STEEL HIT", 2);
    sceneBox.addItem("CHROME BODY", 3);
    sceneBox.addItem("REVERSE RISE", 4);
    sceneBox.addItem("SKYLINE CLOUD", 5);
    sceneBox.addItem("CHROME STUTTER", 6);
    styleCombo(engineBox); styleCombo(sceneBox); styleCombo(filterModeBox);
    addAndMakeVisible(engineBox); addAndMakeVisible(sceneBox); addAndMakeVisible(filterModeBox);
    engineAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(audioProcessor.parameters, "engine", engineBox);
    filterModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(audioProcessor.parameters, "filterMode", filterModeBox);
    filterModeLabel.setText("DYNAMIC FILTER", juce::dontSendNotification);
    filterModeLabel.setFont(juce::Font(juce::FontOptions(8.5f, juce::Font::bold)));
    filterModeLabel.setColour(juce::Label::textColourId, brass);
    addAndMakeVisible(filterModeLabel);

    for (auto* button : { &freezeButton, &syncButton })
    {
        button->setColour(juce::ToggleButton::textColourId, ivory);
        button->setColour(juce::ToggleButton::tickColourId, red);
        button->setColour(juce::ToggleButton::tickDisabledColourId, panelEdge);
        addAndMakeVisible(*button);
    }
    freezeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(audioProcessor.parameters, "freeze", freezeButton);
    syncAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(audioProcessor.parameters, "sync", syncButton);

    randomLabel.setText("CHANCE", juce::dontSendNotification);
    randomLabel.setFont(juce::Font(juce::FontOptions(8.5f, juce::Font::bold)));
    randomLabel.setColour(juce::Label::textColourId, brass);
    randomLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(randomLabel);
    randomAmount.setSliderStyle(juce::Slider::LinearHorizontal);
    randomAmount.setRange(0.0, 1.0, 0.01);
    randomAmount.setValue(0.35);
    randomAmount.setTextBoxStyle(juce::Slider::TextBoxRight, false, 36, 18);
    randomAmount.setColour(juce::Slider::trackColourId, red);
    randomAmount.setColour(juce::Slider::thumbColourId, ivory);
    randomAmount.setColour(juce::Slider::textBoxTextColourId, ivory);
    randomAmount.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible(randomAmount);
    randomButton.setColour(juce::TextButton::buttonColourId, red.darker(0.25f));
    randomButton.setColour(juce::TextButton::buttonOnColourId, red);
    randomButton.setColour(juce::TextButton::textColourOffId, ivory);
    randomButton.onClick = [this] { audioProcessor.randomize((float) randomAmount.getValue()); sceneBox.setSelectedId(1, juce::dontSendNotification); };
    addAndMakeVisible(randomButton);

    for (size_t i = 0; i < knobs.size(); ++i)
    {
        knobs[i] = std::make_unique<juce::Slider>();
        knobs[i]->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        knobs[i]->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 62, 18);
        knobs[i]->setColour(juce::Slider::rotarySliderFillColourId, brass);
        knobs[i]->setColour(juce::Slider::rotarySliderOutlineColourId, panelEdge);
        knobs[i]->setColour(juce::Slider::thumbColourId, ivory);
        knobs[i]->setColour(juce::Slider::textBoxTextColourId, ivory);
        knobs[i]->setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        addAndMakeVisible(*knobs[i]);
        labels[i] = std::make_unique<juce::Label>();
        labels[i]->setText(parameterNames[i], juce::dontSendNotification);
        labels[i]->setJustificationType(juce::Justification::centred);
        labels[i]->setFont(juce::Font(juce::FontOptions(8.5f, juce::Font::bold)));
        labels[i]->setColour(juce::Label::textColourId, ivory.withAlpha(0.72f));
        addAndMakeVisible(*labels[i]);
        attachments[i] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(audioProcessor.parameters, parameterIds[i], *knobs[i]);
    }
    sceneBox.onChange = [this]
    {
        const int selected = sceneBox.getSelectedId();
        if (selected >= 2) audioProcessor.loadScene(selected - 2);
    };
    setResizable(true, true);
    setResizeLimits(1000, 590, 1830, 1080);
    getConstrainer()->setFixedAspectRatio(1220.0 / 720.0);
    setSize(1220, 720);
    startTimerHz(24);
}

void MetropolisAudioProcessorEditor::timerCallback()
{
    inputLevel = audioProcessor.inputMeter.load();
    outputLevel = audioProcessor.outputMeter.load();
    voices = audioProcessor.activeVoices.load();
    repaint();
}

void MetropolisAudioProcessorEditor::paint(juce::Graphics& g)
{
    const float uiScale = juce::jmin((float) getWidth() / 1220.0f, (float) getHeight() / 720.0f);
    const float offsetX = ((float) getWidth() - 1220.0f * uiScale) * 0.5f;
    const float offsetY = ((float) getHeight() - 720.0f * uiScale) * 0.5f;
    g.addTransform(juce::AffineTransform::scale(uiScale).translated(offsetX, offsetY));
    g.fillAll(ink);
    g.setColour(juce::Colour(0xff182224));
    for (int x = 0; x < 1220; x += 48) g.drawVerticalLine(x, 0.0f, 720.0f);
    for (int y = 0; y < 720; y += 48) g.drawHorizontalLine(y, 0.0f, 1220.0f);
    g.setColour(brass.withAlpha(0.6f));
    g.drawRect(18.0f, 16.0f, 1184.0f, 688.0f, 1.0f);
    g.drawRect(23.0f, 21.0f, 1174.0f, 678.0f, 1.0f);

    // Art Deco sunburst and stepped skyline: a geometric homage to the 1927 city.
    const float cx = 366.0f, cy = 72.0f;
    g.setColour(brass.withAlpha(0.72f));
    for (int i = 0; i < 9; ++i)
    {
        const float x = 266.0f + i * 24.0f;
        g.drawLine(cx, cy, x, 112.0f, i % 2 == 0 ? 1.25f : 0.75f);
    }
    g.setColour(teal.withAlpha(0.85f));
    juce::Path towers;
    towers.startNewSubPath(258.0f, 111.0f);
    towers.lineTo(258.0f, 94.0f); towers.lineTo(280.0f, 94.0f); towers.lineTo(280.0f, 75.0f);
    towers.lineTo(300.0f, 75.0f); towers.lineTo(300.0f, 100.0f); towers.lineTo(320.0f, 100.0f);
    towers.lineTo(320.0f, 57.0f); towers.lineTo(342.0f, 57.0f); towers.lineTo(342.0f, 98.0f);
    towers.lineTo(365.0f, 98.0f); towers.lineTo(365.0f, 46.0f); towers.lineTo(389.0f, 46.0f);
    towers.lineTo(389.0f, 96.0f); towers.lineTo(410.0f, 96.0f); towers.lineTo(410.0f, 69.0f);
    towers.lineTo(432.0f, 69.0f); towers.lineTo(432.0f, 111.0f);
    g.strokePath(towers, juce::PathStrokeType(1.6f));
    g.setColour(red.withAlpha(0.8f));
    g.drawLine(48.0f, 112.0f, 1172.0f, 112.0f, 1.2f);

    const juce::Rectangle<float> meter(34.0f, 126.0f, 1152.0f, 72.0f);
    g.setColour(panel); g.fillRoundedRectangle(meter, 4.0f);
    g.setColour(panelEdge); g.drawRoundedRectangle(meter, 4.0f, 1.0f);
    g.setFont(juce::Font(juce::FontOptions(9.0f, juce::Font::bold)));
    g.setColour(brass); g.drawText("INPUT", 52, 139, 70, 16, juce::Justification::left);
    g.drawText("OUTPUT", 52, 171, 70, 16, juce::Justification::left);
    const float inW = juce::jlimit(0.0f, 1.0f, inputLevel * 2.6f) * 1030.0f;
    const float outW = juce::jlimit(0.0f, 1.0f, outputLevel * 2.6f) * 1030.0f;
    g.setColour(teal); g.fillRect(128.0f, 140.0f, inW, 8.0f);
    g.setColour(red); g.fillRect(128.0f, 172.0f, outW, 8.0f);
    g.setColour(ivory.withAlpha(0.72f));
    g.drawText(juce::String("ACTIVE VOICES  ") + juce::String(voices), 1000, 151, 150, 18, juce::Justification::right);

    const juce::Rectangle<int> source(30, 218, 270, 450), shape(314, 218, 424, 450), world(752, 218, 438, 450);
    for (const auto& area : { source, shape, world })
    {
        g.setColour(panel); g.fillRoundedRectangle(area.toFloat(), 5.0f);
        g.setColour(panelEdge); g.drawRoundedRectangle(area.toFloat(), 5.0f, 1.2f);
        g.setColour(brass.withAlpha(0.32f)); g.drawLine((float) area.getX() + 14.0f, (float) area.getY() + 34.0f, (float) area.getRight() - 14.0f, (float) area.getY() + 34.0f, 1.0f);
    }
    g.setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold)));
    g.setColour(brass); g.drawText("THE STRIKE", source.withHeight(30), juce::Justification::centred);
    g.drawText("ENGINE SHAPING", shape.withHeight(30), juce::Justification::centred);
    g.drawText("THE CITY / SPACE", world.withHeight(30), juce::Justification::centred);
    g.setFont(juce::Font(juce::FontOptions(8.0f, juce::Font::plain)));
    g.setColour(teal.withAlpha(0.82f));
    g.drawText("DETECT  •  SYNTHESIZE  •  TRANSFORM", 35, 681, 450, 16, juce::Justification::left);
    g.setColour(brass.withAlpha(0.45f));
    for (int i = 0; i < 5; ++i) g.drawLine(1090.0f + i * 12.0f, 690.0f, 1170.0f, 682.0f - i * 2.0f, 0.8f);
}

void MetropolisAudioProcessorEditor::resized()
{
    const float scale = juce::jmin((float) getWidth() / 1220.0f, (float) getHeight() / 720.0f);
    const float offsetX = ((float) getWidth() - 1220.0f * scale) * 0.5f;
    const float offsetY = ((float) getHeight() - 720.0f * scale) * 0.5f;
    auto bounds = [scale, offsetX, offsetY](int x, int y, int w, int h)
    {
        return juce::Rectangle<int>(juce::roundToInt(offsetX + x * scale), juce::roundToInt(offsetY + y * scale),
                                    juce::roundToInt(w * scale), juce::roundToInt(h * scale));
    };
    title.setBounds(bounds(42, 24, 310, 48));
    title.setFont(juce::Font(juce::FontOptions(34.0f * scale, juce::Font::bold)));
    subtitle.setBounds(bounds(45, 71, 310, 24));
    subtitle.setFont(juce::Font(juce::FontOptions(10.0f * scale, juce::Font::bold)));
    engineBox.setBounds(bounds(465, 42, 165, 34));
    sceneBox.setBounds(bounds(649, 42, 185, 34));
    filterModeLabel.setBounds(bounds(768, 250, 94, 24));
    filterModeLabel.setFont(juce::Font(juce::FontOptions(8.5f * scale, juce::Font::bold)));
    filterModeBox.setBounds(bounds(862, 248, 312, 28));
    freezeButton.setBounds(bounds(844, 42, 78, 34));
    syncButton.setBounds(bounds(924, 42, 72, 34));
    randomLabel.setBounds(bounds(1002, 28, 80, 13));
    randomAmount.setBounds(bounds(1002, 42, 105, 34));
    randomButton.setBounds(bounds(1113, 42, 82, 34));

    auto place = [this, &bounds, scale](int index, int x, int y, int w, int h)
    {
        labels[(size_t) index]->setBounds(bounds(x, y, w, 18));
        labels[(size_t) index]->setFont(juce::Font(juce::FontOptions(8.5f * scale, juce::Font::bold)));
        knobs[(size_t) index]->setBounds(bounds(x + 2, y + 18, w - 4, h - 18));
        knobs[(size_t) index]->setTextBoxStyle(juce::Slider::TextBoxBelow, false,
            juce::jmax(48, juce::roundToInt(62.0f * scale)), juce::jmax(14, juce::roundToInt(18.0f * scale)));
    };
    const std::array<int, 3> rowY { 264, 392, 520 };
    for (int i = 0; i < 4; ++i) place(i, 45 + (i % 2) * 120, 270 + (i / 2) * 174, 110, 148);
    for (int i = 0; i < 10; ++i) place(i + 4, 326 + (i % 4) * 100, rowY[(size_t) (i / 4)], 94, 116);
    const std::array<int, 3> worldRows { 286, 404, 522 };
    for (int i = 0; i < 12; ++i) place(i + 14, 764 + (i % 4) * 102, worldRows[(size_t) (i / 4)], 94, 108);
}
