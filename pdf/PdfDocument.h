#pragma once
// Duncan's PDF - duenner C++-Wrapper um PDFium.
// pdfium.dll wird zur Laufzeit per LoadLibrary geladen (siehe PdfiumApi.h); so baut das Projekt
// auch ohne PDFium-Header/-Importbibliothek, und fehlt die DLL, meldet die App das freundlich.
#include <windows.h>
#include <string>
#include <vector>

class PdfDocument {
public:
    PdfDocument() = default;
    ~PdfDocument() { Close(); }
    PdfDocument(const PdfDocument&) = delete;
    PdfDocument& operator=(const PdfDocument&) = delete;

    // Laedt pdfium.dll (einmalig). Bei Fehler steht der Grund in LastError().
    static bool InitLibrary();
    static const std::wstring& LastError();

    bool Open(const std::wstring& path, const std::string& password = {});
    void Close();
    bool IsOpen() const { return m_doc != nullptr; }
    const std::wstring& Path() const { return m_path; }
    int PageCount() const;

    // Seitengroesse in Punkt (1/72 Zoll), inkl. zusaetzlicher Drehung.
    bool PageSize(int index, double& w, double& h) const;

    // Rendert die Seite in eine 32-Bit-DIB-Section (w x h Pixel, weisser Hintergrund).
    // Der Aufrufer gibt das HBITMAP mit DeleteObject frei.
    HBITMAP RenderPage(int index, int w, int h) const;

    // Zusaetzliche Drehung (0..3 * 90 Grad) pro Seite; wird beim Speichern in die Seite geschrieben.
    int Rotation(int index) const;
    void SetRotation(int index, int quarterTurns);

    // Haengt alle Seiten von 'other' hinten an.
    bool Append(const std::wstring& otherPath);
    // Speichert eine Kopie (inkl. Drehungen und angehaengter Seiten).
    bool SaveCopy(const std::wstring& path);
    bool Modified() const { return m_modified; }

private:
    void* m_doc = nullptr;
    std::vector<char> m_data;     // PDFium liest bei FPDF_LoadMemDocument direkt aus diesem Puffer
    std::wstring m_path;
    std::vector<int> m_rot;
    bool m_modified = false;
};
