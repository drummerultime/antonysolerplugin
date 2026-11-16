#include "Overlays.h"
#include "../PluginProcessor.h"

namespace fragor
{
namespace
{
    juce::Font font (float h, bool bold = true) { return juce::Font (juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain)); }
}

// ============================================================================ Overlay
Overlay::Overlay (const juce::String& t, int w, int h) : title (t), pw (w), ph (h)
{
    addAndMakeVisible (closeBtn);
    closeBtn.onClick = [this] { close(); };
    setWantsKeyboardFocus (true);
}

juce::Rectangle<int> Overlay::panel() const { return getLocalBounds().withSizeKeepingCentre (pw, ph); }

void Overlay::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.6f));
    const auto r = panel().toFloat();
    g.setColour (Col::panel);
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (Col::acid.withAlpha (0.6f));
    g.drawRoundedRectangle (r, 8.0f, 1.5f);
    g.setColour (Col::text);
    g.setFont (font (15.0f));
    g.drawText (title, r.withHeight (34.0f).withTrimmedLeft (16.0f), juce::Justification::centredLeft, false);
}

void Overlay::resized()
{
    const auto p = panel();
    closeBtn.setBounds (p.getRight() - 34, p.getY() + 6, 26, 24);
    layout (p.reduced (14).withTrimmedTop (28));
}

void Overlay::mouseDown (const juce::MouseEvent& e)
{
    if (! panel().contains (e.getPosition())) close();
}

// ============================================================================ CurveEditor
CurveEditor::CurveEditor (FragorProcessor& p) : proc (p) { startTimerHz (30); }

void CurveEditor::setLfo (int i) { lfo = i; reload(); }
void CurveEditor::reload()       { curve = proc.getLfoCurve (lfo); sel = -1; repaint(); }

juce::Point<float> CurveEditor::toScreen (float x, float y) const
{
    const auto a = area();
    return { a.getX() + x * a.getWidth(), a.getBottom() - y * a.getHeight() };
}

juce::Point<float> CurveEditor::toCurve (juce::Point<float> p) const
{
    const auto a = area();
    return { juce::jlimit (0.0f, 1.0f, (p.x - a.getX()) / a.getWidth()), juce::jlimit (0.0f, 1.0f, (a.getBottom() - p.y) / a.getHeight()) };
}

int CurveEditor::pointNear (juce::Point<float> p) const
{
    int best = -1; float bd = 10.0f;
    for (size_t i = 0; i < curve.size(); ++i)
    {
        const float d = toScreen (curve[i].x, curve[i].y).getDistanceFrom (p);
        if (d < bd) { bd = d; best = (int) i; }
    }
    return best;
}

void CurveEditor::commit()
{
    proc.setLfoCurve (lfo, curve);
    const juce::String id = "l" + juce::String (lfo + 1) + "Shape";
    if (auto* p = proc.apvts.getParameter (id))
        if ((int) proc.apvts.getRawParameterValue (id)->load() != (int) LfoShape::Custom)
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 ((float) LfoShape::Custom));
            p->endChangeGesture();
        }
    repaint();
}

void CurveEditor::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    g.setColour (Col::bg);
    g.fillRoundedRectangle (r, 6.0f);
    const auto a = area();

    for (int i = 0; i <= gridX; ++i)
    {
        const float x = a.getX() + a.getWidth() * (float) i / (float) gridX;
        g.setColour ((i % 4 == 0) ? Col::border.brighter (0.3f) : Col::border);
        g.drawVerticalLine ((int) x, a.getY(), a.getBottom());
    }
    for (int i = 0; i <= gridY; ++i)
    {
        const float y = a.getY() + a.getHeight() * (float) i / (float) gridY;
        g.setColour (Col::border);
        g.drawHorizontalLine ((int) y, a.getX(), a.getRight());
    }

    float table[512];
    renderCurve (curve, table, 512);
    juce::Path p, fill;
    for (int i = 0; i < 512; ++i)
    {
        const auto s = toScreen ((float) i / 511.0f, table[i]);
        if (i == 0) { p.startNewSubPath (s); fill.startNewSubPath (a.getX(), a.getBottom()); fill.lineTo (s); }
        else { p.lineTo (s); fill.lineTo (s); }
    }
    fill.lineTo (a.getRight(), a.getBottom());
    fill.closeSubPath();
    g.setColour (Col::violet.withAlpha (0.15f));
    g.fillPath (fill);
    g.setColour (Col::violet.withAlpha (0.35f));
    g.strokePath (p, juce::PathStrokeType (5.0f));
    g.setColour (Col::violet.brighter (0.4f));
    g.strokePath (p, juce::PathStrokeType (2.0f));

    for (size_t i = 0; i < curve.size(); ++i)
    {
        const auto s = toScreen (curve[i].x, curve[i].y);
        const bool end = i == 0 || i + 1 == curve.size();
        g.setColour ((int) i == sel ? Col::hot : (end ? Col::text : Col::acid));
        g.fillEllipse (s.x - 5.0f, s.y - 5.0f, 10.0f, 10.0f);
        g.setColour (Col::bg);
        g.drawEllipse (s.x - 5.0f, s.y - 5.0f, 10.0f, 10.0f, 1.5f);
    }

    if (proc.activeVoices.load() > 0)
    {
        const float v = proc.lfoView[lfo].load();
        g.setColour (Col::hot.withAlpha (0.8f));
        g.drawHorizontalLine ((int) (a.getBottom() - v * a.getHeight()), a.getX(), a.getRight());
    }
}

void CurveEditor::mouseDown (const juce::MouseEvent& e)
{
    const int near = pointNear (e.position);
    segDrag = -1;
    if (e.mods.isPopupMenu())
    {
        if (near > 0 && near + 1 < (int) curve.size()) { curve.erase (curve.begin() + near); sel = -1; commit(); }
        return;
    }
    if (near >= 0) { sel = near; return; }

    const auto c = toCurve (e.position);
    if (e.mods.isAltDown() || e.mods.isCommandDown())
    {
        for (size_t i = 0; i + 1 < curve.size(); ++i)
            if (c.x >= curve[i].x && c.x <= curve[i + 1].x) { segDrag = (int) i; startC = curve[i].c; break; }
        return;
    }
    // nouveau point
    CurvePoint np { c.x, c.y, 0.0f };
    if (snap)
    {
        np.x = std::round (np.x * gridX) / gridX;
        np.y = std::round (np.y * gridY) / gridY;
    }
    np.x = juce::jlimit (0.001f, 0.999f, np.x);
    size_t at = 1;
    while (at < curve.size() && curve[at].x < np.x) ++at;
    curve.insert (curve.begin() + (long) at, np);
    sel = (int) at;
    commit();
}

void CurveEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (segDrag >= 0)
    {
        curve[(size_t) segDrag].c = juce::jlimit (-1.0f, 1.0f, startC - (float) e.getDistanceFromDragStartY() / 120.0f);
        commit();
        return;
    }
    if (sel < 0) return;
    auto c = toCurve (e.position);
    if (snap && ! e.mods.isShiftDown())
    {
        c.x = std::round (c.x * gridX) / gridX;
        c.y = std::round (c.y * gridY) / gridY;
    }
    auto& pt = curve[(size_t) sel];
    pt.y = c.y;
    if (sel > 0 && sel + 1 < (int) curve.size())
        pt.x = juce::jlimit (curve[(size_t) sel - 1].x, curve[(size_t) sel + 1].x, c.x);
    commit();
}

void CurveEditor::mouseUp (const juce::MouseEvent&) { segDrag = -1; }

void CurveEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int near = pointNear (e.position);
    if (near >= 0) { curve[(size_t) near].c = 0.0f; commit(); }
}

// ============================================================================ CurveOverlay
CurveOverlay::CurveOverlay (FragorProcessor& p) : Overlay ("LFO CURVE EDITOR", 820, 440), proc (p), editor (p)
{
    addAndMakeVisible (editor);
    for (int i = 0; i < 3; ++i)
    {
        lfoBtn[i].setButtonText ("LFO " + juce::String (i + 1));
        lfoBtn[i].setClickingTogglesState (true);
        lfoBtn[i].setRadioGroupId (501);
        lfoBtn[i].onClick = [this, i] { editor.setLfo (i); };
        addAndMakeVisible (lfoBtn[i]);
    }
    presets.addItemList (curvePresetNames(), 1);
    presets.setTextWhenNothingSelected ("FORMES...");
    presets.onChange = [this]
    {
        if (presets.getSelectedItemIndex() >= 0) editor.setCurve (curvePreset (presets.getSelectedItemIndex()));
        presets.setSelectedId (0, juce::dontSendNotification);
    };
    addAndMakeVisible (presets);
    grid.addItemList ({ "GRID 1/4", "GRID 1/8", "GRID 1/16", "GRID 1/32" }, 1);
    grid.setSelectedId (3, juce::dontSendNotification);
    grid.onChange = [this] { static const int g[] = { 4, 8, 16, 32 }; editor.gridX = g[grid.getSelectedItemIndex()]; editor.repaint(); };
    addAndMakeVisible (grid);
    snapBtn.setClickingTogglesState (true);
    snapBtn.setToggleState (true, juce::dontSendNotification);
    snapBtn.onClick = [this] { editor.snap = snapBtn.getToggleState(); };
    addAndMakeVisible (snapBtn);
    invertBtn.onClick = [this]
    {
        auto c = editor.getCurve();
        for (auto& pt : c) pt.y = 1.0f - pt.y;
        editor.setCurve (c);
    };
    reverseBtn.onClick = [this]
    {
        auto c = editor.getCurve();
        LfoCurve r;
        for (size_t i = c.size(); i-- > 0;)
            r.push_back ({ 1.0f - c[i].x, c[i].y, i > 0 ? -c[i - 1].c : 0.0f });
        editor.setCurve (r);
    };
    addAndMakeVisible (invertBtn);
    addAndMakeVisible (reverseBtn);
    hint.setText (juce::String::fromUTF8 ("Clic : ajouter un point  -  glisser : déplacer (Maj = sans grille)  -  clic droit : supprimer  -  "
                  "Alt+glisser sur un segment : courbure  -  double-clic : courbure à 0"), juce::dontSendNotification);
    hint.setFont (font (11.0f, false));
    hint.setColour (juce::Label::textColourId, Col::dim);
    addAndMakeVisible (hint);
}

void CurveOverlay::open (int lfo)
{
    for (int i = 0; i < 3; ++i) lfoBtn[i].setToggleState (i == lfo, juce::dontSendNotification);
    editor.setLfo (lfo);
    setVisible (true);
    toFront (true);
}

void CurveOverlay::layout (juce::Rectangle<int> c)
{
    auto top = c.removeFromTop (28);
    for (auto& b : lfoBtn) { b.setBounds (top.removeFromLeft (64)); top.removeFromLeft (4); }
    top.removeFromLeft (16);
    presets.setBounds (top.removeFromLeft (150)); top.removeFromLeft (6);
    grid.setBounds (top.removeFromLeft (110)); top.removeFromLeft (6);
    snapBtn.setBounds (top.removeFromLeft (64)); top.removeFromLeft (6);
    invertBtn.setBounds (top.removeFromLeft (76)); top.removeFromLeft (6);
    reverseBtn.setBounds (top.removeFromLeft (84));
    hint.setBounds (c.removeFromBottom (22));
    c.removeFromTop (8);
    editor.setBounds (c);
}

// ============================================================================ PresetBrowser
PresetBrowser::PresetBrowser (FragorProcessor& p) : Overlay ("PRESETS", 860, 600), proc (p)
{
    search.setTextToShowWhenEmpty ("Rechercher...", Col::dim);
    search.onTextChange = [this] { filter(); };
    search.setFont (font (14.0f, false));
    addAndMakeVisible (search);
    list.setRowHeight (24);
    list.setColour (juce::ListBox::backgroundColourId, Col::bg);
    list.setColour (juce::ListBox::outlineColourId, Col::border);
    list.getVerticalScrollBar().setColour (juce::ScrollBar::thumbColourId, Col::violet);
    addAndMakeVisible (list);

    juce::StringArray tags { "ALL", "FAVORIS", "USER", "GENERATED" };
    for (auto& f : factoryPresets()) tags.addIfNotAlreadyThere (f.category);
    for (auto& t : tags)
    {
        auto* b = tagBtns.add (new juce::TextButton (t));
        b->setClickingTogglesState (true);
        b->setRadioGroupId (601);
        b->onClick = [this, t] { tag = t; filter(); };
        addAndMakeVisible (b);
    }
    tagBtns[0]->setToggleState (true, juce::dontSendNotification);
}

void PresetBrowser::loadFavs()
{
    favs = juce::StringArray::fromLines (FragorProcessor::dataFolder().getChildFile ("favorites.txt").loadFileAsString());
    favs.removeEmptyStrings();
}

void PresetBrowser::saveFavs()
{
    FragorProcessor::dataFolder().getChildFile ("favorites.txt").replaceWithText (favs.joinIntoString ("\n"));
}

void PresetBrowser::rebuild()
{
    loadFavs();
    all.clear();
    const auto& f = factoryPresets();
    for (size_t i = 0; i < f.size(); ++i) all.push_back ({ f[i].name, f[i].category, (int) i, {}, false });
    auto files = FragorProcessor::userPresetFolder().findChildFiles (juce::File::findFiles, true, "*.fragor");
    files.sort();
    for (auto& file : files)
    {
        juce::String cat = "USER";
        if (auto xml = juce::XmlDocument::parse (file)) cat = xml->getStringAttribute ("category", "USER");
        all.push_back ({ file.getFileNameWithoutExtension(), cat, -1, file, false });
    }
    for (auto& e : all) e.fav = favs.contains (key (e));
}

void PresetBrowser::filter()
{
    shown.clear();
    const auto q = search.getText().trim().toLowerCase();
    for (size_t i = 0; i < all.size(); ++i)
    {
        const auto& e = all[i];
        bool ok = tag == "ALL" || (tag == "FAVORIS" && e.fav) || (tag == "USER" && e.factory < 0) || e.category.equalsIgnoreCase (tag);
        if (ok && q.isNotEmpty()) ok = e.name.toLowerCase().contains (q) || e.category.toLowerCase().contains (q);
        if (ok) shown.push_back ((int) i);
    }
    list.updateContent();
    list.repaint();
}

void PresetBrowser::open()
{
    rebuild();
    filter();
    setVisible (true);
    toFront (true);
    search.grabKeyboardFocus();
}

void PresetBrowser::layout (juce::Rectangle<int> c)
{
    auto left = c.removeFromLeft (130);
    for (auto* b : tagBtns) { b->setBounds (left.removeFromTop (26)); left.removeFromTop (4); }
    c.removeFromLeft (10);
    search.setBounds (c.removeFromTop (28));
    c.removeFromTop (8);
    list.setBounds (c);
}

void PresetBrowser::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (row < 0 || row >= (int) shown.size()) return;
    const auto& e = all[(size_t) shown[(size_t) row]];
    const bool current = e.name == proc.presetName;
    if (selected || current) { g.setColour (current ? Col::acid.withAlpha (0.18f) : Col::panel2); g.fillRect (0, 0, w, h); }
    g.setFont (font (16.0f));
    g.setColour (e.fav ? Col::hot : Col::border.brighter (0.3f));
    g.drawText (e.fav ? juce::String::fromUTF8 ("\xe2\x98\x85") : juce::String::fromUTF8 ("\xe2\x98\x86"), 6, 0, 22, h, juce::Justification::centred);
    g.setFont (font (13.0f));
    g.setColour (current ? Col::acid : Col::text);
    g.drawText (e.name, 34, 0, w - 200, h, juce::Justification::centredLeft, true);
    g.setColour (e.factory >= 0 ? Col::violet : Col::hot);
    g.setFont (font (11.0f));
    g.drawText (e.category + (e.factory >= 0 ? "" : "  (user)"), w - 170, 0, 160, h, juce::Justification::centredRight, true);
}

void PresetBrowser::listBoxItemClicked (int row, const juce::MouseEvent& ev)
{
    if (row < 0 || row >= (int) shown.size()) return;
    auto& e = all[(size_t) shown[(size_t) row]];
    if (ev.x < 30)
    {
        e.fav = ! e.fav;
        if (e.fav) favs.addIfNotAlreadyThere (key (e)); else favs.removeString (key (e));
        saveFavs();
        list.repaintRow (row);
        return;
    }
    if (e.factory >= 0) proc.loadFactoryPreset (e.factory);
    else proc.loadUserPreset (e.file);
    list.repaint();
}

void PresetBrowser::listBoxItemDoubleClicked (int, const juce::MouseEvent&) { close(); }

// ============================================================================ Settings
SettingsOverlay::SettingsOverlay (FragorProcessor& p) : Overlay ("REGLAGES", 560, 330), proc (p)
{
    auto& s = proc.apvts;
    autoGain.setToggleState (proc.autoGain, juce::dontSendNotification);
    autoGain.setColour (juce::ToggleButton::tickColourId, Col::acid);
    autoGain.onClick = [this]
    {
        proc.autoGain = autoGain.getToggleState();
        juce::XmlElement x ("FRAGOR_SETTINGS");
        x.setAttribute ("autoGain", proc.autoGain);
        x.writeTo (FragorProcessor::dataFolder().getChildFile ("settings.xml"));
    };
    addAndMakeVisible (autoGain);

    items.push_back (std::make_unique<Choice> (s, "quality", "QUALITE (SUR-ECHANTILLONNAGE)"));
    items.push_back (std::make_unique<Toggle> (s, "mpeOn", "MPE (ROLI, Linnstrument, Push 3...)", Col::violet));
    items.push_back (std::make_unique<Knob> (s, "mpeBend", "MPE BEND", Col::violet));
    items.push_back (std::make_unique<Choice> (s, "midiOut", "SORTIE MIDI"));
    items.push_back (std::make_unique<Knob> (s, "midiOutCC", "CC OUT", Col::hot));
    items.push_back (std::make_unique<Knob> (s, "midiOutNote", "NOTE OUT", Col::hot));
    items.push_back (std::make_unique<Knob> (s, "snapTime", "SNAP MORPH", Col::hot));
    for (auto& i : items) addAndMakeVisible (*i);

    info.setText (juce::String::fromUTF8 ("Sortie MIDI : LFO 1 en CC et/ou une note à chaque pas du séquenceur (pour déclencher un sidechain, un sampler...). "
                  "Dans Live : une piste MIDI > MIDI From : FRAGOR."), juce::dontSendNotification);
    info.setFont (font (11.0f, false));
    info.setColour (juce::Label::textColourId, Col::dim);
    info.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (info);
}

void SettingsOverlay::layout (juce::Rectangle<int> c)
{
    autoGain.setBounds (c.removeFromTop (24));
    c.removeFromTop (10);
    auto row = c.removeFromTop (38);
    items[0]->setBounds (row.removeFromLeft (240));
    row.removeFromLeft (20);
    items[1]->setBounds (row.removeFromLeft (260).withTrimmedTop (14));
    c.removeFromTop (10);
    auto row2 = c.removeFromTop (70);
    items[3]->setBounds (row2.removeFromLeft (160).withHeight (38));
    row2.removeFromLeft (10);
    items[4]->setBounds (row2.removeFromLeft (70));
    items[5]->setBounds (row2.removeFromLeft (70));
    items[6]->setBounds (row2.removeFromLeft (80));
    items[2]->setBounds (row2.removeFromLeft (80));
    c.removeFromTop (10);
    info.setBounds (c);
}

// ============================================================================ Save
SaveOverlay::SaveOverlay (FragorProcessor& p) : Overlay ("SAUVER LE PRESET", 460, 190), proc (p)
{
    name.setFont (font (15.0f, false));
    addAndMakeVisible (name);
    juce::StringArray cats { "USER" };
    for (auto& f : factoryPresets()) cats.addIfNotAlreadyThere (f.category);
    category.addItemList (cats, 1);
    category.setEditableText (true);
    addAndMakeVisible (category);
    addAndMakeVisible (saveBtn);
    saveBtn.setColour (juce::TextButton::buttonColourId, Col::acid);
    saveBtn.setColour (juce::TextButton::textColourOffId, Col::bg);
    saveBtn.onClick = [this]
    {
        auto n = juce::File::createLegalFileName (name.getText().trim());
        if (n.isEmpty()) return;
        auto cat = category.getText().trim();
        auto f = FragorProcessor::userPresetFolder().getChildFile (n + ".fragor");
        proc.saveUserPreset (f, cat);
        close();
    };
    name.onReturnKey = [this] { saveBtn.triggerClick(); };
}

void SaveOverlay::open()
{
    name.setText (proc.presetName.removeCharacters ("*"), false);
    const int idx = category.getNumItems() > 0 ? juce::jmax (0, [this] { for (int i = 0; i < category.getNumItems(); ++i) if (category.getItemText (i).equalsIgnoreCase (proc.presetCategory)) return i; return 0; }()) : 0;
    category.setSelectedItemIndex (idx, juce::dontSendNotification);
    setVisible (true);
    toFront (true);
    name.grabKeyboardFocus();
    name.selectAll();
}

void SaveOverlay::layout (juce::Rectangle<int> c)
{
    name.setBounds (c.removeFromTop (30));
    c.removeFromTop (10);
    auto row = c.removeFromTop (30);
    category.setBounds (row.removeFromLeft (220));
    saveBtn.setBounds (row.removeFromRight (110));
}
}
