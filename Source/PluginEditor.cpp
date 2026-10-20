#include "PluginEditor.h"
#include "BinaryData.h"

namespace
{
    juce::String U (const char* s) { return juce::String::fromUTF8 (s); }
    // números á galega: coma decimal
    juce::String num (double v, int dec) { return juce::String (v, dec).replaceCharacter ('.', ','); }
    bool isBlack (int n) { const int pc = ((n % 12) + 12) % 12; return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10; }
    float hzToMidi (double hz) { return (float) (69.0 + 12.0 * std::log2 (hz / 440.0)); }
}

namespace ui
{
// ================================================================ tipografía
// Barlow: debuxada a partir dos letreiros das estradas
static juce::Typeface::Ptr tf (int which)
{
    static juce::Typeface::Ptr t[3] = {
        juce::Typeface::createSystemTypefaceFor (BinaryData::BarlowSemiCondensedMedium_ttf, BinaryData::BarlowSemiCondensedMedium_ttfSize),
        juce::Typeface::createSystemTypefaceFor (BinaryData::BarlowSemiCondensedSemiBold_ttf, BinaryData::BarlowSemiCondensedSemiBold_ttfSize),
        juce::Typeface::createSystemTypefaceFor (BinaryData::BarlowCondensedSemiBold_ttf, BinaryData::BarlowCondensedSemiBold_ttfSize) };
    return t[which];
}
juce::Font body (float s) { return juce::Font (juce::FontOptions (tf (0)).withHeight (s)); }
juce::Font semi (float s) { return juce::Font (juce::FontOptions (tf (1)).withHeight (s)); }
juce::Font cond (float s) { return juce::Font (juce::FontOptions (tf (2)).withHeight (s)); }

static juce::Path roundedPart (juce::Rectangle<float> r, float rad, bool left, bool right)
{
    juce::Path p;
    p.addRoundedRectangle (r.getX(), r.getY(), r.getWidth(), r.getHeight(), rad, rad, left, right, left, right);
    return p;
}

// ================================================================ aspecto
Look::Look()
{
    setColour (juce::ResizableWindow::backgroundColourId, col::noite);
    setColour (juce::Label::textColourId, col::escuma);
    setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TextEditor::backgroundColourId, col::fondo2);
    setColour (juce::TextEditor::textColourId, col::escuma);
    setColour (juce::TextEditor::outlineColourId, col::lina);
    setColour (juce::TextEditor::focusedOutlineColourId, col::sodio);
    setColour (juce::TextEditor::highlightColourId, col::sodio.withAlpha (0.35f));
    setColour (juce::CaretComponent::caretColourId, col::escuma);
    setColour (juce::ComboBox::backgroundColourId, col::fondo2);
    setColour (juce::ComboBox::textColourId, col::escuma);
    setColour (juce::ComboBox::outlineColourId, col::lina);
    setColour (juce::ComboBox::arrowColourId, col::neboa);
    setColour (juce::PopupMenu::backgroundColourId, col::fondo2);
    setColour (juce::PopupMenu::textColourId, col::escuma);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, col::sodio);
    setColour (juce::PopupMenu::highlightedTextColourId, col::noite);
    setColour (juce::TextButton::buttonColourId, col::fondo2);
    setColour (juce::TextButton::textColourOffId, col::escuma);
    setColour (juce::TextButton::textColourOnId, col::noite);
}

juce::Typeface::Ptr Look::getTypefaceForFont (const juce::Font& f) { return tf (f.isBold() ? 1 : 0); }

// botóns: contorno fino; o escollido, cheo de sodio. Os segmentos van pegados uns a outros.
void Look::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const auto shape = roundedPart (r, 5.0f, ! b.isConnectedOnLeft(), ! b.isConnectedOnRight());
    const float progress = (float) b.getProperties().getWithDefault ("progress", 0.0f);
    if (b.getToggleState())
    {
        g.setColour (down ? col::sodio.darker (0.15f) : col::sodio);
        g.fillPath (shape);
        return;
    }
    g.setColour (over || down ? col::carril.brighter (0.15f) : col::fondo2);
    g.fillPath (shape);
    if (progress > 0.0f)
    {
        g.saveState();
        g.reduceClipRegion (r.withWidth (r.getWidth() * progress).toNearestInt());
        g.setColour (col::sodio.withAlpha (0.28f));
        g.fillPath (shape);
        g.restoreState();
    }
    g.setColour (col::lina);
    g.strokePath (shape, juce::PathStrokeType (1.0f));
}

void Look::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool over, bool)
{
    const bool on = b.getToggleState();
    g.setColour (on ? col::noite : (over ? col::escuma : col::escuma.withAlpha (0.82f)));
    if (! b.isEnabled()) g.setColour (col::neboa.withAlpha (0.5f));
    g.setFont (semi ((float) b.getProperties().getWithDefault ("fontSize", 15.0f)));
    g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (6, 0), juce::Justification::centred, 1, 0.8f);
}

// potenciómetros: arco de 270°, o valor no medio
void Look::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s)
{
    auto r = juce::Rectangle<int> (x, y, w, h).toFloat();
    const float d = juce::jmin (r.getWidth(), r.getHeight()) - 6.0f;
    const auto c = r.getCentre();
    const float rad = d * 0.5f;
    juce::Path track; track.addCentredArc (c.x, c.y, rad, rad, 0.0f, a0, a1, true);
    g.setColour (col::lina); g.strokePath (track, juce::PathStrokeType (3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    const bool bip = s.getMinimum() < 0 && s.getMaximum() > 0;
    const double zeroAt = bip ? 0.0 : (s.getProperties().contains ("centre") ? (double) s.getProperties()["centre"] : s.getMinimum());
    const float zero = a0 + (float) s.valueToProportionOfLength (zeroAt) * (a1 - a0);
    const float ang = a0 + pos * (a1 - a0);
    if (std::abs (ang - zero) > 0.01f)
    {
        juce::Path val; val.addCentredArc (c.x, c.y, rad, rad, 0.0f, juce::jmin (zero, ang), juce::jmax (zero, ang), true);
        g.setColour (s.isEnabled() ? col::sodio : col::neboa.withAlpha (0.4f));
        g.strokePath (val, juce::PathStrokeType (3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    const juce::Point<float> tip (c.x + (rad - 9.0f) * std::sin (ang), c.y - (rad - 9.0f) * std::cos (ang));
    g.setColour (s.isEnabled() ? col::escuma : col::neboa);
    g.fillEllipse (tip.x - 2.5f, tip.y - 2.5f, 5.0f, 5.0f);
    g.setFont (semi (13.5f));
    g.drawText (s.getTextFromValue (s.getValue()).replaceCharacter ('.', ','), r.withSizeKeepingCentre (rad * 2.0f - 8.0f, 18.0f), juce::Justification::centred, false);
}

void Look::drawLabel (juce::Graphics& g, juce::Label& l)
{
    if (l.isBeingEdited()) return;
    g.setColour (l.findColour (juce::Label::textColourId).withMultipliedAlpha (l.isEnabled() ? 1.0f : 0.5f));
    g.setFont (getLabelFont (l));
    g.drawFittedText (l.getText(), l.getLocalBounds().reduced (2, 0), l.getJustificationType(), 1, 0.9f);
}

void Look::drawTooltip (juce::Graphics& g, const juce::String& text, int w, int h)
{
    auto r = juce::Rectangle<int> (w, h).toFloat();
    g.setColour (col::carril); g.fillRoundedRectangle (r, 5.0f);
    g.setColour (col::lina); g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.0f);
    juce::AttributedString s; s.setJustification (juce::Justification::topLeft);
    s.append (text, body (14.5f), col::escuma);
    juce::TextLayout tl; tl.createLayoutWithBalancedLineLengths (s, 290.0f);
    tl.draw (g, r.reduced (11.0f, 8.0f));
}

juce::Rectangle<int> Look::getTooltipBounds (const juce::String& text, juce::Point<int> pos, juce::Rectangle<int> parent)
{
    juce::AttributedString s; s.append (text, body (14.5f));
    juce::TextLayout tl; tl.createLayoutWithBalancedLineLengths (s, 290.0f);
    const int w = (int) tl.getWidth() + 24, h = (int) tl.getHeight() + 18;
    return juce::Rectangle<int> (pos.x > parent.getCentreX() ? pos.x - (w + 12) : pos.x + 12,
                                 pos.y > parent.getCentreY() ? pos.y - (h + 8) : pos.y + 18, w, h).constrainedWithin (parent);
}

void Look::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    auto r = juce::Rectangle<int> (w, h).toFloat().reduced (0.5f);
    g.setColour (box.isMouseOver (true) ? col::carril.brighter (0.15f) : col::fondo2); g.fillRoundedRectangle (r, 5.0f);
    g.setColour (col::lina); g.drawRoundedRectangle (r, 5.0f, 1.0f);
    const float ax = (float) w - 16.0f, ay = (float) h * 0.5f;
    juce::Path p; p.startNewSubPath (ax - 4.5f, ay - 2.0f); p.lineTo (ax, ay + 2.5f); p.lineTo (ax + 4.5f, ay - 2.0f);
    g.setColour (col::neboa); g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void Look::positionComboBoxText (juce::ComboBox& box, juce::Label& l)
{
    l.setBounds (10, 1, box.getWidth() - 30, box.getHeight() - 2);
    l.setFont (getComboBoxFont (box));
}

void Look::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    g.fillAll (col::fondo2);
    g.setColour (col::lina); g.drawRect (0, 0, w, h);
}

// ================================================================ pantalla
void Display::update()
{
    int s1, n1, s2, n2;
    proc.fifo.prepareToRead (proc.fifo.getNumReady(), s1, n1, s2, n2);
    auto take = [this] (const TuneProcessor::Frame& f)
    {
        hist[(size_t) head] = f;
        head = (head + 1) % histSize;
        if (f.voiced) centre += (f.outMidi - centre) * 0.03f;
    };
    for (int i = 0; i < n1; ++i) take (proc.frames[(size_t) (s1 + i)]);
    for (int i = 0; i < n2; ++i) take (proc.frames[(size_t) (s2 + i)]);
    proc.fifo.finishedRead (n1 + n2);
    const float goal = std::round (centre);
    shown += (goal - shown) * 0.12f;
    if (std::abs (goal - shown) < 0.005f) shown = goal;

    if (proc.nowVoiced.load())
    {
        voiced = true; hold = 10;
        target = proc.nowTarget.load();
        hz = proc.nowHz.load();
        inDev = (proc.nowIn.load() - (float) target) * 100.0f;
        outDev = (proc.nowOut.load() - (float) target) * 100.0f;
    }
    else if (hold > 0 && --hold == 0) voiced = false;

    auto toLv = [] (float peak) { return juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (peak, -60.0f) + 48.0f) / 48.0f); };
    inLv = juce::jmax (toLv (proc.inLevel.load()), inLv * 0.88f);
    outLv = juce::jmax (toLv (proc.outLevel.load()), outLv * 0.88f);
    repaint();
}

void Display::paintMeter (juce::Graphics& g, juce::Rectangle<float> r, float level, const juce::String& label)
{
    auto lab = r.removeFromBottom (20.0f);
    g.setFont (body (13.0f)); g.setColour (col::neboa);
    g.drawText (label, lab.expanded (20.0f, 0), juce::Justification::centredBottom, false);
    r.removeFromBottom (6.0f);
    g.setColour (col::lina); g.fillRoundedRectangle (r, 3.0f);
    auto fill = r.withTop (r.getBottom() - r.getHeight() * level);
    g.setColour (level > 0.875f ? col::sodio : col::escuma.withAlpha (0.85f));   // por riba de −6 dB, en sodio
    g.fillRoundedRectangle (fill, 3.0f);
}

void Display::paintArc (juce::Graphics& g, juce::Rectangle<float> area)
{
    const float R = juce::jmin (area.getWidth() * 0.5f - 10.0f, area.getHeight() - 40.0f);
    const juce::Point<float> c (area.getCentreX(), area.getY() + 18.0f + R);
    const float span = 1.3f;    // radiáns a cada lado (±75°)
    auto angOf = [span] (float cents) { return juce::jlimit (-50.0f, 50.0f, cents) / 50.0f * span; };
    auto at = [&] (float ang, float rad) { return juce::Point<float> (c.x + rad * std::sin (ang), c.y - rad * std::cos (ang)); };

    juce::Path arc; arc.addCentredArc (c.x, c.y, R, R, 0.0f, -span, span, true);
    g.setColour (col::lina); g.strokePath (arc, juce::PathStrokeType (2.0f));
    for (int cts = -50; cts <= 50; cts += 5)
    {
        const bool major = cts % 25 == 0;
        const float a = angOf ((float) cts);
        g.setColour (major ? col::neboa : col::lina);
        g.drawLine ({ at (a, R - (major ? 12.0f : 6.0f)), at (a, R) }, major ? 1.6f : 1.0f);
        if (major)
        {
            g.setFont (body (12.5f));
            const auto p = at (a, R + 14.0f);
            g.drawText (cts == 0 ? juce::String ("0") : (cts > 0 ? "+" : U ("−")) + juce::String (std::abs (cts)),
                        juce::Rectangle<float> (p.x - 18.0f, p.y - 8.0f, 36.0f, 16.0f), juce::Justification::centred, false);
        }
    }
    if (voiced)
    {
        // a túa voz: agulla fina de sodio; a afinada: agulla grosa de escuma
        const float ai = angOf (inDev), ao = angOf (outDev);
        g.setColour (col::sodio);
        g.drawLine ({ at (ai, R * 0.72f), at (ai, R - 1.0f) }, 2.0f);
        g.setColour (col::escuma.withAlpha (0.18f));
        g.drawLine ({ at (ao, R * 0.66f), at (ao, R + 4.0f) }, 9.0f);
        g.setColour (col::escuma);
        g.drawLine ({ at (ao, R * 0.66f), at (ao, R + 4.0f) }, 4.0f);
    }

    // a nota
    auto noteBox = juce::Rectangle<float> (c.x - R * 0.6f, c.y - R * 0.62f, R * 1.2f, R * 0.62f);
    if (voiced && target >= 0)
    {
        const auto name = scales::noteName (target);
        const auto oct = juce::String (target / 12 - 2);
        const auto big = cond (88.0f), small = cond (34.0f);
        const float wn = juce::GlyphArrangement::getStringWidth (big, name), wo = juce::GlyphArrangement::getStringWidth (small, oct);
        const float x0 = c.x - (wn + wo + 4.0f) * 0.5f;
        g.setFont (big); g.setColour (col::escuma);
        g.drawText (name, juce::Rectangle<float> (x0, noteBox.getY(), wn + 4.0f, noteBox.getHeight()), juce::Justification::bottomLeft, false);
        g.setFont (small); g.setColour (col::neboa);
        g.drawText (oct, juce::Rectangle<float> (x0 + wn + 4.0f, noteBox.getY(), wo + 4.0f, noteBox.getHeight() - 12.0f), juce::Justification::bottomLeft, false);
    }
    else
    {
        g.setFont (cond (44.0f)); g.setColour (col::neboa.withAlpha (0.6f));
        g.drawText (U ("Canta algo"), noteBox, juce::Justification::centredBottom, false);
    }
    auto info = juce::Rectangle<float> (c.x - R, c.y + 6.0f, R * 2.0f, 20.0f);
    g.setFont (body (15.5f)); g.setColour (col::neboa);
    if (voiced)
    {
        const int cc = juce::roundToInt (outDev - inDev);
        g.drawText (num (hz, 1) + " Hz", info.removeFromLeft (R), juce::Justification::centredLeft, false);
        g.drawText (U ("corrixe ") + (cc > 0 ? "+" : "") + juce::String (cc).replace ("-", U ("−")) + " c", info, juce::Justification::centredRight, false);
    }
}

void Display::paintTrace (juce::Graphics& g, juce::Rectangle<float> r)
{
    g.setColour (col::noite.withAlpha (0.45f)); g.fillRoundedRectangle (r, 5.0f);
    const float span = 12.0f;
    const float laneH = r.getHeight() / span;
    auto Y = [&] (float m) { return r.getBottom() - (m - (shown - span * 0.5f)) * laneH; };
    const int mode = (int) proc.params.getRawParameterValue ("modo")->load();
    uint16_t mask = proc.effectiveMask() | proc.freeMask();
    const int held = proc.nowHeld.load();
    if (mode > 0 && held != 0) mask = (uint16_t) held;

    g.saveState();
    g.reduceClipRegion (r.toNearestInt());
    g.setFont (body (11.5f));
    float lastLabel = 1e9f;
    for (int k = (int) std::floor (shown - span * 0.5f) - 1; k <= (int) std::ceil (shown + span * 0.5f) + 1; ++k)
    {
        const bool ok = (mask >> (((k % 12) + 12) % 12)) & 1;
        const float y = Y ((float) k);
        g.setColour (ok ? col::escuma.withAlpha (0.16f) : col::lina.withAlpha (0.35f));
        g.fillRect (r.getX() + 36.0f, y - 0.5f, r.getWidth() - 40.0f, 1.0f);
        if (ok && lastLabel - y >= 13.0f)
        {
            lastLabel = y;
            g.setColour (col::neboa);
            g.drawText (scales::noteWithOctave (k), juce::Rectangle<float> (r.getX() + 4.0f, y - 7.0f, 32.0f, 14.0f), juce::Justification::centredLeft, false);
        }
    }
    auto pathOf = [&] (bool out)
    {
        juce::Path p; bool pen = false;
        for (int i = 0; i < histSize; ++i)
        {
            const auto& f = hist[(size_t) ((head + i) % histSize)];
            const float x = r.getX() + 40.0f + (r.getWidth() - 50.0f) * (float) i / (float) (histSize - 1);
            if (! f.voiced) { pen = false; continue; }
            const float y = Y (out ? f.outMidi : f.inMidi);
            if (! pen) { p.startNewSubPath (x, y); pen = true; } else p.lineTo (x, y);
        }
        return p;
    };
    g.setColour (col::sodio.withAlpha (0.85f));
    g.strokePath (pathOf (false), juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (col::escuma);
    g.strokePath (pathOf (true), juce::PathStrokeType (2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.restoreState();
}

void Display::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (col::fondo2); g.fillRoundedRectangle (r, 8.0f);
    auto in = r.reduced (18.0f, 16.0f);
    paintMeter (g, in.removeFromLeft (14.0f), inLv, "entrada");
    paintMeter (g, in.removeFromRight (14.0f), outLv, U ("saída"));
    in.reduce (20.0f, 0);
    auto trace = in.removeFromRight (in.getWidth() * 0.42f);
    paintArc (g, in.withTrimmedRight (16.0f));
    paintTrace (g, trace);
}

// ================================================================ teclado
juce::Rectangle<float> Keyboard::keyRect (int note) const
{
    auto r = getLocalBounds().toFloat();
    int whites = 0; for (int n = lo; n <= hi; ++n) if (! isBlack (n)) ++whites;
    const float ww = r.getWidth() / (float) whites;
    int wi = 0; for (int n = lo; n < note; ++n) if (! isBlack (n)) ++wi;
    if (! isBlack (note)) return { r.getX() + ww * (float) wi, r.getY(), ww, r.getHeight() };
    const float bw = ww * 0.6f;
    return { r.getX() + ww * (float) wi - bw * 0.5f, r.getY(), bw, stripH + 3.0f + (r.getHeight() - stripH - 3.0f) * 0.6f };
}

int Keyboard::noteAt (juce::Point<float> p) const
{
    for (int n = lo; n <= hi; ++n) if (isBlack (n) && keyRect (n).contains (p)) return n;
    for (int n = lo; n <= hi; ++n) if (! isBlack (n) && keyRect (n).contains (p)) return n;
    return -1;
}

void Keyboard::paint (juce::Graphics& g)
{
    const uint16_t legal = proc.effectiveMask(), freeM = proc.freeMask();
    const int held = proc.nowHeld.load();
    const int target = proc.nowVoiced.load() ? proc.nowTarget.load() : -1;
    double fLo, fHi;
    tune::Engine::rangeHz ((int) proc.params.getRawParameterValue ("rango")->load(), fLo, fHi);
    const float mLo = hzToMidi (fLo), mHi = hzToMidi (fHi);

    for (int pass = 0; pass < 2; ++pass)
        for (int n = lo; n <= hi; ++n)
        {
            const bool black = isBlack (n);
            if ((pass == 1) != black) continue;
            const int pc = ((n % 12) + 12) % 12;
            const bool inScale = (legal >> pc) & 1, free = (freeM >> pc) & 1;
            auto k = keyRect (n).reduced (black ? 0.0f : 1.0f, 0.0f);
            // tira de estado
            auto strip = k.removeFromTop (stripH);
            k.removeFromTop (3.0f);
            const juce::Colour stripC = free ? col::sodio : (inScale ? col::escuma : col::lina);
            g.setColour (stripC.withAlpha (n == hover ? 1.0f : 0.9f));
            g.fillRoundedRectangle (strip.reduced (black ? 1.0f : 0.5f, 0), 2.0f);
            // tecla
            juce::Colour c = black ? (inScale || free ? juce::Colour (0xff04060c) : juce::Colour (0xff1d2438))
                                   : (inScale || free ? col::escuma : juce::Colour (0xff4a5268));
            if (n == hover) c = black ? c.brighter (0.3f) : c.darker (0.07f);
            if (n == target) c = col::sodio;
            g.setColour (c);
            g.fillRoundedRectangle (k, black ? 2.5f : 3.0f);
            if (free && n != target)
            {
                // libre: raias de sodio
                g.saveState(); g.reduceClipRegion (k.toNearestInt());
                g.setColour (col::sodio.withAlpha (black ? 0.55f : 0.45f));
                for (float x = k.getX() - k.getHeight(); x < k.getRight(); x += 7.0f)
                    g.drawLine (x, k.getBottom(), x + k.getHeight(), k.getY(), 1.5f);
                g.restoreState();
            }
            // fóra do rango da voz: máis escuro
            if (n < mLo - 0.5f || n > mHi + 0.5f)
            {
                g.setColour (col::noite.withAlpha (0.5f));
                g.fillRoundedRectangle (k, 3.0f);
            }
            if ((held >> pc) & 1)
            {
                g.setColour (col::sodio);
                g.fillEllipse (k.getCentreX() - 4.0f, k.getBottom() - (black ? 14.0f : 30.0f), 8.0f, 8.0f);
            }
            if (! black && pc == 0)
            {
                g.setFont (semi (12.0f));
                g.setColour (n == target ? col::noite : (inScale ? juce::Colour (0xff3a4258) : col::escuma.withAlpha (0.6f)));
                g.drawText (scales::noteWithOctave (n), k.withTrimmedBottom (4.0f), juce::Justification::centredBottom, false);
            }
        }
}

void Keyboard::mouseMove (const juce::MouseEvent& e)
{
    const int n = noteAt (e.position);
    if (n != hover) { hover = n; repaint(); }
    setMouseCursor (n >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void Keyboard::mouseDown (const juce::MouseEvent& e)
{
    const int n = noteAt (e.position);
    if (n < 0) return;
    if (e.mods.isPopupMenu() || e.mods.isAltDown()) proc.toggleFreeNote (n);
    else proc.toggleScaleNote (n % 12);
    repaint();
}

juce::String Keyboard::getTooltip()
{
    if (hover < 0) return {};
    const int pc = hover % 12;
    const bool inScale = (proc.effectiveMask() >> pc) & 1, free = (proc.freeMask() >> pc) & 1;
    juce::String st = free ? U (" está libre: se cantas nela, non se corrixe.") : inScale ? U (" está na escala.") : U (" está fóra da escala.");
    return scales::noteName (pc) + st + U (" Clic para poñela ou quitala da escala (en todas as oitavas); clic dereito para deixala libre.");
}

// ================================================================ potenciómetro
Knob::Knob()
{
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    slider.setMouseDragSensitivity (220);
    addAndMakeVisible (slider);
}

void Knob::resized() { slider.setBounds (getLocalBounds().withTrimmedBottom (24).withSizeKeepingCentre (66, 66)); }

void Knob::paint (juce::Graphics& g)
{
    g.setFont (body (14.5f));
    g.setColour (slider.isEnabled() ? col::escuma.withAlpha (0.85f) : col::neboa.withAlpha (0.6f));
    g.drawText (name, getLocalBounds().removeFromBottom (22), juce::Justification::centred, false);
}
} // namespace ui

// ================================================================ editor
namespace lay
{
    // grupos de potenciómetros (x inicial, ancho) e a súa etiqueta
    constexpr int knobW = 80, linkW = 46, gap = 18, rowY = 344;
    constexpr int g1 = 28, g2 = g1 + knobW * 2 + linkW + gap, g3 = g2 + knobW * 2 + gap, g4 = g3 + knobW * 3 + gap;
}

TuneEditor::TuneEditor (TuneProcessor& p) : AudioProcessorEditor (p), proc (p)
{
    using namespace ui;
    setLookAndFeel (&look);
    addAndMakeVisible (display);
    addAndMakeVisible (keyboard);
    display.setTooltip (U ("Á esquerda, a nota á que vai a voz: a agulla laranxa é a túa voz e a branca a afinada (en cents). "
                           "Á dereita, o ton nos últimos segundos."));

    for (int i = 0; i < 12; ++i) tonica.addItem (scales::noteName (i), i + 1);
    for (int i = 0; i < (int) scales::all().size(); ++i) escala.addItem (U (scales::all()[(size_t) i].name), i + 1);
    const char* voces[] = { "Baixo", "Barítono", "Tenor", "Alto", "Mezzo", "Soprano", "Calquera" };
    for (int i = 0; i < 7; ++i) voz.addItem (U (voces[i]), i + 1);
    tonicaAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.params, "tonica", tonica);
    escalaAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.params, "escala", escala);
    vozAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.params, "rango", voz);
    tonica.setTooltip (U ("Tónica da escala."));
    escala.setTooltip (U ("Escala. Cromática corrixe a calquera semitono; «Personalizada» son as notas que acendas no teclado."));
    voz.setTooltip (U ("Tipo de voz: o afinador só busca notas no seu rango (máis rápido e sen saltos de oitava). No teclado, o que queda fóra vese máis escuro."));
    for (auto* c : { &tonica, &escala, &voz }) addAndMakeVisible (c);

    detect.setButtonText ("Detectar");
    detect.setTooltip (U ("Canta uns segundos: Barullo Tune escoita a melodía e escolle a tónica e a escala. Volve pulsar para cancelar."));
    detect.onClick = [this] { if (proc.isKeyListening()) proc.stopKeyListening(); else proc.startKeyListening(); refresh(); };
    addAndMakeVisible (detect);

    const char* modes[3] = { "Escala", "MIDI escala", "MIDI nota" };
    const char* modeTips[3] = {
        "Leva a voz á nota máis próxima da escala.",
        "As notas que toques no teclado MIDI fan de escala, en calquera oitava. Sen notas tocadas, usa a escala.",
        "A voz vai á nota exacta que toques no teclado MIDI, na oitava máis próxima. Sen notas tocadas, usa a escala." };
    setupSegment (modeBtn, 3, "modo", modes, modeTips);
    const char* cals[2] = { "Directo", "Estudio" };
    const char* calTips[2] = {
        "Sen latencia (menos de 1 ms): para cantar en directo e gravar escoitándote. Ao transpor moito, a voz soa a esquío.",
        "Mantén o timbre aínda transpondo unha oitava e deixa mover os formantes. Ten algo de latencia, que o DAW compensa ao reproducir." };
    setupSegment (calBtn, 2, "calidade", cals, calTips);

    setupKnob (vel, "velocidade", "Velocidade",
               "Canto tarda en levar a voz á nota dentro de cada nota. 0 ms: ao momento. Para o efecto robot: velocidade e transición a 0 e vibrato a 0 %.");
    setupKnob (transi, "transicion", U ("Transición").toRawUTF8(),
               "Canto tarda en pasar dunha nota á seguinte cando cambias de nota. Baixo, saltos secos; alto, portamentos suaves.");
    setupKnob (tol, "tolerancia", "Cents",
               "Desafinacións máis pequenas ca isto non se tocan: a voz queda como a cantaches. Desactivada á esquerda do todo.");
    setupKnob (tolT, "toltempo", "Tempo",
               "As desafinacións máis curtas ca isto tampouco se tocan (adornos, ataques). Desactivada á esquerda do todo.");
    setupKnob (vib, "vibrato", "Vibrato", "Canto vibrato da túa voz pasa á saída: 0 % o aplana, 100 % déixao igual, 200 % dóbrao.");
    setupKnob (cant, "cantidade", "Cantidade", "Canto se corrixe: 100 % leva a voz xusto á nota; 50 % deixa a metade da desafinación.");
    setupKnob (form, "formantes", "Formantes", "Move o timbre sen cambiar a nota: para arriba, voz máis pequena e brillante; para abaixo, máis grande e escura. Só en Estudio.");
    setupKnob (trans, "transpor", "Transpor", "Sobe ou baixa a voz en semitonos. En Estudio mantén o timbre; en Directo soa a esquío.");
    setupKnob (la, "la", "La", "Afinación de referencia do La4.");
    la.slider.getProperties().set ("centre", 440.0);
    setupKnob (mest, "mestura", "Mestura", "Canto de voz afinada. Por baixo de 100 % mestúrase coa orixinal, aliñada no tempo.");
    setupKnob (vol, "volume", "Volume", "Volume de saída.");

    link.setButtonText ("ligar");
    link.getProperties().set ("fontSize", 13.0f);
    link.setTooltip (U ("Liga velocidade e transición: ao mover unha, a outra móvese na mesma proporción."));
    link.onClick = [this] { proc.setParamPlain ("ligar", proc.params.getRawParameterValue ("ligar")->load() > 0.5f ? 0.0f : 1.0f); refresh(); };
    addAndMakeVisible (link);
    vel.slider.onValueChange = [this]
    {
        const float v = (float) vel.slider.getValue();
        if (proc.params.getRawParameterValue ("ligar")->load() > 0.5f && lastVel > 0.0f && ! juce::approximatelyEqual (v, lastVel))
        {
            const float t = juce::jlimit (0.0f, 800.0f, (float) transi.slider.getValue() * (v / lastVel));
            lastTransi = t;
            transi.slider.setValue (t, juce::sendNotificationSync);
        }
        lastVel = v;
    };
    transi.slider.onValueChange = [this]
    {
        const float t = (float) transi.slider.getValue();
        if (proc.params.getRawParameterValue ("ligar")->load() > 0.5f && lastTransi > 0.0f && ! juce::approximatelyEqual (t, lastTransi))
        {
            const float v = juce::jlimit (0.0f, 800.0f, (float) vel.slider.getValue() * (t / lastTransi));
            lastVel = v;
            vel.slider.setValue (v, juce::sendNotificationSync);
        }
        lastTransi = t;
    };
    lastVel = (float) vel.slider.getValue(); lastTransi = (float) transi.slider.getValue();

    setSize (1040, 664);
    refresh();
    startTimerHz (30);
}

TuneEditor::~TuneEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void TuneEditor::setupKnob (ui::Knob& k, const char* id, const char* name, const char* tip)
{
    k.name = U (name);
    k.att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.params, id, k.slider);
    if (auto* prm = proc.params.getParameter (id)) k.slider.setDoubleClickReturnValue (true, prm->convertFrom0to1 (prm->getDefaultValue()));
    k.slider.setTooltip (U (tip));
    addAndMakeVisible (k);
}

void TuneEditor::setupSegment (juce::TextButton* b, int count, const char* paramId, const char* const* names, const char* const* tips)
{
    const juce::String id (paramId);
    for (int i = 0; i < count; ++i)
    {
        b[i].setButtonText (U (names[i]));
        b[i].setTooltip (U (tips[i]));
        b[i].setClickingTogglesState (false);
        int edges = 0;
        if (i > 0) edges |= juce::Button::ConnectedOnLeft;
        if (i < count - 1) edges |= juce::Button::ConnectedOnRight;
        b[i].setConnectedEdges (edges);
        b[i].onClick = [this, id, i] { proc.setParamPlain (id, (float) i); refresh(); };
        addAndMakeVisible (b[i]);
    }
}

void TuneEditor::refresh()
{
    const int m = (int) proc.params.getRawParameterValue ("modo")->load();
    const int q = (int) proc.params.getRawParameterValue ("calidade")->load();
    for (int i = 0; i < 3; ++i) modeBtn[i].setToggleState (i == m, juce::dontSendNotification);
    for (int i = 0; i < 2; ++i) calBtn[i].setToggleState (i == q, juce::dontSendNotification);
    link.setToggleState (proc.params.getRawParameterValue ("ligar")->load() > 0.5f, juce::dontSendNotification);
    if (form.slider.isEnabled() != (q == 1)) { form.slider.setEnabled (q == 1); form.repaint(); }

    if (proc.isKeyListening())
    {
        detect.setButtonText (U ("Escoitando… ") + juce::String (juce::roundToInt (proc.keyProgress() * 100.0f)) + " %");
        detect.getProperties().set ("progress", juce::jmax (0.02f, proc.keyProgress()));
    }
    else
    {
        detect.setButtonText ("Detectar");
        detect.getProperties().set ("progress", 0.0f);
    }
    detect.repaint();
}

void TuneEditor::timerCallback()
{
    proc.finishKeyIfReady();
    display.update();
    keyboard.repaint();
    refresh();
    repaint (790, 60, 222, 18);   // latencia
}

void TuneEditor::paint (juce::Graphics& g)
{
    using namespace ui;
    g.fillAll (col::noite);

    g.setFont (cond (34.0f));
    g.setColour (col::escuma);
    g.drawText ("Barullo Tune", juce::Rectangle<float> (28.0f, 20.0f, 220.0f, 42.0f), juce::Justification::centredLeft, false);

    auto small = [&] (const juce::String& t, juce::Rectangle<int> r, juce::Justification j = juce::Justification::centredLeft)
    {
        g.setFont (body (13.5f)); g.setColour (col::neboa); g.drawText (t, r, j, false);
    };
    const float lat = proc.latencyMs();
    small (U ("latencia ") + (lat < 1.0f ? U ("menos de 1 ms") : num (lat, 0) + " ms"), { 790, 60, 222, 18 }, juce::Justification::centred);

    // grupos de potenciómetros, coa súa etiqueta e unha liña entre eles
    using namespace lay;
    auto group = [&] (int x, int w, const char* t)
    {
        g.setFont (semi (14.0f)); g.setColour (col::neboa);
        g.drawText (U (t), juce::Rectangle<int> (x, rowY - 2, w, 18), juce::Justification::centred, false);
        g.setColour (col::lina);
        g.fillRect (x + 8, rowY + 8, (w - 16 - (int) juce::GlyphArrangement::getStringWidth (semi (14.0f), U (t))) / 2 - 8, 1);
        const int tw = (int) juce::GlyphArrangement::getStringWidth (semi (14.0f), U (t));
        g.fillRect (x + w / 2 + tw / 2 + 8, rowY + 8, (w - 16 - tw) / 2 - 8, 1);
    };
    group (g1, knobW * 2 + linkW, "Afinar");
    group (g2, knobW * 2, "Tolerancia");
    group (g3, knobW * 3, "Voz");
    group (g4, knobW * 4, U ("Saída").toRawUTF8());

    // teclado: lenda
    const int ly = 504;
    auto swatch = [&] (int x, juce::Colour c, const char* t, bool hatch)
    {
        auto r = juce::Rectangle<float> ((float) x, (float) ly + 3.0f, 18.0f, 10.0f);
        g.setColour (c); g.fillRoundedRectangle (r, 2.0f);
        if (hatch) { g.setColour (col::noite.withAlpha (0.35f)); for (float xx = r.getX() - 6; xx < r.getRight(); xx += 5) g.drawLine (xx, r.getBottom(), xx + 10, r.getY(), 1.2f); }
        small (U (t), { x + 24, ly, 90, 16 });
    };
    swatch (642, col::escuma, "na escala", false);
    swatch (746, col::sodio, "libre", false);
    swatch (828, col::lina, U ("fóra").toRawUTF8(), false);
    small (U ("clic dereito: libre"), { 900, ly, 112, 16 }, juce::Justification::centredRight);

    g.setFont (body (12.0f)); g.setColour (col::neboa.withAlpha (0.5f));
    g.drawText (U ("Escoita   42°14′N 8°43′W"), juce::Rectangle<int> (712, 644, 300, 16), juce::Justification::centredRight, false);
}

void TuneEditor::resized()
{
    using namespace lay;
    // cabeceira
    detect.setBounds (246, 24, 126, 34);
    tonica.setBounds (384, 24, 84, 34);
    escala.setBounds (476, 24, 172, 34);
    voz.setBounds (656, 24, 122, 34);
    for (int i = 0; i < 2; ++i) calBtn[i].setBounds (790 + i * 111 - (i > 0 ? 1 : 0), 24, 111 + (i > 0 ? 1 : 0), 34);

    display.setBounds (28, 84, 984, 240);

    // potenciómetros
    const int ky = rowY + 20, kh = 102;
    vel.setBounds (g1, ky, knobW, kh);
    link.setBounds (g1 + knobW + 3, ky + 22, linkW - 6, 26);
    transi.setBounds (g1 + knobW + linkW, ky, knobW, kh);
    tol.setBounds (g2, ky, knobW, kh);
    tolT.setBounds (g2 + knobW, ky, knobW, kh);
    vib.setBounds (g3, ky, knobW, kh);
    cant.setBounds (g3 + knobW, ky, knobW, kh);
    form.setBounds (g3 + knobW * 2, ky, knobW, kh);
    trans.setBounds (g4, ky, knobW, kh);
    la.setBounds (g4 + knobW, ky, knobW, kh);
    mest.setBounds (g4 + knobW * 2, ky, knobW, kh);
    vol.setBounds (g4 + knobW * 3, ky, knobW, kh);

    // modo e teclado
    for (int i = 0; i < 3; ++i) modeBtn[i].setBounds (28 + i * 104 - (i > 0 ? 1 : 0), 494, 104 + (i > 0 ? 1 : 0), 30);
    keyboard.setBounds (28, 532, 984, 108);
}
