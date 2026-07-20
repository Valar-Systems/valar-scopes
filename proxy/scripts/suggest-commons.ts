/**
 * suggest-commons.ts -- candidate finder for the civil photo long tail.
 *
 * The harvest tool needs an EXACT Commons "File:..." title per pick. Hand-finding
 * those for dozens of civil types is the slow part; this automates the search
 * half: for each {target, query} it queries the Commons search API for File-space
 * matches, pulls imageinfo+extmetadata, runs the SAME classifyLicense gate the
 * harvest/ingest use, scores for "a clean side-on airliner/GA shot", and prints
 * the best gate-passing candidate (plus alternates) as a ready-to-paste picksheet
 * row. NOTHING is written and no image is downloaded -- this only proposes titles
 * a human then eyeballs before running `npm run harvest`.
 *
 *   npx tsx scripts/suggest-commons.ts            # the built-in priority list
 *   npx tsx scripts/suggest-commons.ts C172 B712  # only these targets
 */
import { classifyLicense } from "../src/photolicense";

const API = "https://commons.wikimedia.org/w/api.php";
const UA = "BlipscopePhotoHarvest/1.0 (+daniel@valarsystems.com)";

// target -> Commons search query. Queries are tight so the type name lands in the
// title; the scorer below still filters out interiors/cockpits/models/wrecks.
const PRIORITY: Record<string, string> = {
  // --- airline (highest desk-radar traffic) ---
  E175: "Embraer E175 airline",
  B712: "Boeing 717 airline",
  BCS3: "Airbus A220-300",
  BCS1: "Airbus A220-100",
  A19N: "Airbus A319neo",
  B37M: "Boeing 737 MAX 7",
  B772: "Boeing 777-200",
  B744: "Boeing 747-400",
  B748: "Boeing 747-8",
  B762: "Boeing 767-200",
  B764: "Boeing 767-400",
  MD11: "McDonnell Douglas MD-11",
  E195: "Embraer 195",
  E290: "Embraer 190-E2",
  AT76: "ATR 72-600",
  // --- general aviation (high count on a local radar) ---
  C172: "Cessna 172 in flight",
  C182: "Cessna 182 in flight",
  SR20: "Cirrus SR20 aircraft",
  P28A: "Piper PA-28 Cherokee",
  BE36: "Beechcraft Bonanza A36",
  DA40: "Diamond DA40 aircraft",
  M20P: "Mooney M20 aircraft",
  TBM9: "Daher TBM 930",
  PC24: "Pilatus PC-24",
  // --- Cessna long tail (GA singles + the Citation family) ---
  C206: "Cessna 206 Stationair in flight",
  C210: "Cessna 210 Centurion aircraft",
  C525: "Cessna CitationJet CJ1 525",
  C25A: "Cessna Citation CJ2 525A",
  C25C: "Cessna Citation CJ4 525C",
  C510: "Cessna Citation Mustang 510",
  C550: "Cessna Citation II 550 aircraft",
  C560: "Cessna Citation V 560 aircraft",
  C680: "Cessna Citation Sovereign 680",
  C68A: "Cessna Citation Latitude 680A",
  // --- Embraer long tail (regional jets + the Phenom/Legacy bizjets) ---
  E135: "Embraer ERJ-135 aircraft",
  E295: "Embraer E195-E2 aircraft",
  E50P: "Embraer Phenom 100 aircraft",
  E55P: "Embraer Phenom 300 aircraft",
  E545: "Embraer Legacy 450 aircraft",
  E550: "Embraer Legacy 500 aircraft",
  // --- traffic-weighted batch 1: cargo/mainline gaps + fast-growing jets ---
  A306: "Airbus A300-600 aircraft",
  B733: "Boeing 737-300 aircraft",
  B734: "Boeing 737-400 aircraft",
  B735: "Boeing 737-500 aircraft",
  B773: "Boeing 777-300 aircraft",
  B77L: "Boeing 777-200LR aircraft",
  B78X: "Boeing 787-10 Dreamliner",
  A35K: "Airbus A350-1000 aircraft",
  A19N: "Airbus A319neo aircraft",
  A338: "Airbus A330-800 aircraft",
  A339: "Airbus A330-900 aircraft",
  DH8A: "De Havilland Dash 8-100 aircraft",
  DH8B: "De Havilland Dash 8-200 aircraft",
  AT46: "ATR 42-600 aircraft",
  SF34: "Saab 340 aircraft in flight",
  F100: "Fokker 100 aircraft",
  SW4: "Fairchild Metroliner SA227 aircraft",
  B190: "Beechcraft 1900D aircraft",
  SF50: "Cirrus Vision SF50 jet",
  HDJT: "Honda HA-420 HondaJet aircraft",
  // --- batch 2: the rest of the named-but-photoless tail ---
  // airliners
  A310: "Airbus A310 airline",
  A318: "Airbus A318 airline",
  A342: "Airbus A340-200 aircraft",
  A343: "Airbus A340-300 aircraft",
  A345: "Airbus A340-500 aircraft",
  A346: "Airbus A340-600 aircraft",
  B3XM: "Boeing 737 MAX 10",
  B462: "British Aerospace BAe 146-200",
  B463: "British Aerospace BAe 146-300",
  B736: "Boeing 737-600 aircraft",
  B753: "Boeing 757-300 aircraft",
  CRJX: "Bombardier CRJ1000 aircraft",
  D328: "Dornier 328 aircraft",
  F70: "Fokker 70 aircraft",
  RJ1H: "Avro RJ100 aircraft",
  RJ85: "Avro RJ85 aircraft",
  SB20: "Saab 2000 aircraft",
  SU95: "Sukhoi Superjet 100 aircraft",
  AT43: "ATR 42-300 aircraft",
  AT45: "ATR 42-500 aircraft",
  AT75: "ATR 72-500 aircraft",
  MD82: "McDonnell Douglas MD-82 aircraft",
  MD83: "McDonnell Douglas MD-83 aircraft",
  MD88: "McDonnell Douglas MD-88 aircraft",
  MD90: "McDonnell Douglas MD-90 aircraft",
  // bizjets
  BE40: "Raytheon Beechjet 400A aircraft",
  CL30: "Bombardier Challenger 300 aircraft",
  CL60: "Bombardier Challenger 604 aircraft",
  F900: "Dassault Falcon 900 aircraft",
  FA7X: "Dassault Falcon 7X aircraft",
  FA8X: "Dassault Falcon 8X aircraft",
  GALX: "IAI Gulfstream G200 Galaxy aircraft",
  GL5T: "Bombardier Global 5000 aircraft",
  GL7T: "Bombardier Global 7500 aircraft",
  GLF4: "Gulfstream IV aircraft",
  H25B: "Hawker 800 aircraft",
  LJ60: "Learjet 60 aircraft",
  LJ75: "Learjet 75 aircraft",
  // GA
  BE33: "Beechcraft Bonanza 33 aircraft",
  BE55: "Beechcraft Baron 55 aircraft",
  BE58: "Beechcraft Baron 58 aircraft",
  DA42: "Diamond DA42 aircraft in flight",
  DA62: "Diamond DA62 aircraft in flight",
  DHC6: "De Havilland Canada DHC-6 Twin Otter",
  P28B: "Piper PA-28R Arrow aircraft",
  PA31: "Piper PA-31 Navajo aircraft",
  PA34: "Piper PA-34 Seneca aircraft",
  PA46: "Piper PA-46 Malibu aircraft",
  RV7: "Van's RV-7 aircraft",
  RV10: "Van's RV-10 aircraft",
  SR2T: "Cirrus SR22T aircraft",
  TBM8: "Socata TBM 850 aircraft",
  // rotor
  A109: "AgustaWestland AW109 helicopter",
  A139: "AgustaWestland AW139 helicopter",
  AS50: "Eurocopter AS350 Ecureuil helicopter",
  AS65: "Eurocopter AS365 Dauphin helicopter",
  B412: "Bell 412 helicopter",
  B429: "Bell 429 helicopter",
  EC30: "Eurocopter EC130 helicopter",
  EH10: "AgustaWestland AW101 Merlin helicopter",
  NH90: "NHIndustries NH90 helicopter",
  S92: "Sikorsky S-92 helicopter",
  // military
  A400: "Airbus A400M Atlas aircraft",
  C295: "Airbus C295 aircraft",
  EUFI: "Eurofighter Typhoon aircraft",
  HAWK: "BAE Systems Hawk aircraft",
  MC12: "Beechcraft MC-12 Liberty aircraft",
  PC21: "Pilatus PC-21 aircraft",
  A338: "Airbus A330-800neo aircraft",
  // --- bizjet / rotor (common secondary) ---
  C700: "Cessna Citation Longitude",
  C750: "Cessna Citation X",
  GLF6: "Gulfstream G650",
  LJ45: "Learjet 45",
  LJ35: "C-21A United States Air Force Learjet",
  EC35: "Eurocopter EC135 helicopter in flight",
  EPIC: "Epic E1000 LT aircraft",
  R22: "Robinson R22 helicopter",
  R66: "Robinson R66 helicopter",
  B06: "Bell 206 JetRanger",
  S76: "Sikorsky S-76 helicopter",
};

interface Page {
  title: string;
  imageinfo?: Array<{
    url: string;
    width: number;
    height: number;
    mime: string;
    descriptionurl: string;
    extmetadata?: Record<string, { value: string }>;
  }>;
}

function stripHtml(s: string): string {
  return s.replace(/<[^>]*>/g, "").replace(/\s+/g, " ").trim();
}

async function searchTitles(query: string): Promise<string[]> {
  const u = new URL(API);
  u.searchParams.set("action", "query");
  u.searchParams.set("list", "search");
  u.searchParams.set("srsearch", query);
  u.searchParams.set("srnamespace", "6"); // File:
  u.searchParams.set("srlimit", "20");
  u.searchParams.set("format", "json");
  const res = await fetch(u, { headers: { "User-Agent": UA } });
  if (!res.ok) return [];
  const body = (await res.json()) as { query?: { search?: Array<{ title: string }> } };
  return (body.query?.search ?? []).map((s) => s.title);
}

async function infoFor(titles: string[]): Promise<Page[]> {
  if (titles.length === 0) return [];
  const u = new URL(API);
  u.searchParams.set("action", "query");
  u.searchParams.set("titles", titles.join("|"));
  u.searchParams.set("prop", "imageinfo");
  u.searchParams.set("iiprop", "extmetadata|url|size|mime");
  u.searchParams.set("format", "json");
  const res = await fetch(u, { headers: { "User-Agent": UA } });
  if (!res.ok) return [];
  const body = (await res.json()) as { query?: { pages?: Record<string, Page> } };
  return Object.values(body.query?.pages ?? {});
}

// Reject obvious non-photos-of-a-flying-aircraft from the title alone.
const BAD = /cockpit|interior|cabin|seat|panel|instrument|engine|wing|tail|gear|model|diagram|drawing|crash|wreck|accident|memorial|scale|toy|patch|logo|map|route|registration|livery detail/i;
const GOOD = /in flight|inflight|landing|takeoff|take-off|taking off|approach|departure|airborne|climb|final|taxi/i;

function score(p: Page, target: string): number {
  const ii = p.imageinfo?.[0];
  if (!ii) return -1e9;
  if (!/^image\/jpeg$/.test(ii.mime)) return -1e9; // device decoder wants baseline JPEG
  const cls = classifyLicense(stripHtml(ii.extmetadata?.LicenseShortName?.value ?? ""));
  const allowed = new Set(["PD-USGov", "CC-BY", "CC-BY-SA", "OGL", "own"]);
  if (!allowed.has(cls)) return -1e9;
  const t = p.title;
  if (BAD.test(t)) return -1e9;
  let s = 0;
  if (ii.width > ii.height) s += 50; // landscape frames the aircraft
  if (ii.width >= 1200) s += 10;
  if (GOOD.test(t)) s += 30;
  if (cls === "CC-BY" || cls === "PD-USGov") s += 8; // no SA obligation
  // aspect close to the 3:2 device sprite is a mild plus
  const ar = ii.width / Math.max(1, ii.height);
  if (ar >= 1.3 && ar <= 1.9) s += 12;
  return s;
}

async function main(): Promise<void> {
  const only = process.argv.slice(2).filter((a) => !a.startsWith("--"));
  const targets = only.length ? only : Object.keys(PRIORITY);

  for (const target of targets) {
    const query = PRIORITY[target] ?? `${target} aircraft`;
    const titles = await searchTitles(query);
    const pages = await infoFor(titles);
    const ranked = pages
      .map((p) => ({ p, s: score(p, target) }))
      .filter((r) => r.s > -1e8)
      .sort((a, b) => b.s - a.s);

    if (ranked.length === 0) {
      console.log(`\n## ${target}  (${query})  -- NO gate-passing candidate found`);
      continue;
    }
    console.log(`\n## ${target}  (${query})`);
    ranked.slice(0, 4).forEach((r, i) => {
      const ii = r.p.imageinfo![0];
      const cls = classifyLicense(stripHtml(ii.extmetadata?.LicenseShortName?.value ?? ""));
      const art = stripHtml(ii.extmetadata?.Artist?.value ?? "");
      const mark = i === 0 ? "PICK" : " alt";
      console.log(`  ${mark} [${cls}] ${ii.width}x${ii.height} ${JSON.stringify(r.p.title)}  -- ${art.slice(0, 48)}`);
    });
    // ready-to-paste picksheet row for the top pick
    const best = ranked[0].p;
    console.log(`  ROW {"target":"${target}","kind":"type","layer":"auto","title":${JSON.stringify(best.title)}}`);
  }
}

main().catch((e) => {
  console.error(String(e instanceof Error ? e.stack : e));
  process.exit(1);
});
