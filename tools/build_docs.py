#!/usr/bin/env python3
"""Convert docs/*.md into standalone HTML pages (Hebrew pages right-to-left)
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

NAV_HE = ('<div class="nav"><a href="{p}START-HERE.html">התחלה מהירה</a> · <a href="{d}HEBREW-GUIDE.html">מדריך</a> · '
          '<a href="{d}ARCHITECTURE.html">ארכיטקטורה</a> · <a href="{d}VALIDATION.html">דוח בדיקות</a> · '
          '<a href="{d}CA-CERTIFICATES.html">תעודות</a> · <a href="{pr}index.html">צילומי מסך</a></div>')


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
    "00_splash": "מסך פתיחה",
    "01_next_race": "המרוץ הבא – ספירה לאחור, שעת זינוק בשעון ישראל, הסשן הבא, הנהג המועדף",
    "02_next_weekend_schedule": "לוח הזמנים של סוף השבוע (נגיעה בספירה)",
    "03_drivers_p1": "טבלת נהגים – הנהג המועדף מודגש",
    "04_drivers_p2": "טבלת נהגים – עמוד 2",
    "05_constructors": "טבלת קבוצות עם פס נקודות יחסי",
    "06_last_race_results_p1": "תוצאות המרוץ האחרון",
    "07_last_race_results_p3": "תוצאות – כולל פרישה (DNF)",
    "08_calendar": "לוח העונה – המרוץ הבא מסומן NEXT",
    "09_race_detail": "פרטי מרוץ עם כל הסשנים",
    "10_settings_p1": "הגדרות",
    "11_settings_p2": "הגדרות – עמוד 2",
    "12_night_dimming": "עמעום לילה",
    "13_favorite_picker": "בחירת נהג מועדף",
    "14_about_p1": "About – מצב מקורות הנתונים",
    "15_about_p2": "About – זיכרון, TLS, בדיקה עצמית",
    "16_dialog": "דיאלוג אישור",
    "17_next_live": "מרוץ בזמן אמת (LIVE)",
    "18_next_same_day": "ביום המרוץ – שעות/דקות/שניות",
    "19_season_complete": "סוף העונה – אלוף ולוח עונה הבאה שטרם פורסם",
    "20_results_pending_notice": "תוצאות של מרוץ חדש יותר עוד לא פורסמו",
    "21_offline_cached": "בלי אינטרנט – נתונים שמורים עם שעת העדכון",
    "22_waiting_for_data": "ממתין לנתונים",
    "23_wifi_not_set_up": "Wi‑Fi לא הוגדר",
    "24_next_cyan_accent": "צבע הדגשה אחר",
    "25_wifi_setup": "מסך הגדרת Wi‑Fi (שם וסיסמה לדוגמה; המכשיר יוצר סיסמה חדשה בכל פעם)",
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
    body = ("<h1>Gridline F1 – צילומי מסך</h1>"
            "<blockquote><p>הצילומים נוצרו על ידי הרצת קוד הממשק האמיתי של הקושחה מול סימולטור תצוגה במחשב "
            "(מוגדל פי 2), עם תשובות API אמיתיות של עונת 2023 ושעון מדומה ב‑2023. "
            "זה <b>לא</b> צילום של הלוח הפיזי – הגוונים במסך האמיתי יכולים להיות מעט שונים.</p></blockquote>"
            '<h2>לאורך (240×320)</h2><div class="gallery portrait">' + "".join(port) + "</div>"
            '<h2>לרוחב (320×240)</h2><div class="gallery">' + "".join(land) + "</div>")
    nav = NAV_HE.format(p="../", d="../docs/", pr="")
    css = CSS + ".gallery.portrait{grid-template-columns:repeat(auto-fill,minmax(220px,1fr))}"
    with open(os.path.join(PREVIEW, "index.html"), "w", encoding="utf-8") as f:
        f.write(PAGE.format(lang="he", dir="rtl", title="Gridline F1 – צילומי מסך", css=css, nav=nav, body=body))
    return len(land) + len(port)


def main():
    for md in sorted(glob.glob(os.path.join(DOCS, "*.md"))):
        name = os.path.splitext(os.path.basename(md))[0]
        rtl = name != "THIRD-PARTY-NOTICES"
        if name == "START-HERE":
            nav = NAV_HE.format(p="", d="docs/", pr="preview/")
        else:
            nav = NAV_HE.format(p="../", d="", pr="../preview/") if rtl else ""
        convert(md, os.path.join(DOCS, name + ".html"), rtl, nav)
        print("html:", name)
    print("gallery:", gallery(), "screens")


if __name__ == "__main__":
    main()
