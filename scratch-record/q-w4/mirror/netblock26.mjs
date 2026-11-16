// SCRATCH (map-server): step8_26.pred Amendment 5b, R5b-0 (with 5b.1's condition). Proves the egress block that every node row of 5b
// except the R2 read runs under: NODE_USE_ENV_PROXY=1 with HTTPS_PROXY and HTTP_PROXY = http://127.0.0.1:9 (a closed port) and NO_PROXY
// unset. THE SAME CLIENT AS THE COVERAGE STEP'S HEAD WORKER: ingest-basemap.ts:462 is `await fetch(u, { method: "HEAD", headers: UA })`,
// Node's global fetch, inside coverageAndTiles' nested worker (not exported, so it cannot be imported). This file makes the same call:
// global fetch, method HEAD, on tiles.json's first url. The headers are left out because they do not route a request.
//   BLOCKED only when the error is a refused connection to THE PROXY, 127.0.0.1:9 (the tile host is not 127.0.0.1, so only a
//     proxied request can be refused there) -> exit 0.
//   NOT BLOCKED (any HTTP response) -> exit 1: the block does not hold, and that one HEAD reached the tile host.
//   anything else (DNS, no network, a timeout, a refusal at another address) -> exit 3, UNCLEAR: it cannot separate "the block works"
//     from "the network happened to be down".
//   node mirror/netblock26.mjs <tiles.json>
import { readFileSync } from "node:fs";

const url = JSON.parse(readFileSync(process.argv[2], "utf8")).files[0].url;
const e = process.env;
console.log(`env: NODE_USE_ENV_PROXY=${e.NODE_USE_ENV_PROXY} HTTPS_PROXY=${e.HTTPS_PROXY} HTTP_PROXY=${e.HTTP_PROXY} NO_PROXY=${e.NO_PROXY ?? e.no_proxy ?? "(unset)"}`);
try {
  const r = await fetch(url, { method: "HEAD", signal: AbortSignal.timeout(15000) });
  console.log(`NOT BLOCKED: HTTP ${r.status}`);
  process.exit(1);
} catch (err) {
  const c = err?.cause;
  const atProxy = c?.code === "ECONNREFUSED" && c?.address === "127.0.0.1" && c?.port === 9;
  console.log(`error: ${err?.name}; cause ${c?.code ?? c?.name ?? "(none)"} at ${c?.address ?? "?"}:${c?.port ?? "?"}; message: ${c?.message ?? err?.message}`);
  console.log(atProxy ? "BLOCKED: ECONNREFUSED at the proxy 127.0.0.1:9" : "UNCLEAR: not a refusal at the proxy address");
  process.exit(atProxy ? 0 : 3);
}
