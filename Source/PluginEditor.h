#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"

namespace ui
{
    // Noite de Vigo: o azul mariño de Escoita, a luz de sodio das farolas para a túa voz
    // e a escuma do mar para a voz afinada.
    namespace col
    {
        const juce::Colour noite   { 0xff0c111f };   // fondo (o da web de Escoita)
        const juce::Colour fondo2  { 0xff10172a };   // paneis
        const juce::Colour carril  { 0xff172038 };   // carrís das notas da escala
        const juce::Colour lina    { 0xff28314b };   // liñas e bordos
        const juce::Colour neboa   { 0xff8d97b0 };   // texto secundario
        const juce::Colour escuma  { 0xffeef3ff };   // texto e voz afinada
        const juce::Colour sodio   { 0xfff2a23a };   // a túa voz e os controis activos
    }

    juce::Font body (float size);
    juce::Font semi (float size);
    juce::Font cond (float size);

    struct Look : juce::LookAndFeel_V4
    {
        Look();
        juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;
        void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
        void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
        void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&) override;
        void drawLabel (juce::Graphics&, juce::Label&) override;
        void drawTooltip (juce::Graphics&, const juce::String&, int w, int h) override;
        juce::Rectangle<int> getTooltipBounds (const juce::String&, juce::Point<int>, juce::Rectangle<int>) override;
        void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
        juce::Font getComboBoxFont (juce::ComboBox&) override { return semi (16.0f); }
        juce::Font getPopupMenuFont() override { return body (16.0f); }
        void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
        void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
    };

    // a pantalla central: medidores de nivel, a nota cun arco de cents e o rastro do ton
    class Display : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        explicit Display (TuneProcessor& p) : proc (p) { hist.fill ({ 0, 0, -1, 0 }); }
        void paint (juce::Graphics&) override;
        void update();
    private:
        void paintMeter (juce::Graphics&, juce::Rectangle<float>, float level, const juce::String& label);
        void paintArc (juce::Graphics&, juce::Rectangle<float>);
        void paintTrace (juce::Graphics&, juce::Rectangle<float>);
        TuneProcessor& proc;
        static constexpr int histSize = 520;
        std::array<TuneProcessor::Frame, histSize> hist;
        int head = 0;
        float centre = 60.0f, shown = 60.0f;
        bool voiced = false; int target = -1; float hz = 0, inDev = 0, outDev = 0;
        int hold = 0;
        float inLv = 0, outLv = 0;
    };

    // o teclado: unha tira enriba de cada tecla di se a nota está na escala, fóra ou libre
    class Keyboard : public juce::Component, public juce::TooltipClient
    {
    public:
        explicit Keyboard (TuneProcessor& p) : proc (p) {}
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
        juce::String getTooltip() override;
        static constexpr int lo = 36, hi = 96;   // Do1 … Do6
        static constexpr float stripH = 12.0f;
    private:
        int noteAt (juce::Point<float>) const;
        juce::Rectangle<float> keyRect (int note) const;
        TuneProcessor& proc;
        int hover = -1;
    };

    struct Knob : public juce::Component
    {
        Knob();
        void resized() override;
        void paint (juce::Graphics&) override;
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
        juce::String name;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
    };
}

class TuneEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit TuneEditor (TuneProcessor&);
    ~TuneEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void tick() { timerCallback(); }   // para as probas
    static constexpr int W = 1040, H = 664;

private:
    // todo vai debuxado a tamaño fixo nun lenzo que despois se escala co tamaño da xanela
    struct Canvas : juce::Component
    {
        std::function<void (juce::Graphics&)> painter;
        void paint (juce::Graphics& g) override { if (painter) painter (g); }
    };

    void timerCallback() override;
    void paintCanvas (juce::Graphics&);
    void layout();
    void setupKnob (ui::Knob&, const char* id, const char* name, const char* tip);
    void setupSegment (juce::TextButton* b, int count, const char* paramId, const char* const* names, const char* const* tips);
    void refresh();

    TuneProcessor& proc;
    ui::Look look;
    Canvas canvas;
    juce::TooltipWindow tooltips { this, 500 };

    ui::Display display { proc };
    ui::Keyboard keyboard { proc };

    juce::ComboBox preset, tonica, escala, voz;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> tonicaAtt, escalaAtt, vozAtt;
    juce::TextButton detect, link, compare;
    juce::TextButton modeBtn[3], calBtn[2];

    ui::Knob vel, transi, tol, tolT, vib, cant, form, trans, la, mest, vol;
    float lastVel = -1, lastTransi = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TuneEditor)
};
