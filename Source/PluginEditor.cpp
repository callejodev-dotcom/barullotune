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
// Montserrat (títulos e rótulos, en maiúsculas espaciadas coma o Lemon Milk da web de
// Escoita) e Inter (o texto corrido, coma na web). As dúas, licenza OFL.
static juce::Typeface::Ptr tf (int which)
{
    static juce::Typeface::Ptr t[3] = {
        juce::Typeface::createSystemTypefaceFor (BinaryData::InterMedium_ttf, BinaryData::InterMedium_ttfSize),
        juce::Typeface::createSystemTypefaceFor (BinaryData::MontserratSemiBold_ttf, BinaryData::MontserratSemiBold_ttfSize),
        juce::Typeface::createSystemTypefaceFor (BinaryData::MontserratBold_ttf, BinaryData::MontserratBold_ttfSize) };
    return t[which];
}
juce::Font body (float s) { return juce::Font (juce::FontOptions (tf (0)).withHeight (s)); }
juce::Font semi (float s) { return juce::Font (juce::FontOptions (tf (1)).withHeight (s)); }
juce::Font cond (float s) { return juce::Font (juce::FontOptions (tf (2)).withHeight (s)); }
// maiúsculas tamén para as letras con acento (o towupper do sistema non sempre as sabe)
juce::String upper (const juce::String& t)
{
    juce::String out;
    out.preallocateBytes (t.getNumBytesAsUTF8());
    for (auto p = t.getCharPointer(); ! p.isEmpty();)
    {
        auto c = p.getAndAdvance();
        if (c >= 'a' && c <= 'z') c -= 32;
        else if (c >= 0xe0 && c <= 0xfe && c != 0xf7) c -= 32;   // à…þ (á, é, í, ó, ú, ñ, ü)
        out += c;
    }
    return out;
}
juce::Font caps (float s, float tracking) { return juce::Font (juce::FontOptions (tf (1)).withHeight (s).withKerningFactor (tracking)); }

void drawCaps (juce::Graphics& g, const juce::String& t, juce::Rectangle<float> r, juce::Justification j, float size, float tracking)
{
    g.setFont (caps (size, tracking));
    g.drawText (upper (t), r, j, false);
}

// texto dun visor: dourado, cun pouco de brillo
static void glowText (juce::Graphics& g, const juce::String& t, juce::Rectangle<float> r, juce::Justification j, juce::Colour c)
{
    g.setColour (c.withAlpha (0.16f));
    for (const auto d : { juce::Point<float> (-0.8f, 0.0f), { 0.8f, 0.0f }, { 0.0f, -0.8f }, { 0.0f, 0.8f } })
        g.drawText (t, r.translated (d.x, d.y), j, false);
    g.setColour (c);
    g.drawText (t, r, j, false);
}

// ================================================================ pezas do aparello
// panel en relevo, coa súa sombra e o brillo do canto de arriba
void paintPanel (juce::Graphics& g, juce::Rectangle<float> r, float rad)
{
    for (int i = 3; i >= 1; --i)
    {
        g.setColour (juce::Colours::black.withAlpha (0.11f));
        g.fillRoundedRectangle (r.translated (0.0f, 2.0f * (float) i).expanded ((float) i), rad + (float) i);
    }
    g.setGradientFill (juce::ColourGradient (col::panel1, 0.0f, r.getY(), col::panel2, 0.0f, r.getBottom(), false));
    g.fillRoundedRectangle (r, rad);
    g.setColour (juce::Colours::white.withAlpha (0.07f));
    g.fillRect (r.getX() + rad, r.getY() + 0.5f, r.getWidth() - 2.0f * rad, 1.0f);
}

// pozo afundido (grupos de teclas, visores)
void paintWell (juce::Graphics& g, juce::Rectangle<float> r, float rad)
{
    g.setColour (juce::Colours::white.withAlpha (0.05f));
    g.fillRoundedRectangle (r.translated (0.0f, 1.0f), rad);
    g.setColour (col::visor.brighter (0.06f));
    g.fillRoundedRectangle (r, rad);
    juce::Path p; p.addRoundedRectangle (r, rad);
    g.saveState();
    g.reduceClipRegion (p);
    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.65f), 0.0f, r.getY(), juce::Colours::transparentBlack, 0.0f, r.getY() + 5.0f, false));
    g.fillRect (r.withHeight (6.0f));
    g.restoreState();
}

// pantalla de cristal: fondo azul fondo e sombra por dentro
void paintGlass (juce::Graphics& g, juce::Rectangle<float> r, float rad)
{
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.fillRoundedRectangle (r.translated (0.0f, 1.0f), rad);
    juce::ColourGradient gr (col::cristal1, r.getCentreX(), r.getY(), col::cristal2, r.getCentreX(), r.getBottom(), false);
    gr.addColour (0.55, juce::Colour (0xff0a1129));
    g.setGradientFill (gr);
    g.fillRoundedRectangle (r, rad);
    juce::Path p; p.addRoundedRectangle (r, rad);
    g.saveState();
    g.reduceClipRegion (p);
    for (int i = 0; i < 5; ++i)
    {
        g.setColour (juce::Colours::black.withAlpha (0.16f - 0.03f * (float) i));
        g.drawRoundedRectangle (r.reduced ((float) i + 0.5f), rad, 1.0f);
    }
    g.restoreState();
}

// o reflexo do cristal, por riba do que se debuxa nel
void paintGlassShine (juce::Graphics& g, juce::Rectangle<float> r, float rad)
{
    juce::Path p; p.addRoundedRectangle (r, rad);
    g.saveState();
    g.reduceClipRegion (p);
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.06f), r.getX(), r.getY(),
                                             juce::Colours::transparentWhite, r.getX() + r.getWidth() * 0.32f, r.getY() + r.getHeight() * 0.55f, false));
    g.fillRect (r);
    g.restoreState();
}

// unha tecla en relevo (todos os botóns)
static void paintKey (juce::Graphics& g, juce::Rectangle<float> r, bool over, bool down)
{
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillRoundedRectangle (r.translated (0.0f, 2.0f), 7.0f);
    if (down) r.translate (0.0f, 1.0f);
    const float lift = over && ! down ? 0.10f : 0.0f;
    g.setGradientFill (juce::ColourGradient (col::tecla1.brighter (lift), 0.0f, r.getY(), col::tecla2.brighter (lift * 0.6f), 0.0f, r.getBottom(), false));
    g.fillRoundedRectangle (r, 7.0f);
    g.setColour (juce::Colours::white.withAlpha (down ? 0.04f : 0.10f));
    g.fillRect (r.getX() + 6.0f, r.getY() + 0.5f, r.getWidth() - 12.0f, 1.0f);
}

// piloto LED: laranxa aceso, apagado escuro
static void paintLed (juce::Graphics& g, juce::Point<float> c, bool on)
{
    if (on)
    {
        g.setColour (col::laranxa.withAlpha (0.16f)); g.fillEllipse (c.x - 7.0f, c.y - 7.0f, 14.0f, 14.0f);
        g.setColour (col::laranxa.withAlpha (0.34f)); g.fillEllipse (c.x - 4.6f, c.y - 4.6f, 9.2f, 9.2f);
        g.setColour (col::laranxa); g.fillEllipse (c.x - 3.0f, c.y - 3.0f, 6.0f, 6.0f);
        g.setColour (juce::Colours::white.withAlpha (0.55f)); g.fillEllipse (c.x - 1.3f, c.y - 2.0f, 2.2f, 2.0f);
    }
    else
    {
        g.setColour (juce::Colour (0xff3a3326)); g.fillEllipse (c.x - 3.0f, c.y - 3.0f, 6.0f, 6.0f);
        g.setColour (juce::Colours::black.withAlpha (0.6f)); g.drawEllipse (c.x - 3.0f, c.y - 3.0f, 6.0f, 6.0f, 0.8f);
    }
}

// ================================================================ aspecto
Look::Look()
{
    setColour (juce::ResizableWindow::backgroundColourId, col::noite);
    setColour (juce::Label::textColourId, col::crema);
    setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TextEditor::backgroundColourId, col::visor);
    setColour (juce::TextEditor::textColourId, col::ouro);
    setColour (juce::TextEditor::outlineColourId, col::lina);
    setColour (juce::TextEditor::focusedOutlineColourId, col::laranxa);
    setColour (juce::TextEditor::highlightColourId, col::laranxa.withAlpha (0.35f));
    setColour (juce::CaretComponent::caretColourId, col::ouro);
    setColour (juce::ComboBox::backgroundColourId, col::visor);
    setColour (juce::ComboBox::textColourId, col::ouro);
    setColour (juce::ComboBox::outlineColourId, col::lina);
    setColour (juce::ComboBox::arrowColourId, col::ouro);
    setColour (juce::PopupMenu::backgroundColourId, col::panel2);
    setColour (juce::PopupMenu::textColourId, col::crema);
    setColour (juce::PopupMenu::headerTextColourId, col::laranxa);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, col::laranxa);
    setColour (juce::PopupMenu::highlightedTextColourId, col::noite);
    setColour (juce::TextButton::buttonColourId, col::tecla2);
    setColour (juce::TextButton::textColourOffId, col::crema2);
    setColour (juce::TextButton::textColourOnId, col::crema);
    setColour (juce::AlertWindow::backgroundColourId, col::panel2);
    setColour (juce::AlertWindow::textColourId, col::crema);
    setColour (juce::AlertWindow::outlineColourId, col::lina);
}

juce::Typeface::Ptr Look::getTypefaceForFont (const juce::Font& f) { return tf (f.isBold() ? 1 : 0); }

// as teclas: en relevo, cun LED (aceso se está escollida); o texto en maiúsculas espaciadas
void Look::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.0f).withTrimmedBottom (1.5f);
    paintKey (g, r, over, down);
    if (down) r.translate (0.0f, 1.0f);
    const float progress = (float) b.getProperties().getWithDefault ("progress", 0.0f);
    if (progress > 0.0f)
    {
        g.saveState();
        g.reduceClipRegion (r.withWidth (r.getWidth() * progress).toNearestInt());
        g.setColour (col::laranxa.withAlpha (0.25f));
        g.fillRoundedRectangle (r, 7.0f);
        g.restoreState();
    }
    if (! b.getProperties().contains ("noLed"))
        paintLed (g, { r.getX() + 13.0f, r.getCentreY() }, b.getToggleState());
    if (b.hasKeyboardFocus (false))
    {
        g.setColour (col::laranxa);
        g.drawRoundedRectangle (r.expanded (1.0f), 8.0f, 1.4f);
    }
}

void Look::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.0f).withTrimmedBottom (1.5f);
    if (down) r.translate (0.0f, 1.0f);
    if (! b.getProperties().contains ("noLed")) r.removeFromLeft (17.0f);
    juce::Colour c = b.getToggleState() || over ? col::crema : col::crema2;
    if (! b.isEnabled()) c = col::crema3.withAlpha (0.6f);
    g.setColour (c);
    g.setFont (caps ((float) b.getProperties().getWithDefault ("fontSize", 9.5f), 0.17f));
    g.drawFittedText (upper (b.getButtonText()), r.toNearestInt().reduced (5, 0), juce::Justification::centred, 1, 0.7f);
}

// os mandos: un disco mecanizado cun anel de LED laranxa e, debaixo, o valor nun visor
void Look::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s)
{
    auto area = juce::Rectangle<int> (x, y, w, h).toFloat();
    auto lcd = area.removeFromBottom (20.0f);
    lcd = lcd.withSizeKeepingCentre (juce::jmin (78.0f, area.getWidth() - 6.0f), 20.0f);
    area.removeFromBottom (5.0f);
    const float d = juce::jmin (area.getWidth(), area.getHeight());
    const auto c = area.withSizeKeepingCentre (d, d).getCentre();
    const float ring = d * 0.5f - 4.0f;

    // o anel: a pista e o valor (dende o cero, ou dende o centro se vai de − a +)
    juce::Path track; track.addCentredArc (c.x, c.y, ring, ring, 0.0f, a0, a1, true);
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.strokePath (track, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    const bool bip = s.getMinimum() < 0 && s.getMaximum() > 0;
    const double zeroAt = bip ? 0.0 : (s.getProperties().contains ("centre") ? (double) s.getProperties()["centre"] : s.getMinimum());
    const float zero = a0 + (float) s.valueToProportionOfLength (zeroAt) * (a1 - a0);
    const float ang = a0 + pos * (a1 - a0);
    if (std::abs (ang - zero) > 0.01f)
    {
        juce::Path val; val.addCentredArc (c.x, c.y, ring, ring, 0.0f, juce::jmin (zero, ang), juce::jmax (zero, ang), true);
        const auto lit = s.isEnabled() ? col::laranxa : col::crema3;
        g.setColour (lit.withAlpha (0.12f)); g.strokePath (val, juce::PathStrokeType (9.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (lit.withAlpha (0.26f)); g.strokePath (val, juce::PathStrokeType (5.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (lit); g.strokePath (val, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    if (s.hasKeyboardFocus (false))
    {
        g.setColour (col::laranxa.withAlpha (0.6f));
        g.drawEllipse (c.x - ring - 5.0f, c.y - ring - 5.0f, (ring + 5.0f) * 2.0f, (ring + 5.0f) * 2.0f, 1.2f);
    }

    // o disco
    const float kr = ring - 9.0f;
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillEllipse (c.x - kr, c.y - kr + 2.5f, kr * 2.0f, kr * 2.0f);
    juce::ColourGradient disc (juce::Colour (0xff4a5577), c.x - kr * 0.3f, c.y - kr * 0.45f, juce::Colour (0xff0e1323), c.x + kr * 0.55f, c.y + kr * 1.05f, true);
    disc.addColour (0.55, juce::Colour (0xff232b45));
    g.setGradientFill (disc);
    g.fillEllipse (c.x - kr, c.y - kr, kr * 2.0f, kr * 2.0f);
    g.setColour (juce::Colours::white.withAlpha (0.10f));
    g.drawEllipse (c.x - kr, c.y - kr, kr * 2.0f, kr * 2.0f, 1.0f);
    const float ir = kr * 0.74f;
    g.setColour (juce::Colours::white.withAlpha (0.05f));
    g.drawEllipse (c.x - ir, c.y - ir, ir * 2.0f, ir * 2.0f, 1.0f);
    const juce::Point<float> tip (c.x + kr * 0.82f * std::sin (ang), c.y - kr * 0.82f * std::cos (ang));
    g.setColour ((s.isEnabled() ? col::crema : col::crema3).withAlpha (0.92f));
    g.drawLine ({ c, tip }, 2.4f);

    // o visor co valor
    paintWell (g, lcd, 5.0f);
    g.setFont (caps (10.0f, 0.08f));
    glowText (g, upper (s.getTextFromValue (s.getValue()).replaceCharacter ('.', ',')), lcd, juce::Justification::centred,
              s.isEnabled() ? col::ouro : col::crema3);
}

// barras (as armonías): un visor co valor énchese desde o cero (ou desde o centro)
void Look::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float, juce::Slider::SliderStyle, juce::Slider& s)
{
    auto r = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (0.5f);
    paintWell (g, r, 5.0f);
    const bool bip = s.getMinimum() < 0 && s.getMaximum() > 0 && ! s.getProperties().contains ("fromMin");
    const float zero = bip ? r.getX() + (float) s.valueToProportionOfLength (0.0) * r.getWidth() : r.getX();
    const float a = juce::jmin (zero, pos), b = juce::jmax (zero, pos);
    if (b - a > 0.5f)
    {
        auto fill = juce::Rectangle<float> (a, r.getY() + 2.0f, b - a, r.getHeight() - 4.0f);
        const auto lit = s.isEnabled() ? col::laranxa : col::crema3;
        g.setGradientFill (juce::ColourGradient (lit.withAlpha (0.55f), 0.0f, fill.getY(), lit.withAlpha (0.3f), 0.0f, fill.getBottom(), false));
        g.fillRoundedRectangle (fill, 3.0f);
    }
    if (s.hasKeyboardFocus (false)) { g.setColour (col::laranxa); g.drawRoundedRectangle (r, 5.0f, 1.4f); }
    g.setFont (caps (10.0f, 0.06f));
    glowText (g, s.getTextFromValue (s.getValue()).replaceCharacter ('.', ',').replaceCharacter ('-', (juce::juce_wchar) 0x2212), r, juce::Justification::centred,
              s.isEnabled() ? col::crema : col::crema3);
}

void Look::drawLabel (juce::Graphics& g, juce::Label& l)
{
    if (l.isBeingEdited()) return;
    if (dynamic_cast<juce::ComboBox*> (l.getParentComponent()) != nullptr)
    {
        // o texto dun selector: dourado e en maiúsculas, coma un visor (estreitado se non colle)
        const auto t = upper (l.getText());
        const auto r = l.getLocalBounds().toFloat().reduced (2.0f, 0.0f);
        auto f = getLabelFont (l);
        const float tw = juce::GlyphArrangement::getStringWidth (f, t);
        if (tw > r.getWidth()) f = f.withHorizontalScale (juce::jmax (0.7f, r.getWidth() / tw));
        g.setFont (f);
        glowText (g, t, r, juce::Justification::centredLeft,
                  l.findColour (juce::Label::textColourId).withMultipliedAlpha (l.isEnabled() ? 1.0f : 0.5f));
        return;
    }
    g.setColour (l.findColour (juce::Label::textColourId).withMultipliedAlpha (l.isEnabled() ? 1.0f : 0.5f));
    g.setFont (getLabelFont (l));
    g.drawFittedText (l.getText(), l.getLocalBounds().reduced (2, 0), l.getJustificationType(), 1, 0.9f);
}

void Look::drawTooltip (juce::Graphics& g, const juce::String& text, int w, int h)
{
    auto r = juce::Rectangle<int> (w, h).toFloat();
    g.setColour (col::panel2); g.fillRoundedRectangle (r, 6.0f);
    g.setColour (col::laranxa.withAlpha (0.35f)); g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
    juce::AttributedString s; s.setJustification (juce::Justification::topLeft);
    s.append (text, body (13.5f), col::crema);
    juce::TextLayout tl; tl.createLayoutWithBalancedLineLengths (s, 290.0f);
    tl.draw (g, r.reduced (12.0f, 9.0f));
}

juce::Rectangle<int> Look::getTooltipBounds (const juce::String& text, juce::Point<int> pos, juce::Rectangle<int> parent)
{
    juce::AttributedString s; s.append (text, body (13.5f));
    juce::TextLayout tl; tl.createLayoutWithBalancedLineLengths (s, 290.0f);
    const int w = (int) tl.getWidth() + 26, h = (int) tl.getHeight() + 20;
    return juce::Rectangle<int> (pos.x > parent.getCentreX() ? pos.x - (w + 12) : pos.x + 12,
                                 pos.y > parent.getCentreY() ? pos.y - (h + 8) : pos.y + 18, w, h).constrainedWithin (parent);
}

// os selectores: un visor afundido, co texto dourado
void Look::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    auto r = juce::Rectangle<int> (w, h).toFloat().reduced (0.5f);
    paintWell (g, r, 8.0f);
    if (box.isMouseOver (true)) { g.setColour (col::ouro.withAlpha (0.05f)); g.fillRoundedRectangle (r, 8.0f); }
    if (box.hasKeyboardFocus (true)) { g.setColour (col::laranxa); g.drawRoundedRectangle (r, 8.0f, 1.4f); }
    const float ax = (float) w - 15.0f, ay = (float) h * 0.5f;
    juce::Path p; p.startNewSubPath (ax - 4.0f, ay - 1.8f); p.lineTo (ax, ay + 2.2f); p.lineTo (ax + 4.0f, ay - 1.8f);
    g.setColour (col::ouro.withAlpha (box.isEnabled() ? 0.9f : 0.4f));
    g.strokePath (p, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void Look::positionComboBoxText (juce::ComboBox& box, juce::Label& l)
{
    l.setBounds (12, 1, box.getWidth() - 32, box.getHeight() - 2);
    l.setFont (getComboBoxFont (box));
}

void Look::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    g.fillAll (col::panel2);
    g.setColour (col::laranxa.withAlpha (0.3f)); g.drawRect (0, 0, w, h);
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

// medidor de LED: 22 pilotos, verdes e os de arriba dourados (por riba de −6 dB)
void Display::paintMeter (juce::Graphics& g, juce::Rectangle<float> r, float level, const juce::String& label)
{
    paintGlass (g, r, 12.0f);
    auto in = r.reduced (0.0f, 14.0f);
    auto lab = in.removeFromBottom (14.0f);
    g.setColour (col::crema2);
    drawCaps (g, label, lab, juce::Justification::centred, 8.0f, 0.2f);
    in.removeFromBottom (8.0f);
    const int n = 22;
    const float bw = 10.0f, gap = 2.0f, sh = (in.getHeight() - gap * (float) (n - 1)) / (float) n;
    const int lit = juce::roundToInt (level * (float) n);
    for (int i = 0; i < n; ++i)
    {
        const auto seg = juce::Rectangle<float> (in.getCentreX() - bw * 0.5f, in.getBottom() - (float) (i + 1) * sh - (float) i * gap, bw, sh);
        if (i < lit)
        {
            const auto c = i >= n - 4 ? col::ouro : col::verde;
            g.setColour (c.withAlpha (0.25f)); g.fillRoundedRectangle (seg.expanded (1.5f), 2.0f);
            g.setColour (c); g.fillRoundedRectangle (seg, 1.0f);
        }
        else
        {
            g.setColour (col::crema.withAlpha (0.07f)); g.fillRoundedRectangle (seg, 1.0f);
        }
    }
    paintGlassShine (g, r, 12.0f);
}

// o afinador: un medidor de agulla (laranxa a túa voz, crema a afinada) e a nota debaixo
void Display::paintArc (juce::Graphics& g, juce::Rectangle<float> area)
{
    const float R = juce::jmin (area.getWidth() * 0.5f - 30.0f, area.getHeight() * 0.38f);
    const juce::Point<float> c (area.getCentreX(), area.getY() + 32.0f + R);
    const float span = 1.0f;    // radiáns a cada lado (±57°)
    auto angOf = [span] (float cents) { return juce::jlimit (-50.0f, 50.0f, cents) / 50.0f * span; };
    auto at = [&] (float ang, float rad) { return juce::Point<float> (c.x + rad * std::sin (ang), c.y - rad * std::cos (ang)); };

    // a zona afinada (±5 c)
    {
        juce::Path z; z.addCentredArc (c.x, c.y, R - 3.0f, R - 3.0f, 0.0f, angOf (-5.0f), angOf (5.0f), true);
        g.setColour (col::verde.withAlpha (0.8f));
        g.strokePath (z, juce::PathStrokeType (4.0f));
    }
    for (int cts = -50; cts <= 50; cts += 5)
    {
        const bool major = cts % 25 == 0;
        const float a = angOf ((float) cts);
        g.setColour (col::crema.withAlpha (major ? 0.7f : 0.3f));
        g.drawLine ({ at (a, R - (major ? 15.0f : 8.0f)), at (a, R) }, major ? 2.0f : 1.0f);
        if (major)
        {
            const auto p = at (a, R + 14.0f);
            g.setColour (col::crema2);
            drawCaps (g, cts == 0 ? juce::String ("0") : (cts > 0 ? "+" : U ("−")) + juce::String (std::abs (cts)),
                      juce::Rectangle<float> (p.x - 20.0f, p.y - 7.0f, 40.0f, 14.0f), juce::Justification::centred, 9.0f, 0.06f);
        }
    }
    // as agullas (sen voz, a afinada descansa no cero, apagada)
    auto needle = [&] (float ang, float len, juce::Colour ink, float width)
    {
        g.setColour (ink.withAlpha (0.18f)); g.drawLine ({ c, at (ang, len) }, width + 5.0f);
        g.setColour (ink.withAlpha (0.35f)); g.drawLine ({ c, at (ang, len) }, width + 2.0f);
        g.setColour (ink); g.drawLine ({ c, at (ang, len) }, width);
    };
    if (voiced)
    {
        needle (angOf (inDev), R - 20.0f, col::laranxa, 2.2f);
        needle (angOf (outDev), R - 8.0f, col::crema, 2.0f);
    }
    else
    {
        g.setColour (col::crema.withAlpha (0.25f));
        g.drawLine ({ c, at (0.0f, R - 8.0f) }, 2.0f);
    }
    {
        juce::ColourGradient hub (juce::Colour (0xff3a4566), c.x - 2.0f, c.y - 3.0f, juce::Colour (0xff0b1020), c.x + 4.0f, c.y + 6.0f, true);
        g.setGradientFill (hub); g.fillEllipse (c.x - 8.0f, c.y - 8.0f, 16.0f, 16.0f);
    }

    // a nota, debaixo do eixo
    auto noteBox = juce::Rectangle<float> (area.getX(), c.y + 8.0f, area.getWidth(), area.getBottom() - c.y - 40.0f);
    if (voiced && target >= 0)
    {
        const auto name = upper (scales::noteName (target));
        const auto oct = juce::String (target / 12 - 2);
        const auto big = cond (50.0f).withExtraKerningFactor (0.04f), small = semi (20.0f);
        const float wn = juce::GlyphArrangement::getStringWidth (big, name), wo = juce::GlyphArrangement::getStringWidth (small, oct);
        const float x0 = c.x - (wn + wo + 4.0f) * 0.5f;
        g.setFont (big);
        g.setColour (col::crema.withAlpha (0.12f));
        g.drawText (name, juce::Rectangle<float> (x0, noteBox.getY(), wn + 6.0f, noteBox.getHeight()).expanded (1.0f), juce::Justification::centredLeft, false);
        g.setColour (col::crema);
        g.drawText (name, juce::Rectangle<float> (x0, noteBox.getY(), wn + 6.0f, noteBox.getHeight()), juce::Justification::centredLeft, false);
        g.setFont (small); g.setColour (col::laranxa);
        g.drawText (oct, juce::Rectangle<float> (x0 + wn + 4.0f, noteBox.getY() - 14.0f, wo + 6.0f, noteBox.getHeight()), juce::Justification::centredLeft, false);
    }
    else
    {
        g.setColour (col::crema3);
        drawCaps (g, U ("Canta algo"), noteBox, juce::Justification::centred, 16.0f, 0.2f);
    }
    auto info = juce::Rectangle<float> (area.getX() + 16.0f, area.getBottom() - 30.0f, area.getWidth() - 32.0f, 16.0f);
    if (voiced)
    {
        const int cc = juce::roundToInt (outDev - inDev);
        g.setColour (col::crema2);
        drawCaps (g, num (hz, 1) + " Hz", info, juce::Justification::centredLeft, 9.0f, 0.2f);
        const auto corr = (cc > 0 ? "+" : "") + juce::String (cc).replace ("-", U ("−")) + " c";
        drawCaps (g, U ("Corrixe"), info.withTrimmedRight (juce::GlyphArrangement::getStringWidth (caps (9.0f, 0.2f), upper (corr)) + 8.0f),
                  juce::Justification::centredRight, 9.0f, 0.2f);
        g.setColour (col::ouro);
        drawCaps (g, corr, info, juce::Justification::centredRight, 9.0f, 0.2f);
    }
}

// o ton nos últimos segundos: carrís das notas da escala, a túa voz e a afinada, con brillo
void Display::paintTrace (juce::Graphics& g, juce::Rectangle<float> box)
{
    auto r = box.reduced (14.0f, 16.0f).withTrimmedTop (16.0f);
    const float span = 12.0f;
    const float laneH = r.getHeight() / span;
    auto Y = [&] (float m) { return r.getBottom() - (m - (shown - span * 0.5f)) * laneH; };
    const int mode = (int) proc.params.getRawParameterValue ("modo")->load();
    uint16_t mask = proc.effectiveMask() | proc.freeMask();
    const int held = proc.nowHeld.load();
    if (mode > 0 && held != 0) mask = (uint16_t) held;

    g.saveState();
    g.reduceClipRegion (box.reduced (2.0f).toNearestInt());
    float lastLabel = 1e9f;
    const float x0 = r.getX() + 42.0f;
    for (int k = (int) std::floor (shown - span * 0.5f) - 1; k <= (int) std::ceil (shown + span * 0.5f) + 1; ++k)
    {
        const bool ok = (mask >> (((k % 12) + 12) % 12)) & 1;
        const float y = Y ((float) k);
        if (ok)
        {
            // carril de punto en punto
            g.setColour (col::crema.withAlpha (0.10f));
            for (float x = x0; x < r.getRight(); x += 6.0f) g.fillRect (x, y - 0.5f, 2.0f, 1.0f);
        }
        if (ok && lastLabel - y >= 13.0f && y > r.getY() - 12.0f && y < r.getBottom() + 6.0f)
        {
            lastLabel = y;
            g.setColour (col::crema3);
            drawCaps (g, scales::noteWithOctave (k), juce::Rectangle<float> (r.getX(), y - 7.0f, 38.0f, 14.0f), juce::Justification::centredRight, 8.0f, 0.1f);
        }
    }
    auto pathOf = [&] (bool out)
    {
        juce::Path p; bool pen = false;
        for (int i = 0; i < histSize; ++i)
        {
            const auto& f = hist[(size_t) ((head + i) % histSize)];
            const float x = x0 + (r.getRight() - x0) * (float) i / (float) (histSize - 1);
            if (! f.voiced) { pen = false; continue; }
            const float y = Y (out ? f.outMidi : f.inMidi);
            if (! pen) { p.startNewSubPath (x, y); pen = true; } else p.lineTo (x, y);
        }
        return p;
    };
    auto glow = [&] (const juce::Path& p, juce::Colour c, float w)
    {
        g.setColour (c.withAlpha (0.14f)); g.strokePath (p, juce::PathStrokeType (w + 5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (c.withAlpha (0.30f)); g.strokePath (p, juce::PathStrokeType (w + 2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (c); g.strokePath (p, juce::PathStrokeType (w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    };
    glow (pathOf (false), col::laranxa, 1.5f);
    glow (pathOf (true), col::crema, 2.0f);
    g.restoreState();
    // a latencia, arriba á dereita (á esquerda do botón do informe)
    const float lat = proc.latencyMs();
    g.setColour (col::crema3);
    drawCaps (g, U ("Latencia ") + (lat < 10.0f ? num (lat, 1) : num (lat, 0)) + " ms",
              juce::Rectangle<float> (box.getX() + 18.0f, box.getY() + 19.0f, box.getWidth() - 18.0f - 130.0f, 14.0f), juce::Justification::centredRight, 8.0f, 0.22f);
}

void Display::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    auto mIn = r.removeFromLeft (42.0f); r.removeFromLeft (8.0f);
    auto mOut = r.removeFromRight (42.0f); r.removeFromRight (8.0f);
    auto tun = r.removeFromLeft (r.getHeight()); r.removeFromLeft (8.0f);   // o afinador, cadrado; o rastro, o resto
    paintMeter (g, mIn, inLv, "In");
    paintMeter (g, mOut, outLv, "Out");
    paintGlass (g, tun, 14.0f); paintArc (g, tun); paintGlassShine (g, tun, 14.0f);
    paintGlass (g, r, 14.0f); paintTrace (g, r); paintGlassShine (g, r, 14.0f);
}

// ================================================================ práctica
PracticePanel::PracticePanel (TuneProcessor& p) : proc (p)
{
    reset.setButtonText (U ("Empezar de novo"));
    reset.onClick = [this] { proc.resetPractice(); repaint(); };
    close.setButtonText (U ("Pechar"));
    close.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible (reset);
    addAndMakeVisible (close);
    setTitle (U ("Informe"));
}

void PracticePanel::resized()
{
    close.setBounds (getWidth() - 116, 12, 102, 30);
    reset.setBounds (getWidth() - 296, 12, 172, 30);
}

void PracticePanel::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    paintGlass (g, r, 14.0f);
    const auto pr = proc.practice();
    g.setColour (col::crema);
    drawCaps (g, U ("Como afinas"), juce::Rectangle<float> (22.0f, 14.0f, 300.0f, 24.0f), juce::Justification::centredLeft, 13.0f, 0.24f);
    if (pr.seconds < 1.0f)
    {
        g.setFont (body (15.0f)); g.setColour (col::crema2);
        g.drawText (U ("Canta un anaco e aquí verás como afinas (a túa voz, antes de corrixila)."), r.reduced (22.0f, 60.0f), juce::Justification::centredLeft, true);
        paintGlassShine (g, r, 14.0f);
        return;
    }
    // á esquerda, as cifras
    auto stat = [&] (float y, const juce::String& big, const juce::String& label)
    {
        g.setFont (cond (24.0f)); g.setColour (col::ouro);
        g.drawText (big, juce::Rectangle<float> (22.0f, y, 104.0f, 30.0f), juce::Justification::centredLeft, false);
        g.setFont (body (13.5f)); g.setColour (col::crema2);
        g.drawText (label, juce::Rectangle<float> (128.0f, y + 3.0f, 230.0f, 24.0f), juce::Justification::centredLeft, false);
    };
    const int secs = juce::roundToInt (pr.seconds);
    stat (52.0f, juce::String (juce::roundToInt (pr.within10 * 100.0f)) + " %", U ("do tempo, a menos de 10 cents"));
    stat (88.0f, juce::String (juce::roundToInt (pr.within25 * 100.0f)) + " %", U ("a menos de 25 cents"));
    stat (124.0f, juce::String (juce::roundToInt (pr.meanAbs)) + " c", U ("de desviación media"));
    const int tend = juce::roundToInt (pr.meanSigned);
    stat (160.0f, (tend > 0 ? "+" : tend < 0 ? juce::String::fromUTF8 ("−") : "") + juce::String (std::abs (tend)) + " c",
          std::abs (tend) < 4 ? U ("centrada") : tend > 0 ? U ("tendes a ir alta") : U ("tendes a ir baixa"));
    g.setColour (col::crema3);
    drawCaps (g, U ("Tempo cantado ") + juce::String (secs / 60) + ":" + juce::String (secs % 60).paddedLeft ('0', 2),
              juce::Rectangle<float> (22.0f, r.getBottom() - 30.0f, 300.0f, 18.0f), juce::Justification::centredLeft, 8.5f, 0.2f);
    // á dereita, unha barra por nota: canto vai alta (arriba) ou baixa (abaixo)
    auto area = juce::Rectangle<float> (390.0f, 54.0f, r.getWidth() - 412.0f, r.getHeight() - 88.0f);
    const float zero = area.getCentreY(), half = area.getHeight() * 0.5f - 14.0f, bw = area.getWidth() / 12.0f;
    g.setColour (col::crema.withAlpha (0.25f)); g.fillRect (area.getX(), zero - 0.5f, area.getWidth(), 1.0f);
    for (const int c : { -25, 25 })
    {
        const float y = zero - (float) c / 50.0f * half;
        g.setColour (col::crema.withAlpha (0.08f));
        for (float x = area.getX(); x < area.getRight(); x += 6.0f) g.fillRect (x, y - 0.5f, 2.0f, 1.0f);
        g.setColour (col::crema3);
        drawCaps (g, (c > 0 ? "+" : juce::String::fromUTF8 ("−")) + "25", juce::Rectangle<float> (area.getX() - 36.0f, y - 7.0f, 30.0f, 14.0f), juce::Justification::centredRight, 8.0f, 0.06f);
    }
    for (int i = 0; i < 12; ++i)
    {
        const float x = area.getX() + bw * (float) i;
        g.setColour (pr.noteSeconds[i] > 0.3f ? col::crema2 : col::crema3.withAlpha (0.5f));
        drawCaps (g, scales::noteName (i), juce::Rectangle<float> (x, area.getBottom() - 2.0f, bw, 16.0f), juce::Justification::centred, 8.0f, 0.06f);
        if (pr.noteSeconds[i] <= 0.3f) continue;   // pouco cantada: sen barra
        const float m = juce::jlimit (-50.0f, 50.0f, pr.noteMean[i]), h = m / 50.0f * half;
        const auto bar = juce::Rectangle<float> (x + bw * 0.28f, h > 0 ? zero - h : zero, bw * 0.44f, std::abs (h) + 1.0f);
        const auto c = std::abs (m) > 10.0f ? col::laranxa : col::verde;
        g.setColour (c.withAlpha (0.22f)); g.fillRoundedRectangle (bar.expanded (2.0f), 3.0f);
        g.setColour (c); g.fillRoundedRectangle (bar, 2.0f);
    }
    paintGlassShine (g, r, 14.0f);
}

// ================================================================ teclado
// cinco oitavas de teclas de verdade: a cor de cada tecla di o estado da nota (crema na
// escala, gris fóra, raias laranxa libre, laranxa a que cantas)
juce::Rectangle<float> Keyboard::keyRect (int note) const
{
    auto r = getLocalBounds().toFloat().withTrimmedTop (bedH);
    int whites = 0; for (int n = lo; n <= hi; ++n) if (! isBlack (n)) ++whites;
    const float ww = r.getWidth() / (float) whites;
    int wi = 0; for (int n = lo; n < note; ++n) if (! isBlack (n)) ++wi;
    if (! isBlack (note)) return { r.getX() + ww * (float) wi, r.getY(), ww, r.getHeight() };
    const float bw = ww * 0.58f;
    return { r.getX() + ww * (float) wi - bw * 0.5f, r.getY(), bw, r.getHeight() * 0.6f };
}

int Keyboard::noteAt (juce::Point<float> p) const
{
    for (int n = lo; n <= hi; ++n) if (isBlack (n) && keyRect (n).contains (p)) return n;
    for (int n = lo; n <= hi; ++n) if (! isBlack (n) && keyRect (n).contains (p)) return n;
    return -1;
}

bool Keyboard::customTuning() const { return (int) proc.params.getRawParameterValue ("afinacion")->load() == 2; }

static juce::Path keyShape (juce::Rectangle<float> r, float rad)
{
    juce::Path p;
    p.addRoundedRectangle (r.getX(), r.getY(), r.getWidth(), r.getHeight(), rad, rad, false, false, true, true);
    return p;
}

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

    // o feltro e a caixa de debaixo
    auto all = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff04060c)); g.fillRoundedRectangle (all, 6.0f);
    g.setColour (col::laranxa.withAlpha (0.35f)); g.fillRect (all.getX() + 4.0f, all.getY() + bedH - 1.5f, all.getWidth() - 8.0f, 1.0f);

    for (int pass = 0; pass < 2; ++pass)
        for (int n = lo; n <= hi; ++n)
        {
            const bool black = isBlack (n);
            if ((pass == 1) != black) continue;
            const int pc = ((n % 12) + 12) % 12;
            const bool inScale = (legal >> pc) & 1, free = (freeM >> pc) & 1;
            const bool singing = n == target, outOfRange = (float) n < mLo - 0.5f || (float) n > mHi + 0.5f;
            auto k = keyRect (n).reduced (black ? 0.0f : 1.0f, 0.0f);
            const auto shape = keyShape (k, black ? 4.0f : 6.0f);
            if (black)
            {
                g.setColour (juce::Colours::black.withAlpha (0.55f));
                g.fillPath (keyShape (k.translated (1.5f, 2.5f), 4.0f));
            }
            // a tecla
            juce::Colour top, bot;
            if (singing)       { top = juce::Colour (0xfff2a33a); bot = col::laranxa; }
            else if (black)
            {
                if (free)          { top = juce::Colour (0xff1a1104); bot = juce::Colour (0xff3a2508); }
                else if (inScale)  { top = juce::Colour (0xff050810); bot = juce::Colour (0xff1c2235); }
                else               { top = juce::Colour (0xff2b3144); bot = juce::Colour (0xff3a4156); }
            }
            else if (inScale || free) { top = juce::Colour (0xffe9e2b6); bot = juce::Colour (0xffe4dcae); }
            else                       { top = juce::Colour (0xff59586a); bot = juce::Colour (0xff6d6a72); }
            if (n == hover && ! singing) { top = black ? top.brighter (0.25f) : top.darker (0.06f); bot = black ? bot.brighter (0.25f) : bot.darker (0.06f); }
            juce::ColourGradient gr (top, 0.0f, k.getY(), bot, 0.0f, k.getBottom(), false);
            if (! black && ! singing) gr.addColour (0.7, inScale || free ? col::crema : juce::Colour (0xff6a6878));
            g.setGradientFill (gr);
            g.fillPath (shape);
            if (singing)
            {
                g.setColour (col::laranxa.withAlpha (0.35f));
                g.strokePath (shape, juce::PathStrokeType (3.0f));
            }
            // o canto de abaixo (a tecla ten fondo)
            g.saveState(); g.reduceClipRegion (shape);
            g.setColour (juce::Colours::black.withAlpha (black ? 0.0f : 0.14f));
            g.fillRect (k.withTop (k.getBottom() - 4.0f));
            if (black) { g.setColour (juce::Colours::white.withAlpha (0.08f)); g.fillRect (k.getX() + 2.0f, k.getBottom() - 5.0f, k.getWidth() - 4.0f, 1.5f); }
            // libre: raias laranxa
            if (free && ! singing)
            {
                g.setColour (col::laranxa.withAlpha (black ? 0.55f : 0.42f));
                for (float x = k.getX() - k.getHeight(); x < k.getRight(); x += 7.0f)
                    g.drawLine (x, k.getBottom(), x + k.getHeight(), k.getY(), 1.5f);
            }
            // fóra do rango da voz: máis escura
            if (outOfRange) { g.setColour (col::noite.withAlpha (0.5f)); g.fillRect (k); }
            g.restoreState();
            // notas tocadas no teclado MIDI: un punto
            if ((held >> pc) & 1)
            {
                const juce::Point<float> c (k.getCentreX(), k.getBottom() - (black ? 12.0f : 30.0f));
                g.setColour (col::laranxa.withAlpha (0.35f)); g.fillEllipse (c.x - 6.0f, c.y - 6.0f, 12.0f, 12.0f);
                g.setColour (col::laranxa); g.fillEllipse (c.x - 3.5f, c.y - 3.5f, 7.0f, 7.0f);
            }
            if (n == focusNote && hasKeyboardFocus (false))
            {
                g.setColour (col::laranxa);
                g.strokePath (keyShape (k.reduced (black ? 0.5f : 1.0f, 0.5f), black ? 4.0f : 6.0f), juce::PathStrokeType (2.0f));
            }
            // os cents de cada nota (afinacións que non son a igual)
            if (tuned && std::abs (cents[pc]) >= 0.5 && (inScale || free))
            {
                g.setColour (black ? col::crema2 : juce::Colour (0xff3a3a3a));
                drawCaps (g, (cents[pc] > 0 ? "+" : U ("−")) + juce::String (juce::roundToInt (std::abs (cents[pc]))),
                          k.withTrimmedBottom (black ? 5.0f : 22.0f).withTop (k.getBottom() - (black ? 18.0f : 36.0f)), juce::Justification::centred, 7.0f, 0.02f);
            }
            if (! black && pc == 0)
            {
                g.setColour (singing ? col::noite : (inScale ? juce::Colour (0xff2a2a2a) : col::crema.withAlpha (0.6f)));
                drawCaps (g, scales::noteWithOctave (n), k.withTrimmedBottom (6.0f), juce::Justification::centredBottom, 8.0f, 0.1f);
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
    editor->setFont (semi (14.0f));
    editor->setText (param->getCurrentValueAsText(), false);
    editor->selectAll();
    editor->setTitle (getTitle() + U (": escribir o valor"));
    auto* parent = getParentComponent();
    // (nun mando, enriba do seu visor)
    const auto at = isRotary() ? getBounds().withTop (getBottom() - 22) : getBounds();
    editor->setBounds (at.withSizeKeepingCentre (juce::jmax (78, getWidth()), 24));
    parent->addAndMakeVisible (*editor);
    editor->grabKeyboardFocus();
    // (o punteiro seguro créase fóra: MSVC le mal o «this» nunha captura dun lambda dentro doutro)
    const juce::Component::SafePointer<ValueSlider> safe (this);
    auto finish = [this, safe] (bool apply)
    {
        if (editor == nullptr) return;
        const auto text = editor->getText();
        juce::MessageManager::callAsync ([sp = safe, text, apply]
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

// o mando e o seu visor ocupan todo menos o rótulo, debaixo
void Knob::resized() { slider.setBounds (getLocalBounds().withTrimmedBottom (18)); }

void Knob::paint (juce::Graphics& g)
{
    g.setColour (slider.isEnabled() ? col::crema2 : col::crema3.withAlpha (0.6f));
    drawCaps (g, name, getLocalBounds().removeFromBottom (16).toFloat(), juce::Justification::centred, 8.5f, 0.2f);
}
} // namespace ui


// ================================================================ editor
namespace lay
{
    // cabeceira
    constexpr int topY = 14, topH = 52, btnY = 23, btnH = 34;
    // pantalla
    constexpr int dispY = 76, dispH = 244;
    // panel dos mandos
    constexpr int panY = 330, panH = 228;
    constexpr int tabY = 344, tabH = 26;
    constexpr int grpY = 388;              // rótulos dos grupos
    constexpr int knobY = 404, knobW = 100, knobH = 134;
    // selectores, teclado e pé
    constexpr int selY = 568, selH = 30;
    constexpr int kbY = 606, kbH = 110;
    constexpr int footY = 722;
    // armonías: unha fila por voz
    constexpr int hX = 152, hHeadY = 390, hRowY = 408, hRowH = 24, hRowStep = 28, hBarX = hX + 184, hBarW = 124, hBarStep = 134;
}

void TuneEditor::ArrowButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    using namespace ui;
    auto r = getLocalBounds().toFloat().reduced (1.0f).withTrimmedBottom (1.5f);
    paintKey (g, r, over && isEnabled(), down);
    if (down) r.translate (0.0f, 1.0f);
    if (hasKeyboardFocus (false)) { g.setColour (col::laranxa); g.drawRoundedRectangle (r.expanded (1.0f), 8.0f, 1.4f); }
    const auto c = r.getCentre();
    const float rad = 6.5f;
    juce::Path p;
    // arco de 3/4 de volta e unha punta de frecha
    const float a0 = isRedo ? -2.4f : 2.4f, a1 = isRedo ? 1.2f : -1.2f;
    p.addCentredArc (c.x, c.y + 1.0f, rad, rad, 0.0f, a0, a1, true);
    g.setColour (isEnabled() ? (over ? col::crema : col::crema2) : col::crema3.withAlpha (0.4f));
    g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    const juce::Point<float> tip (c.x + rad * std::sin (a1), c.y + 1.0f - rad * std::cos (a1));
    const float dir = isRedo ? 1.0f : -1.0f;
    juce::Path head;
    head.addTriangle (tip.x + dir * 4.0f, tip.y, tip.x - dir * 1.5f, tip.y - 4.0f, tip.x - dir * 1.5f, tip.y + 4.0f);
    g.fillPath (head);
}

void TuneEditor::TransportButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    using namespace ui;
    auto r = getLocalBounds().toFloat().reduced (1.0f).withTrimmedBottom (1.5f);
    paintKey (g, r, over && isEnabled(), down);
    if (down) r.translate (0.0f, 1.0f);
    if (hasKeyboardFocus (false)) { g.setColour (col::laranxa); g.drawRoundedRectangle (r.expanded (1.0f), 8.0f, 1.4f); }
    const bool on = getToggleState();
    const auto c = r.getCentre();
    const auto ink = on ? col::laranxa : (isEnabled() ? col::crema2 : col::crema3.withAlpha (0.4f));
    if (on) { g.setColour (col::laranxa.withAlpha (0.25f)); g.fillEllipse (c.x - 9.0f, c.y - 9.0f, 18.0f, 18.0f); }
    g.setColour (ink);
    if (isRecord) g.fillEllipse (c.x - 5.0f, c.y - 5.0f, 10.0f, 10.0f);
    else if (on) { g.fillRect (c.x - 4.5f, c.y - 5.0f, 3.2f, 10.0f); g.fillRect (c.x + 1.3f, c.y - 5.0f, 3.2f, 10.0f); }   // parar
    else { juce::Path t; t.addTriangle (c.x - 3.5f, c.y - 5.5f, c.x - 3.5f, c.y + 5.5f, c.x + 5.5f, c.y); g.fillPath (t); }
}

bool TuneEditor::standaloneUi() const
{
    return proc.wrapperType == juce::AudioProcessor::wrapperType_Standalone || std::getenv ("BT_STANDALONE") != nullptr;
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
    practiceBtn.setButtonText (U ("Informe"));
    practiceBtn.setTooltip (U ("Como afinas ti (a voz que entra, sen corrixir): canto tempo estás preto da nota e en que notas tendes a ir alta ou baixa."));
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
    display.setTooltip (U ("Á esquerda, a nota á que vai a voz: a agulla laranxa é a túa voz e a crema a afinada (en cents). "
                           "Á dereita, o ton nos últimos segundos."));

    // ---- cabeceira
    rebuildPresetMenu();
    preset.onChange = [this] { presetChosen(); };
    preset.setTooltip (U ("Presets: os de fábrica só cambian o carácter da corrección (non a tonalidade nin a voz); os teus gardan todo. Aquí tamén: gardar o actual como preset e copiar entre A e B."));
    canvas.addAndMakeVisible (preset);

    for (int i = 0; i < 2; ++i)
    {
        abBtn[i].setButtonText (i == 0 ? "A" : "B");
        abBtn[i].setTooltip (U ("A/B: dous xogos de axustes para comparar. Cambiar de lado garda o actual. Para copiar un lado no outro, no menú dos presets."));
        abBtn[i].onClick = [this, i] { if (i != proc.abCurrent()) proc.abSwitch(); refresh(); };
        canvas.addAndMakeVisible (abBtn[i]);
    }
    compare.setButtonText (U ("Comparar"));
    compare.setTooltip (U ("Mentres está aceso, escoitas a voz sen afinar (aliñada no tempo, sen armonías) para comparar."));
    compare.onClick = [this] { proc.setParamPlain ("comparar", proc.params.getRawParameterValue ("comparar")->load() > 0.5f ? 0.0f : 1.0f); refresh(); };
    canvas.addAndMakeVisible (compare);
    undoBtn.setTooltip (U ("Desfacer o último cambio."));
    redoBtn.setTooltip (U ("Refacer."));
    undoBtn.onClick = [this] { proc.undo.undo(); refresh(); };
    redoBtn.onClick = [this] { proc.undo.redo(); refresh(); };
    canvas.addAndMakeVisible (undoBtn);
    canvas.addAndMakeVisible (redoBtn);
    // gravar e escoitarse: só na aplicación independente (nun DAW grava o DAW)
    recBtn.setTitle (U ("Gravar")); playBtn.setTitle (U ("Escoitar"));
    recBtn.setTooltip (U ("Gravar a saída nun WAV (en Música/Barullo Tune). Volve pulsar para parar."));
    playBtn.setTooltip (U ("Escoitar a última toma en bucle. Volve pulsar para parar."));
    recBtn.onClick = [this]
    {
        if (proc.isRecording()) proc.stopRecording();
        else
        {
            proc.stopPlayback();
            proc.startRecording (TuneProcessor::takesFolder().getChildFile ("toma-" + juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S") + ".wav"));
        }
        refresh();
    };
    playBtn.onClick = [this]
    {
        if (proc.isPlaying()) proc.stopPlayback();
        else if (proc.lastTake().existsAsFile()) { proc.stopRecording(); proc.startPlayback (proc.lastTake()); }
        refresh();
    };
    if (standaloneUi()) { canvas.addAndMakeVisible (recBtn); canvas.addAndMakeVisible (playBtn); }

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
        tabBtn[i].onClick = [this, i] { showTab (i); };
        canvas.addAndMakeVisible (tabBtn[i]);
    }

    // ---- corrección
    setupKnob (vel, "velocidade", "Velocidade",
               "Canto tarda en levar a voz á nota dentro de cada nota. 0 ms: ao momento. Para o efecto robot: velocidade e transición a 0 e vibrato a 0 %.");
    setupKnob (transi, "transicion", "Transición",
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
    link.setButtonText (U ("ligar"));
    link.getProperties().set ("fontSize", 8.5f);
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
    setupKnob (vgVar, "vibgen_variacion", "Variación", "Vibrato xerado: a velocidade e o fondo cambian amodo, coma nunha voz de verdade.");
    setupKnob (vgTrem, "vibgen_tremolo", "Trémolo", "Vibrato xerado: o nivel tamén oscila, ao mesmo compás.");
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
        for (int b = 0; b < 5; ++b) setupBar (hBar[h][b], (id + bars[b]).toRawUTF8(), U (barTips[b]), U ("Armonía ") + juce::String (h + 1) + ": " + U (barNames[b]));
        hOn[h].setTitle (U ("Armonía ") + juce::String (h + 1));
        hInt[h].setTitle (U ("Armonía ") + juce::String (h + 1) + ": " + U ("intervalo"));
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

    // ---- modo, tonalidade, voz e afinación
    for (int i = 0; i < 12; ++i) tonica.addItem (scales::noteName (i), i + 1);
    for (int i = 0; i < (int) scales::all().size(); ++i) escala.addItem (U (scales::all()[(size_t) i].name), i + 1);
    const char* voces[] = { "Baixo", "Barítono", "Tenor", "Alto", "Mezzo", "Soprano", "Calquera" };
    for (int i = 0; i < 7; ++i) voz.addItem (U (voces[i]), i + 1);
    afin.addItem (U ("Igual"), 1); afin.addItem (U ("Xusta"), 2); afin.addItem (U ("Personalizada"), 3); afin.addItem ("MTS-ESP", 4);
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
    scl.getProperties().set ("noLed", true);
    scl.setTooltip (U ("Importar unha escala de Scala (.scl): as súas notas e os seus cents, desde a tónica."));
    scl.onClick = [this] { importScala(); };
    canvas.addAndMakeVisible (scl);

    detect.setButtonText (U ("Detectar"));
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
    // primeiro os enlaces cos parámetros, antes ca os controis que enlazan (se non, ao
    // destruírse, un enlace quitaríase dun botón xa destruído: «Notas MIDI» vai declarado despois)
    buttonAtts.clear(); comboAtts.clear(); sliderAtts.clear();
    tonicaAtt.reset(); escalaAtt.reset(); vozAtt.reset(); afinAtt.reset();
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
        w->addButton (U ("Gardar"), 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton (U ("Cancelar"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
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
        if (! proc.loadScala (TuneProcessor::readTextFile (f), err))
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
    recBtn.setToggleState (proc.isRecording(), juce::dontSendNotification);
    playBtn.setToggleState (proc.isPlaying(), juce::dontSendNotification);
    playBtn.setEnabled (proc.isPlaying() || proc.lastTake().existsAsFile());
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
        detect.setToggleState (true, juce::dontSendNotification);
    }
    else
    {
        detect.setButtonText (U ("Detectar"));
        detect.getProperties().set ("progress", 0.0f);
        detect.setToggleState (false, juce::dontSendNotification);
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
}

void TuneEditor::paint (juce::Graphics& g) { g.fillAll (ui::col::noite); }

void TuneEditor::paintCanvas (juce::Graphics& g)
{
    using namespace ui;
    using namespace lay;
    // o fondo: degradado e un cepillado vertical moi fino (coma o metal dun aparello)
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff161e33), 0.0f, 0.0f, juce::Colour (0xff0b101e), 0.0f, (float) H, false));
    g.fillAll();
    {
        static const juce::Image brush = []
        {
            juce::Image im (juce::Image::ARGB, 3, 1, true);
            im.setPixelAt (0, 0, juce::Colours::white.withAlpha (0.018f));
            return im;
        }();
        g.setFillType (juce::FillType (brush, juce::AffineTransform()));
        g.fillRect (0, 0, W, H);
    }

    // cabeceira: o panel, a placa co nome e os pozos dos grupos de teclas
    paintPanel (g, juce::Rectangle<float> (16.0f, (float) topY, (float) W - 32.0f, (float) topH), 14.0f);
    {
        const auto plate = juce::Rectangle<float> (26.0f, (float) topY + 6.0f, 188.0f, (float) topH - 12.0f);
        paintWell (g, plate, 9.0f);
        g.setColour (col::crema);
        g.setFont (cond (14.5f).withExtraKerningFactor (0.07f));
        g.drawText ("BARULLO TUNE", plate.reduced (14.0f, 0.0f).withTrimmedBottom (14.0f).withTrimmedTop (4.0f), juce::Justification::centredLeft, false);
        auto sub = plate.reduced (14.0f, 0.0f).withTop (plate.getBottom() - 17.0f).withHeight (11.0f);
        g.setColour (col::crema2);
        g.setFont (caps (7.5f, 0.36f));
        const auto escoita = juce::String ("ESCOITA");
        const float we = juce::GlyphArrangement::getStringWidth (caps (7.5f, 0.36f), escoita);
        g.drawText (escoita, sub, juce::Justification::centredLeft, false);
        g.setColour (col::laranxa);
        g.drawText (".", sub.withTrimmedLeft (we - 1.5f), juce::Justification::centredLeft, false);
        g.setColour (col::crema2);
        g.drawText (U (" · FERRAMENTAS"), sub.withTrimmedLeft (we + 4.0f), juce::Justification::centredLeft, false);
    }
    auto well = [&] (juce::Rectangle<int> r) { paintWell (g, r.toFloat(), 11.0f); };
    well ({ abBtn[0].getX() - 4, abBtn[0].getY() - 4, abBtn[1].getRight() - abBtn[0].getX() + 8, abBtn[0].getHeight() + 8 });
    well ({ calBtn[0].getX() - 4, calBtn[0].getY() - 4, calBtn[2].getRight() - calBtn[0].getX() + 8, calBtn[0].getHeight() + 8 });

    // o panel dos mandos
    paintPanel (g, juce::Rectangle<float> (16.0f, (float) panY, (float) W - 32.0f, (float) panH), 16.0f);
    well ({ tabBtn[0].getX() - 4, tabBtn[0].getY() - 4, tabBtn[numTabs - 1].getRight() - tabBtn[0].getX() + 8, tabBtn[0].getHeight() + 8 });
    well ({ modeBtn[0].getX() - 4, modeBtn[0].getY() - 4, modeBtn[2].getRight() - modeBtn[0].getX() + 8, modeBtn[0].getHeight() + 8 });

    // os grupos de mandos: o rótulo laranxa e unha liña debaixo
    for (const auto& gr : groups[tab])
    {
        const auto r = gr.first.toFloat();
        g.setColour (col::laranxa);
        drawCaps (g, gr.second, r.withHeight (14.0f), juce::Justification::centred, 8.5f, 0.28f);
        g.setColour (col::laranxa.withAlpha (0.25f));
        g.fillRect (r.getX() + 10.0f, r.getY() + 17.0f, r.getWidth() - 20.0f, 1.0f);
    }
    if (tab == TabHarmony)
    {
        g.setColour (col::crema3);
        drawCaps (g, U ("voz"), juce::Rectangle<float> ((float) hX, (float) hHeadY, 36.0f, 12.0f), juce::Justification::centred, 8.0f, 0.2f);
        drawCaps (g, U ("intervalo"), juce::Rectangle<float> ((float) hX + 44.0f, (float) hHeadY, 132.0f, 12.0f), juce::Justification::centred, 8.0f, 0.2f);
        const char* heads[5] = { "nivel", "panorama", "desafinar", "atraso", "formantes" };
        for (int b = 0; b < 5; ++b)
            drawCaps (g, U (heads[b]), juce::Rectangle<float> ((float) (hBarX + b * hBarStep), (float) hHeadY, (float) hBarW, 12.0f), juce::Justification::centred, 8.0f, 0.2f);
        g.setColour (col::crema3);
        drawCaps (g, U ("As armonías van á"), juce::Rectangle<float> ((float) harmMode[0].getX() - 210.0f, (float) harmMode[0].getY(), 200.0f, (float) harmMode[0].getHeight()),
                  juce::Justification::centredRight, 8.0f, 0.2f);
    }

    // a lenda do teclado, á dereita dos selectores
    {
        float x = (float) scl.getRight() + 26.0f;
        const float y = (float) selY + 8.0f;
        auto swatch = [&] (juce::Colour c, bool stripes, const juce::String& t)
        {
            const auto sw = juce::Rectangle<float> (x, y, 12.0f, 14.0f);
            g.setColour (c); g.fillRoundedRectangle (sw, 2.0f);
            if (stripes)
            {
                g.saveState(); g.reduceClipRegion (sw.toNearestInt());
                g.setColour (col::laranxa.withAlpha (0.6f));
                for (float sx = sw.getX() - 14.0f; sx < sw.getRight(); sx += 5.0f) g.drawLine (sx, sw.getBottom(), sx + 14.0f, sw.getY(), 1.2f);
                g.restoreState();
            }
            g.setColour (col::crema2);
            const auto f = caps (8.0f, 0.2f);
            const float tw = juce::GlyphArrangement::getStringWidth (f, upper (t));
            drawCaps (g, t, juce::Rectangle<float> (x + 18.0f, y, tw + 4.0f, 14.0f), juce::Justification::centredLeft, 8.0f, 0.2f);
            x += 18.0f + tw + 22.0f;
        };
        swatch (juce::Colour (0xffe9e2b6), false, U ("escala"));
        swatch (juce::Colour (0xffe9e2b6), true, U ("libre"));
        swatch (juce::Colour (0xff5f5e6e), false, U ("fóra"));
        swatch (col::laranxa, false, U ("cantas"));
    }

    // o pé: Escoita, a axuda e as coordenadas
    {
        const auto r = juce::Rectangle<float> (22.0f, (float) footY, (float) W - 44.0f, 14.0f);
        g.setColour (col::crema3);
        const auto f = caps (8.0f, 0.3f);
        const float we = juce::GlyphArrangement::getStringWidth (f, "ESCOITA");
        drawCaps (g, "ESCOITA", r, juce::Justification::centredLeft, 8.0f, 0.3f);
        g.setColour (col::laranxa);
        drawCaps (g, ".", r.withTrimmedLeft (we - 2.0f), juce::Justification::centredLeft, 8.0f, 0.3f);
        g.setColour (col::crema3);
        drawCaps (g, U (" Laboratorio creativo"), r.withTrimmedLeft (we + 3.0f), juce::Justification::centredLeft, 8.0f, 0.3f);
        drawCaps (g, U ("Clic dereito nun mando: escribir o valor · maiúsculas: fino"), r, juce::Justification::centred, 8.0f, 0.2f);
        drawCaps (g, U ("42°14′N 8°43′W"), r, juce::Justification::centredRight, 8.0f, 0.3f);
    }
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
    for (auto& gr : groups) gr.clear();

    // cabeceira: presets, A/B, comparar, desfacer, (gravar) e a calidade
    preset.setBounds (224, btnY, 166, btnH);
    abBtn[0].setBounds (400, btnY + 2, 36, btnH - 4);
    abBtn[1].setBounds (440, btnY + 2, 36, btnH - 4);
    compare.setBounds (488, btnY, 104, btnH);
    undoBtn.setBounds (600, btnY, 34, btnH);
    redoBtn.setBounds (638, btnY, 34, btnH);
    recBtn.setBounds (680, btnY, 30, btnH);
    playBtn.setBounds (714, btnY, 30, btnH);
    for (int i = 0; i < 3; ++i) calBtn[i].setBounds (756 + i * 86, btnY + 2, 82, btnH - 4);

    // pantalla (e o informe, enriba dela; o seu botón no recanto do rastro do ton)
    display.setBounds (16, dispY, W - 32, dispH);
    practicePanel.setBounds (16, dispY, W - 32, dispH);
    practiceBtn.setBounds (W - 16 - 42 - 8 - 12 - 104, dispY + 12, 104, 28);

    // pestanas e, á dereita, o modo e Detectar
    for (int i = 0; i < numTabs; ++i) tabBtn[i].setBounds (32 + i * 108, tabY, 104, tabH);
    for (int i = 0; i < 3; ++i) modeBtn[i].setBounds (604 + i * 96, tabY, 92, tabH);
    detect.setBounds (900, tabY - 2, W - 28 - 900, tabH + 4);

    // unha fila de mandos: os grupos centrados no panel, cada un co seu rótulo enriba;
    // «extra» (un botón) vai despois do primeiro mando do grupo «extraGroup»
    auto row = [&] (int tabIdx, std::initializer_list<std::pair<juce::String, std::vector<juce::Component*>>> gs,
                    int maxGap, juce::Component* extra = nullptr, int extraW = 0, int extraGroup = -1)
    {
        int total = extraW;
        for (auto& gg : gs) total += (int) gg.second.size() * knobW;
        const int inner = W - 32 - 40, n = (int) gs.size();
        const int gap = n > 1 ? juce::jmin (maxGap, (inner - total) / (n - 1)) : 0;
        int x = 16 + 20 + (inner - (total + gap * (n - 1))) / 2;
        int gi = 0;
        for (auto& gg : gs)
        {
            const int x0 = x;
            for (size_t k = 0; k < gg.second.size(); ++k)
            {
                gg.second[k]->setBounds (x, knobY, knobW, knobH);
                x += knobW;
                if (gi == extraGroup && k == 0 && extra != nullptr)
                {
                    // (centrado na altura do disco do mando)
                    extra->setBounds (x + 3, knobY + 46 - 15, extraW - 6, 30);
                    x += extraW;
                }
            }
            groups[tabIdx].push_back ({ { x0, grpY, x - x0, 20 }, gg.first });
            x += gap;
            ++gi;
        }
    };
    row (TabCorrection, { { U ("Afinar"), { &vel, &transi } }, { U ("Tolerancia"), { &tol, &tolT } },
                          { U ("Liberdade"), { &flex, &human } }, { U ("Voz"), { &vib, &cant } } }, 60, &link, 56, 0);
    row (TabVoice, { { U ("Timbre"), { &form, &trans, &air } }, { U ("Vibrato xerado"), { &vgDepth, &vgRate, &vgDelay, &vgVar, &vgTrem } } }, 60);
    row (TabOutput, { { U ("Saída"), { &la, &mest, &vol } }, { "MIDI", { &bend } } }, 60, &midiOut, 150, 1);

    // armonías: o nivel da principal á esquerda e unha fila por voz
    principal.setBounds (32, knobY, knobW, knobH);
    for (int h = 0; h < TuneProcessor::numHarmonies; ++h)
    {
        const int y = hRowY + h * hRowStep;
        hOn[h].setBounds (hX, y - 1, 38, hRowH + 2);
        hInt[h].setBounds (hX + 44, y, 132, hRowH);
        for (int b = 0; b < 5; ++b) hBar[h][b].setBounds (hBarX + b * hBarStep, y, hBarW, hRowH);
    }
    harmMode[0].setBounds (W - 32 - 236, hRowY + 4 * hRowStep + 6, 96, 26);
    harmMode[1].setBounds (W - 32 - 136, hRowY + 4 * hRowStep + 6, 136, 26);

    // selectores (tonalidade, escala, voz, afinación) e .scl; e o teclado
    tonica.setBounds (16, selY, 86, selH);
    escala.setBounds (108, selY, 196, selH);
    voz.setBounds (310, selY, 140, selH);
    afin.setBounds (456, selY, 176, selH);
    scl.setBounds (638, selY, 56, selH);
    keyboard.setBounds (16, kbY, W - 32, kbH);
}
