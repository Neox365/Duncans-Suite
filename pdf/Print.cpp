#include "Print.h"

#include <commdlg.h>
#include <winspool.h>

#include <algorithm>
#include <cmath>

namespace pdfprint {

std::vector<int> PageRange(int pageCount, int from, int to) {
    std::vector<int> v;
    if (pageCount <= 0) return v;
    if (from <= 0 || to <= 0) { from = 1; to = pageCount; }
    from = std::clamp(from, 1, pageCount);
    to = std::clamp(to, 1, pageCount);
    if (to < from) std::swap(from, to);
    for (int p = from; p <= to; ++p) v.push_back(p - 1);
    return v;
}

bool PrintPages(HDC dc, const PdfDocument& doc, const std::vector<int>& pages, const wchar_t* jobName, const wchar_t* outFile, std::wstring& err) {
    err.clear();
    if (!dc || !doc.IsOpen() || pages.empty()) { err = L"Es gibt nichts zu drucken."; return false; }
    const int areaW = GetDeviceCaps(dc, HORZRES), areaH = GetDeviceCaps(dc, VERTRES);
    const int dpiX = std::max(72, GetDeviceCaps(dc, LOGPIXELSX)), dpiY = std::max(72, GetDeviceCaps(dc, LOGPIXELSY));
    if (areaW <= 0 || areaH <= 0) { err = L"Der Drucker meldet keinen bedruckbaren Bereich."; return false; }

    DOCINFOW di{};
    di.cbSize = sizeof(di);
    di.lpszDocName = jobName && *jobName ? jobName : L"Duncan's PDF";
    di.lpszOutput = outFile && *outFile ? outFile : nullptr;
    if (StartDocW(dc, &di) <= 0) { err = L"Der Druckauftrag konnte nicht gestartet werden."; return false; }

    bool ok = true;
    for (int page : pages) {
        double pw = 0, ph = 0;
        if (page < 0 || page >= doc.PageCount() || !doc.PageSize(page, pw, ph) || pw <= 0 || ph <= 0) continue;
        // Zielgroesse in Geraetepixeln: Seite in Originalgroesse, hoechstens so gross wie der bedruckbare Bereich
        double dw = pw / 72.0 * dpiX, dh = ph / 72.0 * dpiY;
        const double s = std::min(1.0, std::min(areaW / dw, areaH / dh));
        dw *= s; dh *= s;
        const int destW = std::max(1, static_cast<int>(std::lround(dw))), destH = std::max(1, static_cast<int>(std::lround(dh)));
        // Rendergroesse: wie das Ziel, aber hoechstens 300 dpi (sonst werden die Bilder riesig)
        const double rs = std::min(1.0, 300.0 / dpiX);
        const int rw = std::max(1, static_cast<int>(std::lround(destW * rs))), rh = std::max(1, static_cast<int>(std::lround(destH * rs)));
        HBITMAP bmp = doc.RenderPage(page, rw, rh);
        if (!bmp) { err = L"Eine Seite konnte nicht aufbereitet werden."; ok = false; break; }
        BITMAP bm{};
        GetObjectW(bmp, sizeof(bm), &bm);
        if (StartPage(dc) <= 0) { DeleteObject(bmp); err = L"Der Drucker hat die Seite abgelehnt."; ok = false; break; }
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = rw;
        bi.bmiHeader.biHeight = -rh;              // top-down wie die Section
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        SetStretchBltMode(dc, HALFTONE);
        SetBrushOrgEx(dc, 0, 0, nullptr);
        const int x = (areaW - destW) / 2, y = (areaH - destH) / 2;
        const int lines = bm.bmBits ? StretchDIBits(dc, x, y, destW, destH, 0, 0, rw, rh, bm.bmBits, &bi, DIB_RGB_COLORS, SRCCOPY) : GDI_ERROR;
        DeleteObject(bmp);
        EndPage(dc);
        if (lines == GDI_ERROR || lines == 0) { err = L"Eine Seite konnte nicht an den Drucker gesendet werden."; ok = false; break; }
    }
    if (ok) EndDoc(dc); else AbortDoc(dc);
    return ok;
}

bool PrintWithDialog(HWND owner, const PdfDocument& doc, const std::wstring& jobName, bool& cancelled, std::wstring& err) {
    cancelled = false;
    err.clear();
    if (!doc.IsOpen()) { err = L"Es ist keine PDF geöffnet."; return false; }
    PRINTDLGW pd{};
    pd.lStructSize = sizeof(pd);
    pd.hwndOwner = owner;
    pd.Flags = PD_RETURNDC | PD_NOSELECTION | PD_USEDEVMODECOPIESANDCOLLATE | PD_ALLPAGES;
    pd.nMinPage = 1;
    pd.nMaxPage = static_cast<WORD>(std::min(doc.PageCount(), 65535));
    pd.nFromPage = 1;
    pd.nToPage = pd.nMaxPage;
    pd.nCopies = 1;
    if (!PrintDlgW(&pd)) {
        const DWORD e = CommDlgExtendedError();
        if (e == 0) cancelled = true; else err = L"Der Druckdialog konnte nicht geöffnet werden.";
        return false;
    }
    const std::vector<int> pages = (pd.Flags & PD_PAGENUMS) ? PageRange(doc.PageCount(), pd.nFromPage, pd.nToPage) : PageRange(doc.PageCount(), 0, 0);
    bool ok = false;
    if (pd.hDC) {
        HCURSOR old = SetCursor(LoadCursor(nullptr, IDC_WAIT));
        ok = PrintPages(pd.hDC, doc, pages, jobName.c_str(), nullptr, err);
        SetCursor(old);
        DeleteDC(pd.hDC);
    } else err = L"Für den gewählten Drucker gibt es keinen Druckkontext.";
    if (pd.hDevMode) GlobalFree(pd.hDevMode);
    if (pd.hDevNames) GlobalFree(pd.hDevNames);
    return ok;
}

}  // namespace pdfprint
