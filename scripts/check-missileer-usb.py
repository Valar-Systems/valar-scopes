#!/usr/bin/env python3
"""No Missileer build ships the USB keyboard, and every Missileer build uses the chip's
built-in USB-Serial/JTAG (Daniel, 2026-10-12).

FEATURE_USB_OPEN (a long press typed the shown message's link into the computer as a
HID keyboard) needed ARDUINO_USB_MODE=0, TinyUSB -- and on TinyUSB a firmware that
misbehaves before USB comes up cannot be reflashed without opening the case. The ruling:
MODE=1 for missileer-s3-128 and everything that extends it; the feature's code stays
behind its flag and NO env builds it. This makes that a CI failure, not a comment:

  config  (default)   every [env:*] in platformio.ini is resolved through `extends` and
                      ${section.option}; FAIL if any env defines FEATURE_USB_OPEN, or if
                      any Missileer env (named missileer-*, or extending one) does not
                      resolve to -DARDUINO_USB_MODE=1. BLIND (exit 2) if no Missileer env
                      is found -- a check that sees nothing proves nothing.
  --image <bin>       the built image must carry the build anchor "[build] env=" and no
                      "[usb-open]" string (the feature's own log prefix).
  --selftest          plants each violation in a scratch ini / image and requires FAIL.

Exit: 0 clean, 1 violation, 2 blind / usage.
"""
import configparser
import os
import re
import sys
import tempfile

ANCHOR = b'[build] env='
MARKER = b'[usb-open]'
REF = re.compile(r'\$\{([^}]+)\}')


def load(path):
    cp = configparser.ConfigParser(interpolation=None, strict=False, inline_comment_prefixes=None)
    cp.optionxform = str
    with open(path, encoding='utf-8') as f:
        cp.read_file(f)
    return cp


def raw(cp, section, opt, seen=()):
    """An option's raw value for a section, following `extends` like PlatformIO."""
    if section in seen:
        return None
    if cp.has_section(section) and cp.has_option(section, opt):
        return cp.get(section, opt)
    if cp.has_section(section) and cp.has_option(section, 'extends'):
        for parent in [p.strip() for p in cp.get(section, 'extends').split(',') if p.strip()]:
            v = raw(cp, parent, opt, seen + (section,))
            if v is not None:
                return v
    if section.startswith('env:') and cp.has_section('env') and cp.has_option('env', opt):
        return cp.get('env', opt)
    return None


def resolve(cp, value, depth=0):
    if depth > 20:
        raise SystemExit('BLIND: ${...} interpolation does not terminate')

    def sub(m):
        ref = m.group(1)
        if ref.startswith('sysenv.') or '.' not in ref:
            return ''
        sec, opt = ref.rsplit('.', 1)
        v = raw(cp, sec, opt)
        return resolve(cp, v, depth + 1) if v is not None else ''
    return REF.sub(sub, value or '')


def flags(cp, env):
    """-D macros (name -> value) of an env's resolved build_flags, comments stripped."""
    text = resolve(cp, raw(cp, 'env:' + env, 'build_flags') or '')
    out = {}
    for line in text.splitlines():
        line = line.split(';', 1)[0]
        for tok in line.split():
            if tok.startswith('-D'):
                name, _, val = tok[2:].partition('=')
                out[name] = val
            elif tok.startswith('-U'):
                out.pop(tok[2:], None)
    return out


def extends_missileer(cp, env, seen=()):
    if env.startswith('missileer-'):
        return True
    sec = 'env:' + env
    if env in seen or not cp.has_section(sec) or not cp.has_option(sec, 'extends'):
        return False
    return any(p.strip().startswith('env:') and extends_missileer(cp, p.strip()[4:], seen + (env,))
               for p in cp.get(sec, 'extends').split(','))


def check_config(path):
    cp = load(path)
    envs = [s[4:] for s in cp.sections() if s.startswith('env:')]
    missileer = [e for e in envs if extends_missileer(cp, e)]
    if not missileer:
        print(f'BLIND: no Missileer env in {path} -- nothing was checked')
        return 2
    bad = 0
    for e in envs:
        f = flags(cp, e)
        if 'FEATURE_USB_OPEN' in f:
            print(f'FAIL: [env:{e}] defines FEATURE_USB_OPEN -- no env may build the USB keyboard')
            bad += 1
        if e in missileer and f.get('ARDUINO_USB_MODE') != '1':
            print(f'FAIL: [env:{e}] resolves ARDUINO_USB_MODE={f.get("ARDUINO_USB_MODE", "<unset>")} '
                  f'-- every Missileer env uses the built-in USB-Serial/JTAG (MODE=1)')
            bad += 1
    if not bad:
        print(f'ok: {len(envs)} envs resolved, none builds FEATURE_USB_OPEN; '
              f'{len(missileer)} Missileer envs all ARDUINO_USB_MODE=1 ({", ".join(sorted(missileer))})')
    return 1 if bad else 0


def check_image(path):
    try:
        with open(path, 'rb') as f:
            data = f.read()
    except OSError as e:
        print(f'BLIND: cannot read {path}: {e}')
        return 2
    if ANCHOR not in data:
        print(f'BLIND: "{ANCHOR.decode()}" not in {path} -- cannot tell a clean image from an unreadable one')
        return 2
    if MARKER in data:
        print(f'FAIL: "{MARKER.decode()}" is in {path} -- the USB keyboard is compiled in')
        return 1
    print(f'ok: no "{MARKER.decode()}" in {path} (anchor present)')
    return 0


def selftest():
    base = '''
[common]
build_flags = -DCOMMON
[env:missileer-s3-128]
extends = common
build_flags =
    ${common.build_flags}
    -DFEATURE_EAM
    -DARDUINO_USB_MODE=1   ; built-in USB-Serial/JTAG
[env:missileer-photo]
extends = env:missileer-s3-128
build_flags =
    ${env:missileer-s3-128.build_flags}
    -DPHOTO_MODE
[env:blipscope-s3-128]
build_flags = -DARDUINO_USB_MODE=1
'''
    cases = [
        ('clean', base, 0),
        ('MODE=0 on the shipping env', base.replace('-DARDUINO_USB_MODE=1   ;', '-DARDUINO_USB_MODE=0   ;'), 1),
        ('FEATURE_USB_OPEN on the shipping env', base.replace('-DFEATURE_EAM', '-DFEATURE_EAM\n    -DFEATURE_USB_OPEN'), 1),
        ('a photo child overriding MODE=0', base.replace('-DPHOTO_MODE', '-DPHOTO_MODE\n    -DARDUINO_USB_MODE=0'), 1),
        ('FEATURE_USB_OPEN in [common] (reaches every env)', base.replace('-DCOMMON', '-DCOMMON -DFEATURE_USB_OPEN'), 1),
        ('FEATURE_USB_OPEN on a non-Missileer env', base.replace('build_flags = -DARDUINO_USB_MODE=1', 'build_flags = -DARDUINO_USB_MODE=1 -DFEATURE_USB_OPEN'), 1),
        ('a Missileer env with no USB mode at all', base.replace('    -DARDUINO_USB_MODE=1   ; built-in USB-Serial/JTAG\n', ''), 1),
        ('the flag only in a comment is not a define', base.replace('-DFEATURE_EAM', '-DFEATURE_EAM   ; was -DFEATURE_USB_OPEN'), 0),
        ('no Missileer env at all', '[env:blipscope-s3-128]\nbuild_flags = -DARDUINO_USB_MODE=1\n', 2),
    ]
    bad = 0
    with tempfile.TemporaryDirectory() as d:
        for name, ini, want in cases:
            p = os.path.join(d, 'platformio.ini')
            with open(p, 'w', encoding='utf-8') as f:
                f.write(ini)
            import io
            import contextlib
            with contextlib.redirect_stdout(io.StringIO()):
                got = check_config(p)
            ok = got == want
            bad += not ok
            print(f"  {'ok  ' if ok else 'FAIL'}  config: {name} -> {got} (want {want})")
        for name, blob, want in [('clean image', b'xx[build] env=missileer-s3-128xx', 0),
                                 ('image with the keyboard', b'xx[build] env=missileer-s3-128xx[usb-open] HID', 1),
                                 ('image without the anchor', b'nothing readable', 2)]:
            p = os.path.join(d, 'fw.bin')
            with open(p, 'wb') as f:
                f.write(blob)
            import io
            import contextlib
            with contextlib.redirect_stdout(io.StringIO()):
                got = check_image(p)
            ok = got == want
            bad += not ok
            print(f"  {'ok  ' if ok else 'FAIL'}  image: {name} -> {got} (want {want})")
    print('SELFTEST PASSED' if not bad else f'SELFTEST FAILED ({bad})')
    return 1 if bad else 0


def main(argv):
    if '--selftest' in argv:
        return selftest()
    if '--image' in argv:
        i = argv.index('--image')
        if i + 1 >= len(argv):
            print(__doc__)
            return 2
        return check_image(argv[i + 1])
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    return check_config(argv[0] if argv else os.path.join(root, 'platformio.ini'))


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
