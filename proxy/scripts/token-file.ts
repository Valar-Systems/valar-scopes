/**
 * token-file.ts -- where the by-hand scripts get their Cloudflare tokens.
 *
 *   bash:        export BLIPSCOPE_TOKEN_FILE=~/.config/blipscope/tokens
 *   PowerShell:  $env:BLIPSCOPE_TOKEN_FILE = "$HOME\.config\blipscope\tokens"
 *
 * Set it PER SHELL, never at user level. The file lives OUTSIDE any git working
 * tree and holds one token per purpose, keyed:
 *
 *   # blank lines and # comments are ignored
 *   kv-read=<Workers KV Storage: Read token>
 *   analytics-read=<Account Analytics: Read token>
 *   blip-key=<the bench device's key>          (docs/blip-creds-token-file.md)
 *   blip-device=<the bench device's id>
 *
 * blip-key / blip-device (E2:08's pair) are parsed but resolved by NOTHING since docs/test-identity.md:
 * the checks and ingests run as the test identity (resolveTestIdentity), and refuse BLIP_KEY / BLIP_DEVICE.
 *
 * WHY NOT CLOUDFLARE_API_TOKEN. wrangler PREFERS that variable over a
 * `wrangler login` session, so a KV-scoped token left at user level shadows the
 * OAuth login and deploy.sh fails exactly as if there were no credential (the
 * trap proxy/scripts/deploy.sh documents). None of these scripts reads it any more;
 * see CLAUDE.md, "No user-level CLOUDFLARE_API_TOKEN on the machine, ever".
 *
 * CI does not use the file: each workflow passes its secret in a variable named
 * for the script (PHOTO_KV_READ_TOKEN, CF_API_TOKEN), and resolveToken() takes
 * that first when it is non-empty.
 *
 * NEVER PRINTS A VALUE. Errors name the variable, the path, the key and a line
 * number -- never a line's content, which could be a token.
 *
 * No static node: imports: the unit tests run in the Workers pool, which has no
 * node:fs. The real filesystem is reached through process.getBuiltinModule, and
 * only when no test double is passed in. Erasable TypeScript only, because
 * dashboard/scripts/smoke-analytics.mjs imports this file under
 * node --experimental-strip-types.
 */

export const TOKEN_FILE_ENV = "BLIPSCOPE_TOKEN_FILE";

// test-mac, test-device, test-key, test-key-staging: the TEST identity the production checks run as, written by
// mint-test-identity.ts (docs/test-identity.md). records-device: E2:08's id, which identifies whose production records
// to read and is NOT a credential. blip-key/blip-device: E2:08's pair, read by no check once docs/test-identity.md lands.
export type TokenPurpose =
  | "kv-read" | "analytics-read" | "blip-key" | "blip-device"
  | "test-mac" | "test-device" | "test-key" | "test-key-staging" | "records-device";
export const PURPOSES: readonly TokenPurpose[] = [
  "kv-read", "analytics-read", "blip-key", "blip-device", "test-mac", "test-device", "test-key", "test-key-staging", "records-device",
];

export const FORMAT_HELP =
  `${TOKEN_FILE_ENV} names a file OUTSIDE the repo, set per shell ` +
  `(bash: export ${TOKEN_FILE_ENV}=~/.config/blipscope/tokens; ` +
  `PowerShell: $env:${TOKEN_FILE_ENV} = "$HOME\\.config\\blipscope\\tokens"). ` +
  `Format: one key=value per line, keys ${PURPOSES.join(", ")}; blank lines and # comments ignored. ` +
  `Do not use CLOUDFLARE_API_TOKEN instead: wrangler prefers it over \`wrangler login\` (see CLAUDE.md).`;

export class TokenFileError extends Error {
  constructor(message: string) {
    super(message);
    this.name = "TokenFileError";
  }
}

/** Everything the reader touches, so tests can run it without a filesystem. */
export interface TokenFileIo {
  env: Record<string, string | undefined>;
  platform: string;
  /** Absolute, normalised form of a path. */
  resolve(p: string): string;
  dirname(p: string): string;
  join(a: string, b: string): string;
  exists(p: string): boolean;
  read(p: string): string;
  /** st_mode of the file (permission bits are what matter). */
  mode(p: string): number;
  /** The repo this helper lives in (the checkout that must not hold the file). */
  repoRoot: string;
  warn(msg: string): void;
}

// ---------------------------------------------------------------- the parse

/** The value for one key. Throws naming the key and line numbers, never content. */
export function parseTokenFile(text: string, purpose: TokenPurpose, where: string): string {
  let found: string | undefined;
  let foundAt = 0;
  const lines = text.replace(/^﻿/, "").split(/\r?\n/);
  for (let i = 0; i < lines.length; i++) {
    const line = lines[i]!.trim();
    if (!line || line.startsWith("#")) continue;
    const eq = line.indexOf("=");
    if (eq <= 0) {
      throw new TokenFileError(
        `${where}: line ${i + 1} is not key=value (content not shown). ` +
          `A bare token on its own line is the old one-token-per-file format; prefix it with its key. ${FORMAT_HELP}`,
      );
    }
    const key = line.slice(0, eq).trim();
    if (key !== purpose) continue;
    if (found !== undefined) {
      throw new TokenFileError(`${where}: key "${purpose}" appears twice (lines ${foundAt} and ${i + 1}). Keep one.`);
    }
    found = line.slice(eq + 1).trim();
    foundAt = i + 1;
  }
  if (found === undefined) {
    throw new TokenFileError(`${where}: no "${purpose}" line. This script needs ${purpose}=<token>. ${FORMAT_HELP}`);
  }
  if (!found) throw new TokenFileError(`${where}: "${purpose}" is empty (line ${foundAt}).`);
  return found;
}

// ---------------------------------------------------------------- the path

/** True when p is inside the repo, or inside ANY git working tree (one `git add -A` away). */
export function insideRepoOrGitTree(p: string, io: TokenFileIo): boolean {
  const norm = (s: string) => (io.platform === "win32" ? s.toLowerCase() : s).replace(/[\\/]+$/, "");
  const abs = io.resolve(p);
  const root = norm(io.resolve(io.repoRoot));
  const a = norm(abs);
  if (a === root || a.startsWith(root + "/") || a.startsWith(root + "\\")) return true;
  let d = io.dirname(abs);
  for (;;) {
    if (io.exists(io.join(d, ".git"))) return true;
    const up = io.dirname(d);
    if (up === d) return false;
    d = up;
  }
}

/** The token file's text and path, with every refusal readTokenFile applies. `what` names the caller's need. */
export function readTokenFileText(what: string, io: TokenFileIo = nodeIo()): { text: string; path: string } {
  const path = io.env[TOKEN_FILE_ENV];
  if (!path || !path.trim()) {
    throw new TokenFileError(`${TOKEN_FILE_ENV} is not set, so there is no ${what}. ${FORMAT_HELP}`);
  }
  const p = path.trim();
  if (!io.exists(p)) throw new TokenFileError(`${TOKEN_FILE_ENV} points at ${p}, which does not exist. ${FORMAT_HELP}`);
  if (insideRepoOrGitTree(p, io)) {
    throw new TokenFileError(`${TOKEN_FILE_ENV} points at ${p}, which is inside a git working tree; move it out of the repo.`);
  }
  if (io.platform === "win32") {
    io.warn(`note: ${p}: file permissions are not checked on Windows; keep it readable by you alone.`);
  } else if ((io.mode(p) & 0o077) !== 0) {
    throw new TokenFileError(`${p} is readable by others (mode ${(io.mode(p) & 0o777).toString(8)}); run: chmod 600 ${p}`);
  }
  return { text: io.read(p), path: p };
}

/** The token for one purpose, from the file BLIPSCOPE_TOKEN_FILE names. Never printed. */
export function readTokenFile(purpose: TokenPurpose, io: TokenFileIo = nodeIo()): string {
  const { text, path } = readTokenFileText(`${purpose} token`, io);
  return parseTokenFile(text, purpose, path);
}

/** The KEYS present in a token file's text (never a value): what mint-test-identity.ts refuses to overwrite. */
export function tokenFileKeys(text: string): Set<string> {
  const keys = new Set<string>();
  for (const raw of text.replace(/^﻿/, "").split(/\r?\n/)) {
    const line = raw.trim();
    if (!line || line.startsWith("#")) continue;
    const eq = line.indexOf("=");
    if (eq > 0) keys.add(line.slice(0, eq).trim());
  }
  return keys;
}

/**
 * The TEST identity (docs/test-identity.md): the ONLY pair smoke-prod.sh, watch-upstream.sh and both ingests may use.
 * Refused, with a TokenFileError and before any request:
 *   - BLIP_KEY or BLIP_DEVICE set in the environment, whatever its value (the checks never sign in as a board);
 *   - test-device equal to the file's records-device (E2:08's id), so a board's pair written into the test fields
 *     is refused too;
 *   - a missing test field, or one out of shape (16 and 64 lowercase hex).
 * It never reads blip-key or blip-device. `source` is NAMES only, safe to print.
 */
export function resolveTestIdentity(target: "production" | "staging", io: TokenFileIo = nodeIo()): { key: string; device: string; source: string } {
  for (const v of ["BLIP_KEY", "BLIP_DEVICE"]) {
    if (io.env[v]) throw new TokenFileError(`${v} is set in this environment. The production checks run only as the test identity (docs/test-identity.md): unset ${v}.`);
  }
  const keyField: TokenPurpose = target === "staging" ? "test-key-staging" : "test-key";
  const { text, path } = readTokenFileText(`test identity (test-device + ${keyField})`, io);
  const device = parseTokenFile(text, "test-device", path);
  const key = parseTokenFile(text, keyField, path);
  if (!/^[0-9a-f]{16}$/.test(device) || !/^[0-9a-f]{64}$/.test(key)) {
    throw new TokenFileError(`${path}: test-device or ${keyField} is not in its shape (16 and 64 lowercase hex; values not shown).`);
  }
  if (tokenFileKeys(text).has("records-device") && parseTokenFile(text, "records-device", path) === device) {
    throw new TokenFileError(`${path}: test-device equals records-device, a real board's id: refused (docs/test-identity.md).`);
  }
  return { key, device, source: `${TOKEN_FILE_ENV} (test-device + ${keyField})` };
}

/**
 * CI's named variable when it is set and non-empty (`||`, not `??`: an empty
 * variable must fall through), else the file. `source` is a NAME, safe to print.
 */
export function resolveToken(
  purpose: TokenPurpose,
  ciVar?: string,
  io?: TokenFileIo,
): { token: string; source: string } {
  const env = io?.env ?? nodeProcess().env;
  const ci = ciVar ? env[ciVar] : undefined;
  if (ci) return { token: ci, source: ciVar! };
  return { token: readTokenFile(purpose, io ?? nodeIo()), source: `${TOKEN_FILE_ENV} (${purpose})` };
}

// ---------------------------------------------------------------- the real io

interface NodeProcess {
  env: Record<string, string | undefined>;
  platform: string;
  getBuiltinModule(id: string): unknown;
}

function nodeProcess(): NodeProcess {
  const p = (globalThis as unknown as { process?: NodeProcess }).process;
  if (!p || typeof p.getBuiltinModule !== "function") {
    throw new TokenFileError("token-file needs Node 22.3 or newer (process.getBuiltinModule).");
  }
  return p;
}

export function nodeIo(): TokenFileIo {
  const proc = nodeProcess();
  const fs = proc.getBuiltinModule("node:fs") as {
    existsSync(p: string): boolean;
    readFileSync(p: string, enc: "utf8"): string;
    statSync(p: string): { mode: number };
  };
  const path = proc.getBuiltinModule("node:path") as {
    resolve(...p: string[]): string;
    dirname(p: string): string;
    join(...p: string[]): string;
  };
  const url = proc.getBuiltinModule("node:url") as { fileURLToPath(u: string): string };
  const here = path.dirname(url.fileURLToPath(import.meta.url)); // proxy/scripts
  return {
    env: proc.env,
    platform: proc.platform,
    resolve: (p) => path.resolve(p),
    dirname: (p) => path.dirname(p),
    join: (a, b) => path.join(a, b),
    exists: (p) => fs.existsSync(p),
    read: (p) => fs.readFileSync(p, "utf8"),
    mode: (p) => fs.statSync(p).mode,
    repoRoot: path.resolve(here, "..", ".."),
    warn: (m) => console.error(m),
  };
}
