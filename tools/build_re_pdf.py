# SPDX-License-Identifier: GPL-3.0-or-later
"""Build the reverse-engineering reference PDF from docs/ingenieria-inversa.md.

The PDF is a binary, so it is written under local/docs/ (the pre-commit check keeps binaries
out of git); share it from there. Needs the Python `markdown` package and Chrome or Edge.
Usage: build_re_pdf.py [SOURCE_MD] [OUTPUT_PDF]
English: build_re_pdf.py docs/en/reverse-engineering.md  (-> local/docs/MLB-PowerPros-2008-reverse-engineering.pdf)
"""
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

import markdown
from markdown.extensions.toc import slugify_unicode

ROOT = pathlib.Path(__file__).resolve().parent.parent
BROWSERS = [
    r"C:\Program Files\Google\Chrome\Application\chrome.exe",
    r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe",
    "chrome", "chromium", "msedge",
]
CSS = """
@page { size: A4; margin: 18mm 16mm; }
body { font-family: 'Segoe UI', Arial, sans-serif; font-size: 10.5pt; line-height: 1.45; color: #1b1b1b; }
h1 { font-size: 22pt; border-bottom: 3px solid #1f4e79; padding-bottom: 4px; }
h2 { font-size: 15pt; color: #1f4e79; border-bottom: 1px solid #c8d3df; margin-top: 26px; page-break-after: avoid; }
h3 { font-size: 12pt; color: #2e5e8c; page-break-after: avoid; }
code { font-family: Consolas, monospace; font-size: 9.5pt; background: #f2f4f7; padding: 0 3px; }
pre { background: #f2f4f7; padding: 8px; font-size: 9pt; white-space: pre-wrap; page-break-inside: avoid; }
table { border-collapse: collapse; width: 100%; margin: 8px 0; font-size: 9.5pt; page-break-inside: auto; }
th, td { border: 1px solid #c8d3df; padding: 3px 6px; vertical-align: top; text-align: left; }
th { background: #e6edf5; }
tr { page-break-inside: avoid; }
.toc { background: #f7f9fb; border: 1px solid #dde4ec; padding: 6px 14px; }
"""


def browser() -> str:
    for candidate in BROWSERS:
        if pathlib.Path(candidate).exists() or shutil.which(candidate):
            return candidate
    sys.exit("Chrome or Edge not found")


def main() -> None:
    source = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "docs" / "ingenieria-inversa.md"
    english = source.resolve().parent.name == "en"          # docs/en/reverse-engineering.md
    name = "MLB-PowerPros-2008-reverse-engineering.pdf" if english else "MLB-PowerPros-2008-ingenieria-inversa.pdf"
    output = (pathlib.Path(sys.argv[2]) if len(sys.argv) > 2 else ROOT / "local" / "docs" / name).resolve()  # Chrome needs it absolute
    # GitHub nests lists at 2 spaces; Python-Markdown needs 4.
    text = re.sub(r"^( +)(?=[-*] |\d+\. )", lambda m: m[1] * 2, source.read_text(encoding="utf-8"), flags=re.M)
    # Keep accents in heading ids, as GitHub does, so the index links (#escenas-menús-…) resolve.
    body = markdown.markdown(text, extensions=["tables", "fenced_code", "toc", "sane_lists"],
                             extension_configs={"toc": {"slugify": slugify_unicode}})
    html = f"<!doctype html><html lang='{"en" if english else "es"}'><head><meta charset='utf-8'><style>{CSS}</style></head><body>{body}</body></html>"
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        page = pathlib.Path(tmp) / "reference.html"
        page.write_text(html, encoding="utf-8")
        subprocess.run([browser(), "--headless=new", "--disable-gpu", "--no-pdf-header-footer",
                        f"--user-data-dir={tmp}\\profile", f"--print-to-pdf={output}", page.as_uri()],
                       check=True, capture_output=True, timeout=120)
    print(output)


if __name__ == "__main__":
    main()
