#pragma once

#include "Widgets.h"
#include "../Presets.h"

class FragorProcessor;

namespace fragor
{
    // Fond assombri + panneau central ; clic en dehors = fermer
    class Overlay : public juce::Component
    {
    public:
        Overlay (const juce::String& title, int w, int h);
        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent& e) override;
        juce::Rectangle<int> panel() const;
        std::function<void()> onClose;
        void close() { setVisible (false); if (onClose) onClose(); }
    protected:
        virtual void layout (juce::Rectangle<int> content) = 0;
        juce::String title;
        int pw, ph;
        juce::TextButton closeBtn { "X" };
    };

    // ------------------------------------------------------------ Éditeur de courbe de LFO
    class CurveEditor : public juce::Component, private juce::Timer
    {
    public:
        explicit CurveEditor (FragorProcessor& p);
        void setLfo (int i);
        int  getLfo() const { return lfo; }
        void setCurve (const LfoCurve& c) { curve = c; commit(); }
        const LfoCurve& getCurve() const { return curve; }
        void reload();
        bool snap = true;
        int gridX = 16, gridY = 8;

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
    private:
        void timerCallback() override { repaint(); }
        juce::Rectangle<float> area() const { return getLocalBounds().toFloat().reduced (12.0f); }
        juce::Point<float> toScreen (float x, float y) const;
        juce::Point<float> toCurve (juce::Point<float> p) const;
        int pointNear (juce::Point<float> p) const;
        void commit();
        FragorProcessor& proc;
        int lfo = 0;
        LfoCurve curve;
        int sel = -1, segDrag = -1;
        float startC = 0.0f;
    };

    class CurveOverlay : public Overlay
    {
    public:
        explicit CurveOverlay (FragorProcessor& p);
        void open (int lfo);
    protected:
        void layout (juce::Rectangle<int> c) override;
    private:
        FragorProcessor& proc;
        CurveEditor editor;
        juce::TextButton lfoBtn[3], invertBtn { "INVERT" }, reverseBtn { "REVERSE" }, snapBtn { "SNAP" };
        juce::ComboBox presets, grid;
        juce::Label hint;
    };

    // ------------------------------------------------------------ Navigateur de presets
    class PresetBrowser : public Overlay, private juce::ListBoxModel
    {
    public:
        explicit PresetBrowser (FragorProcessor& p);
        void open();
    protected:
        void layout (juce::Rectangle<int> c) override;
    private:
        struct Entry { juce::String name, category; int factory = -1; juce::File file; bool fav = false; };
        int getNumRows() override { return (int) shown.size(); }
        void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
        void listBoxItemClicked (int row, const juce::MouseEvent&) override;
        void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
        void rebuild();
        void filter();
        void loadFavs();
        void saveFavs();
        juce::String key (const Entry& e) const { return e.factory >= 0 ? "F:" + e.name : "U:" + e.file.getFileName(); }

        FragorProcessor& proc;
        std::vector<Entry> all;
        std::vector<int> shown;
        juce::StringArray favs;
        juce::String tag { "ALL" };
        juce::TextEditor search;
        juce::ListBox list { "presets", this };
        juce::OwnedArray<juce::TextButton> tagBtns;
    };

    // ------------------------------------------------------------ Réglages (MPE, MIDI out, qualité...)
    class SettingsOverlay : public Overlay
    {
    public:
        explicit SettingsOverlay (FragorProcessor& p);
    protected:
        void layout (juce::Rectangle<int> c) override;
    private:
        FragorProcessor& proc;
        juce::ToggleButton autoGain { "AUTO GAIN (normalise presets / GENERATE / MUTATE)" };
        std::vector<std::unique_ptr<juce::Component>> items;
        juce::Label info;
    };

    // ------------------------------------------------------------ Sauvegarde : nom + catégorie
    class SaveOverlay : public Overlay
    {
    public:
        explicit SaveOverlay (FragorProcessor& p);
        void open();
    protected:
        void layout (juce::Rectangle<int> c) override;
    private:
        FragorProcessor& proc;
        juce::TextEditor name;
        juce::ComboBox category;
        juce::TextButton saveBtn { "SAVE" };
    };
}
