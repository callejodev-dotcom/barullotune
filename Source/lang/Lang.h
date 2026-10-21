// Barullo Tune — idiomas da interface e dos nomes dos parámetros.
//
// Os textos escríbense no código en galego, dentro de T("…"); a táboa de
// Source/lang/Strings.h ten cada un en galego, castelán, inglés, portugués, catalán e
// éuscaro. Se falta unha tradución, sae en galego (tools/idiomas.py compróbao).
// O idioma é o do sistema (se é un destes) ou o galego, e pódese cambiar na interface:
// gárdase nas preferencias do usuario. Os nomes dos parámetros cambian ao volver cargar
// o plugin (os DAW lense unha vez).
#pragma once
#include <juce_core/juce_core.h>

namespace lang
{
    enum Id { gl = 0, es, en, pt, ca, eu, count };

    Id current();
    void set (Id id);               // e gárdao nas preferencias
    void setForTesting (Id id);     // sen gardar (as probas)
    const char* nameOf (Id id);     // «Galego», «Español»…
    juce::String tr (const char* galicianUtf8);

    // nome dunha nota na lingua actual (en inglés, C D E…; o resto, Do Re Mi…)
    juce::String noteName (int pitchClass);
    // a oitava: o Do central é Do3 nas linguas románicas e no éuscaro, C4 en inglés
    int octaveOf (int midiNote);
}

#define T(x) ::lang::tr (x)
