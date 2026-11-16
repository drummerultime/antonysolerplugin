#pragma once

// Petits motifs graphiques Art deco, clins d'oeil a Metropolis (dessins originaux, vectoriels).
#include <juce_gui_basics/juce_gui_basics.h>

namespace alea::gui::metro
{
// Silhouette de ville Art deco (tours a gradins, une grande tour centrale), tres discrete.
inline void drawSkyline (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour c)
{
    juce::Path p;
    const float x0 = area.getX(), w = area.getWidth(), base = area.getBottom(), h = area.getHeight();
    juce::Random r (1927);
    float x = x0;
    while (x < x0 + w)
    {
        const float bw = 10.0f + r.nextFloat() * 26.0f;
        const bool tall = std::abs ((x + bw * 0.5f - x0) / w - 0.5f) < 0.02f;
        const float th = tall ? h : h * (0.25f + 0.5f * r.nextFloat());
        const int steps = tall ? 4 : 1 + r.nextInt (3);
        float left = x, width = bw, top = base - th * 0.6f;
        p.addRectangle (left, top, width, base - top);
        for (int s = 0; s < steps; ++s)
        {
            left += width * 0.17f; width *= 0.66f;
            const float t2 = top - th * 0.4f / (float) steps;
            p.addRectangle (left, t2, width, top - t2 + 0.5f);
            top = t2;
        }
        if (tall) p.addTriangle (left, top, left + width, top, left + width * 0.5f, area.getY() - 4.0f);
        x += bw + 2.0f + r.nextFloat() * 8.0f;
    }
    g.setColour (c);
    g.fillPath (p);
}

// ROTWANG : pentagramme cercle et anneaux electriques du laboratoire
inline void drawRotwangSeal (juce::Graphics& g, juce::Point<float> c, float r, juce::Colour col, float pulse)
{
    for (int k = 0; k < 3; ++k)
    {
        const float rr = r * (1.05f + 0.22f * (float) k + 0.12f * pulse);
        g.setColour (col.withAlpha (juce::jlimit (0.0f, 1.0f, 0.10f + 0.25f * pulse - 0.05f * (float) k)));
        g.drawEllipse (c.x - rr, c.y - rr * 0.38f, rr * 2.0f, rr * 0.76f, 1.0f);
    }
    g.setColour (col.withAlpha (0.55f));
    g.drawEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f, 1.2f);
    juce::Path star;
    for (int i = 0; i < 5; ++i)
    {
        const float a = juce::MathConstants<float>::pi + (float) (i * 2 % 5) * juce::MathConstants<float>::twoPi / 5.0f;   // pointe en bas
        const juce::Point<float> q (c.x + r * 0.9f * std::sin (a), c.y - r * 0.9f * std::cos (a));
        if (i == 0) star.startNewSubPath (q); else star.lineTo (q);
    }
    star.closeSubPath();
    g.setColour (col.withAlpha (0.45f + 0.4f * pulse));
    g.strokePath (star, juce::PathStrokeType (1.1f));
}

// GROT : horloge de dix heures du gardien de la machine-coeur
inline void drawTenHourClock (juce::Graphics& g, juce::Point<float> c, float r, juce::Colour col, int hour, float flash)
{
    g.setColour (col.withAlpha (0.12f + 0.25f * flash));
    g.fillEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f);
    g.setColour (col.withAlpha (0.6f));
    g.drawEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f, 1.4f);
    for (int i = 0; i < 10; ++i)
    {
        const float a = (float) i / 10.0f * juce::MathConstants<float>::twoPi;
        const float r0 = r * (i == 0 ? 0.62f : 0.74f), r1 = r * 0.9f;
        g.drawLine (c.x + r0 * std::sin (a), c.y - r0 * std::cos (a), c.x + r1 * std::sin (a), c.y - r1 * std::cos (a), i == 0 ? 1.8f : 1.0f);
    }
    const float a = (float) (hour % 10) / 10.0f * juce::MathConstants<float>::twoPi;
    g.setColour (col.withAlpha (0.85f));
    g.drawLine (c.x, c.y, c.x + r * 0.7f * std::sin (a), c.y - r * 0.7f * std::cos (a), 1.8f);
    g.fillEllipse (c.x - 2.0f, c.y - 2.0f, 4.0f, 4.0f);
}

// MARIA : halo Art deco partage en deux (l'humaine et son double mecanique)
inline void drawDoubleHalo (juce::Graphics& g, juce::Point<float> c, float r, juce::Colour warm, juce::Colour cold, float glow)
{
    const int rays = 17;
    for (int i = 0; i < rays; ++i)
    {
        const float th = (float) i / (float) (rays - 1) * juce::MathConstants<float>::pi;   // 0 = droite, pi = gauche
        const bool left = th > juce::MathConstants<float>::halfPi + 0.01f;
        const bool mid = std::abs (th - juce::MathConstants<float>::halfPi) < 0.01f;
        const float ri = r * 0.5f, ro = r * (i % 2 == 0 ? 1.0f : 0.78f);
        const juce::Colour col = mid ? cold.interpolatedWith (warm, 0.5f) : (left ? warm : cold);
        g.setColour (col.withAlpha (juce::jlimit (0.0f, 1.0f, 0.35f + 0.5f * glow * (left ? 1.0f : 0.6f))));
        g.drawLine (c.x + ri * std::cos (th), c.y - ri * std::sin (th), c.x + ro * std::cos (th), c.y - ro * std::sin (th), 1.1f);
    }
    juce::Path a1; a1.addCentredArc (c.x, c.y, r * 0.38f, r * 0.38f, 0.0f, -juce::MathConstants<float>::halfPi, 0.0f, true);
    g.setColour (warm.withAlpha (0.85f)); g.strokePath (a1, juce::PathStrokeType (1.5f));
    juce::Path a2; a2.addCentredArc (c.x, c.y, r * 0.38f, r * 0.38f, 0.0f, 0.0f, juce::MathConstants<float>::halfPi, true);
    g.setColour (cold.withAlpha (0.85f)); g.strokePath (a2, juce::PathStrokeType (1.5f));
    g.setColour (cold.withAlpha (0.4f));
    g.drawLine (c.x - r * 1.05f, c.y, c.x + r * 1.05f, c.y, 0.8f);
}

} // namespace alea::gui::metro
