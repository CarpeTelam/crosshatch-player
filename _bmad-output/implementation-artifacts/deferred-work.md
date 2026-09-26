- source_plan: `_bmad-output/initiative-crosshatch-player-v1/epic-platform-baseline/story-games-build-flag-empty-game-libraries-and-host-suites-plan.md`
  summary: `sim.sh build` does not notice that `simulator.ini` changed since `sim.sh setup`, so a clone set up before a flag change builds the simulator envs from a stale `platformio.local.ini` block.
  evidence: `cmd_build` runs `pio run` directly; `cmd_setup` is the only writer of the managed block. After this story, a stale block lacks `-DFREEINK_CAP_GAMES=1`, so `simulator_x4pro` silently builds without the game libraries until `sim.sh setup` is re-run.

- source_plan: `_bmad-output/initiative-crosshatch-player-v1/epic-platform-baseline/story-upstream-touch-ledger-and-its-ci-job-plan.md`
  summary: Add a one-line AGENTS.md Policy pointer that an upstream-file change needs a row in `docs/crosshatch/upstream-touches.md` and that `scripts/check_upstream_touches.py` checks it locally.
  evidence: Review pass 1 (blind hunter): agents learn about the ledger only when the CI job fails; AGENTS.md is agent-context, so the edit is deferred rather than patched.

- source_plan: `_bmad-output/initiative-crosshatch-player-v1/epic-platform-baseline/story-fork-update-source-plan.md`
  summary: Verify on a device that the games-enabled update check maps a 404 from the fork's releases/latest to "no update", a network failure to "failed", and offers then stops offering a `-ch.N` release.
  evidence: Review pass 1 (verification-gap): `OtaUpdater.cpp` and `ForkReleaseProbe` are excluded from the host suite and the simulator, so the guarded wiring and the probe are covered only by builds, `strings` checks, and a live `curl` showing the 404 today; the fork release story's device check is the natural place.
