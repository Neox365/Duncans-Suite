#pragma once
// Rechnungs-Vorlage: Daten -> Summen -> PDF (Seitenaufbau mit Absender, Rechnungskopf, Empfaenger, Positionstabelle mit
// Seitenumbruch, Summen, Zahlungshinweis). Reine Logik, getrennt testbar.
#include <cmath>
#include <string>
#include <vector>

#include "PdfWriter.h"
#include "Store.h"

namespace invoice {

struct Item {
    std::wstring desc;
    double qty = 1;
    long long unitCents = 0;
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
};

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

struct Totals {
    long long net = 0, vat = 0, gross = 0;
    std::vector<long long> lines;   // Betrag je Position in Cent
};

inline long long LineCents(const Item& it)
{
    return static_cast<long long>(std::llround(it.qty * static_cast<double>(it.unitCents)));
}

inline Totals Compute(const Data& d)
{
    Totals t;
    for (const Item& it : d.items) {
        t.lines.push_back(LineCents(it));
        t.net += t.lines.back();
    }
    t.vat = d.smallBusiness ? 0 : static_cast<long long>(std::llround(static_cast<double>(t.net) * d.vatPercent / 100.0));
    t.gross = t.net + t.vat;
    return t;
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

inline std::string BuildPdf(const Data& d)
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
    pdf.TextRight(R, top - 2, 24, true, L"RECHNUNG", 0.15);
    const double lx = 390;
    double ky = top - 30;
    auto kv = [&](const wchar_t* k, const std::wstring& v) {
        if (v.empty()) return;
        pdf.Text(lx, ky, 10, false, k, 0.4);
        pdf.TextRight(R, ky, 10, true, v);
        ky -= 14;
    };
    kv(L"Rechnungsnr.", d.number);
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
    const double cPos = L + 4, cDesc = L + 30, cQty = 360, cUnit = 450, cSum = R - 4;
    auto header = [&](double hy) {
        pdf.FillRect(L, hy - 6, R - L, 20, 0.93);
        pdf.Text(cPos, hy, 9, true, L"Pos.");
        pdf.Text(cDesc, hy, 9, true, L"Beschreibung");
        pdf.TextRight(cQty, hy, 9, true, L"Menge");
        pdf.TextRight(cUnit, hy, 9, true, L"Einzelpreis");
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
        pdf.Text(cUnit - 70, ty, 10, false, L"Umsatzsteuer " + std::to_wstring(d.vatPercent) + L" %", 0.3);
        pdf.TextRight(cSum, ty, 10, false, docs::FormatAmount(tot.vat));
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
    for (const auto& line : Wrap(closing, R - L, 9.5)) {
        if (ty < floorY) break;
        pdf.Text(L, ty, 9.5, false, line, 0.15);
        ty -= 13;
    }
    if (d.smallBusiness && ty >= floorY) { ty -= 4; pdf.Text(L, ty, 9, true, SmallBusinessNote(), 0.2); ty -= 14; }

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
    return pdf.Build(d.number.empty() ? L"Rechnung" : L"Rechnung " + d.number);
}

}  // namespace invoice
