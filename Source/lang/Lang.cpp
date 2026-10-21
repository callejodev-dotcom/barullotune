#include "Lang.h"
#include <juce_data_structures/juce_data_structures.h>
#include <atomic>
#include <unordered_map>
#include <set>
#include <cstdlib>

namespace lang
{
namespace
{
    struct Row { const char* t[count]; };
    #include "Strings.h"   // static const Row rows[] = { … };

    std::atomic<int> currentId { -1 };

    juce::PropertiesFile::Options options()
    {
        juce::PropertiesFile::Options o;
        o.applicationName = "Barullo Tune";
        o.folderName = "Barullo Tune";
        o.filenameSuffix = "settings";
        o.osxLibrarySubFolder = "Application Support";
        return o;
    }

    Id fromSystem()
    {
        const auto l = juce::SystemStats::getUserLanguage().toLowerCase().substring (0, 2);
        if (l == "es") return es;
        if (l == "en") return en;
        if (l == "pt") return pt;
        if (l == "ca") return ca;
        if (l == "eu") return eu;
        return gl;   // galego tamén para as linguas que aínda non ten
    }

    const std::unordered_map<std::string, const Row*>& table()
    {
        static const auto m = []
        {
            std::unordered_map<std::string, const Row*> mm;
            for (const auto& r : rows) mm[r.t[gl]] = &r;
            return mm;
        }();
        return m;
    }
}

Id current()
{
    int id = currentId.load();
    if (id < 0)
    {
        juce::PropertiesFile f (options());
        const auto saved = f.getValue ("lang", "");
        id = fromSystem();
        for (int i = 0; i < count; ++i) if (saved == nameOf ((Id) i)) id = i;
        currentId = id;
    }
    return (Id) id;
}

void set (Id id)
{
    currentId = (int) id;
    juce::PropertiesFile f (options());
    f.setValue ("lang", nameOf (id));
    f.saveIfNeeded();
}

void setForTesting (Id id) { currentId = (int) id; }

const char* nameOf (Id id)
{
    static const char* n[count] = { "Galego", "Español", "English", "Português", "Català", "Euskara" };
    return n[juce::jlimit (0, (int) count - 1, (int) id)];
}

juce::String tr (const char* g)
{
    // BT_LANG_KEYS=ficheiro: apunta cada texto pedido (para tools/idiomas.py)
    static const char* keysPath = std::getenv ("BT_LANG_KEYS");
    if (keysPath != nullptr && g != nullptr && g[0] != 0)
    {
        static juce::CriticalSection lock;
        static std::set<std::string> seen;
        const juce::ScopedLock sl (lock);
        if (seen.insert (g).second) juce::File (juce::String (keysPath)).appendText (juce::String::fromUTF8 (g).replace ("\n", "\\n") + "\n", false, false, nullptr);
    }
    const Id id = current();
    if (id != gl)
    {
        const auto& m = table();
        const auto it = m.find (g);
        if (it != m.end() && it->second->t[id] != nullptr && it->second->t[id][0] != 0) return juce::String::fromUTF8 (it->second->t[id]);
    }
    return juce::String::fromUTF8 (g);
}

juce::String noteName (int pc)
{
    static const char* romance[12] = { "Do", "Do♯", "Re", "Mi♭", "Mi", "Fa", "Fa♯", "Sol", "La♭", "La", "Si♭", "Si" };
    static const char* port[12] = { "Dó", "Dó♯", "Ré", "Mi♭", "Mi", "Fá", "Fá♯", "Sol", "Lá♭", "Lá", "Si♭", "Si" };
    static const char* eng[12] = { "C", "C♯", "D", "E♭", "E", "F", "F♯", "G", "A♭", "A", "B♭", "B" };
    const int i = ((pc % 12) + 12) % 12;
    const Id id = current();
    return juce::String::fromUTF8 (id == en ? eng[i] : id == pt ? port[i] : romance[i]);
}

int octaveOf (int midi) { return midi / 12 - (current() == en ? 1 : 2); }
}
