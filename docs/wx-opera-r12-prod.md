# R1 and R2 on production, after the proxy deploy that carries #623 (predictions first, 2026-11-11)

**For:** the reviewer chat. **Status:** predictions, frozen before the read. **Read-only:** 49 requests to production
`/wx/cref` as the test identity, and one `/healthz`.

**The condition, from Q-ROUTED (a), ruled 2026-11-08 ~02:33Z:** "After that deploy, you read R1 and R2 on production."
**~05:49Z on 2026-11-11:** "R1 and R2: production has run 68dc169a since 05:18:54Z (block H), so they can run now."

**What production runs:**
- 68dc169a contains #623's squash `dbf8cb74`.
- `git diff dbf8cb74 68dc169a` is empty for `proxy/src/wx/` and for `proxy/test/fixtures/wx/region-points.json`, so
  production's region table and dispatch are #623's exactly.

## The reads (`tools/wx/r12_points.py`, then `tools/wx/r12_prod.sh`)

The points come from `r12_points.py`. It decodes `region.generated.ts` in Python, a second implementation of
`region.ts`'s decoder, and writes the request points to a file it never prints. The 40 label points are quantised with
the device's `round2` (half away from zero), as the vitest's `deviceQuery` does, at `r = 161`.

| row | what | predicted |
|---|---|---|
| **P0** | `/healthz`: the commit, and the `wx` block's `cref` and `opuk` lines | commit **68dc169a**; `cref` and `opuk` both fresh (frame age under their limits, 600 s and 1,800 s) |
| **R1-P** | `/wx/cref` at the 40 label points (`region-points.json`'s `d1`) | the **5 isles points** (the United Kingdom, Ireland, the Isle of Man, Jersey, Guernsey): **200, ETag product `opuk`, `X-Wx-Max-Age` 720**. The **35 others: 200, ETag product `cref`, the all-14 crop (body byte 32 = `0xE0`), no `X-Wx-Max-Age`**. That is R1 as frozen in §7.4, through production |
| **R2-P count** | `opuk`'s lattice points outside the UK land box, recounted by `r12_points.py` | **48,721**, the vitest's figure, every one in one of §7.2's three areas: western Ireland (west of 8.70 W), the Channel Islands (south of 49.80 N, 3.00-2.00 W), France's coast (south of 49.80 N, east of 2.00 W to 1.80 E). **"other" = 0.** The per-area counts are printed, not graded. The table's total points equal its header's (6,921,706) |
| **R2-P sample** | `/wx/cref` at 9 of those points: the first, middle and last in the table's scan order in each area | each **200, ETag product `opuk`, `X-Wx-Max-Age` 720** |
| **Controls** | | **R2-P count must equal the vitest's 48,721** before any sample is believed. **R1-P's 35** are the control that the endpoint doesn't answer `opuk` everywhere |
| **If a product is stale** | P0 shows `cref` or `opuk` past its limit | not a reading: re-read once 10 minutes later. Stale a second time means **BLIND**, which fails the row |
| escape | anything else | stop and report |
