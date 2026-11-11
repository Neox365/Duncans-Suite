#pragma once
// E-Rechnung erzeugen: Factur-X / ZUGFeRD (Profil EN 16931, UN/CEFACT CII) aus den Rechnungsdaten.
// Das XML wird ins PDF eingebettet (BuildEInvoicePdf) und kann auch einzeln gespeichert werden (BuildEInvoiceXml).
// Ausgestellt wird immer ein Inlandsumsatz in Euro an einen Empfaenger in Deutschland; Steuerkategorien: S (Regelsatz), Z (0 %), E (Kleinunternehmer § 19 UStG).
// Nicht enthalten: Leitweg-ID (XRechnung fuer Behoerden), Lieferdatum/Leistungszeitraum, Rabatte auf Belegebene, Auslandsadressen.
// Das Ergebnis ist nicht gegen den offiziellen KoSIT-Pruefer validiert (siehe Test: nur Aufbau und Summen).
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "Invoice.h"

namespace invoice {

namespace fx {

inline std::string Esc(const std::wstring& w)
{
    std::string out;
    for (char c : docs::WideToUtf8(w)) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default:
                if (static_cast<unsigned char>(c) >= 0x20 || c == '\n' || c == '\t') out.push_back(c);   // Steuerzeichen sind in XML nicht erlaubt
        }
    }
    return out;
}

inline std::string Money(long long cents)
{
    char buf[48];
    std::snprintf(buf, sizeof buf, "%s%lld.%02lld", cents < 0 ? "-" : "", std::llabs(cents) / 100, std::llabs(cents) % 100);
    return buf;
}

// Menge mit bis zu 4 Nachkommastellen, ohne ueberfluessige Nullen
inline std::string Qty(double q)
{
    char buf[48];
    std::snprintf(buf, sizeof buf, "%.4f", q);
    std::string s = buf;
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    return s.empty() || s == "-0" ? "0" : s;
}

inline std::string Date(const std::wstring& iso)   // 2026-11-11 -> 20261111 (Format 102)
{
    std::string out;
    for (wchar_t c : iso)
        if (c >= L'0' && c <= L'9') out.push_back(static_cast<char>(c));
    return out;
}

inline bool IsoOk(const std::wstring& iso) { return iso.size() == 10 && iso[4] == L'-' && iso[7] == L'-'; }

struct Addr { std::wstring street, zip, city; };

// Letzte Zeile "12345 Ort" -> PLZ/Ort, davor Strasse (alle weiteren Zeilen mit ", " verbunden). false, wenn die Adresse nicht so aufgebaut ist.
inline bool ParseAddress(const std::wstring& raw, Addr& a)
{
    const std::vector<std::wstring> lines = SplitLines(raw);
    if (lines.size() < 2) return false;
    const std::wstring& last = lines.back();
    size_t i = 0;
    while (i < last.size() && last[i] >= L'0' && last[i] <= L'9') ++i;
    if (i != 5 || i >= last.size() || last[i] != L' ') return false;   // deutsche Postleitzahl: 5 Ziffern
    a.zip = last.substr(0, 5);
    a.city = docs::Trim(last.substr(6));
    a.street.clear();
    for (size_t k = 0; k + 1 < lines.size(); ++k) a.street += (k ? L", " : L"") + lines[k];
    return !a.city.empty() && !a.street.empty();
}

inline std::string AddrXml(const Addr& a)
{
    return "<ram:PostalTradeAddress><ram:PostcodeCode>" + Esc(a.zip) + "</ram:PostcodeCode><ram:LineOne>" + Esc(a.street) + "</ram:LineOne><ram:CityName>" + Esc(a.city) +
           "</ram:CityName><ram:CountryID>DE</ram:CountryID></ram:PostalTradeAddress>";
}

}  // namespace fx

// Leer, wenn sich aus den Daten eine E-Rechnung bauen laesst; sonst der Grund (fuer den Nutzer)
inline std::wstring EInvoiceProblem(const Data& d)
{
    fx::Addr a;
    if (d.issuer.empty()) return L"Absender fehlt";
    if (!fx::ParseAddress(d.issuerAddress, a)) return L"Absender-Adresse braucht Straße und eine letzte Zeile „PLZ Ort“ (5 Ziffern)";
    if (d.taxNo.empty()) return L"Steuernummer oder USt-IdNr. fehlt";
    if (d.recipient.empty()) return L"Empfänger fehlt";
    if (!fx::ParseAddress(d.recipientAddress, a)) return L"Empfänger-Adresse braucht Straße und eine letzte Zeile „PLZ Ort“ (5 Ziffern)";
    if (d.number.empty()) return L"Rechnungsnummer fehlt";
    if (!fx::IsoOk(d.dateIso)) return L"Rechnungsdatum fehlt";
    if (d.items.empty()) return L"keine Positionen";
    for (const Item& it : d.items)
        if (docs::Trim(it.desc).empty()) return L"eine Position hat keine Bezeichnung";
    return std::wstring();
}

// CII-XML (UTF-8) im Profil EN 16931; leer, wenn EInvoiceProblem(d) nicht leer ist
inline std::string BuildEInvoiceXml(const Data& d)
{
    if (!EInvoiceProblem(d).empty()) return std::string();
    using namespace fx;
    Addr sa, ra;
    ParseAddress(d.issuerAddress, sa);
    ParseAddress(d.recipientAddress, ra);
    const Totals t = Compute(d);
    std::string x;
    x += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    x += "<rsm:CrossIndustryInvoice xmlns:rsm=\"urn:un:unece:uncefact:data:standard:CrossIndustryInvoice:100\" "
         "xmlns:ram=\"urn:un:unece:uncefact:data:standard:ReusableAggregateBusinessInformationEntity:100\" "
         "xmlns:udt=\"urn:un:unece:uncefact:data:standard:UnqualifiedDataType:100\" "
         "xmlns:qdt=\"urn:un:unece:uncefact:data:standard:QualifiedDataType:100\" "
         "xmlns:xs=\"http://www.w3.org/2001/XMLSchema\">\n";
    x += "<rsm:ExchangedDocumentContext><ram:GuidelineSpecifiedDocumentContextParameter><ram:ID>urn:cen.eu:en16931:2017</ram:ID>"
         "</ram:GuidelineSpecifiedDocumentContextParameter></rsm:ExchangedDocumentContext>\n";
    x += "<rsm:ExchangedDocument><ram:ID>" + Esc(d.number) + "</ram:ID><ram:TypeCode>380</ram:TypeCode><ram:IssueDateTime><udt:DateTimeString format=\"102\">" +
         Date(d.dateIso) + "</udt:DateTimeString></ram:IssueDateTime>";
    x += "</rsm:ExchangedDocument>\n<rsm:SupplyChainTradeTransaction>\n";

    const char* lineCat = d.smallBusiness ? "E" : nullptr;
    for (size_t i = 0; i < d.items.size(); ++i) {
        const Item& it = d.items[i];
        double qty = it.qty;
        long long unit = it.unitCents;
        if (unit < 0) { unit = -unit; qty = -qty; }   // Einzelpreis darf nicht negativ sein (BR-27): Abzuege als negative Menge
        const int pct = d.smallBusiness ? 0 : ItemVat(d, it);
        const char* cat = lineCat ? lineCat : (pct == 0 ? "Z" : "S");
        x += "<ram:IncludedSupplyChainTradeLineItem><ram:AssociatedDocumentLineDocument><ram:LineID>" + std::to_string(i + 1) + "</ram:LineID></ram:AssociatedDocumentLineDocument>";
        x += "<ram:SpecifiedTradeProduct><ram:Name>" + Esc(docs::Trim(it.desc)) + "</ram:Name></ram:SpecifiedTradeProduct>";
        x += "<ram:SpecifiedLineTradeAgreement><ram:NetPriceProductTradePrice><ram:ChargeAmount>" + Money(unit) + "</ram:ChargeAmount></ram:NetPriceProductTradePrice></ram:SpecifiedLineTradeAgreement>";
        x += "<ram:SpecifiedLineTradeDelivery><ram:BilledQuantity unitCode=\"C62\">" + Qty(qty) + "</ram:BilledQuantity></ram:SpecifiedLineTradeDelivery>";
        x += std::string("<ram:SpecifiedLineTradeSettlement><ram:ApplicableTradeTax><ram:TypeCode>VAT</ram:TypeCode><ram:CategoryCode>") + cat + "</ram:CategoryCode><ram:RateApplicablePercent>" +
             std::to_string(pct) + "</ram:RateApplicablePercent></ram:ApplicableTradeTax><ram:SpecifiedTradeSettlementLineMonetarySummation><ram:LineTotalAmount>" + Money(t.lines[i]) +
             "</ram:LineTotalAmount></ram:SpecifiedTradeSettlementLineMonetarySummation></ram:SpecifiedLineTradeSettlement></ram:IncludedSupplyChainTradeLineItem>\n";
    }

    // Vertragsparteien
    x += "<ram:ApplicableHeaderTradeAgreement><ram:SellerTradeParty><ram:Name>" + Esc(d.issuer) + "</ram:Name>";
    if (!d.phone.empty() || !d.email.empty()) {
        x += "<ram:DefinedTradeContact><ram:PersonName>" + Esc(d.owner.empty() ? d.issuer : d.owner) + "</ram:PersonName>";
        if (!d.phone.empty()) x += "<ram:TelephoneUniversalCommunication><ram:CompleteNumber>" + Esc(d.phone) + "</ram:CompleteNumber></ram:TelephoneUniversalCommunication>";
        if (!d.email.empty()) x += "<ram:EmailURIUniversalCommunication><ram:URIID>" + Esc(d.email) + "</ram:URIID></ram:EmailURIUniversalCommunication>";
        x += "</ram:DefinedTradeContact>";
    }
    x += AddrXml(sa);
    if (!d.email.empty()) x += "<ram:URIUniversalCommunication><ram:URIID schemeID=\"EM\">" + Esc(d.email) + "</ram:URIID></ram:URIUniversalCommunication>";
    const bool vatId = std::wstring(TaxNoLabel(d.taxNo)) == L"USt-IdNr.";
    x += std::string("<ram:SpecifiedTaxRegistration><ram:ID schemeID=\"") + (vatId ? "VA" : "FC") + "\">" + Esc(d.taxNo) + "</ram:ID></ram:SpecifiedTaxRegistration></ram:SellerTradeParty>";
    x += "<ram:BuyerTradeParty><ram:Name>" + Esc(d.recipient) + "</ram:Name>" + AddrXml(ra) + "</ram:BuyerTradeParty></ram:ApplicableHeaderTradeAgreement>\n";
    x += "<ram:ApplicableHeaderTradeDelivery/>\n";

    // Zahlung und Summen
    x += "<ram:ApplicableHeaderTradeSettlement><ram:PaymentReference>" + Esc(d.number) + "</ram:PaymentReference><ram:InvoiceCurrencyCode>EUR</ram:InvoiceCurrencyCode>";
    if (giro::IbanValid(d.iban)) {
        x += "<ram:SpecifiedTradeSettlementPaymentMeans><ram:TypeCode>58</ram:TypeCode><ram:PayeePartyCreditorFinancialAccount><ram:IBANID>" + Esc(giro::NormalizeIban(d.iban)) +
             "</ram:IBANID></ram:PayeePartyCreditorFinancialAccount>";
        if (!d.bic.empty()) x += "<ram:PayeeSpecifiedCreditorFinancialInstitution><ram:BICID>" + Esc(d.bic) + "</ram:BICID></ram:PayeeSpecifiedCreditorFinancialInstitution>";
        x += "</ram:SpecifiedTradeSettlementPaymentMeans>";
    }
    if (d.smallBusiness) {
        x += "<ram:ApplicableTradeTax><ram:CalculatedAmount>0.00</ram:CalculatedAmount><ram:TypeCode>VAT</ram:TypeCode><ram:ExemptionReason>" + Esc(SmallBusinessNote()) +
             "</ram:ExemptionReason><ram:BasisAmount>" + Money(t.net) + "</ram:BasisAmount><ram:CategoryCode>E</ram:CategoryCode><ram:RateApplicablePercent>0</ram:RateApplicablePercent></ram:ApplicableTradeTax>";
    } else {
        for (const VatLine& v : t.vatLines)
            x += "<ram:ApplicableTradeTax><ram:CalculatedAmount>" + Money(v.vat) + "</ram:CalculatedAmount><ram:TypeCode>VAT</ram:TypeCode><ram:BasisAmount>" + Money(v.net) +
                 "</ram:BasisAmount><ram:CategoryCode>" + (v.percent == 0 ? "Z" : "S") + "</ram:CategoryCode><ram:RateApplicablePercent>" + std::to_string(v.percent) + "</ram:RateApplicablePercent></ram:ApplicableTradeTax>";
    }
    x += "<ram:SpecifiedTradePaymentTerms>";
    if (fx::IsoOk(d.dueIso)) x += "<ram:DueDateDateTime><udt:DateTimeString format=\"102\">" + Date(d.dueIso) + "</udt:DateTimeString></ram:DueDateDateTime>";
    else x += "<ram:Description>Zahlbar sofort ohne Abzug</ram:Description>";
    x += "</ram:SpecifiedTradePaymentTerms>";
    x += "<ram:SpecifiedTradeSettlementHeaderMonetarySummation><ram:LineTotalAmount>" + Money(t.net) + "</ram:LineTotalAmount><ram:TaxBasisTotalAmount>" + Money(t.net) +
         "</ram:TaxBasisTotalAmount><ram:TaxTotalAmount currencyID=\"EUR\">" + Money(t.vat) + "</ram:TaxTotalAmount><ram:GrandTotalAmount>" + Money(t.gross) +
         "</ram:GrandTotalAmount><ram:DuePayableAmount>" + Money(t.gross) + "</ram:DuePayableAmount></ram:SpecifiedTradeSettlementHeaderMonetarySummation>";
    x += "</ram:ApplicableHeaderTradeSettlement>\n</rsm:SupplyChainTradeTransaction>\n</rsm:CrossIndustryInvoice>\n";
    return x;
}

}  // namespace invoice
