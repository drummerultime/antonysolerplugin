#pragma once

#include "../../../Shared/Gui/GuiCommon.h"
#include "../ColorProcessor.h"

namespace color::gui
{
using namespace alea::gui;

const juce::Colour colorCol { 0xffc58cff };   // couleur de COLOR
juce::Colour functionColour (int fn);          // 0 tonique, 1 sous-dominante, 2 dominante, 3 couleur
juce::String functionLetter (int fn);

// Progression : un bloc par accord (largeur = duree), nom, degre, fonction. Clic = ecoute,
// clic droit = substitutions / reharmonisation / qualite / basse / duree / suite probable,
// glisser le bord droit = duree.
class ChordLane : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit ChordLane (ColorAudioProcessor& p) : proc (p) {}
    std::function<void()> onChanged;
    std::function<void (int)> onSelect;
    int selected = 0;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> blockRect (int i) const;
    int chordAt (juce::Point<float> p, bool& onEdge) const;
    void chordMenu (int i);
    ColorAudioProcessor& proc;
    int resizing = -1, resizeStartBeats = 0;
    float resizeStartX = 0;
    bool didResize = false;
};

// Rythme : coups (barre = duree tenue), puis couloirs Velocity, Gate, Proba, Partie, Strum, Octave, Ratchet, Condition, Nudge
class RhythmGrid : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit RhythmGrid (ColorAudioProcessor& p) : proc (p) {}
    enum Lane { Vel = 0, Gate, Prob, Part, Strum, Oct, Rat, Cond, Nudge, kNumLanes };
    std::function<void (int)> onLockStep;
    int lockStep = -1;
    ColorAudioProcessor::Preview preview;
    bool showPreview = true;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override { dragMode = 0; }
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    int visSteps() const;
    float cellW() const;
    int stepAt (float x) const;
    int laneAt (float y) const;
    void editLane (int lane, int step, float y, bool reset);
    void stepMenu (int step);
    ColorAudioProcessor& proc;
    int dragMode = 0;           // 1 = longueur d'un coup, 2 = couloir, 3 = peinture on, 4 = peinture off
    int dragStep = -1, dragLane = -1;
    static constexpr float kHitsH = 46.0f, kLabelW = 64.0f;
};

// Clavier + nom de l'accord joue, degre, fonction, gamme parente (Russell), tension
class VoicingView : public juce::Component
{
public:
    explicit VoicingView (ColorAudioProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    int shownChord = -1;          // accord selectionne (si rien ne joue)
private:
    ColorAudioProcessor& proc;
};

// Onglet THEORIE : analyse de la progression, cycle des quintes, suggestions de suite
class TheoryView : public juce::Component
{
public:
    explicit TheoryView (ColorAudioProcessor& p);
    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh();
    std::function<void()> onChanged;
    int selected = 0;
private:
    ColorAudioProcessor& proc;
    juce::TextButton sugg[6];
    std::vector<theory::Chord> suggChords;
    juce::Rectangle<int> tableArea, circleArea, suggArea;
};

} // namespace color::gui
