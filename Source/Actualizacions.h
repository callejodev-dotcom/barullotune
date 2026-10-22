#pragma once
#include <juce_data_structures/juce_data_structures.h>

// Avisar das versións novas: unha vez ao día, nun fío aparte, pregúntase a GitHub cal é
// a última versión publicada (sen mandar ningún dato nin descargar nada máis). O resultado
// gárdase nos axustes, así que as outras instancias e as aperturas do mesmo día non volven
// preguntar. Pódese desactivar (menú dos presets).
namespace actualizacions
{
    // «candidate» é posterior a «current»? (números separados por puntos; acepta un «v» diante)
    bool isNewer (const juce::String& candidate, const juce::String& current);

    // a páxina onde se descarga a última versión
    juce::String downloadsUrl();

    class Checker : private juce::Thread
    {
    public:
        Checker();
        ~Checker() override;
        // a versión nova (sen «v»), ou baleiro se non hai, non se sabe ou está desactivado
        juce::String newVersion() const;
        bool enabled() const;
        void setEnabled (bool);

    private:
        void run() override;
        void startIfDue();
        std::unique_ptr<juce::PropertiesFile> settings;
        mutable juce::CriticalSection lock;
        juce::String latest;
        std::unique_ptr<juce::WebInputStream> stream;
    };
}
