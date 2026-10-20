#include "PluginEditor.h"
#include "BinaryData.h"

namespace
{
    juce::String U (const char* s) { return juce::String::fromUTF8 (s); }
}

namespace ui
{
// ================================================================ tipografía
static juce::Typeface::Ptr tf (int which)
{
    static juce::Typeface::Ptr t[3] = {
        juce::Typeface::createSystemTypefaceFor (BinaryData::BarlowSemiCondensedMedium_ttf, BinaryData::BarlowSemiCondensedMedium_ttfSize),
        juce::Typeface::createSystemTypefaceFor (BinaryData::BarlowSemiCondensedSemiBold_ttf, BinaryData::BarlowSemiCondensedSemiBold_ttfSize),
        juce::Typeface::createSystemTypefaceFor (BinaryData::BarlowCondensedSemiBold_ttf, BinaryData::BarlowCondensedSemiBold_ttfSize) };
    return t[which];
}
juce::Font body (float s) { return juce::Font (juce::FontOptions (tf (0)).withHeight (s)); }
juce::Font bold (float s) { return juce::Font (juce::FontOptions (tf (1)).withHeight (s)); }
juce::Font cond (float s) { return juce::Font (juce::FontOptions (tf (2)).withHeight (s)); }

static juce::String upper (const juce::String& s)
{
    return s.toUpperCase().replace (U ("á"), U ("Á")).replace (U ("é"), U ("É")).replace (U ("í"), U ("Í"))
            .replace (U ("ó"), U ("Ó")).replace (U ("ú"), U ("Ú")).replace (U ("ñ"), U ("Ñ"));
}

void printed (juce::Graphics& g, const juce::String& t, juce::Rectangle<float> r, float size, juce::Colour c, juce::Justification j, float tracking)
{
    g.setColour (c);
    g.setFont (juce::Font (juce::FontOptions (tf (1)).withHeight (size).withKerningFactor (tracking)));
    g.drawText (t, r, j, false);
}

static void ledWindow (juce::Graphics& g, juce::Rectangle<float> r)
{
    g.setColour (juce::Colour (0xff050506)); g.fillRoundedRectangle (r.expanded (2.0f), 5.0f);
    g.setColour (col::ledBg); g.fillRoundedRectangle (r, 4.0f);
}

// ================================================================ aspecto
Look::Look()
{
    setColour (juce::ResizableWindow::backgroundColourId, col::body);
    setColour (juce::TextButton::buttonColourId, col::rubber);
    setColour (juce::TextButton::textColourOffId, col::print);
    setColour (juce::TextButton::textColourOnId, col::print);
    setColour (juce::Label::textColourId, col::printDim);
    setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxTextColourId, col::print);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::TextEditor::backgroundColourId, col::bodyLo);
    setColour (juce::TextEditor::textColourId, col::print);
    setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TextEditor::focusedOutlineColourId, col::teal);
    setColour (juce::TextEditor::highlightColourId, col::teal.withAlpha (0.35f));
    setColour (juce::CaretComponent::caretColourId, col::print);
    setColour (juce::ComboBox::backgroundColourId, col::bodyLo);
    setColour (juce::ComboBox::textColourId, col::print);
    setColour (juce::ComboBox::outlineColourId, col::line);
    setColour (juce::ComboBox::arrowColourId, col::printDim);
    setColour (juce::PopupMenu::backgroundColourId, col::bodyHi);
    setColour (juce::PopupMenu::textColourId, col::print);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, col::teal.withAlpha (0.3f));
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
}

juce::Typeface::Ptr Look::getTypefaceForFont (const juce::Font& f) { return tf (f.isBold() ? 1 : 0); }

void Look::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.0f, 1.0f);
    const bool led = (bool) b.getProperties()["led"];
    const bool sunk = down || (b.getToggleState() && ! led);
    r.removeFromBottom (2.5f);
    g.setColour (col::bodyLo); g.fillRoundedRectangle (r.translated (0, 2.5f), 3.5f);
    auto face = sunk ? r.translated (0, 1.5f) : r;
    auto c = col::rubber;
    if (sunk) c = c.darker (0.25f); else if (over) c = c.brighter (0.08f);
    g.setColour (c); g.fillRoundedRectangle (face, 3.5f);
    g.setColour (juce::Colours::white.withAlpha (sunk ? 0.06f : 0.14f));
    g.fillRoundedRectangle (face.withHeight (2.0f).reduced (3.0f, 0).translated (0, 1.0f), 1.0f);
    if (led)
    {
        const bool on = b.getToggleState();
        auto l = juce::Rectangle<float> (face.getX() + 11.0f, face.getCentreY() - 3.5f, 7.0f, 7.0f);
        if (on) { g.setColour (col::green.withAlpha (0.3f)); g.fillEllipse (l.expanded (3.5f)); }
        g.setColour (on ? col::green : col::greenOff);
        g.fillEllipse (l);
    }
}

void Look::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool down)
{
    auto r = b.getLocalBounds().withTrimmedBottom (3);
    if (down || (b.getToggleState() && ! (bool) b.getProperties()["led"])) r.translate (0, 1);
    if ((bool) b.getProperties()["led"]) r.removeFromLeft (18);
    printed (g, upper (b.getButtonText()), r.reduced (6, 0).toFloat(), 13.0f, col::print, juce::Justification::centred, 0.06f);
}

void Look::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float, juce::Slider::SliderStyle, juce::Slider& s)
{
    const float cy = (float) y + (float) h * 0.5f;
    auto slot = juce::Rectangle<float> ((float) x, cy - 3.0f, (float) w, 6.0f);
    g.setColour (juce::Colour (0xff050506)); g.fillRoundedRectangle (slot, 3.0f);
    // parte activa (desde o centro se o rango é simétrico)
    const bool bip = s.getMinimum() < 0 && s.getMaximum() > 0;
    const float from = bip ? (float) x + (float) w * (float) ((0 - s.getMinimum()) / (s.getMaximum() - s.getMinimum())) : (float) x;
    g.setColour (col::teal.withAlpha (0.45f));
    g.fillRoundedRectangle (juce::Rectangle<float> (juce::jmin (from, pos), cy - 1.5f, std::abs (pos - from), 3.0f), 1.5f);
    auto cap = juce::Rectangle<float> (pos - 7.0f, cy - 10.0f, 14.0f, 20.0f);
    g.setColour (col::bodyLo); g.fillRoundedRectangle (cap.translated (0, 2.0f), 2.5f);
    g.setColour (juce::Colour (0xff3a3b40)); g.fillRoundedRectangle (cap, 2.5f);
    g.setColour (juce::Colours::white.withAlpha (0.10f)); g.fillRect (cap.withHeight (2.0f).reduced (2.0f, 0).translated (0, 1.0f));
    g.setColour (col::redBtn); g.fillRect (pos - 1.0f, cap.getY(), 2.0f, cap.getHeight());
}

void Look::drawLabel (juce::Graphics& g, juce::Label& l)
{
    if (! l.findColour (juce::Label::backgroundColourId).isTransparent()) g.fillAll (l.findColour (juce::Label::backgroundColourId));
    if (l.isBeingEdited()) return;
    g.setColour (l.findColour (juce::Label::textColourId).withMultipliedAlpha (l.isEnabled() ? 1.0f : 0.5f));
    g.setFont (getLabelFont (l));
    g.drawFittedText (l.getText(), l.getLocalBounds().reduced (2, 0), l.getJustificationType(), 1, 0.9f);
}

juce::Label* Look::createSliderTextBox (juce::Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (s);
    l->setFont (bold (14.5f));
    l->setColour (juce::Label::textColourId, col::print);
    l->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    l->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    l->setJustificationType (juce::Justification::centredRight);
    return l;
}

void Look::drawTooltip (juce::Graphics& g, const juce::String& text, int w, int h)
{
    auto r = juce::Rectangle<int> (w, h).toFloat();
    g.setColour (col::bodyHi); g.fillRoundedRectangle (r, 4.0f);
    g.setColour (col::line); g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);
    juce::AttributedString s; s.setJustification (juce::Justification::topLeft);
    s.append (text, body (14.0f), col::print);
    juce::TextLayout tl; tl.createLayoutWithBalancedLineLengths (s, 300.0f);
    tl.draw (g, r.reduced (10.0f, 7.0f));
}

juce::Rectangle<int> Look::getTooltipBounds (const juce::String& text, juce::Point<int> pos, juce::Rectangle<int> parent)
{
    juce::AttributedString s; s.append (text, body (14.0f));
    juce::TextLayout tl; tl.createLayoutWithBalancedLineLengths (s, 300.0f);
    const int w = (int) tl.getWidth() + 22, h = (int) tl.getHeight() + 16;
    return juce::Rectangle<int> (pos.x > parent.getCentreX() ? pos.x - (w + 12) : pos.x + 12,
                                 pos.y > parent.getCentreY() ? pos.y - (h + 8) : pos.y + 18, w, h).constrainedWithin (parent);
}

void Look::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    auto r = juce::Rectangle<int> (w, h).toFloat().reduced (0.5f);
    g.setColour (col::bodyLo); g.fillRoundedRectangle (r, 4.0f);
    g.setColour (box.hasKeyboardFocus (false) ? col::teal : col::line); g.drawRoundedRectangle (r, 4.0f, 1.0f);
    const float ax = (float) w - 18.0f, ay = (float) h * 0.5f;
    juce::Path p; p.addTriangle (ax - 5.0f, ay - 2.5f, ax + 5.0f, ay - 2.5f, ax, ay + 3.5f);
    g.setColour (col::printDim); g.fillPath (p);
}

void Look::positionComboBoxText (juce::ComboBox& box, juce::Label& l)
{
    l.setBounds (10, 1, box.getWidth() - 34, box.getHeight() - 2);
    l.setFont (getComboBoxFont (box));
}

// ================================================================ nota e medidor
void NoteView::update()
{
    const bool v = proc.nowVoiced.load();
    if (v)
    {
        voiced = true; holdFrames = 12;
        target = proc.nowTarget.load();
        hz = proc.nowHz.load();
        inDev = (proc.nowIn.load() - (float) target) * 100.0f;
        outDev = (proc.nowOut.load() - (float) target) * 100.0f;
    }
    else if (holdFrames > 0 && --holdFrames == 0) voiced = false;
    repaint();
}

void NoteView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    ledWindow (g, r);
    auto in = r.reduced (16.0f, 12.0f);

    auto top = in.removeFromTop (in.getHeight() * 0.58f);
    const juce::String name = (voiced && target >= 0) ? upper (scales::noteWithOctave (target)) : juce::String ("- -");
    auto f = cond (78.0f);
    g.setFont (f);
    if (voiced)
    {
        g.setColour (col::led.withAlpha (0.12f));
        for (int dx = -2; dx <= 2; dx += 2) for (int dy = -2; dy <= 2; dy += 2)
            g.drawText (name, top.translated ((float) dx, (float) dy), juce::Justification::centredLeft, false);
    }
    g.setColour (voiced ? col::led : col::ledOff.brighter (0.4f));
    g.drawText (name, top, juce::Justification::centredLeft, false);

    g.setFont (body (15.0f));
    g.setColour (voiced ? col::led.withAlpha (0.8f) : col::ledOff.brighter (0.5f));
    auto info = top.removeFromRight (100.0f);
    g.drawText (voiced ? juce::String (hz, 1) + " Hz" : juce::String ("sen voz"), info.removeFromTop (info.getHeight() * 0.5f), juce::Justification::bottomRight);
    if (voiced)
        g.drawText ((outDev - inDev >= 0 ? "+" : "") + juce::String (juce::roundToInt (outDev - inDev)) + " c",
                    info, juce::Justification::topRight);

    // medidor: −50 … +50 cents arredor da nota
    in.removeFromTop (6.0f);
    auto scale = in.removeFromTop (14.0f);
    auto bar = in.removeFromTop (16.0f);
    g.setFont (body (11.5f));
    g.setColour (col::ledOff.brighter (0.7f));
    for (int c = -50; c <= 50; c += 25)
    {
        const float x = bar.getX() + bar.getWidth() * (float) (c + 50) / 100.0f;
        g.fillRect (x - 0.5f, bar.getY(), 1.0f, c == 0 ? bar.getHeight() : 5.0f);
        g.drawText (c == 0 ? juce::String ("0") : (c > 0 ? "+" : "") + juce::String (c),
                    juce::Rectangle<float> (x - 20.0f, scale.getY(), 40.0f, scale.getHeight()), juce::Justification::centred);
    }
    g.setColour (col::ledOff.brighter (0.25f));
    g.fillRect (bar.withHeight (2.0f).withY (bar.getCentreY() - 1.0f));
    if (voiced)
    {
        auto mark = [&] (float c, juce::Colour colr, float w)
        {
            const float x = bar.getX() + bar.getWidth() * (juce::jlimit (-50.0f, 50.0f, c) + 50.0f) / 100.0f;
            g.setColour (colr.withAlpha (0.3f)); g.fillRoundedRectangle (x - w, bar.getY() - 1.0f, w * 2.0f, bar.getHeight() + 2.0f, 2.0f);
            g.setColour (colr); g.fillRoundedRectangle (x - w * 0.5f, bar.getY(), w, bar.getHeight(), 1.5f);
        };
        mark (inDev, col::amber, 4.0f);
        mark (outDev, col::led, 5.0f);
    }
    g.setFont (body (12.0f));
    g.setColour (col::amber.withAlpha (0.8f));
    auto leg = in.removeFromTop (18.0f).translated (0, 4.0f);
    g.drawText (U ("● entrada"), leg.removeFromLeft (80.0f), juce::Justification::centredLeft);
    g.setColour (col::led.withAlpha (0.9f));
    g.drawText (U ("● saída"), leg.removeFromLeft (80.0f), juce::Justification::centredLeft);
}

// ================================================================ gráfico do ton
void PitchGraph::drain()
{
    int s1, n1, s2, n2;
    const int ready = proc.fifo.getNumReady();
    proc.fifo.prepareToRead (ready, s1, n1, s2, n2);
    auto take = [this] (const TuneProcessor::Frame& f)
    {
        hist[(size_t) head] = f;
        head = (head + 1) % histSize;
        if (f.voiced) centre += (f.outMidi - centre) * 0.02f;
    };
    for (int i = 0; i < n1; ++i) take (proc.frames[(size_t) (s1 + i)]);
    for (int i = 0; i < n2; ++i) take (proc.frames[(size_t) (s2 + i)]);
    proc.fifo.finishedRead (n1 + n2);
    if (n1 + n2 > 0) repaint();
}

void PitchGraph::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    ledWindow (g, r);
    auto in = r.reduced (10.0f, 10.0f);
    in.removeFromLeft (44.0f);
    const float span = 7.5f;
    const float lo = centre - span, hi = centre + span;
    auto Y = [&] (float m) { return in.getBottom() - (m - lo) / (hi - lo) * in.getHeight(); };

    const int mode = (int) proc.params.getRawParameterValue ("modo")->load();
    uint16_t mask = proc.effectiveMask();
    const int held = proc.nowHeld.load();
    if (mode > 0 && held != 0) mask = (uint16_t) held;

    g.setFont (body (11.5f));
    float lastLabelY = 1e9f;
    for (int m = (int) std::ceil (lo); m <= (int) std::floor (hi); ++m)
    {
        const bool ok = (mask >> (m % 12)) & 1;
        const float y = Y ((float) m);
        g.setColour (ok ? col::ledOff.brighter (0.55f) : col::ledOff.brighter (0.12f));
        g.fillRect (in.getX(), y - 0.5f, in.getWidth(), 1.0f);
        if (ok && lastLabelY - y >= 14.0f)
        {
            lastLabelY = y;
            g.setColour (col::led.withAlpha (0.55f));
            g.drawText (scales::noteWithOctave (m), juce::Rectangle<float> (r.getX() + 6.0f, y - 8.0f, 44.0f, 16.0f), juce::Justification::centredLeft);
        }
    }

    auto pathOf = [&] (bool out)
    {
        juce::Path p; bool pen = false;
        for (int i = 0; i < histSize; ++i)
        {
            const auto& f = hist[(size_t) ((head + i) % histSize)];
            const float x = in.getX() + in.getWidth() * (float) i / (float) (histSize - 1);
            if (! f.voiced) { pen = false; continue; }
            const float y = juce::jlimit (in.getY(), in.getBottom(), Y (out ? f.outMidi : f.inMidi));
            if (! pen) { p.startNewSubPath (x, y); pen = true; } else p.lineTo (x, y);
        }
        return p;
    };
    g.saveState();
    g.reduceClipRegion (in.toNearestInt());
    const auto pin = pathOf (false), pout = pathOf (true);
    g.setColour (col::amber.withAlpha (0.65f));
    g.strokePath (pin, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (col::led.withAlpha (0.25f));
    g.strokePath (pout, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (col::led);
    g.strokePath (pout, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.restoreState();
}

// ================================================================ teclas da escala
static const int whitePcs[7] = { 0, 2, 4, 5, 7, 9, 11 };

juce::Rectangle<float> ScaleKeys::keyRect (int pc) const
{
    auto r = getLocalBounds().toFloat();
    const float ww = r.getWidth() / 7.0f;
    for (int i = 0; i < 7; ++i)
        if (whitePcs[i] == pc) return { r.getX() + ww * (float) i, r.getY(), ww, r.getHeight() };
    // negras: á dereita da branca anterior
    const int prevWhite = pc - 1;
    int idx = 0; for (int i = 0; i < 7; ++i) if (whitePcs[i] == prevWhite) idx = i;
    const float bw = ww * 0.62f;
    return { r.getX() + ww * (float) (idx + 1) - bw * 0.5f, r.getY(), bw, r.getHeight() * 0.6f };
}

static bool isBlack (int pc) { return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10; }

void ScaleKeys::update()
{
    const uint16_t m = proc.effectiveMask();
    const int t = proc.nowVoiced.load() ? proc.nowTarget.load() : -1;
    const int h = proc.nowHeld.load();
    const int md = (int) proc.params.getRawParameterValue ("modo")->load();
    if (m != mask || t != target || h != held || md != mode) { mask = m; target = t; held = (uint16_t) h; mode = md; repaint(); }
}

void ScaleKeys::paint (juce::Graphics& g)
{
    const int tpc = target >= 0 ? target % 12 : -1;
    const bool midiRules = mode > 0 && held != 0;
    auto drawKey = [&] (int pc)
    {
        auto k = keyRect (pc).reduced (1.0f, 0);
        const bool black = isBlack (pc);
        const bool on = (mask >> pc) & 1;
        juce::Colour c = black ? col::blackKey : col::ivory;
        if (! on) c = black ? c.brighter (0.12f) : c.darker (0.55f);
        if (midiRules) c = c.withMultipliedBrightness (0.8f);
        g.setColour (col::bodyLo); g.fillRoundedRectangle (k.translated (0, 2.0f), 3.0f);
        g.setColour (c); g.fillRoundedRectangle (k, 3.0f);
        if (pc == tpc) { g.setColour (col::led.withAlpha (black ? 0.85f : 0.55f)); g.fillRoundedRectangle (k, 3.0f); }
        const float d = 9.0f;
        auto dot = juce::Rectangle<float> (k.getCentreX() - d * 0.5f, k.getBottom() - d - (black ? 8.0f : 26.0f), d, d);
        if (on && ! midiRules) { g.setColour (col::teal); g.fillEllipse (dot); }
        if ((held >> pc) & 1) { g.setColour (col::amber); g.fillEllipse (dot.expanded (midiRules ? 0.0f : 2.0f)); }
        if (! black)
        {
            g.setFont (bold (12.5f));
            g.setColour (on ? juce::Colour (0xff2a2a2e) : juce::Colour (0xff1a1a1c));
            g.drawText (scales::noteName (pc), juce::Rectangle<float> (k.getX(), k.getBottom() - 20.0f, k.getWidth(), 18.0f), juce::Justification::centred);
        }
    };
    for (int pc : whitePcs) drawKey (pc);
    for (int pc = 0; pc < 12; ++pc) if (isBlack (pc)) drawKey (pc);
}

void ScaleKeys::mouseDown (const juce::MouseEvent& e)
{
    const auto p = e.position;
    for (int pc = 0; pc < 12; ++pc) if (isBlack (pc) && keyRect (pc).contains (p)) { proc.toggleScaleNote (pc); return; }
    for (int pc : whitePcs) if (keyRect (pc).contains (p)) { proc.toggleScaleNote (pc); return; }
}
} // namespace ui

// ================================================================ editor
TuneEditor::TuneEditor (TuneProcessor& p) : AudioProcessorEditor (p), proc (p)
{
    setLookAndFeel (&look);
    addAndMakeVisible (noteView);
    addAndMakeVisible (graph);
    addAndMakeVisible (keys);
    keys.setTooltip (U ("Notas permitidas. Pulsa unha tecla para quitala ou poñela (a escala pasa a «Personalizada»). "
                        "A tecla vermella é a nota á que vai a voz agora; os puntos ámbar, as notas que tocas polo MIDI."));
    noteView.setInterceptsMouseClicks (false, false);
    graph.setInterceptsMouseClicks (false, false);

    for (int i = 0; i < 12; ++i) tonica.addItem (scales::noteName (i), i + 1);
    for (int i = 0; i < (int) scales::all().size(); ++i) escala.addItem (U (scales::all()[(size_t) i].name), i + 1);
    tonicaAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.params, "tonica", tonica);
    escalaAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.params, "escala", escala);
    tonica.setTooltip (U ("Tónica da escala."));
    escala.setTooltip (U ("Escala. Cromática corrixe a calquera semitón; «Personalizada» son as teclas que acendas a man."));
    addAndMakeVisible (tonica);
    addAndMakeVisible (escala);

    const char* names[3] = { "Escala", "MIDI: escala", "MIDI: nota" };
    const char* tips[3] = {
        "Corrixe á nota máis próxima da escala escollida.",
        "As notas que toques no teclado MIDI fan de escala (en calquera oitava). Sen notas tocadas, usa a escala escollida.",
        "A voz vai á nota exacta que toques no teclado MIDI, na oitava máis próxima á voz. Sen notas tocadas, usa a escala." };
    for (int i = 0; i < 3; ++i)
    {
        auto& b = modeBtn[i];
        b.setButtonText (U (names[i]));
        b.setTooltip (U (tips[i]));
        b.getProperties().set ("led", true);
        b.setClickingTogglesState (false);
        b.onClick = [this, i]
        {
            if (auto* prm = proc.params.getParameter ("modo"))
            { prm->beginChangeGesture(); prm->setValueNotifyingHost (prm->convertTo0to1 ((float) i)); prm->endChangeGesture(); }
            refreshModes();
        };
        addAndMakeVisible (b);
    }

    setupKnob (vel, "velocidade", "Velocidade",
               "Canto tarda en levar a voz á nota. Ao mínimo, efecto robot: salta de nota en seco. "
               "20–80 ms: corrección clara. 150–400 ms: natural, respecta o vibrato.");
    setupKnob (cant, "cantidade", "Cantidade", "Canto se corrixe: 100 % leva a voz xusto á nota; 50 % deixa a metade da desafinación.");
    setupKnob (hum, "humanizar", "Humanizar",
               "Nas notas longas a corrección vaise relaxando para que non soe mecánica. Os ataques seguen afinándose rápido.");
    setupKnob (trans, "transpor", "Transpor", "Sobe ou baixa a voz en semitonos sen cambiar os formantes (non soa a esquío).");
    setupKnob (la, "la", "La", "Afinación de referencia do La4.");
    setupKnob (mest, "mestura", "Mestura", "Canto de voz corrixida. Por baixo de 100 % mestúrase coa orixinal, aliñada no tempo.");
    setupKnob (vol, "volume", "Volume", "Volume de saída.");

    setSize (940, 632);
    refreshModes();
    startTimerHz (30);
}

TuneEditor::~TuneEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void TuneEditor::setupKnob (ui::Knob& k, const char* id, const char* name, const char* tip)
{
    k.label.setText (ui::upper (U (name)), juce::dontSendNotification);
    k.label.setFont (ui::bold (13.0f));
    k.label.setColour (juce::Label::textColourId, ui::col::teal);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 78, 22);
    k.slider.setTooltip (U (tip));
    k.label.setTooltip (k.slider.getTooltip());
    k.att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.params, id, k.slider);
    addAndMakeVisible (k.label);
    addAndMakeVisible (k.slider);
}

void TuneEditor::refreshModes()
{
    const int m = (int) proc.params.getRawParameterValue ("modo")->load();
    for (int i = 0; i < 3; ++i) modeBtn[i].setToggleState (i == m, juce::dontSendNotification);
}

void TuneEditor::timerCallback()
{
    noteView.update();
    graph.drain();
    keys.update();
    refreshModes();
}

void TuneEditor::paint (juce::Graphics& g)
{
    using namespace ui;
    g.fillAll (col::body);
    auto r = getLocalBounds().toFloat();
    g.setColour (col::line); g.drawRoundedRectangle (r.reduced (10.0f), 6.0f, 1.0f);

    g.setColour (col::print); g.setFont (bold (32.0f));
    g.drawText ("BARULLO TUNE", juce::Rectangle<float> (34.0f, 22.0f, 300.0f, 40.0f), juce::Justification::centredLeft);
    printed (g, U ("CORRECTOR DE AFINACIÓN · VOZ · ESCOITA"), { 260.0f, 30.0f, 420.0f, 26.0f }, 13.0f, col::gold, juce::Justification::centredLeft);

    auto label = [&] (juce::Rectangle<int> sec, const char* t)
    {
        printed (g, U (t), sec.toFloat().withHeight (16.0f).translated (0, -21.0f), 12.5f, col::teal, juce::Justification::centredLeft);
    };
    label (secNote, "NOTA");
    label (secGraph, "TON  ·  ENTRADA E SAÍDA");
    label (secScale, "ESCALA");
    label (secMode, "MODO");
    label (secCtl, "CORRECCIÓN");
    label (secCtl.withX (480), "AFINACIÓN E SAÍDA");

    g.setColour (col::line);
    g.fillRect (34, secCtl.getY() - 34, getWidth() - 68, 1);
}

void TuneEditor::resized()
{
    const int L = 34, R = getWidth() - 34;
    secNote = { L, 98, 300, 186 };
    secGraph = { L + 316, 98, R - (L + 316), 186 };
    noteView.setBounds (secNote);
    graph.setBounds (secGraph);

    secScale = { L, 330, 400, 112 };
    keys.setBounds (secScale);
    tonica.setBounds (L + 420, 330, 200, 34);
    escala.setBounds (L + 420, 374, 200, 34);
    secMode = { L + 640, 330, R - (L + 640), 112 };
    for (int i = 0; i < 3; ++i) modeBtn[i].setBounds (secMode.getX(), secMode.getY() + i * 38, secMode.getWidth(), 34);

    secCtl = { L, 498, 420, 110 };
    auto row = [] (ui::Knob& k, int x, int y, int w)
    {
        k.label.setBounds (x, y, 110, 26);
        k.slider.setBounds (x + 110, y, w - 110, 26);
    };
    row (vel, L, 498, 420);   row (cant, L, 526, 420);  row (hum, L, 554, 420);
    row (trans, 480, 498, R - 480); row (la, 480, 526, R - 480); row (mest, 480, 554, R - 480); row (vol, 480, 582, R - 480);
}
