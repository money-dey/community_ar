# Strategy — what Community AR is competing on

A short document capturing what this project is trying to be and,
more importantly, what it is *not* trying to be. Written after Phase 3
shipped, before Phase 4 started, when the question came up:
*"Can Community AR do warping better than Snapchat?"*

That question is a decision point, not a rhetorical one. Getting the
answer wrong means either building the wrong project (impossible target,
years of work with no payoff) or scoping too small (missing what
actually matters). This document records the answer and the reasoning
behind it, so future work doesn't lose the thread.

---

## The honest technical assessment

**Community AR will not beat Snapchat's warping in v1. Probably not
in v2. Possibly never, on the "overall warping quality" scoreboard.**

Snapchat's warping is the visible tip of roughly a decade of focused
investment: a face mesh denser than MediaPipe's (1000+ points vs. 468),
custom-trained models for face-specific tasks, hardware-tuned shader
implementations across dozens of GPUs, per-region semantic masks
informing how warps blend at boundaries, and — critically — years of
telemetry on which warp configurations users perceive as natural vs.
uncanny. That tuning lives in thousands of magic constants learned by
watching millions of users react to filter variants. A small team
approximates it; matching it requires similar feedback loops.

The Phase 4 warp engine, as specified, produces warps that work
reasonably well on most faces in most conditions. That is not the same
as "beats Snapchat," and pretending otherwise would be self-deception.

## The scoreboard that actually matters

The right question is not "can we beat Snapchat overall" but "can we
beat Snapchat on the specific things this project is architected to
compete on." Two candidates:

**1. Open-source availability.** Snapchat's warping is proprietary
and locked to their app. No path exists for a developer to use it in
their own Flutter application. Community AR doesn't have to be
*better* than Snapchat — it has to *exist as an open alternative.*
80% of Snapchat's quality, actually usable outside their app, is a
meaningful value proposition.

**2. Cross-skin-tone correctness.** Snapchat's filters are documented
(by users, journalists, small academic literature) to perform worse
on darker skin tones. The segmentation falters, landmark tracking
jitters, and "subtle" smoothing turns into obvious washing-out. The
reason isn't malice — it's that training data and tuning were dominated
by lighter skin samples, and the market signal for fixing dark-skin
failures was weak enough that they never got prioritized.

Community AR was architected with this as a first-class concern.
Every technical decision points at it:

- Oklab color math instead of RGB (invariant 8)
- Baseline-luma threshold scaling in every beauty shader pass
- Deliberate landmark selection avoiding MediaPipe's known weak spots
- Verification checklists that require dark-skin testing as a release
  blocker (`docs/car-phase-3.md`, `TESTING.md`)

**If we execute on the differentiator, we do not need to beat Snapchat
overall. We need to be the obvious choice for anyone who cares about
this specific property.** That's a much smaller, more achievable bar.

## Adjacent differentiators worth investing in

The same "under-served by the incumbent" logic applies to two more
categories where the gap is real:

**Diverse hair textures** — Snapchat handles 4A-4C hair, locs, and
braids poorly enough that there's documented user frustration.
Community AR's `MulticlassSegmenterBackend` outputs a hair channel;
Phase 6+ effects can consume it. Investment here has the same shape
as the skin-tone bet: sustained attention to failures the incumbent
ignores.

**Transparent quality trade-offs** — Snapchat's approach is
"it works or it doesn't." Community AR's tier system (`BeautyQuality`
enum, auto-benchmark, adaptive throttling) exposes the trade-offs
explicitly. This matters for developers building on the library who
need to reason about device-class support in their own apps.

**Open extensibility** — you cannot add a custom warp to Snapchat.
You can to this. For niche use cases (medical AR, accessibility, AR
research), the extensibility itself is the value; quality is a
secondary concern.

## What this means for Phase 4

The Phase 4 requirements document (`docs/car-phase-4-requirements.md`)
specifies four warp effects: `EyeEnlargeEffect`, `NoseReshapeEffect`,
`LipPlumpEffect`, `FaceSlimEffect`. That scope was chosen to keep pace
with what consumer beauty apps typically offer — the four effects
essentially every filter library ships.

**Given the differentiation strategy above, this scope is defensible
but not obligatory.** Two reasonable positions:

### Position A: Ship all four (current requirements doc)

- Community AR becomes a general-purpose face AR library that also
  happens to be strong on diverse skin tones
- Feature parity closes with consumer expectations sooner
- Risk: attention gets divided across four effects with no single one
  being genuinely differentiated

### Position B: Ship one, invest the saved time in the differentiator

- Ship `EyeEnlargeEffect` as the single Phase 4 warp; defer the other
  three to Phase 4.5 or later
- Use the saved time on: cross-skin-tone comparison content
  (screenshots, videos, published benchmarks vs. major competitors),
  hair-texture handling improvements, tutorial content that
  demonstrates the differentiator
- Risk: looks feature-poor to users evaluating on a checklist
- Reward: the project's positioning becomes visibly distinctive rather
  than "another beauty filter"

**My recommendation: Position B.** Reasoning:

- Pre-1.0 projects benefit more from being distinctive than from being
  complete. The distinctive property attracts contributors and users
  who care; completeness alone attracts neither.
- The four Phase 4 effects are architecturally similar (all share the
  same engine). Shipping one proves the engine; shipping the other
  three is mechanical work that can happen later without re-thinking.
- The differentiator work has compounding value. Screenshots of
  Community AR beauty vs. major competitors on darker skin tones is
  the kind of content that spreads. A fourth warp effect is not.
- Position A can be adopted from Position B (just build the other
  three when ready). Position B is harder to adopt from Position A
  (you've already spent the time).

## The decision

**Not made yet.** This document exists to capture the framing so
whoever picks up Phase 4 can make the call with the reasoning in
front of them. Both positions are defensible; the important thing is
not to drift into Position A by default because it's what the
requirements document already spelled out.

If Position B is chosen, the concrete changes to the current plan:

1. `docs/car-phase-4-requirements.md` gets a note at the top marking
   `NoseReshapeEffect`, `LipPlumpEffect`, and `FaceSlimEffect` as
   scoped out of the initial Phase 4 release, moved to a Phase 4.5
   follow-up
2. Type IDs 33-35 stay reserved (per invariant 2, we don't reclaim
   type IDs even for things we haven't shipped)
3. The engine itself gets built the same way — it needs to be general
   enough to support all four effects, even if only one ships initially
4. Roadmap in `README.md` updates to reflect the tighter scope

If Position A is chosen, the current requirements document stands and
Phase 4 proceeds as specified.

---

## Related documents

- `docs/car-phase-4-requirements.md` — the tactical spec for Phase 4
- `docs/CARRIED_FORWARD.md` — technical deferrals with concrete
  "when to do this" conditions
- `README.md` — the public roadmap (updates depending on which
  Position is chosen)
- `CLAUDE.md` invariant 10 — "diverse faces are part of done";
  the operational commitment to the differentiator

## For future readers

If you're picking this up months from now, the useful thing to know
is that this document exists because a specific question was asked at
a specific moment. That question is worth asking again periodically:
**what are we competing on, and is our current work pointed at that?**

The answer might change. The Snapchat landscape changes. The
open-source AR landscape changes. What was under-served a year ago
might be well-served today, and vice versa. Re-read this document
when starting a new phase; update it when the answer shifts.
