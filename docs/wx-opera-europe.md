# OPERA radar beyond the UK: a plan, predictions first (2026-11-06)

**For:** the reviewer chat, and Daniel for the cost question in §4. **Status:** a plan only. No code, no deploy, and
no Worker, bucket or KV write before review. The maps for these countries are separate and later; they are not in
this plan.

**Read §4 first.** On today's measured figures this projects at **about $4.0 a month**, over the $2 allowance, and
at both ends of the 1.6x band ($2.93 to $5.75). As ruled, over $2 means stop and ask Daniel. The plan says so now, before any code is
written, instead of finding it out on the dev Worker.

## 0. The ruling, verbatim (2026-11-06 ~15:12Z, pasted at 15:13Z)

> For: UK data session, from the reviewer chat, 2026-11-06 ~15:12Z
>
> Daniel's decision (2026-11-06): radar beyond the UK. Every country the OPERA composite covers at 90% or more in A3.23's ranking gets OPERA radar from the server, as the UK does now. That includes the UK, and the four tiny countries listed apart at 100%. Plan it now, as a docs PR with predictions first; no code or deploy before my review. It starts after R3′ completes (the opuk redeploy and its re-reads).
>
> The plan must cover:
> 1. The list, named in full from A3.23, with each country's covered % and in-domain %. It leaves out every country under 90% (Italy, Bulgaria, Belarus, Albania, Ukraine, Kosovo, Greece, North Macedonia, the Faroes) and every partial-domain country.
> 2. How a point picks its product: the served region as a union of those countries' Natural Earth polygons. Say how you test a point against it (on the composite grid or by polygon), what a point just outside gets (today's behaviour), and a check with a sabotage at a border with an excluded country.
> 3. Whether it stays one product (opuk's box grown to the region) or becomes several, with what that does to frame size, R2 writes and storage (expire-1d kept). Also the age limit: the X-Wx-Max-Age channel at its current value.
> 4. Cost, measured before production on the dev Worker (Plan S style): CPU per build for the larger crop against today's opuk median (1,612.5 ms), and the projected monthly total for all overages together. Over $2 a month: stop and ask Daniel.
> 5. Credits: /credits' OPERA line must read right for the whole region (CC BY 4.0, as now). Show it with a check.
> 6. No firmware change: a v19 device anywhere in the region gets the radar through /wx/cref, decided server-side, with no device-side list to keep in step. Say so with a check.
> 7. CI predicted from a finished run that postdates main's last change to the set of jobs. Merges by the rule.
>
> Maps for these countries are separate and later; not in this plan.

R3′ completed at about 16:34Z (docs/wx-opuk-r3, `e3304b95`), and this plan was started after it.

## 0b. The ruling on this plan (2026-11-06 ~17:08Z), verbatim

> For: UK data session, from the reviewer chat, 2026-11-06 ~17:08Z
>
> #570 (radar beyond the UK) is approved to merge at 9fa9d717bce2 once its CI reads 31: 28 success, 3 skipped, 0 failed. Merge by the rule.
>
> The rulings:
> 1. Q-COST (Daniel, 2026-11-06): (b). The cheaper parts come first, each its own change with predictions:
>    - prune by key;
>    - one latest record for all OPERA products;
>    - the resample map from R2.
>    Then the dev-Worker measurement, M1–M6 as frozen. If M6 projects the total over $2 a month: stop and ask Daniel. Nothing goes to production before that.
>    The two R2 changes also touch today's production opuk. Deploy each by the rule, with an R3′-style registration and its rollback target recorded first.
> 2. Q-UKBOX: yes. Today's UK land box stays served beside the polygons, so no UK point loses opuk.
> 3. Q-WORKERS: one Worker per product, #556's pattern: an [env.<id>] per product, each with its own cron, cpu_ms 12,000 and inert stop.
> 4. Q-COAST: decided later, on the code stage's printed figure, as you proposed.
> 5. Record beside §4: my earlier estimate to Daniel (20 cents to $1.50 a month) was mine and wrong. The plan's model is the one that counts.

- **#570 merged** at 17:12:06Z as `c2caa9d43609`, by the rule: main's head H `459d50feb0ca` had finished its CI and
  was re-read in the merge command, and the squash tree equals `merge-tree(H, 9fa9d717bce2)`. Its CI read 31: 28
  success, 3 skipped, 0 failed, as predicted.
- **Stage 1 is now the three cheaper parts, in the ruled order:** 1a prune by key (§11), 1b one `latest` record for
  all OPERA products, 1c the resample map from R2. Each is its own change with predictions. The code stage and the
  dev-Worker measurement follow, and nothing for the new products goes to production before M6.

## 1. The list: 40 countries

**The source** is A3.23 (`docs/opera-coverage-2026-11-05.md`): the survey's output `graded.json`, sha256
`0b0d43ac4363093b840655aaa763a57b78946b856f13bc1c60156b6374c1a6bd`, re-hashed before use. It covers 36 ranked
countries at 90% or more, plus the four tiny ones listed apart at 100%. Names are Natural Earth's `ADMIN` field (so
Serbia is "Republic of Serbia"). "Served by" is §3's products.

| country | covered % (median) | in-domain % | served by |
|---|---|---|---|
| Aland | 100.00 | 100.1 | baltic |
| Andorra | 100.00 | 99.3 | iberia |
| Austria | 100.00 | 100.0 | central, southeast, west |
| Belgium | 100.00 | 100.0 | west |
| Bosnia and Herzegovina | 100.00 | 100.0 | southeast |
| Croatia | 100.00 | 100.0 | southeast |
| Czechia | 100.00 | 100.0 | central |
| Denmark | 100.00 | 100.0 | scandisw, central |
| Estonia | 100.00 | 100.0 | baltic |
| Finland | 100.00 | 100.0 | scandin, baltic |
| France | 100.00 | 86.0 | west, iberia, isles |
| Germany | 100.00 | 100.0 | west, central, scandisw |
| Hungary | 100.00 | 100.0 | central, southeast |
| Ireland | 100.00 | 100.0 | isles |
| Isle of Man | 100.00 | 99.3 | isles |
| Jersey | 100.00 | 98.3 | isles |
| Liechtenstein | 100.00 | 99.8 | west |
| Lithuania | 100.00 | 100.0 | baltic, central |
| Luxembourg | 100.00 | 100.3 | west |
| Malta | 100.00 | 101.9 | malta |
| Moldova | 100.00 | 100.0 | southeast |
| Montenegro | 100.00 | 100.1 | southeast |
| Netherlands | 100.00 | 99.2 | west |
| Poland | 100.00 | 100.0 | central |
| Portugal | 100.00 | 96.3 | iberia |
| Romania | 100.00 | 100.0 | southeast, central |
| Slovakia | 100.00 | 100.0 | central |
| Slovenia | 100.00 | 100.0 | southeast |
| Sweden | 100.00 (lowest frame 99.39) | 100.0 | scandin, scandisw, baltic |
| Switzerland | 100.00 | 100.0 | west |
| Iceland | 99.99 | 100.0 | iceland |
| United Kingdom | 99.44 | 100.0 | isles |
| Norway | 99.29 | 83.7 | scandin, scandisw, iceland (Jan Mayen) |
| Latvia | 97.90 | 100.0 | baltic |
| Spain | 92.58 | 98.5 | iberia |
| Republic of Serbia | 90.99 | 100.0 | southeast |
| *listed apart (under 100 pixels):* San Marino | 100 | 102.8 | southeast |
| *listed apart:* Monaco | 100 | 100.9 | west |
| *listed apart:* Gibraltar | 100 | 108.3 | iberia |
| *listed apart:* Guernsey | 100 | 100.0 | isles |

**Left out, named in full:**
- **Under 90% covered (9):** Italy 70.57, Bulgaria 67.88, Belarus 45.05, Albania 27.53, Ukraine 25.75, Kosovo 17.25,
  the Faroe Islands 0.00, Greece 0.00 and North Macedonia 0.00.
- **Partial-domain, listed apart for being under 50% in-domain (6):** Morocco, Russia, Turkey, Tunisia, Algeria and
  Greenland.
- **Anything not in A3.23's tables,** for example the Vatican, which has no pixel at 1 km.

**Parts of served countries outside the composite are not served,** because the composite has no data there. Each
polygon part is tested against the raster (`eu_region.py`):

| country | parts outside the raster | area |
|---|---|---|
| France | 11 (the overseas departments) | 88,916 km² |
| Norway | 23 (Svalbard and Bouvet Island) | 62,135 km² |
| Spain | 8 (the Canaries) | 7,436 km² |
| Portugal | 14 (the Azores and Madeira) | 3,421 km² |
| Netherlands | 3 (the Caribbean Netherlands) | 295 km² |

No part straddles the raster's edge. Jan Mayen is inside the raster and served. These areas match A3.23's in-domain
shortfalls, taking each pixel as 1 km²: dropped / (dropped + in-domain pixels) is France 13.96%, Norway 16.26%,
Spain 1.47%, Portugal 3.74% and the Netherlands 0.79%. A3.23 has 14.0, 16.3, 1.5, 3.7 and 0.8.

## 2. How a point picks its product

**The served region** is the union of the 40 countries' Natural Earth 1:10m admin-0 polygons (v5.1.2, sha256
`239eec57ac17f100a11e2536cffc56752c318b50ae765b0918ff7aab4ce8f255`, the survey's file), keeping only the parts inside
the composite (§1). **Plus today's UK land box (Q-UKBOX):** keeping it means no UK point that gets `opuk` today
loses it. The box holds no excluded country's land (`eu_verify.py`). The land in it is the UK, Ireland east of
8.7°W, the Isle of Man, and France's coast from 0.44°E to 1.8°E north of 49.8°N, all served.

**Tested by polygon, on the quantised point, precomputed:**
- **The input is the quantised point.** The server already answers from it alone (`quantise()`: lat/lon at 0.01°,
  the device's own 2 dp). So the only points that can ever be asked are the 0.01° lattice.
- **A generator** (`proxy/scripts/gen-wx-region.mjs`, at the code stage) tests every lattice point in the region's
  box against the polygons by the pixel-centre rule, the survey's rule. It writes one row of runs per lattice row,
  each run holding the product that serves it (0 for none), as a generated module. The module's header carries the
  Natural Earth sha256 and the rule list, and `--check` regenerates it byte-identical, like `embed-pages.mjs`.
- **At request time** a lookup replaces `inLandBox()` in `servedProduct()`: row = (lat − first row) / 0.01, then a
  binary search of that row's runs. There is no polygon and no projection in the request path, and the answer equals
  a polygon test on the quantised point by construction. D4 checks that.
- **Why not the composite grid:** a test there would project each request into LAEA and ask a 1 km pixel. The
  lattice is what the request actually is, it is finer, and it needs no projection.

**Which product:** an ordered rule list of rectangles (§3). A served point goes to the first rule whose rectangle
holds it, so every served point has exactly one product. The generator asserts that no served point is left over;
`eu_rules.py` measured a left-over area of 0.

**A point just outside gets today's behaviour, unchanged:** `servedProduct()` returns `cref`, as for any point
outside the UK box now. For a European point, `cref`'s crop box misses `cref`'s grid, so `handleWx` answers
**HTTP 200 with the all-14 (no coverage) crop**, with ETag product `cref`, no `X-Wx-Max-Age` and no tile read
(`handler.ts`, the `boxMissesGrid` branch). The device draws no radar there.

**The checks, frozen now and run at the code stage** (vitest, `proxy/test/wx-region.test.ts`, plus the generator's
own run):

| check | what | predicted | sabotage | predicted under it |
|---|---|---|---|---|
| **D1** served set | `servedProduct("cref")` at each of the 40 countries' Natural Earth label points (`LABEL_X`/`LABEL_Y`), quantised as the device sends them | **40 of 40** on the product `eu_country_products.py` gives (each label point, rounded to 2 dp as the device sends it, is inside its own country and the region and takes the same product: measured, 40 of 40, `eu_d1q.py`) | **D1s:** generate without Malta | **red on 1 (Malta)**, which gets `cref` |
| **D2** excluded controls | the label points of all 15 left-out countries (§1: the 9 under 90% and the 6 partial-domain) | **15 of 15 → `cref`**, 200, all-14, no `X-Wx-Max-Age` | **D2s:** dispatch by product grid instead of by region (K5's `byWindow`) | **red on exactly 8**, the label points inside some product grid (`eu_verify.py`): Italy, Bulgaria, Belarus, Albania, Ukraine, Kosovo, the Faroe Islands and North Macedonia. **The other 7** (Greece, Morocco, Russia, Turkey, Tunisia, Algeria, Greenland) lie outside every grid and **stay `cref`** |
| **D3** borders with an excluded country | at 15 named crossings, the lattice point nearest the border on each side, chosen by an **independent** implementation (shapely, Python) from the same polygons: Nova Gorica/Gorizia (SI/IT), Menton/Ventimiglia (FR/IT), Chiasso/Como (CH/IT), the Brenner (AT/IT), San Marino/Italy (inside and out), Narva/Ivangorod (EE/RU), Storskog (NO/RU), Vaalimaa (FI/RU), Terespol/Brest (PL/BY), Kybartai/Chernyshevskoye (LT/RU), Giurgiu/Ruse (RO/BG), Uzhhorod (SK/UA), Ceuta (ES/MA), Montenegro/Albania at Sukobin, and Serbia/Kosovo at Merdare | **served side → its product, excluded side → `cref`'s all-14, at all 15** | **D3s-a:** generate with Italy added, with the generator's left-over refusal off for the sabotage. **D3s-b:** dispatch by product grid (as D2s) | **D3s-a red on the 5 Italian sides. D3s-b red on all 15** excluded sides, each inside a product grid by the 2.02° margin |
| **D4** exhaustive along those borders | every lattice point within 3 cells of a served-excluded border: the generated table against shapely's point-in-polygon | **0 disagreements**; the count of points is printed | **D4s:** the generated table with Italy added | **red:** disagreements equal the Italian-side points in the set |
| **G1** the generated module | `--check` regenerates byte-identical; the header's sha256 equals the file's; the rule list in the header equals `products.ts`'s | as stated | **G1s:** edit one run | **red** (`--check` differs) |

**Watched, not graded: coastal points (Q-COAST).** A coastal town whose quantised point falls just offshore of
Natural Earth's 1:10m coastline gets `cref`'s all-14, as a European point does today. The code stage prints how
many of Natural Earth's 1:10m populated places in the 40 countries quantise to a point outside the region. A sea
buffer, if any, is for a ruling on that figure. The UK is covered either way by keeping the box (Q-UKBOX).

## 3. One product or several: several (10)

**One product cannot be built.** A single grid over the region, grown by opuk's 2.02° margin, is 6,016 x 4,006 =
**24.1 M cells (10.9x opuk)**, 96 WXG1 tiles and **12.58 MB a frame**. Its build would hold, by `buildMap` and
`readWindow`:
- an `Int32` per cell (`fill`), about 96 MB;
- an `Int32` per window pixel (`cellOf`), plus `value` and `quality` as `Float32`, over a window of nearly the
  whole 3,800 x 4,400 raster.

That is far over the isolate's 128 MB. K11 measured opuk alone at a 54 MB live peak in Node. By §4's model its CPU
would be about 15 s a build (cells 10.9x opuk, a window of at most the whole raster, 7.5x), over the 12 s cap in one
invocation.

**So: 10 products, by an ordered rule list** (first match wins; `eu_rules.py`). Each product's grid is the box of
the served land its rule takes, grown by 2.02° at 0.01°, exactly as `OPUK_GRID` is built. **The cap:** each
product's estimated build memory stays within 1.5x opuk's. The estimate is the `fill`, `cellOf`, value and quality
arrays plus the frame, the shape K11 measured at 54 MB for opuk. **`opuk` keeps its id** and becomes the
British Isles product: its grid grows, and the device sees an ETag change once.

| product | rule (first match) | grid (cols x rows) | cells (x opuk) | tiles | frame | window (x opuk) | memory est. (x opuk) |
|---|---|---|---|---|---|---|---|
| `opuk` (isles) | UK, Ireland, Man, Channel Is., + today's UK box | 1954 x 1585 | 3.10 M (1.41) | 16 | 2.10 MB | 1.43 | 1.43 |
| `opis` (iceland) | Iceland + Jan Mayen | 2065 x 1183 | 2.44 M (1.11) | 15 | 1.97 MB | 0.78 | 0.89 |
| `opib` (iberia) | Spain, Portugal, Andorra, Gibraltar, France's south edge | 1805 x 1268 | 2.29 M (1.04) | 12 | 1.57 MB | 1.16 | 1.13 |
| `opmt` (malta) | Malta | 444 x 433 | 0.19 M (0.09) | 1 | 0.13 MB | 0.09 | 0.09 |
| `opwe` (west) | France, Benelux, Switzerland, Liechtenstein, Monaco, west Germany | 1968 x 1638 | 3.22 M (1.46) | 16 | 2.10 MB | 1.49 | **1.49** |
| `opce` (central) | east Germany, Poland, Czechia, Slovakia, Austria, north Hungary | 1775 x 1215 | 2.16 M (0.98) | 12 | 1.57 MB | 0.92 | 0.95 |
| `opse` (southeast) | Slovenia, Croatia, Bosnia, Serbia, Montenegro, Romania, Moldova, south Hungary, San Marino | 2368 x 1069 | 2.53 M (1.15) | 15 | 1.97 MB | 1.27 | 1.26 |
| `opba` (baltic) | Baltics, Aland, south Finland, east Sweden | 1962 x 1316 | 2.58 M (1.17) | 12 | 1.57 MB | 1.04 | 1.08 |
| `opsw` (scandisw) | Denmark, south Norway, west Sweden, north Germany | 1541 x 1335 | 2.06 M (0.93) | 12 | 1.57 MB | 0.72 | 0.79 |
| `opsn` (scandin) | Norway, Sweden and Finland north of 63° | 2832 x 1222 | 3.46 M (1.57) | 18 | 2.36 MB | 0.97 | 1.15 |
| **total** | | | **24.03 M (10.9)** | **129** | **16.9 MB** | **9.87** | max **1.49** |

- **Considered and rejected:** an automatic bisection under the same cap gave 21 products, several of them slivers.
  The script is `eu_partition.py`, sha256 `c0fa18f6ed6bb5d39c733171215658f0ed31e4ef35ed65c5f59fdf2aa46e9cc1`,
  kept in scratch.
- **Workers (Q-WORKERS):** one Worker per product, as #556 split `opuk`: one toml with an `[env.<id>]` per product,
  each with its own cron, its own `cpu_ms` 12,000 and an `inert` env to stop it.
  - **Why not one Worker:** all 10 products get a new frame at the same moment, from the same composite. One
    invocation building them all would take about 17 s. Spreading them across checks, one build a check, delays the
    last product by about 5 minutes, against a 720 s device limit.
  - The alternative is one scheduler fanning out to a builder over a service binding. It is fewer Workers but a new
    mechanism.
- **Frame size and storage:** 16.9 MB of frames per 5-minute set, against opuk's 1.18 MB.
  - Retention stays an hour (12 frames a product), so about 203 MB is held. That is under R2's 10 GB free storage.
  - **`expire-1d` is kept** as the backstop. As R3′ saw, it also removes a stopped product's `latest` after a day.
- **R2 writes:** each product writes 3 Class A operations per frame (the frame, `latest`, and the prune's list),
  **25,920 a month**. Ten products add **259,200** (§4).
- **The age limit:** every OPERA product takes `opuk`'s row as it stands: `maxAgeS` 1,800, and `deviceMaxAgeS` 720,
  sent as `X-Wx-Max-Age` on the 200 and the 304 alike.
  - **A1 (frozen):** every product with `source: "opera"` has exactly those two values, and a request served by each
    carries `X-Wx-Max-Age: 720` on both the 200 and the 304.
  - **A1s:** one product's row without `deviceMaxAgeS` → **red on that product** (no header).
- **Rollout order (stage 3):** a product's ingest runs and its `latest` is read before the proxy routes to it.
  Otherwise a point that gets `cref`'s 200 today would get `wx_stale` 503 from a product with no frame yet.

## 4. Cost: over $2 on today's figures, so this is Daniel's question first

**The baseline** (`cost_read.py`, read 16:44:45Z on 2026-11-06, over the 30 days before it; its output's sha256
`494263247aa0226b3e9ebce11b5531866fccac162a49dd444589871af44342ce`):

| | run rate a month | free tier |
|---|---|---|
| Workers CPU | 37,673,398 ms (this barely includes `opuk`, which ran for a few hours in that window) | 30 M |
| R2 Class A | 929,006 | 1 M |
| R2 Class B | 4,803,699 | 10 M |

**The projection, today's build code** (one build a product per 5-minute frame, 8,640 a month):
- **CPU per build** is modelled as opuk's median (1,612.5 ms, R3′-6) times the mean of the product's cells ratio and
  window ratio.
  - Summed over the 10 products (unrounded ratios) that is **10.384 x 1,612.5 = 16,744 ms a set**, so 144.7 M ms a
    month.
  - The checks that build nothing add 10 x 86,400 x 7 ms = 6.0 M ms (opuk's "rest" median, R3′-6).
  - **CPU in total is 188.4 M ms, 158.4 M over: $3.17.**
- **R2 Class A:** 929,006 + 259,200 = 1,188,206, 188,206 over: **$0.85.**
- **R2 Class B:** 4,803,699 + 10 x 430,000 (opuk's figure in `cost_read.py`) = 9.10 M. That is under 10 M: **$0**,
  with 0.9 M of headroom. The served crops add up to 4 tile reads per 200 for each European device.
- **Storage:** about 0.2 GB, under 10 GB: **$0.**
- **Total: about $4.02 a month. Across the 1.6x band on CPU, $2.93 to $5.75. Over $2 at both ends.**
- **Recorded beside this, as ruled ~17:08Z (item 5), in the reviewer's words:** "my earlier estimate to Daniel (20
  cents to $1.50 a month) was mine and wrong. The plan's model is the one that counts."

**The cheaper parts, each a measured change of its own, with projected effects:**

| change | what it saves | projected |
|---|---|---|
| prune by key, not by listing (frame keys are epochs; `DeleteObject` is free) | Class A 3 → 2 a frame | Class A **$0.85 → $0.46** |
| one `latest` record for all OPERA products, one write a set | Class A 2 → 1.1 a frame | with the above, **→ $0.11** |
| the resample map from R2, not rebuilt per cold isolate | the map is 0.55-0.59 s against 0.25-0.41 s a frame in Node (K11); the Worker's cold/warm split is **not measured** | if most builds are cold, CPU could drop by about half: total **about $1.8**. Unproven |

**Q-COST, for Daniel:** which of these?
- **(a)** Allow up to about $5 a month for this, and build it as it stands.
- **(b)** Build the cheaper parts first. The dev Worker then measures, and if the total still projects over $2, stop
  and ask again. **(Recommended:** the R2 parts are small and certain, and the map's share is a single
  measurement.)
- **(c)** Narrow the region.

**The measurement on the dev Worker (stage 2, Plan S style), frozen now:**
- **Where:** `blipscope-wx-ingest-dev` writing to `blipscope-wx-dev`, never production.
- **Which products:** the three largest (`opwe`, `opsn`, `opuk`) and the smallest (`opmt`), one at a time, each to
  its first 12 counted single builds. Phases are clock-driven, as in S4.
- **How it is read:** from Workers Logs invocation records, **at ABR 1 only**: a sampled read refuses (R3′'s lesson).
  The ingest line gains a `map` field (`"cache"` or `"built"`) so cold and warm builds are counted apart.

| row | predicted |
|---|---|
| **M1** median, single builds | `opwe` **2,378 ms** in [1,487, 3,805]; `opsn` **2,050 ms** in [1,281, 3,280]; `opuk` **2,284 ms** in [1,427, 3,654] (the model from unrounded ratios, x and ÷ 1.6) |
| **M2** `opmt` | median **≤ 600 ms**. The model's 141 ms ignores a build's fixed costs, so only a bound is predicted |
| **M3** max, any build | **≤ 6,000 ms**, half the 12 s cap |
| **M4** outcomes | **0 `exceededCpu`, 0 `exceededMemory`** |
| **M5** cold and warm | printed, not graded: counts and medians by `map` |
| **M6** the projection | the model refitted to M1 and M2, summed over 10 products, plus §4's R2 lines. **Predicted over $2, so stop and ask Daniel, as ruled**, unless Q-COST is answered first |
| escape | anything outside the rows above: stop and report; nothing goes to production |

## 5. Credits

**The line /credits serves** (`photolicense.ts`, on main) already reads:

> Weather radar outside the US: EUMETNET OPERA CIRRUS maximum-reflectivity composite, created by Meteo France,
> licensed under CC BY 4.0 (https://creativecommons.org/licenses/by/4.0/). Reprojected to a 0.01° grid and binned to
> 5 dBZ steps by Valar Systems. Not endorsed by EUMETNET.

It names no country, so it reads right for the whole region as it stands. **No change is planned.**

| check | what | predicted | sabotage | predicted under it |
|---|---|---|---|---|
| **CR1** | the page `renderCreditsHtml` renders (what /credits serves) holds **exactly one** OPERA line. That line contains "CC BY 4.0" and the link to `creativecommons.org/licenses/by/4.0/`, and **no** country or region name from a fixed list (UK, United Kingdom, Britain, England, Scotland, Wales, Ireland, Europe, and the 40 names in §1). Control: the line is present and non-empty | as stated | **CR1s-a:** "outside the US" → "in the UK". **CR1s-b:** drop "CC BY 4.0" | **red** on each |

## 6. No firmware change

**What a v19 device does** (#445, `feat/wx-device`, at its head `baac7575b963`):
- It asks `GET <base>/api/v1/blipscope/wx/cref?lat=&lon=&r=`. The only call site is
  `CloudFeed::WeatherUrl(req->cloudBase, "cref")`.
- `WxEnabled()` is the layer toggle, the buffers, cloud mode and a URL. **It reads no location.**
- It takes `X-Wx-Max-Age` from the response.

So the product is decided server-side, from the quantised point, and there is no device-side list to keep in step.

| check | what | predicted | sabotage | predicted under it |
|---|---|---|---|---|
| **F1** the firmware side, derived from the firmware's own source (not transcribed) | at #445's head, the set of product arguments at every `WeatherUrl(` call site is exactly `{"cref"}`, and the body of `WxEnabled()` mentions neither `lat` nor `lon` | as stated | **F1s-a:** a copy with a second call passing `"opwe"`. **F1s-b:** a copy with a latitude test in `WxEnabled()` | **red** on each |
| **F2** the server side, as the device asks | `handleWx` at the 40 label points, with lat/lon through the device's own rounding (`wireloc::Round2`, 2 dp) and r in whole km | **40 of 40** answered by the product D1 names, with `X-Wx-Max-Age: 720` | **F2s:** generate without Malta | **red on 1** |

- F1 runs against #445's head as a script, because #445 is not on main. It joins CI when #445 merges.

## 7. CI and the merge

**This PR** is docs plus three Python instruments under `docs/wx-opera-europe/`, with no workflow change.
**Predicted: 31 check runs: 28 success, 3 skipped, 0 failed.**
- **Baseline:** #568's PR run on head `8692e118052f` (docs-only), started 08:07:56Z on 2026-11-06 and finished:
  31 = 28/3/0.
- **The last job-set change** is `84409651` (#552, the `ota-identity` job, 2026-11-05T20:10:52Z), and #568's run
  postdates it. **The last workflow change** is `259e8688` (#558, a step only), and the run postdates that too.
- Re-read just before the push: main's last change to `.github/workflows` or `skus.yml` is still `259e8688`.

**It also carries the queued wording fix** to `docs/uk-data.md` §A1.1d, as the next docs change. "LPS's answer named
all four datasets" becomes "LPS's answer covers the four datasets Daniel's question named".

**Merge by the rule:**
- wait for main's head H to finish its CI;
- re-read `origin/main` in the same command as the merge, and merge only if it is still H;
- afterwards, check the squash tree against `merge-tree(H, PR head)`.

## 8. Stages

1. **This PR:** the plan and its frozen rows. No code.
2. **The code PR,** after review: the 10 product rows, the generator and its generated module, `servedProduct()` by
   the region table, the per-product Worker toml with `inert` envs, the dev envs, and D1-D4, G1, A1, CR1, F2 and F1's
   script. Nothing deploys.
3. **The dev measurement** (§4, M1-M6), after Q-COST and a slot. It stops at M6 if over $2.
4. **Production,** staged as R3′ was: each ingest Worker first, its `latest` read, then the proxy's dispatch; one
   deployer at a time, rollback targets recorded first. Its rows are registered in stage 2's PR.

## 9. Questions

- **Q-COST** (Daniel): §4: (a), (b) or (c). **Recommended: (b).**
- **Q-UKBOX:** keep today's UK land box as served, beside the polygons, so no UK point loses `opuk` (§2).
  **Recommended: yes.**
- **Q-WORKERS:** one Worker per product, or one scheduler fanning out to a builder (§3). **Recommended: one per
  product,** #556's pattern.
- **Q-COAST:** whether to add a sea buffer that never enters an excluded country's land, decided on the code stage's
  printed figure (§2). **No recommendation until that figure exists.**

## 10. Inputs and instruments

All of these are read-only and offline, run in my scratch with Python 3 and numpy, pyproj 3.8.0 and shapely 2.2.0.

| file | sha256 | output |
|---|---|---|
| `docs/wx-opera-europe/eu_region.py` | `2ca3db1b2c200d6fa67a45c9585e84a61454a4d01c016689b86e6df4510992dc` | the 40, the parts outside the raster, the one-product grid; output sha256 `679de5117af33b414c1b9f9d9a158be971729853f1ec9ed0fe9afa76f077376b` |
| `docs/wx-opera-europe/eu_rules.py` | `cd7000540ad3d451697c5b75939a25503e0421075283184564c961b3e27eaa59` | §3's table, left-over area 0, max memory 1.49x; output sha256 `f4727ddef3744c36c0952e6d7bd4dfe526ef5dafa91de89bb2c8742fb8217680` |
| `docs/wx-opera-europe/eu_country_products.py` | `f07992d088d2a928b4cccf818aabec43ce01ba1180a20fc1e872d188ca6ea107` | §1's "served by"; every label point inside, 40 of 40 |
| `docs/wx-opera-europe/eu_d1q.py` | `4c5ab5bdade8803d4ebe82dd164f11a04e62bd44e429930381cd60d14ad7aeb3` | D1's premise, quantised: 40 of 40 inside their own country and the region, each on the same product as unquantised |
| `docs/wx-opera-europe/eu_verify.py` | `661ac7e1a0785b764ae25031fc971b50b52a6c5786b2294fe3b4bcf4ca83587b` | the UK box's land (no excluded country), the 15 left-out label points against the product grids (8 inside), the model's unrounded sum (10.3839) |

- **Commands:** `python -I eu_region.py graded.json ne_10m_admin_0_countries.geojson eu_region.json`, then
  `python -I eu_rules.py graded.json ne_10m_admin_0_countries.geojson eu_rules.json`, then
  `python -I eu_country_products.py graded.json ne_10m_admin_0_countries.geojson eu_rules.json`, and
  `python -I eu_verify.py graded.json ne_10m_admin_0_countries.geojson eu_rules.json` and the same for `eu_d1q.py`.
- `eu_region.py`, `eu_rules.py` and the bisection assert the two input sha256s before reading; the other three read
  `eu_rules.json` and the same files.
- **No coordinate of a device or a person appears here.** The rule rectangles and the border crossings are public
  geography, like `OPUK_LAND` in `products.ts`.

## 11. Stage 1a: prune by key (predictions frozen before any code, 2026-11-06)

**What changes: the OPERA products only.** Today that means `opuk`. `cref` is unchanged: its frame epochs carry
seconds, so it keeps listing.
- **Today:** `pruneOld` lists `wx/opuk/frames/` on every storing check. That is a ListObjects, R2 Class A. It then
  deletes the frames older than the cut, `now - RETAIN_S`, never the one `latest` names.
- **New, `pruneByKey`:** the candidates are every whole-minute epoch `e` with `cut - 54,000 <= e < cut`, which is
  **900 keys**, minus the key `latest` names. One `bucket.delete(keys)` removes them. That is a DeleteObjects, which
  is free. **No ListObjects.**
  - **Why whole minutes, not the 5-minute cadence:** `epochOfKey` fixes an OPERA frame's seconds at 0, so every
    frame epoch is a whole minute. A frame published off the 5-minute grid would escape a 300 s step.
  - **Why 15 hours:** that is 900 keys, under the binding's 1,000 a call, and it cleans up after any stop shorter
    than that. R3′'s stop was 9 h 23 min. Older strays are left to `expire-1d`, which stays.
- **The log:** an OPERA storing check's `wx_ingest` line carries **`pruneKeys`** (the candidates sent: 900, or 899
  when `latest`'s key is inside the window) **instead of `pruned`**. R2's delete does not say which keys existed.
  `cref`'s `pruned` is unchanged.
- **What it saves:** one ListObjects per storing check, 8,640 a month for `opuk`. Over §3's 10 products, Class A
  falls from $0.85 to about $0.46 a month.
- **Before the deploy, a tools change:** `tools/wx/r3_5b.py` and `r3p.py` learn `pruneKeys`, and refuse a Logs read
  at an ABR level other than 1 (R3′'s note). That is read-only tooling on the R3 branch, with its own sabotage: a
  read forced to ABR 10 must refuse.

**The checks (vitest, `proxy/test/wx-opera.test.ts`):**

| check | what | predicted | sabotage | predicted under it |
|---|---|---|---|---|
| **PK1** (K10 re-registered as **K10′**) | a storing check on `opuk`, then a check that stores nothing, counting R2 `list` and `delete` calls | storing: **0 lists, 1 delete**. Not storing: **0 lists, 0 deletes** | **PK1s-a:** prune by listing (`IngestX.pruneBy: "list"`). **PK1s-b:** `pruneAlways`, K10's own sabotage, kept | **a: red,** the storing check lists 1. **b: red,** the non-storing check deletes 1 |
| **PK2** the keys left, compared as whole lists | `pruneByKey` on a bucket seeded with `opuk` frames every 300 s from 18 h ago to now, plus one frame exactly at the cut, plus one at a whole minute off the 5-minute grid inside the window, with `latest` naming a frame inside the window | **left = every frame at or after the cut, the frame `latest` names, and every frame older than `cut - 54,000`** | **PK2s-a:** the bound inclusive at the cut. **PK2s-b:** `latest`'s key not excluded. **PK2s-c:** a 300 s step | **a: red,** the frame at the cut goes. **b: red,** `latest`'s frame goes. **c: red,** the off-grid frame stays |
| **PK3** against today's prune | the same seeded bucket, pruned by `pruneOld` and by `pruneByKey` | **identical**, except the frames older than `cut - 54,000`, which only `pruneOld` removes | (covered by PK2's sabotages) | |
| **PK4** `cref` unchanged (control) | a `cref` storing check | it **still lists (at least 1)**, and its line still carries `pruned` | | |
| **PK5** the count | `pruneByKey`'s return | **900** with `latest` outside the window; **899** with it inside | | |

**The deploy to production `opuk`, R3′-style, registered now.** It runs only after review, merge and a slot.

| row | what | predicted |
|---|---|---|
| **PKD-0** | the rollback target, recorded first | `opuk` is `a4e6c685-295b-4f7b-a839-e1ad12c633a5` (top level, `WX_INGEST` on). Only the `opuk` Worker is deployed, from a clean checkout of the merge commit. The `cref` Worker (`257f49eb`) and the proxy are not deployed: the change does not reach them |
| **PKD-1** | the version record | bindings exactly `WX` -> blipscope-wx and `WX_INGEST` = "on"; `cpu_ms` 12000 |
| **PKD-2** | every storing check in the first 70 min (Logs, ABR 1 only) | `stored` or `backfilled`, **`pruneKeys` 900** and no `pruned` field; **0 error lines** |
| **PKD-3** | at the first firing + 10 min, R3′-3 and R3′-4's reads | `wx.opuk` `frameAgeS` <= 1,800 and `coveredShare` in [0.70, 0.80]. LHR: ETag `opuk`, `X-Wx-Max-Age` 720, frame = `latest` |
| **PKD-4** | R3′-5b′'s rule at +70 min, read twice 10 min apart | **<= 13 frames**; every epoch >= the last storing start - 3,600, except `latest`'s; the +300 s sabotage fails on the oldest |
| **PKD-5** | R2 ListObjects on blipscope-wx per hour, the 2 h before against the 2 h after (R2 analytics, with its sample interval) | **printed, not graded:** the dataset is sampled. Expected to fall by about `opuk`'s storing checks, about 12 an hour |
| **PKD-6** | CPU, R3′-6's method over 2 h, ABR 1 | `opuk` single-build median in **[1,008, 2,580] ms** (R3′-6's 1,612.5, x and ÷ 1.6); max <= 5,000 ms; **0 `exceededCpu`** |
| escape | anything outside these rows | **roll `opuk` back to `a4e6c685` at once**, stop and report |

- **No board hold is proposed.** It is `opuk` only, as for the 15:07Z redeploy; the reviewer's call.
- **CI for this change's PR:** it touches `proxy/`, so `workers.yml` runs. **Predicted: 34 = 31 success, 3 skipped,
  0 failed.**
  - Baseline: #565's PR run on `da52c54f55eb`, which touches `proxy/`, started 2026-11-06T05:06:16Z and finished:
    34 = 31/3/0.
  - It postdates the last job-set change, `84409651` (#552), and the last workflow change, `259e8688` (#558, a
    step).
**The readings before the push (2026-11-06, the code at this PR's second commit):**
- **`tsc --noEmit`:** clean.
- **vitest, the two files changed** (`test/wx-opera.test.ts` and `test/wx-ingest.test.ts`, not the whole suite):
  **71 of 71 passed, exit 0.** It started at 17:34:39Z, after the poller saw no other session's vitest or `sabotage-`
  process and TIME_WAIT under 2,000. Another session's vitest was running when the code was ready, so the run waited
  for it.

| row | got | verdict |
|---|---|---|
| **PK1** | storing: 0 lists, 1 delete, `pruneKeys` 900, no `pruned`. Then `already-stored`: 0 lists, 0 deletes. **PK1s-a** (prune by listing): the storing check lists. **PK1s-b** (`pruneAlways`): the second check deletes 1 | as predicted; both sabotages red |
| **PK2** | seeded 218; **left 38 = expected 38**, as whole lists (13 at or after the cut, `latest`'s, 24 strays). **PK2s-a:** the frame at the cut goes. **PK2s-b:** `latest`'s frame goes. **PK2s-c:** the off-grid frame stays | as predicted; all three sabotages red |
| **PK3** | 14 left by listing and 38 by key; the 24 strays are the whole difference | as predicted |
| **PK4** | `cref` stored with 1 list and `pruned` 0 (a number), no `pruneKeys` | as predicted |
| **PK5** | 899 with `latest` inside the window, 900 with it outside | as predicted |

- **The tools change** is `6ac157d0` on docs/wx-opuk-r3:
  - **Controls:** `r3p.py lines` at ABR [1] read normally, exit 0; `r3_5b.py opuk` at 17:25:31Z read ABR [1] and
    passed, its +300 s sabotage failing on the oldest frame.
  - **Sabotage:** with `R3P_FORCE_ABR=10`, `r3p.py`'s three modes each refuse with exit 2, and `r3_5b.py` reads
    BLIND.
  - **Unforced, the API sampled a read itself.** `r3_5b.py` at 17:22:54Z, over a window of about 21 minutes, came
    back at **ABR [100]**, found no pruning check, and read BLIND. That is the case the guard exists for: the
    sampling is real and it varies from call to call.

- **Ahead, for stage 1b (one `latest` record):** §4's "one write a set" assumed a single writer for each 5-minute
  set. Under Q-WORKERS, ten Workers each finish their own product at their own time, so no one Worker holds a whole
  set. Stage 1b's predictions will name how the shared record is written, and what one product's failure does to
  the others.

### 11.1 The stage 1a deploy to production `opuk` (the record)

**The ruling, 2026-11-06 ~18:22Z, verbatim:**

> For: UK data session, from the reviewer chat, 2026-11-06 ~18:22Z
>
> #570's merge recorded: by the rule, tree checked.
>
> #573 (stage 1a, prune by key) -> approved to merge at 3594321d3bcc. Predictions frozen first; PK1-PK5 and all five sabotages as predicted; CI 34 = 31/3/0 from a baseline after the last job-set change. My read of pruneByKey: exactly 900 whole-minute keys strictly before the cut, never latest's frame, under the 1,000-key delete limit. Merge by the rule (Q-M1).
>
> Deploy slot granted after the merge and its main CI: PKD-0 to PKD-6 as registered in section 11, rollback target opuk a4e6c685 recorded first, only the opuk Worker. No board hold: the 18:45Z bench window replays rain over Bend, which never reads opuk. Announce start and end.
> - One amendment before it runs: a Logs read that comes back sampled (ABR above 1) may be re-read once over a narrower window. A second sampled read is BLIND, BLIND fails the row, and the frozen rollback applies.
>
> Stage 1b -> (b) is the design (each product's latest in KV, no shared record, products stay independent), but not now. Next as you planned: 1c's predictions on its own branch, then measure with M6. Build 1b only if M6 still reads over $2/month.
>
> The -c user.email slip: recorded; the rule stands.

**#573 merged** at 18:24:54Z as `36d0b657ece7`, by the rule.
- Main's head H was `551fd0303675` (#572), with its CI finished: 34 = 32 success, 2 skipped.
- The merge command re-read `origin/main`, found it still H, and merged with `--match-head-commit 3594321d3bcc`.
- The squash tree equals `merge-tree(H, 3594321d3bcc)` (`b2df01358385`).
- **Main CI on `36d0b657`, predicted 34 = 32 success, 2 skipped, 0 failed.** The baseline is #572's finished main
  run on `551fd030`, a `proxy/` main push after the last job-set change (`84409651`).

**PKD-0, recorded before the deploy:**
- **What `opuk` runs now:** `wrangler deployments status` gives **`a4e6c685-295b-4f7b-a839-e1ad12c633a5` at 100%**,
  created 15:11:29Z.
- **The rollback:** `wrangler rollback a4e6c685-295b-4f7b-a839-e1ad12c633a5 -c wx-ingest-opuk.wrangler.toml`,
  straight back to that version. The fallback is a redeploy from the clean checkout of `9db83163` (scratchpad
  `wt-plan-s`).
- **The deploy:** `npx wrangler deploy -c wx-ingest-opuk.wrangler.toml`, at the top level, from a clean detached
  checkout of `36d0b657` (`wt-pkd`, with `npm ci` from its own lockfile). **Only the `opuk` Worker.** The `cref`
  Worker (`257f49eb`) and the proxy are not touched.

**The read plan, frozen before the start, with the ~18:22Z amendment applied.** T is the deploy's end.
- **Every Logs read is cut into 900 s pieces** (`R3P_CHUNK_S=900`, tools at `55e81d76` on docs/wx-opuk-r3):
  PKD-2's 70 minutes and PKD-6's 2 hours. Every piece must read at ABR 1, and the tool refuses otherwise.
  - A refused read is re-read **once** in 450 s pieces.
  - A second sampled read is **BLIND, BLIND fails the row, and the rollback applies.**
- **PKD-4** uses `r3_5b.py`'s default 1,200 s lookback; a sampled read is re-read once at 600 s (`8e2776d5`). A
  second sampled read is BLIND.
- **When each row is read:**
  - PKD-2: T to T+70 min, read at T+72 min.
  - PKD-3: the first firing + 10 min.
  - PKD-4: T+70 and T+80 min.
  - PKD-6: T to T+2 h, read at T+2 h+5 min.
  - PKD-5: [T-2 h, T) against [T, T+2 h), read at T+2 h+10 min (`r2_ops.py`, `3a737da6`), printed only.
- **The "before" figures, not graded:**
  - **CPU** (`r3p.py cpu`, 16:25-18:25Z in 900 s pieces): ABR [1], 120 records all `ok`; single builds n 24,
    median 1,971.5 ms, **max 4,227 ms**; the rest median 10 ms. That max is well above R3′-6's 2,545 ms, on the old
    code, so the spread is wider than R3′-6 showed. PKD-6's max bound of 5,000 ms stands as registered.
  - **R2, 16:00-18:00Z:** ListObjects 142 and 139 an hour (a list carries no object name, so `cref` and `opuk` are
    not split); DeleteObjects 46 and 56; PutObject `wx/cref` 56 and 55, `wx/opuk` 25 and 25.
- **No board hold,** as ruled. Start and end are announced through the reviewer chat.
