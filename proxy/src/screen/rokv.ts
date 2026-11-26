// A READ-ONLY view of the boards' ENRICH_KV (phase 1, registration D2 and P-K1). The screen runs the boards'
// handleEnrich unchanged, and handleEnrich writes its `ac:` and `rt:` caches; through this view those writes are
// DROPPED and COUNTED, never thrown (a throw would turn every resolution into an error). The boards' namespace is
// written only by the boards.
//
// The count is the evidence the wrapper is in the path: test/screen-enrich.test.ts asserts it is above zero while the
// real namespace saw no write at all.

export interface ReadOnlyKv { kv: KVNamespace; dropped(): number }

export function readOnlyKv(kv: KVNamespace): ReadOnlyKv {
  let dropped = 0;
  const view = {
    get: kv.get.bind(kv),
    getWithMetadata: kv.getWithMetadata.bind(kv),
    list: kv.list.bind(kv),
    /*P-K1*/
    put: async (): Promise<void> => { dropped++; },
    delete: async (): Promise<void> => { dropped++; },
  };
  return { kv: view as unknown as KVNamespace, dropped: () => dropped };
}
