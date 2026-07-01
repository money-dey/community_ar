# Welcome message — variants for different contexts

`CONTRIBUTING.md` (in the repo root) is the canonical long-form welcome. The
short-form versions below are for places where length kills readability.

---

## Variant 1 — Pinned Discussions post

> **Welcome to Community AR**
>
> Glad you found us. This is a small, opinionated, ambitious project — an open-source face AR library for Flutter, currently pre-1.0. The roadmap is real and getting it shipped depends on contributors.
>
> **Before you write code:** skim [`CONTRIBUTING.md`](../CONTRIBUTING.md) and [`CLAUDE.md`](../CLAUDE.md). The first tells you how we work; the second tells you why the codebase is structured the way it is. Together they save you from writing PRs we'd ask you to rewrite.
>
> **If you want to ship something useful in your first hour:** run the example app on a real device and file a detailed report on how it performs. Device coverage is the single biggest gap between us and polished closed-source alternatives — and you don't need to write code to help fill it.
>
> **Use Discussions for:** design questions, "is this a good idea?", showing off what you've built, asking for help.
> **Use Issues for:** concrete bugs and concrete feature requests.
>
> Be kind, assume good faith, treat each other as colleagues. The maintainers hold that line — we have no patience for the toxic patterns common in some open-source communities.
>
> Looking forward to your contribution, whatever form it takes.

---

## Variant 2 — First-time contributor auto-comment on PR

> 🎉 **Welcome — first contribution to Community AR**
>
> Thanks for opening this PR. A maintainer will review it within a few days.
>
> A few notes for first-time contributors:
>
> - **Read the PR description guidelines in [`CONTRIBUTING.md`](../CONTRIBUTING.md).** A good description (what changed, why, alternatives considered) speeds review meaningfully.
> - **If your change touches the C ABI, the public Dart API, or the architecture documented in [`CLAUDE.md`](../CLAUDE.md):** flag this in the description so we route the right reviewer.
> - **Review comments are suggestions** unless they say `blocking:`. Discussion is welcome.
> - **If you don't hear back in a week, ping the PR.** We try to respond promptly but sometimes the right reviewer is heads-down elsewhere.
>
> Looking forward to digging in.

---

## Variant 3 — Issue template welcome (top of bug-report template)

> Thanks for opening an issue. Before you write:
>
> - Search [open issues](../../issues) and [closed issues](../../issues?q=is%3Aissue+is%3Aclosed) — your problem may already be known.
> - For design questions or "is this a good idea?" conversations, [Discussions](../../discussions) is the better venue.
> - For bugs: include device, OS version, Flutter version, and reproduction steps. Vague reports take longer to act on.
>
> Be kind in your phrasing — the maintainers are human and (mostly) volunteers.

---

## Variant 4 — README "Contributing" section (already in README.md, shown here for reference)

> **Contributions are warmly welcomed.** The roadmap is substantial and getting it shipped depends on engineers, testers, and designers from many backgrounds.
>
> What matters most right now: device testing, shader work, on-device ML improvements, documentation, design.
>
> Start here:
> - [`CONTRIBUTING.md`](CONTRIBUTING.md) — how we work
> - [`CLAUDE.md`](CLAUDE.md) — how the codebase wants to be modified
> - Good first issues: [labeled `good first issue`](../../labels/good%20first%20issue)

---

## Variant 5 — Short "we welcome you" for use elsewhere (Twitter/Mastodon thread, conference talks, etc.)

> Community AR is an open-source face AR library for Flutter. The codebase is small enough to read, opinionated enough to be coherent, and ambitious enough that contributions matter. If you've ever wanted to ship a beauty filter or face effect in your Flutter app and didn't want to take a dependency on a closed SDK — try it, file an issue, send a PR.

---

## Variant 6 — For a non-technical audience (potential testers, designers)

> **You don't need to write code to help Community AR.**
>
> We're building an open-source face AR library for mobile apps — the kind of technology behind beauty filters, virtual makeup, and AR accessories. We need testers, designers, and people willing to try the demo on their phone and tell us what's wrong.
>
> No experience with face AR? Good — we want fresh eyes. The thing that gets a library from "tech demo" to "people actually use it" is breadth of testing, and that's something anyone with a phone and a face can contribute to.
>
> Try the demo, tell us what's broken, propose effects you wish existed. That's a real contribution. Open an Issue or join Discussions to start.

---

## Notes on tone

The welcome messages above share a few intentional choices:

- **Specific over generic.** "Run the example app and file a detailed report" is concrete and accomplishable. "We welcome all contributions!" is noise.
- **Honest about expectations.** Pre-1.0, opinionated, small maintainer team — these aren't selling points but they prevent mismatched expectations.
- **No false enthusiasm.** "Glad you found us" reads as warmer than "We're SO EXCITED you're here!!" because it implies the writer is a real person, not a marketing automation.
- **Boundary-setting where it matters.** "We have no patience for toxic patterns" is direct because indirect language about culture invites the people who'll test those boundaries.
- **Crediting non-code contributions.** Device testing, design, docs, and bug reports are real contributions — saying so explicitly helps non-coders see themselves in the project.
