#pragma once
// Einzeiliges Textfeld (nur der Editier-Zustand, ohne Zeichnen): Cursor, Markierung, Einfuegen, Loeschen,
// Wortspruenge. Die Oberflaeche zeichnet das Feld selbst (Direct2D) und reicht Tasten/Zeichen hierher weiter.
#include <algorithm>
#include <cwctype>
#include <string>

namespace docs {

struct TextEdit {
    std::wstring text;
    size_t caret = 0, anchor = 0;   // Markierung = [min, max) der beiden
    size_t maxLen = 120;

    bool HasSelection() const { return caret != anchor; }
    size_t SelBegin() const { return std::min(caret, anchor); }
    size_t SelEnd() const { return std::max(caret, anchor); }
    std::wstring Selected() const { return text.substr(SelBegin(), SelEnd() - SelBegin()); }

    void SetText(const std::wstring& s)
    {
        text = s.substr(0, maxLen);
        caret = anchor = text.size();
    }
    void DeleteSelection()
    {
        if (!HasSelection()) return;
        text.erase(SelBegin(), SelEnd() - SelBegin());
        caret = anchor = std::min(caret, anchor);
    }
    // Steuerzeichen (ausser Tab -> Leerzeichen) werden verworfen, die Laenge begrenzt
    void Insert(const std::wstring& in)
    {
        std::wstring s;
        for (wchar_t c : in) {
            if (c == L'\t') c = L' ';
            if (c >= 0x20 && c != 0x7F) s.push_back(c);
        }
        DeleteSelection();
        const size_t room = text.size() >= maxLen ? 0 : maxLen - text.size();
        s = s.substr(0, room);
        text.insert(caret, s);
        caret = anchor = caret + s.size();
    }
    void Backspace()
    {
        if (HasSelection()) { DeleteSelection(); return; }
        if (caret == 0) return;
        text.erase(caret - 1, 1);
        caret = anchor = caret - 1;
    }
    void Delete()
    {
        if (HasSelection()) { DeleteSelection(); return; }
        if (caret >= text.size()) return;
        text.erase(caret, 1);
        anchor = caret;
    }
    void SetCaret(size_t pos, bool extend)
    {
        caret = std::min(pos, text.size());
        if (!extend) anchor = caret;
    }
    // dir < 0 links, > 0 rechts; word = bis zum naechsten Wortanfang/-ende (Strg)
    void Move(int dir, bool extend, bool word)
    {
        if (!extend && HasSelection()) {   // ohne Umschalt: Markierung auf den passenden Rand zusammenziehen
            SetCaret(dir < 0 ? SelBegin() : SelEnd(), false);
            return;
        }
        size_t p = caret;
        if (dir < 0) {
            if (p > 0) --p;
            if (word) {
                while (p > 0 && std::iswspace(text[p])) --p;
                while (p > 0 && !std::iswspace(text[p - 1])) --p;
            }
        } else {
            if (p < text.size()) ++p;
            if (word) {
                while (p < text.size() && !std::iswspace(text[p])) ++p;
                while (p < text.size() && std::iswspace(text[p])) ++p;
            }
        }
        SetCaret(p, extend);
    }
    void Home(bool extend) { SetCaret(0, extend); }
    void End(bool extend) { SetCaret(text.size(), extend); }
    void SelectAll() { anchor = 0; caret = text.size(); }
};

}  // namespace docs
