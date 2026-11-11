"""Gegenprobe der erzeugten E-Rechnungen mit der unabhaengigen Bibliothek factur-x: Pruefung gegen die offiziellen XSD-Schemas (EN 16931) und
Lesen des PDF-Anhangs. Die Geschaeftsregeln (Schematron, KoSIT) werden damit NICHT geprueft.
Aufruf:  python tools/CheckFacturX.py ordner   (Ordner mit rechnung.xml, rechnung.pdf, klein.xml, gutschrift.xml: docs/tests/FacturXTest.exe ordner)
Braucht: pip install factur-x"""
import sys, os
import facturx
d = sys.argv[1]
for name in ("rechnung.xml", "klein.xml", "gutschrift.xml", "leistung1.xml", "leistung2.xml"):
    facturx.xml_check_xsd(open(os.path.join(d, name), "rb").read(), flavor="factur-x", level="en16931")
    print(name, "entspricht dem Factur-X-Schema (EN 16931)")
fname, xml = facturx.get_xml_from_pdf(open(os.path.join(d, "rechnung.pdf"), "rb"), check_xsd=True)
print("PDF-Anhang gelesen:", fname, len(xml), "Bytes, Schema ok")
