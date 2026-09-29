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

export type TokenPurpose = "kv-read" | "analytics-read";
export const PURPOSES: readonly TokenPurpose[] = ["kv-read", "analytics-read"];

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

/** The token for one purpose, from the file BLIPSCOPE_TOKEN_FILE names. Never printed. */
export function readTokenFile(purpose: TokenPurpose, io: TokenFileIo = nodeIo()): string {
  const path = io.env[TOKEN_FILE_ENV];
  if (!path || !path.trim()) {
    throw new TokenFileError(`${TOKEN_FILE_ENV} is not set, so there is no ${purpose} token. ${FORMAT_HELP}`);
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
  return parseTokenFile(io.read(p), purpose, p);
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
