/**
 * derive-device-key.ts -- mint a per-device API key at manufacture.
 *
 *   DEVICE_KEY_SECRET=... npm run derive-device-key <deviceId>
 *
 * The key is HMAC-SHA256(DEVICE_KEY_SECRET, deviceId) as hex -- exactly what the
 * Worker recomputes to validate (src/deviceauth.ts). The operator holds the
 * secret locally (never in the repo); the device stores the printed key like it
 * stores the shared key today, and sends it as X-Blip-Key alongside its
 * X-Blip-Device id. Because minting needs the secret, a device-authed request is
 * trustworthy enough to back the leaderboard "verified" tier -- unlike the shared
 * build key, which is extractable from the open-source firmware.
 *
 * deviceId is the device's leaderboard/identity hash (DeviceIdentity::Leaderboard-
 * Id() on the firmware): 8-32 lowercase hex chars.
 */
import { createHmac } from "node:crypto";

function main(): void {
  const deviceId = (process.argv[2] ?? "").trim().toLowerCase();
  const secret = process.env.DEVICE_KEY_SECRET ?? "";
  if (!secret) {
    console.error("set DEVICE_KEY_SECRET in the environment (the Worker's secret)");
    process.exit(1);
  }
  if (!/^[0-9a-f]{8,32}$/.test(deviceId)) {
    console.error("usage: DEVICE_KEY_SECRET=... npm run derive-device-key <deviceId (8-32 hex)>");
    process.exit(1);
  }
  const key = createHmac("sha256", secret).update(deviceId).digest("hex");
  console.log(key);
}

main();
