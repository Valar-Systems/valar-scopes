/**
 * Emit the derivation fixture that grades the firmware's port of
 * valar-eam-feed's src/game/derive.ts -- the GameFormat pattern
 * (valar-eam-feed's emit-device-fixture.ts), applied to class and T.
 *
 * ---------------------------------------------------------------------------
 * WHERE THIS LIVES, AND WHAT IT READS FROM THE SERVER
 *
 * Ruled 2026-10-12 (Daniel): "#187: the generator script and the derivation
 * JSON belong in valar-scopes beside #402." So this script, the .json and the
 * .h all live here, in valar-scopes, and this repo is where the vectors change.
 * It was copied from valar-eam-feed (PR #187, 5e43012); valar-eam-feed's own
 * copy predates the move.
 *
 * The derivation's AUTHORITY is still the server's function, so the script
 * calls it from a valar-eam-feed checkout named by --server (default
 * ../valar-eam-feed; those modules need nothing but node:crypto). It imports two
 * modules from there, src/game/config.ts (buildGameConfig: the parameters) and
 * src/game/derive.ts (deriveMessage and its helpers), and writes nothing there.
 *
 * ---------------------------------------------------------------------------
 * THE EXPECTED VALUES ARE THE SERVER FUNCTION'S OUTPUT, NEVER A READING OF IT
 *
 * Every expected class, tier, offset and T below is produced by calling
 * `deriveMessage` -- the same function POST /votes re-derives with. The device
 * port is graded against it with whole-value equality. A fixture of hand-typed
 * expectations would grade the port against somebody's understanding of the
 * spec, which is how two implementations drift while both pass.
 *
 * The CASES are chosen here, and chosen at the edges where a port can disagree
 * (docs/game-derivation.md "Fixture coverage", ruling 2026-09-22):
 *
 *   - a msg_id in each class;
 *   - a class draw EXACTLY on each cumulative boundary, and the tier draw on the
 *     tier boundary: strict `<` sends each to the NEXT bucket;
 *   - T at minS and at maxS-1 for every tier (maxS is exclusive);
 *   - heard_at on a whole minute with a whole-minute offset: the ceiling must
 *     NOT add a minute;
 *   - T landing 59.5 s past a minute, and 0.5 s past one: both ceil up;
 *   - a non-ASCII msg_id, pinning "the UTF-8 bytes of msg_id".
 *
 * Boundary ids are FOUND by deterministic search over realistic ids
 * (`eam-<8 hex>`, the shape src/normalize.ts assigns), so a rerun finds the
 * same ids. The tier-boundary search is the slow one (~20 M hashes).
 *
 * ---------------------------------------------------------------------------
 * PARAMETERS: THE SERVER CHECKOUT'S DEFAULTS, NOT THE DEPLOYED /config
 *
 * The fixture grades the FUNCTION, and the device takes its parameters from
 * /config at runtime, so the parameters are inputs here and are emitted beside
 * the cases. Whether production serves the same numbers is a separate question
 * that valar-eam-feed's emit-device-fixture.ts --check answers against the live
 * service. Env overrides are refused, so a local GAME_WEIGHT_* cannot leak in.
 *
 * ---------------------------------------------------------------------------
 * THE EXECUTION WINDOW VECTORS: THE SAME FILE, A DIFFERENT SOURCE OF TRUTH
 *
 * Ruled 2026-10-11 (Daniel): "ResolveTurn -> T ± 1 s", and "a shared
 * test-vector file that both the device host tests and valar-eam-feed's votes
 * tests run (early edge, late edge, just outside each)". This is that file: one
 * fixture, one emitter, one --check, so enrollment item 5's command covers the
 * window as well as the derivation.
 *
 * The window cases are NOT the server function's output. The derivation's
 * authority is derive.ts, so its expectations are derive.ts's answers. The
 * window's authority is the ruling, and BOTH implementations (votes.ts here,
 * DrillMachine::ResolveTurn on the device) are graded against it. So the
 * verdicts below are written down from the ruling, before either side runs
 * them, and the emitter refuses to write a table whose literal verdicts
 * disagree with the ruling's own arithmetic (a typo guard, not a third
 * implementation anyone grades against).
 *
 * Every instant is a WHOLE MILLISECOND. The device measures microseconds and
 * the wire carries milliseconds; only at whole-ms instants do the two
 * resolutions read the ruling the same way, and which one decides a turn a
 * fraction of a millisecond outside an edge is an open question (design doc
 * "Timings"), so no vector here asserts an answer to it.
 *
 * Emitted as (both committed here, in valar-scopes):
 *   .json  test/fixtures/derivation.json, the record: every case with its
 *          `covers` reason and the server's whole Derivation, plus the window
 *          section. It is what a server test suite reads.
 *   .h     test/fixtures/derivation_fixture.h, what the host suite compiles
 *          (test/host/test_derive.cpp, test/host/test_game_agreement.cpp).
 *
 * Usage, from the valar-scopes root:
 *   npx tsx scripts/emit-derivation-fixture.ts [--server <valar-eam-feed>] --out <file.json> [--header <file.h>]
 *   npx tsx scripts/emit-derivation-fixture.ts --check [--server <valar-eam-feed>] --out <file.json> [--header <file.h>]
 *
 * Exit codes: 0 fine - 1 drift (in --check) - 2 this script could not do its job.
 */
import { execFileSync } from 'node:child_process';
import { existsSync, readFileSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { pathToFileURL } from 'node:url';
import { createHash } from 'node:crypto';

// The shapes this script reads from the server's two modules. Structural only: the
// server's own types (valar-eam-feed src/game/derive.ts, src/types.ts) are the
// definition, and tsx does not type-check across the two checkouts.
interface DerivationParams {
  epoch: number;
  decode: { weights: readonly { class: string; ppm: number }[] };
  timing: { tOffset: readonly { tier: string; ppm: number; minS: number; maxS: number }[] };
}
interface Derivation {
  class: string;
  tier: string | null;
  offset_s: number | null;
  t_at: string | null;
  draws: { class_u32: number; tier_u32: number; offset_u32: number; class_ppm: number; tier_ppm: number };
}
interface ServerConfig extends DerivationParams {
  timing: DerivationParams['timing'] & { executionWindowS: number };
}

// Bound by loadServer() before anything below runs.
let buildGameConfig: () => ServerConfig;
let deriveMessage: (msgId: string, heardAtIso: string, params: DerivationParams) => Derivation;
let derivationParamProblems: (params: DerivationParams) => string[];
let toPpmDraw: (u32: number) => number;

const OVERRIDES = [
  'GAME_WEIGHT_NAM', 'GAME_WEIGHT_FDM', 'GAME_WEIGHT_EXECUTION',
  'GAME_T_NORMAL_WEIGHT', 'GAME_T_SNAP_WEIGHT', 'GAME_T_NORMAL_MIN_S', 'GAME_T_NORMAL_MAX_S',
  'GAME_T_SNAP_MIN_S', 'GAME_T_SNAP_MAX_S', 'GAME_DERIVATION_EPOCH',
];

/** A whole-minute instant every heard_at is built from. Arbitrary, and fixed. */
const ANCHOR_MS = Date.UTC(2026, 8, 22, 14, 0, 0);
const SEARCH_LIMIT = 200_000_000;

function die(msg: string): never {
  console.error(`\nCANNOT EMIT: ${msg}\n`);
  console.error('This is the script failing, not a check failing. Exit 2.');
  process.exit(2);
}

/**
 * Bind the server's functions from a valar-eam-feed checkout. Returns a one-line
 * description of the checkout for the console (never written into the fixture,
 * so --check does not drift every time the server's HEAD moves).
 */
async function loadServer(dir: string): Promise<string> {
  const root = resolve(dir);
  const configTs = resolve(root, 'src/game/config.ts');
  const deriveTs = resolve(root, 'src/game/derive.ts');
  for (const f of [configTs, deriveTs]) {
    if (!existsSync(f)) die(`${f} does not exist. Point --server at a valar-eam-feed checkout (default ../valar-eam-feed).`);
  }
  // A CommonJS module imported from here arrives as its exports, or under `default`.
  const pick = (m: Record<string, unknown>) => ((m.default as Record<string, unknown>) ?? m);
  const cfg = pick(await import(pathToFileURL(configTs).href));
  const der = pick(await import(pathToFileURL(deriveTs).href));
  const fn = (m: Record<string, unknown>, name: string, file: string) => {
    if (typeof m[name] !== 'function') die(`${file} exports no function ${name}; the server's API moved, so this script must follow it`);
    return m[name] as never;
  };
  buildGameConfig = fn(cfg, 'buildGameConfig', configTs);
  deriveMessage = fn(der, 'deriveMessage', deriveTs);
  derivationParamProblems = fn(der, 'derivationParamProblems', deriveTs);
  toPpmDraw = fn(der, 'toPpmDraw', deriveTs);

  let where = root;
  try {
    const head = execFileSync('git', ['-C', root, 'rev-parse', 'HEAD'], { encoding: 'utf8', stdio: ['ignore', 'pipe', 'ignore'] }).trim();
    const dirty = execFileSync('git', ['-C', root, 'status', '--porcelain', '--', 'src/game', 'src/types.ts'], { encoding: 'utf8', stdio: ['ignore', 'pipe', 'ignore'] }).trim();
    where += ` @ ${head}${dirty ? ' (src/game or src/types.ts has UNCOMMITTED changes)' : ''}`;
  } catch {
    where += ' (not a git checkout: commit unknown)';
  }
  return where;
}

const candidate =(n: number) => `eam-${n.toString(16).padStart(8, '0')}`;

/** The first candidate id whose raw draws satisfy `ok`, searched from `from`. */
function search(label: string, ok: (d: Buffer) => boolean, from = 0): string {
  for (let n = from; n < SEARCH_LIMIT; n++) {
    const id = candidate(n);
    if (ok(createHash('sha256').update(Buffer.from(id, 'utf8')).digest())) return id;
  }
  return die(`no id found for "${label}" within ${SEARCH_LIMIT} candidates`);
}

interface Case {
  name: string;
  /** Which coverage requirement this case exists for. */
  covers: string;
  msg_id: string;
  heard_at: string;
  expected: Derivation;
}

function buildCases(params: DerivationParams): Case[] {
  const cases: Case[] = [];
  const add = (name: string, covers: string, msgId: string, heardAt: string) => {
    cases.push({ name, covers, msg_id: msgId, heard_at: heardAt, expected: deriveMessage(msgId, heardAt, params) });
  };
  const anchor = new Date(ANCHOR_MS).toISOString();

  // Class and tier are read from the raw digest with the SAME helpers the
  // function uses (toPpmDraw), and the result is confirmed by deriveMessage
  // itself before it is emitted -- the search only proposes ids.
  const cum = (list: readonly { ppm: number }[]) => list.map((_, i) => list.slice(0, i + 1).reduce((a, w) => a + w.ppm, 0));
  const classCum = cum(params.decode.weights);
  const tierCum = cum(params.timing.tOffset);
  const pickIdx = (c: number[], u: number) => c.findIndex((x) => u < x);
  const clsIdx = (d: Buffer) => pickIdx(classCum, toPpmDraw(d.readUInt32BE(0)));
  const execIdx = params.decode.weights.findIndex((w) => w.class === 'execution');
  const tierIdx = (d: Buffer) => pickIdx(tierCum, toPpmDraw(d.readUInt32BE(4)));

  // 1. One msg_id in each class.
  params.decode.weights.forEach((w, i) => {
    add(`class-${w.class}`, 'a msg_id in each class', search(w.class, (d) => clsIdx(d) === i), anchor);
  });

  // 2. Class draw EXACTLY on each interior cumulative boundary -> the NEXT bucket.
  for (const b of classCum.slice(0, -1)) {
    const id = search(`class boundary ${b}`, (d) => toPpmDraw(d.readUInt32BE(0)) === b);
    add(`class-boundary-${b}`, 'u_ppm exactly on a cumulative boundary falls into the NEXT bucket', id, anchor);
  }
  // ...and the tier draw on each interior tier boundary (only reached for an execution).
  for (const b of tierCum.slice(0, -1)) {
    const id = search(`tier boundary ${b}`, (d) => clsIdx(d) === execIdx && toPpmDraw(d.readUInt32BE(4)) === b);
    add(`tier-boundary-${b}`, 'u_ppm exactly on a cumulative boundary falls into the NEXT bucket', id, anchor);
  }

  // 3. T at minS and at maxS-1 for every tier. heard_at is the whole-minute
  //    anchor, so these double as "heard_at already on a minute boundary".
  params.timing.tOffset.forEach((t, i) => {
    const span = t.maxS - t.minS;
    const at = (want: number) => (d: Buffer) => clsIdx(d) === execIdx && tierIdx(d) === i && d.readUInt32BE(8) % span === want;
    add(`tier-${t.tier}-minS`, 'T at minS for each tier', search(`${t.tier} minS`, at(0)), anchor);
    add(`tier-${t.tier}-maxS-1`, 'T at maxS-1 for each tier', search(`${t.tier} maxS-1`, at(span - 1)), anchor);
  });

  // 4 & 5. The minute ceiling, on one execution id so only heard_at varies.
  const execId = cases.find((c) => c.name === 'class-execution')!.msg_id;
  const offsetS = deriveMessage(execId, anchor, params).offset_s!;
  const heardFor = (rawTMs: number) => new Date(rawTMs - offsetS * 1000).toISOString();
  add('ceil-exact-minute', 'heard_at + offset already on a whole minute: ceil must not add a minute', execId, heardFor(ANCHOR_MS + 20 * 60_000));
  add('ceil-59.5s-past', 'T 59.5 s past a minute boundary ceils to the next minute', execId, heardFor(ANCHOR_MS + 20 * 60_000 + 59_500));
  add('ceil-0.5s-past', 'T 0.5 s past a minute boundary ceils to the next minute', execId, heardFor(ANCHOR_MS + 20 * 60_000 + 500));

  // 6. The UTF-8 bytes of msg_id, not UTF-16 code units and not Latin-1.
  add('utf8-msg-id', 'SHA-256 over the UTF-8 bytes of msg_id', 'eam-é✓-utf8', anchor);

  return cases;
}

// ---------------------------------------------------------------------------
// The execution window (ruled 2026-10-11)
// ---------------------------------------------------------------------------

/** The width these vectors are written for: T +/- 1 s. */
const WINDOW_S = 2;

interface WindowVector {
  name: string;
  /** Which part of the ruling this case exists for. */
  covers: string;
  /** The key turn's instant minus T, as the device measures it. A whole millisecond. */
  deviation_us: number;
  /** In the window: the device commits it AND the server keeps it PENDING. */
  counts: boolean;
}

/**
 * Written down from the ruling, before either side ran them. The edges are
 * INCLUDED: the design doc's §4 ruling 1 says "|dev| <= 1.0 s" and its table
 * scores the 1.0 s window edge (900 m, capped), so the edge is a valid shot.
 */
const WINDOW_VECTORS: readonly WindowVector[] = [
  { name: 'at-t', covers: 'T itself', deviation_us: 0, counts: true },
  {
    name: 'early-inside',
    covers: 'half a second before T counts (the [T, T+2 s] shape ruled 2026-09-28 refused it)',
    deviation_us: -500_000,
    counts: true,
  },
  { name: 'early-edge', covers: 'the early edge, T - 1 s, is IN (|dev| <= 1.0 s)', deviation_us: -1_000_000, counts: true },
  { name: 'late-edge', covers: 'the late edge, T + 1 s, is IN (|dev| <= 1.0 s)', deviation_us: 1_000_000, counts: true },
  {
    name: 'early-just-outside',
    covers: 'one millisecond before the early edge is OUT (whole ms only: sub-ms is an open question)',
    deviation_us: -1_001_000,
    counts: false,
  },
  {
    name: 'late-just-outside',
    covers: 'one millisecond after the late edge is OUT (whole ms only: sub-ms is an open question)',
    deviation_us: 1_001_000,
    counts: false,
  },
  {
    name: 'late-outside',
    covers: '1.5 s after T is OUT (the [T, T+2 s] shape ruled 2026-09-28 accepted it)',
    deviation_us: 1_500_000,
    counts: false,
  },
];

function buildWindow(windowS: number) {
  if (windowS !== WINDOW_S) {
    die(`timing.executionWindowS is ${windowS} in the server checkout (--server); the window vectors are written for ${WINDOW_S} s (T +/- 1 s). Re-rule, then rewrite them.`);
  }
  const halfUs = (WINDOW_S / 2) * 1_000_000;
  const cases = WINDOW_VECTORS.map((v) => {
    if (!Number.isInteger(v.deviation_us) || v.deviation_us % 1000 !== 0) {
      die(`window vector ${v.name}: ${v.deviation_us} us is not a whole millisecond`);
    }
    // The ruling's own arithmetic, as a typo guard on the literal verdicts.
    if (v.counts !== (Math.abs(v.deviation_us) <= halfUs)) {
      die(`window vector ${v.name}: counts=${v.counts} contradicts |dev| <= ${halfUs} us`);
    }
    // What POST /votes/:id/execute carries: whole milliseconds, Math.round (the
    // route's own rounding, src/routes/game.ts; the device's
    // DeviationMsForServer is graded against it).
    return { ...v, wire_ms: Math.round(v.deviation_us / 1000) };
  });
  return {
    ruling: 'ResolveTurn -> T ± 1 s (Daniel, 2026-10-11)',
    edges: 'both included: valar-scopes docs/missileer-game-design.md §4 ruling 1, "|dev| <= 1.0 s", which scores the 1.0 s edge',
    execution_window_s: windowS,
    cases,
  };
}

function buildFixture() {
  const set = OVERRIDES.filter((k) => process.env[k] !== undefined && process.env[k] !== '');
  if (set.length) die(`env overrides are set (${set.join(', ')}); the fixture is emitted from the server checkout's defaults only`);
  const cfg = buildGameConfig();
  const params: DerivationParams = { epoch: cfg.epoch, decode: { weights: cfg.decode.weights }, timing: { tOffset: cfg.timing.tOffset } };
  const problems = derivationParamProblems(params);
  if (problems.length) die(`the derivation parameters are unsound:\n  ${problems.join('\n  ')}`);
  return {
    _README:
      'GENERATED by valar-scopes scripts/emit-derivation-fixture.ts. Do not edit by hand. '
      + 'Every `expected` in `cases` is the output of valar-eam-feed src/game/derive.ts deriveMessage -- the function POST /votes re-derives with. '
      + 'The `window` verdicts are the 2026-10-11 ruling\'s, written in the emitter, and graded on the device and the server both.',
    spec: 'valar-eam-feed docs/game-derivation.md',
    params: { epoch: params.epoch, weights: params.decode.weights, tOffset: params.timing.tOffset },
    cases: buildCases(params),
    window: buildWindow(cfg.timing.executionWindowS),
  };
}

type Fixture = ReturnType<typeof buildFixture>;

const CLASS_ENUM: Record<string, string> = { nam: 'kNam', fdm: 'kFdm', execution: 'kExecution' };
const TIER_ENUM: Record<string, string> = { normal: 'kNormal', snap: 'kSnap' };

/** The C header: parameters, inputs, and whole expected values. */
function renderHeader(fx: Fixture): string {
  const esc = (s: string) => Array.from(Buffer.from(s, 'utf8'))
    .map((b) => (b >= 0x20 && b < 0x7f && b !== 0x22 && b !== 0x5c ? String.fromCharCode(b) : `\\x${b.toString(16).padStart(2, '0')}""`))
    .join('');
  const L: string[] = [];
  L.push('// GENERATED FILE -- DO NOT EDIT.');
  L.push('//');
  L.push('// valar-scopes    scripts/emit-derivation-fixture.ts');
  L.push('//   derivation    valar-eam-feed src/game/derive.ts, called by that script');
  L.push('//   spec          valar-eam-feed docs/game-derivation.md');
  L.push(`//   epoch         ${fx.params.epoch}`);
  L.push('//');
  L.push("// Every expected value below is the SERVER's own deriveMessage() output -- the");
  L.push('// function POST /votes re-derives with and rejects a mismatch against. Grade');
  L.push('// the port with whole-value equality; never loosen a comparison to pass.');
  L.push('');
  L.push('#ifndef VALAR_TEST_DERIVATION_FIXTURE_H');
  L.push('#define VALAR_TEST_DERIVATION_FIXTURE_H');
  L.push('');
  L.push('#include <stddef.h>');
  L.push('#include <stdint.h>');
  L.push('');
  L.push('namespace derivation_fixture {');
  L.push('');
  L.push('enum class Class : uint8_t { kNam, kFdm, kExecution };');
  L.push('enum class Tier : uint8_t { kNone, kNormal, kSnap };');
  L.push('');
  L.push('/// The parameters, as /config serves them: ORDERED, integer ppm.');
  L.push('struct Weight { Class cls; uint32_t ppm; };');
  L.push('struct TierParam { Tier tier; uint32_t ppm; uint32_t min_s; uint32_t max_s; };');
  L.push(`constexpr uint32_t kEpoch = ${fx.params.epoch};`);
  L.push('constexpr Weight kWeights[] = {');
  for (const w of fx.params.weights) L.push(`    { Class::${CLASS_ENUM[w.class]}, ${w.ppm} },`);
  L.push('};');
  L.push('constexpr TierParam kTiers[] = {');
  for (const t of fx.params.tOffset) L.push(`    { Tier::${TIER_ENUM[t.tier]}, ${t.ppm}, ${t.minS}, ${t.maxS} },`);
  L.push('};');
  L.push('');
  L.push('struct Case {');
  L.push('    const char* name;');
  L.push('    const char* msg_id;     // UTF-8');
  L.push('    const char* heard_at;   // exactly as served');
  L.push('    uint32_t class_u32, tier_u32, offset_u32;');
  L.push('    uint32_t class_ppm, tier_ppm;');
  L.push('    Class cls;');
  L.push('    Tier tier;              // kNone unless execution');
  L.push('    int32_t offset_s;       // -1 unless execution');
  L.push('    int64_t t_at_ms;        // Unix ms; -1 unless execution');
  L.push('    const char* t_at;       // ISO-8601 as the server renders it; "" unless execution');
  L.push('    const char* class_wire; // the class as POST /votes takes it ("nam", "fdm", "execution")');
  L.push('};');
  L.push('');
  L.push('constexpr Case kCases[] = {');
  for (const c of fx.cases) {
    const e = c.expected;
    L.push(`    // ${c.covers}`);
    L.push(
      `    { "${esc(c.name)}", "${esc(c.msg_id)}", "${esc(c.heard_at)}",`
      + ` ${e.draws.class_u32}u, ${e.draws.tier_u32}u, ${e.draws.offset_u32}u, ${e.draws.class_ppm}u, ${e.draws.tier_ppm}u,`
      + ` Class::${CLASS_ENUM[e.class]}, Tier::${e.tier ? TIER_ENUM[e.tier] : 'kNone'}, ${e.offset_s ?? -1},`
      + ` ${e.t_at ? `${Date.parse(e.t_at)}LL` : '-1LL'}, "${e.t_at ?? ''}", "${e.class}" },`,
    );
  }
  L.push('};');
  L.push('constexpr size_t kCaseCount = sizeof(kCases) / sizeof(kCases[0]);');
  L.push('');
  const w = fx.window;
  L.push('// ---------------------------------------------------------------------------');
  L.push(`// THE EXECUTION WINDOW. Ruled: ${w.ruling}.`);
  L.push(`// Edges: ${w.edges}.`);
  L.push('//');
  L.push("// NOT the server function's output, unlike kCases: these verdicts are the");
  L.push("// RULING's, written in the emitter, and the server's votes tests run the same");
  L.push('// table against executeVote. Every instant is a whole millisecond: which side');
  L.push('// decides a turn a fraction of a millisecond outside an edge is an open');
  L.push('// question, so no case here answers it.');
  L.push('// ---------------------------------------------------------------------------');
  L.push('');
  L.push('/// The width the cases are written for, as /config serves it (timing.executionWindowS).');
  L.push(`constexpr uint32_t kWindowWidthUs = ${Math.round(w.execution_window_s * 1e6)}u;`);
  L.push('');
  L.push('struct WindowCase {');
  L.push('    const char* name;');
  L.push('    int64_t deviation_us;   // the key turn minus T, as the device measures it');
  L.push('    int64_t wire_ms;        // what POST /votes/:id/execute carries: Math.round(deviation_us / 1000)');
  L.push('    bool counts;            // in the window: the device commits it, the server keeps it PENDING');
  L.push('};');
  L.push('');
  L.push('constexpr WindowCase kWindowCases[] = {');
  for (const c of w.cases) {
    L.push(`    // ${c.covers}`);
    L.push(`    { "${esc(c.name)}", ${c.deviation_us}LL, ${c.wire_ms}LL, ${c.counts ? 'true' : 'false'} },`);
  }
  L.push('};');
  L.push('constexpr size_t kWindowCaseCount = sizeof(kWindowCases) / sizeof(kWindowCases[0]);');
  L.push('');
  L.push('}  // namespace derivation_fixture');
  L.push('');
  L.push('#endif  // VALAR_TEST_DERIVATION_FIXTURE_H');
  return L.join('\n') + '\n';
}

function parseArgs() {
  const a = process.argv.slice(2);
  const get = (k: string) => {
    const i = a.indexOf(k);
    return i >= 0 ? a[i + 1] : undefined;
  };
  const out = get('--out');
  if (!out) {
    console.error('usage: emit-derivation-fixture.ts [--server <valar-eam-feed checkout>] --out <file.json> [--header <file.h>] [--check]');
    process.exit(2);
  }
  return { out, header: get('--header'), check: a.includes('--check'), server: get('--server') ?? '../valar-eam-feed' };
}

async function main() {
  const args = parseArgs();
  console.log(`server: ${await loadServer(args.server)}`);
  const fresh = buildFixture();
  const json = JSON.stringify(fresh, null, 2) + '\n';
  const lf = (s: string) => s.replace(/\r\n/g, '\n');

  if (!args.check) {
    writeFileSync(args.out, json, 'utf8');
    console.log(`wrote ${args.out} (${fresh.cases.length} derivation cases, ${fresh.window.cases.length} window cases)`);
    if (args.header) {
      writeFileSync(args.header, renderHeader(fresh), 'utf8');
      console.log(`wrote ${args.header}`);
    }
    return;
  }

  if (!existsSync(args.out)) die(`--check: ${args.out} does not exist`);
  let drift = false;
  if (lf(readFileSync(args.out, 'utf8')) !== json) {
    console.error(`DRIFT: ${args.out} no longer matches the server's src/game/derive.ts (at --server) or the window vectors in this script. Regenerate it and re-grade the device port.`);
    drift = true;
  } else {
    console.log(`ok   ${args.out} matches deriveMessage() and the window vectors`);
  }
  if (args.header) {
    // LF-normalised: valar-scopes is checked out with core.autocrlf on Windows.
    const was = existsSync(args.header) ? lf(readFileSync(args.header, 'utf8')) : '';
    if (was !== renderHeader(fresh)) {
      console.error(`DRIFT: ${args.header} does not match what this script renders. Hand-edited, or emitted by another version.`);
      drift = true;
    } else {
      console.log(`ok   ${args.header} matches`);
    }
  }
  if (drift) process.exit(1);
}

main().catch((e: unknown) => die(e instanceof Error ? (e.stack ?? e.message) : String(e)));
