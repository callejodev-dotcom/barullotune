#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include <escoita_nucleo/escoita_nucleo.h>

namespace ui
{
    // Os de Escoita: o azul mariño da noite de Vigo, a crema da súa letra e a luz laranxa,
    // nun aparello de estudio (paneis en relevo, pantallas de cristal, visores dourados)
    namespace col
    {
        const juce::Colour noite    { 0xff0c111f };   // fondo (o da web de Escoita)
        const juce::Colour crema    { 0xfffff7c1 };   // texto e voz afinada (o da web)
        const juce::Colour crema2   { 0xffbdb792 };   // texto secundario
        const juce::Colour crema3   { 0xff857f67 };   // rótulos discretos
        const juce::Colour laranxa  { 0xffe08b16 };   // a túa voz, o activo, os LED
        const juce::Colour ouro     { 0xffedba23 };   // os visores
        const juce::Colour verde    { 0xff9dc6a0 };   // afinado e niveis
        const juce::Colour panel1   { 0xff1c2540 }, panel2 { 0xff111830 };    // paneis en relevo
        const juce::Colour tecla1   { 0xff27314f }, tecla2 { 0xff1a2239 };    // teclas
        const juce::Colour visor    { 0xff070b16 };   // visores e pozos
        const juce::Colour cristal1 { 0xff142046 }, cristal2 { 0xff070c1c };  // as pantallas
        const juce::Colour lina     { 0xff2a3350 };   // liñas
        // (os nomes de antes)
        const juce::Colour escuma = crema, sodio = laranxa, neboa = crema2, fondo2 = panel2;
    }

    juce::Font body (float size);    // Inter: texto corrido
    juce::Font semi (float size);    // Montserrat SemiBold: valores
    juce::Font cond (float size);    // Montserrat Bold: a nota, títulos
    // Montserrat en maiúsculas espaciadas (os rótulos, coma na web de Escoita)
    juce::Font caps (float size, float tracking = 0.18f);
    using escoita::maiusculas;                   // maiúsculas, tamén con acentos (do núcleo)
    void drawCaps (juce::Graphics&, const juce::String&, juce::Rectangle<float>, juce::Justification, float size, float tracking = 0.18f);

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
        juce::Font getComboBoxFont (juce::ComboBox& b) override { return caps (juce::jmin (13.5f, (float) b.getHeight() * 0.5f), 0.1f); }
        juce::Font getPopupMenuFont() override { return body (16.0f); }
        void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos, juce::Slider::SliderStyle, juce::Slider&) override;
        void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
        void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
    };

    // pezas do aparello (tamén para o editor)
    void paintPanel (juce::Graphics&, juce::Rectangle<float>, float radius);
    void paintWell (juce::Graphics&, juce::Rectangle<float>, float radius);
    void paintGlass (juce::Graphics&, juce::Rectangle<float>, float radius);
    void paintGlassShine (juce::Graphics&, juce::Rectangle<float>, float radius);

    // a pantalla central: medidores de nivel, a nota cun medidor de agulla e o rastro do ton
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
        int spokenTarget = -2, spokenCents = 999;
        static constexpr int histSize = 520;
        std::array<TuneProcessor::Frame, histSize> hist;
        int head = 0;
        float centre = 60.0f, shown = 60.0f;
        bool voiced = false; int target = -1; float hz = 0, inDev = 0, outDev = 0;
        int hold = 0;
        float inLv = 0, outLv = 0;
    };

    // o informe da práctica: como afina quen canta (por riba da pantalla)
    class PracticePanel : public juce::Component
    {
    public:
        explicit PracticePanel (TuneProcessor& p);
        void paint (juce::Graphics&) override;
        void resized() override;
        void update() { repaint(); }
        std::function<void()> onClose;
    private:
        TuneProcessor& proc;
        juce::TextButton reset, close;
    };

    // Edición: o rolo das notas capturadas, sobre a liña de tempo do DAW. Cada nota é un
    // bloque á altura á que vai (o ton cantado, a liña laranxa); arrastrala arriba ou abaixo
    // lévaa a outra nota (con Alt, cents), dobre clic devólvea a como estaba. Segue o DAW.
    class RoloNotas : public juce::Component, public juce::TooltipClient
    {
    public:
        explicit RoloNotas (TuneProcessor& p);
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
        bool keyPressed (const juce::KeyPress&) override;
        juce::String getTooltip() override;
        void tick();                                  // 30 veces por segundo: notas novas e o DAW
        // accións (cada unha, un paso de desfacer)
        void restaurarSeleccionada();
        void todasAEscala();
        void borrarCaptura();
        void zoom (double factor);
        bool seguir = true;                           // a vista vai co DAW
        uint32_t seleccionada = 0;
        std::function<void()> onSeleccion;            // (a interface amosa a nota escollida)
        double segundos = 8.0;                        // o que se ve
    private:
        struct Vista { juce::Rectangle<float> area; double t0, seg, mLo, mHi; };
        Vista vista() const;
        float xDe (const Vista&, double t) const;
        double tDe (const Vista&, float x) const;
        float yDe (const Vista&, double m) const;
        const Edicion::Nota* notaEn (juce::Point<float>) const;
        void editarCon (uint32_t id, int destino, double cents, bool editada, int destinoAntes, double centsAntes, bool editadaAntes);
        TuneProcessor& proc;
        double t0 = 0.0, centroM = 60.0, altoM = 14.0;   // a vista: comezo (s), centro e alto (semitonos)
        bool vManual = false;
        // arrastre
        uint32_t arrastrando = 0;
        int destino0 = 0; double cents0 = 0; bool editada0 = false;
        float yInicio = 0;
        bool movendoVista = false; double t0Inicio = 0;
        double ultimaPos = -2.0;
        uint32_t ultimaRevision = 0, hover = 0;
        uint16_t ultimaMascara = 0xffff;
    public:
        // (estático: o desfacer vive no procesador e pode durar máis ca a xanela)
        static void aplicar (TuneProcessor&, uint32_t id, int destino, double cents, bool editada);
    };

    // o teclado: cinco oitavas; a cor de cada tecla di se a nota está na escala, fóra ou libre.
    // Coa afinación Personalizada, maiúsculas + arrastrar en vertical cambia os cents da nota
    class Keyboard : public juce::Component, public juce::TooltipClient
    {
    public:
        explicit Keyboard (TuneProcessor& p) : proc (p) { setWantsKeyboardFocus (true); setTitle (juce::String::fromUTF8 ("Teclado da escala")); }
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
        juce::String getTooltip() override;
        bool keyPressed (const juce::KeyPress&) override;
        void focusGained (FocusChangeType) override { if (focusNote < 0) focusNote = 60; announce(); repaint(); }
        void focusLost (FocusChangeType) override { repaint(); }
        static constexpr int lo = 36, hi = 96;   // Do1 … Do6
        static constexpr float bedH = 4.0f;      // o feltro de enriba das teclas
    private:
        int noteAt (juce::Point<float>) const;
        juce::Rectangle<float> keyRect (int note) const;
        bool customTuning() const;
        juce::String describe (int note) const;
        void announce();
        TuneProcessor& proc;
        int hover = -1, dragPc = -1, focusNote = -1;
        float dragStartCents = 0;
    };

    // un control que se pode escribir (clic dereito ou Cmd/Ctrl-clic) e mover fino (maiúsculas): do núcleo
    using ValueSlider = escoita::MandoEditable;

    struct Knob : public juce::Component
    {
        Knob();
        void resized() override;
        void paint (juce::Graphics&) override;
        ValueSlider slider { juce::Slider::RotaryHorizontalVerticalDrag };
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
    void mouseDown (const juce::MouseEvent&) override;   // (global) cada xesto, un paso de desfacer
    void tick() { timerCallback(); }   // para as probas
    void showTab (int t);              // para as probas
    bool typeValue (const juce::String& paramId, const juce::String& text);   // para as probas: escribir nun mando
    ui::Keyboard& keyboardForTests() { return keyboard; }
    void showPractice (bool show) { if (show != practicePanel.isVisible() && practiceBtn.onClick) practiceBtn.onClick(); }
    static constexpr int W = 1040, H = 744;
    enum Tab { TabCorrection = 0, TabVoice, TabHarmony, TabOutput, TabEdit, numTabs };
    void demoEdicion();                // para as capturas: unha toma capturada con dúas notas editadas

private:
    // todo vai debuxado a tamaño fixo nun lenzo que despois se escala co tamaño da xanela
    struct Canvas : juce::Component
    {
        std::function<void (juce::Graphics&)> painter;
        void paint (juce::Graphics& g) override { if (painter) painter (g); }
    };
    // gravar (un círculo) e escoitar en bucle (un triángulo), na app independente
    struct TransportButton : juce::Button
    {
        TransportButton (bool rec) : juce::Button (rec ? "gravar" : "escoitar"), isRecord (rec) {}
        void paintButton (juce::Graphics&, bool over, bool down) override;
        bool isRecord;
    };
    // o aviso dunha versión nova, no pé (unha ligazón á páxina de descargas)
    struct UpdateButton : juce::Button
    {
        UpdateButton() : juce::Button ("actualizar") {}
        void paintButton (juce::Graphics&, bool over, bool down) override;
        juce::String version;
    };
    // botón de desfacer / refacer (unha frecha en arco)
    struct ArrowButton : juce::Button
    {
        ArrowButton (bool redo) : juce::Button (redo ? "refacer" : "desfacer"), isRedo (redo) {}
        void paintButton (juce::Graphics&, bool over, bool down) override;
        bool isRedo;
    };

    void timerCallback() override;
    void paintCanvas (juce::Graphics&);
    void layout();
    void setupKnob (ui::Knob&, const char* id, const char* name, const char* tip);
    void setupSegment (juce::TextButton* b, int count, const char* paramId, const char* const* names, const char* const* tips);
    void setupBar (ui::ValueSlider&, const char* id, const juce::String& tip, const juce::String& title);
    void refresh();
    void rebuildPresetMenu();
    void presetChosen();
    void importScala();

    TuneProcessor& proc;
    ui::Look look;
    Canvas canvas;
    juce::TooltipWindow tooltips { this, 500 };

    ui::Display display { proc };
    ui::Keyboard keyboard { proc };
    ui::PracticePanel practicePanel { proc };
    juce::TextButton practiceBtn;

    // cabeceira
    juce::ComboBox preset;
    juce::TextButton abBtn[2], compare;
    ArrowButton undoBtn { false }, redoBtn { true };
    TransportButton recBtn { true }, playBtn { false };
    bool standaloneUi() const;
    juce::TextButton calBtn[3];
    // pestanas
    juce::TextButton tabBtn[numTabs];
    int tab = TabCorrection;
    // corrección
    ui::Knob vel, transi, tol, tolT, flex, human, vib, cant;
    juce::TextButton link;
    float lastVel = -1, lastTransi = -1;
    // voz
    ui::Knob form, trans, air, vgDepth, vgRate, vgDelay, vgVar, vgTrem;
    // armonías
    ui::Knob principal;
    juce::TextButton harmMode[2];
    juce::TextButton hOn[TuneProcessor::numHarmonies];
    juce::ComboBox hInt[TuneProcessor::numHarmonies];
    ui::ValueSlider hBar[TuneProcessor::numHarmonies][5];
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> buttonAtts;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>> comboAtts;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> sliderAtts;
    // saída
    ui::Knob la, mest, vol, bend;
    juce::TextButton midiOut;
    // edición
    ui::RoloNotas rolo { proc };
    juce::TextButton edCapturar, edSeguir, edEscala, edRestaurar, edBorrar, edZoomMenos, edZoomMais;
    // modo, tonalidade, voz e afinación
    juce::ComboBox tonica, escala, voz, afin;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> tonicaAtt, escalaAtt, vozAtt, afinAtt;
    juce::TextButton detect, scl, modeBtn[3];
    std::unique_ptr<juce::FileChooser> chooser;
    juce::String lastPresetList;
    juce::SharedResourcePointer<escoita::Actualizacions> updates;
    UpdateButton updateBtn;
    void refreshUpdate();
    // os grupos de mandos de cada pestana (o rótulo e o seu ancho), para debuxalos
    std::vector<std::pair<juce::Rectangle<int>, juce::String>> groups[numTabs];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TuneEditor)
};
