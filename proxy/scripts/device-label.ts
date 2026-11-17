/**
 * device-label.ts -- the LABEL our own request line carries for a device id (docs/workers-logs-invocation-off.md,
 * section 4): the first 8 hex characters of SHA-256 over the normalised id.
 *
 *   printf '%s' "<device id>" | npx tsx scripts/device-label.ts
 *
 * Reads ONE device id from STDIN -- never from an argument, never from the environment -- and prints only its label.
 * The id is never printed. A malformed input is refused (exit 2) with a message that does not quote it, and so is any
 * argument at all. It uses the Worker's own normDeviceId() and deviceLabel() (src/devlabel.ts), so the label it prints
 * is the label the Worker logs. Tested by scripts/device-label.selftest.mts, with EXAMPLE ids only.
 */
import { readFileSync } from "node:fs";
import { deviceLabel, normDeviceId } from "../src/devlabel";

async function main(): Promise<void> {
  if (process.argv.length > 2) {
    console.error("refused: the device id is read from stdin only, never from an argument (nothing printed)");
    process.exit(2);
  }
  let raw = "";
  try {
    raw = readFileSync(0, "utf8");
  } catch {
    raw = "";
  }
  const id = normDeviceId(raw);
  if (id === null) {
    console.error("refused: stdin does not hold exactly one device id (8-32 hex); the input is not shown");
    process.exit(2);
  }
  process.stdout.write(`${await deviceLabel(id)}\n`);
}

void main();
