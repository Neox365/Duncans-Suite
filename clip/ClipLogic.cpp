#include "ClipLogic.h"

#include <objidl.h>
#include <shlobj.h>
#include <wincrypt.h>

#include <algorithm>
#include <iterator>

#ifdef _MSC_VER
#pragma comment(lib, "gdiplus.lib")
#endif
// gdiplus.h braucht min/max aus <algorithm>
using std::max;
using std::min;
#include <objbase.h>
#include <propidl.h>
#include <gdiplus.h>

namespace clip {

namespace {
    std::wstring g_dataDir;
    ULONG_PTR g_gdiToken = 0;

    void EnsureGdiplus() {
        if (g_gdiToken) return;
        Gdiplus::GdiplusStartupInput in;
        Gdiplus::GdiplusStartup(&g_gdiToken, &in, nullptr);
    }

    std::wstring LowerCopy(const std::wstring& s) {
        std::wstring r = s;
        if (!r.empty()) CharLowerBuffW(&r[0], static_cast<DWORD>(r.size()));
        return r;
    }

    bool IsBlank(const std::wstring& s) {
        return std::all_of(s.begin(), s.end(), [](wchar_t c) { return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n'; });
    }

    // Trennzeichen der Speicherdatei duerfen im Text nicht vorkommen
    std::wstring Sanitize(std::wstring s) {
        for (wchar_t& c : s) if (c == 0x1E || c == 0x1F) c = L' ';
        return s;
    }

    std::string ToUtf8(const std::wstring& w) {
        if (w.empty()) return {};
        const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
        std::string r(static_cast<size_t>(n), '\0');
        WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), &r[0], n, nullptr, nullptr);
        return r;
    }

    std::wstring FromUtf8(const std::string& s) {
        if (s.empty()) return {};
        const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
        std::wstring r(static_cast<size_t>(n), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), &r[0], n);
        return r;
    }

    std::wstring Path(const wchar_t* name) { return DataDir() + L"\\" + name; }

    bool WriteFileBytes(const std::wstring& path, const std::string& data) {
        const std::wstring tmp = path + L".tmp";
        HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return false;
        DWORD w = 0;
        const bool ok = WriteFile(h, data.data(), static_cast<DWORD>(data.size()), &w, nullptr) && w == data.size();
        CloseHandle(h);
        if (!ok) { DeleteFileW(tmp.c_str()); return false; }
        return MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;   // atomar: nie eine halb geschriebene Datei
    }

    bool ReadFileBytes(const std::wstring& path, std::string& out) {
        HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return false;
        const DWORD size = GetFileSize(h, nullptr);
        if (size == INVALID_FILE_SIZE || size > (64u << 20)) { CloseHandle(h); return false; }
        out.assign(size, '\0');
        DWORD r = 0;
        const bool ok = size == 0 || (ReadFile(h, &out[0], size, &r, nullptr) && r == size);
        CloseHandle(h);
        return ok;
    }

    bool OpenClipboardRetry() {
        for (int i = 0; i < 25; ++i) {                  // bis ~0,5 s: andere Programme (z. B. weitere Zwischenablage-Manager) lesen oft gleichzeitig mit
            if (OpenClipboard(nullptr)) return true;
            Sleep(20);
        }
        return false;
    }

    const char kMagic[] = "DCLP1\n";

    // Datensaetze: Felder mit 0x1F, Saetze mit 0x1E getrennt
    std::string PackRecords(const std::vector<std::vector<std::wstring>>& recs) {
        std::wstring all;
        for (size_t i = 0; i < recs.size(); ++i) {
            if (i) all += static_cast<wchar_t>(0x1E);
            for (size_t f = 0; f < recs[i].size(); ++f) {
                if (f) all += static_cast<wchar_t>(0x1F);
                all += Sanitize(recs[i][f]);
            }
        }
        return std::string(kMagic) + ToUtf8(all);
    }

    bool UnpackRecords(const std::string& blob, std::vector<std::vector<std::wstring>>& out) {
        out.clear();
        if (blob.compare(0, sizeof(kMagic) - 1, kMagic) != 0) return false;
        const std::wstring all = FromUtf8(blob.substr(sizeof(kMagic) - 1));
        if (all.empty()) return true;
        size_t start = 0;
        while (start <= all.size()) {
            const size_t e = all.find(static_cast<wchar_t>(0x1E), start);
            const std::wstring rec = all.substr(start, e == std::wstring::npos ? std::wstring::npos : e - start);
            std::vector<std::wstring> fields;
            size_t fs = 0;
            while (fs <= rec.size()) {
                const size_t fe = rec.find(static_cast<wchar_t>(0x1F), fs);
                fields.push_back(rec.substr(fs, fe == std::wstring::npos ? std::wstring::npos : fe - fs));
                if (fe == std::wstring::npos) break;
                fs = fe + 1;
            }
            out.push_back(std::move(fields));
            if (e == std::wstring::npos) break;
            start = e + 1;
        }
        return true;
    }

    // ---- Bilder (GDI+) ----
    bool PngEncoderClsid(CLSID& clsid) {
        UINT num = 0, size = 0;
        Gdiplus::GetImageEncodersSize(&num, &size);
        if (!size) return false;
        std::vector<char> buf(size);
        auto* info = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buf.data());
        Gdiplus::GetImageEncoders(num, size, info);
        for (UINT i = 0; i < num; ++i)
            if (wcscmp(info[i].MimeType, L"image/png") == 0) { clsid = info[i].Clsid; return true; }
        return false;
    }

    bool BitmapToPng(Gdiplus::Bitmap& bmp, std::string& png) {
        CLSID clsid;
        if (!PngEncoderClsid(clsid)) return false;
        IStream* stream = nullptr;
        if (CreateStreamOnHGlobal(nullptr, TRUE, &stream) != S_OK) return false;
        bool ok = bmp.Save(stream, &clsid, nullptr) == Gdiplus::Ok;
        if (ok) {
            HGLOBAL h = nullptr;
            if (GetHGlobalFromStream(stream, &h) == S_OK) {
                const SIZE_T size = GlobalSize(h);
                STATSTG st{};
                stream->Stat(&st, STATFLAG_NONAME);
                const SIZE_T real = st.cbSize.QuadPart ? static_cast<SIZE_T>(st.cbSize.QuadPart) : size;
                if (const void* p = GlobalLock(h)) { png.assign(static_cast<const char*>(p), real); GlobalUnlock(h); }
                else ok = false;
            } else ok = false;
        }
        stream->Release();
        return ok;
    }

    // Decodiert PNG-Bytes in ein GDI+-Bitmap (die Bytes werden kopiert; das Bitmap bleibt gueltig, solange 'keep' lebt)
    struct DecodedImage {
        IStream* stream = nullptr;
        Gdiplus::Bitmap* bmp = nullptr;
        ~DecodedImage() { delete bmp; if (stream) stream->Release(); }
    };

    bool DecodePng(const std::string& png, DecodedImage& out) {
        EnsureGdiplus();
        HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, png.size());
        if (!h) return false;
        if (void* p = GlobalLock(h)) { memcpy(p, png.data(), png.size()); GlobalUnlock(h); } else { GlobalFree(h); return false; }
        if (CreateStreamOnHGlobal(h, TRUE, &out.stream) != S_OK) { GlobalFree(h); return false; }
        out.bmp = Gdiplus::Bitmap::FromStream(out.stream);
        if (!out.bmp || out.bmp->GetLastStatus() != Gdiplus::Ok) { delete out.bmp; out.bmp = nullptr; return false; }
        return true;
    }

    // Zeichen eines Schluessel-Tokens
    bool IsTokenChar(wchar_t c) { return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9') || c == L'_' || c == L'-'; }
    bool IsAlnum(wchar_t c) { return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9'); }
    bool IsDigit(wchar_t c) { return c >= L'0' && c <= L'9'; }

    bool Luhn(const std::wstring& digits) {
        int sum = 0; bool dbl = false;
        for (size_t i = digits.size(); i-- > 0;) {
            int d = digits[i] - L'0';
            if (dbl) { d *= 2; if (d > 9) d -= 9; }
            sum += d; dbl = !dbl;
        }
        return sum % 10 == 0;
    }

    bool IbanValid(const std::wstring& iban) {                 // iban: nur Grossbuchstaben und Ziffern, mod-97-Pruefung
        std::wstring r = iban.substr(4) + iban.substr(0, 4);
        int rem = 0;
        for (wchar_t c : r) {
            if (IsDigit(c)) rem = (rem * 10 + (c - L'0')) % 97;
            else if (c >= L'A' && c <= L'Z') rem = (rem * 100 + (c - L'A' + 10)) % 97;
            else return false;
        }
        return rem == 1;
    }
}

// ---- Verlauf ----
bool AddHistory(Store& s, const std::wstring& text, unsigned long long now) {
    if (IsBlank(text) || text.size() > kMaxTextLen) return false;
    for (size_t i = 0; i < s.history.size(); ++i) {
        if (s.history[i].IsImage() || s.history[i].text != text) continue;
        if (i == 0) { s.history[0].time = now; return false; }          // schon vorn
        HistoryItem it = std::move(s.history[i]);
        it.time = now;
        s.history.erase(s.history.begin() + static_cast<std::ptrdiff_t>(i));
        s.history.insert(s.history.begin(), std::move(it));             // nach vorn holen
        return true;
    }
    HistoryItem it;
    it.id = s.nextId++;
    it.text = text;
    it.time = now;
    s.history.insert(s.history.begin(), std::move(it));
    while (s.history.size() > kMaxHistory) s.history.pop_back();
    return true;
}

bool AddImage(Store& s, std::string png, int width, int height, unsigned long long now) {
    if (png.empty() || png.size() > kMaxImageBytes) return false;
    for (size_t i = 0; i < s.history.size(); ++i) {
        if (!s.history[i].IsImage() || s.history[i].png != png) continue;
        if (i == 0) { s.history[0].time = now; return false; }
        HistoryItem it = std::move(s.history[i]);
        it.time = now;
        s.history.erase(s.history.begin() + static_cast<std::ptrdiff_t>(i));
        s.history.insert(s.history.begin(), std::move(it));
        return true;
    }
    HistoryItem it;
    it.id = s.nextId++;
    it.png = std::move(png);
    it.width = width; it.height = height;
    it.text = L"Bild " + std::to_wstring(width) + L" × " + std::to_wstring(height);
    it.time = now;
    s.history.insert(s.history.begin(), std::move(it));
    size_t images = 0;
    for (const HistoryItem& h : s.history) if (h.IsImage()) ++images;
    for (size_t i = s.history.size(); images > kMaxImages && i-- > 0;)  // aelteste Bilder zuerst entfernen
        if (s.history[i].IsImage()) { s.history.erase(s.history.begin() + static_cast<std::ptrdiff_t>(i)); --images; }
    while (s.history.size() > kMaxHistory) s.history.pop_back();
    return true;
}

bool RemoveHistory(Store& s, size_t index) {
    if (index >= s.history.size()) return false;
    s.history.erase(s.history.begin() + static_cast<std::ptrdiff_t>(index));
    return true;
}

size_t PurgeHistory(Store& s, unsigned long long maxAgeMs, unsigned long long now) {
    if (maxAgeMs == 0) return 0;
    const size_t before = s.history.size();
    s.history.erase(std::remove_if(s.history.begin(), s.history.end(),
                    [&](const HistoryItem& h) { return now > h.time && now - h.time > maxAgeMs; }), s.history.end());
    return before - s.history.size();
}

// ---- Prompts ----
Prompt MakePrompt(const std::wstring& text) {
    Prompt p;
    p.text = text;
    size_t i = 0;
    while (i < text.size()) {                               // erste nicht leere Zeile
        const size_t e = text.find_first_of(L"\r\n", i);
        std::wstring line = text.substr(i, e == std::wstring::npos ? std::wstring::npos : e - i);
        const size_t a = line.find_first_not_of(L" \t");
        if (a != std::wstring::npos) {
            line = line.substr(a);
            while (!line.empty() && (line.back() == L' ' || line.back() == L'\t')) line.pop_back();
            p.title = line;
            break;
        }
        if (e == std::wstring::npos) break;
        i = e + 1;
    }
    if (p.title.size() > 40) { p.title.resize(39); p.title += L"…"; }
    if (p.title.empty()) p.title = L"Prompt";
    return p;
}

std::wstring Expand(const std::wstring& text, const std::wstring& clipText, const SYSTEMTIME* now) {
    SYSTEMTIME st{};
    if (now) st = *now; else GetLocalTime(&st);
    static const wchar_t* const kDays[7] = { L"Sonntag", L"Montag", L"Dienstag", L"Mittwoch", L"Donnerstag", L"Freitag", L"Samstag" };
    wchar_t buf[32];
    std::wstring out;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == L'{') {
            const size_t e = text.find(L'}', i + 1);
            if (e != std::wstring::npos && e - i <= 12) {
                const std::wstring key = LowerCopy(text.substr(i + 1, e - i - 1));
                bool known = true;
                if (key == L"clip") out += clipText;
                else if (key == L"datum") { swprintf(buf, 32, L"%02d.%02d.%04d", st.wDay, st.wMonth, st.wYear); out += buf; }
                else if (key == L"zeit") { swprintf(buf, 32, L"%02d:%02d", st.wHour, st.wMinute); out += buf; }
                else if (key == L"wochentag") out += kDays[st.wDayOfWeek % 7];
                else known = false;
                if (known) { i = e; continue; }
            }
        }
        out += text[i];
    }
    return out;
}

bool Matches(const std::wstring& haystack, const std::wstring& filter) {
    const std::wstring h = LowerCopy(haystack), f = LowerCopy(filter);
    size_t i = 0;
    while (i < f.size()) {
        while (i < f.size() && f[i] == L' ') ++i;
        const size_t e = f.find(L' ', i);
        const std::wstring word = f.substr(i, e == std::wstring::npos ? std::wstring::npos : e - i);
        if (!word.empty() && h.find(word) == std::wstring::npos) return false;
        if (e == std::wstring::npos) break;
        i = e + 1;
    }
    return true;
}

std::wstring Preview(const std::wstring& text, size_t maxChars) {
    std::wstring r;
    bool lastSpace = true;
    for (wchar_t c : text) {
        const bool ws = c == L' ' || c == L'\t' || c == L'\r' || c == L'\n';
        if (ws) { if (!lastSpace) r += L' '; lastSpace = true; }
        else { r += c; lastSpace = false; }
        if (r.size() > maxChars + 1) break;
    }
    while (!r.empty() && r.back() == L' ') r.pop_back();
    if (r.size() > maxChars) { r.resize(maxChars > 0 ? maxChars - 1 : 0); r += L"…"; }
    return r;
}

unsigned long long NowSeconds() {
    FILETIME ft; GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER u; u.LowPart = ft.dwLowDateTime; u.HighPart = ft.dwHighDateTime;
    return u.QuadPart / 10000000ULL;
}

std::vector<size_t> PromptOrderByRecent(const std::vector<Prompt>& prompts) {
    std::vector<size_t> idx(prompts.size());
    for (size_t i = 0; i < idx.size(); ++i) idx[i] = i;
    std::stable_sort(idx.begin(), idx.end(), [&](size_t a, size_t b) { return prompts[a].lastUsed > prompts[b].lastUsed; });
    return idx;
}

std::vector<std::wstring> FolderNames(const std::vector<Prompt>& prompts) {
    std::vector<std::wstring> r;
    for (const Prompt& p : prompts)
        if (!p.folder.empty() && std::find(r.begin(), r.end(), p.folder) == r.end()) r.push_back(p.folder);
    return r;
}

// ---- Export / Import ----
std::wstring ExportPrompts(const std::vector<Prompt>& prompts) {
    std::wstring out = L"# Duncan's Clip – Prompts\r\n# Jede Zeile \"=== Titel | Ordner ===\" beginnt einen Prompt, darunter steht sein Text.\r\n";
    for (const Prompt& p : prompts) {
        out += L"\r\n=== " + p.title + L" | " + p.folder + L" ===\r\n";
        size_t i = 0;
        while (i <= p.text.size()) {
            const size_t e = p.text.find_first_of(L"\r\n", i);
            std::wstring line = p.text.substr(i, e == std::wstring::npos ? std::wstring::npos : e - i);
            if (line.rfind(L"===", 0) == 0) line.insert(0, L" ");        // Zeilen, die wie Kopfzeilen aussehen, schuetzen
            out += line + L"\r\n";
            if (e == std::wstring::npos) break;
            i = e + 1;
            if (p.text[e] == L'\r' && i < p.text.size() && p.text[i] == L'\n') ++i;
        }
    }
    return out;
}

std::vector<Prompt> ImportPrompts(const std::wstring& fileText) {
    std::vector<Prompt> out;
    std::vector<std::wstring> lines;
    size_t i = 0;
    while (i <= fileText.size()) {
        const size_t e = fileText.find(L'\n', i);
        std::wstring line = fileText.substr(i, e == std::wstring::npos ? std::wstring::npos : e - i);
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        lines.push_back(line);
        if (e == std::wstring::npos) break;
        i = e + 1;
    }
    bool inPrompt = false;
    std::vector<std::wstring> body;
    auto flush = [&]() {
        if (!inPrompt) return;
        while (!body.empty() && body.back().empty()) body.pop_back();          // Leerzeilen vor der naechsten Kopfzeile
        std::wstring text;
        for (size_t k = 0; k < body.size(); ++k) { if (k) text += L"\r\n"; text += body[k]; }
        if (!IsBlank(text)) out.back().text = text; else out.pop_back();
        body.clear(); inPrompt = false;
    };
    for (const std::wstring& line : lines) {
        if (line.size() >= 8 && line.rfind(L"=== ", 0) == 0 && line.compare(line.size() - 4, 4, L" ===") == 0) {
            flush();
            std::wstring inner = line.substr(4, line.size() - 8);
            Prompt p;
            const size_t bar = inner.rfind(L" | ");
            if (bar != std::wstring::npos) { p.title = inner.substr(0, bar); p.folder = inner.substr(bar + 3); }
            else p.title = inner;
            if (p.title.empty()) p.title = L"Prompt";
            out.push_back(p);
            inPrompt = true;
            continue;
        }
        if (!inPrompt) continue;                                               // Kommentare vor dem ersten Prompt
        std::wstring l = line;
        if (l.rfind(L" ===", 0) == 0) l.erase(0, 1);                          // Schutz-Leerzeichen wieder entfernen
        body.push_back(l);
    }
    flush();
    return out;
}

int MergePrompts(std::vector<Prompt>& dst, const std::vector<Prompt>& src) {
    int added = 0;
    for (const Prompt& p : src) {
        if (IsBlank(p.text)) continue;
        const bool dup = std::any_of(dst.begin(), dst.end(), [&](const Prompt& d) { return d.title == p.title && d.text == p.text && d.folder == p.folder; });
        if (dup) continue;
        dst.push_back(p);
        ++added;
    }
    return added;
}

bool WriteTextFile(const std::wstring& path, const std::wstring& text) {
    return WriteFileBytes(path, std::string("\xEF\xBB\xBF") + ToUtf8(text));
}

bool ReadTextFile(const std::wstring& path, std::wstring& text) {
    std::string b;
    if (!ReadFileBytes(path, b)) return false;
    if (b.size() >= 2 && static_cast<unsigned char>(b[0]) == 0xFF && static_cast<unsigned char>(b[1]) == 0xFE) {      // UTF-16 LE
        text.assign(reinterpret_cast<const wchar_t*>(b.data() + 2), (b.size() - 2) / sizeof(wchar_t));
        return true;
    }
    if (b.size() >= 3 && static_cast<unsigned char>(b[0]) == 0xEF && static_cast<unsigned char>(b[1]) == 0xBB && static_cast<unsigned char>(b[2]) == 0xBF) b.erase(0, 3);
    text = FromUtf8(b);
    return true;
}

// ---- Sperrregeln ----
bool IsPasswordManagerExe(const std::wstring& exeLower) {
    static const wchar_t* const kList[] = {
        L"keepass.exe", L"keepassxc.exe", L"bitwarden.exe", L"1password.exe", L"lastpass.exe", L"dashlane.exe",
        L"enpass.exe", L"nordpass.exe", L"roboform.exe", L"duncansvault.exe",
    };
    for (const wchar_t* p : kList) if (exeLower == p) return true;
    return false;
}

std::wstring DetectSecret(const std::wstring& text) {
    if (text.size() < 12) return L"";
    // private Schluessel
    if (text.find(L"-----BEGIN ") != std::wstring::npos && text.find(L"PRIVATE KEY") != std::wstring::npos) return L"privater Schlüssel";

    // Tokens mit bekanntem Anfang: Anfang muss am Wortanfang stehen, danach genug Token-Zeichen
    struct Pat { const wchar_t* prefix; size_t minTail; bool upperDigitOnly; const wchar_t* kind; };
    static const Pat kPats[] = {
        { L"sk-", 20, false, L"API-Schlüssel" }, { L"sk_live_", 16, false, L"API-Schlüssel" }, { L"rk_live_", 16, false, L"API-Schlüssel" },
        { L"sk_test_", 16, false, L"API-Schlüssel" },
        { L"ghp_", 30, false, L"GitHub-Token" }, { L"gho_", 30, false, L"GitHub-Token" }, { L"ghu_", 30, false, L"GitHub-Token" },
        { L"ghs_", 30, false, L"GitHub-Token" }, { L"ghr_", 30, false, L"GitHub-Token" }, { L"github_pat_", 30, false, L"GitHub-Token" },
        { L"glpat-", 20, false, L"GitLab-Token" }, { L"hf_", 30, false, L"Hugging-Face-Token" }, { L"npm_", 30, false, L"npm-Token" },
        { L"xoxb-", 20, false, L"Slack-Token" }, { L"xoxp-", 20, false, L"Slack-Token" }, { L"xoxa-", 20, false, L"Slack-Token" },
        { L"xoxs-", 20, false, L"Slack-Token" }, { L"AIza", 35, false, L"Google-API-Schlüssel" },
        { L"AKIA", 16, true, L"AWS-Schlüssel" }, { L"ASIA", 16, true, L"AWS-Schlüssel" },
    };
    for (const Pat& p : kPats) {
        const size_t plen = wcslen(p.prefix);
        size_t pos = 0;
        while ((pos = text.find(p.prefix, pos)) != std::wstring::npos) {
            const bool boundary = pos == 0 || !IsAlnum(text[pos - 1]);
            size_t n = 0;
            if (boundary) {
                while (pos + plen + n < text.size()) {
                    const wchar_t c = text[pos + plen + n];
                    const bool ok = p.upperDigitOnly ? ((c >= L'A' && c <= L'Z') || IsDigit(c)) : IsTokenChar(c);
                    if (!ok) break;
                    ++n;
                }
                if (n >= p.minTail) return p.kind;
            }
            pos += plen;
        }
    }
    // JWT: eyJ... mit drei durch Punkte getrennten Teilen
    for (size_t pos = text.find(L"eyJ"); pos != std::wstring::npos; pos = text.find(L"eyJ", pos + 3)) {
        if (pos > 0 && IsAlnum(text[pos - 1])) continue;
        size_t j = pos; int dots = 0; size_t seg = 0, minSeg = 1000;
        while (j < text.size() && (IsTokenChar(text[j]) || text[j] == L'.')) {
            if (text[j] == L'.') { minSeg = std::min(minSeg, seg); seg = 0; ++dots; } else ++seg;
            ++j;
        }
        minSeg = std::min(minSeg, seg);
        if (dots == 2 && minSeg >= 8) return L"JWT-Token";
    }
    // Zugangsdaten wie "password=...", "api_key: ..." (Wert mindestens 16 Zeichen mit Ziffer und Buchstabe)
    {
        static const wchar_t* const kKeys[] = { L"password", L"passwort", L"passwd", L"secret", L"api_key", L"apikey", L"api-key", L"token", L"private_key", L"access_key" };
        const std::wstring low = LowerCopy(text);
        for (const wchar_t* k : kKeys) {
            const size_t klen = wcslen(k);
            for (size_t pos = low.find(k); pos != std::wstring::npos; pos = low.find(k, pos + klen)) {
                size_t j = pos + klen;
                while (j < low.size() && (low[j] == L' ' || low[j] == L'"' || low[j] == L'\'')) ++j;
                if (j >= low.size() || (low[j] != L'=' && low[j] != L':')) continue;
                ++j;
                while (j < low.size() && (low[j] == L' ' || low[j] == L'"' || low[j] == L'\'')) ++j;
                size_t n = 0; bool digit = false, alpha = false;
                while (j + n < text.size()) {
                    const wchar_t c = text[j + n];
                    if (!(IsAlnum(c) || wcschr(L"+/_-=.!@#$%^&*", c))) break;
                    if (IsDigit(c)) digit = true; else if (IsAlnum(c)) alpha = true;
                    ++n;
                }
                if (n >= 16 && digit && alpha) return L"Zugangsdaten";
            }
        }
    }
    // Kreditkartennummer: 13-19 Ziffern (auch mit Leerzeichen/Bindestrichen gruppiert), Luhn-Pruefsumme, Beginn 3-6
    for (size_t i = 0; i < text.size(); ++i) {
        if (!IsDigit(text[i]) || (i > 0 && IsDigit(text[i - 1]))) continue;
        std::wstring digits; size_t j = i;
        while (j < text.size()) {
            if (IsDigit(text[j])) { digits += text[j]; ++j; }
            else if ((text[j] == L' ' || text[j] == L'-') && j + 1 < text.size() && IsDigit(text[j + 1]) && !digits.empty()) ++j;
            else break;
        }
        if (digits.size() >= 13 && digits.size() <= 19 && digits[0] >= L'3' && digits[0] <= L'6' && Luhn(digits)) return L"Kreditkartennummer";
        i = j > i ? j - 1 : i;
    }
    // IBAN (nur Laender mit bekannter Laenge, mod-97-Pruefung)
    {
        std::wstring norm;
        for (wchar_t c : text) if (c != L' ') norm += c;
        struct Len { const wchar_t* cc; size_t len; };
        static const Len kLens[] = { { L"DE", 22 }, { L"AT", 20 }, { L"CH", 21 }, { L"FR", 27 }, { L"GB", 22 }, { L"NL", 18 }, { L"IT", 27 }, { L"ES", 24 },
                                     { L"BE", 16 }, { L"LU", 20 }, { L"PL", 28 }, { L"DK", 18 }, { L"SE", 24 }, { L"NO", 15 }, { L"FI", 18 }, { L"PT", 25 }, { L"IE", 22 }, { L"CZ", 24 } };
        for (size_t i = 0; i + 4 < norm.size(); ++i) {
            if (i > 0 && IsAlnum(norm[i - 1])) continue;
            if (!(norm[i] >= L'A' && norm[i] <= L'Z' && norm[i + 1] >= L'A' && norm[i + 1] <= L'Z' && IsDigit(norm[i + 2]) && IsDigit(norm[i + 3]))) continue;
            for (const Len& l : kLens) {
                if (norm[i] != l.cc[0] || norm[i + 1] != l.cc[1] || i + l.len > norm.size()) continue;
                const std::wstring cand = norm.substr(i, l.len);
                const bool end = i + l.len == norm.size() || !IsAlnum(norm[i + l.len]);
                const bool chars = std::all_of(cand.begin(), cand.end(), [](wchar_t c) { return (c >= L'A' && c <= L'Z') || IsDigit(c); });
                if (end && chars && IbanValid(cand)) return L"IBAN";
            }
        }
    }
    return L"";
}

SkipReason SkipReasonFor(const std::wstring& text, bool excludedByFormat, const std::wstring& ownerExeLower) {
    if (excludedByFormat) return SkipReason::Excluded;
    if (IsPasswordManagerExe(ownerExeLower)) return SkipReason::PasswordManager;
    if (IsBlank(text)) return SkipReason::Blank;
    if (text.size() > kMaxTextLen) return SkipReason::TooLong;
    if (!DetectSecret(text).empty()) return SkipReason::Secret;
    return SkipReason::None;
}

bool ShouldSkip(const std::wstring& text, bool excludedByFormat, const std::wstring& ownerExeLower) {
    return SkipReasonFor(text, excludedByFormat, ownerExeLower) != SkipReason::None;
}

// ---- Zwischenablage ----
bool ReadClipboardText(std::wstring& out) {
    out.clear();
    if (!IsClipboardFormatAvailable(CF_UNICODETEXT)) return false;
    if (!OpenClipboardRetry()) return false;
    bool ok = false;
    if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
        if (const wchar_t* p = static_cast<const wchar_t*>(GlobalLock(h))) {
            const size_t maxChars = GlobalSize(h) / sizeof(wchar_t);
            size_t n = 0;
            while (n < maxChars && p[n]) ++n;
            out.assign(p, n);
            GlobalUnlock(h);
            ok = true;
        }
    }
    CloseClipboard();
    return ok;
}

namespace {
    // Markierung "nicht in Verlauf/Cloud" (eigene Schreibvorgaenge); Zwischenablage muss geoeffnet sein
    void MarkPrivate() {
        for (const wchar_t* name : { L"CanIncludeInClipboardHistory", L"CanUploadToCloudClipboard" }) {
            if (HGLOBAL f = GlobalAlloc(GMEM_MOVEABLE, sizeof(DWORD))) {
                if (DWORD* d = static_cast<DWORD*>(GlobalLock(f))) {
                    *d = 0;
                    GlobalUnlock(f);
                    if (!SetClipboardData(RegisterClipboardFormatW(name), f)) GlobalFree(f);
                } else GlobalFree(f);
            }
        }
    }
}

bool WriteClipboardText(const std::wstring& text, bool markPrivate) {
    if (!OpenClipboardRetry()) return false;
    EmptyClipboard();
    bool ok = false;
    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    if (HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
        if (void* p = GlobalLock(h)) {
            memcpy(p, text.c_str(), bytes);
            GlobalUnlock(h);
            ok = SetClipboardData(CF_UNICODETEXT, h) != nullptr;
        }
        if (!ok) GlobalFree(h);
    }
    if (markPrivate) MarkPrivate();
    CloseClipboard();
    return ok;
}

bool ClipboardHasImage() {
    return IsClipboardFormatAvailable(CF_DIB) || IsClipboardFormatAvailable(CF_DIBV5) || IsClipboardFormatAvailable(RegisterClipboardFormatW(L"PNG"));
}

bool ReadClipboardImage(std::string& png, int& width, int& height) {
    png.clear(); width = height = 0;
    if (!ClipboardHasImage()) return false;
    EnsureGdiplus();
    if (!OpenClipboardRetry()) return false;
    bool ok = false;
    const UINT pngFmt = RegisterClipboardFormatW(L"PNG");
    if (IsClipboardFormatAvailable(pngFmt)) {                       // viele Programme (Browser, Snipping Tool) liefern fertiges PNG
        if (HANDLE h = GetClipboardData(pngFmt)) {
            const SIZE_T size = GlobalSize(h);
            if (size > 0 && size <= kMaxImageBytes)
                if (const void* p = GlobalLock(h)) {
                    png.assign(static_cast<const char*>(p), size);
                    GlobalUnlock(h);
                    DecodedImage d;
                    if (DecodePng(png, d)) { width = static_cast<int>(d.bmp->GetWidth()); height = static_cast<int>(d.bmp->GetHeight()); ok = true; }
                }
        }
    }
    if (!ok) {                                                      // sonst aus dem DIB
        const UINT fmt = IsClipboardFormatAvailable(CF_DIBV5) ? CF_DIBV5 : CF_DIB;
        if (HANDLE h = GetClipboardData(fmt)) {
            if (const BYTE* p = static_cast<const BYTE*>(GlobalLock(h))) {
                const auto* hdr = reinterpret_cast<const BITMAPINFOHEADER*>(p);
                size_t offset = hdr->biSize;
                if (hdr->biSize == sizeof(BITMAPINFOHEADER) && hdr->biCompression == BI_BITFIELDS) offset += 12;
                if (hdr->biBitCount <= 8) offset += (hdr->biClrUsed ? hdr->biClrUsed : (1u << hdr->biBitCount)) * sizeof(RGBQUAD);
                else offset += hdr->biClrUsed * sizeof(RGBQUAD);
                if (offset < GlobalSize(h) && hdr->biWidth > 0 && hdr->biHeight != 0) {
                    Gdiplus::Bitmap bmp(reinterpret_cast<const BITMAPINFO*>(p), const_cast<BYTE*>(p) + offset);
                    if (bmp.GetLastStatus() == Gdiplus::Ok) {
                        width = static_cast<int>(bmp.GetWidth()); height = static_cast<int>(bmp.GetHeight());
                        ok = BitmapToPng(bmp, png);
                    }
                }
                GlobalUnlock(h);
            }
        }
    }
    CloseClipboard();
    if (ok && (png.empty() || png.size() > kMaxImageBytes)) ok = false;
    if (!ok) { png.clear(); width = height = 0; }
    return ok;
}

bool WriteClipboardImage(const std::string& png, bool markPrivate) {
    DecodedImage d;
    if (!DecodePng(png, d)) return false;
    HBITMAP hbm = nullptr;
    if (d.bmp->GetHBITMAP(Gdiplus::Color(255, 255, 255, 255), &hbm) != Gdiplus::Ok || !hbm) return false;
    if (!OpenClipboardRetry()) { DeleteObject(hbm); return false; }
    EmptyClipboard();
    bool ok = SetClipboardData(CF_BITMAP, hbm) != nullptr;
    if (!ok) DeleteObject(hbm);
    if (ok) {                                                       // zusaetzlich das verlustfreie PNG anbieten (Programme mit Transparenz-Unterstuetzung)
        if (HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, png.size())) {
            if (void* p = GlobalLock(h)) { memcpy(p, png.data(), png.size()); GlobalUnlock(h); if (!SetClipboardData(RegisterClipboardFormatW(L"PNG"), h)) GlobalFree(h); }
            else GlobalFree(h);
        }
        if (markPrivate) MarkPrivate();
    }
    CloseClipboard();
    return ok;
}

HBITMAP MakeThumbnail(const std::string& png, int maxW, int maxH, int& outW, int& outH) {
    outW = outH = 0;
    DecodedImage d;
    if (!DecodePng(png, d) || maxW <= 0 || maxH <= 0) return nullptr;
    const double sw = static_cast<double>(d.bmp->GetWidth()), sh = static_cast<double>(d.bmp->GetHeight());
    if (sw <= 0 || sh <= 0) return nullptr;
    const double scale = std::min({ maxW / sw, maxH / sh, 1.0 });
    outW = std::max(1, static_cast<int>(sw * scale)); outH = std::max(1, static_cast<int>(sh * scale));
    Gdiplus::Bitmap thumb(outW, outH, PixelFormat32bppRGB);
    {
        Gdiplus::Graphics g(&thumb);
        g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        g.Clear(Gdiplus::Color(255, 30, 33, 41));
        g.DrawImage(d.bmp, 0, 0, outW, outH);
    }
    HBITMAP hbm = nullptr;
    if (thumb.GetHBITMAP(Gdiplus::Color(255, 30, 33, 41), &hbm) != Gdiplus::Ok) return nullptr;
    return hbm;
}

bool ClipboardIsExcluded() {
    if (IsClipboardFormatAvailable(RegisterClipboardFormatW(L"ExcludeClipboardContentFromMonitorProcessing"))) return true;
    bool excluded = false;
    for (const wchar_t* name : { L"CanIncludeInClipboardHistory", L"CanUploadToCloudClipboard" }) {
        const UINT fmt = RegisterClipboardFormatW(name);
        if (!IsClipboardFormatAvailable(fmt)) continue;
        // Die Daten der Markierung sind unmittelbar nach dem Setzen manchmal noch nicht lesbar (Groesse 0). Kurz warten; bleibt sie
        // unlesbar, gilt der Eintrag im Zweifel als ausgeschlossen (lieber einen Eintrag zu wenig speichern als einen vertraulichen).
        bool known = false;
        for (int attempt = 0; attempt < 8 && !known; ++attempt) {
            if (attempt) Sleep(25);
            if (!OpenClipboardRetry()) continue;
            if (HANDLE h = GetClipboardData(fmt)) {
                if (GlobalSize(h) >= sizeof(DWORD))
                    if (const DWORD* d = static_cast<const DWORD*>(GlobalLock(h))) { known = true; if (*d == 0) excluded = true; GlobalUnlock(h); }
            }
            CloseClipboard();
        }
        if (!known) excluded = true;
    }
    return excluded;
}

namespace {
    std::wstring ExeNameOfPid(DWORD pid) {
        if (!pid) return L"";
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!h) return L"";
        wchar_t buf[MAX_PATH * 2] = {};
        DWORD n = static_cast<DWORD>(std::size(buf));
        std::wstring r;
        if (QueryFullProcessImageNameW(h, 0, buf, &n)) {
            r.assign(buf, n);
            const size_t slash = r.find_last_of(L'\\');
            if (slash != std::wstring::npos) r = r.substr(slash + 1);
        }
        CloseHandle(h);
        return LowerCopy(r);
    }
}

std::wstring ClipboardOwnerExe() {
    HWND owner = GetClipboardOwner();
    if (!owner) return L"";
    DWORD pid = 0;
    GetWindowThreadProcessId(owner, &pid);
    return ExeNameOfPid(pid);
}

void SendPasteKeys() {
    INPUT in[4] = {};
    for (INPUT& i : in) i.type = INPUT_KEYBOARD;
    in[0].ki.wVk = VK_CONTROL;
    in[1].ki.wVk = 'V';
    in[2].ki.wVk = 'V';  in[2].ki.dwFlags = KEYEVENTF_KEYUP;
    in[3].ki.wVk = VK_CONTROL; in[3].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(4, in, sizeof(INPUT));
}

// ---- Markierung uebernehmen ----
bool IsTerminalExe(const std::wstring& exeLower) {
    static const wchar_t* const kList[] = {
        L"windowsterminal.exe", L"wt.exe", L"cmd.exe", L"powershell.exe", L"pwsh.exe", L"conhost.exe", L"openconsole.exe", L"mintty.exe",
        L"alacritty.exe", L"wezterm-gui.exe", L"putty.exe", L"kitty.exe", L"bash.exe", L"wsl.exe", L"wslhost.exe", L"tabby.exe",
        L"hyper.exe", L"conemu64.exe", L"conemu.exe", L"fluent-terminal.exe",
    };
    for (const wchar_t* p : kList) if (exeLower == p) return true;
    return false;
}

bool IsTerminalWindow(HWND h) {
    if (!h) return false;
    wchar_t cls[128] = {};
    GetClassNameW(h, cls, 128);
    const std::wstring c = cls;
    if (c == L"ConsoleWindowClass" || c == L"CASCADIA_HOSTING_WINDOW_CLASS" || c == L"PseudoConsoleWindow" || c == L"mintty" || c == L"VirtualConsoleClass") return true;
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    return IsTerminalExe(ExeNameOfPid(pid));
}

bool CaptureSelection(HWND target, std::wstring& selection) {
    selection.clear();
    if (!target || IsTerminalWindow(target)) return false;
    auto down = [](int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; };
    int waited = 0;
    while ((down(VK_CONTROL) || down(VK_MENU) || down(VK_SHIFT) || down(VK_LWIN) || down(VK_RWIN)) && waited < 600) { Sleep(10); waited += 10; }
    if (waited >= 600) return false;                                // Tasten bleiben gedrueckt: nicht eingreifen
    bool hadText = false;
    std::wstring prev;
    if (IsClipboardFormatAvailable(CF_UNICODETEXT)) { hadText = ReadClipboardText(prev); if (!hadText) return false; }
    else if (CountClipboardFormats() > 0) return false;             // Bild/Dateien in der Zwischenablage: nicht anfassen (liessen sich nicht zurueckgeben)
    const DWORD seq = GetClipboardSequenceNumber();
    INPUT in[4] = {};
    for (INPUT& i : in) i.type = INPUT_KEYBOARD;
    in[0].ki.wVk = VK_CONTROL;
    in[1].ki.wVk = 'C';
    in[2].ki.wVk = 'C';  in[2].ki.dwFlags = KEYEVENTF_KEYUP;
    in[3].ki.wVk = VK_CONTROL; in[3].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(4, in, sizeof(INPUT));
    for (int i = 0; i < 50 && GetClipboardSequenceNumber() == seq; ++i) Sleep(10);
    if (GetClipboardSequenceNumber() == seq) return false;          // nichts markiert (oder das Programm kopiert nicht)
    Sleep(30);
    std::wstring got;
    const bool ok = ReadClipboardText(got) && !IsBlank(got);
    if (hadText) WriteClipboardText(prev, false);                   // alte Zwischenablage zurueckgeben
    else if (OpenClipboardRetry()) { EmptyClipboard(); CloseClipboard(); }
    if (ok) selection = got;
    return ok;
}

// ---- Einstellungen und Speicherung ----
int HotkeyCount() { return 3; }

const Hotkey& HotkeyPreset(int index) {
    static const Hotkey kKeys[3] = {
        { MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'V', L"Strg+Alt+V" },
        { MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, 'V', L"Strg+Umschalt+V" },
        { MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'C', L"Strg+Alt+C" },
    };
    return kKeys[std::clamp(index, 0, 2)];
}

unsigned long long RetentionMs(int retention) {
    return retention == 1 ? 3600000ULL : retention == 2 ? 86400000ULL : 0ULL;
}

const wchar_t* RetentionName(int retention) {
    return retention == 1 ? L"1 Stunde" : retention == 2 ? L"1 Tag" : L"nie";
}

std::wstring DataDir() {
    if (!g_dataDir.empty()) return g_dataDir;
    wchar_t local[MAX_PATH] = {};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, local))) return L".";
    g_dataDir = std::wstring(local) + L"\\DuncansClip";
    CreateDirectoryW(g_dataDir.c_str(), nullptr);
    return g_dataDir;
}

void SetDataDirForTest(const std::wstring& dir) { g_dataDir = dir; CreateDirectoryW(dir.c_str(), nullptr); }

Settings LoadSettings() {
    Settings s;
    const std::wstring f = Path(L"settings.ini");
    s.saveHistory = GetPrivateProfileIntW(L"Settings", L"SaveHistory", 0, f.c_str()) != 0;
    s.hotkey = std::clamp(static_cast<int>(GetPrivateProfileIntW(L"Settings", L"Hotkey", 0, f.c_str())), 0, HotkeyCount() - 1);
    s.retention = std::clamp(static_cast<int>(GetPrivateProfileIntW(L"Settings", L"Retention", 0, f.c_str())), 0, 2);
    s.captureSelection = GetPrivateProfileIntW(L"Settings", L"CaptureSelection", 1, f.c_str()) != 0;
    s.hideIntro = GetPrivateProfileIntW(L"Settings", L"HideIntro", 0, f.c_str()) != 0;
    s.autostart = GetAutostart();
    return s;
}

bool SaveSettings(const Settings& s) {
    const std::wstring f = Path(L"settings.ini");
    bool ok = WritePrivateProfileStringW(L"Settings", L"SaveHistory", s.saveHistory ? L"1" : L"0", f.c_str()) != 0;
    ok &= WritePrivateProfileStringW(L"Settings", L"Hotkey", std::to_wstring(s.hotkey).c_str(), f.c_str()) != 0;
    ok &= WritePrivateProfileStringW(L"Settings", L"Retention", std::to_wstring(s.retention).c_str(), f.c_str()) != 0;
    ok &= WritePrivateProfileStringW(L"Settings", L"CaptureSelection", s.captureSelection ? L"1" : L"0", f.c_str()) != 0;
    ok &= WritePrivateProfileStringW(L"Settings", L"HideIntro", s.hideIntro ? L"1" : L"0", f.c_str()) != 0;
    return ok;
}

bool Protect(const std::string& plain, std::string& cipher) {
    DATA_BLOB in{ static_cast<DWORD>(plain.size()), reinterpret_cast<BYTE*>(const_cast<char*>(plain.data())) }, out{};
    if (!CryptProtectData(&in, L"DuncansClip", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) return false;
    cipher.assign(reinterpret_cast<char*>(out.pbData), out.cbData);
    LocalFree(out.pbData);
    return true;
}

bool Unprotect(const std::string& cipher, std::string& plain) {
    DATA_BLOB in{ static_cast<DWORD>(cipher.size()), reinterpret_cast<BYTE*>(const_cast<char*>(cipher.data())) }, out{};
    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) return false;
    plain.assign(reinterpret_cast<char*>(out.pbData), out.cbData);
    SecureZeroMemory(out.pbData, out.cbData);
    LocalFree(out.pbData);
    return true;
}

namespace {
    bool SaveEncrypted(const wchar_t* name, const std::string& blob) {
        std::string cipher;
        return Protect(blob, cipher) && WriteFileBytes(Path(name), cipher);
    }
    bool LoadEncrypted(const wchar_t* name, std::vector<std::vector<std::wstring>>& recs) {
        std::string cipher, plain;
        if (!ReadFileBytes(Path(name), cipher) || !Unprotect(cipher, plain)) return false;
        const bool ok = UnpackRecords(plain, recs);
        SecureZeroMemory(&plain[0], plain.size());
        return ok;
    }
}

bool SavePrompts(const std::vector<Prompt>& p) {
    std::vector<std::vector<std::wstring>> recs;
    for (const Prompt& x : p) recs.push_back({ x.title, x.text, x.folder, std::to_wstring(x.lastUsed) });
    return SaveEncrypted(L"prompts.dat", PackRecords(recs));
}

std::vector<Prompt> LoadPrompts() {
    std::vector<Prompt> out;
    std::vector<std::vector<std::wstring>> recs;
    if (!LoadEncrypted(L"prompts.dat", recs)) return out;
    for (const auto& r : recs) {
        if (r.size() < 2) continue;                                // Dateien aus Clip 0.1.0 haben nur Titel und Text
        Prompt p;
        p.title = r[0]; p.text = r[1];
        if (r.size() > 2) p.folder = r[2];
        if (r.size() > 3) p.lastUsed = wcstoull(r[3].c_str(), nullptr, 10);
        out.push_back(std::move(p));
    }
    return out;
}

bool SaveHistory(const std::vector<std::wstring>& h) {
    std::vector<std::vector<std::wstring>> recs;
    for (const std::wstring& x : h) recs.push_back({ x });
    return SaveEncrypted(L"history.dat", PackRecords(recs));
}

std::vector<std::wstring> LoadHistory() {
    std::vector<std::wstring> out;
    std::vector<std::vector<std::wstring>> recs;
    if (!LoadEncrypted(L"history.dat", recs)) return out;
    for (const auto& r : recs) if (r.size() == 1 && !r[0].empty() && out.size() < kMaxHistory) out.push_back(r[0]);
    return out;
}

void DeleteHistoryFile() { DeleteFileW(Path(L"history.dat").c_str()); }

// ---- Autostart ----
namespace { const wchar_t* const kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run"; }

bool GetAutostart() {
    HKEY k = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &k) != ERROR_SUCCESS) return false;
    const bool exists = RegQueryValueExW(k, L"DuncansClip", nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
    RegCloseKey(k);
    return exists;
}

bool SetAutostart(bool on, const std::wstring& exePath) {
    HKEY k = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) != ERROR_SUCCESS) return false;
    LONG r;
    if (on) {
        const std::wstring cmd = L"\"" + exePath + L"\" --background";
        r = RegSetValueExW(k, L"DuncansClip", 0, REG_SZ, reinterpret_cast<const BYTE*>(cmd.c_str()), static_cast<DWORD>((cmd.size() + 1) * sizeof(wchar_t)));
    } else {
        r = RegDeleteValueW(k, L"DuncansClip");
        if (r == ERROR_FILE_NOT_FOUND) r = ERROR_SUCCESS;
    }
    RegCloseKey(k);
    return r == ERROR_SUCCESS;
}

}  // namespace clip
