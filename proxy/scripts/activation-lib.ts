/**
 * activation-lib.ts -- the pure half of scripts/activation.ts (docs/device-activation.md).
 *
 * No node: imports, so the unit tests import it in the Workers pool. Two jobs, and both are
 * about what reaches a terminal:
 *
 *   - parseImport: the MACs in a factory list or an esptool log, and the NUMBERS of the
 *     lines that are neither -- never their content;
 *   - summarize: the route's answer as one line of counts or a state. It reads only the
 *     known numeric fields and the state enum, so an id cannot reach the output even if a
 *     future route started sending one.
 */

/** "MAC: 90:70:69:..." as esptool prints it (any spacing), or a bare MAC alone on a line. */
const LOG_MAC = /\bMAC:\s*((?:[0-9a-f]{2}[:-]){5}[0-9a-f]{2})\b/i;
const BARE_MAC = /^(?:[0-9a-f]{2}[:-]?){5}[0-9a-f]{2}$/i;

export interface ParsedImport {
  /** Unique, in first-seen order, lowercase and colon-separated. */
  macs: string[];
  /** 1-based line numbers of lines that are not a MAC (list mode only). */
  badLines: number[];
  mode: "list" | "log";
}

function canonical(raw: string): string {
  const h = raw.toLowerCase().replace(/[^0-9a-f]/g, "");
  return h.match(/../g)!.join(":");
}

/**
 * LOG MODE when any line carries esptool's "MAC:" label: only those lines count, and every
 * other line is ordinary log text. LIST MODE otherwise: every line that is not blank or a
 * # comment must be a MAC, and one that is not is reported by number.
 */
export function parseImport(text: string): ParsedImport {
  const lines = text.split(/\r?\n/);
  const mode: ParsedImport["mode"] = lines.some((l) => LOG_MAC.test(l)) ? "log" : "list";
  const seen = new Set<string>();
  const macs: string[] = [];
  const badLines: number[] = [];
  lines.forEach((line, i) => {
    const t = line.trim();
    let mac: string | null = null;
    if (mode === "log") {
      const m = t.match(LOG_MAC);
      if (m) mac = canonical(m[1] as string);
    } else if (t === "" || t.startsWith("#")) {
      return;
    } else if (BARE_MAC.test(t)) {
      mac = canonical(t);
    } else {
      badLines.push(i + 1);
    }
    if (mac && !seen.has(mac)) {
      seen.add(mac);
      macs.push(mac);
    }
  });
  return { macs, badLines, mode };
}

/** Split into the route's batch size. */
export function chunk<T>(xs: readonly T[], size: number): T[][] {
  const out: T[][] = [];
  for (let i = 0; i < xs.length; i += size) out.push(xs.slice(i, i + size));
  return out;
}

const count = (x: unknown): number => (typeof x === "number" && Number.isInteger(x) && x >= 0 ? x : NaN);
const STATES = new Set(["not_listed", "listed", "activated"]);

/** One line for the terminal: counts or a state, never an id. null when the answer is not the shape expected. */
export function summarize(op: "add" | "reset" | "status", body: Record<string, unknown>): string | null {
  if (op === "status") {
    const s = body.state;
    return typeof s === "string" && STATES.has(s) ? `status: ${s.replace("_", " ")}` : null;
  }
  if (op === "add") {
    const [a, b, c] = [count(body.listed), count(body.already_listed), count(body.already_activated)];
    if ([a, b, c].some(Number.isNaN)) return null;
    return `add: ${a} newly listed, ${b} already listed, ${c} already activated (left as they were)`;
  }
  const [a, b, c] = [count(body.reopened), count(body.already_open), count(body.not_listed)];
  if ([a, b, c].some(Number.isNaN)) return null;
  return `reset: ${a} re-opened, ${b} already open, ${c} not on the list (reset never lists)`;
}

/** Sum add answers across batches. */
export function sumAdd(bodies: Record<string, unknown>[]): Record<string, number> {
  const t = { listed: 0, already_listed: 0, already_activated: 0 };
  for (const b of bodies) {
    t.listed += count(b.listed);
    t.already_listed += count(b.already_listed);
    t.already_activated += count(b.already_activated);
  }
  return t;
}

/** The route's error code, if it is one of ours; never echo arbitrary text from a response. */
export function errorCode(body: unknown): string {
  const e = (body as { error?: unknown } | null)?.error;
  return typeof e === "string" && /^[a-z_]{1,40}$/.test(e) ? e : "unknown";
}

/* ===========================================================================
 * THE SEED (docs/device-activation.md, "Seeding before the deploy"): Daniel's bench
 * records written as `listed` rows straight into KV, before the admin route exists.
 * ======================================================================== */

const ID16 = /^[0-9a-f]{16}$/;

export interface ParsedRecords {
  mode: "csv" | "ids" | "macs";
  /** csv and ids modes: unique, lowercase, in first-seen order. */
  ids: string[];
  /** macs mode: for the caller to derive with the mint's own function. */
  macs: string[];
  /** 1-based numbers of the lines that are not what the mode expects. Never their content. */
  badLines: number[];
}

/**
 * CSV mode when the first non-blank line is a header naming a `device_id` column (provisioned.csv's
 * shape); ids mode when every content line is 16 hex; otherwise MAC/log mode (parseImport).
 */
export function parseRecords(text: string): ParsedRecords {
  const lines = text.split(/\r?\n/);
  const first = lines.findIndex((l) => l.trim() !== "" && !l.trim().startsWith("#"));
  const seen = new Set<string>();
  const ids: string[] = [];
  const badLines: number[] = [];
  const add = (id: string) => { if (!seen.has(id)) { seen.add(id); ids.push(id); } };

  const header = first >= 0 ? (lines[first] as string).split(",").map((h) => h.trim().toLowerCase()) : [];
  const col = header.indexOf("device_id");
  if (col >= 0) {
    for (let i = first + 1; i < lines.length; i++) {
      const line = lines[i] as string;
      if (line.trim() === "") continue;
      const id = (line.split(",")[col] ?? "").trim().toLowerCase();
      if (ID16.test(id)) add(id);
      else badLines.push(i + 1);
    }
    return { mode: "csv", ids, macs: [], badLines };
  }
  const content = lines.map((l, i) => [l.trim(), i + 1] as const).filter(([l]) => l !== "" && !l.startsWith("#"));
  if (content.length > 0 && content.every(([l]) => ID16.test(l.toLowerCase()))) {
    for (const [l] of content) add(l.toLowerCase());
    return { mode: "ids", ids, macs: [], badLines: [] };
  }
  const p = parseImport(text);
  return { mode: "macs", ids: [], macs: p.macs, badLines: p.badLines };
}

/** The ids that get a new row: every intended id that has none. A seed NEVER rewrites an existing row. */
export function planSeed(intended: readonly string[], existing: ReadonlySet<string>): string[] {
  return intended.filter((id) => !existing.has(id));
}

export type SeedState = "listed" | "activated";

/** The two operations the seed needs from a store; wrangler in the CLI, a Map in the tests. */
export interface SeedStore {
  /** The ids that have an `act:` row now, in any state. */
  listedIds(): Promise<Set<string>>;
  /**
   * Write a row in `state` for each id. May silently drop some (a KV bulk put does). An `activated` row is written
   * with no `gen`: no tool can compute the generation (it is keyed by DEVICE_KEY_SECRET, which only the Worker holds),
   * and the gate reads a gen-less activated row as activated under every generation -- refused, fail closed.
   */
  putRows(ids: readonly string[], state: SeedState): Promise<void>;
}

export interface SeedResult {
  read: number;
  alreadyPresent: number;
  written: number;
  repaired: number;
  missing: number;
  complete: boolean;
}

/**
 * WRITE, VERIFY, REPAIR, VERIFY (CLAUDE.md: a KV bulk put drops keys silently, and `Success!` means
 * nothing). The result is counts only, and it says complete only when the store, read back, holds
 * every intended id.
 */
export async function seedWith(store: SeedStore, intended: readonly string[], state: SeedState = "listed"): Promise<SeedResult> {
  const before = await store.listedIds();
  const toWrite = planSeed(intended, before);
  if (toWrite.length > 0) await store.putRows(toWrite, state);
  let present = await store.listedIds();
  const dropped = toWrite.filter((id) => !present.has(id));
  if (dropped.length > 0) {
    await store.putRows(dropped, state);
    present = await store.listedIds();
  }
  const missing = intended.filter((id) => !present.has(id)).length;
  return {
    read: intended.length,
    alreadyPresent: intended.length - toWrite.length,
    written: toWrite.length,
    repaired: dropped.filter((id) => present.has(id)).length,
    missing,
    complete: missing === 0,
  };
}

export function summarizeSeed(env: string, r: SeedResult): string {
  return `seed (${env}): read ${r.read}, already present ${r.alreadyPresent}, written ${r.written}, ` +
    `repaired ${r.repaired}, missing ${r.missing} -> ${r.complete ? "COMPLETE" : "INCOMPLETE"}`;
}

/* ===========================================================================
 * THE COVERAGE CHECK's arithmetic (scripts/activation-coverage.ts): who authenticated,
 * against who has a row. Counts and one boolean; the ids stay in the caller's memory.
 * ======================================================================== */

export interface Coverage {
  union: number;
  listed: number;
  missing: number;
  missingWithLedgerRow: number;
  missingWithoutLedgerRow: number;
  testIdentityMissing: boolean | null;
}

export function coverage(
  seen: ReadonlySet<string>,
  listed: ReadonlySet<string>,
  ledger: ReadonlySet<string>,
  testDevice: string | null,
): Coverage {
  const missing = [...seen].filter((id) => !listed.has(id));
  const withLedger = missing.filter((id) => ledger.has(id)).length;
  return {
    union: seen.size,
    listed: seen.size - missing.length,
    missing: missing.length,
    missingWithLedgerRow: withLedger,
    missingWithoutLedgerRow: missing.length - withLedger,
    testIdentityMissing: testDevice === null ? null : missing.includes(testDevice),
  };
}

/* ===========================================================================
 * THE SWEEP (accepted ~20:15Z): after #548 deploys, every grandfathered id is read once
 * through the Worker's `status`, which writes the current generation onto it. What the
 * sweep reports is read back from KV, not inferred from the answers.
 * ======================================================================== */

export interface SweepCounts {
  swept: number;
  activated: number;
  stamped: number;
  unstamped: number;
}

/** `states[i]` is what status answered for id i; `gens[i]` is the gen read back from its row (absent: undefined/null). */
export function sweepCounts(states: readonly string[], gens: ReadonlyArray<unknown>): SweepCounts {
  const stamped = gens.filter((g) => typeof g === "string" && /^[0-9a-f]{12}$/.test(g)).length;
  return {
    swept: states.length,
    activated: states.filter((s) => s === "activated").length,
    stamped,
    unstamped: states.length - stamped,
  };
}

export function summarizeSweep(env: string, c: SweepCounts): string {
  return `sweep (${env}): swept ${c.swept}, activated ${c.activated}, stamped ${c.stamped}, unstamped ${c.unstamped}`;
}

/* ===========================================================================
 * A FAILED WRANGLER CALL MUST NAME ITS CAUSE AND NEVER AN ID (G-1, ruled ~20:35Z).
 * G-1's first run showed the tail of stderr -- two copies of a config warning -- while the
 * error itself was on stdout, which was dropped. Both streams go through this.
 * ======================================================================== */

const ANSI = /\x1b\[[0-9;]*m/g;
// wrangler's warning about the env's experimental "unsafe" ratelimit fields: printed on every call, never the cause.
const KNOWN_NOISE = [/unsafe" fields are experimental/, /^\s*▲?\s*\[?WARNING\]?\s*Processing wrangler\.toml configuration:?\s*$/,
  /^\s*-\s*"env\.(production|staging)" environment configuration\s*$/];

export function failureText(stdout: string, stderr: string, cap = 1200): string {
  const lines = `${stderr}\n${stdout}`.replace(ANSI, "").split(/\r?\n/)
    .filter((l) => l.trim() !== "")
    .filter((l) => !/"name"\s*:/.test(l))                 // a key listing is a list of ids: never shown
    .filter((l) => !KNOWN_NOISE.some((re) => re.test(l)))
    .map((l) => l.replace(/[0-9a-f]{16}/gi, "<id>"));
  const text = lines.join("\n");
  return text.length > cap ? `${text.slice(0, cap)}\n...` : text || "(wrangler printed nothing)";
}
