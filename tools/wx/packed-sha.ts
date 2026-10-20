// Node reference for the workerd run-check: the same ingest, same frame, sha256 of the packed grid.
import { createHash } from "node:crypto";
import { ingestFrame } from "./ingest.ts";
const { frame } = await ingestFrame(`https://noaa-mrms-pds.s3.amazonaws.com/${process.argv[2]}`);
console.log(JSON.stringify({ runtime: "node", frame: frame.validTimeUtc, packedBytes: frame.packed.length, sha256: createHash("sha256").update(frame.packed).digest("hex") }));
