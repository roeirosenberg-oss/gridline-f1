#!/usr/bin/env python3
"""Convert docs/*.md into standalone HTML pages
and build preview/index.html from the simulator screenshots.

    python3 tools/build_docs.py      (requires: pip install markdown)
"""
import glob
import html
import os
import re

import markdown

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DOCS = os.path.join(ROOT, "docs")
PREVIEW = os.path.join(ROOT, "preview")

CSS = """
:root{--bg:#ffffff;--fg:#14171c;--dim:#5b6573;--line:#e3e6eb;--card:#f6f7f9;--acc:#e5352b;--code:#eef0f3}
@media (prefers-color-scheme: dark){:root{--bg:#0b0d11;--fg:#eef1f5;--dim:#9aa3b1;--line:#262c37;--card:#141922;--acc:#ff4a3d;--code:#1b212c}}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--fg);font:16px/1.65 "Segoe UI",system-ui,-apple-system,Arial,sans-serif}
main{max-width:920px;margin:0 auto;padding:28px 18px 64px}
h1{font-size:28px;margin:0 0 8px;border-bottom:3px solid var(--acc);padding-bottom:8px}
h2{font-size:21px;margin:34px 0 10px;padding-top:6px}
h3{font-size:17px;margin:24px 0 8px}
a{color:var(--acc)}
code,pre{font-family:Consolas,"Cascadia Mono",monospace;background:var(--code);border-radius:5px;direction:ltr;unicode-bidi:isolate}
code{padding:1px 5px;font-size:.92em}
pre{padding:12px;overflow:auto;text-align:left}
pre code{background:none;padding:0}
table{border-collapse:collapse;width:100%;margin:12px 0;font-size:15px;display:block;overflow-x:auto}
th,td{border:1px solid var(--line);padding:7px 10px;vertical-align:top}
th{background:var(--card);text-align:start}
blockquote{margin:14px 0;padding:10px 14px;background:var(--card);border-inline-start:4px solid var(--acc);border-radius:6px}
blockquote p{margin:0}
hr{border:0;border-top:1px solid var(--line);margin:28px 0}
.nav{font-size:14px;color:var(--dim);margin-bottom:18px}
.gallery{display:grid;grid-template-columns:repeat(auto-fill,minmax(300px,1fr));gap:18px}
figure{margin:0;background:var(--card);border:1px solid var(--line);border-radius:10px;padding:10px}
figure img{width:100%;image-rendering:pixelated;border-radius:6px;display:block;background:#000}
figcaption{font-size:14px;color:var(--dim);margin-top:8px}
"""

PAGE = """<!doctype html>
<html lang="{lang}" dir="{dir}"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>{title}</title><style>{css}</style></head>
<body><main>{nav}{body}</main></body></html>
"""

NAV = ('<div class="nav"><a href="{p}START-HERE.html">Quick start</a> · <a href="{d}GUIDE.html">Guide</a> · '
       '<a href="{d}ARCHITECTURE.html">Architecture</a> · <a href="{d}VALIDATION.html">Test report</a> · '
       '<a href="{d}CA-CERTIFICATES.html">Certificates</a> · <a href="{pr}index.html">Screenshots</a></div>')


def convert(md_path, out_path, rtl, nav):
    text = open(md_path, encoding="utf-8").read()
    body = markdown.markdown(text, extensions=["tables", "fenced_code", "sane_lists"])
    # internal .md links -> .html
    body = re.sub(r'href="([^"]+)\.md"', r'href="\1.html"', body)
    title = re.search(r"^# (.+)$", text, re.M).group(1)
    with open(out_path, "w", encoding="utf-8") as f:
        f.write(PAGE.format(lang="he" if rtl else "en", dir="rtl" if rtl else "ltr", title=html.escape(title),
                            css=CSS, nav=nav, body=body))


CAPTIONS = {
    "00_splash": "Splash screen",
    "01_next_race": "Next race – countdown, start time in Israel time, next session, favorite driver",
    "02_next_weekend_schedule": "Weekend schedule (tap the countdown)",
    "03_drivers_p1": "Driver standings – favorite driver highlighted",
    "04_drivers_p2": "Driver standings – page 2",
    "05_constructors": "Constructor standings with a relative points bar",
    "06_last_race_results_p1": "Last race results",
    "07_last_race_results_p3": "Results – including retirements (DNF)",
    "08_calendar": "Season calendar – next race marked NEXT",
    "09_race_detail": "Race details with all sessions",
    "10_settings_p1": "Settings",
    "11_settings_p2": "Settings – page 2",
    "12_night_dimming": "Night dimming",
    "13_favorite_picker": "Favorite driver picker",
    "14_about_p1": "About – status of the data sources",
    "15_about_p2": "About – memory, TLS, self-test",
    "16_dialog": "Confirmation dialog",
    "17_next_live": "Race in progress (LIVE)",
    "18_next_same_day": "On race day – hours/minutes/seconds",
    "19_season_complete": "End of season – champion, and next season's calendar not yet published",
    "20_results_pending_notice": "Results of a newer race not published yet",
    "21_offline_cached": "No internet – cached data with the update time",
    "22_waiting_for_data": "Waiting for data",
    "23_wifi_not_set_up": "Wi‑Fi not set up",
    "24_next_cyan_accent": "Different accent color",
    "25_wifi_setup": "Wi‑Fi setup screen (sample name and password; the device creates a new password each time)",
}


def gallery():
    all_shots = sorted(p for p in glob.glob(os.path.join(PREVIEW, "screens", "*.png")) if "@2x" not in p)

    def figs(portrait):
        out = []
        for p in all_shots:
            name = os.path.splitext(os.path.basename(p))[0]
            if name.startswith("p") != portrait:
                continue
            cap = CAPTIONS.get(name[1:] if portrait else name, name)
            out.append(f'<figure><img src="screens/{name}@2x.png" alt="{html.escape(cap)}" loading="lazy">'
                       f'<figcaption>{html.escape(cap)}</figcaption></figure>')
        return out

    land, port = figs(False), figs(True)
    body = ("<h1>Gridline F1 – Screenshots</h1>"
            "<blockquote><p>These screenshots were made by running the firmware's real UI code against a display "
            "simulator on the PC (scaled 2×), with real API responses from the 2023 season and a simulated 2023 clock. "
            "This is <b>not</b> a photo of the physical board – colors on the real screen may differ slightly.</p></blockquote>"
            '<h2>Portrait (240×320)</h2><div class="gallery portrait">' + "".join(port) + "</div>"
            '<h2>Landscape (320×240)</h2><div class="gallery">' + "".join(land) + "</div>")
    nav = NAV.format(p="../", d="../docs/", pr="")
    css = CSS + ".gallery.portrait{grid-template-columns:repeat(auto-fill,minmax(220px,1fr))}"
    with open(os.path.join(PREVIEW, "index.html"), "w", encoding="utf-8") as f:
        f.write(PAGE.format(lang="en", dir="ltr", title="Gridline F1 – Screenshots", css=css, nav=nav, body=body))
    return len(land) + len(port)


def main():
    for md in sorted(glob.glob(os.path.join(DOCS, "*.md"))):
        name = os.path.splitext(os.path.basename(md))[0]
        if name == "START-HERE":
            nav = NAV.format(p="", d="docs/", pr="preview/")
        elif name == "THIRD-PARTY-NOTICES":
            nav = ""
        else:
            nav = NAV.format(p="../", d="", pr="../preview/")
        convert(md, os.path.join(DOCS, name + ".html"), False, nav)
        print("html:", name)
    print("gallery:", gallery(), "screens")


if __name__ == "__main__":
    main()
