#!/usr/bin/env python3
"""The device config page's form, as the browser would post it (docs/fresh-boot-automation.md: step 6, R0-R2, K-loc).

The page posts `new FormData(form#cfg)` to /save. This builds the same body from the page's HTML, so the driver can
re-post "the page's own current values" with exactly one box left out, snapshot the board's settings before a factory
reset, and compare them after the restore.

THE BODY CARRIES THE OWNER'S SETTINGS (his location among them). It is only ever WRITTEN TO A FILE the caller names;
nothing here prints a value. Output is counts and field NAMES.

  python3 scripts/fresh-boot-form.py body <page.html> <out.body> [--drop NAME ...]
        writes the urlencoded body; prints the field count, what was dropped, and the NAMES of masked secret fields
  python3 scripts/fresh-boot-form.py compare <snapshot.body> <page.html> [--ignore NAME ...]
        the page's current body against a snapshot, field by field: "N of M fields equal", the NAMES that differ;
        exit 0 when all equal (ignored names aside), 1 otherwise
  python3 scripts/fresh-boot-form.py field-equals <page.html> NAME VALUE
        exit 0 and "equal" when the form's NAME holds VALUE, else 1 and "not equal" (never the value it holds)
  python3 scripts/fresh-boot-form.py --selftest

FormData's rules, as implemented: named, non-disabled controls inside the form, in document order; inputs of type
submit/button/reset/image/file left out; a checkbox or radio only when checked, valued "on" without a value attribute;
a select's selected options, or its first option when none is selected; a textarea's text, newlines as CRLF.
"""
import sys
import urllib.parse
from html.parser import HTMLParser

SKIP_TYPES = {"submit", "button", "reset", "image", "file"}


class _Form(HTMLParser):
    def __init__(self, form_id):
        super().__init__(convert_charrefs=True)
        self.form_id, self.depth, self.pairs = form_id, 0, []
        self._select = None   # (name, options [(value, selected)], multiple)
        self._option = None   # [value or None, selected, text]
        self._textarea = None  # [name, text]

    def handle_starttag(self, tag, attrs):
        a = {k: (v if v is not None else "") for k, v in attrs}
        if tag == "form":
            if self.depth or a.get("id") == self.form_id:
                self.depth += 1
            return
        if not self.depth:
            return
        name, disabled = a.get("name", ""), "disabled" in a
        if tag == "input" and name and not disabled:
            t = a.get("type", "text").lower()
            if t in SKIP_TYPES:
                return
            if t in ("checkbox", "radio"):
                if "checked" in a:
                    self.pairs.append((name, a.get("value", "on") if "value" in a else "on"))
            else:
                self.pairs.append((name, a.get("value", "")))
        elif tag == "select":
            self._select = (name if not disabled else "", [], "multiple" in a)
        elif tag == "option" and self._select is not None:
            self._option = [a["value"] if "value" in a else None, "selected" in a, ""]
        elif tag == "textarea" and name and not disabled:
            self._textarea = [name, ""]

    def handle_data(self, data):
        if self._option is not None:
            self._option[2] += data
        if self._textarea is not None:
            self._textarea[1] += data

    def handle_endtag(self, tag):
        if tag == "form" and self.depth:
            self.depth -= 1
        elif tag == "option" and self._option is not None and self._select is not None:
            v, sel, text = self._option
            self._select[1].append((v if v is not None else " ".join(text.split()), sel))
            self._option = None
        elif tag == "select" and self._select is not None:
            name, opts, multiple = self._select
            if name and opts:
                chosen = [v for v, s in opts if s]
                if not chosen:
                    chosen = [opts[0][0]]
                elif not multiple:
                    chosen = chosen[-1:]   # a single select keeps the last selected option, as browsers do
                self.pairs += [(name, v) for v in chosen]
            self._select = None
        elif tag == "textarea" and self._textarea is not None:
            name, text = self._textarea
            text = text[1:] if text.startswith("\n") else text   # the newline right after <textarea> is not content
            self.pairs.append((name, text.replace("\r\n", "\n").replace("\n", "\r\n")))
            self._textarea = None


def form_pairs(html, form_id="cfg"):
    p = _Form(form_id)
    p.feed(html)
    p.close()
    return p.pairs


def masked(pairs):
    return sorted({n for n, v in pairs if v and set(v) == {"*"}})


def read(path):
    with open(path, encoding="utf-8", errors="replace") as fh:
        return fh.read()


def cmd_body(page, out, drop):
    pairs = form_pairs(read(page))
    if not pairs:
        print("NO FORM: form#cfg was not found or had no fields; nothing written")
        return 2
    kept = [(n, v) for n, v in pairs if n not in drop]
    with open(out, "w", encoding="utf-8", newline="") as fh:
        fh.write(urllib.parse.urlencode(kept))
    gone = sorted({n for n, _ in pairs} & set(drop))
    print(f"fields {len(kept)} written (of {len(pairs)}); dropped: {', '.join(gone) or 'none'}; "
          f"masked secret fields: {', '.join(masked(pairs)) or 'none'}")
    return 0


def _norm(pairs):
    return [(n, v.replace("\r\n", "\n")) for n, v in pairs]


def cmd_compare(snap, page, ignore):
    then = _norm(urllib.parse.parse_qsl(read(snap), keep_blank_values=True))
    now = _norm(form_pairs(read(page)))
    names = []
    for n, _ in then + now:
        if n not in names:
            names.append(n)
    differ = [n for n in names if n not in ignore and [v for k, v in then if k == n] != [v for k, v in now if k == n]]
    total = len([n for n in names if n not in ignore])
    print(f"{total - len(differ)} of {total} fields equal" + (f"; differ: {', '.join(differ)}" if differ else "")
          + (f"; ignored: {', '.join(sorted(ignore))}" if ignore else ""))
    return 0 if not differ else 1


def cmd_field_equals(page, name, value):
    vals = [v for n, v in form_pairs(read(page)) if n == name]
    ok = vals == [value]
    print("equal" if ok else f"not equal ({len(vals)} value(s) for that name)")
    return 0 if ok else 1


SYNTH = """<html><body><input name="outside" value="x">
<form id="other"><input name="other1" value="o"></form>
<form id="cfg" action="/save" method="POST">
 <input name="loc0-lat" value='47.45'> <input name="loc0-lon" value="-122.31">
 <input name="title" value="A &amp; B">
 <input name="logbook" type="checkbox" checked>
 <input name="chime" type="checkbox">
 <input name="kind" type="radio" value="a"><input name="kind" type="radio" value="b" checked>
 <input name="secret" type="password" value="********">
 <input name="gone" value="d" disabled>
 <input type="submit" name="go" value="Save">
 <input name="noval">
 <select name="units"><option value="km">km</option><option value="nm" selected>nm</option></select>
 <select name="theme"><option>Dark</option><option>Light</option></select>
 <textarea name="note">
line one
line two</textarea>
</form></body></html>"""

SYNTH_WANT = [("loc0-lat", "47.45"), ("loc0-lon", "-122.31"), ("title", "A & B"), ("logbook", "on"), ("kind", "b"),
              ("secret", "********"), ("noval", ""), ("units", "nm"), ("theme", "Dark"), ("note", "line one\r\nline two")]


def selftest():
    import os
    import tempfile
    bad = 0

    def check(name, ok):
        nonlocal bad
        bad += 0 if ok else 1
        print(f"{'PASS' if ok else 'FAIL'} {name}")

    got = form_pairs(SYNTH)
    check("F1 FormData's rules: the exact pairs, in order", got == SYNTH_WANT)
    check("F2 only form#cfg: nothing from outside it or from another form", all(n not in ("outside", "other1") for n, _ in got))
    check("F3 masked secrets are named", masked(got) == ["secret"])
    d = tempfile.mkdtemp(prefix="fbform-")
    try:
        page, body, page2 = (os.path.join(d, f) for f in ("p.html", "b.body", "p2.html"))
        with open(page, "w", encoding="utf-8") as fh:
            fh.write(SYNTH)
        import contextlib
        import io
        with contextlib.redirect_stdout(io.StringIO()):
            cmd_body(page, body, {"logbook"})
        sent = urllib.parse.parse_qsl(read(body), keep_blank_values=True)
        check("F4 --drop logbook leaves out logbook and nothing else", sent == [p for p in SYNTH_WANT if p[0] != "logbook"])
        with contextlib.redirect_stdout(io.StringIO()):
            cmd_body(page, body, set())
        check("F4-control: without --drop, logbook is posted (the drop is not vacuous)",
              ("logbook", "on") in urllib.parse.parse_qsl(read(body), keep_blank_values=True))
        with contextlib.redirect_stdout(io.StringIO()):
            same = cmd_compare(body, page, set())
        check("F5 compare: a page against its own snapshot is all equal", same == 0)
        with open(page2, "w", encoding="utf-8") as fh:
            fh.write(SYNTH.replace("value='47.45'", "value='47.46'"))
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            diff = cmd_compare(body, page2, set())
        out = buf.getvalue()
        check("F5-red: one changed value is caught, by NAME, with no value printed",
              diff == 1 and "differ: loc0-lat" in out and "47.4" not in out)
        with contextlib.redirect_stdout(io.StringIO()):
            eq, ne = cmd_field_equals(page, "loc0-lat", "47.45"), cmd_field_equals(page, "loc0-lat", "1.0")
        check("F6 field-equals: equal, and not equal", eq == 0 and ne == 1)
        # F7 (the reviewer's condition, 02:0xZ): a made-up SENTINEL value run through every printing path -- body,
        # compare (equal and differing), field-equals (equal and not), and a failing call through the guard -- must
        # appear in NO output, stdout or stderr; and the count/names lines must still print (the control).
        sentinel = "SENTINEL-12.3456789"
        spage, sbody, spage2 = (os.path.join(d, f) for f in ("s.html", "s.body", "s2.html"))
        with open(spage, "w", encoding="utf-8") as fh:
            fh.write(SYNTH.replace("value='47.45'", f"value='{sentinel}'").replace("********", sentinel.replace("SENTINEL", "*" * 8)))
        with open(spage2, "w", encoding="utf-8") as fh:
            fh.write(SYNTH.replace("value='47.45'", f"value='{sentinel}X'"))
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            cmd_body(spage, sbody, {"logbook"})
            cmd_compare(sbody, spage, set())
            cmd_compare(sbody, spage2, set())
            cmd_field_equals(spage, "loc0-lat", sentinel)
            cmd_field_equals(spage2, "loc0-lat", sentinel)
            guarded(["compare", sbody, os.path.join(d, "missing.html")])   # an error path: type only
        text = out.getvalue() + err.getvalue()
        check("F7-plant: the sentinel IS in the page and in the snapshot body (else absence proves nothing)",
              sentinel in read(spage) and sentinel in read(sbody))
        check("F7 the sentinel reaches no output, stdout or stderr", "SENTINEL" not in text and "12.3456789" not in text)
        check("F7-control: the count and names lines still print",
              "fields" in text and "differ: loc0-lat" in text and "FAILED: FileNotFoundError" in text)
    finally:
        import shutil
        shutil.rmtree(d, ignore_errors=True)
    print("SELFTEST PASSED" if not bad else f"SELFTEST FAILED: {bad}")
    return 0 if not bad else 1


def main(argv):
    if argv[:1] == ["--selftest"]:
        return selftest()
    if len(argv) >= 3 and argv[0] == "body":
        return cmd_body(argv[1], argv[2], set(argv[argv.index("--drop") + 1:]) if "--drop" in argv else set())
    if len(argv) >= 3 and argv[0] == "compare":
        return cmd_compare(argv[1], argv[2], set(argv[argv.index("--ignore") + 1:]) if "--ignore" in argv else set())
    if len(argv) == 4 and argv[0] == "field-equals":
        return cmd_field_equals(argv[1], argv[2], argv[3])
    print(__doc__)
    return 2


def guarded(argv):
    """Any failure prints its exception TYPE only: an exception's message can carry a value it was handed."""
    try:
        return main(argv)
    except SystemExit:
        raise
    except Exception as ex:   # noqa: BLE001 -- the point is to swallow the message
        print(f"FAILED: {type(ex).__name__}")
        return 3


if __name__ == "__main__":
    sys.exit(guarded(sys.argv[1:]))
