#pragma once
// Dispatch.h - Minimale Helfer fuer COM-Automation (IDispatch, Late Binding).
// Damit sind fuer den Windows Update Agent keine speziellen SDK-Header noetig.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <objbase.h>
#include <oleauto.h>

#include <algorithm>
#include <string>
#include <vector>

#ifdef _MSC_VER
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#endif

namespace dispatch
{
    // Eigene GUID-Konstanten, damit uuid.lib nicht gelinkt werden muss
    const GUID kNullGuid = {};
    const GUID kIidDispatch = { 0x00020400, 0x0000, 0x0000, { 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };

    // ---- Minimale IDispatch-Helfer ---------------------------------------------------------

    // Ruft eine Methode/Property per Name auf. args in normaler Reihenfolge.
    inline HRESULT Invoke(IDispatch* obj, const wchar_t* name, WORD flags, VARIANT* result,
                   std::vector<VARIANT> args = {})
    {
        DISPID id = 0;
        LPOLESTR n = const_cast<LPOLESTR>(name);
        HRESULT hr = obj->GetIDsOfNames(kNullGuid, &n, 1, LOCALE_USER_DEFAULT, &id);
        if (FAILED(hr))
            return hr;

        std::reverse(args.begin(), args.end());   // IDispatch erwartet umgekehrte Reihenfolge
        DISPPARAMS params{};
        params.cArgs = static_cast<UINT>(args.size());
        params.rgvarg = args.empty() ? nullptr : args.data();
        DISPID named = DISPID_PROPERTYPUT;
        if (flags & DISPATCH_PROPERTYPUT)
        {
            params.cNamedArgs = 1;
            params.rgdispidNamedArgs = &named;
        }
        EXCEPINFO ex{};
        UINT argErr = 0;
        return obj->Invoke(id, kNullGuid, LOCALE_USER_DEFAULT, flags, &params, result, &ex, &argErr);
    }

    // Besitzt ein IDispatch* und gibt es frei.
    struct Disp
    {
        IDispatch* p = nullptr;
        Disp() = default;
        explicit Disp(IDispatch* ptr) : p(ptr) {}
        Disp(const Disp&) = delete;
        Disp& operator=(const Disp&) = delete;
        Disp(Disp&& o) noexcept : p(o.p) { o.p = nullptr; }
        ~Disp() { if (p) p->Release(); }
        explicit operator bool() const { return p != nullptr; }
    };

    // Liefert ein Objekt-Property (z. B. Updates) bzw. Methodenergebnis.
    inline Disp GetObject(IDispatch* obj, const wchar_t* name, WORD flags = DISPATCH_PROPERTYGET,
                   std::vector<VARIANT> args = {})
    {
        VARIANT v;
        VariantInit(&v);
        if (FAILED(Invoke(obj, name, flags, &v, std::move(args))))
            return Disp();
        Disp d;
        if (v.vt == VT_DISPATCH && v.pdispVal)
        {
            d.p = v.pdispVal;
            d.p->AddRef();
        }
        VariantClear(&v);
        return d;
    }

    inline std::wstring GetString(IDispatch* obj, const wchar_t* name)
    {
        VARIANT v;
        VariantInit(&v);
        std::wstring out;
        if (SUCCEEDED(Invoke(obj, name, DISPATCH_PROPERTYGET, &v)) && v.vt == VT_BSTR && v.bstrVal)
            out = v.bstrVal;
        VariantClear(&v);
        return out;
    }

    inline long GetLong(IDispatch* obj, const wchar_t* name)
    {
        VARIANT v;
        VariantInit(&v);
        long out = 0;
        if (SUCCEEDED(Invoke(obj, name, DISPATCH_PROPERTYGET, &v)) && SUCCEEDED(VariantChangeType(&v, &v, 0, VT_I4)))
            out = v.lVal;
        VariantClear(&v);
        return out;
    }

    inline bool GetDate(IDispatch* obj, const wchar_t* name, std::wstring& text, int& key)
    {
        VARIANT v;
        VariantInit(&v);
        bool ok = false;
        if (SUCCEEDED(Invoke(obj, name, DISPATCH_PROPERTYGET, &v)) && v.vt == VT_DATE)
        {
            SYSTEMTIME st{};
            if (VariantTimeToSystemTime(v.date, &st))
            {
                key = st.wYear * 10000 + st.wMonth * 100 + st.wDay;
                wchar_t buf[16];
                wsprintfW(buf, L"%02u.%02u.%04u", st.wDay, st.wMonth, st.wYear);
                text = buf;
                ok = true;
            }
        }
        VariantClear(&v);
        return ok;
    }

    inline VARIANT BstrArg(const wchar_t* s)
    {
        VARIANT v;
        VariantInit(&v);
        v.vt = VT_BSTR;
        v.bstrVal = SysAllocString(s);
        return v;
    }

    inline VARIANT LongArg(long n)
    {
        VARIANT v;
        VariantInit(&v);
        v.vt = VT_I4;
        v.lVal = n;
        return v;
    }


    inline bool GetBool(IDispatch* obj, const wchar_t* name)
    {
        VARIANT v;
        VariantInit(&v);
        bool out = false;
        if (SUCCEEDED(Invoke(obj, name, DISPATCH_PROPERTYGET, &v)) && SUCCEEDED(VariantChangeType(&v, &v, 0, VT_BOOL)))
            out = v.boolVal != VARIANT_FALSE;
        VariantClear(&v);
        return out;
    }

    // Setzt eine Property auf ein Objekt (z. B. Installer.Updates = Collection).
    inline HRESULT PutObject(IDispatch* obj, const wchar_t* name, IDispatch* value)
    {
        VARIANT v;
        VariantInit(&v);
        v.vt = VT_DISPATCH;
        v.pdispVal = value;
        return Invoke(obj, name, DISPATCH_PROPERTYPUT, nullptr, { v });
    }

    inline HRESULT PutBool(IDispatch* obj, const wchar_t* name, bool value)
    {
        VARIANT v;
        VariantInit(&v);
        v.vt = VT_BOOL;
        v.boolVal = value ? VARIANT_TRUE : VARIANT_FALSE;
        return Invoke(obj, name, DISPATCH_PROPERTYPUT, nullptr, { v });
    }

    // Legt ein COM-Objekt per ProgID an.
    inline Disp CreateByProgId(const wchar_t* progId)
    {
        CLSID clsid;
        if (FAILED(CLSIDFromProgID(progId, &clsid)))
            return Disp();
        IDispatch* raw = nullptr;
        if (FAILED(CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER, kIidDispatch, reinterpret_cast<void**>(&raw))))
            return Disp();
        return Disp(raw);
    }
}
