#include "PluginEditor.h"
#include "BinaryData.h"

namespace
{
    juce::String U (const char* s) { return T (s); }   // todos os textos pasan pola táboa dos idiomas
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
    if (b.hasKeyboardFocus (false)) { g.setColour (col::sodio); g.strokePath (shape, juce::PathStrokeType (1.6f)); }
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
    if (s.hasKeyboardFocus (false))   // co foco do teclado: un aro de sodio
    {
        g.setColour (col::sodio.withAlpha (0.6f));
        g.drawEllipse (c.x - rad - 3.0f, c.y - rad - 3.0f, (rad + 3.0f) * 2.0f, (rad + 3.0f) * 2.0f, 1.2f);
    }
    const juce::Point<float> tip (c.x + (rad - 9.0f) * std::sin (ang), c.y - (rad - 9.0f) * std::cos (ang));
    g.setColour (s.isEnabled() ? col::escuma : col::neboa);
    g.fillEllipse (tip.x - 2.5f, tip.y - 2.5f, 5.0f, 5.0f);
    g.setFont (semi (13.5f));
    g.drawText (s.getTextFromValue (s.getValue()).replaceCharacter ('.', ','), r.withSizeKeepingCentre (rad * 2.0f - 8.0f, 18.0f), juce::Justification::centred, false);
}

// barras (as armonías): o valor de cheo desde o cero (ou desde o centro, se vai de − a +) e o texto no medio
void Look::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float, juce::Slider::SliderStyle, juce::Slider& s)
{
    auto r = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (0.5f);
    g.setColour (col::fondo2); g.fillRoundedRectangle (r, 4.0f);
    const bool bip = s.getMinimum() < 0 && s.getMaximum() > 0 && ! s.getProperties().contains ("fromMin");
    const float zero = bip ? r.getX() + (float) s.valueToProportionOfLength (0.0) * r.getWidth() : r.getX();
    const float a = juce::jmin (zero, pos), b = juce::jmax (zero, pos);
    if (b - a > 0.5f)
    {
        g.setColour ((s.isEnabled() ? col::sodio : col::neboa).withAlpha (0.55f));
        g.fillRoundedRectangle (juce::Rectangle<float> (a, r.getY(), b - a, r.getHeight()), 3.0f);
    }
    g.setColour (s.hasKeyboardFocus (false) ? col::sodio : col::lina); g.drawRoundedRectangle (r, 4.0f, s.hasKeyboardFocus (false) ? 1.6f : 1.0f);
    g.setColour (s.isEnabled() ? col::escuma : col::neboa);
    g.setFont (semi (13.5f));
    g.drawText (s.getTextFromValue (s.getValue()).replaceCharacter ('.', ','), r.toNearestInt(), juce::Justification::centred, false);
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
    g.setColour (box.hasKeyboardFocus (true) ? col::sodio : col::lina); g.drawRoundedRectangle (r, 5.0f, box.hasKeyboardFocus (true) ? 1.6f : 1.0f);
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

    // para os lectores de pantalla: a nota e a corrección (só cando cambian de verdade)
    {
        const int t = voiced ? target : -1, cc = voiced ? juce::roundToInt ((outDev - inDev) / 5.0f) * 5 : 0;
        if (t != spokenTarget || cc != spokenCents)
        {
            spokenTarget = t; spokenCents = cc;
            setTitle (t < 0 ? U ("Afinador: sen voz") : U ("Afinador: ") + scales::noteWithOctave (t) + U (", corrixe ") + juce::String (cc) + " cents");
            if (auto* h = getAccessibilityHandler()) h->notifyAccessibilityEvent (juce::AccessibilityEvent::titleChanged);
        }
    }

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
        const auto oct = juce::String (lang::octaveOf (target));
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
    paintMeter (g, in.removeFromLeft (14.0f), inLv, T ("entrada"));
    paintMeter (g, in.removeFromRight (14.0f), outLv, U ("saída"));
    in.reduce (20.0f, 0);
    auto trace = in.removeFromRight (in.getWidth() * 0.42f);
    paintArc (g, in.withTrimmedRight (16.0f));
    paintTrace (g, trace);
}

// ================================================================ práctica
PracticePanel::PracticePanel (TuneProcessor& p) : proc (p)
{
    reset.setButtonText (T ("Empezar de novo"));
    reset.onClick = [this] { proc.resetPractice(); repaint(); };
    close.setButtonText (T ("Pechar"));
    close.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible (reset);
    addAndMakeVisible (close);
    setTitle (T ("Informe"));
}

void PracticePanel::resized()
{
    close.setBounds (getWidth() - 100, 12, 86, 26);
    reset.setBounds (getWidth() - 236, 12, 128, 26);
}

void PracticePanel::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (col::fondo2); g.fillRoundedRectangle (r, 8.0f);
    g.setColour (col::lina); g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);
    const auto pr = proc.practice();
    g.setFont (cond (28.0f)); g.setColour (col::escuma);
    g.drawText (T ("Como afinas"), juce::Rectangle<float> (20.0f, 10.0f, 300.0f, 32.0f), juce::Justification::centredLeft, false);
    if (pr.seconds < 1.0f)
    {
        g.setFont (body (17.0f)); g.setColour (col::neboa);
        g.drawText (T ("Canta un anaco e aquí verás como afinas (a túa voz, antes de corrixila)."), r.reduced (20.0f, 60.0f), juce::Justification::centredLeft, true);
        return;
    }
    // á esquerda, as cifras
    auto stat = [&] (float y, const juce::String& big, const juce::String& label)
    {
        g.setFont (cond (30.0f)); g.setColour (col::escuma);
        g.drawText (big, juce::Rectangle<float> (20.0f, y, 110.0f, 32.0f), juce::Justification::centredLeft, false);
        g.setFont (body (14.5f)); g.setColour (col::neboa);
        g.drawText (label, juce::Rectangle<float> (130.0f, y + 4.0f, 230.0f, 26.0f), juce::Justification::centredLeft, false);
    };
    const int secs = juce::roundToInt (pr.seconds);
    stat (52.0f, juce::String (juce::roundToInt (pr.within10 * 100.0f)) + " %", T ("do tempo, a menos de 10 cents"));
    stat (88.0f, juce::String (juce::roundToInt (pr.within25 * 100.0f)) + " %", T ("a menos de 25 cents"));
    stat (124.0f, juce::String (juce::roundToInt (pr.meanAbs)) + " c", T ("de desviación media"));
    const int tend = juce::roundToInt (pr.meanSigned);
    stat (160.0f, (tend > 0 ? "+" : tend < 0 ? juce::String::fromUTF8 ("−") : "") + juce::String (std::abs (tend)) + " c",
          std::abs (tend) < 4 ? T ("centrada") : tend > 0 ? T ("tendes a ir alta") : T ("tendes a ir baixa"));
    g.setFont (body (13.5f)); g.setColour (col::neboa);
    g.drawText (T ("tempo cantado: ") + juce::String (secs / 60) + ":" + juce::String (secs % 60).paddedLeft ('0', 2),
                juce::Rectangle<float> (20.0f, r.getBottom() - 30.0f, 300.0f, 20.0f), juce::Justification::centredLeft, false);
    // á dereita, unha barra por nota: canto vai alta (arriba) ou baixa (abaixo)
    auto area = juce::Rectangle<float> (380.0f, 52.0f, r.getWidth() - 400.0f, r.getHeight() - 84.0f);
    const float zero = area.getCentreY(), half = area.getHeight() * 0.5f - 14.0f, bw = area.getWidth() / 12.0f;
    g.setColour (col::lina); g.fillRect (area.getX(), zero - 0.5f, area.getWidth(), 1.0f);
    g.setFont (body (11.5f));
    for (const int c : { -25, 25 })
    {
        const float y = zero - (float) c / 50.0f * half;
        g.setColour (col::lina.withAlpha (0.6f)); g.fillRect (area.getX(), y - 0.5f, area.getWidth(), 1.0f);
        g.setColour (col::neboa); g.drawText ((c > 0 ? "+" : juce::String::fromUTF8 ("−")) + "25", juce::Rectangle<float> (area.getX() - 34.0f, y - 8.0f, 30.0f, 16.0f), juce::Justification::centredRight, false);
    }
    for (int i = 0; i < 12; ++i)
    {
        const float x = area.getX() + bw * (float) i;
        g.setFont (semi (13.0f)); g.setColour (pr.noteSeconds[i] > 0.3f ? col::escuma : col::neboa.withAlpha (0.5f));
        g.drawText (scales::noteName (i), juce::Rectangle<float> (x, area.getBottom() - 2.0f, bw, 18.0f), juce::Justification::centred, false);
        if (pr.noteSeconds[i] <= 0.3f) continue;   // pouco cantada: sen barra
        const float m = juce::jlimit (-50.0f, 50.0f, pr.noteMean[i]), h = m / 50.0f * half;
        g.setColour (std::abs (m) > 10.0f ? col::sodio : col::escuma.withAlpha (0.85f));
        g.fillRoundedRectangle (juce::Rectangle<float> (x + bw * 0.25f, h > 0 ? zero - h : zero, bw * 0.5f, std::abs (h) + 1.0f), 2.0f);
    }
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

bool Keyboard::customTuning() const { return (int) proc.params.getRawParameterValue ("afinacion")->load() == 2; }

void Keyboard::paint (juce::Graphics& g)
{
    const uint16_t legal = proc.effectiveMask(), freeM = proc.freeMask();
    double cents[12]; proc.tuningCents (cents);
    const bool tuned = (int) proc.params.getRawParameterValue ("afinacion")->load() != 0;
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
            // cents da afinación (só no Do de cada oitava ás teclas brancas cabe; nas negras, se hai sitio)
            if (tuned && std::abs (cents[pc]) >= 0.5 && (inScale || free) && ! black)
            {
                g.setFont (semi (9.5f));
                g.setColour (col::noite);
                g.drawText ((cents[pc] > 0 ? "+" : U ("−")) + juce::String (juce::roundToInt (std::abs (cents[pc]))), strip, juce::Justification::centred, false);
            }
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
            if ((float) n < mLo - 0.5f || (float) n > mHi + 0.5f)
            {
                g.setColour (col::noite.withAlpha (0.5f));
                g.fillRoundedRectangle (k, 3.0f);
            }
            if ((held >> pc) & 1)
            {
                g.setColour (col::sodio);
                g.fillEllipse (k.getCentreX() - 4.0f, k.getBottom() - (black ? 14.0f : 30.0f), 8.0f, 8.0f);
            }
            if (n == focusNote && hasKeyboardFocus (false))
            {
                g.setColour (col::sodio);
                g.drawRoundedRectangle (k.expanded (black ? 0.0f : -1.0f), 3.0f, 2.0f);
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
    dragPc = -1;
    if (n < 0) return;
    if (e.mods.isShiftDown() && customTuning())
    {
        // maiúsculas + arrastrar: os cents desta nota (en todas as oitavas)
        dragPc = n % 12;
        dragStartCents = proc.params.getRawParameterValue ("cents" + juce::String (dragPc))->load();
        return;
    }
    if (e.mods.isPopupMenu() || e.mods.isAltDown()) proc.toggleFreeNote (n);
    else proc.toggleScaleNote (n % 12);
    repaint();
}

void Keyboard::mouseDrag (const juce::MouseEvent& e)
{
    if (dragPc < 0) return;
    const float c = juce::jlimit (-50.0f, 50.0f, dragStartCents - (float) e.getDistanceFromDragStartY() * 0.5f);
    if (auto* prm = proc.params.getParameter ("cents" + juce::String (dragPc)))
        prm->setValueNotifyingHost (prm->convertTo0to1 (std::round (c * 10.0f) / 10.0f));
    repaint();
}

juce::String Keyboard::describe (int note) const
{
    const int pc = ((note % 12) + 12) % 12;
    const bool inScale = (proc.effectiveMask() >> pc) & 1, free = (proc.freeMask() >> pc) & 1;
    juce::String d = scales::noteWithOctave (note) + (free ? U (": libre") : inScale ? U (": na escala") : U (": fóra da escala"));
    if (customTuning())
    {
        const float c = proc.params.getRawParameterValue ("cents" + juce::String (pc))->load();
        d << ", " << (c > 0 ? "+" : "") << juce::String (c, 1) << " cents";
    }
    return d;
}

void Keyboard::announce()
{
    if (focusNote < 0) return;
    setTitle (U ("Teclado da escala. ") + describe (focusNote));
    if (auto* h = getAccessibilityHandler()) h->notifyAccessibilityEvent (juce::AccessibilityEvent::titleChanged);
}

// sen rato: frechas para moverse (arriba e abaixo, unha oitava), espazo ou intro para
// poñer ou quitar a nota da escala, F para deixala libre; coa afinación Personalizada,
// maiúsculas + arriba/abaixo cambia os cents da nota (de 1 en 1)
bool Keyboard::keyPressed (const juce::KeyPress& k)
{
    if (focusNote < 0) focusNote = 60;
    const int code = k.getKeyCode();
    const bool shift = k.getModifiers().isShiftDown();
    if (shift && customTuning() && (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey))
    {
        const int pc = focusNote % 12;
        if (auto* prm = proc.params.getParameter ("cents" + juce::String (pc)))
        {
            const float c = juce::jlimit (-50.0f, 50.0f, prm->convertFrom0to1 (prm->getValue()) + (code == juce::KeyPress::upKey ? 1.0f : -1.0f));
            prm->beginChangeGesture(); prm->setValueNotifyingHost (prm->convertTo0to1 (c)); prm->endChangeGesture();
        }
    }
    else if (code == juce::KeyPress::leftKey) focusNote = juce::jmax (lo, focusNote - 1);
    else if (code == juce::KeyPress::rightKey) focusNote = juce::jmin (hi, focusNote + 1);
    else if (code == juce::KeyPress::upKey) focusNote = juce::jmin (hi, focusNote + 12);
    else if (code == juce::KeyPress::downKey) focusNote = juce::jmax (lo, focusNote - 12);
    else if (code == juce::KeyPress::spaceKey || code == juce::KeyPress::returnKey) proc.toggleScaleNote (focusNote % 12);
    else if (k.getTextCharacter() == 'f' || k.getTextCharacter() == 'F') proc.toggleFreeNote (focusNote);
    else return false;
    announce();
    repaint();
    return true;
}

juce::String Keyboard::getTooltip()
{
    if (hover < 0) return {};
    const int pc = hover % 12;
    const bool inScale = (proc.effectiveMask() >> pc) & 1, free = (proc.freeMask() >> pc) & 1;
    juce::String st = free ? U (" está libre: se cantas nela, non se corrixe.") : inScale ? U (" está na escala.") : U (" está fóra da escala.");
    juce::String tip = scales::noteName (pc) + st + U (" Clic para poñela ou quitala da escala (en todas as oitavas); clic dereito para deixala libre.");
    if (customTuning())
    {
        const float c = proc.params.getRawParameterValue ("cents" + juce::String (pc))->load();
        tip << U (" Afinación: ") << (c > 0 ? "+" : "") << juce::String (c, 1).replaceCharacter ('.', ',') << U (" c (maiúsculas + arrastrar arriba ou abaixo para cambiala).");
    }
    return tip;
}

// ================================================================ potenciómetro
Knob::Knob()
{
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    slider.setMouseDragSensitivity (220);
    addAndMakeVisible (slider);
}

// ---- escribir o valor e mover fino
void ValueSlider::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() || e.mods.isCommandDown() || e.mods.isCtrlDown()) { showEditor(); return; }
    normalSensitivity = getMouseDragSensitivity();
    if (e.mods.isShiftDown()) setMouseDragSensitivity (normalSensitivity * 5);   // fino
    juce::Slider::mouseDown (e);
}

void ValueSlider::mouseUp (const juce::MouseEvent& e)
{
    juce::Slider::mouseUp (e);
    setMouseDragSensitivity (normalSensitivity);
}

bool ValueSlider::keyPressed (const juce::KeyPress& k)
{
    if (k.getKeyCode() == juce::KeyPress::returnKey) { showEditor(); return true; }   // intro: escribir o valor
    return juce::Slider::keyPressed (k);
}

void ValueSlider::showEditor()
{
    if (param == nullptr || getParentComponent() == nullptr) return;
    editor = std::make_unique<juce::TextEditor>();
    editor->setJustification (juce::Justification::centred);
    editor->setFont (semi (15.0f));
    editor->setText (param->getCurrentValueAsText(), false);
    editor->selectAll();
    editor->setTitle (getTitle() + U (": escribir o valor"));
    auto* parent = getParentComponent();
    editor->setBounds (getBounds().withSizeKeepingCentre (juce::jmax (70, getWidth()), 24));
    parent->addAndMakeVisible (*editor);
    editor->grabKeyboardFocus();
    auto finish = [this] (bool apply)
    {
        if (editor == nullptr) return;
        const auto text = editor->getText();
        juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<ValueSlider> (this), text, apply]
        {
            if (sp == nullptr) return;
            if (apply) sp->applyText (text);
            sp->editor.reset();
            sp->grabKeyboardFocus();
        });
    };
    editor->onReturnKey = [finish] { finish (true); };
    editor->onEscapeKey = [finish] { finish (false); };
    editor->onFocusLost = [finish] { finish (true); };
}

bool ValueSlider::applyText (const juce::String& text)
{
    if (param == nullptr || text.trim().isEmpty()) return false;
    auto t = text.trim().replaceCharacter (',', '.').replace (juce::String::fromUTF8 ("−"), "-");
    setValue (param->convertFrom0to1 (param->getValueForText (t)), juce::sendNotificationSync);
    return true;
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
    constexpr int knobW = 80, linkW = 46, gap = 18;
    constexpr int tabY = 338, tabH = 28;
    constexpr int rowY = 384;              // etiquetas dos grupos
    constexpr int knobY = rowY + 14, knobH = 100;
    constexpr int rowB = 512;              // modo, tonalidade, voz e afinación
    constexpr int kbY = 550, kbH = 108;
    // armonías: unha fila por voz
    constexpr int hX = 132, hRowY = 400, hRowH = 22, hRowStep = 26, hBarX = hX + 168, hBarW = 136, hBarStep = 144;
    // cada pestana centrada
    constexpr int corrW = knobW * 2 + linkW + (knobW * 2) * 3 + gap * 3, corrX = (TuneEditor::W - corrW) / 2;
    constexpr int voiceW = knobW * 3 + knobW * 5 + gap, voiceX = (TuneEditor::W - voiceW) / 2;
    constexpr int outW = knobW * 3 + gap + knobW + 110 + gap + 150, outX = (TuneEditor::W - outW) / 2;
    constexpr int langX = outX + knobW * 3 + gap + knobW + 110 + gap;
}

void TuneEditor::ArrowButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    using namespace ui;
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (over || down ? col::carril.brighter (0.15f) : col::fondo2); g.fillRoundedRectangle (r, 5.0f);
    g.setColour (col::lina); g.drawRoundedRectangle (r, 5.0f, 1.0f);
    const auto c = r.getCentre();
    const float rad = 7.0f;
    juce::Path p;
    // arco de 3/4 de volta e unha punta de frecha
    const float a0 = isRedo ? -2.4f : 2.4f, a1 = isRedo ? 1.2f : -1.2f;
    p.addCentredArc (c.x, c.y + 1.0f, rad, rad, 0.0f, a0, a1, true);
    g.setColour (isEnabled() ? col::escuma : col::neboa.withAlpha (0.4f));
    g.strokePath (p, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    const juce::Point<float> tip (c.x + rad * std::sin (a1), c.y + 1.0f - rad * std::cos (a1));
    const float dir = isRedo ? 1.0f : -1.0f;
    juce::Path head;
    head.addTriangle (tip.x + dir * 4.0f, tip.y, tip.x - dir * 1.5f, tip.y - 4.5f, tip.x - dir * 1.5f, tip.y + 4.5f);
    g.fillPath (head);
}

TuneEditor::TuneEditor (TuneProcessor& p) : AudioProcessorEditor (p), proc (p)
{
    using namespace ui;
    setLookAndFeel (&look);
    addAndMakeVisible (canvas);
    canvas.painter = [this] (juce::Graphics& g) { paintCanvas (g); };
    canvas.addAndMakeVisible (display);
    canvas.addAndMakeVisible (keyboard);
    canvas.addChildComponent (practicePanel);
    practicePanel.onClose = [this] { practicePanel.setVisible (false); practiceBtn.setVisible (true); };
    practiceBtn.setButtonText (T ("Informe"));
    practiceBtn.getProperties().set ("fontSize", 13.0f);
    practiceBtn.setTooltip (T ("Como afinas ti (a voz que entra, sen corrixir): canto tempo estás preto da nota e en que notas tendes a ir alta ou baixa."));
    practiceBtn.onClick = [this]
    {
        practicePanel.setVisible (true);
        practiceBtn.setVisible (false);   // o panel ten o seu «Pechar»
    };
    canvas.addAndMakeVisible (practiceBtn);
    // accesibilidade: o tabulador percorre os controis; cada un co seu nome
    canvas.setFocusContainerType (juce::Component::FocusContainerType::keyboardFocusContainer);
    canvas.setTitle ("Barullo Tune");
    display.setTitle (U ("Afinador"));
    preset.setTitle ("Presets"); undoBtn.setTitle (U ("Desfacer")); redoBtn.setTitle (U ("Refacer"));
    display.setTooltip (U ("Á esquerda, a nota á que vai a voz: a agulla laranxa é a túa voz e a branca a afinada (en cents). "
                           "Á dereita, o ton nos últimos segundos."));

    // ---- cabeceira
    rebuildPresetMenu();
    preset.onChange = [this] { presetChosen(); };
    preset.setTooltip (U ("Presets: os de fábrica só cambian o carácter da corrección (non a tonalidade nin a voz); os teus gardan todo. Aquí tamén: gardar o actual como preset e copiar entre A e B."));
    canvas.addAndMakeVisible (preset);

    for (int i = 0; i < 2; ++i)
    {
        abBtn[i].setButtonText (i == 0 ? "A" : "B");
        abBtn[i].setConnectedEdges (i == 0 ? juce::Button::ConnectedOnRight : juce::Button::ConnectedOnLeft);
        abBtn[i].setTooltip (U ("A/B: dous xogos de axustes para comparar. Cambiar de lado garda o actual. Para copiar un lado no outro, no menú dos presets."));
        abBtn[i].onClick = [this, i] { if (i != proc.abCurrent()) proc.abSwitch(); refresh(); };
        canvas.addAndMakeVisible (abBtn[i]);
    }
    compare.setButtonText (T ("Comparar"));
    compare.setTooltip (U ("Mentres está aceso, escoitas a voz sen afinar (aliñada no tempo, sen armonías) para comparar."));
    compare.onClick = [this] { proc.setParamPlain ("comparar", proc.params.getRawParameterValue ("comparar")->load() > 0.5f ? 0.0f : 1.0f); refresh(); };
    canvas.addAndMakeVisible (compare);
    undoBtn.setTooltip (U ("Desfacer o último cambio."));
    redoBtn.setTooltip (U ("Refacer."));
    undoBtn.onClick = [this] { proc.undo.undo(); refresh(); };
    redoBtn.onClick = [this] { proc.undo.redo(); refresh(); };
    canvas.addAndMakeVisible (undoBtn);
    canvas.addAndMakeVisible (redoBtn);

    const char* cals[3] = { "Directo", "Preciso", "Estudio" };
    const char* calTips[3] = {
        "Sen latencia (menos de 1 ms): para cantar en directo escoitándote. Afina algo menos exacto cando a voz se move moito.",
        "12 ms de latencia: afina o dobre de exacto ca Directo, porque le a voz xa medida. Para gravar ou tocar en directo se non che molesta un pouco de atraso.",
        "O máis exacto e o mellor son ao transpor moito (copia ciclos enteiros da voz). Ten máis latencia (15–42 ms segundo a voz), que o DAW compensa ao reproducir." };
    setupSegment (calBtn, 3, "calidade", cals, calTips);

    // ---- pestanas
    const char* tabs[numTabs] = { "Corrección", "Voz", "Armonías", "Saída" };
    const char* tabTips[numTabs] = { "Como se afina: velocidade, transición, tolerancia, flexibilidade, humanizar.",
                                     "O timbre (formantes, transpor, aire) e o vibrato xerado.",
                                     "Ata catro voces novas a partir da túa.",
                                     "La de referencia, mestura, volume e MIDI." };
    for (int i = 0; i < numTabs; ++i)
    {
        tabBtn[i].setButtonText (U (tabs[i]));
        tabBtn[i].setTooltip (U (tabTips[i]));
        int edges = 0;
        if (i > 0) edges |= juce::Button::ConnectedOnLeft;
        if (i < numTabs - 1) edges |= juce::Button::ConnectedOnRight;
        tabBtn[i].setConnectedEdges (edges);
        tabBtn[i].onClick = [this, i] { showTab (i); };
        canvas.addAndMakeVisible (tabBtn[i]);
    }

    // ---- corrección
    setupKnob (vel, "velocidade", "Velocidade",
               "Canto tarda en levar a voz á nota dentro de cada nota. 0 ms: ao momento. Para o efecto robot: velocidade e transición a 0 e vibrato a 0 %.");
    setupKnob (transi, "transicion", U ("Transición").toRawUTF8(),
               "Canto tarda en pasar dunha nota á seguinte cando cambias de nota. Baixo, saltos secos; alto, portamentos suaves.");
    setupKnob (tol, "tolerancia", "Cents",
               "Desafinacións máis pequenas ca isto non se tocan: a voz queda como a cantaches. Desactivada á esquerda do todo.");
    setupKnob (tolT, "toltempo", "Tempo",
               "As desafinacións máis curtas ca isto tampouco se tocan (adornos, ataques). Desactivada á esquerda do todo.");
    setupKnob (flex, "flexibilidade", "Flexibilidade",
               "Canto máis lonxe da nota vai a voz, menos se corrixe: as blue notes, os arrastres e as notas que buscas a propósito quedan; o que está preto, afínase.");
    setupKnob (human, "humanizar", "Humanizar",
               "As notas curtas corríxense coa velocidade de sempre; nas longas, a corrección vaise relaxando e a nota respira.");
    setupKnob (vib, "vibrato", "Vibrato", "Canto vibrato da túa voz pasa á saída: 0 % o aplana, 100 % déixao igual, 200 % dóbrao.");
    setupKnob (cant, "cantidade", "Cantidade", "Canto se corrixe: 100 % leva a voz xusto á nota; 50 % deixa a metade da desafinación.");
    link.setButtonText (T ("ligar"));
    link.getProperties().set ("fontSize", 13.0f);
    link.setTooltip (U ("Liga velocidade e transición: ao mover unha, a outra móvese na mesma proporción."));
    link.onClick = [this] { proc.setParamPlain ("ligar", proc.params.getRawParameterValue ("ligar")->load() > 0.5f ? 0.0f : 1.0f); refresh(); };
    canvas.addAndMakeVisible (link);
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

    // ---- voz
    setupKnob (form, "formantes", "Formantes", "Move o timbre sen cambiar a nota: para arriba, voz máis pequena e brillante; para abaixo, máis grande e escura.");
    setupKnob (trans, "transpor", "Transpor", "Sobe ou baixa a voz en semitonos mantendo o timbre (sen voz de esquío), en calquera xeito.");
    setupKnob (air, "aire", "Aire", "Alento enriba da voz afinada (só cando hai voz).");
    setupKnob (vgDepth, "vibgen_fondo", "Fondo", "Vibrato xerado, enriba do da túa voz: canto sobe e baixa (en cents). Á esquerda do todo, sen vibrato xerado.");
    setupKnob (vgRate, "vibgen_velocidade", "Velocidade", "Vibrato xerado: cantas oscilacións por segundo.");
    setupKnob (vgDelay, "vibgen_retardo", "Retardo", "Vibrato xerado: canto tarda en entrar desde o comezo de cada nota (entra amodo, en 200 ms).");
    setupKnob (vgVar, "vibgen_variacion", U ("Variación").toRawUTF8(), "Vibrato xerado: a velocidade e o fondo cambian amodo, coma nunha voz de verdade.");
    setupKnob (vgTrem, "vibgen_tremolo", U ("Trémolo").toRawUTF8(), "Vibrato xerado: o nivel tamén oscila, ao mesmo compás.");
    for (auto* k : { &vgRate, &vgDelay, &vgVar, &vgTrem }) k->slider.getProperties().set ("centre", k == &vgRate ? 5.5 : 0.0);

    // ---- armonías
    setupKnob (principal, "principal", "Principal", "Nivel da voz principal (a túa, afinada). Á esquerda do todo, só as armonías.");
    const char* hm[2] = { "Escala", "Acorde MIDI" };
    const char* hmTips[2] = { "Cada armonía vai ao seu intervalo dentro da escala (a terceira dunha nota é a nota da escala dous pasos arriba).",
                              "Cada armonía vai a unha das teclas MIDI que toques (de grave a aguda)." };
    setupSegment (harmMode, 2, "armonia_modo", hm, hmTips);
    const char* bars[5] = { "_nivel", "_pan", "_desafinar", "_atraso", "_formantes" };
    const char* barTips[5] = { "Nivel desta armonía.", "Onde soa: esquerda ou dereita.", "Un pouco desafinada (cents), para que non soe pegada á principal.",
                               "Un pouco atrasada (ms), coma alguén que canta contigo.", "Formantes: máis grande ou máis pequena, coma outra persoa." };
    for (int h = 0; h < TuneProcessor::numHarmonies; ++h)
    {
        const juce::String id ("harm" + juce::String (h + 1));
        hOn[h].setButtonText (juce::String (h + 1));
        hOn[h].setClickingTogglesState (true);
        hOn[h].setTooltip (U ("Acende ou apaga a armonía ") + juce::String (h + 1) + ".");
        buttonAtts.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.params, id, hOn[h]));
        canvas.addAndMakeVisible (hOn[h]);
        hInt[h].addItemList (TuneProcessor::intervalNames(), 1);
        hInt[h].setTooltip (U ("Intervalo desta armonía respecto da túa voz."));
        comboAtts.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.params, id + "_intervalo", hInt[h]));
        canvas.addAndMakeVisible (hInt[h]);
        static const char* barNames[5] = { "nivel", "panorama", "desafinar", "atraso", "formantes" };
        for (int b = 0; b < 5; ++b) setupBar (hBar[h][b], (id + bars[b]).toRawUTF8(), U (barTips[b]), U ("Armonía ") + juce::String (h + 1) + ": " + T (barNames[b]));
        hOn[h].setTitle (U ("Armonía ") + juce::String (h + 1));
        hInt[h].setTitle (U ("Armonía ") + juce::String (h + 1) + ": " + T ("intervalo"));
        hBar[h][0].getProperties().set ("fromMin", true);   // o nivel énchese desde a esquerda
    }

    // ---- saída
    setupKnob (la, "la", "La", "Afinación de referencia do La4.");
    la.slider.getProperties().set ("centre", 440.0);
    setupKnob (mest, "mestura", "Mestura", "Canto de voz afinada. Por baixo de 100 % mestúrase coa orixinal, aliñada no tempo.");
    setupKnob (vol, "volume", "Volume", "Volume de saída.");
    setupKnob (bend, "bend_rango", "Bend", "MIDI nota: canto dobra a roda de pitch bend (semitonos). Con MPE, cada nota co bend da súa canle.");
    midiOut.setButtonText (U ("Notas MIDI"));
    midiOut.setClickingTogglesState (true);
    midiOut.setTooltip (U ("As notas que cantas saen como notas MIDI, para tocar un sintetizador coa voz."));
    buttonAtts.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.params, "midi_saida", midiOut));
    canvas.addAndMakeVisible (midiOut);
    for (int i = 0; i < lang::count; ++i) language.addItem (juce::String::fromUTF8 (lang::nameOf ((lang::Id) i)), i + 1);
    language.setSelectedId ((int) lang::current() + 1, juce::dontSendNotification);
    language.setTitle (T ("Idioma"));
    language.setTooltip (T ("O idioma da interface e dos nomes dos parámetros."));
    language.onChange = [this]
    {
        const int id = language.getSelectedId() - 1;
        if (id < 0 || id == (int) lang::current()) return;
        lang::set ((lang::Id) id);
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, T ("Idioma"),
            T ("O idioma cambia ao pechar e volver abrir a xanela; os nomes dos parámetros, ao volver cargar o plugin."));
    };
    canvas.addAndMakeVisible (language);

    // ---- modo, tonalidade, voz e afinación
    for (int i = 0; i < 12; ++i) tonica.addItem (scales::noteName (i), i + 1);
    for (int i = 0; i < (int) scales::all().size(); ++i) escala.addItem (U (scales::all()[(size_t) i].name), i + 1);
    const char* voces[] = { "Baixo", "Barítono", "Tenor", "Alto", "Mezzo", "Soprano", "Calquera" };
    for (int i = 0; i < 7; ++i) voz.addItem (U (voces[i]), i + 1);
    afin.addItem (T ("Igual"), 1); afin.addItem (T ("Xusta"), 2); afin.addItem (T ("Personalizada"), 3); afin.addItem ("MTS-ESP", 4);
    tonicaAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.params, "tonica", tonica);
    escalaAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.params, "escala", escala);
    vozAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.params, "rango", voz);
    afinAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.params, "afinacion", afin);
    tonica.setTooltip (U ("Tónica da escala."));
    escala.setTooltip (U ("Escala. Cromática corrixe a calquera semitono; «Personalizada» son as notas que acendas no teclado."));
    voz.setTooltip (U ("Tipo de voz: o afinador só busca notas no seu rango (máis rápido e sen saltos de oitava). No teclado, o que queda fóra vese máis escuro."));
    afin.setTooltip (U ("Afinación: Igual (os doce semitonos iguais), Xusta (respecto da tónica: terceiras e quintas puras), Personalizada (os cents de cada nota: maiúsculas + arrastrar no teclado) ou MTS-ESP (a que diga o mestre de MTS-ESP, ou as mensaxes MIDI de afinación)."));
    for (auto* c : { &tonica, &escala, &voz, &afin }) canvas.addAndMakeVisible (c);
    tonica.setTitle (U ("Tónica")); escala.setTitle (U ("Escala")); voz.setTitle (U ("Tipo de voz")); afin.setTitle (U ("Afinación"));
    scl.setButtonText (".scl");
    scl.getProperties().set ("fontSize", 13.0f);
    scl.setTooltip (U ("Importar unha escala de Scala (.scl): as súas notas e os seus cents, desde a tónica."));
    scl.onClick = [this] { importScala(); };
    canvas.addAndMakeVisible (scl);

    detect.setButtonText (T ("Detectar"));
    detect.setTooltip (U ("Canta uns segundos (ou pon a base na entrada lateral «Acompañamento»): Barullo Tune escoita e escolle a tónica e a escala. Volve pulsar para cancelar."));
    detect.onClick = [this] { if (proc.isKeyListening()) proc.stopKeyListening(); else proc.startKeyListening(); refresh(); };
    canvas.addAndMakeVisible (detect);

    const char* modes[3] = { "Escala", "MIDI escala", "MIDI nota" };
    const char* modeTips[3] = {
        "Leva a voz á nota máis próxima da escala.",
        "As notas que toques no teclado MIDI fan de escala, en calquera oitava. Sen notas tocadas, usa a escala.",
        "A voz vai á nota exacta que toques no teclado MIDI, na oitava máis próxima; a roda de pitch bend dóbraa. Sen notas tocadas, usa a escala." };
    setupSegment (modeBtn, 3, "modo", modes, modeTips);

    layout();
    showTab (juce::jlimit (0, (int) numTabs - 1, proc.uiTab.load()));
    juce::Desktop::getInstance().addGlobalMouseListener (this);
    const float sc = juce::jlimit (0.75f, 1.5f, proc.uiScale.load());   // antes de nada: resized() vaino sobrescribir
    setSize (juce::roundToInt (W * sc), juce::roundToInt (H * sc));
    setResizable (true, true);
    setResizeLimits (W * 3 / 4, H * 3 / 4, W * 3 / 2, H * 3 / 2);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) W / (double) H);
    refresh();
    startTimerHz (30);
}

TuneEditor::~TuneEditor()
{
    juce::Desktop::getInstance().removeGlobalMouseListener (this);
    stopTimer();
    setLookAndFeel (nullptr);
}

// cada clic (en calquera mando) empeza un paso novo de desfacer
void TuneEditor::mouseDown (const juce::MouseEvent&) { proc.undo.beginNewTransaction(); }

void TuneEditor::setupKnob (ui::Knob& k, const char* id, const char* name, const char* tip)
{
    k.name = U (name);
    k.slider.setTitle (k.name);
    k.slider.param = proc.params.getParameter (id);
    k.att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.params, id, k.slider);
    if (auto* prm = proc.params.getParameter (id)) k.slider.setDoubleClickReturnValue (true, prm->convertFrom0to1 (prm->getDefaultValue()));
    k.slider.setTooltip (U (tip));
    canvas.addAndMakeVisible (k);
}

void TuneEditor::setupBar (ui::ValueSlider& s, const char* id, const juce::String& tip, const juce::String& title)
{
    s.param = proc.params.getParameter (id);
    s.setTitle (title);
    s.setSliderStyle (juce::Slider::LinearBar);
    s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    s.setMouseDragSensitivity (180);
    sliderAtts.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.params, id, s));
    if (auto* prm = proc.params.getParameter (id)) s.setDoubleClickReturnValue (true, prm->convertFrom0to1 (prm->getDefaultValue()));
    s.setTooltip (tip);
    canvas.addAndMakeVisible (s);
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
        canvas.addAndMakeVisible (b[i]);
    }
}

bool TuneEditor::typeValue (const juce::String& paramId, const juce::String& text)
{
    for (auto* k : { &vel, &transi, &tol, &tolT, &flex, &human, &vib, &cant, &form, &trans, &air, &vgDepth, &vgRate, &vgDelay, &vgVar, &vgTrem, &principal, &la, &mest, &vol, &bend })
        if (k->slider.param != nullptr && k->slider.param->paramID == paramId) return k->slider.applyText (text);
    for (auto& row : hBar) for (auto& b : row) if (b.param != nullptr && b.param->paramID == paramId) return b.applyText (text);
    return false;
}

void TuneEditor::showTab (int t)
{
    tab = juce::jlimit (0, (int) numTabs - 1, t);
    proc.uiTab = tab;
    const bool c = tab == TabCorrection, v = tab == TabVoice, h = tab == TabHarmony, o = tab == TabOutput;
    for (auto* k : { &vel, &transi, &tol, &tolT, &flex, &human, &vib, &cant }) k->setVisible (c);
    link.setVisible (c);
    for (auto* k : { &form, &trans, &air, &vgDepth, &vgRate, &vgDelay, &vgVar, &vgTrem }) k->setVisible (v);
    principal.setVisible (h);
    for (auto& b : harmMode) b.setVisible (h);
    for (int i = 0; i < TuneProcessor::numHarmonies; ++i)
    {
        hOn[i].setVisible (h); hInt[i].setVisible (h);
        for (auto& b : hBar[i]) b.setVisible (h);
    }
    for (auto* k : { &la, &mest, &vol, &bend }) k->setVisible (o);
    midiOut.setVisible (o);
    language.setVisible (o);
    for (int i = 0; i < numTabs; ++i) tabBtn[i].setToggleState (i == tab, juce::dontSendNotification);
    canvas.repaint();
}

// o menú dos presets: os de fábrica, os teus e as accións
void TuneEditor::rebuildPresetMenu()
{
    preset.clear (juce::dontSendNotification);
    auto* menu = preset.getRootMenu();
    menu->addSectionHeader (U ("De fábrica"));
    for (int i = 0; i < proc.getNumPrograms(); ++i) preset.addItem (proc.getProgramName (i), i + 1);
    const auto user = proc.userPresets();
    if (! user.isEmpty())
    {
        menu->addSectionHeader (U ("Os teus"));
        for (int i = 0; i < user.size(); ++i) preset.addItem (user[i].getFileNameWithoutExtension(), 101 + i);
    }
    preset.addSeparator();
    preset.addItem (U ("Gardar como preset…"), 1000);
    preset.addItem (U ("Abrir a carpeta dos presets"), 1001);
    preset.addItem (proc.abCurrent() == 0 ? U ("Copiar A en B") : U ("Copiar B en A"), 1002);
    lastPresetList.clear();
    for (auto& f : user) lastPresetList << f.getFileName() << "|";
    preset.setSelectedId (proc.getCurrentProgram() + 1, juce::dontSendNotification);
}

void TuneEditor::presetChosen()
{
    const int id = preset.getSelectedId();
    if (id >= 1 && id <= proc.getNumPrograms()) { proc.undo.beginNewTransaction(); proc.setCurrentProgram (id - 1); refresh(); return; }
    if (id >= 101 && id < 1000)
    {
        const auto user = proc.userPresets();
        if (juce::isPositiveAndBelow (id - 101, user.size())) proc.loadUserPreset (user[id - 101]);
        const auto name = preset.getItemText (preset.indexOfItemId (id));
        preset.setText (name, juce::dontSendNotification);
        refresh();
        return;
    }
    preset.setSelectedId (proc.getCurrentProgram() + 1, juce::dontSendNotification);
    if (id == 1000)
    {
        auto* w = new juce::AlertWindow (U ("Gardar preset"), U ("Nome do preset (garda todos os axustes):"), juce::MessageBoxIconType::NoIcon);
        w->addTextEditor ("nome", "", "");
        w->addButton (T ("Gardar"), 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton (T ("Cancelar"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
        w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w] (int r)
        {
            if (r == 1 && proc.saveUserPreset (w->getTextEditorContents ("nome"))) rebuildPresetMenu();
        }), true);
    }
    else if (id == 1001)
    {
        const auto dir = proc.presetFolder();
        dir.createDirectory();
        dir.startAsProcess();
    }
    else if (id == 1002) proc.abCopy();
}

void TuneEditor::importScala()
{
    chooser = std::make_unique<juce::FileChooser> (U ("Escala de Scala"), juce::File(), "*.scl");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
    {
        const auto f = fc.getResult();
        if (! f.existsAsFile()) return;
        juce::String err;
        proc.undo.beginNewTransaction();
        if (! proc.loadScala (f.loadFileAsString(), err))
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Scala", err);
        refresh();
    });
}

void TuneEditor::refresh()
{
    auto seg = [this] (juce::TextButton* b, int count, const char* id)
    {
        const int v = (int) proc.params.getRawParameterValue (id)->load();
        for (int i = 0; i < count; ++i) b[i].setToggleState (i == v, juce::dontSendNotification);
    };
    seg (modeBtn, 3, "modo");
    seg (calBtn, 3, "calidade");
    seg (harmMode, 2, "armonia_modo");
    link.setToggleState (proc.params.getRawParameterValue ("ligar")->load() > 0.5f, juce::dontSendNotification);
    compare.setToggleState (proc.params.getRawParameterValue ("comparar")->load() > 0.5f, juce::dontSendNotification);
    for (int i = 0; i < 2; ++i) abBtn[i].setToggleState (i == proc.abCurrent(), juce::dontSendNotification);
    undoBtn.setEnabled (proc.undo.canUndo());
    redoBtn.setEnabled (proc.undo.canRedo());
    if (preset.getSelectedId() > 0 && preset.getSelectedId() <= proc.getNumPrograms() && preset.getSelectedId() != proc.getCurrentProgram() + 1)
        preset.setSelectedId (proc.getCurrentProgram() + 1, juce::dontSendNotification);
    // armonías apagadas: a súa fila máis apagada
    for (int h = 0; h < TuneProcessor::numHarmonies; ++h)
    {
        const bool on = proc.params.getRawParameterValue ("harm" + juce::String (h + 1))->load() > 0.5f;
        hInt[h].setAlpha (on ? 1.0f : 0.55f);
        for (auto& b : hBar[h]) b.setAlpha (on ? 1.0f : 0.55f);
    }

    if (proc.isKeyListening())
    {
        detect.setButtonText (U ("Escoitando… ") + juce::String (juce::roundToInt (proc.keyProgress() * 100.0f)) + " %");
        detect.getProperties().set ("progress", juce::jmax (0.02f, proc.keyProgress()));
    }
    else
    {
        detect.setButtonText (T ("Detectar"));
        detect.getProperties().set ("progress", 0.0f);
    }
    detect.repaint();
}

void TuneEditor::timerCallback()
{
    proc.finishKeyIfReady();
    display.update();
    keyboard.repaint();
    if (practicePanel.isVisible()) practicePanel.update();
    refresh();
    // a lista dos presets propios cambia se alguén garda ou borra ficheiros
    static int countdown = 0;
    if (++countdown >= 60)
    {
        countdown = 0;
        juce::String now; for (auto& f : proc.userPresets()) now << f.getFileName() << "|";
        if (now != lastPresetList && ! preset.isPopupActive()) rebuildPresetMenu();
    }
    canvas.repaint (790, 60, 222, 18);   // latencia
}

void TuneEditor::paint (juce::Graphics& g) { g.fillAll (ui::col::noite); }

void TuneEditor::paintCanvas (juce::Graphics& g)
{
    using namespace ui;
    g.fillAll (col::noite);

    g.setFont (cond (34.0f));
    g.setColour (col::escuma);
    g.drawText ("Barullo Tune", juce::Rectangle<float> (28.0f, 20.0f, 200.0f, 42.0f), juce::Justification::centredLeft, false);

    auto small = [&] (const juce::String& t, juce::Rectangle<int> r, juce::Justification j = juce::Justification::centredLeft)
    {
        g.setFont (body (13.5f)); g.setColour (col::neboa); g.drawText (t, r, j, false);
    };
    const float lat = proc.latencyMs();
    small (U ("latencia ") + (lat < 1.0f ? U ("menos de 1 ms") : num (lat, 0) + " ms"), { 790, 60, 222, 18 }, juce::Justification::centred);

    // grupos de mandos, coa súa etiqueta e unha liña a cada lado
    using namespace lay;
    auto group = [&] (int x, int w, const juce::String& t)
    {
        const auto f = semi (14.0f);
        g.setFont (f); g.setColour (col::neboa);
        g.drawText (t, juce::Rectangle<int> (x, rowY - 2, w, 18), juce::Justification::centred, false);
        const int tw = (int) juce::GlyphArrangement::getStringWidth (f, t);
        g.setColour (col::lina);
        g.fillRect (x + 8, rowY + 8, (w - tw) / 2 - 16, 1);
        g.fillRect (x + w / 2 + tw / 2 + 8, rowY + 8, (w - tw) / 2 - 16, 1);
    };
    if (tab == TabCorrection)
    {
        int x = corrX;
        group (x, knobW * 2 + linkW, T ("Afinar")); x += knobW * 2 + linkW + gap;
        group (x, knobW * 2, T ("Tolerancia")); x += knobW * 2 + gap;
        group (x, knobW * 2, T ("Liberdade")); x += knobW * 2 + gap;
        group (x, knobW * 2, T ("Voz"));
    }
    else if (tab == TabVoice)
    {
        group (voiceX, knobW * 3, T ("Timbre"));
        group (voiceX + knobW * 3 + gap, knobW * 5, T ("Vibrato xerado"));
    }
    else if (tab == TabHarmony)
    {
        g.setFont (semi (13.0f)); g.setColour (col::neboa);
        g.drawText (U ("voz"), juce::Rectangle<int> (hX, rowY - 2, 34, 16), juce::Justification::centred, false);
        g.drawText (U ("intervalo"), juce::Rectangle<int> (hX + 40, rowY - 2, 120, 16), juce::Justification::centred, false);
        const char* heads[5] = { "nivel", "panorama", "desafinar", "atraso", "formantes" };
        for (int b = 0; b < 5; ++b)
            g.drawText (T (heads[b]), juce::Rectangle<int> (hBarX + b * hBarStep, rowY - 2, hBarW, 16), juce::Justification::centred, false);
    }
    else
    {
        group (outX, knobW * 3, U ("Saída"));
        group (outX + knobW * 3 + gap, knobW + 110, "MIDI");
        group (langX, 150, T ("Idioma"));
    }

    // lenda do teclado, no pé
    auto swatch = [&] (int x, juce::Colour c, const juce::String& t)
    {
        g.setColour (c); g.fillRoundedRectangle ((float) x, (float) kbY + kbH + 10.0f, 14.0f, 10.0f, 2.0f);
        small (t, { x + 19, kbY + kbH + 7, 60, 16 });
    };
    swatch (28, col::escuma, T ("escala"));
    swatch (92, col::sodio, T ("libre"));
    swatch (148, col::lina, U ("fóra"));
    small (U ("clic dereito nun mando: escribir o valor · maiúsculas: fino, ou os cents da tecla"), { 210, kbY + kbH + 7, 490, 16 });

    g.setFont (body (12.0f)); g.setColour (col::neboa.withAlpha (0.5f));
    g.drawText (U ("Escoita   42°14′N 8°43′W"), juce::Rectangle<int> (712, kbY + kbH + 7, 300, 16), juce::Justification::centredRight, false);
}

void TuneEditor::resized()
{
    const float sc = (float) getWidth() / (float) W;
    canvas.setTransform (juce::AffineTransform::scale (sc));
    proc.uiScale = sc;
}

void TuneEditor::layout()
{
    using namespace lay;
    canvas.setBounds (0, 0, W, H);
    // cabeceira: presets, A/B, comparar, desfacer e xeito
    preset.setBounds (236, 24, 210, 34);
    abBtn[0].setBounds (454, 24, 36, 34);
    abBtn[1].setBounds (489, 24, 37, 34);
    compare.setBounds (534, 24, 104, 34);
    undoBtn.setBounds (646, 24, 34, 34);
    redoBtn.setBounds (684, 24, 34, 34);
    for (int i = 0; i < 3; ++i) calBtn[i].setBounds (790 + i * 74 - (i > 0 ? 1 : 0), 24, 74 + (i > 0 ? 1 : 0), 34);

    display.setBounds (28, 84, 984, 240);
    practicePanel.setBounds (28, 84, 984, 240);
    practiceBtn.setBounds (28 + 984 - 128, 92, 78, 22);   // (á esquerda do medidor de saída)

    // pestanas (e, en Armonías, o seu modo á dereita)
    for (int i = 0; i < numTabs; ++i) tabBtn[i].setBounds (28 + i * 112 - (i > 0 ? 1 : 0), tabY, 112 + (i > 0 ? 1 : 0), tabH);
    harmMode[0].setBounds (792, tabY, 110, tabH);
    harmMode[1].setBounds (901, tabY, 111, tabH);

    // corrección
    {
        int x = corrX;
        vel.setBounds (x, knobY, knobW, knobH);
        link.setBounds (x + knobW + 3, knobY + 22, linkW - 6, 26);
        transi.setBounds (x + knobW + linkW, knobY, knobW, knobH);
        x += knobW * 2 + linkW + gap;
        tol.setBounds (x, knobY, knobW, knobH); tolT.setBounds (x + knobW, knobY, knobW, knobH);
        x += knobW * 2 + gap;
        flex.setBounds (x, knobY, knobW, knobH); human.setBounds (x + knobW, knobY, knobW, knobH);
        x += knobW * 2 + gap;
        vib.setBounds (x, knobY, knobW, knobH); cant.setBounds (x + knobW, knobY, knobW, knobH);
    }
    // voz
    {
        int x = voiceX;
        for (auto* k : { &form, &trans, &air }) { k->setBounds (x, knobY, knobW, knobH); x += knobW; }
        x += gap;
        for (auto* k : { &vgDepth, &vgRate, &vgDelay, &vgVar, &vgTrem }) { k->setBounds (x, knobY, knobW, knobH); x += knobW; }
    }
    // armonías
    principal.setBounds (28, knobY, knobW, knobH);
    for (int h = 0; h < TuneProcessor::numHarmonies; ++h)
    {
        const int y = hRowY + h * hRowStep;
        hOn[h].setBounds (hX, y, 34, hRowH);
        hInt[h].setBounds (hX + 40, y, 120, hRowH);
        for (int b = 0; b < 5; ++b) hBar[h][b].setBounds (hBarX + b * hBarStep, y, hBarW, hRowH);
    }
    // saída
    {
        int x = outX;
        for (auto* k : { &la, &mest, &vol }) { k->setBounds (x, knobY, knobW, knobH); x += knobW; }
        x += gap;
        bend.setBounds (x, knobY, knobW, knobH);
        midiOut.setBounds (x + knobW + 6, knobY + 26, 98, 30);
        language.setBounds (langX + 10, knobY + 26, 130, 30);
    }

    // modo, tonalidade, voz, afinación e o teclado
    for (int i = 0; i < 3; ++i) modeBtn[i].setBounds (28 + i * 104 - (i > 0 ? 1 : 0), rowB, 104 + (i > 0 ? 1 : 0), 30);
    detect.setBounds (352, rowB, 112, 30);
    tonica.setBounds (472, rowB, 78, 30);
    escala.setBounds (558, rowB, 160, 30);
    voz.setBounds (726, rowB, 112, 30);
    afin.setBounds (846, rowB, 118, 30);
    scl.setBounds (968, rowB, 44, 30);
    keyboard.setBounds (28, kbY, 984, kbH);
}
