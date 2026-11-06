// docs/smoke-basemap-query.md: the smoke's /basemap check sends the query a device sends. Both sides of the contract
// are read as TEXT -- the smoke script itself and Weatherscope's make_package.py -- and the smoke's URL goes through the
// route's own parser. SQ-C reproduces the defect this replaced: lat, lon and r alone are refused bad_eps, which the
// inert 503 hid until a binding existed.
import { describe, expect, it } from "vitest";
import { parseQuery } from "../src/basemap/query";
import smoke from "../scripts/smoke-prod.sh?raw";
import makePackage from "../../tools/weatherscope/basemap/make_package.py?raw";

const smokeLines = smoke.split("\n").map((l) => l.replace(/\r$/, ""));
/** A shell default, `NAME="${NAME:-value}"`: the value. */
function shDefault(name: string): string | undefined {
  const line = smokeLines.find((l) => l.startsWith(`${name}="\${${name}:-`));
  return line ? line.slice(line.indexOf(":-") + 2, line.lastIndexOf("}")) : undefined;
}
/** A shell constant, `NAME=value` or `NAME="value"`: the value. */
function shConst(name: string): string | undefined {
  const line = smokeLines.find((l) => l.startsWith(`${name}=`));
  return line ? line.slice(name.length + 1).replace(/^"|"$/g, "") : undefined;
}
const pyLines = makePackage.split("\n");
/** A Python integer constant, `NAME = 97_000_000  # ...`. */
function pyInt(name: string): number {
  const line = pyLines.find((l) => l.startsWith(`${name} =`) || l.startsWith(`${name}=`)) ?? "";
  return Number((line.split("=")[1] ?? "").split("#")[0]!.trim().replace(/_/g, ""));
}
/** A Python tuple constant, `NAME = (a, b, c)`: its items as text. */
function pyTuple(name: string): string[] {
  const line = pyLines.find((l) => l.startsWith(`${name} =`) || l.startsWith(`${name}=`)) ?? "";
  const inner = line.slice(line.indexOf("(") + 1, line.indexOf(")"));
  return inner.split(",").map((s) => s.trim().replace(/^"|"$/g, "").replace(/_/g, "")).filter((s) => s !== "");
}

/** The smoke's /basemap URL with its own defaults and constants filled in. */
function smokeUrl(): string {
  const line = smokeLines.find((l) => l.includes("/api/v1/blipscope/basemap?")) ?? "";
  const start = line.indexOf("$BASE/api/v1/blipscope/basemap?");
  const tmpl = start < 0 ? "" : line.slice(start + "$BASE".length, line.indexOf('"', start));
  const vars: Record<string, string | undefined> = {
    LAT: shDefault("LAT"), LON: shDefault("LON"), R: shDefault("R"),
    BM_R: shConst("BM_R"), BM_EPS: shConst("BM_EPS"), BM_LAYERS: shConst("BM_LAYERS"), BM_LEVELS: shConst("BM_LEVELS"),
  };
  return "https://x.invalid" + tmpl.replace(/\$([A-Z_]+)/g, (_m, v: string) => vars[v] ?? `<unset:${v}>`);
}

describe("SQ: the smoke's /basemap query is the device's", () => {
  it("SQ1: the smoke's URL, filled from its own defaults, parses with the route's own parseQuery", () => {
    const url = smokeUrl();
    expect(url, "CONTROL: the smoke's /basemap request is found, every variable resolved").toMatch(/\/api\/v1\/blipscope\/basemap\?/);
    expect(url).not.toContain("<unset:");
    const p = parseQuery(new URL(url));
    expect(p.ok ? "ok" : p.code).toBe("ok");
  });

  it("SQ2: its r, eps, levels and layers are Weatherscope's, read from make_package.py", () => {
    const q = new URL(smokeUrl()).searchParams;
    const levels = (q.get("levels") ?? "").split(",").filter(Boolean).map((p) => p.split(":").map(Number))
      .sort((a, b) => a[0]! - b[0]!);
    const n = pyInt("LEVEL_N");
    const want = pyTuple("LEVELS_MM").map(Number).sort((a, b) => a - b).map((r) => [r, n]);
    expect({
      r: Number(q.get("r")),
      eps: Number(q.get("eps")),
      levels,
      layers: (q.get("layers") ?? "").split(",").sort(),
    }).toEqual({
      r: pyInt("R_MM") / 1_000_000,
      eps: pyInt("TOLERANCE_MM"),
      levels: want,
      // docs/basemap-route.md §3: the route's six layers, all of which Weatherscope asks for (line 2116)
      layers: ["peaks", "places", "rivers", "roads", "terrain", "water"],
    });
    expect(want.length, "CONTROL: make_package.py's three levels are read").toBe(3);
    for (const l of (q.get("layers") ?? "").split(",").filter((x) => x !== "terrain")) {
      expect(pyTuple("LAYERS"), `make_package.py builds the ${l} layer`).toContain(l);
    }
  });

  it("SQ-C: the URL as it stood before (lat, lon and r only) is refused bad_eps -- the defect, through the route's parser", () => {
    const old = `https://x.invalid/api/v1/blipscope/basemap?lat=${shDefault("LAT")}&lon=${shDefault("LON")}&r=${shDefault("R")}`;
    const p = parseQuery(new URL(old));
    expect(p.ok ? "ok" : p.code).toBe("bad_eps");
  });
});
