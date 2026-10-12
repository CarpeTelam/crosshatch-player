---
title: 'Link interface, frame codec, and FakeLink'
type: 'feature'
ticket: '2'
created: '2026-10-12'
baseline_revision: '32362b3d8c108117c94c123e2cd6a8bacf8c9910'
status: built
route: 'full'
route_source: 'auto'
review: 'thorough'
review_source: 'pinned'
lenses_ran: ['blind-hunter', 'edge-case-hunter', 'verification-gap', 'intent-alignment']
review_loop_iteration: 0
followup_review_recommended: false
context:
  - '{project-root}/AGENTS.md'
  - '{project-root}/docs/crosshatch/orchestrated-epics.md'
warnings: ['oversized']
deferred:
  - summary: >-
      Nothing ties Protocol::PROTO to GameScript::Codec::VERSION, so a codec bump can ship without a PROTO bump.
    evidence: |-
      PROTO is enforced only in Protocol.cpp's decode; the codec bytes ride in STATE snapshots. lib/GameCore cannot include lib/GameScript (check_layers.py), so only a suite that sees both can pin the pair (owner Decision C7: entry 13, which may touch test/**).
    location: >-
      lib/GameCore/Protocol.h (PROTO), lib/GameScript/Codec.h:15
    severity: medium
  - summary: >-
      expectLinkContract takes two links, so it cannot check that two peers never share a handle, that a handle is never reused after the link forgets a peer, or that receive never returns NO_PEER across many peers.
    evidence: |-
      Only FakeLinkTest checks these for FakeLink. UdpLink (entry 16) and EspNowLink (entry 11, on the device) are held to them only if the contract gains a three-link variant.
    location: >-
      test/game_core/LinkContract.h
    severity: low
  - summary: >-
      FakeLink cannot make send() return false for a full send queue, the one failure the device produces that the double cannot.
    evidence: |-
      ILink documents false for a full queue and ReliableLink must treat it as an ordinary loss; FakeLink's header lists "no send queue that fills". Entry 4 is the first consumer and adds a refuse-N knob with its test.
    location: >-
      test/game_core/FakeLink.h
    severity: low
---

<intent-contract>

## Intent

**Problem:** The GameCore lane (`ReliableLink`, lobby, `NearbySession`) and the radio lane (`EspNowLink`, later `UdpLink`) need one agreed contract before either starts: the link interface, the peer handle, and every byte of AD-13's frame. Nothing of it exists yet, and a radio fact that leaks into `GameCore` stops the epic (R7).

**Approach:** Add `GameCore::ILink` (with its peer handle) and `GameCore::Protocol` (the only encoder and decoder of the frame and its seven payloads) as new `lib/GameCore` files, and a seeded, header-only `FakeLink` plus a reusable link-contract check in `test/game_core/`. This plan's Design Notes fix the contract verbatim for the owner's checkpoint; implementation follows the owner's answers.

## Boundaries & Constraints

**Always:** `lib/GameCore` includes only std, `lib/Memory`, and `lib/JsonParser` (`scripts/check_layers.py`): `ILink.h` and `Protocol.h` add no edge. No radio fact in either: no MAC, channel, RSSI, peer-table size, or ESP-NOW constant (the 1,470 B is `Protocol::MAX_FRAME_BYTES`, a protocol constant; entry 5 checks it against the radio). Everything is little-endian, written and read a byte at a time (no unaligned cast; RISC-V faults). No static initializer, no mutable static (AD-2); `Protocol` never allocates and never logs. `FakeLink` stays gtest-free and header-only so the harness suites (which already have `test/game_core` on their include path) reach it. Test doubles follow the Build-agent brief's rule: `FakeLink` names the device behaviour it stands in for in a comment and here, and `LinkContract.h` pins the agreement. Code adapted from crossplay carries `// Adapted from crossplay 0a1d17f host-tests/link/FakeLink.h (MIT).` plus its copyright line.

**Never:** `MOVE` or `REJECT` on the wire (R4). No `ReliableLink`, no `seq` or `ack` logic (entry 4 owns their meaning; `Protocol` only carries the two fields). No lobby logic, no `NearbySession`, no `EspNowLink`, no `src/**`, `lib/GameScript/**`, `games/**`, upstream file, or `freeink-sdk` change. No edit to `Session.h` (its `MOVE_BYTES` and `REJECT` comment are entry 7's) or to the spine, `docs/`, or CMake files outside `test/game_core/` (the AD-13 amendments this plan implies are listed under Auto Run Result for the orchestrator).

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Round trip, every type | a `Frame` of each of the 7 types at its limits (gameId 32 B, label 16 B, snapshot 1 B and 1,400 B, seq/ack/session extremes) | `encode` returns the table's size; `decode` returns `Ok` and an equal `Frame`; re-encoding the decoded frame gives identical bytes | none |
| Short or long frame | 0 B, 11 B, a header-only `STATE`, every proper prefix of each golden frame; 1,471 B | `TooShort` / `BadPayload` / `TooLong` | refused, `out` unspecified |
| Wrong magic, `proto`, or `session` | either magic byte changed; `proto` 0, 2, 255; frame session differs from the filter; session 0 on the wire | `BadMagic` / `BadProto` / `BadSession` | refused |
| Over-long field | `gameId` 33 B, `hostLabel` 17 B, snapshot 1,401 B (encode and a crafted decode); snapshot empty | `encode` returns 0; `decode` returns `BadPayload` | refused; the limit itself (32, 16, 1,400) accepted |
| Malformed value | unknown type (0, 8, 255); `ABORT` reason 0 or 6; `STATE` `over` 2, `over` 1 with a turn, not over with turn 0 or winners; `ACCEPT` seat 0 or above `n`; `ADVERT` taken above max; trailing bytes on a fixed payload | `BadType` / `BadPayload` | refused |
| Peer handle | default handle; handle from `receive`; handle from another link | invalid sends return false; a received handle sends to its peer; a foreign handle returns false and is counted | none |
| Broadcast | three endpoints, one broadcasts | the other two each receive one copy, the sender none; `carried()` is 2 | none |
| Seeded faults | loss, duplicates, delay, reorder from seed S, run twice; seed T | identical drops, delays, duplicates, order for S twice; T differs | none |
| Packet storm | a looping echo past a carried budget | `overBudget()` true, so a test asserting it fails | none |

</intent-contract>

## Code Map

- `lib/GameCore/ILink.h` -- new: `PeerHandle`, `NO_PEER`, `ILink` (Design Notes 1 and 2, verbatim). Pattern: `IClock.h`, `IRandom.h` (ports are one small header with a virtual destructor).
- `lib/GameCore/Protocol.h`, `lib/GameCore/Protocol.cpp` -- new: constants, `Type`, `AbortReason`, `DecodeStatus`, `Header`, the payload structs, `Frame`, `encode`, `decode` (Design Notes 3 and 4). Includes `Session.h` for `SNAPSHOT_BYTES` (1,400), `IGameRules.h`'s `Status`, and `Roster.h`'s `MAX_SEATS` (16).
- `test/game_core/FakeLink.h` -- new, header-only: `ManualClock`, `Faults`, `FakeLinkNet` (the shared medium), `FakeLink` (an endpoint). Adapted from crossplay `host-tests/link/FakeLink.h` (`Medium`: seeded xorshift, per-copy loss and jitter rolls, broadcast expanded per receiver, a `carried` counter, a blackhole for one-way range).
- `test/game_core/LinkContract.h` -- new, header-only: `expectLinkContract(...)`, the checks every `ILink` must pass; run over `FakeLink` here, over `UdpLink` in entry 16.
- `test/game_core/ProtocolTest.cpp`, `test/game_core/FakeLinkTest.cpp` -- new suites.
- `test/game_core/CMakeLists.txt` -- add the two tests and `lib/GameCore/Protocol.cpp` to `GameCoreTest` (inside this entry's `touches`).
- Read, not changed: `lib/GameCore/Session.h` (`SNAPSHOT_BYTES`), `IGameRules.h` (`Status`: `turn` 0 when over, `winners` bit `seat - 1`), `Roster.h` (`MAX_SEATS` 16), `IClock.h`, `test/game_script/CMakeLists.txt:127` (the harness already has `test/game_core` on its include path), `docs/crosshatch/formats.md` (`Codec::VERSION` is 1, `lib/GameScript/Codec.h:15`).
- Not touched, and why: `lib/GameCore` has no existing function rewritten, so no `git log -L` guard check applies; every file is new except `test/game_core/CMakeLists.txt` (one source list).

## Tasks & Acceptance

**Execution:**
- [ ] `lib/GameCore/ILink.h` -- add `PeerHandle`, `NO_PEER`, `ILink` exactly as Design Notes 1 and 2 write them -- the contract the two lanes share.
- [ ] `lib/GameCore/Protocol.h`, `Protocol.cpp` -- add the constants, enums, structs, `encode`, and `decode` of Design Notes 3 and 4, checks in the order written there; byte-at-a-time little-endian helpers; no allocation, no log, no static initializer -- AD-13's codec, R1.
- [ ] `test/game_core/FakeLink.h` -- add `ManualClock`, `Faults`, `FakeLinkNet`, `FakeLink` per Design Notes 6 -- the seeded hostile link every later suite shares.
- [ ] `test/game_core/LinkContract.h` -- add `expectLinkContract` (contract rules of Design Notes 1 and 2) -- the test that pins the double against the interface the device adapters also implement.
- [ ] `test/game_core/ProtocolTest.cpp` -- golden byte vectors for every type written by hand (an oracle independent of `encode`), round trips at the limits, every refusal of the matrix (including every proper prefix of each golden frame and every one-byte-longer frame), the 1,418 B largest frame against 1,470 B, the canonical property (a seeded loop of single-byte mutations of valid frames: `decode` never reads outside the span, and any frame it accepts re-encodes to identical bytes), `encode` into a too-small buffer writes nothing.
- [ ] `test/game_core/FakeLinkTest.cpp` -- the matrix rows for handles, broadcast, each fault alone (loss 100 %, duplicate 100 %, delay by fake time, reorder), seed repeat and difference over a recorded trace, the carried budget, capacity-too-small drop, over-limit send, `end()` twice, one-way blackhole, and `expectLinkContract` over a `FakeLink` pair.
- [ ] `test/game_core/CMakeLists.txt` -- add `ProtocolTest.cpp`, `FakeLinkTest.cpp`, `${REPO_ROOT}/lib/GameCore/Protocol.cpp`.
- [ ] Mutants (recorded in Implementation Notes, then reverted): drop the `proto` check, the snapshot limit, the `session` filter, one golden offset, `FakeLink`'s seed use, and `FakeLink`'s broadcast self-exclusion; each makes a named test red.

**Acceptance Criteria:**
- Given any frame of the seven types within its limits, when it is encoded and decoded, then the `Frame` is equal and re-encoding it gives the same bytes.
- Given a frame that is short, longer than 1,470 B, has a wrong magic or `proto`, carries a session other than the filter's (or 0), an unknown type, an over-long field, or any malformed value, when it is decoded, then `decode` returns the matching non-`Ok` status; and each limit (`gameId` 32 B, `hostLabel` 16 B, snapshot 1,400 B, frame 1,470 B) is accepted at the limit.
- Given a `STATE` with a 1,400 B snapshot, when it is encoded, then it is 1,418 B and `static_assert`s hold it within `Protocol::MAX_FRAME_BYTES` (1,470).
- Given two `FakeLinkNet`s with one seed and the same traffic, when each runs, then their delivery traces are identical; given another seed, then they differ.
- Given a broadcast from one of three endpoints, when it is sent, then each other endpoint receives its own copy and `carried()` counts both.
- Given `scripts/check_layers.py`, when it runs, then it passes with no new edge.
- Given a games-on default and `x4pro` build, when the flash budget runs, then growth over the base Measurement (+258,048 B flash, +784 B static RAM) is within 9,504 B and 160 B (expected 0 B: nothing references the new code; unmeasured until the firmware checks run).

## Implementation Notes

- Implemented as Design Notes 1 to 6 write them. `ILink.h` puts `PeerHandle` (section 2) before `ILink`, since `ILink` uses it; the comments are the plan's. `Protocol.h` carries the byte tables in comments (Auto Run Result's first bullet).
- `encode` validates every field before it writes anything, with the same rules as `decode` (so an encoded frame always decodes and a refused one leaves `out` untouched); `decode` fills a local `Frame` and assigns `out` only on `Ok`.
- `FakeLink`: handle value is the net's process-unique tag (bits 22..31, from `nextNetId()`), `endpoint + 1` (bits 16..21), and a per-link counter (bits 0..15), after the review (first draft: `(endpoint + 1) << 16 | counter`; see the Review Triage Log), so a net holds at most 63 endpoints and a link 65,535 handles (no suite comes near either); a reordered copy is held back by `reorderExtraMs` (a later frame then overtakes it); rolls per submitted frame are one duplicate roll, then loss, jitter, and reorder per copy. The loss, jitter, delay, and reorder knobs never change the number of rolls; the duplicate roll, a blackholed source (no rolls at all), and ended receivers do. `end()` discards frames waiting for that endpoint, not frames it already sent.
- `LinkContract.h` takes two links and a `pump`; the foreign-handle check uses each link's handle for the other, used on the other (no third link needed).
- A STATE prefix with at least one snapshot byte is a valid shorter STATE, and one byte appended to a STATE is a longer one: `ProtocolTest` checks every proper prefix of every golden except STATE's, where it checks prefixes up to the empty snapshot as refused and the rest as shorter STATEs.
- ASan/UBSan: `ProtocolTest.cpp` and `FakeLinkTest.cpp` with `Protocol.cpp`, built with `-fsanitize=address,undefined` outside CMake, pass all 49 tests.
- Mutants (each applied alone, host suite run, then reverted; the files were restored byte for byte):
  - drop the `proto` check: `ProtocolTest.WrongMagicProtoOrSessionIsRefused`, `ChecksRunInTheOrderTheHeaderWrites`, and both mutation suites red.
  - drop the snapshot limit in `decode`: `OverLongFieldsAreRefusedByEncodeAndDecode`, `TooLongFramesAreRefusedBeforeAnythingElse`, and the seeded mutation suite red; in `encode`: `OverLongFieldsAreRefusedByEncodeAndDecode` red.
  - drop the `session` filter: `WrongMagicProtoOrSessionIsRefused` red.
  - move one golden offset (STATE `winners` written at payload offset 3): `GoldenFramesDecodeToTheExpectedFrame`, `EveryTypeRoundTripsAtItsLimits`, `EncodeIntoATooSmallBufferWritesNothing`, and the single-byte mutation suite red.
  - `FakeLink` ignores its seed: `ASeedRepeatsExactlyAndAnotherSeedDiffers` and `SeedZeroIsMappedToAWorkingSeed` red.
  - `FakeLink` broadcast reaches its sender: `ABroadcastReachesTheOthersAndNotTheSender`, `AReceivedHandleSendsBackToItsPeer`, `EndTwiceIsSafeAndStopsAllTraffic`, and `FakeLinkAgreesWithTheLinkContract` red.
- Host evidence: `ctest` 2,119 of 2,119 pass (49 new: `ProtocolTest` 21, `FakeLinkTest` 28); `check_layers.py` passes (608 include edges, no new edge) and `check_layers_test.py` passes (53); `check_upstream_touches.py` passes; `./bin/clang-format-fix` twice, the second changed nothing.
- Not run (after the review): `pio run -e x4pro`, `pio run -e default`, `pio check` (default and `-e x4pro`), `check_flash_budget.py`; flash and static RAM are unmeasured.

## Plan Change Log

- 2026-10-12 (resume after the owner's checkpoint): recorded Q1 (strict) and Q2 (`ACK` type 7) as decided and C3 to C8 as approved in Design Notes 5, dropped the "If overruled" column and the OWNER marks, and updated Auto Run Result's pending-decision text; no task changed (none carried an overruled path). `baseline_revision` moved from `3737543c` (the drafting commit) to `32362b3d` (this run's start): the 5 commits between are other lanes' merged work and docs, which would pollute the review diff; nothing of this story existed before.

- 2026-10-12 (review): the patched handle rule (values unique across every link instance in the process) supersedes Design Notes 2's examples "EspNowLink: its table slot; UdpLink: the port" for entries 5 and 16, which tag their values with an instance id. No task changed. KEEP: the contract and every golden vector as built.

## Review Triage Log

### 2026-10-12 — Review pass
- verdicts: 27 findings — high 0, medium 3, low 16, false 8, maybe-false 0
- findings (lens order as reported: verification-gap, intent-alignment, edge-case-hunter, blind-hunter; all four ran as context-free subagents):
  - `[medium]` `[defer]` Nothing ties `Protocol::PROTO` to `GameScript::Codec::VERSION` (verification-gap) — real, but owner Decision C7 (epic Notes, entry 2's plan checkpoint) sends the tie test to entry 13 because `lib/GameCore` cannot include `lib/GameScript`; the fix needs files outside this entry's `touches`. Deferred (see `deferred`).
  - `[low]` `[patch]` A blackholed source draws no rolls, so the "same number of rolls" claim fails for that knob (verification-gap) — grouped with the two other roll-count findings below; fix applied: comment reworded, knob test extended.
  - `[false]` `[reject]` `ILink` is exercised only through `FakeLink` (intent-alignment) — no production adapter exists until entries 5 and 16; `LinkContract.h` is the reusable check they run (plan Design Notes 6); no bad outcome here.
  - `[false]` `[reject]` `check_layers.py` and the firmware checks are not shown by the diff (intent-alignment) — they run in Verification: `check_layers.py` passed on the patched tree; the firmware checks are recorded under Verification below.
  - `[false]` `[reject]` Two readings of the 1,470 B figure (intent-alignment) — the plan's Design Notes 3 and the entry's verify settle it: the whole datagram, header included, as `Protocol::MAX_FRAME_BYTES`; a full `STATE` is 1,418 B under a `static_assert`.
  - `[false]` `[reject]` The packet-storm check has no real consumer yet (intent-alignment) — the entry asks for a counter that makes a storm fail a test; `APacketStormTripsTheCarriedBudget` trips it and entries 4 and 8 are its consumers.
  - `[false]` `[reject]` Seed repeatability is shown on one thread only (intent-alignment) — the header and plan say so (a seed repeats exactly when one thread drives the net); entries 4, 6, 7 drive it that way.
  - `[false]` `[reject]` An empty snapshot is refused though the intent does not say so (intent-alignment) — the STATE table fixes S = 1..1,400 (Design Notes 3), approved by the owner.
  - `[false]` `[reject]` Fidelity to crossplay's `FakeLink.h` is stated, not shown (intent-alignment) — the adaptation comment names the file and commit; the entry asks for the same capabilities, which the tests show.
  - `[low]` `[patch]` `jitterMs + 1` wraps to 0 at `UINT32_MAX`, a modulo by zero (edge-case-hunter) — fix applied: the modulus is computed in 64 bits; `AJitterOfTheLargestValueDoesNotDivideByZero` added.
  - `[medium]` `[patch]` A handle carries only the endpoint index, so a handle from another `FakeLinkNet` can send to a different peer instead of returning false and being counted (edge-case-hunter) — grouped with the blind-hunter finding on cross-instance handles: a real adapter numbering peers by slot or port would collide the same way, so `ILink`'s "another instance's handle names no peer" rule is only satisfiable with values unique across instances. Fix applied: each net gets a process-unique tag folded into the handle, one sentence in `ILink.h` states the adapter duty, `AHandleFromAnotherNetIsForeignEvenWithTheSameEndpointAndCounter` added. Bears on entries 5 and 16 (Auto Run Result).
  - `[low]` `[patch]` The roll-count claim in the `FakeLink.h` header is overstated (edge-case-hunter) — same root cause as the verification-gap blackhole row; header reworded, and `LossDoesNotReshuffle...`, `JitterDoesNotReshuffle...`, `ReorderDoesNotReshuffle...`, `DelayShiftsEveryDelivery...` tests replace the loss-only test.
  - `[low]` `[patch]` `encode` into a buffer overlapping a span the `Frame` holds corrupts silently (edge-case-hunter) — fix applied: `Protocol.h` states `out` must not overlap any span or view in `frame`.
  - `[medium]` `[patch]` Cross-instance handle rule satisfiable only by `FakeLink`'s endpoint tag (blind-hunter) — same entry as the edge-case handle row; patched together.
  - `[false]` `[reject]` `ILink` says "never truncated" while `FakeLink` lists "no 250 B v1 truncation" (blind-hunter) — the truncation is ESP-NOW v1 behaviour, outside the supported radio (epic Notes: ESP-NOW v2 required, both devices one build); the `FakeLink` line only lists a device difference. Entry 5 owns the radio fact.
  - `[low]` `[patch]` Roll-count claim overstated (blind-hunter) — same root cause as above; patched together.
  - `[low]` `[patch]` `~FakeLink` does not call `end()`, so a destroyed link keeps receiving queued copies and inflates `carried()` (blind-hunter) — fix applied: the destructor calls `end()`; every suite declares its net before its links.
  - `[low]` `[defer]` `expectLinkContract` takes two links, so it cannot check that two peers never share a handle or a handle is never reused (blind-hunter) — caused by this change but the remedy (a three-link variant) belongs with the first non-fake adapter run, entry 16; `FakeLinkTest` checks both rules for `FakeLink` today. Deferred (see `deferred`).
  - `[low]` `[defer]` `FakeLink` cannot make `send` fail for a full queue (blind-hunter) — documented as more permissive than the device; the knob adds public test surface the plan scoped out, and entry 4 (`ReliableLink`, the first consumer of a false `send`) adds it with its test. Deferred (see `deferred`).
  - `[low]` `[patch]` The heap-overrun comment needs ASan, which the host build does not enable (blind-hunter) — fix applied: the comment says the overread shows when the suite runs under ASan/UBSan, run by hand (done here, 49 of 49 before the patches).
  - `[low]` `[reject]` Duplicated `pattern()` and xorshift helpers across test files (blind-hunter) — cosmetic; the copies differ on purpose (seed multipliers, types) and a shared header is more than a direct correction.
  - `[low]` `[reject]` `PeerHandle::fromValue` is public (blind-hunter) — the plan's section 2 (owner-approved) writes it so; adapters and tests are its callers and a friend/key type would add surface.
  - `[low]` `[reject]` Comments cite entries and the AD that a reader of `lib/GameCore` cannot resolve; wire format record (blind-hunter) — spine AD-13 (amended, `32362b3d`) names `Protocol.h` the home of the layouts, so the header is the canonical copy; `docs/` is outside `touches`.
  - `[low]` `[patch]` The MIT permission notice does not travel with `FakeLink.h` (blind-hunter) — fix applied: the full notice from crossplay's `LICENSE` follows the copyright line.
  - `[low]` `[reject]` `Join::hostApi` holds the guest's level (blind-hunter) — AD-13 names the field `hostApi` for both, and the plan (section 3) records it; the comment says what it holds.
  - `[low]` `[patch]` `EXPECT_GE(drain(b), 1u)` is a weak bound (blind-hunter) — fix applied: `EXPECT_EQ(..., 1u)` with a comment naming the one frame.
  - `[low]` `[reject]` `Faults` percentages are not validated (blind-hunter) — a test double; an out-of-range value changes only the test's own faults.

## Design Notes

Everything in 1 to 5 below is **decided**: the owner approved Design Notes 1 to 5 as recommended (Q1 strict refusal of another `proto`, Q2 `ACK` as type 7, C3 to C8 as recommended) in the epic Notes, "Decision (owner, 2026-10-12, entry 2's plan checkpoint, the link contract)", recorded in commit `32362b3d` together with the amendments to R1, R3, entry 6's verify, and spine AD-13. Section 6 holds the test side. The implementation builds these answers; no overruled path remains.

### 1. `GameCore::ILink` (verbatim, `lib/GameCore/ILink.h`)

```cpp
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace GameCore {

// (PeerHandle: see section 2.)

// Port (spine AD-1): a datagram link to the other devices in range. It moves opaque frames and names a peer
// only by a PeerHandle. It knows nothing of ESP-NOW, a MAC address, a channel, or a socket; adapters
// (EspNowLink, UdpLink in the simulator, FakeLink in tests) hold every such fact.
//
// Frames: one call carries one whole frame, 1..Protocol::MAX_FRAME_BYTES bytes. A frame is delivered whole
// or not at all (never truncated, split, or merged with another). Nothing else is promised: frames may be
// lost, duplicated, delayed, or reordered, which is what ReliableLink exists for.
//
// Ownership: the caller owns every buffer it passes, and the link never keeps a pointer to one past the call.
// send() and broadcast() copy the frame before returning, so the caller may reuse its buffer at once.
// receive() copies the frame into the caller's buffer, which should be Protocol::MAX_FRAME_BYTES long.
//
// Threads: an ILink is not thread-safe. One task calls it at a time (the GameLink task while it runs; the
// thread that stopped the task afterwards, for end()). An adapter synchronizes whatever its own receive
// callback or socket thread shares with receive(). No call blocks on the air: each hands off and returns.
class ILink {
 public:
  virtual ~ILink() = default;

  // Hands one frame for `to`. True: the link took the frame for transmission (says nothing about arrival).
  // False, nothing sent: `to` is invalid or names no peer this link has received from; the frame is empty
  // or longer than Protocol::MAX_FRAME_BYTES; the link has ended; or it cannot take a frame now (its send
  // queue is full). A false is an ordinary loss: the caller does not retry in a loop, ReliableLink resends.
  virtual bool send(PeerHandle to, std::span<const uint8_t> frame) = 0;

  // Hands one frame for every other device in range that listens. The sender never receives its own
  // broadcast. Same true/false rules as send() minus the peer. Only ADVERT is broadcast (AD-13, by
  // convention of the lobby; the link does not check).
  virtual bool broadcast(std::span<const uint8_t> frame) = 0;

  // Takes the oldest waiting frame. True: `from` is the sender's handle (never invalid), the frame is at the
  // start of `buffer`, and `length` is its size. False: nothing waiting; `from` and `length` are untouched.
  // A waiting frame longer than `buffer.size()` is dropped by the link (never truncated) and receive()
  // goes on to the next one, so false means the queue is empty, not that a frame was dropped. A frame
  // whose sender the link has no room to name is dropped the same way.
  virtual bool receive(PeerHandle& from, std::span<uint8_t> buffer, size_t& length) = 0;

  // Brings the link down. Idempotent. Afterwards send() and broadcast() return false, receive() returns
  // false (waiting frames are discarded), and every earlier handle is dead. There is no begin(): an adapter
  // is brought up by its own factory, which is where a radio fact (a failed bring-up, the Wi-Fi state)
  // belongs, and is handed up already running.
  virtual void end() = 0;
};

}  // namespace GameCore
```

Not on `ILink`, on purpose: the host label (the radio layer hands the label's bytes to the lobby separately, R3, R8), this device's own address or handle (nothing in the protocol needs it), peer-gone events (silence is `ReliableLink`'s, R2), and counters (`NearbySession` writes the `LINK` lines).

### 2. The peer handle (verbatim, same header)

```cpp
// Names one peer of one link instance. Opaque: session code stores it, compares it, and passes it back to
// the link that gave it, and does nothing else with it. It is never a MAC address, a port, or an index it may
// interpret; value() is for adapters, tests, and log lines, and means nothing on another link or device.
class PeerHandle {
 public:
  constexpr PeerHandle() = default;  // invalid
  // For ILink implementations only: session code never makes a valid handle, it receives one.
  static constexpr PeerHandle fromValue(const uint32_t value) { return PeerHandle(value); }

  constexpr bool valid() const { return id != 0; }
  constexpr uint32_t value() const { return id; }
  friend constexpr bool operator==(PeerHandle, PeerHandle) = default;  // equality only: no ordering, no hash

 private:
  explicit constexpr PeerHandle(const uint32_t value) : id(value) {}
  uint32_t id = 0;
};

inline constexpr PeerHandle NO_PEER{};
```

Rules:
- **Created** only by a link, only when it receives a frame from a peer it has not named yet (and, for tests, `FakeLink::handleOf`, which stands for "has heard"). There is no `connect(address)`: a session reaches a peer it has not heard from only by broadcasting, so the first handle of a guest comes from the host's `ADVERT` and the host's from the guest's `JOIN`.
- **Stable and unique** for the life of one link instance, from first receipt to `end()`: the same peer always gives an equal handle, two peers never share one, and a handle is never reused for a different peer, even if the link forgets the first. Valid values are nonzero and chosen by the adapter (EspNowLink: its table slot; UdpLink: the port; FakeLink: endpoint and counter).
- **Invalid** is the default-constructed handle, `NO_PEER`. `send` to it returns false. `receive` never returns it. A valid handle from another link instance, or a dead one after `end()`, names no peer: `send` returns false. (`FakeLink` makes that checkable by tagging handles with their endpoint and counting a foreign use.)
- **Compared** with `==` and `!=` only. A table keyed by handle searches linearly (v1 has at most a few peers).
- **Capacity** is the adapter's: when its table is full it drops a frame from a new peer (counted by the adapter), and the contract says only that `receive` never returns an unnamed sender.

### 3. Frame header and payloads (verbatim)

All integers little-endian. A frame is `header (12 B) + payload`; there is no length field, the payload is the rest of the datagram (the link preserves boundaries).

**Header** (`HEADER_BYTES` = 12):

| Offset | Size | Field | Value |
| --- | --- | --- | --- |
| 0 | 1 | magic | `0x58` (`X`) |
| 1 | 1 | magic | `0x48` (`H`) |
| 2 | 1 | `proto` | `PROTO` = 1 |
| 3 | 1 | `type` | type code (section 4) |
| 4 | 4 | `session` u32 | host's id, never 0 on the wire |
| 8 | 2 | `seq` u16 | `ReliableLink`'s; `Protocol` carries it |
| 10 | 2 | `ack` u16 | `ReliableLink`'s; `Protocol` carries it |

`seq` and `ack` are plain fields to `Protocol`: it neither checks nor interprets them. For frames that consume no `seq` (`ADVERT`, `PING`, `ACK`, `ABORT`) `ReliableLink` writes `seq` 0 and ignores it on receipt; `ack` rides on every frame (0 on `ADVERT`). The radio lane never reads either field.

**`ADVERT`** (type 1; broadcast; payload 18 + L + M B, L = `gameId` length 0..32, M = label length 0..16):

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 1 | `gameIdLen` L (0..32) |
| 1 | L | `gameId` bytes (the manifest id; `Protocol` checks the length only) |
| 1 + L | 8 | `pkgHash` |
| 9 + L | 1 | `manifestApi` u8 (the game's declared `api`) |
| 10 + L | 1 | `hostApi` u8 (the host's `API_LEVEL`) |
| 11 + L | 4 | `surface` u32 (`API_SURFACE_CRC`) |
| 15 + L | 1 | `seatsMax` (1..16) |
| 16 + L | 1 | `seatsTaken` (0..`seatsMax`) |
| 17 + L | 1 | `labelLen` M (0..16) |
| 18 + L | M | `hostLabel` bytes (opaque; the radio layer's, shown by the lobby) |

Payload 18..66 B, frame 30..78 B. The frame is exactly this long: a trailing byte is refused.

**`JOIN`** (type 2; unicast guest to host; header `session` is the host's, copied from its `ADVERT`; payload 13 B, frame 25 B):

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 8 | `pkgHash` |
| 8 | 1 | `hostApi` u8 (the guest's own `API_LEVEL`; AD-13 calls the field `hostApi` for both) |
| 9 | 4 | `surface` u32 |

**`ACCEPT`** (type 3; unicast host to each guest at Start; payload 2 B, frame 14 B):

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 1 | `seat` (1..`n`; the guest's) |
| 1 | 1 | `n` (seat count, `seat` <= `n` <= 16) |

**`STATE`** (type 4; reliable, latest-wins; payload 6 + S B, S = snapshot length 1..1,400, frame 19..1,418 B):

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 2 | `ver` u16 |
| 2 | 1 | `turn` u8 (a seat 1..16 while playing; 0 when over) |
| 3 | 1 | `over` u8 (0 or 1) |
| 4 | 2 | `winners` u16 (bit `seat - 1`; 0 unless over) |
| 6 | S | `snapshot` (the encoded state, `SNAPSHOT_BYTES` at most) |

The snapshot is the rest of the frame. `over` 1 needs `turn` 0; `over` 0 needs `turn` 1..16 and `winners` 0 (`IGameRules.h`'s `Status`, which `LuaGame::readStatus` already enforces on a script's result).

**`ABORT`** (type 5; unsequenced; payload 1 B, frame 13 B):

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 1 | `reason`: 1 `left`, 2 `script_error`, 3 `timeout`, 4 `full`, 5 `version_mismatch` (0 and above 5 refused) |

**`PING`** (type 6) and **`ACK`** (type 7): empty payload, frame 12 B, unsequenced; any payload byte is refused. They differ only in meaning, which `ReliableLink` owns: `ACK` answers a reliable frame (its `ack` field is the point, and entry 4 lets it double as the keepalive), `PING` is the idle keepalive.

**Limits and the largest frame.** `MAX_FRAME_BYTES` = 1,470 (the whole datagram, header included, which is what ESP-NOW v2's `ESP_NOW_MAX_DATA_LEN_V2` limits; entry 5 holds a `static_assert` of it). The largest payload any type reaches is `STATE`'s 1,406 B (`MAX_PAYLOAD_BYTES`), so the largest frame is 1,418 B (`MAX_STATE_FRAME_BYTES`), 52 B under the ceiling. The largest other frame is `ADVERT`'s 78 B. A snapshot of 1,401 B, a `gameId` of 33 B, and a label of 17 B are refused; 1,400, 32, and 16 are accepted.

**Worked examples** (the test's golden vectors are written like these, by hand): `ACCEPT` seat 2 of n = 2, session `0x11223344`, `seq` `0x0102`, `ack` `0x0304`:
`58 48 01 03 44 33 22 11 02 01 04 03 02 02` (14 B).
`ADVERT` for `gameId` "tic", `pkgHash` `a0..a7`, `manifestApi` 1, `hostApi` 1, `surface` `0xEE049B84`, 2 seats with 1 taken, label "X4 3F2A", session `0x11223344`, `seq` 0, `ack` 0:
`58 48 01 01 44 33 22 11 00 00 00 00 03 74 69 63 a0 a1 a2 a3 a4 a5 a6 a7 01 01 84 9b 04 ee 02 01 07 58 34 20 33 46 32 41` (40 B = 12 + 18 + 3 + 7).

**API (verbatim, `lib/GameCore/Protocol.h`, namespace `GameCore::Protocol`):**

```cpp
inline constexpr uint8_t PROTO = 1;
inline constexpr size_t HEADER_BYTES = 12;
inline constexpr size_t MAX_FRAME_BYTES = 1470;
inline constexpr size_t MAX_GAME_ID_BYTES = 32;
inline constexpr size_t MAX_HOST_LABEL_BYTES = 16;
inline constexpr size_t PKG_HASH_BYTES = 8;
inline constexpr size_t MAX_SNAPSHOT_BYTES = SNAPSHOT_BYTES;                      // 1400 (Session.h)
inline constexpr size_t MAX_PAYLOAD_BYTES = 6 + MAX_SNAPSHOT_BYTES;               // 1406, STATE's
inline constexpr size_t MAX_STATE_FRAME_BYTES = HEADER_BYTES + MAX_PAYLOAD_BYTES; // 1418
static_assert(MAX_STATE_FRAME_BYTES <= MAX_FRAME_BYTES);
inline constexpr uint32_t ANY_SESSION = 0;  // decode's filter: accept any session

enum class Type : uint8_t { Advert = 1, Join = 2, Accept = 3, State = 4, Abort = 5, Ping = 6, Ack = 7 };
enum class AbortReason : uint8_t { Left = 1, ScriptError = 2, Timeout = 3, Full = 4, VersionMismatch = 5 };
enum class DecodeStatus : uint8_t { Ok, TooShort, TooLong, BadMagic, BadProto, BadSession, BadType, BadPayload };

struct Header { Type type = Type::Ping; uint32_t session = 0; uint16_t seq = 0; uint16_t ack = 0; };
struct Advert { std::string_view gameId; std::array<uint8_t, PKG_HASH_BYTES> pkgHash{}; uint8_t manifestApi = 0;
                uint8_t hostApi = 0; uint32_t surface = 0; uint8_t seatsMax = 0; uint8_t seatsTaken = 0;
                std::span<const uint8_t> hostLabel; };
struct Join { std::array<uint8_t, PKG_HASH_BYTES> pkgHash{}; uint8_t hostApi = 0; uint32_t surface = 0; };
struct Accept { uint8_t seat = 0; uint8_t n = 0; };
struct State { uint16_t ver = 0; Status status; std::span<const uint8_t> snapshot; };
// Only the member for header.type is read by encode or set by decode (Abort: abortReason; Ping, Ack: none).
struct Frame { Header header; Advert advert; Join join; Accept accept; State state; AbortReason abortReason = AbortReason::Left; };

// Writes `frame` to `out`; returns its size, or 0 and writes nothing when it does not fit `out`, a field is
// over its limit or out of range, or header.session is 0. Writes the header's proto as PROTO.
size_t encode(std::span<uint8_t> out, const Frame& frame);
// Reads `in` as one frame. Checks in this order, returning the first failure: length >= 12 (TooShort), <= 1470
// (TooLong), magic, proto (BadProto), session (BadSession: session 0 on the wire, or `session` != ANY_SESSION
// and != the frame's), type (BadType), then the payload's exact length, field limits, and value ranges
// (BadPayload). On Ok `out` is set; on any failure it is unspecified. Spans and the gameId view in `out`
// alias `in` and live as long as it does. Never reads outside `in`.
DecodeStatus decode(std::span<const uint8_t> in, uint32_t session, Frame& out);
```

Buffers: the caller owns `out` and `in`; `Protocol` copies nothing and keeps nothing. A `Frame` is about 120 B (under the 256 B local rule); the frame buffers (1,470 B) belong on the heap or in a task's owned storage.

### 4. `proto` and type codes

- **`proto` = 1.** It is bumped for any change to the header, a payload, or a type code, and for any bump of `GameScript::Codec::VERSION` (now 1; AD-13: `proto` "includes the codec version"). `lib/GameCore` cannot include `lib/GameScript` (layer table), so nothing in this entry ties the two numbers; the rule lives in a comment on `PROTO` and a tie test is deferred (Auto Run Result).
- **Type codes** (compact and 1-based; 0 is never a type): `ADVERT` 1, `JOIN` 2, `ACCEPT` 3, `STATE` 4, `ABORT` 5, `PING` 6, `ACK` 7. AD-13 fixed no numbers; the codes `MOVE` (4) and `REJECT` (6) would have taken are not reserved, since nothing has shipped. An unknown type is refused (`BadType`).
- **`ABORT` reasons:** `left` 1, `script_error` 2, `timeout` 3, `full` 4, `version_mismatch` 5.
- **`session`:** 0 is reserved for "no session" and is never on the wire; the host's `IRandom` draw is redrawn until nonzero (entry 6). A guest discovering hosts decodes with `ANY_SESSION`; once it has joined, with the host's.

### 5. Choices among readings, with a recommendation

| # | Choice | The readings | Decided (owner, 2026-10-12: approved as recommended; epic Notes Decision on entry 2's plan checkpoint) |
| --- | --- | --- | --- |
| **Q1** (decided: A, strict) | A host on another `proto`. R1 says `Protocol` refuses a wrong `proto`; R3 and entry 6's verify said (before the amendment) a guest lists a different `proto` as "Different version" and a host answers an incompatible `JOIN` with `ABORT(version_mismatch)`. A refused frame cannot be listed or answered, so the two cannot both hold. | **A (strict):** every type refuses a wrong `proto`; a host on another `proto` is invisible to the guest (and a `JOIN` or `ABORT` across `proto`s is silently dropped); "Different version" then comes from package hash, `hostApi`/`manifestApi`, or `surface` only, and entry 6's verify and R3 drop the word `proto`. **B (frozen discovery):** `ADVERT` and `ABORT` keep their bytes forever across `proto` values (later protos only append, so their decoders must allow trailing bytes), `decode` accepts them on a foreign `proto` and reports it, and the lobby grays the row. | **A.** It is what R1 says and the smallest codec; one `proto` exists and nothing can test a promise about a future layout; both devices run one build (epic Notes), so a `proto` mismatch means two different firmware builds, which the package hash and `surface` already show for the same game. Cost: such a host does not appear at all. |
| **Q2** (decided: yes, type 7) | `ACK` is a frame type. AD-13's payload table omits it and R1's list of types leaves it out, but AD-13's text, R2, and entry 4 all name `ACK` (an unsequenced frame; an `ACK` doubling as the keepalive). | **Yes:** type 7, empty payload, beside `PING` (6). **No:** only `PING`, whose `ack` field then does both jobs. | **Yes**, since R2 and entry 4 are written around an `ACK` that is not the idle keepalive (a reply that cannot be mistaken for a probe), and the cost is one enum value. |
| C3 | `ILink::end()` is on the interface; there is no `begin()`. | Teardown by `end()` (idempotent, testable, matches "radio off after the lobby", R7, R8) versus teardown by the owner's destructor. | `end()`. Entry 8's "teardown brings the link down" and entry 8's "leaves no link" become one call a test observes. |
| C4 | Caller-owned buffers with a copy in `send` and `receive`, against a link-owned receive buffer returned by pointer. | Copy in (one 1,470 B copy per frame) versus zero-copy (a pointer valid until the next call). | Copy in. Ownership is obvious across tasks and for the PSRAM ring buffer; a 1,470 B copy is far below the radio's cost. |
| C5 | `Protocol` checks value ranges that make a frame impossible, not only its structure (`STATE` status consistency, seats within 1..16 and `seat` <= `n`, `seatsTaken` <= `seatsMax`, `ABORT` reason set). | Structure only, leaving the rest to `Session` and the lobby. | Check them, so a decoded `Frame` never holds a status or seat no sender can produce and every accepted frame has exactly one encoding (a property the tests assert). |
| C6 | `gameId` and `hostLabel` may be empty (0..32, 0..16), `hostLabel` is opaque bytes. | A new minimum of 1 for `gameId`. | Keep AD-13's limits as written (R1: "length limits otherwise unchanged"); an empty id never matches a manifest's. |
| C7 | `PROTO` is not tied to `Codec::VERSION` by a test here (layer rule). | A test in `test/game_script` (which sees both). | Defer to entry 13 (it may touch `test/**`) and say so in the `PROTO` comment. |
| C8 | `FakeLink` carries crossplay's MIT notice (name the source file as the brief says, plus the copyright line of the repository it comes from). | Name the file only. | Both; one extra comment line. |

### 6. The test side

**`FakeLink`** (`test/game_core/FakeLink.h`, namespace `GameCore::LinkTest`, shape not final): `ManualClock` is an atomic-backed `IClock` with `advance` and `set` (entries 4 to 8 need one clock for the link and the code under test, and four copies of one exist already). `Faults{lossPercent, duplicatePercent, reorderPercent, delayMs, jitterMs, reorderExtraMs}` is set for the whole net or for one direction; `blackhole(source)` drops everything a source sends, as crossplay's one-way range does. `FakeLinkNet(const IClock&, seed)` owns the queue and one xorshift32 state (seed 0 mapped to a fixed nonzero), under one mutex since entry 8's suites call it from two tasks (a seed repeats exactly when one thread drives it, as entries 4, 6, 7 do). Rule for repeatability: a fixed number of rolls per copy, in the same order whatever the percentages (so changing one knob does not reshuffle the others). A frame's copies are: for a unicast, 1 (or 2 with the duplicate roll); for a broadcast, those per other endpoint in endpoint order, each with its own loss, delay, and reorder rolls. Delivery order is `(due time, queue order)`, so a reorder is a later frame given an earlier due time. Counters: `carried()` (copies queued, duplicates and per-receiver broadcast copies each counted, as crossplay's), `dropped()`, `refusedSends()`, `foreignHandleSends()`, `droppedForCapacity()`; `setCarriedBudget(n)`/`overBudget()` make a packet storm a failing assertion. `FakeLink` (an endpoint) implements `ILink`: `handleOf(peer)` for tests, handles tagged with their owning endpoint, `send` over `Protocol::MAX_FRAME_BYTES` or empty returns false, `receive` with a short buffer drops (never truncates).

**What `FakeLink` stands in for, and where it is more permissive than the device:** it stands in for `EspNowLink` and `UdpLink` under the `ILink` contract in section 1. It is more permissive in that it has no send queue that fills, no peer-table limit, no 250 B v1 truncation, no radio time (delay is virtual), no channel or range, and unicast works to any endpoint of the net. It is stricter in tagging handles by endpoint. `LinkContract.h` (`expectLinkContract` taking two connected links and a callback that makes sent frames receivable) pins the contract rules every implementation shares: an invalid or foreign handle returns false; a received handle sends back to its sender; boundaries are kept; a short buffer drops, never truncates; an empty or over-limit frame is refused; a broadcast reaches the others and not the sender; `end()` twice is safe and stops all traffic. It runs over `FakeLink` here and over `UdpLink` in entry 16; `EspNowLink` is outside the host harness, so its agreement is checked on the device (entry 11).

**Crossplay** (read at `0a1d17f`): `FakeLink.h` is adapted (the seeded medium, per-copy rolls, broadcast per receiver, the carried counter, the blackhole). `LinkProtocol.h/.cpp` is a reading reference only: its byte-at-a-time little-endian helpers and its refusal of a length that disagrees with the header are the model, its 192 B cap, 4-byte magic, and `Say` types are not adopted (epic Notes, "Not adopted").

**Evidence the choices rest on:** header, payload table, and the `ver`/`session`/`ABORT` rules: spine AD-13 (and its 2026-10-12 amendment); `MOVE` and `REJECT` left the set: epic R4 and the 2026-10-12 Decision; `ACK` unsequenced: AD-13 text, R2, entry 4; opaque handle and no MAC: R1 and the 2026-10-10 Decision; 1,470 B and `static_assert` in `EspNowLink`: entry 2's verify and entry 5; `winners` mask and 16-seat bound: `Roster.h`, `IGameRules.h`; `SNAPSHOT_BYTES` 1,400: `Session.h`; hand-listed GameCore sources and the include path: `test/game_core/CMakeLists.txt`, `test/game_script/CMakeLists.txt:127`.

## Verification

**Commands** (host suites and fast checks during implementation; firmware once, after the review and its patches, before the commit):
- `flock /tmp/crosshatch-hosttest.lock sh -c 'cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test && ctest --test-dir build/test --output-on-failure -j'` -- expected: all pass; baseline 2,059 tests at `c28120a7`, plus the new `ProtocolTest` and `FakeLinkTest` cases (counts recorded after the run).
- `python3 scripts/check_layers.py`, `python3 scripts/check_layers_test.py` -- expected: pass, no new edge.
- `python3 scripts/check_upstream_touches.py` -- expected: pass (every file is new or fork-only).
- `./bin/clang-format-fix` twice, then `git status` -- expected: the second run changes nothing; a formatting-only change outside this entry's paths is named under Auto Run Result.
- Mutants -- expected: each of the six listed in Tasks makes a named test red; recorded in Implementation Notes.
- `export PLATFORMIO_BUILD_CACHE_DIR=/home/user/crosshatch-player/.cache`, then under `flock /tmp/crosshatch-build.lock sh -c '...'`: `pio run -e x4pro`, `pio run -e default`, `pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high` and the same with `-e x4pro`, and `python3 scripts/check_flash_budget.py` `build on`, `build off`, `compare`, `objects` -- expected: all succeed; growth over the base Measurement (+258,048 B flash, +784 B static RAM, x4pro games on minus off) within 9,504 B and 160 B, subtracting two measurements taken the same way at one commit; expected about 0 B (new code is unreferenced), recorded with the method; `objects` reports no static initializer in `Protocol.cpp`.
- No simulator screenshots (no screen changes) and no fresh-clone gate run (no CI gate or workflow changes).

**Results** (all commands above ran green on the patched tree; figures, mutants, and the size method are under Auto Run Result's Verification): host 2,124 of 2,124; `check_layers.py`, `check_upstream_touches.py`, and the format run clean; firmware builds, `pio check` (default and x4pro), `compare`, and `objects` pass; this entry's own size cost is 0 B flash and 0 B static RAM.

**Manual checks:**
- The Design Notes' tables match `Protocol.cpp`'s offsets byte for byte (the golden vectors are written from the tables, not from `encode`).
- `ILink.h` and `Protocol.h` contain no radio word (MAC, channel, RSSI, ESP, peer table).

## Auto Run Result

**Status `built`** (full route; resumed after the owner's plan checkpoint, whose answers are in Design Notes 5). Baseline `32362b3d8c108117c94c123e2cd6a8bacf8c9910`, branch `epic-play-nearby-lane-a`. The commit hash is the branch tip commit with the subject `feat: add the nearby-play link interface, frame codec, and FakeLink`.

**Summary.** `GameCore::ILink` (with `PeerHandle`, `NO_PEER`) and `GameCore::Protocol` (AD-13's 12 B header, seven payload types `ADVERT` 1, `JOIN` 2, `ACCEPT` 3, `STATE` 4, `ABORT` 5, `PING` 6, `ACK` 7, `encode` and `decode`, no `MOVE` or `REJECT`) are new `lib/GameCore` files with no radio fact, no allocation, no log, and no static initializer. A seeded, header-only `FakeLink` (`ManualClock`, `Faults`, `FakeLinkNet`, `FakeLink`) and a reusable `expectLinkContract` check live in `test/game_core/`. Every Design Note 1 to 5 choice is built as the owner approved it: strict refusal of another `proto` (Q1), `ACK` as type 7 (Q2), `end()` and no `begin()`, copy-in buffers, value-range checks in the codec, `gameId` 0..32 and label 0..16, tie test deferred, crossplay copyright line (plus, after review, the MIT notice).

**Files.**
- `lib/GameCore/ILink.h` -- new: `PeerHandle`, `NO_PEER`, `ILink`.
- `lib/GameCore/Protocol.h`, `Protocol.cpp` -- new: constants, enums, structs, `encode`, `decode`; the byte tables are in `Protocol.h` (the home AD-13 names).
- `test/game_core/FakeLink.h` -- new: the seeded hostile link, with the crossplay adaptation notice.
- `test/game_core/LinkContract.h` -- new: the contract every `ILink` must pass.
- `test/game_core/ProtocolTest.cpp`, `FakeLinkTest.cpp` -- new suites (ProtocolTest 21, FakeLinkTest 33 after review).
- `test/game_core/CMakeLists.txt` -- the two suites and `Protocol.cpp` added to `GameCoreTest`.
- This plan.

**Review breakdown** (four lenses, context-free subagents, one pass; details in the Review Triage Log): 27 findings -- high 0, medium 3, low 16, false 8. Patched (7 entries, from 14 findings): handle values unique across nets, and the `ILink.h` sentence that an adapter does the same (medium); the roll-count claim reworded and its knob tests extended; jitter modulus in 64 bits; `~FakeLink` calls `end()`; the `encode` no-overlap note; the ASan comment; the MIT permission notice; the exact drain count. Deferred (3, in the frontmatter): the `PROTO` to `Codec::VERSION` tie test (medium, entry 13, owner Decision C7), a three-link variant of the contract (low, entry 16), a full-send-queue knob on `FakeLink` (low, entry 4). Rejected with reasons: 8 false findings (the intent-alignment lens's seven observations and the 250 B truncation remark) and 6 low (duplicated test helpers, public `fromValue`, entry references in comments, `hostApi` naming, unvalidated `Faults` percentages -- reasons in the log). `followup_review_recommended: false` (one medium entry patched, no high).

**Verification** (host evidence on the patched tree, firmware on the same tree before the commit):
- Host suite: `ctest` 2,124 of 2,124 pass: 54 new tests (ProtocolTest 21, FakeLinkTest 33) over 2,070 others. The epic Measurement recorded 2,059 at `c28120a7`; the 11 tests between are from entries merged since (not counted separately).
- `scripts/check_layers.py` passes (608 include edges, no new edge); `check_layers_test.py` passes (53, before the patches; no script changed); `scripts/check_upstream_touches.py` passes; `./bin/clang-format-fix` run twice, the second changed nothing, nothing outside this entry's paths changed.
- Matrix audit: every row of the I/O matrix has a passing covering test (round trips: `EveryTypeRoundTripsAtItsLimits`; short/long: `ShortFramesAreRefused`, `EveryProperPrefix...`, `TooLongFrames...`; magic/proto/session: `WrongMagicProtoOrSessionIsRefused`, `ChecksRunInTheOrder...`; over-long: `OverLongFieldsAreRefusedByEncodeAndDecode`; malformed: `MalformedValuesAreRefusedByDecode`, `EncodeRefusesAFrameNoSenderCouldProduce`; handles: the `FakeLinkTest` handle cases; broadcast: `ABroadcastReachesTheOthersAndNotTheSender`; seeded faults: `ASeedRepeatsExactlyAndAnotherSeedDiffers` and the knob tests; storm: `APacketStormTripsTheCarriedBudget`).
- Mutants (before the review patches; all red, all reverted): `proto` check, snapshot limit (decode and encode), `session` filter, a golden offset, `FakeLink`'s seed, broadcast self-exclusion -- see Implementation Notes. ASan/UBSan by hand on the two new suites: all pass before the patches (not re-run after them).
- Firmware, on the patched working tree over `32362b3d` under the build lock with the shared cache, in this worktree (`/home/user/wt/epic-play-nearby-lane-a`): `pio run -e x4pro` (via `check_flash_budget.py build on`) and `build off` succeed; `compare` exits 0; `objects` exits 0 ("No problems", 50 game objects); `pio run -e default` succeeds; `pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high` passes for `default` and for `-e x4pro` (no defects).
- Size: x4pro games on 6,213,808 B, off 5,955,312 B, **+258,496 B flash** and **+784 B static RAM** (`.dram0.bss` +16, `.iram0.text` +760, `.iram0.text_end` +8). That is +448 B flash and +0 B RAM over the epic's base Measurement (+258,048 B, +784 B), but the base was measured in the main checkout and this is a different worktree path (9 characters longer, 50 game objects, 448 B is about 50 x 9, so the path text of `__FILE__` strings). Like for like, at one tree and one path: the same `build on` with `lib/GameCore/ILink.h`, `Protocol.h`, and `Protocol.cpp` taken out of the tree gives the identical 6,213,808 B and 190,248 B (the games-off build is 5,955,312 B in both and equals the base's), so this entry's own cost is **0 B flash and 0 B static RAM** (`Protocol.cpp.o` is built into `lib0a4/GameCore` but no symbol of `ILink` or `Protocol` is in `firmware.elf`). Share spent: 0 of 9,504 B flash and 0 of 160 B RAM. Later entries compare in their own worktree, or against a base measured there.
- No simulator screenshots (no screen change) and no fresh-clone gate run (no CI gate or workflow change).

**For the orchestrator (outside this entry's `touches`).**
- Spine AD-13 already records the layouts' home (`Protocol.h`); `docs/crosshatch/formats.md` remains a natural place for a copy. `Session.h`'s `MOVE_BYTES` and the `REJECT` comment are stale (entry 7).
- Entries 5 and 16: the handle rule now says an adapter's handle values are unique across every link instance in the process (Design Notes 2 gave "table slot" and "the port" as examples; each needs an instance tag in its high bits, see `ILink.h`). `LinkContract.h` asserts that a handle from the other link of the pair returns false.
- No file outside this entry's `touches` changed; `./bin/clang-format-fix` changed nothing outside it.

**Residual risks.** `FakeLink` cannot pin `EspNowLink`'s agreement on the host (entry 11 checks it on the device); the contract check is two-link only (deferred); the `FakeLink` handle layout caps a net at 63 endpoints and a link at 65,535 handles (no suite comes near).
