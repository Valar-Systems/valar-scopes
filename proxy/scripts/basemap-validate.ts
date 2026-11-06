// One status from the proxy's own validate() for a package file: what the device's reader would say of it
// (docs/basemap-deploy.md, Q5 as ruled). Used by basemap-verdict.sh, so the smoke refuses exactly what the device would.
// Prints the status alone on one line. Exit 0 kOk, 1 refused, 2 unreadable.
//
//   npx tsx scripts/basemap-validate.ts <file>
import { readFileSync } from "node:fs";
import { validate } from "../src/basemap/format";

const f = process.argv[2];
let bytes: Uint8Array;
try { bytes = new Uint8Array(readFileSync(f ?? "")); } catch { console.log("UNREADABLE"); process.exit(2); }
const status = validate(bytes).status;
console.log(status);
process.exit(status === "kOk" ? 0 : 1);
