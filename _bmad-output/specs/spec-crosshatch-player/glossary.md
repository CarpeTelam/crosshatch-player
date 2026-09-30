# Glossary

| Term | Meaning |
| --- | --- |
| `.chgame` | One game package: a flat zip with `manifest.json`, `main.lua`, optional `*.lua` modules and `*.png` images. |
| Inbox | `/games/` on the SD card. The launcher installs every `*.chgame` found there when it opens. |
| Seat | A player slot, numbered from 1. Seat 0 means everyone at game over in pass-and-play. |
| Mode | `solo` (one seat), `pass` (all seats on one device), `nearby` (one seat per device over ESP-NOW). |
| Authority | The device that runs `setup` and `apply` and holds the canonical snapshot: the local device in solo and pass, the host in nearby. |
| Host / guest | In Play Nearby, the host is seat 1 and the authority; guests join its lobby. |
| Snapshot | The encoded game `state`, the single source of truth, replicated whole after every accepted move. |
| Move | The value `input` returns; the authority passes it to `apply`, which accepts or rejects it. |
| Hidden game | A manifest with `hidden = true`. In pass-and-play the runtime inserts the hand-off. |
| Hand-off | The runtime-owned blank screen, pushed with a full refresh, shown between seats in a hidden pass game. |
| API level | The integer version of the `ch.*` API and icon set; a frozen level never changes and later levels only add, while a preview level, level 1 until v1 closes, may still change. |
| Upstream-touch ledger | `docs/crosshatch/upstream-touches.md`: the only upstream files v1 may change, enforced in CI. |
