// Test der Clip-Logik (Duncan's Clip). Nur Windows. Veraendert kurz die Zwischenablage und stellt sie danach wieder her.
// g++ -std=c++17 -DUNICODE -D_UNICODE -I.. ClipTest.cpp ../ClipLogic.cpp -o ClipTest.exe -lgdiplus -lole32 -lcrypt32 -luser32 -lshell32 -ladvapi32 -lgdi32 -static
#include "../ClipLogic.h"

#include <algorithm>
#include <cstdio>
#include <string>

using std::max;
using std::min;
#include <objbase.h>
#include <propidl.h>
#include <gdiplus.h>

using namespace clip;

static int g_fail = 0, g_pass = 0;
#define CHECK(cond) do { if (cond) ++g_pass; else { ++g_fail; std::printf("FEHLER Zeile %d: %s\n", __LINE__, #cond); } } while (0)

static bool OpenRetry() { for (int i = 0; i < 80; ++i) { if (OpenClipboard(nullptr)) return true; Sleep(25); } return false; }
// Text ohne Sperrmarkierung in die Zwischenablage legen (wie ein normales Programm)
static void SetPlain(const std::wstring& t) {
    OpenRetry(); EmptyClipboard();
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, (t.size() + 1) * sizeof(wchar_t));
    memcpy(GlobalLock(h), t.c_str(), (t.size() + 1) * sizeof(wchar_t)); GlobalUnlock(h);
    SetClipboardData(CF_UNICODETEXT, h);
    CloseClipboard();
}
static void SetWithFormat(const std::wstring& t, const wchar_t* fmtName, bool dword, DWORD value) {
    OpenRetry(); EmptyClipboard();
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, (t.size() + 1) * sizeof(wchar_t));
    memcpy(GlobalLock(h), t.c_str(), (t.size() + 1) * sizeof(wchar_t)); GlobalUnlock(h);
    SetClipboardData(CF_UNICODETEXT, h);
    HGLOBAL f = GlobalAlloc(GMEM_MOVEABLE, dword ? sizeof(DWORD) : 1);
    if (dword) *static_cast<DWORD*>(GlobalLock(f)) = value; else *static_cast<char*>(GlobalLock(f)) = 0;
    GlobalUnlock(f);
    SetClipboardData(RegisterClipboardFormatW(fmtName), f);
    CloseClipboard();
}
// Lesen mit Wiederholung: ein anderes Programm (z. B. ein laufendes Clip) kann die Zwischenablage kurz belegen
static bool ReadRetry(std::wstring& out) {
    for (int i = 0; i < 40; ++i) { if (ReadClipboardText(out)) return true; Sleep(50); }
    return false;
}

int main() {
    wchar_t tmp[MAX_PATH]; GetTempPathW(MAX_PATH, tmp);
    const std::wstring dir = std::wstring(tmp) + L"ClipTest_" + std::to_wstring(GetCurrentProcessId());
    SetDataDirForTest(dir);

    // ---- Verlauf ----
    Store s;
    CHECK(AddHistory(s, L"eins", 1000));
    CHECK(AddHistory(s, L"zwei", 2000));
    CHECK(!AddHistory(s, L"zwei", 2500));                 // schon vorn
    CHECK(AddHistory(s, L"eins", 3000));                  // nach vorn geholt
    CHECK(s.history.size() == 2 && s.history[0].text == L"eins" && s.history[1].text == L"zwei");
    CHECK(!AddHistory(s, L"   \r\n\t ", 1));              // leer
    CHECK(!AddHistory(s, std::wstring(kMaxTextLen + 1, L'x'), 1));
    CHECK(AddHistory(s, std::wstring(kMaxTextLen, L'x'), 4000));
    for (int i = 0; i < 80; ++i) AddHistory(s, L"Eintrag " + std::to_wstring(i), 5000 + static_cast<unsigned>(i));
    CHECK(s.history.size() == kMaxHistory);
    CHECK(s.history[0].text == L"Eintrag 79" && s.history[kMaxHistory - 1].text == L"Eintrag 30");
    CHECK(RemoveHistory(s, 0) && s.history[0].text == L"Eintrag 78");
    CHECK(!RemoveHistory(s, 999));
    {   // Aufbewahrung
        Store r;
        AddHistory(r, L"alt", 1000); AddHistory(r, L"mittel", 4000); AddHistory(r, L"neu", 5800);
        CHECK(PurgeHistory(r, 0, 9000) == 0 && r.history.size() == 3);               // 0 = nie loeschen
        CHECK(PurgeHistory(r, 3000, 6000) == 1 && r.history.size() == 2);            // "alt" (Alter 5000) fliegt raus
        CHECK(r.history[0].text == L"neu" && r.history[1].text == L"mittel");
        CHECK(PurgeHistory(r, 1000, 8000) == 2 && r.history.empty());
        CHECK(RetentionMs(0) == 0 && RetentionMs(1) == 3600000ULL && RetentionMs(2) == 86400000ULL);
        CHECK(std::wstring(RetentionName(0)) == L"nie" && std::wstring(RetentionName(2)) == L"1 Tag");
    }
    {   // Bilder (hier mit Platzhalter-Bytes; echte PNGs weiter unten)
        Store r;
        AddHistory(r, L"Text", 1);
        CHECK(AddImage(r, "aaa", 10, 20, 2) && r.history[0].IsImage() && r.history[0].text == L"Bild 10 × 20");
        CHECK(!AddImage(r, "aaa", 10, 20, 3));            // schon vorn
        CHECK(AddImage(r, "bbb", 1, 1, 4) && AddImage(r, "aaa", 10, 20, 5) && r.history[0].png == "aaa" && r.history[1].png == "bbb");
        CHECK(!AddImage(r, "", 1, 1, 6));
        CHECK(!AddImage(r, std::string(kMaxImageBytes + 1, 'x'), 1, 1, 6));
        for (int i = 0; i < 15; ++i) AddImage(r, "bild" + std::to_string(i), 1, 1, 10 + static_cast<unsigned>(i));
        size_t images = 0, texts = 0;
        for (const HistoryItem& h : r.history) { if (h.IsImage()) ++images; else ++texts; }
        CHECK(images == kMaxImages && texts == 1);        // Bilder begrenzt, Text bleibt
        CHECK(r.history[0].png == "bild14");
    }

    // ---- Prompts, Platzhalter, Filter, Vorschau ----
    CHECK(MakePrompt(L"\r\n  Fasse zusammen:  \nText hier").title == L"Fasse zusammen:");
    CHECK(MakePrompt(L"").title == L"Prompt");
    CHECK(MakePrompt(std::wstring(100, L'a')).title.size() == 40);
    CHECK(Expand(L"Fasse zusammen: {clip}", L"ABC") == L"Fasse zusammen: ABC");
    CHECK(Expand(L"{CLIP} und {Clip}", L"X") == L"X und X");
    CHECK(Expand(L"ohne Platzhalter", L"X") == L"ohne Platzhalter");
    CHECK(Expand(L"a{clip}b", L"") == L"ab");
    CHECK(Expand(L"{clip}{clip}", L"1") == L"11");
    {
        SYSTEMTIME t{}; t.wYear = 2026; t.wMonth = 11; t.wDay = 5; t.wHour = 9; t.wMinute = 7; t.wDayOfWeek = 4;
        CHECK(Expand(L"{datum} {zeit} {wochentag}", L"", &t) == L"05.11.2026 09:07 Donnerstag");
        CHECK(Expand(L"{DATUM} und {foo} und {clip", L"x", &t) == L"05.11.2026 und {foo} und {clip");   // Unbekanntes bleibt
        CHECK(Expand(L"Am {datum}: {clip}", L"Text", &t) == L"Am 05.11.2026: Text");
        CHECK(!Expand(L"{datum}", L"").empty());                                                         // aktuelle Zeit
    }
    CHECK(Matches(L"Hallo Welt", L"") && Matches(L"Hallo Welt", L"welt hal") && !Matches(L"Hallo Welt", L"welt xyz"));
    CHECK(Matches(L"ÄÖÜ Straße", L"äöü"));
    CHECK(Preview(L"a\r\n\tb   c", 100) == L"a b c");
    CHECK(Preview(L"abcdefghij", 5) == L"abcd…");
    CHECK(Preview(L"   ", 5).empty());
    {   // Reihenfolge nach Benutzung und Ordner
        std::vector<Prompt> ps = { { L"a", L"x", L"", 0 }, { L"b", L"y", L"Mail", 50 }, { L"c", L"z", L"Code", 10 }, { L"d", L"w", L"Mail", 0 } };
        const std::vector<size_t> o = PromptOrderByRecent(ps);
        CHECK(o.size() == 4 && o[0] == 1 && o[1] == 2 && o[2] == 0 && o[3] == 3);       // zuletzt benutzte zuerst, sonst gespeicherte Reihenfolge
        const std::vector<std::wstring> f = FolderNames(ps);
        CHECK(f.size() == 2 && f[0] == L"Mail" && f[1] == L"Code");
        CHECK(NowSeconds() > 13000000000ULL);                                           // Windows-Zeit in Sekunden (> Jahr 2012)
    }

    // ---- Export / Import ----
    {
        std::vector<Prompt> ps = {
            { L"Zusammenfassen", L"Fasse zusammen:\r\n\r\n{clip}", L"KI", 0 },
            { L"Mit | Strich", L"Zeile 1\r\n=== Das sieht wie eine Kopfzeile aus ===\r\nZeile 3", L"", 0 },
            { L"Übersetzen äöü", L"Übersetze: {clip}", L"Mail & Co", 0 },
        };
        const std::wstring txt = ExportPrompts(ps);
        CHECK(txt.find(L"=== Zusammenfassen | KI ===") != std::wstring::npos);
        std::vector<Prompt> back = ImportPrompts(txt);
        CHECK(back.size() == 3);
        if (back.size() == 3) {
            for (size_t i = 0; i < 3; ++i) CHECK(back[i].title == ps[i].title && back[i].text == ps[i].text && back[i].folder == ps[i].folder);
        }
        CHECK(MergePrompts(ps, back) == 0 && ps.size() == 3);                           // nichts doppelt
        std::vector<Prompt> extra = { { L"Neu", L"Ein neuer Text", L"", 0 }, { L"Leer", L"   ", L"", 0 } };
        CHECK(MergePrompts(ps, extra) == 1 && ps.size() == 4);                          // leerer Text wird nicht uebernommen
        // von Hand geschriebene Datei: Kommentare, LF statt CRLF, Leerzeilen, Prompt ohne Text
        const std::wstring hand = L"# mein Kommentar\n\n=== Eins | Ordner A ===\nErste Zeile\nZweite Zeile\n\n\n=== Leer | ===\n\n=== Zwei ===\nNur Text\n";
        std::vector<Prompt> h = ImportPrompts(hand);
        CHECK(h.size() == 2 && h[0].title == L"Eins" && h[0].folder == L"Ordner A" && h[0].text == L"Erste Zeile\r\nZweite Zeile");
        CHECK(h.size() == 2 && h[1].title == L"Zwei" && h[1].folder.empty() && h[1].text == L"Nur Text");
        CHECK(ImportPrompts(L"kein gueltiges Format\nnur Text").empty());
        const std::wstring file = dir + L"\\export.txt";
        std::wstring readBack;
        CHECK(WriteTextFile(file, txt) && ReadTextFile(file, readBack) && readBack == txt);
    }

    // ---- Sperrregeln ----
    CHECK(IsPasswordManagerExe(L"keepassxc.exe") && IsPasswordManagerExe(L"duncansvault.exe") && !IsPasswordManagerExe(L"notepad.exe"));
    CHECK(!ShouldSkip(L"normal", false, L"notepad.exe"));
    CHECK(ShouldSkip(L"geheim", true, L"notepad.exe"));
    CHECK(ShouldSkip(L"geheim", false, L"bitwarden.exe"));
    CHECK(ShouldSkip(L"  ", false, L""));
    CHECK(ShouldSkip(std::wstring(kMaxTextLen + 1, L'a'), false, L""));
    // Vom Nutzer gesperrte Programme
    {
        CHECK(NormalizeExeName(L"C:\\Programme\\Google\\Chrome.EXE") == L"chrome.exe" && NormalizeExeName(L" Notepad ") == L"notepad.exe");
        CHECK(NormalizeExeName(L"").empty() && NormalizeExeName(L"a;b").empty() && NormalizeExeName(L"x*.exe").empty());
        std::vector<std::wstring> ex;
        CHECK(AddExcludedExe(ex, L"Chrome") && AddExcludedExe(ex, L"C:\\x\\bank.exe"));
        CHECK(!AddExcludedExe(ex, L"CHROME.exe") && ex.size() == 2);                       // keine Doppelten
        CHECK(IsExcludedExe(ex, L"chrome.exe") && !IsExcludedExe(ex, L"notepad.exe") && !IsExcludedExe(ex, L""));
        CHECK(JoinExcluded(ex) == L"chrome.exe;bank.exe");
        CHECK(SplitExcluded(L"a.exe;;B;a.EXE; ").size() == 2 && SplitExcluded(L"").empty());
        CHECK(SkipReasonFor(L"x", false, L"chrome.exe", &ex) == SkipReason::UserExcluded);
        CHECK(SkipReasonFor(L"x", false, L"notepad.exe", &ex) == SkipReason::None && !ShouldSkip(L"x", false, L"chrome.exe"));
        CHECK(SkipReasonFor(L"x", false, L"keepass.exe", &ex) == SkipReason::PasswordManager);
        CHECK(RemoveExcludedExe(ex, L"chrome.exe") && !RemoveExcludedExe(ex, L"chrome.exe") && ex.size() == 1);
        std::vector<std::wstring> many;
        for (int i = 0; i < 60; ++i) AddExcludedExe(many, L"p" + std::to_wstring(i));
        CHECK(many.size() == kMaxExcluded);
        // Speichern und Laden
        Settings st;
        st.excludedExes = {L"a.exe", L"b.exe"};
        CHECK(SaveSettings(st) && LoadSettings().excludedExes == st.excludedExes);
        st.excludedExes.clear();
        CHECK(SaveSettings(st) && LoadSettings().excludedExes.empty());
    }
    CHECK(SkipReasonFor(L"x", true, L"") == SkipReason::Excluded && SkipReasonFor(L"x", false, L"keepass.exe") == SkipReason::PasswordManager);
    CHECK(SkipReasonFor(L" ", false, L"") == SkipReason::Blank && SkipReasonFor(L"hallo welt", false, L"") == SkipReason::None);
    CHECK(SkipReasonFor(L"gh" L"p_abcdefghijklmnopqrstuvwxyz0123456789ABCD", false, L"") == SkipReason::Secret);
    CHECK(IsTerminalExe(L"windowsterminal.exe") && IsTerminalExe(L"cmd.exe") && IsTerminalExe(L"pwsh.exe") && !IsTerminalExe(L"notepad.exe") && !IsTerminalExe(L"chrome.exe"));

    // Geheimnisse: sollen erkannt werden
    const wchar_t* const secrets[] = {
        L"sk" L"-proj-abcdefghijklmnopqrstuvwxyz0123456789",
        L"sk" L"-ant-api03-abcdefghijklmnopqrstuvwxyz0123456789ABCDEF",
        L"Mein Key: gh" L"p_abcdefghijklmnopqrstuvwxyz0123456789ABCD",
        L"github" L"_pat_11ABCDEFG0abcdefghijklmnopqrstuvwxyz0123456789",
        L"AKI" L"AIOSFODNN7EXAMPLE",
        L"h" L"f_abcdefghijklmnopqrstuvwxyzABCDEFGH",
        L"xo" L"xb-123456789012-abcdefghijklmnopqrstuv",
        L"ey" L"JhbGciOiJIUzI1NiJ9.eyJzdWIiOiIxMjM0NTY3ODkwIn0.dozjgNryP4J3jVmNHl0w5N_XgL0n3I9PlFUP0THsR8U",
        L"-----BEGIN RSA PRIVATE KEY-----\nMIIEowIBAAKCAQEA...\n-----END RSA PRIVATE KEY-----",
        L"password=Abcdefgh12345678xyz",
        L"api_key: \"abc123def456ghi789jkl\"",
        L"4111 1111 1111 1111",
        L"Karte 4111-1111-1111-1111 bitte",
        L"5555555555554444",
        L"IBAN: DE89 3704 0044 0532 0130 00",
        L"AT61 1904 3002 3457 3201",
    };
    for (const wchar_t* t : secrets) { const bool d = !DetectSecret(t).empty(); CHECK(d); if (!d) std::printf("  nicht erkannt: %ls\n", t); }
    // Normale Texte: duerfen NICHT als Geheimnis gelten
    const wchar_t* const normal[] = {
        L"Hallo, das ist ein ganz normaler Satz mit Zahlen 12345 und dem Jahr 2026.",
        L"desk-top-icon-with-a-very-long-name-here",
        L"4111 1111 1111 1112",
        L"+49 170 1234567",
        L"1730000000000",
        L"token = getToken()",
        L"password: ******",
        L"DE89 3704 0044 0532 0130 01",
        L"const x = 1; function hello() { return 'world'; }",
        L"Die Rechnungsnummer lautet 2026-11-0005 und ist bis zum 15.12.2026 zu zahlen.",
        L"https://example.com/seite?id=12345&name=test",
        L"kurz",
    };
    for (const wchar_t* t : normal) { const bool d = DetectSecret(t).empty(); CHECK(d); if (!d) std::printf("  faelschlich erkannt: %ls (%ls)\n", t, DetectSecret(t).c_str()); }

    // ---- Verschluesselung und Speichern ----
    std::string c, p;
    CHECK(Protect("Geheimnis äöü", c) && c != "Geheimnis äöü" && Unprotect(c, p) && p == "Geheimnis äöü");
    CHECK(!Unprotect("kein gueltiger Block", p));
    CHECK(LoadPrompts().empty() && LoadHistory().empty());           // nichts gespeichert
    std::vector<Prompt> prompts = { { L"Zusammenfassen", L"Fasse zusammen:\r\n{clip}", L"KI", 1234567ULL }, { L"Übersetzen ö", L"Übersetze ins Englische: {clip}", L"", 0 }, { L"Leer", L"", L"X", 5 } };
    CHECK(SavePrompts(prompts));
    std::vector<Prompt> back = LoadPrompts();
    CHECK(back.size() == 3 && back[0].title == prompts[0].title && back[0].text == prompts[0].text && back[0].folder == L"KI" && back[0].lastUsed == 1234567ULL);
    CHECK(back.size() == 3 && back[1].text == prompts[1].text && back[1].folder.empty() && back[2].text.empty() && back[2].folder == L"X");
    prompts.push_back({ L"Sonder\x1f" L"zeichen", L"a\x1e" L"b", L"", 0 });   // Trennzeichen werden entschaerft, nichts zerbricht
    CHECK(SavePrompts(prompts) && LoadPrompts().size() == 4);
    {   // Datei im Format von Clip 0.1.0 (nur Titel und Text) bleibt lesbar
        std::string blob = std::string("DCLP1\n") + "Alt-Titel" + "\x1f" + "Alt-Text" + "\x1e" + "Zweiter" + "\x1f" + "Text 2", cipher;
        CHECK(Protect(blob, cipher));
        HANDLE h = CreateFileW((dir + L"\\prompts.dat").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        DWORD w; WriteFile(h, cipher.data(), static_cast<DWORD>(cipher.size()), &w, nullptr); CloseHandle(h);
        std::vector<Prompt> old = LoadPrompts();
        CHECK(old.size() == 2 && old[0].title == L"Alt-Titel" && old[0].text == L"Alt-Text" && old[0].folder.empty() && old[0].lastUsed == 0 && old[1].text == L"Text 2");
    }
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
    Settings st; st.saveHistory = true; st.hotkey = 2; st.retention = 1; st.captureSelection = false; st.hideIntro = true;
    CHECK(SaveSettings(st));
    Settings st2 = LoadSettings();
    CHECK(st2.saveHistory && st2.hotkey == 2 && st2.retention == 1 && !st2.captureSelection && st2.hideIntro);
    CHECK(HotkeyPreset(-5).vk == 'V' && HotkeyPreset(99).vk == 'C' && HotkeyCount() == 3);
    CHECK(LoadSettings().captureSelection == false);

    // ---- Zwischenablage (alten Text sichern und wiederherstellen) ----
    std::wstring original; const bool hadText = ReadClipboardText(original);
    CHECK(WriteClipboardText(L"Testtext äöü\r\nzweite Zeile"));
    std::wstring got; CHECK(ReadRetry(got) && got == L"Testtext äöü\r\nzweite Zeile");
    CHECK(ClipboardIsExcluded());                                     // eigene Schreibvorgaenge sind markiert
    SetPlain(L"normal kopiert");
    CHECK(!ClipboardIsExcluded() && ReadRetry(got) && got == L"normal kopiert");
    SetWithFormat(L"geheim", L"ExcludeClipboardContentFromMonitorProcessing", false, 0);
    CHECK(ClipboardIsExcluded());
    SetWithFormat(L"geheim2", L"CanIncludeInClipboardHistory", true, 0);
    CHECK(ClipboardIsExcluded());
    SetWithFormat(L"offen", L"CanIncludeInClipboardHistory", true, 1);
    CHECK(!ClipboardIsExcluded());
    (void)ClipboardOwnerExe();                                        // darf nicht abstuerzen (kein Besitzer bei Fremdschreibern)

    // ---- Bilder in der Zwischenablage ----
    {
        Gdiplus::GdiplusStartupInput gin; ULONG_PTR tok = 0;
        Gdiplus::GdiplusStartup(&tok, &gin, nullptr);
        HBITMAP hbm = nullptr;
        {
            Gdiplus::Bitmap bmp(64, 48, PixelFormat32bppRGB);
            Gdiplus::Graphics g(&bmp);
            g.Clear(Gdiplus::Color(255, 200, 30, 30));
            bmp.GetHBITMAP(Gdiplus::Color(255, 0, 0, 0), &hbm);
        }
        OpenRetry(); EmptyClipboard(); SetClipboardData(CF_BITMAP, hbm); CloseClipboard();
        CHECK(ClipboardHasImage());
        std::string png; int w = 0, h = 0;
        CHECK(ReadClipboardImage(png, w, h) && w == 64 && h == 48 && png.size() > 8 && static_cast<unsigned char>(png[0]) == 0x89 && png.substr(1, 3) == "PNG");
        Store is;
        CHECK(AddImage(is, png, w, h, 1) && is.history[0].text == L"Bild 64 × 48");
        int tw = 0, th = 0;
        HBITMAP thumb = MakeThumbnail(png, 32, 32, tw, th);
        CHECK(thumb != nullptr && tw == 32 && th == 24);                // Seitenverhaeltnis bleibt
        if (thumb) DeleteObject(thumb);
        int bw = 0, bh = 0;
        HBITMAP big = MakeThumbnail(png, 500, 500, bw, bh);
        CHECK(big != nullptr && bw == 64 && bh == 48);                  // nie hochskaliert
        if (big) DeleteObject(big);
        CHECK(MakeThumbnail("kein PNG", 32, 32, tw, th) == nullptr);
        // Zurueck in die Zwischenablage und erneut lesen
        CHECK(WriteClipboardImage(png));
        CHECK(ClipboardHasImage() && ClipboardIsExcluded());
        std::string png2; int w2 = 0, h2 = 0;
        CHECK(ReadClipboardImage(png2, w2, h2) && w2 == 64 && h2 == 48);
        CHECK(!WriteClipboardImage("kein PNG"));
        Gdiplus::GdiplusShutdown(tok);
    }
    if (hadText) SetPlain(original); else { OpenClipboard(nullptr); EmptyClipboard(); CloseClipboard(); }

    // ---- Autostart (nur, wenn er vorher aus war; danach wieder aus) ----
    if (!GetAutostart()) {
        CHECK(SetAutostart(true, L"C:\\Test\\DuncansClip.exe") && GetAutostart());
        CHECK(SetAutostart(false, L"") && !GetAutostart());
    } else std::printf("Hinweis: Autostart war schon an, Test uebersprungen\n");

    std::printf("%d bestanden, %d fehlgeschlagen\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
