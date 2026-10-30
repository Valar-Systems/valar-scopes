"""Q56 (docs/weatherscope-stage3.md A12.57): a model of ESP-IDF's TLSF for the Weatherscope PSRAM readings, checked
against every reading on record (python -I tools/weatherscope/board/psram_model.py prints model against measured).

A request of n bytes becomes n + 12 inside the heap (light poisoning's 8 B head and 4 B tail), rounded up to its
second-level size class: for m >= 128, the class width is 2^(floor(log2 m) - 5) (SL_INDEX_COUNT_LOG2 = 5); below 128,
m is aligned to 4. The block's header adds 4 B to the free-size delta. heap_caps_get_allocated_size() reports the
rounded size less the 12 B of poisoning.
"""


def roundclass(m):
    if m < 128:
        return (m + 3) & ~3
    w = 1 << (m.bit_length() - 1 - 5)
    return (m + w - 1) // w * w


def allocated(n):
    return roundclass(n + 12) - 12


def free_cost(n):
    return roundclass(n + 12) + 4


def group(blocks):
    return sum(free_cost(n) for n in blocks)


rows = []
# Q's two blocks (A12.25/A12.35): psram_q read 103,432 on two boots
rows.append(("q3 psram_q", group([57600, 44640]), 103432))
# step 7's 3.5b group (this boot): psram_base read 72,972; blocks 58,356 / 9,972 / 4,596
rows.append(("step 7 psram_base", group([9776, 57600, 4512]), 72972))
for n, want in ((57600, 58356), (9776, 9972), (4512, 4596)):
    rows.append((f"allocated({n})", allocated(n), want))
# the main group: sprite, grid, scenario, in, out, view, cycle, layer, townOrder (171 x 4), records (15 x 104)
q = [115200, 38520, 368, 432, 688, 992, 16, 59488, 684, 1560]
m = [115200, 38520, 368, 472, 688, 992, 16, 59504, 684, 1560]
s7 = [115200, 44640, 368, 472, 688, 992, 20, 59504, 684, 1560]
rows.append(("q3 psram_used", group(q), 221076))
rows.append(("anchor psram_used", group(m), 221020))
rows.append(("step 7 psram_used", group(s7), 227168))
for name, model, measured in rows:
    print(f"{name:22s} model {model:7d}  measured {measured:7d}  diff {measured - model:+d}")
print(f"model deltas: anchor - q3 {group(m) - group(q):+d} (measured {221020 - 221076:+d}); "
      f"step 7 - anchor {group(s7) - group(m):+d} (measured {227168 - 221020:+d})")
