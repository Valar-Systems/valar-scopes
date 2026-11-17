#!/usr/bin/env python3
"""The exception list, by SCAN (docs/speed-p5p6.md 8c, the reviewer's ruling (B)): in one reference object tree, every
object whose ALLOCATED sections hold a compile stamp OF THAT BUILD -- its local date as __DATE__ writes it ("Mmm dd
yyyy", day space-padded) or a __TIME__ ("hh:mm:ss") inside the build's own local time window. Each hit is one line
`<object> <section>  # compile stamp`, the form objcmp.py --except reads.

    stampscan.py <tree> <build log>    the window is the build child's own start and exit lines (UTC), in local time
"""
import os
import re
import sys
from datetime import datetime, timezone

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from objcmp import Elf, objects   # noqa: E402  (one ELF reader, shared)

MONTHS = 'Jan|Feb|Mar|Apr|May|Jun|Jul|Aug|Sep|Oct|Nov|Dec'
DATE = re.compile(rb'(?:' + MONTHS.encode() + rb') [ 0-9]\d \d{4}')
TIME = re.compile(rb'\d\d:\d\d:\d\d')
CHILD = re.compile(r'^(\d\d:\d\d:\d\d)Z (child pid|child exit)')


def window(log, day):
    """(local date bytes as __DATE__ writes it, local start hh:mm:ss, local end hh:mm:ss) from the child's UTC lines."""
    ts = [m.group(1) for m in (CHILD.match(ln) for ln in open(log, encoding='utf-8', errors='replace')) if m]
    if len(ts) < 2:
        raise SystemExit(f'RIG: {log} has no child start/exit lines')
    def local(hms):
        t = datetime.strptime(day + ' ' + hms, '%Y-%m-%d %H:%M:%S').replace(tzinfo=timezone.utc).astimezone()
        return t
    a, b = local(ts[0]), local(ts[-1])
    date = ('%s %2d %d' % (a.strftime('%b'), a.day, a.year)).encode()
    return date, a.strftime('%H:%M:%S').encode(), b.strftime('%H:%M:%S').encode()


def scan(tree, date, t0, t1):
    hits = []
    for rel in sorted(objects(tree)):
        e = Elf(open(os.path.join(tree, rel), 'rb').read())
        for name, secs in sorted(e.alloc().items()):
            for _, body in secs:
                if not body:
                    continue
                if any(m.group() == date for m in DATE.finditer(body)) or \
                   any(t0 <= m.group() <= t1 for m in TIME.finditer(body)):
                    hits.append((rel, name))
                    break
    return hits


if __name__ == '__main__':
    if len(sys.argv) != 4:
        print(__doc__ + '\n    stampscan.py <tree> <build log> <UTC day YYYY-MM-DD>')
        sys.exit(2)
    date, t0, t1 = window(sys.argv[2], sys.argv[3])
    hits = scan(sys.argv[1], date, t0, t1)
    print(f'# stamp scan: {sys.argv[1].rstrip("/").split("/")[-1]}: date {date.decode()}, window {t0.decode()}-{t1.decode()} '
          f'local; {len(hits)} entr{"y" if len(hits) == 1 else "ies"}')
    for rel, name in hits:
        print(f'{rel} {name}  # compile stamp')
