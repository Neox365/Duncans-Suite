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

struct FPDF_FILEACCESS {
    unsigned long m_FileLen;
    int (*m_GetBlock)(void* param, unsigned long position, unsigned char* buf, unsigned long size);
    void* m_Param;
};
struct FS_RECTF { float left, top, right, bottom; };            // top > bottom (PDF-Koordinaten, y nach oben)
struct FS_QUADPOINTSF { float x1, y1, x2, y2, x3, y3, x4, y4; };
struct FS_MATRIX { float a, b, c, d, e, f; };
struct FPDF_IMAGEOBJ_METADATA {
    unsigned int width, height;
    float horizontal_dpi, vertical_dpi;
    unsigned int bits_per_pixel;
    int colorspace;
    int marked_content_id;
};

enum { FPDF_ANNOT = 0x01, FPDF_LCD_TEXT = 0x02 };
enum { FPDFBitmap_Gray = 1, FPDFBitmap_BGR = 2, FPDFBitmap_BGRx = 3, FPDFBitmap_BGRA = 4 };
enum { FPDF_ANNOT_TEXT = 1, FPDF_ANNOT_HIGHLIGHT = 9 };
enum { FPDF_PAGEOBJ_IMAGE = 3 };
enum { FPDF_NO_INCREMENTAL = 2 };

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
    int (*TextGetCharIndexAtPos)(void* textPage, double x, double y, double xTol, double yTol) = nullptr;
    FPDF_BOOL (*DeviceToPage)(FPDF_PAGE, int sx, int sy, int sw, int sh, int rotate, int dx, int dy, double* px, double* py) = nullptr;
    // Seiten verschieben / neues Dokument
    FPDF_DOCUMENT (*CreateNewDocument)() = nullptr;
    FPDF_BOOL (*MovePages)(FPDF_DOCUMENT, const int* pageIndices, unsigned long count, int destPageIndex) = nullptr;
    // Anmerkungen (fpdf_annot.h)
    void* (*PageCreateAnnot)(FPDF_PAGE, int subtype) = nullptr;
    void (*PageCloseAnnot)(void* annot) = nullptr;
    FPDF_BOOL (*AnnotSetColor)(void* annot, int colorType, unsigned R, unsigned G, unsigned B, unsigned A) = nullptr;
    FPDF_BOOL (*AnnotAppendAttachmentPoints)(void* annot, const FS_QUADPOINTSF*) = nullptr;
    FPDF_BOOL (*AnnotSetRect)(void* annot, const FS_RECTF*) = nullptr;
    FPDF_BOOL (*AnnotSetStringValue)(void* annot, const char* key, const unsigned short* value) = nullptr;
    // Seitenobjekte / Bilder (fpdf_edit.h) fuer das Verkleinern
    int (*PageCountObjects)(FPDF_PAGE) = nullptr;
    void* (*PageGetObject)(FPDF_PAGE, int index) = nullptr;
    int (*PageObjGetType)(void* obj) = nullptr;
    FPDF_BITMAP (*ImageObjGetBitmap)(void* image) = nullptr;
    FPDF_BOOL (*ImageObjGetMetadata)(void* image, FPDF_PAGE, FPDF_IMAGEOBJ_METADATA*) = nullptr;
    unsigned long (*ImageObjGetImageDataRaw)(void* image, void* buffer, unsigned long len) = nullptr;
    FPDF_BOOL (*ImageObjLoadJpegFileInline)(FPDF_PAGE* pages, int count, void* image, FPDF_FILEACCESS*) = nullptr;
    FPDF_BOOL (*PageGenerateContent)(FPDF_PAGE) = nullptr;
    int (*BitmapGetWidth)(FPDF_BITMAP) = nullptr;
    int (*BitmapGetHeight)(FPDF_BITMAP) = nullptr;
    int (*BitmapGetStride)(FPDF_BITMAP) = nullptr;
    void* (*BitmapGetBuffer)(FPDF_BITMAP) = nullptr;
    int (*BitmapGetFormat)(FPDF_BITMAP) = nullptr;
};
