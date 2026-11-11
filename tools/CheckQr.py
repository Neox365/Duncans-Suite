"""Liest die vom QrTest geschriebenen 0/1-Matrizen mit einem unabhaengigen QR-Decoder (zxing-cpp) und vergleicht den Inhalt.
Aufruf:  python CheckQr.py <Ordner mit qr_*.txt>
         python CheckQr.py --pdf <Ordner mit giro_*.pdf>   (liest den GiroCode aus den vom GiroTest erzeugten Rechnungs-PDFs)
Einmalig:  python -m venv venv ; venv\\Scripts\\pip install zxing-cpp pillow numpy
"""
import glob
import os
import sys

import numpy as np
import zxingcpp
from PIL import Image


def decode(matrix, scale=8, quiet=4):
    n = len(matrix)
    img = np.full(((n + 2 * quiet) * scale,) * 2, 255, dtype=np.uint8)
    for y, row in enumerate(matrix):
        for x, ch in enumerate(row):
            if ch == "1":
                img[(y + quiet) * scale:(y + quiet + 1) * scale, (x + quiet) * scale:(x + quiet + 1) * scale] = 0
    res = zxingcpp.read_barcodes(Image.fromarray(img))
    return res[0].text if res else None


def main(folder):
    bad = 0
    files = sorted(glob.glob(os.path.join(folder, "qr_*.txt")), key=lambda p: int(os.path.basename(p)[3:-4]))
    for path in files:
        lines = open(path, encoding="utf-8").read().split("\n")
        expect, matrix = lines[0], [l for l in lines[1:] if l]
        got = decode(matrix)
        ok = got == expect
        bad += 0 if ok else 1
        print(f"{os.path.basename(path):12s} {len(matrix)}x{len(matrix)}  {'OK' if ok else 'FEHLER: ' + repr(got)}")
    print(f"{len(files)} Codes, {bad} Fehler")
    return 1 if bad or not files else 0


# Erwartete Betraege der Beispielrechnungen aus docs/tests/GiroTest.cpp (netto + Steuer, in EUR)
EXPECT_PDF = {
    "giro_einfach.pdf": ("EUR1331.37", "Mustermann Webdesign", "BYLADEM1001", "Rechnung RE-2026-001"),
    "giro_mehrere_saetze.pdf": ("EUR1243.50", "Mustermann Webdesign", "BYLADEM1001", "Rechnung RE-2026-001"),
    "giro_viele_positionen.pdf": ("EUR362.18", "Mustermann Webdesign", "BYLADEM1001", "Rechnung RE-2026-001"),
    "giro_ohne_bic.pdf": ("EUR1331.37", "Mustermann Webdesign", "", "Rechnung RE-2026-002"),
}


def read_pdf_qr(path, mod=2.4, scale=6, quiet=4):
    """Liest die schwarzen Rechtecke ("g ... re f" mit Grauwert 0) aus den unkomprimierten Seitenstroemen und setzt sie zum Bild zusammen."""
    import re
    data = open(path, "rb").read().decode("latin-1")
    rects = [tuple(map(float, m.groups())) for m in re.finditer(r"(?<![\d.])0\.000 g ([\d.\-]+) ([\d.\-]+) ([\d.\-]+) ([\d.\-]+) re f", data)]
    if not rects:
        return None
    x0 = min(r[0] for r in rects)
    y1 = max(r[1] + r[3] for r in rects)
    x1 = max(r[0] + r[2] for r in rects)
    y0 = min(r[1] for r in rects)
    w, h = int(round((x1 - x0) / mod)), int(round((y1 - y0) / mod))
    img = np.full(((h + 2 * quiet) * scale, (w + 2 * quiet) * scale), 255, dtype=np.uint8)
    for x, y, rw, rh in rects:
        c0 = int(round((x - x0) / mod)); c1 = int(round((x + rw - x0) / mod))
        r0 = int(round((y1 - (y + rh)) / mod)); r1 = int(round((y1 - y) / mod))
        img[(r0 + quiet) * scale:(r1 + quiet) * scale, (c0 + quiet) * scale:(c1 + quiet) * scale] = 0
    res = zxingcpp.read_barcodes(Image.fromarray(img))
    return (res[0].text, w, h) if res else None


def main_pdf(folder):
    bad = 0
    for path in sorted(glob.glob(os.path.join(folder, "giro_*.pdf"))):
        name = os.path.basename(path)
        got = read_pdf_qr(path)
        exp = EXPECT_PDF.get(name)
        if not got:
            print(f"{name:28s} FEHLER: kein QR-Code lesbar"); bad += 1; continue
        text, w, h = got
        lines = text.split("\n")
        ok = (len(lines) >= 11 and lines[:4] == ["BCD", "002", "1", "SCT"] and lines[4] == exp[2] and lines[5] == exp[1]
              and lines[6] == "DE02120300000000202051" and lines[7] == exp[0] and lines[10] == exp[3])
        print(f"{name:28s} {w}x{h} Module  {'OK' if ok else 'FEHLER: ' + repr(text)}")
        bad += 0 if ok else 1
    print(f"{bad} Fehler")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main_pdf(sys.argv[2]) if sys.argv[1] == "--pdf" else main(sys.argv[1]))
