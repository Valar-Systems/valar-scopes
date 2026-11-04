"""PD7 (docs/wx-opuk.md): #525's /credits, counted as pinned. Reads a page (a URL or a file), takes the photo-credits
section, and counts each row's OWN licence -- the first label after ") &middot; " -- never raw occurrences (a BY-SA row
prints its link twice; OPERA's line holds "CC BY 4.0"; "CC BY-SA 3.0" prefixes "CC BY-SA 3.0 AU").

Usage: python -I credits_pd7.py <url-or-file>
"""
import json
import re
import sys
import urllib.request

PREDICTED = {"CC BY-SA 4.0": 48, "CC BY-SA 2.0": 36, "CC BY-SA 3.0": 18, "CC BY-SA 3.0 AU": 12, "CC BY 2.0": 32,
             "CC BY 4.0": 24, "CC BY 3.0": 3, "CC BY 2.5": 2, "Open Government Licence v1.0": 1}
CLAUSE = "this adapted image is available under the same licence,"

src = sys.argv[1]
if src.startswith("http"):
    with urllib.request.urlopen(urllib.request.Request(src, headers={"User-Agent": "uk-data-pd7/1"}), timeout=30) as r:
        page = r.read().decode("utf-8")
else:
    page = open(src, encoding="utf-8").read()

at = page.find("<h2>Photo credits</h2>")
if at < 0:
    raise SystemExit("BLIND: no photo-credits section")
rows = re.findall(r"<li>(.*?)</li>", page[at:], re.S)
counts, sa_rows, sa_own, other = {}, 0, 0, {}
for row in rows:
    m = re.search(r"\) &middot; (?:<a href=\"[^\"]*\" rel=\"noopener\">([^<]+)</a>|([^&<;]+?)(?: &mdash;|;|<| &middot;|$))", row)
    label = (m.group(1) or m.group(2) or "").strip() if m else "UNPARSED"
    if label in PREDICTED:
        counts[label] = counts.get(label, 0) + 1
    else:
        other[label] = other.get(label, 0) + 1
    if CLAUSE in row:
        sa_rows += 1
        tail = row.split(CLAUSE, 1)[1]
        n = re.search(r"<a href=\"[^\"]*\" rel=\"noopener\">([^<]+)</a>", tail)
        if n and n.group(1) == label:
            sa_own += 1
out = {"rows": len(rows), "licensed": sum(counts.values()), "by_label": counts, "other_labels": other,
       "adsbdb_anywhere": page.count("adsbdb"),
       "standing_data_line": page.count('Route data from <a href="https://github.com/vradarserver/standing-data"'),   # the line is matched by its own anchor: the template links the name
       "sharealike_clause_rows": sa_rows, "sharealike_naming_own_licence": sa_own,
       "sharealike_paragraph": page.count("ShareAlike: each CC BY-SA image here is adapted")}
out["as_predicted"] = (counts == PREDICTED and out["adsbdb_anywhere"] == 0 and out["standing_data_line"] == 1
                       and sa_rows == 114 and sa_own == 114 and out["sharealike_paragraph"] == 1)
print(json.dumps(out, indent=1))
