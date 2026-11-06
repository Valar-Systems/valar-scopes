import { describe, expect, it } from "vitest";
import {
  TOKEN_FILE_ENV,
  TokenFileError,
  parseTokenFile,
  readTokenFile,
  resolveTestIdentity,
  resolveToken,
  type TokenFileIo,
} from "../scripts/token-file";

// A fake filesystem: the Workers pool has no node:fs, and the helper takes its
// io as a parameter for exactly this. POSIX-style paths throughout.
const SECRET_KV = "kv-SECRET-value-0001";
const SECRET_AN = "an-SECRET-value-0002";

function fakeIo(opts: {
  env?: Record<string, string | undefined>;
  files?: Record<string, string>;
  modes?: Record<string, number>;
  dirs?: string[]; // directories that contain a .git
  platform?: string;
  repoRoot?: string;
}): TokenFileIo & { warnings: string[] } {
  const files = opts.files ?? {};
  const gitDirs = new Set(opts.dirs ?? []);
  const warnings: string[] = [];
  const dirname = (p: string) => {
    const i = p.replace(/\/+$/, "").lastIndexOf("/");
    return i <= 0 ? "/" : p.slice(0, i);
  };
  return {
    env: opts.env ?? {},
    platform: opts.platform ?? "linux",
    resolve: (p) => p,
    dirname,
    join: (a, b) => (a.endsWith("/") ? a + b : `${a}/${b}`),
    exists: (p) => p in files || (p.endsWith("/.git") && gitDirs.has(dirname(p))),
    read: (p) => {
      if (!(p in files)) throw new Error(`ENOENT ${p}`);
      return files[p]!;
    },
    mode: (p) => opts.modes?.[p] ?? 0o100600,
    repoRoot: opts.repoRoot ?? "/work/valar-scopes",
    warn: (m) => warnings.push(m),
    warnings,
  };
}

const PATH = "/home/d/.config/blipscope/tokens";
const GOOD = `# by-hand tokens\n\nkv-read = ${SECRET_KV}  \r\nanalytics-read=\t${SECRET_AN}\n`;

/** Runs fn, returns the thrown error's message (fails the test if nothing throws). */
function thrown(fn: () => unknown): string {
  try {
    fn();
  } catch (e) {
    expect(e).toBeInstanceOf(TokenFileError);
    return (e as Error).message;
  }
  throw new Error("expected a throw");
}

describe("readTokenFile", () => {
  it("reads the right key and trims whitespace (spaces, tabs, CRLF)", () => {
    const io = fakeIo({ env: { [TOKEN_FILE_ENV]: `  ${PATH}  ` }, files: { [PATH]: GOOD } });
    expect(readTokenFile("kv-read", io)).toBe(SECRET_KV);
    expect(readTokenFile("analytics-read", io)).toBe(SECRET_AN);
  });

  it("missing env var: names the variable and the format", () => {
    for (const v of [undefined, "", "   "]) {
      const msg = thrown(() => readTokenFile("kv-read", fakeIo({ env: { [TOKEN_FILE_ENV]: v } })));
      expect(msg).toContain(`${TOKEN_FILE_ENV} is not set`);
      expect(msg).toContain("key=value");
      expect(msg).toContain("kv-read");
    }
  });

  it("missing file: names the variable and the path", () => {
    const msg = thrown(() => readTokenFile("kv-read", fakeIo({ env: { [TOKEN_FILE_ENV]: PATH } })));
    expect(msg).toContain(TOKEN_FILE_ENV);
    expect(msg).toContain(`${PATH}, which does not exist`);
  });

  it("refuses a path inside the repo, even with no .git to find", () => {
    const inRepo = "/work/valar-scopes/proxy/tokens";
    const msg = thrown(() =>
      readTokenFile("kv-read", fakeIo({ env: { [TOKEN_FILE_ENV]: inRepo }, files: { [inRepo]: GOOD } })),
    );
    expect(msg).toContain("inside a git working tree");
  });

  it("refuses a path inside any other git working tree", () => {
    const p = "/home/d/dotfiles/cf/tokens";
    const msg = thrown(() =>
      readTokenFile("kv-read", fakeIo({ env: { [TOKEN_FILE_ENV]: p }, files: { [p]: GOOD }, dirs: ["/home/d/dotfiles"] })),
    );
    expect(msg).toContain("inside a git working tree");
  });

  it("refuses the repo path case-insensitively on Windows", () => {
    const p = "C:/WORK/Valar-Scopes/tokens";
    const msg = thrown(() =>
      readTokenFile("kv-read", fakeIo({
        env: { [TOKEN_FILE_ENV]: p }, files: { [p]: GOOD }, platform: "win32", repoRoot: "C:/work/valar-scopes",
      })),
    );
    expect(msg).toContain("inside a git working tree");
  });

  it("POSIX: refuses a file group/other can read; accepts 600 and 400", () => {
    for (const mode of [0o100644, 0o100640, 0o100604, 0o100660]) {
      const io = fakeIo({ env: { [TOKEN_FILE_ENV]: PATH }, files: { [PATH]: GOOD }, modes: { [PATH]: mode } });
      expect(thrown(() => readTokenFile("kv-read", io))).toContain("chmod 600");
    }
    for (const mode of [0o100600, 0o100400]) {
      const io = fakeIo({ env: { [TOKEN_FILE_ENV]: PATH }, files: { [PATH]: GOOD }, modes: { [PATH]: mode } });
      expect(readTokenFile("kv-read", io)).toBe(SECRET_KV);
    }
  });

  it("Windows: warns about permissions instead of refusing", () => {
    const io = fakeIo({
      env: { [TOKEN_FILE_ENV]: PATH }, files: { [PATH]: GOOD }, modes: { [PATH]: 0o100666 }, platform: "win32",
    });
    expect(readTokenFile("kv-read", io)).toBe(SECRET_KV);
    expect(io.warnings.join("\n")).toContain("not checked on Windows");
  });
});

describe("parseTokenFile", () => {
  it("a missing key names the key and the format", () => {
    const msg = thrown(() => parseTokenFile(`kv-read=${SECRET_KV}\n`, "analytics-read", PATH));
    expect(msg).toContain('no "analytics-read" line');
    expect(msg).toContain("key=value");
  });

  it("an empty value, a duplicate key, and a bare token are all refused", () => {
    expect(thrown(() => parseTokenFile("kv-read=   \n", "kv-read", PATH))).toContain("is empty");
    expect(thrown(() => parseTokenFile(`kv-read=${SECRET_KV}\nkv-read=${SECRET_AN}\n`, "kv-read", PATH))).toContain(
      "appears twice (lines 1 and 2)",
    );
    expect(thrown(() => parseTokenFile(`${SECRET_KV}\n`, "kv-read", PATH))).toContain("line 1 is not key=value");
  });

  it("keeps an '=' inside a value", () => {
    expect(parseTokenFile("kv-read=abc=def==\n", "kv-read", PATH)).toBe("abc=def==");
  });
});

describe("never prints a value", () => {
  // Every failure mode, with both secrets somewhere in the input: no message and
  // no warning may contain either.
  const cases: [string, () => TokenFileIo][] = [
    ["bare token line", () => fakeIo({ env: { [TOKEN_FILE_ENV]: PATH }, files: { [PATH]: `${SECRET_KV}\n` } })],
    ["duplicate key", () => fakeIo({ env: { [TOKEN_FILE_ENV]: PATH }, files: { [PATH]: `kv-read=${SECRET_KV}\nkv-read=${SECRET_AN}\n` } })],
    ["missing key", () => fakeIo({ env: { [TOKEN_FILE_ENV]: PATH }, files: { [PATH]: `analytics-read=${SECRET_AN}\n` } })],
    ["world-readable", () => fakeIo({ env: { [TOKEN_FILE_ENV]: PATH }, files: { [PATH]: GOOD }, modes: { [PATH]: 0o100644 } })],
    ["windows warning", () => fakeIo({ env: { [TOKEN_FILE_ENV]: PATH }, files: { [PATH]: GOOD }, platform: "win32" })],
  ];
  for (const [name, make] of cases) {
    it(name, () => {
      const io = make() as TokenFileIo & { warnings: string[] };
      let msg = "";
      try {
        readTokenFile("kv-read", io);
      } catch (e) {
        msg = (e as Error).message;
      }
      const out = msg + "\n" + io.warnings.join("\n");
      expect(out).not.toContain(SECRET_KV);
      expect(out).not.toContain(SECRET_AN);
    });
  }
});

describe("resolveToken", () => {
  it("CI's named variable wins when non-empty, and its source is a NAME", () => {
    const io = fakeIo({ env: { PHOTO_KV_READ_TOKEN: SECRET_AN, [TOKEN_FILE_ENV]: PATH }, files: { [PATH]: GOOD } });
    expect(resolveToken("kv-read", "PHOTO_KV_READ_TOKEN", io)).toEqual({ token: SECRET_AN, source: "PHOTO_KV_READ_TOKEN" });
  });

  it("an empty CI variable falls through to the file", () => {
    const io = fakeIo({ env: { PHOTO_KV_READ_TOKEN: "", [TOKEN_FILE_ENV]: PATH }, files: { [PATH]: GOOD } });
    const r = resolveToken("kv-read", "PHOTO_KV_READ_TOKEN", io);
    expect(r.token).toBe(SECRET_KV);
    expect(r.source).toBe(`${TOKEN_FILE_ENV} (kv-read)`);
  });

  it("never reads CLOUDFLARE_API_TOKEN", () => {
    const io = fakeIo({ env: { CLOUDFLARE_API_TOKEN: SECRET_AN } });
    const msg = thrown(() => resolveToken("kv-read", "PHOTO_KV_READ_TOKEN", io));
    expect(msg).toContain(`${TOKEN_FILE_ENV} is not set`);
    expect(msg).not.toContain(SECRET_AN);
    expect(thrown(() => resolveToken("analytics-read", undefined, io))).toContain(TOKEN_FILE_ENV);
  });
});

// docs/blip-creds-token-file.md: BLIP_KEY / BLIP_DEVICE from the Windows user environment to the token file.
describe("blip-key and blip-device (T1-T3)", () => {
  const KEY = "blip-SECRET-key-0003";
  const DEV = "0000000000000000"; // EXAMPLE -- not a real device
  const BLIP = `${GOOD}blip-key=${KEY}\nblip-device = ${DEV}\nsome-future-key=ignored\n`;

  it("T1: both new purposes parse beside the old ones, and an unknown key is still skipped", () => {
    const io = fakeIo({ env: { [TOKEN_FILE_ENV]: PATH }, files: { [PATH]: BLIP } });
    expect(readTokenFile("blip-key", io)).toBe(KEY);
    expect(readTokenFile("blip-device", io)).toBe(DEV);
    expect(readTokenFile("kv-read", io)).toBe(SECRET_KV);
  });

  it("T1: the refusals hold for the new purposes, and no message carries a value", () => {
    const inRepo = "/work/valar-scopes/tokens";
    const msgs = [
      thrown(() => readTokenFile("blip-key", fakeIo({ env: { [TOKEN_FILE_ENV]: inRepo }, files: { [inRepo]: BLIP } }))),
      thrown(() => readTokenFile("blip-key", fakeIo({ env: { [TOKEN_FILE_ENV]: PATH }, files: { [PATH]: BLIP },
                                                       modes: { [PATH]: 0o100644 } }))),
      thrown(() => parseTokenFile(`blip-key=${KEY}\n${KEY}\n`, "blip-key", PATH)),
      thrown(() => parseTokenFile(`blip-key=${KEY}\nblip-key=${KEY}\n`, "blip-key", PATH)),
      thrown(() => parseTokenFile("blip-key=\n", "blip-key", PATH)),
      thrown(() => parseTokenFile(GOOD, "blip-device", PATH)),
    ];
    expect(msgs[0]).toContain("inside");
    expect(msgs[1]).toContain("readable by others");
    expect(msgs[2]).toContain("not key=value");
    expect(msgs[3]).toContain("appears twice");
    expect(msgs[4]).toContain("is empty");
    expect(msgs[5]).toContain('no "blip-device" line');
    for (const m of msgs) {
      expect(m).not.toContain(KEY);
      expect(m).not.toContain(SECRET_KV);
    }
  });

});

// docs/test-identity.md: the TEST identity, the only pair the production checks and both ingests may use. blip-key and
// blip-device stay parseable (above) but nothing resolves them any more: resolveBlipCreds is gone.
describe("the test identity (resolveTestIdentity)", () => {
  const DEV = "0000000000000000"; // EXAMPLE -- records-device, not a real device
  const TDEV = "ab".repeat(8); // EXAMPLE test-device, built at run time so no 16-hex literal sits in the repo
  const TKEY = "c".repeat(64), TKEYS = "d".repeat(64); // EXAMPLE keys
  const FILE = `${GOOD}blip-key=blip-SECRET-key-0003\nblip-device=${DEV}\ntest-device=${TDEV}\ntest-key=${TKEY}\ntest-key-staging=${TKEYS}\nrecords-device=${DEV}\n`;
  const io = (env: Record<string, string | undefined> = {}, text = FILE) => fakeIo({ env: { [TOKEN_FILE_ENV]: PATH, ...env }, files: { [PATH]: text } });

  it("production takes test-key, staging test-key-staging, the same test-device; the source is names only", () => {
    expect(resolveTestIdentity("production", io())).toEqual({ key: TKEY, device: TDEV, source: `${TOKEN_FILE_ENV} (test-device + test-key)` });
    expect(resolveTestIdentity("staging", io())).toEqual({ key: TKEYS, device: TDEV, source: `${TOKEN_FILE_ENV} (test-device + test-key-staging)` });
  });

  it("BLIP_KEY or BLIP_DEVICE set: refused before the file is opened, whatever the value", () => {
    for (const env of [{ BLIP_KEY: TKEY }, { BLIP_DEVICE: TDEV }, { BLIP_KEY: "x", BLIP_DEVICE: DEV }]) {
      const i = io(env);
      i.read = () => { throw new Error("the file was read"); };
      const msg = thrown(() => resolveTestIdentity("production", i));
      expect(msg).toMatch(/BLIP_(KEY|DEVICE) is set/);
      expect(msg).not.toContain(TKEY);
    }
  });

  it("test-device equal to records-device (a board's id): refused", () => {
    const msg = thrown(() => resolveTestIdentity("production", io({}, FILE.replace(`test-device=${TDEV}`, `test-device=${DEV}`))));
    expect(msg).toContain("equals records-device");
  });

  it("a missing or misshapen test field: refused, and no message carries a value", () => {
    const msgs = [
      thrown(() => resolveTestIdentity("staging", io({}, FILE.replace(/^test-key-staging=.*$/m, "")))),
      thrown(() => resolveTestIdentity("production", io({}, FILE.replace(`test-key=${TKEY}`, "test-key=short")))),
      thrown(() => resolveTestIdentity("production", io({}, FILE.replace(`test-device=${TDEV}`, "test-device=XYZ")))),
    ];
    expect(msgs[0]).toContain('no "test-key-staging" line');
    expect(msgs[1]).toContain("not in its shape");
    expect(msgs[2]).toContain("not in its shape");
    for (const m of msgs) for (const v of [TKEY, TKEYS, TDEV, "blip-SECRET-key-0003"]) expect(m).not.toContain(v);
  });
});
