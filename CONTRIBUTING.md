# Contributing to Community AR

Welcome. We're glad you're here.

Community AR is built as a real open source project — not a "code dump with license," not "here are our scraps, fix them." The roadmap is ambitious and getting it shipped depends on contributors. Your time matters and this document is written to respect it.

## What you're walking into

A few honest things about this codebase before you invest in it:

**It's opinionated.** The architecture has strong commitments — Oklab color math, per-track state, zero-copy GPU pipelines, three effect engines instead of per-feature classes. These exist because the alternatives are worse for the project's goals, not because of inertia. [`CLAUDE.md`](CLAUDE.md) is the operating manual for these decisions; please read it before proposing significant changes.

**It's pre-1.0.** APIs will change. Internal code will be refactored. We mark stable boundaries (the C ABI, the public Dart API) and treat everything else as work in progress. If you build on top, expect to update your code when we cut new versions.

**It's verbose by design.** Files open with multi-paragraph "why this file exists, what tensions it resolved" comments. This is deliberate — it's slow to write and fast to read, which is the right trade-off for a codebase meant to be modified by many hands.

**Quality is real.** We test on diverse faces and devices because the alternative produces visibly worse output for half the world. "Works on my iPhone" isn't done; the verification checklists in `docs/car-phase_*.md` are.

**The maintainer team is small.** Reviews may take a few days. We try to give substantive feedback on every PR but sometimes that means waiting for the right reviewer. If you don't hear back in a week, ping the PR.

## How to find your first contribution

You don't need permission to start working — but you might save time by skimming open issues for the `good first issue` and `help wanted` labels first. If you want to propose something not currently tracked, open a Discussions thread before writing code.

### Match by skill and interest

| If you have... | Try... |
|---|---|
| A Flutter app and curiosity | Run the example app, report what breaks on your device |
| GLSL shader experience | Pick a planned Bucket A effect (iris, teeth, brows) — each is ~70 lines |
| C++/graphics background | Improve `MaskRasterizer` (handle concave polygons properly), or extend `RenderContext` |
| ML / MediaPipe familiarity | Help us validate or improve segmentation quality on diverse hair textures |
| Design sense | Propose the v1 effect catalog — what looks should the library ship with by default? |
| Technical writing | Tutorials, especially first-time-Flutter and first-time-AR ones |
| A device we don't have | Run the verification checklist, file detailed reports |

### The fastest meaningful contribution

If you want to ship something useful in your first hour: **run the example app on a real device, and file a detailed issue about how it performs**. Specifically:

- What device, OS version, and Flutter version
- Frame rate (visible from the debug overlay)
- Lipstick quality across the palette swatches
- Anything that looks wrong, jittery, or doesn't match the demo on other devices

Device coverage is the single biggest gap between this library and polished closed-source alternatives. Filling it doesn't require coding — just attention.

## How to propose larger changes

For anything bigger than a small fix:

1. **Open a Discussions thread first.** Describe what you want to build and why. Tag a maintainer if you want fast feedback.
2. **Wait for at least one ack.** This is for your benefit — we'd rather catch architectural issues before you've spent a weekend on a PR we'd ask you to rewrite.
3. **Reference the discussion in your PR description.**

This isn't bureaucracy. It's because the codebase has invariants (see [`CLAUDE.md`](CLAUDE.md)) that aren't obvious from reading code alone, and we want your time to land where it matters.

## How we work

### Pull requests

- Small, focused PRs review faster than large ones. If you're touching more than ~500 lines, consider splitting.
- Write the PR description like a story: *what changed, why, what alternatives you considered, what to look at first.*
- **Tests are required, not optional.** [`TESTING.md`](TESTING.md) at the repo root is the testing guide — it walks first-time contributors through what to run before submitting, what tests to add for your specific kind of change, and the on-device verification protocol for changes that touch rendering. Read it before opening your first PR.
- New effects aren't merged until they've been tested on real faces — at minimum two skin tones and two hair textures. We can help with this if you don't have the devices/faces yourself.

### Code review

- We review for: architectural fit, performance, code clarity, test coverage, and quality across diverse inputs.
- Reviewer comments are suggestions unless they say `blocking:`. Discussion is welcome.
- Reviewers should be specific and kind. If you've been on the receiving end of toxic open source culture, you won't get it here. If you find yourself acting as a reviewer, hold that line.

### Communication

- **GitHub Issues** for concrete bugs and feature requests.
- **GitHub Discussions** for design questions, "is this a good idea?" conversations, and showing off what you've built.
- **PR threads** for everything related to a specific change.

We try to communicate in writing in public channels by default. Private DMs about the project happen when needed but the default is public so the rest of the community can learn from the conversation.

### Style and conventions

- C++17. `#pragma once`. No exceptions in render-thread paths. See [`CLAUDE.md`](CLAUDE.md) for the full conventions doc.
- `dart format` on every commit.
- Comments explain *why*, not *what*.
- Don't break the C ABI without a discussion.

## What we won't accept

Being upfront about these saves everyone's time:

- **AI-generated PRs without human review.** AI tools are fine for drafting, but submit only what you've personally read, tested, and stand behind. PRs that read as unverified bulk-generated code will be closed.
- **Drive-by performance "optimizations" without measurement.** Bring numbers.
- **Style-only changes to existing code.** If you find code that bothers you, open an issue first.
- **Changes that improve one face/skin tone/hair texture at the expense of others.** Quality is universal here; trade-offs need to be explicit and justified.
- **Behavior that's hostile to other contributors.** This includes condescension, gatekeeping, and pretending that asking questions is somehow inferior to "figuring it out yourself." We have no patience for any of it.

## Recognition

Contributors are credited in release notes. Significant contributions earn co-authorship on the relevant commits. Major and sustained contributors are invited to join the maintainer team — this happens organically based on demonstrated judgment, not by application.

## Code of conduct

We use the [Contributor Covenant](https://www.contributor-covenant.org/version/2/1/code_of_conduct/) v2.1. Short version: be kind, assume good faith, and treat each other as colleagues. The maintainers enforce it.

## License

By contributing, you agree that your contributions are licensed under Apache 2.0 (the same license as the project).

---

## A note on motivation

If you're contributing to this project, you probably care about face AR working well — not just for the use cases that show up in research papers, but for the use cases real people have in real apps.

The library is built openly because the best version of it is one that many people contribute to. Closed proprietary SDKs optimize for the markets their owners care about. An open library can optimize for whatever its contributors collectively bring. That's the bet. Help us prove it.

Whatever you bring — code, tests, docs, design, device coverage, honest bug reports — thank you for being here.
