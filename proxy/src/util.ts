// Build a JSON response with an explicit Content-Length and no chunked encoding.
// The ESP32 firmware's streaming JSON reader wants a known length (its chunked
// fallback costs heap), and fixed-length bodies keep HTTP/1.1 keep-alive simple.
export function jsonResponse(
  body: string | object,
  status = 200,
  extra: Record<string, string> = {},
): Response {
  const text = typeof body === "string" ? body : JSON.stringify(body);
  const bytes = new TextEncoder().encode(text);
  return new Response(bytes, {
    status,
    headers: {
      "Content-Type": "application/json",
      "Content-Length": String(bytes.byteLength),
      // Devices must not receive transformed (compressed -> chunked) bodies, and
      // must not HTTP-cache: freshness is signalled in-band via the `t` field.
      "Cache-Control": "no-store, no-transform",
      ...extra,
    },
  });
}

export function errorResponse(
  status: number,
  error: string,
  extra: Record<string, string> = {},
): Response {
  return jsonResponse({ v: 1, error }, status, extra);
}

export function clientIp(request: Request): string {
  return request.headers.get("CF-Connecting-IP") ?? "0.0.0.0";
}

export function intEnv(value: string | undefined, fallback: number): number {
  const n = value === undefined ? NaN : Number(value);
  return Number.isFinite(n) ? n : fallback;
}
