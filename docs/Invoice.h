#pragma once
// Rechnungs-Vorlage: Daten -> Summen -> PDF (Seitenaufbau mit Absender, Rechnungskopf, Empfaenger, Positionstabelle mit
// Seitenumbruch, Summen, Zahlungshinweis). Reine Logik, getrennt testbar.
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "GiroCode.h"
#include "PdfWriter.h"
#include "QrCode.h"
#include "Store.h"

namespace invoice {

struct Item {
    std::wstring desc;
    double qty = 1;
    long long unitCents = 0;
    int vatPercent = -1;   // Umsatzsteuersatz dieser Position; -1 = Satz der Rechnung (Data::vatPercent)
};

struct Data {
    std::wstring issuer, issuerAddress;        // Absender = Glaeubiger (Adresse: Zeilen mit '|' oder Zeilenumbruch trennen)
    std::wstring recipient, recipientAddress;  // Empfaenger
    std::wstring number, dateIso, dueIso;
    std::wstring note;                         // Zahlungshinweis / Bankverbindung / Gruss
    std::vector<Item> items;
    int vatPercent = 19;
    std::wstring owner, court;                 // Inhaber / gesetzlicher Vertreter, Amtsgericht / HRB (Fusszeile links)
    std::wstring phone, email, web;            // Kontakt (Fusszeile Mitte)
    std::wstring bank, iban, bic;              // Bankverbindung (Fusszeile rechts)
    std::wstring taxNo;                        // Steuernummer oder USt-IdNr. des Absenders (leer = keine Angabe)
    bool smallBusiness = false;                // Kleinunternehmer nach § 19 UStG: keine Umsatzsteuer, Hinweistext auf der PDF
    std::wstring cancels, cancelsDateIso;      // gesetzt = Gutschrift/Storno zur Rechnung mit dieser Nummer und diesem Datum (Betraege bleiben positiv)
};

// Rechnungsdaten als Text ablegen (fuer Storno): je Zeile "Schluessel<TAB>Wert", Positionen "item<TAB>Text<TAB>Menge<TAB>Cent<TAB>USt"
inline std::string SerializeData(const Data& d)
{
    auto clean = [](std::wstring s) { for (auto& c : s) if (c == L'\t' || c == L'\n' || c == L'\r') c = L' '; return docs::WideToUtf8(s); };
    std::string o = "#Rechnungsdaten 1\n";
    auto kv = [&](const char* k, const std::wstring& v) { o += std::string(k) + "\t" + clean(v) + "\n"; };
    kv("issuer", d.issuer); kv("issuerAddress", d.issuerAddress); kv("recipient", d.recipient); kv("recipientAddress", d.recipientAddress);
    kv("number", d.number); kv("date", d.dateIso); kv("due", d.dueIso); kv("note", d.note); kv("vat", std::to_wstring(d.vatPercent));
    kv("owner", d.owner); kv("court", d.court); kv("phone", d.phone); kv("email", d.email); kv("web", d.web);
    kv("bank", d.bank); kv("iban", d.iban); kv("bic", d.bic); kv("taxNo", d.taxNo); kv("small", d.smallBusiness ? L"1" : L"0");
    kv("cancels", d.cancels); kv("cancelsDate", d.cancelsDateIso);
    for (const Item& it : d.items) {
        char q[48];
        std::snprintf(q, sizeof q, "%.6f", it.qty);
        o += "item\t" + clean(it.desc) + "\t" + q + "\t" + std::to_string(it.unitCents) + "\t" + std::to_string(it.vatPercent) + "\n";
    }
    return o;
}

inline bool ParseData(const std::string& text, Data& d)
{
    d = Data{};
    bool any = false;
    size_t p = 0;
    while (p < text.size()) {
        size_t nl = text.find('\n', p);
        if (nl == std::string::npos) nl = text.size();
        std::string line = text.substr(p, nl - p);
        p = nl + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        std::vector<std::string> f;
        size_t s = 0;
        for (;;) {
            const size_t t = line.find('\t', s);
            if (t == std::string::npos) { f.push_back(line.substr(s)); break; }
            f.push_back(line.substr(s, t - s));
            s = t + 1;
        }
        auto w = [&](size_t i) { return i < f.size() ? changelog::Utf8ToWide(f[i]) : std::wstring(); };
        const std::string& k = f[0];
        try {
            if (k == "issuer") d.issuer = w(1); else if (k == "issuerAddress") d.issuerAddress = w(1);
            else if (k == "recipient") d.recipient = w(1); else if (k == "recipientAddress") d.recipientAddress = w(1);
            else if (k == "number") d.number = w(1); else if (k == "date") d.dateIso = w(1); else if (k == "due") d.dueIso = w(1);
            else if (k == "note") d.note = w(1); else if (k == "vat") d.vatPercent = std::stoi(f.size() > 1 ? f[1] : "19");
            else if (k == "owner") d.owner = w(1); else if (k == "court") d.court = w(1); else if (k == "phone") d.phone = w(1);
            else if (k == "email") d.email = w(1); else if (k == "web") d.web = w(1); else if (k == "bank") d.bank = w(1);
            else if (k == "iban") d.iban = w(1); else if (k == "bic") d.bic = w(1); else if (k == "taxNo") d.taxNo = w(1);
            else if (k == "small") d.smallBusiness = f.size() > 1 && f[1] == "1";
            else if (k == "cancels") d.cancels = w(1); else if (k == "cancelsDate") d.cancelsDateIso = w(1);
            else if (k == "item" && f.size() >= 5) {
                Item it;
                it.desc = w(1); it.qty = std::stod(f[2]); it.unitCents = std::stoll(f[3]); it.vatPercent = std::stoi(f[4]);
                d.items.push_back(it);
            }
            any = true;
        } catch (...) { return false; }
    }
    return any && !d.items.empty();
}

// Standard-Schlusstext; [Gesamtsumme] und [Fälligkeitsdatum] werden beim Erzeugen eingesetzt
inline const wchar_t* DefaultNote()
{
    return L"Bitte überweisen Sie den Rechnungsbetrag von [Gesamtsumme] bis zum [Fälligkeitsdatum] auf das unten genannte Konto. "
           L"Soweit nicht anders angegeben, entspricht das Rechnungsdatum dem Leistungsdatum. Vielen Dank für Ihren Auftrag!";
}

inline std::wstring ReplaceAll(std::wstring s, const std::wstring& from, const std::wstring& to)
{
    for (size_t p = 0; (p = s.find(from, p)) != std::wstring::npos; p += to.size()) s.replace(p, from.size(), to);
    return s;
}

// Platzhalter ersetzen; ohne Faelligkeit entfaellt "bis zum [Faelligkeitsdatum]"
inline std::wstring ExpandNote(std::wstring note, long long grossCents, const std::wstring& dueIso)
{
    if (dueIso.empty()) note = ReplaceAll(note, L" bis zum [Fälligkeitsdatum]", L"");
    note = ReplaceAll(note, L"[Gesamtsumme]", docs::FormatAmount(grossCents));
    return ReplaceAll(note, L"[Fälligkeitsdatum]", docs::FormatDate(dueIso));
}

inline const wchar_t* SmallBusinessNote() { return L"Gemäß § 19 UStG wird keine Umsatzsteuer berechnet."; }

// "DE123456789" -> USt-IdNr., alles andere -> Steuernummer
inline const wchar_t* TaxNoLabel(const std::wstring& no)
{
    return no.size() > 2 && no[0] >= L'A' && no[0] <= L'Z' && no[1] >= L'A' && no[1] <= L'Z' ? L"USt-IdNr." : L"Steuernr.";
}

struct VatLine {
    int percent = 0;
    long long net = 0, vat = 0;   // Nettosumme aller Positionen mit diesem Satz und die darauf berechnete Steuer
};

struct Totals {
    long long net = 0, vat = 0, gross = 0;
    std::vector<long long> lines;   // Betrag je Position in Cent
    std::vector<VatLine> vatLines;  // je Steuersatz (aufsteigend); bei Kleinunternehmern leer
};

inline int ItemVat(const Data& d, const Item& it) { return it.vatPercent >= 0 ? it.vatPercent : d.vatPercent; }

inline long long LineCents(const Item& it)
{
    return static_cast<long long>(std::llround(it.qty * static_cast<double>(it.unitCents)));
}

// Summen: Die Steuer wird je Steuersatz auf die Nettosumme dieses Satzes berechnet und kaufmaennisch gerundet (nicht je Position)
inline Totals Compute(const Data& d)
{
    Totals t;
    for (const Item& it : d.items) {
        t.lines.push_back(LineCents(it));
        t.net += t.lines.back();
        if (d.smallBusiness) continue;
        const int pct = ItemVat(d, it);
        auto f = std::find_if(t.vatLines.begin(), t.vatLines.end(), [&](const VatLine& v) { return v.percent == pct; });
        if (f == t.vatLines.end()) { VatLine v; v.percent = pct; t.vatLines.push_back(v); f = t.vatLines.end() - 1; }
        f->net += t.lines.back();
    }
    std::sort(t.vatLines.begin(), t.vatLines.end(), [](const VatLine& a, const VatLine& b) { return a.percent < b.percent; });
    for (VatLine& v : t.vatLines) {
        v.vat = static_cast<long long>(std::llround(static_cast<double>(v.net) * v.percent / 100.0));
        t.vat += v.vat;
    }
    t.gross = t.net + t.vat;
    return t;
}

// Zeichnet den QR-Code als schwarze Rechtecke (je Zeile zusammenhaengende Module zu einem Rechteck); (x, yTop) = linke obere Ecke, mod = Modulgroesse in Punkt
inline void DrawQr(pdfw::Document& pdf, const qr::Code& c, double x, double yTop, double mod)
{
    for (int row = 0; row < c.size; ++row) {
        int col = 0;
        while (col < c.size) {
            if (!c.Dark(col, row)) { ++col; continue; }
            int end = col;
            while (end < c.size && c.Dark(end, row)) ++end;
            pdf.FillRect(x + col * mod, yTop - (row + 1) * mod, (end - col) * mod, mod + 0.04, 0.0);   // 0,04 pt Ueberlappung gegen helle Nahtlinien
            col = end;
        }
    }
}

// "1,5" / "2" -> 1.5 / 2.0 (Komma oder Punkt); false bei Muell
inline bool ParseQuantity(const std::wstring& in, double& out)
{
    std::wstring s = docs::Trim(in);
    if (s.empty()) return false;
    for (auto& c : s)
        if (c == L',') c = L'.';
    int dots = 0;
    for (wchar_t c : s) {
        if (c == L'.') ++dots;
        else if (c < L'0' || c > L'9') return false;
    }
    if (dots > 1 || s == L".") return false;
    out = std::stod(s);
    return out >= 0 && out < 1e7;
}

inline std::wstring FormatQuantity(double q)
{
    if (std::fabs(q - std::round(q)) < 1e-9) return std::to_wstring(static_cast<long long>(std::llround(q)));
    wchar_t buf[32];
    swprintf(buf, 32, L"%.2f", q);
    std::wstring s = buf;
    for (auto& c : s)
        if (c == L'.') c = L',';
    while (!s.empty() && s.back() == L'0') s.pop_back();
    return s;
}

inline std::vector<std::wstring> SplitLines(const std::wstring& s)
{
    std::vector<std::wstring> out;
    std::wstring cur;
    for (wchar_t c : s) {
        if (c == L'|' || c == L'\n' || c == L'\r') {
            if (!docs::Trim(cur).empty()) out.push_back(docs::Trim(cur));
            cur.clear();
        } else cur.push_back(c);
    }
    if (!docs::Trim(cur).empty()) out.push_back(docs::Trim(cur));
    return out;
}

// Zeilenumbruch nach Breite (Punkt); zu lange Woerter werden zeichenweise getrennt
inline std::vector<std::wstring> Wrap(const std::wstring& text, double width, double size, bool bold = false)
{
    std::vector<std::wstring> lines;
    for (const std::wstring& para : SplitLines(text)) {
        std::wstring cur;
        size_t p = 0;
        while (p < para.size()) {
            size_t e = para.find(L' ', p);
            if (e == std::wstring::npos) e = para.size();
            std::wstring word = para.substr(p, e - p);
            p = e + 1;
            while (pdfw::Document::Width(word, size, bold) > width && word.size() > 1) {   // Wort laenger als die Zeile
                size_t n = word.size() - 1;
                while (n > 1 && pdfw::Document::Width(word.substr(0, n), size, bold) > width) --n;
                if (!cur.empty()) { lines.push_back(cur); cur.clear(); }
                lines.push_back(word.substr(0, n));
                word = word.substr(n);
            }
            const std::wstring trial = cur.empty() ? word : cur + L" " + word;
            if (!cur.empty() && pdfw::Document::Width(trial, size, bold) > width) {
                lines.push_back(cur);
                cur = word;
            } else cur = trial;
        }
        if (!cur.empty()) lines.push_back(cur);
    }
    return lines;
}

inline std::string BuildPdf(const Data& d, const std::string* eInvoiceXml = nullptr)
{
    using pdfw::Document;
    const double L = 56, R = pdfw::kPageW - 56, top = pdfw::kPageH - 52;
    Document pdf;
    const Totals tot = Compute(d);
    const bool hasFooter = !(d.owner.empty() && d.court.empty() && d.taxNo.empty() && d.phone.empty() && d.email.empty() && d.web.empty() &&
                             d.bank.empty() && d.iban.empty() && d.bic.empty());

    // Absender links oben
    pdf.Text(L, top, 15, true, d.issuer.empty() ? L"Rechnung" : d.issuer);
    double y = top - 16;
    for (const auto& line : SplitLines(d.issuerAddress)) {
        pdf.Text(L, y, 9, false, line, 0.35);
        y -= 11;
    }
    // Rechnungskopf rechts
    const bool credit = !d.cancels.empty();
    pdf.TextRight(R, top - 2, credit ? 20 : 24, true, credit ? L"GUTSCHRIFT" : L"RECHNUNG", 0.15);
    const double lx = 390;
    double ky = top - 30;
    auto kv = [&](const wchar_t* k, const std::wstring& v) {
        if (v.empty()) return;
        pdf.Text(lx, ky, 10, false, k, 0.4);
        pdf.TextRight(R, ky, 10, true, v);
        ky -= 14;
    };
    kv(credit ? L"Gutschriftsnr." : L"Rechnungsnr.", d.number);
    if (credit) kv(L"Storno zu", d.cancels + (d.cancelsDateIso.empty() ? L"" : L" vom " + docs::FormatDate(d.cancelsDateIso)));
    kv(L"Datum", docs::FormatDate(d.dateIso));
    kv(L"Fällig am", docs::FormatDate(d.dueIso));

    // Empfaenger
    double ry = top - 130;
    if (!d.issuer.empty()) {
        std::wstring ret = d.issuer;
        const auto al = SplitLines(d.issuerAddress);
        if (!al.empty()) ret += L" · " + al.front();
        pdf.Text(L, ry + 22, 7, false, ret, 0.45);
        pdf.Line(L, ry + 18, L + 230, ry + 18, 0.4, 0.75);
    }
    pdf.Text(L, ry, 11, true, d.recipient);
    ry -= 14;
    for (const auto& line : SplitLines(d.recipientAddress)) {
        pdf.Text(L, ry, 10, false, line);
        ry -= 13;
    }

    // Tabelle
    const bool multiVat = tot.vatLines.size() > 1;   // mehrere Steuersaetze: eigene Spalte mit dem Satz je Position
    const double cPos = L + 4, cDesc = L + 30, cQty = multiVat ? 340 : 360, cUnit = multiVat ? 415 : 450, cVat = 458, cSum = R - 4;
    auto header = [&](double hy) {
        pdf.FillRect(L, hy - 6, R - L, 20, 0.93);
        pdf.Text(cPos, hy, 9, true, L"Pos.");
        pdf.Text(cDesc, hy, 9, true, L"Beschreibung");
        pdf.TextRight(cQty, hy, 9, true, L"Menge");
        pdf.TextRight(cUnit, hy, 9, true, L"Einzelpreis");
        if (multiVat) pdf.TextRight(cVat, hy, 9, true, L"USt");
        pdf.TextRight(cSum, hy, 9, true, L"Gesamt");
    };
    double ty = top - 250;
    header(ty);
    ty -= 26;
    for (size_t i = 0; i < d.items.size(); ++i) {
        const auto lines = Wrap(d.items[i].desc, cQty - 60 - cDesc, 10);
        const double h = 13.0 * std::max<size_t>(1, lines.size()) + 6;
        if (ty - h < (hasFooter ? 215 : 190)) {   // Seitenumbruch: Platz fuer Summen reservieren, auf der naechsten Seite weitermachen
            pdf.NewPage();
            ty = top - 10;
            header(ty);
            ty -= 26;
        }
        pdf.Text(cPos, ty, 10, false, std::to_wstring(i + 1), 0.4);
        double ly = ty;
        for (const auto& line : lines) { pdf.Text(cDesc, ly, 10, false, line); ly -= 13; }
        pdf.TextRight(cQty, ty, 10, false, FormatQuantity(d.items[i].qty));
        pdf.TextRight(cUnit, ty, 10, false, docs::FormatAmount(d.items[i].unitCents));
        if (multiVat) pdf.TextRight(cVat, ty, 10, false, std::to_wstring(ItemVat(d, d.items[i])) + L" %", 0.3);
        pdf.TextRight(cSum, ty, 10, false, docs::FormatAmount(tot.lines[i]));
        ty -= h;
        pdf.Line(L, ty + 8, R, ty + 8, 0.3, 0.85);
    }

    // Summen
    ty -= 10;
    if (ty < 170) { pdf.NewPage(); ty = top - 10; }
    if (!d.smallBusiness) {
        pdf.Text(cUnit - 70, ty, 10, false, L"Zwischensumme (netto)", 0.3);
        pdf.TextRight(cSum, ty, 10, false, docs::FormatAmount(tot.net));
        ty -= 15;
        for (size_t vi = 0; vi < tot.vatLines.size(); ++vi) {
            const VatLine& vl = tot.vatLines[vi];
            if (multiVat) pdf.Text(300, ty, 9.5, false, L"Umsatzsteuer " + std::to_wstring(vl.percent) + L" % auf " + docs::FormatAmount(vl.net), 0.3);
            else pdf.Text(cUnit - 70, ty, 10, false, L"Umsatzsteuer " + std::to_wstring(vl.percent) + L" %", 0.3);
            pdf.TextRight(cSum, ty, 10, false, docs::FormatAmount(vl.vat));
            if (vi + 1 < tot.vatLines.size()) ty -= 14;
        }
    }
    ty -= 8;
    pdf.Line(cUnit - 72, ty, R, ty, 0.8, 0.3);
    ty -= 18;
    pdf.FillRect(cUnit - 76, ty - 6, R - cUnit + 76, 22, 0.93);
    pdf.Text(cUnit - 70, ty, 11, true, L"Gesamtbetrag");
    pdf.TextRight(cSum, ty, 12, true, docs::FormatAmount(tot.gross));
    ty -= 38;

    // Schlusstext / Zahlungshinweis (leer = Standardtext), danach der Hinweis nach § 19 UStG
    const std::wstring closing = ExpandNote(docs::Trim(d.note).empty() ? std::wstring(DefaultNote()) : d.note, tot.gross, d.dueIso);
    const double floorY = hasFooter ? 112 : 60;
    // GiroCode: Banking-App liest Empfaenger, IBAN, Betrag und Verwendungszweck. Nur mit gueltiger IBAN und positivem Gesamtbetrag.
    qr::Code qrCode;
    bool giro = false;
    const std::string epc = giro::EpcPayload(d.issuer, d.iban, d.bic, tot.gross, d.number.empty() ? L"Rechnung" : L"Rechnung " + d.number);
    if (!epc.empty() && qr::Encode(epc, qrCode)) giro = true;
    const double qrMod = 2.4, qrSize = giro ? qrCode.size * qrMod : 0;
    bool qrBeside = false;
    if (giro && ty + 4 - (qrSize + 24) >= floorY) {   // genug Platz neben dem Schlusstext
        qrBeside = true;
        DrawQr(pdf, qrCode, R - qrSize, ty + 10, qrMod);
        pdf.Text(R - qrSize, ty + 10 - qrSize - 10, 8, true, L"GiroCode", 0.2);
        pdf.Text(R - qrSize, ty + 10 - qrSize - 19, 7, false, L"mit der Banking-App scannen", 0.35);
    }
    for (const auto& line : Wrap(closing, R - L - (qrBeside ? qrSize + 24 : 0), 9.5)) {
        if (ty < floorY) break;
        pdf.Text(L, ty, 9.5, false, line, 0.15);
        ty -= 13;
    }
    if (d.smallBusiness && ty >= floorY) { ty -= 4; pdf.Text(L, ty, 9, true, SmallBusinessNote(), 0.2); ty -= 14; }
    if (giro && !qrBeside) {   // kein Platz neben dem Text: der GiroCode kommt oben auf eine eigene Seite
        pdf.NewPage();
        const double qy = top - 10;
        DrawQr(pdf, qrCode, R - qrSize, qy, qrMod);
        pdf.Text(R - qrSize, qy - qrSize - 10, 8, true, L"GiroCode", 0.2);
        pdf.Text(R - qrSize, qy - qrSize - 19, 7, false, L"mit der Banking-App scannen", 0.35);
    }

    // Fusszeile mit drei Spalten (auf jeder Seite): Firma / Register / Steuer | Kontakt | Bank
    if (hasFooter) {
        struct Col { std::vector<std::pair<bool, std::wstring>> lines; };
        Col c[3];
        auto add = [&](Col& col, bool bold, const std::wstring& label, const std::wstring& v) {
            if (docs::Trim(v).empty()) return;
            col.lines.push_back({bold, label + docs::Trim(v)});
        };
        add(c[0], true, L"", d.issuer);
        add(c[0], false, L"", d.owner);
        add(c[0], false, L"", d.court);
        add(c[0], false, d.taxNo.empty() ? L"" : std::wstring(TaxNoLabel(d.taxNo)) + L": ", d.taxNo);
        add(c[1], false, L"Tel.: ", d.phone);
        add(c[1], false, L"E-Mail: ", d.email);
        add(c[1], false, L"Web: ", d.web);
        add(c[2], false, L"", d.bank);
        add(c[2], false, L"IBAN: ", d.iban);
        add(c[2], false, L"BIC: ", d.bic);
        const double colW = (R - L - 2 * 14) / 3;
        for (size_t pg = 0; pg < pdf.PageCount(); ++pg) {
            pdf.SelectPage(pg);
            pdf.Line(L, 104, R, 104, 0.5, 0.75);
            for (int ci = 0; ci < 3; ++ci) {
                double fy = 92;
                for (const auto& ln : c[ci].lines)
                    for (const auto& part : Wrap(ln.second, colW, 7.5, ln.first)) {
                        if (fy < 52) break;
                        pdf.Text(L + ci * (colW + 14), fy, 7.5, ln.first, part, 0.3);
                        fy -= 9.5;
                    }
            }
        }
        pdf.SelectLast();
    }

    pdf.Footer(L"Erstellt mit Oggys Docs-Files");
    return pdf.Build(d.number.empty() ? L"Rechnung" : std::wstring(credit ? L"Gutschrift " : L"Rechnung ") + d.number, eInvoiceXml);
}

}  // namespace invoice
