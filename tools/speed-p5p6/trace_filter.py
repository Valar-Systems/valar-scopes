#!/usr/bin/env python3
"""The board addendum's SEAL (docs/speed-p5p6.md, section 5). It reads one sealed bench-trace log and prints ONLY:
  - bench-trace's own lines (attach, end, verdict, read error, open failed, the contamination banner);
  - [speed-p5], [speed-p6] and [speed-a2] lines, whole (the equality rows; addendum 2's margin probe);
  - [build] and [boot] lines, whole;
  - from the main [health] line, ONLY its heap fields (heap free, largest, free8, psram_free, tlsOk). Its frame-time
    fields are dropped, and every other [health] line that carries a timing (sweep=, clear=, outline=, sweepparts=,
    sweepscan=) is withheld whole. [health] ALLOC FAILED and preventive-reboot lines pass whole.
Everything else is WITHHELD and only COUNTED, by tag; it is never printed. [frame-bench] records stay unread until
the gains addendum. The seal is the sha256 of the raw file, printed first.

    trace_filter.py <trace.log>      exit 0 printed; 2 usage or unreadable
    trace_filter.py --selftest       exit 0 every check as predicted, 1 otherwise
"""
import collections
import hashlib
import re
import sys

STAMP = re.compile(r'^(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z) ?(.*)$')
META = ('single open', 'attached', '=== ', 'VERDICT:', 'READ ERROR', 'OPEN FAILED', '!!!')
WHOLE = ('[speed-p5]', '[speed-p6]', '[speed-a2]', '[build]', '[boot]')
HEAP = re.compile(r'heap free=(\d+) largest=(\d+) free8=(\d+) psram_free=(\d+) tlsOk=(-?\d+)')
HEALTH_WHOLE = ('[health] ALLOC FAILED', '[health] preventive reboot')
TAG = re.compile(r'^(\[[^\]]{1,24}\])')


def filter_lines(lines):
    """(printed lines, withheld counts by tag)."""
    out, held = [], collections.Counter()
    for raw in lines:
        m = STAMP.match(raw.rstrip('\r\n'))
        ts, body = (m.group(1), m.group(2)) if m else ('-', raw.rstrip('\r\n'))
        if body.startswith(META):
            out.append(f'{ts} {body}')
        elif body.startswith(WHOLE) or body.startswith(HEALTH_WHOLE):
            out.append(f'{ts} {body}')
        elif body.startswith('[health]'):
            h = HEAP.search(body)
            if h:
                out.append(f'{ts} [health] heap free={h.group(1)} largest={h.group(2)} free8={h.group(3)} '
                           f'psram_free={h.group(4)} tlsOk={h.group(5)}')
            else:
                held['[health] (timing)'] += 1
        else:
            t = TAG.match(body)
            held[t.group(1) if t else '(untagged)'] += 1
    return out, held


def main(path):
    try:
        data = open(path, 'rb').read()
    except OSError as e:
        print(f'UNREADABLE: {e}')
        return 2
    print(f'seal sha256 {hashlib.sha256(data).hexdigest()}  bytes {len(data)}  lines {data.count(b"\n")}')
    out, held = filter_lines(data.decode('utf-8', 'replace').splitlines())
    for line in out:
        print(line)
    print('withheld (count only): ' + (', '.join(f'{k} {v}' for k, v in sorted(held.items())) or 'none'))
    return 0


def selftest():
    t = '2026-11-17T10:00:{:02d}Z '
    trace = [t.format(0) + 'single open, no reattach: a read error ENDS this run rather than reopening',
             t.format(1) + 'attached (single open, DTR/RTS asserted)',
             t.format(2) + '[build] env=blipscope-s3-128-framebench-c1b fw=v19 features=cloud',
             t.format(3) + '[boot] reset reason=SW',
             t.format(4) + '[frame-bench] dump S0 0: 0a 0b 0c 0d 0e 0f',
             t.format(5) + '[frame-bench] scene S0 outline-8copy settle',
             t.format(6) + '[health] frame avg=41.2ms p95=55.0ms max=80.1ms  n=600 ap=1 resid=+0.3ms  heap free=123456 '
                           'largest=65536 free8=120000 psram_free=7000000 tlsOk=1 rej=0 ball=1/2  allocFail=0',
             t.format(7) + '[health] sweep=4600/9000us n=600 blend=1',
             t.format(8) + '[health] clear=3820/5000us n=600 push=1',
             t.format(9) + '[health] outline=900/1200us n=600',
             t.format(10) + '[health] ALLOC FAILED: 4096 B (caps 0x4) in x (#1)',
             t.format(11) + '[wifi] connected ip 192.0.2.10',
             t.format(12) + '[speed-p5] clear S8 frames 4321 mismatched-px 0',
             t.format(13) + '[speed-p6] labels S0 checked 8800 mismatches 0',
             t.format(13) + '[speed-a2] heap margin S2 p0 50048',
             t.format(14) + 'some untagged output',
             t.format(15) + '=== end ===',
             t.format(16) + 'VERDICT: clean -- no host-caused reset within 5s of attach.']
    out, held = filter_lines(trace)
    text = '\n'.join(out)
    joined_in = '\n'.join(trace)
    bad = 0

    def ok(cond, what):
        nonlocal bad
        bad += not cond
        print(f"  {'ok  ' if cond else 'FAIL'}  {what}")

    # CONTROL: each forbidden string IS in the input, so its absence from the output is a fact about the filter.
    for s in ('[frame-bench]', 'frame avg=', 'p95=', 'sweep=', 'clear=3820', 'outline=', '[wifi]', '192.0.2.10', 'untagged'):
        ok(s in joined_in, f'CONTROL: the input carries {s!r}')
        ok(s not in text, f'the output carries no {s!r}')
    for s in ('[build] env=', '[boot] reset reason=SW', 'heap free=123456 largest=65536 free8=120000 psram_free=7000000 tlsOk=1',
              '[health] ALLOC FAILED', '[speed-p5] clear S8 frames 4321 mismatched-px 0',
              '[speed-p6] labels S0 checked 8800 mismatches 0', '[speed-a2] heap margin S2 p0 50048', 'attached',
              '=== end ===', 'VERDICT: clean'):
        ok(s in text, f'the output keeps {s!r}')
    ok(held == {'[frame-bench]': 2, '[health] (timing)': 3, '[wifi]': 1, '(untagged)': 1},
       f'withheld counts exactly {{[frame-bench] 2, [health] (timing) 3, [wifi] 1, (untagged) 1}} (got {dict(held)})')
    ok(len(out) == 11, f'exactly 11 lines printed (got {len(out)})')
    print('SELFTEST PASSED' if not bad else f'SELFTEST FAILED ({bad})')
    return 0 if not bad else 1


if __name__ == '__main__':
    if sys.argv[1:] == ['--selftest']:
        sys.exit(selftest())
    if len(sys.argv) != 2:
        print(__doc__)
        sys.exit(2)
    sys.exit(main(sys.argv[1]))
