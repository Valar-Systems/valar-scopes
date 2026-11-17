// device-label.selftest.mts -- scripts/device-label.ts (docs/workers-logs-invocation-off.md, section 8), against EXAMPLE
// ids only: 0000000000000000 and 0123456789abcdef are example data, never a device. Their labels were computed outside
// this code, with Python's hashlib. Rows H1-H6, then S-DLECHO: a copy of the helper that prints its INPUT instead of the
// label. The same checks must catch that copy, or H1-H6 passing proves nothing.
//   npx tsx scripts/device-label.selftest.mts
import { spawnSync } from "node:child_process";
import { existsSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const PROXY = join(dirname(fileURLToPath(import.meta.url)), "..");
const TSX = join(PROXY, "node_modules", "tsx", "dist", "cli.mjs");
const HELPER = join(PROXY, "scripts", "device-label.ts");
const EX0 = "0000000000000000", EX0_LABEL = "fcdb4b42"; // EXAMPLE id
const EX1 = "0123456789abcdef", EX1_LABEL = "9f9f5111"; // EXAMPLE id

let off = 0;
function row(name: string, ok: boolean, detail: string): void {
  console.log(`${ok ? "PASS" : "FAIL"} ${name}: ${detail}`);
  if (!ok) off++;
}

type Run = { code: number | null; out: string; err: string };
function run(script: string, input: string, args: string[] = []): Run {
  const r = spawnSync(process.execPath, [TSX, script, ...args], { input, encoding: "utf8", cwd: PROXY });
  return { code: r.status, out: r.stdout ?? "", err: r.stderr ?? "" };
}
const holdsAnId = (r: Run) => [EX0, EX1].some((id) => (r.out + r.err).toLowerCase().includes(id));

/** H1-H6 against one script. Returns the names of the rows that failed; prints nothing of the runs' output. */
function checks(script: string): string[] {
  const f: string[] = [];
  const h1 = run(script, `${EX0}\n`);
  if (!(h1.code === 0 && h1.out === `${EX0_LABEL}\n`)) f.push("H1");
  const h2 = run(script, `  ${EX1.toUpperCase()} \r\n`);
  if (!(h2.code === 0 && h2.out === `${EX1_LABEL}\n`)) f.push("H2");
  const h3 = run(script, "not-a-device-id-zz");
  if (!(h3.code === 2 && h3.out === "" && !h3.err.includes("not-a-device-id-zz"))) f.push("H3");
  const h4 = run(script, "", [EX0]);
  if (!(h4.code === 2 && h4.out === "")) f.push("H4");
  const h5 = run(script, `${EX0}\n${EX1}\n`);
  if (!(h5.code === 2 && h5.out === "")) f.push("H5");
  if ([h1, h2, h3, h4, h5].some(holdsAnId)) f.push("H6");
  return f;
}

// The rows, one line each, never the helper's own output.
if (!existsSync(HELPER)) {
  row("H1-H6", false, "scripts/device-label.ts does not exist");
} else {
  const f = checks(HELPER);
  const say: Record<string, string> = {
    H1: `an example id on stdin prints exactly its label (${EX0_LABEL}) and a newline, exit 0`,
    H2: "the same rule as the Worker: surrounding space and upper case normalise to the same label",
    H3: "a malformed input is refused (exit 2), with no stdout and the input not quoted",
    H4: "an id given as an ARGUMENT is refused (exit 2), with no stdout",
    H5: "two ids on stdin is not ONE id: refused (exit 2), with no stdout",
    H6: "no run's stdout or stderr holds an input id",
  };
  for (const h of ["H1", "H2", "H3", "H4", "H5", "H6"]) row(h, !f.includes(h), say[h]!);
}

// S-DLECHO: a copy that prints its input instead of the label. The same checks must fail it on H1 and H6 at least.
const anchor = "process.stdout.write(`${await deviceLabel(id)}\\n`);";
const src = existsSync(HELPER) ? readFileSync(HELPER, "utf8") : "";
if (src.split(anchor).length !== 2) {
  row("S-DLECHO", false, "anchor not found exactly once in scripts/device-label.ts (BLIND: the sabotage cannot be planted)");
} else {
  const sab = join(PROXY, "scripts", "zz-device-label-sabotaged.ts");
  writeFileSync(sab, src.replace(anchor, "process.stdout.write(raw); // SABOTAGE S-DLECHO"));
  try {
    const f = checks(sab);
    row("S-DLECHO", f.includes("H1") && f.includes("H6"), `the echoing copy fails ${f.length ? f.join(", ") : "nothing"} (H1 and H6 must be among them)`);
  } finally {
    rmSync(sab, { force: true });
  }
}

console.log(off ? `SELFTEST: ${off} row(s) off` : "SELFTEST PASSED");
process.exit(off ? 1 : 0);
