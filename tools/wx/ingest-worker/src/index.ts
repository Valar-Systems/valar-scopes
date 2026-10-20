// Local workerd run-check for tools/wx/ingest.ts. GET /?key=<s3 key> -> the packed grid's sha256 + header.
import { ingestFrame } from "../../ingest.ts";
export default {
  async fetch(req: Request): Promise<Response> {
    const key = new URL(req.url).searchParams.get("key");
    if (!key) return new Response("need ?key=", { status: 400 });
    const { frame } = await ingestFrame(`https://noaa-mrms-pds.s3.amazonaws.com/${key}`);
    const digest = new Uint8Array(await crypto.subtle.digest("SHA-256", frame.packed));
    const hex = [...digest].map((b) => b.toString(16).padStart(2, "0")).join("");
    return Response.json({ runtime: "workerd", frame: frame.validTimeUtc, ni: frame.ni, nj: frame.nj, packedBytes: frame.packed.length, sha256: hex });
  },
};
