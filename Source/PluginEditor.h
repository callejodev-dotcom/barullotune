#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"

namespace ui
{
    // Mesma familia ca Barullo FM: corpo negro, botóns de goma, rótulos verde auga e
    // ámbar, ventás de LED vermello.
    namespace col
    {
        const juce::Colour body      { 0xff151517 };
        const juce::Colour bodyHi    { 0xff1e1f22 };
        const juce::Colour bodyLo    { 0xff0b0b0c };
        const juce::Colour line      { 0xff3b3c41 };
        const juce::Colour rubber    { 0xff4a4b50 };
        const juce::Colour print     { 0xffd8d5cc };
        const juce::Colour printDim  { 0xff9a9ca3 };
        const juce::Colour teal      { 0xff58c6a0 };
        const juce::Colour amber     { 0xffe9a24a };
        const juce::Colour gold      { 0xffc9a648 };
        const juce::Colour redBtn    { 0xffd8483c };
        const juce::Colour led       { 0xffff3b2f };
        const juce::Colour ledOff    { 0xff2b0806 };
        const juce::Colour ledBg     { 0xff120302 };
        const juce::Colour green     { 0xff45e39a };
        const juce::Colour greenOff  { 0xff10291d };
        const juce::Colour ivory     { 0xfff4f3ee };
        const juce::Colour blackKey  { 0xff1a1b1e };
    }

    juce::Font body (float size);
    juce::Font bold (float size);
    juce::Font cond (float size);
    void printed (juce::Graphics&, const juce::String&, juce::Rectangle<float>, float size, juce::Colour, juce::Justification, float tracking = 0.08f);

    struct Look : juce::LookAndFeel_V4
    {
        Look();
        juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;
        void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
        void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
        void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                               juce::Slider::SliderStyle, juce::Slider&) override;
        juce::Label* createSliderTextBox (juce::Slider&) override;
        void drawLabel (juce::Graphics&, juce::Label&) override;
        void drawTooltip (juce::Graphics&, const juce::String&, int w, int h) override;
        juce::Rectangle<int> getTooltipBounds (const juce::String&, juce::Point<int>, juce::Rectangle<int>) override;
        void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
        juce::Font getComboBoxFont (juce::ComboBox&) override { return bold (15.0f); }
        juce::Font getPopupMenuFont() override { return body (15.0f); }
        void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    };

    // a nota e o medidor de afinación
    class NoteView : public juce::Component
    {
    public:
        explicit NoteView (TuneProcessor& p) : proc (p) {}
        void paint (juce::Graphics&) override;
        void update();
    private:
        TuneProcessor& proc;
        bool voiced = false; int target = -1; float hz = 0, inDev = 0, outDev = 0;
        int holdFrames = 0;
    };

    // o gráfico do ton: entrada (ámbar) e saída (vermello), coas notas da escala
    class PitchGraph : public juce::Component
    {
    public:
        explicit PitchGraph (TuneProcessor& p) : proc (p) { hist.fill ({ 0, 0, -1, 0 }); }
        void paint (juce::Graphics&) override;
        void drain();
    private:
        TuneProcessor& proc;
        static constexpr int histSize = 800;
        std::array<TuneProcessor::Frame, histSize> hist;
        int head = 0;
        float centre = 60.0f;
    };

    // unha oitava de teclas: acesas as notas permitidas; pulsar cambia a escala
    class ScaleKeys : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        explicit ScaleKeys (TuneProcessor& p) : proc (p) {}
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void update();
    private:
        juce::Rectangle<float> keyRect (int pc) const;
        TuneProcessor& proc;
        uint16_t mask = 0, held = 0; int target = -1; int mode = 0;
    };

    struct Knob
    {
        juce::Label label;
        juce::Slider slider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
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

private:
    void timerCallback() override;
    void setupKnob (ui::Knob&, const char* id, const char* name, const char* tip);
    void refreshModes();

    TuneProcessor& proc;
    ui::Look look;
    juce::TooltipWindow tooltips { this, 600 };

    ui::NoteView noteView { proc };
    ui::PitchGraph graph { proc };
    ui::ScaleKeys keys { proc };

    juce::ComboBox tonica, escala;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> tonicaAtt, escalaAtt;
    juce::TextButton modeBtn[3];

    ui::Knob vel, cant, hum, trans, la, mest, vol;

    juce::Rectangle<int> secNote, secGraph, secScale, secMode, secCtl;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TuneEditor)
};
