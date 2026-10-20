#pragma once
// Asynchroner Thumbnail-Lader: Worker-Threads dekodieren per WIC, der UI-Thread
// bekommt fertige BGRA-Puffer per Nachricht und baut daraus D2D-Bitmaps.
//
// Prioritaet: Der UI-Thread ruft bei jedem Scrollen SetWanted() mit der aktuell
// sichtbaren Liste (wichtigstes zuerst). Alles, was schon aus dem Sichtbereich
// gescrollt ist, faellt dadurch aus der Warteschlange -> Scrollen bleibt fluessig.
#include <windows.h>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

struct ThumbRequest {
    uint32_t generation = 0;
    size_t index = 0;
    bool preview = false;  // true: grosse Vorschau fuer das Detail-Panel
    UINT size = 0;         // laengste Kante in Pixeln
    std::wstring path;
};

struct ThumbResult {
    uint32_t generation = 0;
    size_t index = 0;
    bool preview = false;
    bool ok = false;
    UINT width = 0, height = 0;
    std::vector<BYTE> pixels;  // 32bpp premultiplied BGRA, Stride = width * 4
};

class ThumbnailLoader {
public:
    ~ThumbnailLoader() { Stop(); }

    // workers = 0 -> automatisch (Kerne - 1, mindestens 2)
    void Start(HWND notifyWnd, UINT notifyMsg, unsigned workers = 0);
    void Stop();

    // Verwirft alle offenen Anfragen (z. B. bei Ordnerwechsel).
    uint32_t NewGeneration();
    uint32_t Generation() const { return m_generation.load(); }

    // Ersetzt die Warteschlange. reqs[0] hat die hoechste Prioritaet.
    void SetWanted(uint32_t generation, std::vector<ThumbRequest> reqs);

    std::vector<ThumbResult> Drain();

private:
    void Worker();

    HWND m_wnd = nullptr;
    UINT m_msg = 0;
    std::vector<std::thread> m_threads;

    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::deque<ThumbRequest> m_queue;
    std::set<size_t> m_running;
    bool m_stop = false;

    std::mutex m_resultMutex;
    std::vector<ThumbResult> m_results;
    std::atomic<bool> m_notifyPending{false};
    std::atomic<uint32_t> m_generation{1};
};
