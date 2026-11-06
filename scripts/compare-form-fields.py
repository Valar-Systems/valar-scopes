#!/usr/bin/env python3
"""The config form's field set, compared between two served pages (docs/wx-device.md WD8d): a layout change -- such as
moving the weather settings into their own section -- must leave every control's name, type, value, checked state,
selected option and option values exactly as they were.

    python scripts/compare-form-fields.py --selftest       prove it catches a renamed field (S-ws) and its kin
    import: fields(html) -> the set; compare(a, b) -> the differences

Differences are reported by FIELD NAME and attribute only, never by value: the form holds the location and masked
secrets. Exit: 0 identical / selftest passed, 1 different / failed.
"""
from __future__ import annotations

import sys
from html.parser import HTMLParser


class _Form(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.inside, self.controls, self.sel, self.ta = False, [], None, None

    def handle_starttag(self, tag, attrs):
        a = dict(attrs)
        if tag == "form" and a.get("id") == "cfg":
            self.inside = True; return
        if not self.inside: return
        if tag == "input":
            self.controls.append({"tag": "input", "name": a.get("name"), "type": (a.get("type") or "text").lower(),
                                  "value": a.get("value", ""), "checked": "checked" in a, "options": ()})
        elif tag == "select":
            self.sel = {"tag": "select", "name": a.get("name"), "type": "select", "value": "", "checked": False, "options": []}
        elif tag == "option" and self.sel is not None:
            self.sel["options"].append((a.get("value", ""), "selected" in a))
        elif tag == "textarea":
            self.ta = {"tag": "textarea", "name": a.get("name"), "type": "textarea", "value": "", "checked": False, "options": ()}

    def handle_endtag(self, tag):
        if tag == "form" and self.inside: self.inside = False
        elif tag == "select" and self.sel is not None:
            self.sel["options"] = tuple(self.sel["options"]); self.controls.append(self.sel); self.sel = None
        elif tag == "textarea" and self.ta is not None:
            self.controls.append(self.ta); self.ta = None

    def handle_data(self, data):
        if self.ta is not None: self.ta["value"] += data


def fields(html: str) -> dict:
    """{name: (tag, type, value, checked, options)} for every NAMED control inside the form."""
    f = _Form(); f.feed(html)
    out = {}
    for c in f.controls:
        if not c["name"]: continue
        key = c["name"]
        if c["type"] == "radio": key = f"{c['name']}={c['value']}"     # one entry per radio button
        out[key] = (c["tag"], c["type"], c["value"], c["checked"], c["options"])
    return out


def compare(a_html: str, b_html: str) -> list[str]:
    a, b = fields(a_html), fields(b_html)
    diffs = [f"field '{n}' is only in the first page" for n in sorted(set(a) - set(b))]
    diffs += [f"field '{n}' is only in the second page" for n in sorted(set(b) - set(a))]
    what = ("tag", "type", "value", "checked state", "options or the selected option")
    for n in sorted(set(a) & set(b)):
        for i, w in enumerate(what):
            if a[n][i] != b[n][i]:
                diffs.append(f"field '{n}': its {w} differs")
    return diffs


def selftest() -> int:
    before = ('<form id="cfg"><div class="grid3"><label><input name="airports" type="checkbox" checked></label>'
              '<label><input name="wx-layer" type="checkbox" checked></label>'
              '<select name="wx-sweep"><option value="4" selected>4</option><option value="3">3</option></select>'
              '<label><input name="trail" type="checkbox"></label></div><input name="latitude" value="1.0"></form>')
    moved = ('<form id="cfg"><div class="grid3"><label><input name="airports" type="checkbox" checked></label>'
             '<label><input name="trail" type="checkbox"></label></div><fieldset><legend>Weather</legend><div class="grid3">'
             '<label><input name="wx-layer" type="checkbox" checked></label>'
             '<select name="wx-sweep"><option value="4" selected>4</option><option value="3">3</option></select>'
             '</div><div class="wxlegend"></div></fieldset><input name="latitude" value="1.0"></form>')
    cases = [("the same fields moved into a new section", moved, True),
             ("S-ws: a field in the new section renamed (wx-sweep -> wx-swep)", moved.replace('name="wx-sweep"', 'name="wx-swep"'), False),
             ("a checkbox's default flipped", moved.replace('name="wx-layer" type="checkbox" checked', 'name="wx-layer" type="checkbox"'), False),
             ("an option removed", moved.replace('<option value="3">3</option>', ""), False),
             ("a value changed", moved.replace('value="1.0"', 'value="1.5"'), False),
             ("a field moved OUTSIDE the form", moved.replace('<input name="latitude" value="1.0"></form>', '</form><input name="latitude" value="1.0">'), False)]
    bad = 0
    for label, page, want_same in cases:
        d = compare(before, page)
        ok = (not d) == want_same
        bad += not ok
        print(f"{'ok  ' if ok else 'FAIL'}  {'allows' if want_same else 'detects'}: {label}" + ("" if ok else f" -- {d}"))
    print("self-test PASSED" if not bad else f"self-test FAILED ({bad})")
    return 1 if bad else 0


if __name__ == "__main__":
    if sys.argv[1:2] == ["--selftest"]:
        sys.exit(selftest())
    print(__doc__)
    sys.exit(2)
