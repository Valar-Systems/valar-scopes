# The per-product age limit: the server's side (B's ruling, 2026-11-04)

**Status:** the proposal and its predictions, frozen before any code. The R1 reading behind it is `docs/uk-data.md`
A3.14-A3.15 on `docs/uk-data-r1`. The ruling is A3.17 there.

**The ruling (reviewer chat, pasted by Daniel, 2026-11-04 ~17:50Z), B's part, verbatim:**
> B: option 1, a per-product age limit, with opuk at 720 s and cref at 600 s. Today's schedule; no extra checks.
> - The server tells the device each product's limit. Your side: the /wx/cref handler states 720 for opuk, and states
>   nothing new for cref. blipscope-5c's side, in #445: read it, clamp it to [600, 900], and default to 600 when it's
>   absent or invalid.
> - The channel (a response header, or a field in the crop's header) is agreed between you and blipscope-5c, with the
>   reason. You propose it, predictions first, in your PR.

## Why it is needed

- **On R1's week,** at the device's 600 s limit, an `opuk` layer blanks **5.51 %** of the time, 7.31 times an hour.
- **Even an ingest that served every frame the instant it was listed** would blank 2.19 %, because OPERA lists a frame
  about 252 s after its time, against a 60 s poll.
- **At 720 s,** the same week blanks 0.69 %, 0.15 times an hour.

## The proposal: a response header, `X-Wx-Max-Age`

**`X-Wx-Max-Age: <seconds>`**, decimal ASCII digits, on every 200 and 304 from `/wx/<product>` whose **served** product
states a limit.
- **`opuk` states 720.** `cref` states nothing, so its answers carry no such header, and the device keeps 600.
- **Errors carry none** (503, 400, 404): an error has no crop to age.

**Why a header and not a field in the crop's header:**
1. **WXC1 v1 is frozen and graded on both sides:** the device's `wxcrop::Validate` (which refuses unknown flag bits),
   WS7's and K12's fixtures, and the encoder. A new field means a v2 or a reused byte. Either moves a contract that two
   sessions' tests hold, for one number.
2. **The device already collects headers on this request.** #445's `/wx` fetch (`HttpRequestManager.cpp`, at
   `f001221`) collects `Transfer-Encoding`, `ETag`, `X-Cache` and `X-Upstream`. Reading one more is one entry in that
   list.
3. **A 304 carries it too.** The limit belongs to the product, not to one crop's bytes, so a 304 restates it with no
   body. A field in the crop would ride only on a 200.
4. **Its failure is safe and visible.** If a header is ever lost (there is no intermediary: device to Worker,
   `Cache-Control: no-cache`), the device falls back to 600 s. It then blanks more, never shows an older frame than
   it should.

**The device's side, blipscope-5c's, in #445 (as ruled; stated here only so the two sides meet):**
- read `X-Wx-Max-Age`;
- accept it if it matches `^[0-9]{1,5}$`;
- clamp it to [600, 900];
- use 600 when it is absent or does not match.

## The predictions, frozen before the code

| | check | predicted |
|---|---|---|
| **A1** | `/wx/cref` at LHR (served `opuk`) | 200 with `X-Wx-Max-Age: 720`; the 304 for its ETag also carries `X-Wx-Max-Age: 720` |
| **A2** | `/wx/cref` at KMCI (served `cref`) | 200 and 304 carry **no** `X-Wx-Max-Age` |
| **A3** | `/wx/cref` at CDG and PHNL (`cref`'s all-14 crops) | no `X-Wx-Max-Age` |
| **A4** | `/wx/opuk` at LHR | 200 with `X-Wx-Max-Age: 720` |
| **A5** | 503 `wx_stale` (an `opuk` frame 1,801 s old), 400 `bad_lat`, 404 `not_found` | no `X-Wx-Max-Age` |
| **A6** | **the sabotage:** the header taken from the REQUESTED product (`cref`, which states none) | A1's 200 at LHR carries no header, so A1 is red |
| **A7** | the products' table | `opuk`'s row states 720; `cref`'s states nothing; 720 lies inside the device's [600, 900] |
| — | the whole proxy suite, and host-tests | green |

**Not in this PR:** smoke's UK `/wx` check, which is R4's. It will assert `X-Wx-Max-Age: 720` at a UK point. Today's
smoke asks at Bend, which `cref` serves, and a header that is never sent there cannot be failed by it.

**Outside the set:** any other reading means stop and report.
