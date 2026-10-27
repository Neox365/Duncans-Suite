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
    // Loescht Seiten (Indizes in beliebiger Reihenfolge). Die letzte Seite bleibt immer erhalten (false, wenn alle gewaehlt waeren).
    bool DeletePages(std::vector<int> indices);

    // Textsuche (ohne Gross-/Kleinschreibung). Treffer = Zeichenbereich einer Seite.
    struct TextHit { int page = 0, start = 0, count = 0; };
    std::vector<TextHit> Search(const std::wstring& needle, size_t maxHits = 20000) const;
    // Rechtecke (Pixel) eines Treffers fuer die Seite, gerendert in w x h Pixeln mit der aktuellen Drehung.
    void HitRects(const TextHit& hit, int w, int h, std::vector<RECT>& out) const;
    // Verschiebt Seiten (Indizes aufsteigend) vor die Seite 'insertBefore' (0..PageCount, Index im Dokument vor dem Verschieben).
    // newOrder (optional): neue Position jeder bisherigen Seite (Index alt -> neu).
    bool MovePages(const std::vector<int>& indices, int insertBefore, std::vector<int>* newOrder = nullptr);

    // Text markieren / kommentieren. Koordinaten in Pixeln der gerenderten Seite (w x h, aktuelle Drehung).
    int CharAt(int index, int w, int h, int dx, int dy) const;                    // Zeichenindex an der Position oder -1
    bool AddHighlight(int index, int startChar, int charCount, unsigned long rgb = 0x00EBFF /* COLORREF: Gelb */);   // Hervorhebung in der Farbe rgb (COLORREF 0x00BBGGRR)
    bool RemoveHighlightAt(int index, int w, int h, int dx, int dy);              // Radierer: entfernt Markierungen unter dem Punkt (Pixel der gerenderten Seite)
    bool AddNote(int index, int w, int h, int dx, int dy, const std::wstring& text);   // Notiz-Symbol mit Kommentar

    struct CompressStats { unsigned long long before = 0, after = 0; int images = 0, recompressed = 0; };
    // Speichert eine verkleinerte Kopie: Bilder auf maxDpi herunterrechnen und als JPEG neu kodieren,
    // nur erreichbare Objekte uebernehmen. Das offene Dokument bleibt unveraendert. Lesezeichen/Formulare gehen verloren.
    bool SaveCompressed(const std::wstring& path, int maxDpi, int jpegQuality, CompressStats& stats);

    // Speichert eine Kopie (inkl. Drehungen und angehaengter Seiten).
    // Nach dem Schwaerzen wird dabei ein bereinigtes Dokument geschrieben (nur erreichbare Seiteninhalte), damit keine Reste bleiben.
    bool SaveCopy(const std::wstring& path);
    // Speichert die Seiten 'indices' als eigene PDF (Auszug/Split). Das offene Dokument bleibt unveraendert.
    bool SavePages(const std::wstring& path, const std::vector<int>& indices);

    // Stempel / Unterschrift: Bild mit Alphakanal auf eine Seite setzen (nicht vormultipliziertes BGRA, Zeilen von oben nach unten).
    struct Image { int w = 0, h = 0; std::vector<unsigned char> bgra; };
    static bool EditSupported();                           // false: pdfium.dll kann keine Seitenobjekte bearbeiten
    bool PlaceImage(int index, int pw, int ph, const RECT& px, const Image& img);   // px = Zielrechteck in Pixeln der gerenderten Seite (pw x ph)

    // Schwaerzen: Text, der unter den Rechtecken (Pixel der gerenderten Seite) liegt, wird aus der Seite entfernt und durch einen schwarzen
    // Balken ersetzt. Laesst sich der Text nicht sauber trennen (Bilder, Formularobjekte, Textlaeufe die ueber den Bereich hinausgehen),
    // wird die Seite als Bild neu aufgebaut (200+ dpi, ohne Text, Notizen und Markierungen).
    struct RedactResult { int textRemoved = 0, annotsRemoved = 0; bool flattened = false; };
    bool Redact(int index, int pw, int ph, const std::vector<RECT>& rects, RedactResult& out);
    bool Redacted() const { return m_sanitize; }
    bool Modified() const { return m_modified; }

    // Zuschneiden: setzt den sichtbaren Bereich (CropBox) der Seiten auf einen Ausschnitt der angezeigten Seite.
    // Koordinaten als Anteile 0..1 der angezeigten Seite (links/oben = 0); der Seiteninhalt bleibt in der Datei erhalten.
    bool CropPages(const std::vector<int>& indices, double fx0, double fy0, double fx1, double fy1);
    bool ResetCrop(const std::vector<int>& indices);       // CropBox = MediaBox

    // Formulare (AcroForm) ausfuellen. Die Koordinaten sind Pixel der gerenderten Seite (w x h, aktuelle Drehung).
    bool HasForms();
    HBITMAP RenderPageForm(int index, int w, int h);       // wie RenderPage, aber mit Formularfeldern (Fokus, Auswahl, Markierung)
    enum FormMouseKind { FormDown, FormUp, FormMove };
    bool FormMouse(FormMouseKind kind, int index, int w, int h, int dx, int dy);   // true = Ereignis behandelt
    int FormFieldAt(int index, int w, int h, int dx, int dy);   // Feldtyp unter der Position oder -1
    bool FormKey(int index, int vk, bool down, int modifiers);
    bool FormChar(int index, int ch, int modifiers);
    std::wstring FormSelectedText(int index);
    bool FormReplaceSelection(int index, const std::wstring& text);
    bool FormSelectAll(int index);
    void FormKillFocus();                                  // uebernimmt den Feldinhalt in das Dokument
    bool TakeFormDirty();                                  // Seite muss neu gezeichnet werden
    static void SetFormRedrawHook(void (*hook)());         // wird aus PDFium-Rueckrufen/Timern aufgerufen
    static void SetBakeRotations(bool on);                 // false: Drehungen beim Speichern nicht in die Datei schreiben (nur Ansicht)

private:
    void BakeRotations();                    // schreibt die Zusatzdrehungen in die Seiten
    void CloseForm();
    bool UseFormPage(int index);
    HBITMAP RenderImpl(void* page, int index, int w, int h, bool withForm) const;
    void* m_form = nullptr;
    void* m_formInfo = nullptr;
    void* m_formPage = nullptr;
    int m_formPageIdx = -1;
    int m_hasForms = -1;
    bool m_formDirty = false;
    friend struct FormCallbacks;
    void* m_doc = nullptr;
    std::vector<char> m_data;     // PDFium liest bei FPDF_LoadMemDocument direkt aus diesem Puffer
    std::wstring m_path;
    std::vector<int> m_rot;
    bool m_modified = false;
    bool m_sanitize = false;      // es wurde geschwaerzt: SaveCopy schreibt ein bereinigtes Dokument
};
