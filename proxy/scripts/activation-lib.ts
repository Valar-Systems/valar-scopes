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
