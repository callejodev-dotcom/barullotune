#include "Actualizacions.h"

namespace actualizacions
{
bool isNewer (const juce::String& candidate, const juce::String& current)
{
    auto parts = [] (const juce::String& s)
    {
        juce::StringArray a;
        a.addTokens (s.trim().trimCharactersAtStart ("vV"), ".", {});
        std::array<int, 3> n {};
        for (int i = 0; i < 3 && i < a.size(); ++i) n[(size_t) i] = a[i].getIntValue();
        return n;
    };
    return parts (candidate) > parts (current);
}

juce::String downloadsUrl() { return "https://github.com/callejodev-dotcom/barullotune/releases/latest"; }

Checker::Checker() : juce::Thread ("Barullo Tune: versions")
{
   #ifdef BARULLO_NO_PLUGIN_ENTRY
    // na proba non se sae á rede nin se tocan os axustes; BT_NOVA_VERSION finxe unha versión nova
    if (auto* v = std::getenv ("BT_NOVA_VERSION")) latest = v;
   #else
    juce::PropertiesFile::Options o;
    o.applicationName = "Barullo Tune";
    o.folderName = "Barullo Tune";
    o.filenameSuffix = ".settings";
    o.osxLibrarySubFolder = "Application Support";
    o.millisecondsBeforeSaving = -1;   // gárdase á man (tamén desde o fío)
    settings = std::make_unique<juce::PropertiesFile> (o);
    startIfDue();
   #endif
}

Checker::~Checker()
{
    signalThreadShouldExit();
    {
        const juce::ScopedLock sl (lock);
        if (stream != nullptr) stream->cancel();
    }
    stopThread (10000);
}

bool Checker::enabled() const { return settings == nullptr || settings->getBoolValue ("avisarVersions", true); }

void Checker::setEnabled (bool on)
{
    if (settings == nullptr) return;
    settings->setValue ("avisarVersions", on);
    settings->saveIfNeeded();
    if (on) startIfDue();
}

juce::String Checker::newVersion() const
{
    if (! enabled()) return {};
    const juce::ScopedLock sl (lock);
    return isNewer (latest, BARULLO_VERSION) ? latest : juce::String();
}

// o que se soubo hoxe vale; se pasou un día (ou nunca se preguntou), pregúntase
void Checker::startIfDue()
{
    if (settings == nullptr || ! enabled()) return;
    {
        const juce::ScopedLock sl (lock);
        latest = settings->getValue ("ultimaVersion");
    }
    const auto now = juce::Time::currentTimeMillis();
    const auto last = settings->getValue ("ultimaComprobacion").getLargeIntValue();
    if (now >= last && now - last < 24 * 3600 * 1000LL) return;
    if (! isThreadRunning()) startThread (juce::Thread::Priority::low);
}

void Checker::run()
{
    {
        const juce::ScopedLock sl (lock);
        if (threadShouldExit()) return;
        stream = std::make_unique<juce::WebInputStream> (juce::URL ("https://api.github.com/repos/callejodev-dotcom/barullotune/releases/latest"), false);
        stream->withExtraHeaders ("Accept: application/vnd.github+json\r\nUser-Agent: BarulloTune/" BARULLO_VERSION)
               .withConnectionTimeout (8000);
    }
    juce::String text;
    if (stream->connect (nullptr) && stream->getStatusCode() == 200 && ! threadShouldExit())
        text = stream->readEntireStreamAsString();
    {
        const juce::ScopedLock sl (lock);
        stream.reset();
    }
    // sen rede ou sen resposta: nada (volverase preguntar a próxima vez)
    const auto tag = juce::JSON::parse (text).getProperty ("tag_name", {}).toString().trim().trimCharactersAtStart ("vV");
    if (threadShouldExit() || tag.isEmpty()) return;
    {
        const juce::ScopedLock sl (lock);
        latest = tag;
    }
    settings->setValue ("ultimaVersion", tag);
    settings->setValue ("ultimaComprobacion", juce::String (juce::Time::currentTimeMillis()));
    settings->saveIfNeeded();
}
}
