#include "ThumbnailLoader.h"
#include "ImageOps.h"

#include <wrl/client.h>
#include <algorithm>
#include <cmath>

using Microsoft::WRL::ComPtr;

namespace {

size_t Key(const ThumbRequest& r) { return r.index * 2 + (r.preview ? 1 : 0); }

ThumbResult Load(IWICImagingFactory* f, const ThumbRequest& r) {
    ThumbResult res;
    res.generation = r.generation;
    res.index = r.index;
    res.preview = r.preview;

    ComPtr<IWICBitmapDecoder> dec;
    if (FAILED(f->CreateDecoderFromFilename(r.path.c_str(), nullptr, GENERIC_READ,
                                            WICDecodeMetadataCacheOnDemand, &dec)))
        return res;
    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(dec->GetFrame(0, &frame))) return res;

    UINT w = 0, h = 0;
    frame->GetSize(&w, &h);
    if (!w || !h || !r.size) return res;

    double s = std::min(1.0, std::min((double)r.size / w, (double)r.size / h));
    UINT tw = std::max(1u, (UINT)std::lround(w * s));
    UINT th = std::max(1u, (UINT)std::lround(h * s));

    ComPtr<IWICBitmapSource> src = imgops::ScaledSource(f, frame.Get(), tw, th);  // JPEG: DCT-Skalierung
    if (!src) return res;

    // EXIF-Drehung (Handyfotos im Hochformat)
    WICBitmapTransformOptions t = imgops::OrientationToTransform(imgops::ReadOrientation(frame.Get()));
    if (t != WICBitmapTransformRotate0) {
        ComPtr<IWICBitmapFlipRotator> fr;
        if (SUCCEEDED(f->CreateBitmapFlipRotator(&fr)) && SUCCEEDED(fr->Initialize(src.Get(), t))) src = fr;
    }

    ComPtr<IWICFormatConverter> cv;
    if (FAILED(f->CreateFormatConverter(&cv)) ||
        FAILED(cv->Initialize(src.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.0,
                              WICBitmapPaletteTypeCustom)))
        return res;

    UINT ow = 0, oh = 0;
    cv->GetSize(&ow, &oh);
    res.pixels.resize((size_t)ow * oh * 4);
    if (FAILED(cv->CopyPixels(nullptr, ow * 4, (UINT)res.pixels.size(), res.pixels.data()))) {
        res.pixels.clear();
        return res;
    }
    res.width = ow;
    res.height = oh;
    res.ok = true;
    return res;
}

}  // namespace

void ThumbnailLoader::Start(HWND wnd, UINT msg, unsigned workers) {
    m_wnd = wnd;
    m_msg = msg;
    if (workers == 0) {
        unsigned hc = std::thread::hardware_concurrency();
        workers = std::max(2u, hc > 1 ? hc - 1 : 1u);
    }
    for (unsigned i = 0; i < workers; ++i) m_threads.emplace_back(&ThumbnailLoader::Worker, this);
}

void ThumbnailLoader::Stop() {
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_stop = true;
        m_queue.clear();
    }
    m_cv.notify_all();
    for (auto& t : m_threads)
        if (t.joinable()) t.join();
    m_threads.clear();
}

uint32_t ThumbnailLoader::NewGeneration() {
    std::lock_guard<std::mutex> lk(m_mutex);
    m_queue.clear();
    return ++m_generation;
}

void ThumbnailLoader::SetWanted(uint32_t generation, std::vector<ThumbRequest> reqs) {
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (generation != m_generation.load()) return;
        m_queue.clear();
        for (auto& r : reqs) {
            if (m_running.count(Key(r))) continue;
            m_queue.push_back(std::move(r));
        }
    }
    m_cv.notify_all();
}

std::vector<ThumbResult> ThumbnailLoader::Drain() {
    std::vector<ThumbResult> out;
    std::lock_guard<std::mutex> lk(m_resultMutex);
    out.swap(m_results);
    m_notifyPending = false;
    return out;
}

void ThumbnailLoader::Worker() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    {
        ComPtr<IWICImagingFactory> factory;
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
        for (;;) {
            ThumbRequest req;
            {
                std::unique_lock<std::mutex> lk(m_mutex);
                m_cv.wait(lk, [&] { return m_stop || !m_queue.empty(); });
                if (m_stop) break;
                req = std::move(m_queue.front());
                m_queue.pop_front();
                m_running.insert(Key(req));
            }
            ThumbResult res;
            if (factory && req.generation == m_generation.load()) res = Load(factory.Get(), req);
            else { res.generation = req.generation; res.index = req.index; res.preview = req.preview; }
            {
                std::lock_guard<std::mutex> lk(m_mutex);
                m_running.erase(Key(req));
            }
            if (res.generation == m_generation.load()) {
                {
                    std::lock_guard<std::mutex> lk(m_resultMutex);
                    m_results.push_back(std::move(res));
                }
                if (!m_notifyPending.exchange(true)) PostMessageW(m_wnd, m_msg, 0, 0);
            }
        }
    }
    CoUninitialize();
}
