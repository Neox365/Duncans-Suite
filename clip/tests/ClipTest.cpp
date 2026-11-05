// Test der Clip-Logik (Duncan's Clip). Nur Windows. Veraendert kurz die Zwischenablage und stellt sie danach wieder her.
// g++ -std=c++17 -DUNICODE -D_UNICODE -I.. ClipTest.cpp ../ClipLogic.cpp -o ClipTest.exe -lole32 -lcrypt32 -luser32 -lshell32 -ladvapi32 -static
#include "../ClipLogic.h"

#include <cstdio>
#include <string>

using namespace clip;

static int g_fail = 0, g_pass = 0;
#define CHECK(cond) do { if (cond) ++g_pass; else { ++g_fail; std::printf("FEHLER Zeile %d: %s\n", __LINE__, #cond); } } while (0)

// Text ohne Sperrmarkierung in die Zwischenablage legen (wie ein normales Programm)
static void SetPlain(const std::wstring& t) {
    OpenClipboard(nullptr); EmptyClipboard();
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, (t.size() + 1) * sizeof(wchar_t));
    memcpy(GlobalLock(h), t.c_str(), (t.size() + 1) * sizeof(wchar_t)); GlobalUnlock(h);
    SetClipboardData(CF_UNICODETEXT, h);
    CloseClipboard();
}
static void SetWithFormat(const std::wstring& t, const wchar_t* fmtName, bool dword, DWORD value) {
    OpenClipboard(nullptr); EmptyClipboard();
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, (t.size() + 1) * sizeof(wchar_t));
    memcpy(GlobalLock(h), t.c_str(), (t.size() + 1) * sizeof(wchar_t)); GlobalUnlock(h);
    SetClipboardData(CF_UNICODETEXT, h);
    HGLOBAL f = GlobalAlloc(GMEM_MOVEABLE, dword ? sizeof(DWORD) : 1);
    if (dword) *static_cast<DWORD*>(GlobalLock(f)) = value; else *static_cast<char*>(GlobalLock(f)) = 0;
    GlobalUnlock(f);
    SetClipboardData(RegisterClipboardFormatW(fmtName), f);
    CloseClipboard();
}

int main() {
    wchar_t tmp[MAX_PATH]; GetTempPathW(MAX_PATH, tmp);
    const std::wstring dir = std::wstring(tmp) + L"ClipTest_" + std::to_wstring(GetCurrentProcessId());
    SetDataDirForTest(dir);

    // Verlauf
    Store s;
    CHECK(AddHistory(s, L"eins"));
    CHECK(AddHistory(s, L"zwei"));
    CHECK(!AddHistory(s, L"zwei"));                       // schon vorn
    CHECK(AddHistory(s, L"eins"));                        // nach vorn geholt
    CHECK(s.history.size() == 2 && s.history[0] == L"eins" && s.history[1] == L"zwei");
    CHECK(!AddHistory(s, L"   \r\n\t "));                 // leer
    CHECK(!AddHistory(s, std::wstring(kMaxTextLen + 1, L'x')));
    CHECK(AddHistory(s, std::wstring(kMaxTextLen, L'x')));
    for (int i = 0; i < 80; ++i) AddHistory(s, L"Eintrag " + std::to_wstring(i));
    CHECK(s.history.size() == kMaxHistory);
    CHECK(s.history[0] == L"Eintrag 79" && s.history[kMaxHistory - 1] == L"Eintrag 30");
    CHECK(RemoveHistory(s, 0) && s.history[0] == L"Eintrag 78");
    CHECK(!RemoveHistory(s, 999));

    // Prompts, Platzhalter, Filter, Vorschau
    CHECK(MakePrompt(L"\r\n  Fasse zusammen:  \nText hier").title == L"Fasse zusammen:");
    CHECK(MakePrompt(L"").title == L"Prompt");
    CHECK(MakePrompt(std::wstring(100, L'a')).title.size() == 40);
    CHECK(Expand(L"Fasse zusammen: {clip}", L"ABC") == L"Fasse zusammen: ABC");
    CHECK(Expand(L"{CLIP} und {Clip}", L"X") == L"X und X");
    CHECK(Expand(L"ohne Platzhalter", L"X") == L"ohne Platzhalter");
    CHECK(Expand(L"a{clip}b", L"") == L"ab");
    CHECK(Expand(L"{clip}{clip}", L"1") == L"11");
    CHECK(Matches(L"Hallo Welt", L"") && Matches(L"Hallo Welt", L"welt hal") && !Matches(L"Hallo Welt", L"welt xyz"));
    CHECK(Matches(L"ÄÖÜ Straße", L"äöü"));
    CHECK(Preview(L"a\r\n\tb   c", 100) == L"a b c");
    CHECK(Preview(L"abcdefghij", 5) == L"abcd…");
    CHECK(Preview(L"   ", 5).empty());

    // Sperrregeln
    CHECK(IsPasswordManagerExe(L"keepassxc.exe") && IsPasswordManagerExe(L"duncansvault.exe") && !IsPasswordManagerExe(L"notepad.exe"));
    CHECK(!ShouldSkip(L"normal", false, L"notepad.exe"));
    CHECK(ShouldSkip(L"geheim", true, L"notepad.exe"));
    CHECK(ShouldSkip(L"geheim", false, L"bitwarden.exe"));
    CHECK(ShouldSkip(L"  ", false, L""));
    CHECK(ShouldSkip(std::wstring(kMaxTextLen + 1, L'a'), false, L""));

    // Verschluesselung und Speichern
    std::string c, p;
    CHECK(Protect("Geheimnis äöü", c) && c != "Geheimnis äöü" && Unprotect(c, p) && p == "Geheimnis äöü");
    CHECK(!Unprotect("kein gueltiger Block", p));
    CHECK(LoadPrompts().empty() && LoadHistory().empty());           // nichts gespeichert
    std::vector<Prompt> prompts = { { L"Zusammenfassen", L"Fasse zusammen:\r\n{clip}" }, { L"Übersetzen ö", L"Übersetze ins Englische: {clip}" }, { L"Leer", L"" } };
    CHECK(SavePrompts(prompts));
    std::vector<Prompt> back = LoadPrompts();
    CHECK(back.size() == 3 && back[0].title == prompts[0].title && back[0].text == prompts[0].text && back[1].text == prompts[1].text && back[2].text.empty());
    prompts.push_back({ L"Sonder\x1f" L"zeichen", L"a\x1e" L"b" });   // Trennzeichen werden entschaerft, nichts zerbricht
    CHECK(SavePrompts(prompts) && LoadPrompts().size() == 4);
    CHECK(SaveHistory({ L"eins", L"zwei\nmit Umbruch", L"drei äöü" }));
    std::vector<std::wstring> hb = LoadHistory();
    CHECK(hb.size() == 3 && hb[1] == L"zwei\nmit Umbruch" && hb[2] == L"drei äöü");
    DeleteHistoryFile();
    CHECK(LoadHistory().empty());
    {   // beschaedigte Datei: keine Prompts, kein Absturz
        HANDLE h = CreateFileW((dir + L"\\prompts.dat").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        DWORD w; WriteFile(h, "muell", 5, &w, nullptr); CloseHandle(h);
        CHECK(LoadPrompts().empty());
    }
    Settings st; st.saveHistory = true; st.hotkey = 2;
    CHECK(SaveSettings(st));
    Settings st2 = LoadSettings();
    CHECK(st2.saveHistory && st2.hotkey == 2);
    CHECK(HotkeyPreset(-5).vk == 'V' && HotkeyPreset(99).vk == 'C' && HotkeyCount() == 3);

    // Zwischenablage (alten Text sichern und wiederherstellen)
    std::wstring original; const bool hadText = ReadClipboardText(original);
    CHECK(WriteClipboardText(L"Testtext äöü\r\nzweite Zeile"));
    std::wstring got; CHECK(ReadClipboardText(got) && got == L"Testtext äöü\r\nzweite Zeile");
    CHECK(ClipboardIsExcluded());                                     // eigene Schreibvorgaenge sind markiert
    SetPlain(L"normal kopiert");
    CHECK(!ClipboardIsExcluded() && ReadClipboardText(got) && got == L"normal kopiert");
    SetWithFormat(L"geheim", L"ExcludeClipboardContentFromMonitorProcessing", false, 0);
    CHECK(ClipboardIsExcluded());
    SetWithFormat(L"geheim2", L"CanIncludeInClipboardHistory", true, 0);
    CHECK(ClipboardIsExcluded());
    SetWithFormat(L"offen", L"CanIncludeInClipboardHistory", true, 1);
    CHECK(!ClipboardIsExcluded());
    (void)ClipboardOwnerExe();                                        // darf nicht abstuerzen (kein Besitzer bei Fremdschreibern)
    if (hadText) SetPlain(original); else { OpenClipboard(nullptr); EmptyClipboard(); CloseClipboard(); }

    // Autostart (nur, wenn er vorher aus war; danach wieder aus)
    if (!GetAutostart()) {
        CHECK(SetAutostart(true, L"C:\\Test\\DuncansClip.exe") && GetAutostart());
        CHECK(SetAutostart(false, L"") && !GetAutostart());
    } else std::printf("Hinweis: Autostart war schon an, Test uebersprungen\n");

    std::printf("%d bestanden, %d fehlgeschlagen\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
