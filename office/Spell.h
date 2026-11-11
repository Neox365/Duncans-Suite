#pragma once
// Rechtschreibpruefung ueber die Windows-Schnittstelle ISpellChecker (Windows 8 und neuer, dieselben Woerterbuecher wie in anderen Windows-Programmen).
// Deutsch wird bevorzugt, sonst Englisch, sonst die erste Sprache, die Windows kann. Nichts verlaesst den PC.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>

#include <algorithm>
#include <string>
#include <vector>

#if defined(__has_include)
#if __has_include(<spellcheck.h>)
#include <spellcheck.h>
#define DUNCANS_HAVE_SPELLCHECK 1
#endif
#endif

namespace spell {

struct Miss {
    LONG start = 0, length = 0;    // Stelle im geprueften Text (Zeichen)
    std::wstring word;
    bool repeated = false;         // doppeltes Wort ("das das"): Vorschlag ist, eines zu loeschen
    std::wstring replacement;      // Ersetzung, die Windows selbst vorschlaegt (z. B. bei bekannten Tippfehlern)
};

// Woerter, die Windows oft zu Unrecht meldet und die in Rechnungen/Briefen ueberall vorkommen: Zahlen, Kennungen, Adressen
inline bool IgnorableWord(const std::wstring& w)
{
    bool letter = false;
    for (wchar_t c : w) {
        if (c >= L'0' && c <= L'9') return true;                   // IBAN, Rechnungsnummern, Postleitzahlen
        if (c == L'@' || c == L'/' || c == L'\\' || c == L'_') return true;   // Adressen, Pfade
        if (iswalpha(c)) letter = true;
    }
    if (!letter) return true;
    if (w.size() >= 4 && (w.compare(0, 4, L"www.") == 0 || w.compare(0, 4, L"http") == 0)) return true;
    return false;
}

class Checker {
public:
    explicit Checker(const std::wstring& preferred = L"de-DE") { Create(preferred); }
    ~Checker() { Release(); }
    Checker(const Checker&) = delete;
    Checker& operator=(const Checker&) = delete;

    bool Ok() const { return m_ok; }
    const std::wstring& Language() const { return m_lang; }

    // Faengt bei Zeichen from an und liefert bis zu maxMisses Fehler (Positionen beziehen sich auf den ganzen Text). Zahlen, Adressen und Kennungen zaehlen nicht.
    std::vector<Miss> Find(const std::wstring& text, size_t from = 0, size_t maxMisses = 1) const
    {
        std::vector<Miss> out;
#ifdef DUNCANS_HAVE_SPELLCHECK
        if (!m_ok || from >= text.size()) return out;
        const std::wstring part = text.substr(from);
        IEnumSpellingError* en = nullptr;
        if (FAILED(m_chk->Check(part.c_str(), &en)) || !en) return out;
        for (;;) {
            ISpellingError* e = nullptr;
            if (en->Next(&e) != S_OK || !e) break;
            ULONG st = 0, ln = 0;
            CORRECTIVE_ACTION act = CORRECTIVE_ACTION_NONE;
            e->get_StartIndex(&st);
            e->get_Length(&ln);
            e->get_CorrectiveAction(&act);
            Miss m;
            m.start = static_cast<LONG>(from + st);
            m.length = static_cast<LONG>(ln);
            if (st + ln <= part.size()) m.word = part.substr(st, ln);
            m.repeated = act == CORRECTIVE_ACTION_DELETE;
            if (act == CORRECTIVE_ACTION_REPLACE) {
                LPWSTR r = nullptr;
                if (SUCCEEDED(e->get_Replacement(&r)) && r) { m.replacement = r; CoTaskMemFree(r); }
            }
            e->Release();
            if (m.word.empty() || (!m.repeated && IgnorableWord(m.word))) continue;
            out.push_back(std::move(m));
            if (out.size() >= maxMisses) break;
        }
        en->Release();
#else
        (void)text; (void)from; (void)maxMisses;
#endif
        return out;
    }

    std::vector<std::wstring> Suggest(const std::wstring& word, size_t maxCount = 8) const
    {
        std::vector<std::wstring> out;
#ifdef DUNCANS_HAVE_SPELLCHECK
        if (!m_ok) return out;
        IEnumString* en = nullptr;
        if (FAILED(m_chk->Suggest(word.c_str(), &en)) || !en) return out;
        for (;;) {
            LPOLESTR s = nullptr;
            ULONG got = 0;
            if (en->Next(1, &s, &got) != S_OK || !s) break;
            out.push_back(s);
            CoTaskMemFree(s);
            if (out.size() >= maxCount) break;
        }
        en->Release();
#else
        (void)word; (void)maxCount;
#endif
        return out;
    }

    void Ignore(const std::wstring& word)   // fuer diese Sitzung
    {
#ifdef DUNCANS_HAVE_SPELLCHECK
        if (m_ok) m_chk->Ignore(word.c_str());
#else
        (void)word;
#endif
    }

    void AddToDictionary(const std::wstring& word)   // dauerhaft (benutzerdefiniertes Windows-Woerterbuch)
    {
#ifdef DUNCANS_HAVE_SPELLCHECK
        if (m_ok) m_chk->Add(word.c_str());
#else
        (void)word;
#endif
    }

private:
    void Create(const std::wstring& preferred)
    {
#ifdef DUNCANS_HAVE_SPELLCHECK
        const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        m_coInit = SUCCEEDED(hr);   // auch S_FALSE zaehlt (wieder freigeben); RPC_E_CHANGED_MODE: schon anders initialisiert, COM bleibt benutzbar
        if (FAILED(CoCreateInstance(__uuidof(SpellCheckerFactory), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&m_fac)))) return;
        std::vector<std::wstring> tries = {preferred, L"de", L"en-US", L"en-GB", L"en"};
        for (const std::wstring& tag : tries) {
            BOOL sup = FALSE;
            if (SUCCEEDED(m_fac->IsSupported(tag.c_str(), &sup)) && sup && SUCCEEDED(m_fac->CreateSpellChecker(tag.c_str(), &m_chk)) && m_chk) {
                m_lang = tag;
                m_ok = true;
                return;
            }
        }
#else
        (void)preferred;
#endif
    }
    void Release()
    {
#ifdef DUNCANS_HAVE_SPELLCHECK
        if (m_chk) m_chk->Release();
        if (m_fac) m_fac->Release();
        if (m_coInit) CoUninitialize();
#endif
    }
#ifdef DUNCANS_HAVE_SPELLCHECK
    ISpellCheckerFactory* m_fac = nullptr;
    ISpellChecker* m_chk = nullptr;
    bool m_coInit = false;
#endif
    bool m_ok = false;
    std::wstring m_lang;
};

}  // namespace spell
