#include "LinkPanel.h"

namespace alea::gui
{
using namespace alea::link;

static juce::String noteName (int root)
{
    static const char* n[12] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return n[((root % 12) + 12) % 12];
}

LinkPanel::LinkPanel (Link& l, juce::AudioProcessorValueTreeState& s, juce::String extraHelp)
    : link (l), state (s), help (std::move (extraHelp))
{
    addAndMakeVisible (masterBtn);
    atts.push_back (std::make_unique<juce::ButtonParameterAttachment> (*state.getParameter ("lk_on"), masterBtn));
    for (int f = 0; f < kNumFields; ++f)
    {
        fieldBtns[f].setButtonText (fieldLabel (f));
        addAndMakeVisible (fieldBtns[f]);
        atts.push_back (std::make_unique<juce::ButtonParameterAttachment> (*state.getParameter (fieldParamId (f)), fieldBtns[f]));
    }
    addAndMakeVisible (extraArea);
    startTimerHz (5);
}

LinkPanel::~LinkPanel() { stopTimer(); atts.clear(); }

void LinkPanel::resized()
{
    peersArea = { 0, 0, 640, getHeight() };
    masterBtn.setBounds (660, 30, 160, 24);
    for (int f = 0; f < kNumFields; ++f)
        fieldBtns[f].setBounds (660 + (f % 4) * 150, 64 + (f / 4) * 28, 146, 24);
    extraArea.setBounds (660, 64 + 4 * 28 + 8, getWidth() - 660, getHeight() - (64 + 4 * 28 + 8));
}

void LinkPanel::paint (juce::Graphics& g)
{
    g.setColour (col::panel);
    g.fillRoundedRectangle (peersArea.toFloat(), 6.0f);
    g.fillRoundedRectangle (juce::Rectangle<float> (650.0f, 0.0f, (float) getWidth() - 650.0f, (float) getHeight()), 6.0f);

    g.setColour (col::rnd);
    g.setFont (font (12.0f, true));
    g.drawText ("ALEALINK : instances detectees", 10, 4, 400, 20, juce::Justification::centredLeft);
    g.drawText ("SYNCHRONISER", 660, 4, 300, 20, juce::Justification::centredLeft);

    g.setFont (font (11.5f));
    int y = 32;
    if (! link.connected())
    {
        g.setColour (col::lock);
        g.drawText ("Memoire partagee indisponible", 12, y, 600, 20, juce::Justification::centredLeft);
        return;
    }
    auto drawRow = [&] (const PeerInfo& pi, bool me)
    {
        auto r = juce::Rectangle<int> (10, y, 620, 26);
        g.setColour (me ? col::panelHi.brighter (0.15f) : col::panelHi);
        g.fillRoundedRectangle (r.toFloat(), 4.0f);
        g.setColour (pi.kind == KindAlea ? col::accent : (pi.kind == KindColor ? juce::Colour (0xffc58cff) : col::rnd));
        g.fillEllipse ((float) r.getX() + 8.0f, (float) r.getCentreY() - 5.0f, 10.0f, 10.0f);
        g.setColour (col::text);
        const juce::String name = juce::String (kindName (pi.kind)) + " #" + juce::String (pi.slot + 1) + (me ? "  (cette instance)" : "");
        g.drawText (name, r.getX() + 26, r.getY(), 200, r.getHeight(), juce::Justification::centredLeft);
        g.setColour (pi.linked ? col::text : col::textDim);
        g.drawText (juce::String (pi.bpm, 1) + " BPM", r.getX() + 230, r.getY(), 80, r.getHeight(), juce::Justification::centredLeft);
        g.drawText (pi.playing ? "lecture" : "arret", r.getX() + 310, r.getY(), 70, r.getHeight(), juce::Justification::centredLeft);
        g.drawText ("Pattern " + juce::String::charToString ((juce::juce_wchar) ('A' + juce::jlimit (0, 7, pi.pattern))),
                    r.getX() + 380, r.getY(), 80, r.getHeight(), juce::Justification::centredLeft);
        g.drawText ("Ton " + noteName (pi.root), r.getX() + 460, r.getY(), 70, r.getHeight(), juce::Justification::centredLeft);
        g.setColour (pi.linked ? col::rnd : col::textDim);
        g.drawText (pi.linked ? "lie" : "libre", r.getX() + 540, r.getY(), 70, r.getHeight(), juce::Justification::centredLeft);
        y += 30;
    };
    for (auto& pi : link.peers (true))
        if (pi.slot == link.number() - 1) drawRow (pi, true);
    for (auto& pi : link.peers (false)) drawRow (pi, false);
    if (link.peers (false).isEmpty())
    {
        g.setColour (col::textDim);
        g.drawFittedText ("Aucune autre instance pour l'instant. Ouvre Alea, Talea et/ou Color sur d'autres pistes : "
                          "elles apparaissent ici automatiquement et se synchronisent.", 12, y + 4, 610, 40, juce::Justification::topLeft, 3);
        y += 48;
    }
    g.setColour (col::textDim);
    g.setFont (font (10.5f));
    g.drawFittedText ("La derniere instance qui change une valeur cochee la transmet aux autres. Dans un DAW, le tempo et la position "
                      "viennent de l'hote ; hors DAW (applis autonomes, transport a l'arret), Play et le tempo sont partages "
                      "et les instances se calent sur une horloge commune. " + help,
                      juce::Rectangle<int> (12, getHeight() - 70, 616, 64), juce::Justification::topLeft, 5);
}
} // namespace alea::gui
