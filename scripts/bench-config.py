#!/usr/bin/env python3
"""Drive a Blipscope config page the way a BROWSER does, with guards.

WHY THIS EXISTS (2026-09-10). An ad-hoc script posted the config form to two
bench boards and silently turned off ALL 43 toggles on each. The board stopped
dimming at night, which voided gate B3 -- a person was ready to watch a reboot
on a panel that was never dim.

The mechanism matters, because the mitigation was already in place and was
wrong:

  * a PARTIAL post is unsafe: SaveToggle() writes "false" for any absent
    checkbox when the cfg-form marker is present. This was known, and a
    whole-form post was chosen specifically to avoid it.
  * the whole-form post was built by parsing the rendered page with an
    attribute regex that required `name=value`. The page renders a BARE
    `checked` attribute. So no checkbox was ever seen as checked, every one was
    omitted from the body, and SaveToggle wrote "false" for all 43.

A mitigation was built, believed, and never verified to do the thing it
claimed. And the disproof was printed on every single run: "posting 37 fields"
against a form with 43 checkboxes plus ~20 other inputs. The number was in the
output and nobody read it.

Hence the guards below. Neither is style; each one alone would have caught it.
"""
import argparse
import re
import sys
import urllib.parse
import urllib.request

# Attributes with an OPTIONAL value, so bare booleans (checked, selected,
# disabled) are captured. The defect this file exists for was a regex that
# required the equals sign.
ATTR = re.compile(r"([\w-]+)(?:\s*=\s*('[^']*'|\"[^\"]*\"|[^\s>]+))?")
TAG = re.compile(r"<(input|select|textarea)\b([^>]*)>", re.I)


def attrs(blob):
    out = {}
    for m in ATTR.finditer(blob):
        raw = m.group(2)
        if raw is None:
            val = ""
        elif raw[:1] in ("'", '"'):
            val = raw[1:-1]
        else:
            val = raw
        out[m.group(1).lower()] = val
    return out


def get_page(base, timeout=25, tries=6):
    last = None
    for _ in range(tries):
        try:
            with urllib.request.urlopen(base + "/", timeout=timeout) as r:
                return r.read().decode("utf-8", "replace")
        except Exception as e:  # boards drop off wifi briefly; retry
            last = e
    raise last


def parse_form(html):
    """Return (fields, checkboxes): the browser submission set, and box states."""
    fields = []
    boxes = {}
    for m in TAG.finditer(html):
        kind = m.group(1).lower()
        a = attrs(m.group(2))
        if "name" not in a or kind in ("select", "textarea"):
            continue
        typ = a.get("type", "text")
        if typ == "checkbox":
            boxes[a["name"]] = "checked" in a
            if "checked" in a:
                fields.append((a["name"], a.get("value", "on")))
            continue
        fields.append((a["name"], a.get("value", "")))
    for m in re.finditer(r"<select\b([^>]*)>(.*?)</select>", html, re.I | re.S):
        a = attrs(m.group(1))
        if "name" not in a:
            continue
        sel = re.search(r"<option[^>]*\bselected\b[^>]*>", m.group(2), re.I)
        fields.append((a["name"], attrs(sel.group(0)).get("value", "") if sel else ""))
    for m in re.finditer(r"<textarea\b([^>]*)>(.*?)</textarea>", html, re.I | re.S):
        a = attrs(m.group(1))
        if "name" in a:
            fields.append((a["name"], m.group(2)))
    return fields, boxes


def post(base, body, timeout=30):
    data = urllib.parse.urlencode(body).encode()
    req = urllib.request.Request(
        base + "/save",
        data=data,
        headers={
            "Content-Type": "application/x-www-form-urlencoded",
            # The page's own fetch() sets this; the server rejects
            # state-changing POSTs without it as cross-origin/CSRF.
            "X-Blipscope": "1",
        },
    )
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return r.status


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", required=True)
    ap.add_argument("--show", action="store_true")
    ap.add_argument("--set", action="append", default=[], metavar="K=V")
    ap.add_argument("--check", action="append", default=[], metavar="NAME")
    ap.add_argument("--uncheck", action="append", default=[], metavar="NAME")
    ap.add_argument("--toggle-set", metavar="a,b,c",
                    help="make EXACTLY these checked and every other box unchecked")
    ap.add_argument("--allow-zero-checked", action="store_true",
                    help="permit a post that leaves zero boxes checked (guard 2)")
    ap.add_argument("--expect-checked", type=int, default=None,
                    help="assert this many boxes are checked AFTER the post")
    a = ap.parse_args()

    base = a.host if a.host.startswith("http") else "http://" + a.host
    html = get_page(base)
    fields, boxes = parse_form(html)
    before_on = sorted(n for n, v in boxes.items() if v)

    print("%s  %d/%d checked, %d submitted fields"
          % (base, len(before_on), len(boxes), len(fields)))
    if a.show:
        print("  ON : " + (", ".join(before_on) or "(none)"))
        print("  OFF: " + ", ".join(sorted(n for n, v in boxes.items() if not v)))
        return 0

    want = dict(boxes)
    if a.toggle_set is not None:
        keep = set(s.strip() for s in a.toggle_set.split(",") if s.strip())
        unknown = keep - set(boxes)
        if unknown:
            print("REFUSED: unknown checkbox name(s): %s" % ", ".join(sorted(unknown)))
            return 2
        want = dict((n, n in keep) for n in boxes)
    for n in a.check:
        if n not in want:
            print("REFUSED: unknown checkbox %r" % n)
            return 2
        want[n] = True
    for n in a.uncheck:
        if n not in want:
            print("REFUSED: unknown checkbox %r" % n)
            return 2
        want[n] = False

    overrides = {}
    for kv in a.set:
        k, _, v = kv.partition("=")
        overrides[k] = v

    # Rebuild the submission set from the DESIRED checkbox state.
    body = [(n, v) for n, v in fields if n not in boxes]
    body = [(n, overrides.get(n, v)) for n, v in body]
    for n, on in want.items():
        if on:
            body.append((n, "on"))

    after_want = sum(1 for v in want.values() if v)

    # ---- GUARD 2: never silently uncheck everything ------------------------
    # "uncheck all 43" is almost never what a script means, and it is exactly
    # what the 2026-09-10 defect did. Refuse unless it is said out loud.
    if after_want == 0 and len(before_on) > 0 and not a.allow_zero_checked:
        print("REFUSED: this post would leave 0/%d checked, down from %d."
              % (len(boxes), len(before_on)))
        print("  That is the shape of a parse bug, not an intention.")
        print("  Pass --allow-zero-checked if you really mean it.")
        return 3

    print("  posting %d fields; checkboxes %d -> %d"
          % (len(body), len(before_on), after_want))
    st = post(base, body)
    print("  POST /save -> HTTP %s" % st)

    # ---- GUARD 1: read the form back and assert what actually landed -------
    _, boxes2 = parse_form(get_page(base))
    after_on = sorted(n for n, v in boxes2.items() if v)
    print("  readback: %d/%d checked" % (len(after_on), len(boxes2)))

    ok = True
    if len(after_on) != after_want:
        print("  MISMATCH: intended %d checked, board reports %d"
              % (after_want, len(after_on)))
        wanted = set(n for n, v in want.items() if v)
        missing = sorted(wanted - set(after_on))
        extra = sorted(set(after_on) - wanted)
        if missing:
            print("    did not stick: %s" % ", ".join(missing))
        if extra:
            print("    unexpectedly on: %s" % ", ".join(extra))
        ok = False
    if a.expect_checked is not None and len(after_on) != a.expect_checked:
        print("  EXPECTATION FAILED: --expect-checked %d, got %d"
              % (a.expect_checked, len(after_on)))
        ok = False
    print("  RESULT: %s" % ("OK" if ok else "FAILED"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
