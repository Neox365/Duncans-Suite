#pragma once
// Kundenliste fuer den Rechnungs-Generator: Name + Adresse, gespeichert als ".kunden.tsv" im Rechnungsordner (wandert mit den Rechnungen).
// Reine Logik, getrennt testbar.
#include <algorithm>
#include <cwctype>
#include <string>
#include <vector>

#include "Store.h"

namespace docs {

struct Customer {
    std::wstring name, address;   // Adresse so, wie sie im Rechnungsformular steht ("Strasse | PLZ Ort")
};

inline std::wstring CustomerKey(const std::wstring& name)   // Vergleich ohne Beachtung der Schreibweise und Randleerzeichen
{
    std::wstring k = Trim(name);
    for (wchar_t& c : k) c = static_cast<wchar_t>(std::towlower(c));
    return k;
}

inline void SortCustomers(std::vector<Customer>& v)
{
    std::stable_sort(v.begin(), v.end(), [](const Customer& a, const Customer& b) { return CustomerKey(a.name) < CustomerKey(b.name); });
}

inline std::string SerializeCustomers(const std::vector<Customer>& list)
{
    std::vector<Customer> v = list;
    SortCustomers(v);
    std::wstring out = L"#OggysDocsFiles-Kunden 1\n";
    for (const Customer& c : v) out += CleanField(c.name) + L"\t" + CleanField(c.address) + L"\n";
    return WideToUtf8(out);
}

inline std::vector<Customer> ParseCustomers(const std::string& utf8)
{
    std::vector<Customer> out;
    std::string text = utf8.size() > 4u * 1024 * 1024 ? utf8.substr(0, 4u * 1024 * 1024) : utf8;
    if (text.compare(0, 3, "\xEF\xBB\xBF") == 0) text.erase(0, 3);
    const std::wstring all = changelog::Utf8ToWide(text);
    size_t pos = 0;
    while (pos < all.size()) {
        size_t nl = all.find(L'\n', pos);
        if (nl == std::wstring::npos) nl = all.size();
        std::wstring line = all.substr(pos, nl - pos);
        pos = nl + 1;
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        if (line.empty() || line[0] == L'#') continue;
        const size_t tab = line.find(L'\t');
        Customer c;
        c.name = Trim(tab == std::wstring::npos ? line : line.substr(0, tab));
        c.address = tab == std::wstring::npos ? std::wstring() : Trim(line.substr(tab + 1));
        if (c.name.empty()) continue;
        const std::wstring key = CustomerKey(c.name);
        auto dup = std::find_if(out.begin(), out.end(), [&](const Customer& x) { return CustomerKey(x.name) == key; });
        if (dup == out.end()) out.push_back(std::move(c));
        else if (!c.address.empty()) dup->address = c.address;
    }
    SortCustomers(out);
    return out;
}

// Legt den Kunden an oder aktualisiert seine Adresse (eine leere Adresse ueberschreibt nichts). true, wenn sich die Liste geaendert hat.
inline bool UpsertCustomer(std::vector<Customer>& list, const std::wstring& nameIn, const std::wstring& addressIn)
{
    const std::wstring name = CleanField(nameIn), address = CleanField(addressIn);
    if (name.empty()) return false;
    const std::wstring key = CustomerKey(name);
    for (Customer& c : list) {
        if (CustomerKey(c.name) != key) continue;
        if (address.empty() || c.address == address) return false;
        c.address = address;
        return true;
    }
    list.push_back({name, address});
    SortCustomers(list);
    return true;
}

inline bool RemoveCustomer(std::vector<Customer>& list, const std::wstring& name)
{
    const std::wstring key = CustomerKey(name);
    const auto it = std::remove_if(list.begin(), list.end(), [&](const Customer& c) { return CustomerKey(c.name) == key; });
    if (it == list.end()) return false;
    list.erase(it, list.end());
    return true;
}

}  // namespace docs
