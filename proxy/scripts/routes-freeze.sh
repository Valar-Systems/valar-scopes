#!/usr/bin/env bash
# routes-freeze.sh -- W4: the routes-refresh job's first step. Reads ROUTES_FREEZE_UNTIL (a repository variable, set by
# whoever schedules a recording; docs/route-weekly-code.md, "Who sets the freeze, and how") and exits 0 run, 4 skip,
# 1 fail. The rule is G.freeze in routes-guards.ts; routes-freeze.ts applies it at the job's actual start.
set -u
cd "$(dirname "${BASH_SOURCE[0]}")/.."
exec npx --no-install tsx scripts/routes-freeze.ts # NPM1: never a download inside the token job
