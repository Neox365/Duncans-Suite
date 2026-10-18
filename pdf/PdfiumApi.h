#pragma once
// Minimale Deklarationen der benoetigten PDFium-C-API (public/fpdfview.h, fpdf_edit.h, fpdf_ppo.h, fpdf_save.h).
// Alle Funktionen werden aus pdfium.dll geladen. Typen und Werte entsprechen den offiziellen Headern.
#include <windows.h>

typedef void* FPDF_DOCUMENT;
typedef void* FPDF_PAGE;
typedef void* FPDF_BITMAP;
typedef int FPDF_BOOL;

struct FPDF_LIBRARY_CONFIG {
    int version;                       // 2
    const char** m_pUserFontPaths;
    void* m_pIsolate;
    unsigned m_v8EmbedderSlot;
    void* m_pPlatform;
    int m_RendererType;
};

struct FPDF_FILEWRITE {
    int version;                       // 1
    int (*WriteBlock)(FPDF_FILEWRITE* self, const void* data, unsigned long size);
};

enum { FPDF_ANNOT = 0x01, FPDF_LCD_TEXT = 0x02 };
enum { FPDFBitmap_BGRx = 3 };

struct PdfiumApi {
    HMODULE module = nullptr;
    void (*InitLibraryWithConfig)(const FPDF_LIBRARY_CONFIG*) = nullptr;
    void (*DestroyLibrary)() = nullptr;
    unsigned long (*GetLastError)() = nullptr;
    FPDF_DOCUMENT (*LoadMemDocument)(const void* data, int size, const char* password) = nullptr;
    void (*CloseDocument)(FPDF_DOCUMENT) = nullptr;
    int (*GetPageCount)(FPDF_DOCUMENT) = nullptr;
    FPDF_PAGE (*LoadPage)(FPDF_DOCUMENT, int index) = nullptr;
    void (*ClosePage)(FPDF_PAGE) = nullptr;
    float (*GetPageWidthF)(FPDF_PAGE) = nullptr;
    float (*GetPageHeightF)(FPDF_PAGE) = nullptr;
    FPDF_BITMAP (*BitmapCreateEx)(int w, int h, int format, void* buffer, int stride) = nullptr;
    void (*BitmapFillRect)(FPDF_BITMAP, int left, int top, int w, int h, unsigned long color) = nullptr;
    void (*BitmapDestroy)(FPDF_BITMAP) = nullptr;
    void (*RenderPageBitmap)(FPDF_BITMAP, FPDF_PAGE, int sx, int sy, int sw, int sh, int rotate, int flags) = nullptr;
    int (*PageGetRotation)(FPDF_PAGE) = nullptr;
    void (*PageSetRotation)(FPDF_PAGE, int rotate) = nullptr;
    FPDF_BOOL (*ImportPages)(FPDF_DOCUMENT dest, FPDF_DOCUMENT src, const char* pageRange, int index) = nullptr;
    FPDF_BOOL (*SaveAsCopy)(FPDF_DOCUMENT, FPDF_FILEWRITE*, unsigned long flags) = nullptr;
    void (*PageDelete)(FPDF_DOCUMENT, int index) = nullptr;
    FPDF_BOOL (*PageToDevice)(FPDF_PAGE, int sx, int sy, int sw, int sh, int rotate, double px, double py, int* dx, int* dy) = nullptr;
    // Textsuche (fpdf_text.h); der Textseiten- und der Such-Handle sind undurchsichtige Zeiger
    void* (*TextLoadPage)(FPDF_PAGE) = nullptr;
    void (*TextClosePage)(void* textPage) = nullptr;
    void* (*TextFindStart)(void* textPage, const unsigned short* findWhat, unsigned long flags, int startIndex) = nullptr;
    FPDF_BOOL (*TextFindNext)(void* search) = nullptr;
    int (*TextGetSchResultIndex)(void* search) = nullptr;
    int (*TextGetSchCount)(void* search) = nullptr;
    void (*TextFindClose)(void* search) = nullptr;
    int (*TextCountRects)(void* textPage, int startIndex, int count) = nullptr;
    FPDF_BOOL (*TextGetRect)(void* textPage, int rectIndex, double* left, double* top, double* right, double* bottom) = nullptr;
};
