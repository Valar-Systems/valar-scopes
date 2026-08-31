# Git hooks

**Not active until you opt in**, once per clone:

```sh
git config core.hooksPath .githooks
```

`core.hooksPath` is local config, so cloning the repo does not install these and
CI does not run them. That is deliberate — a hook that a fresh clone silently
inherits is a hook nobody knows they are subject to — but it does mean **the
hooks are a convenience for the person who enables them, not a guarantee about
what is in the tree.** Anything that must hold for every commit belongs in CI.

## `pre-commit` — refuse a real device id

Blocks a commit that stages a bare 16-hex-digit token outside the allowlist.

**Why it exists.** Twice on 2026-08-31, hours apart, by someone who had just
written the rule down:

- an example annotation in `proxy/src/revocation.ts` carried a real device id
  next to the words *"RMA 2026-08-04, board resold"*. During an unrelated
  incident that read as a **record**, and cost an hour of genuine alarm about a
  fleet unit in a stranger's hands. It was example text all along.
- a `git add -A proxy/` then swept an operator scratch file into a public repo.

A CLAUDE.md entry does not survive the twentieth minute of a good debugging run.
`git add -A` stages what you did not look at, and that is the actual mechanism.

**A device id is not a credential** — the key is `HMAC(DEVICE_KEY_SECRET, id)`
and the secret does the work, which is why a secret rotation was the remediation
for the August exposure and a revocation was not. The cost of an id in the tree
is that it becomes a record somebody acts on later, plus a thin privacy surface
once the ids belong to customers rather than to a bench.

**Allowlist:** `0000000000000000`, `0123456789abcdef`, and `beefbeefbeefbeef` —
the synthetic bench identity, which is a real enrolled id but is synthetic *by
design* and exists precisely so that real ids never have to appear.

**Escape hatch:** `git commit --no-verify`, when a real id genuinely belongs in
the commit. Deliberate, and visible in the shell history.

**Proven in both directions on install, which is the only way a guard is worth
having** — a hook that never refuses looks exactly like a hook that works:

```
$ printf '# note about board <a real id>\n' > t.txt && git add t.txt && git commit
COMMIT REFUSED: a real-looking device id is staged.
  t.txt
      <the id, named>

$ printf '0123456789abcdef\n' > t.txt && git add t.txt && git commit
[chore/... 6650f06] hook selftest: allowlisted id passes
```
