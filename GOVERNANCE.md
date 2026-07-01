# GOVERNANCE.md

**⚠️ Internal working document — Project Manager's operating playbook**

This is a personal operating playbook, not a public governance charter. It's written for the Project Manager (the maintainer) to use as a reference while building the project's process disciplines. Some sections describe internal thinking that wouldn't be appropriate for a public-facing document (e.g., BDFL framing, vendor-capture concerns). Before publishing any version of this:

- Decide whether to split into `GOVERNANCE.md` (public, formal) and a private playbook
- Remove or soften sections marked **internal**
- Have at least one outside reader review tone

---

## Framing

A few principles that should shape every decision in this document.

**The project is solo or near-solo for a meaningful stretch.** Process for 1-3 contributors is dramatically different from process for 50. Don't adopt Kubernetes-style governance for a project with three contributors — it kills velocity and looks performative.

**Process serves the work, not the other way around.** Every rule has a cost: contributors must learn it, the PM must enforce it, and it can be wrong. Add rules in response to felt pain, not preemptively.

**Asynchronous-first is not optional.** The PM is in Cameroon. Contributors will be everywhere. All decisions happen in writing in public GitHub threads so the record exists for any time zone.

**Start light, add weight as you genuinely need it.** Projects that thrive aren't the ones with the most polished process — they're the ones whose process matches their actual size and stage.

---

## Project management

### Tooling

For the first 6-12 months: **GitHub Projects** is sufficient. Free, native to the platform contributors are already using, publicly visible. Move to something else only when actually outgrown.

### Project board configuration

**One Project board, three views:**

| View | Purpose | Audience |
|---|---|---|
| **Roadmap** | Gantt-style timeline showing all 8 phases | Public-facing planning artifact |
| **Sprint** | Kanban (Backlog / In Progress / In Review / Done) for the current 2-4 week horizon | Day-to-day operating view |
| **Triage** | Table view filtered to issues without status | PM's weekly sweep |

### Labels that earn their keep

Start with this minimal set:

- `phase-N` — one per active phase
- `good-first-issue` — GitHub surfaces these to newcomers via search
- `help-wanted` — broader call for contributions
- `bug`, `enhancement`, `documentation`, `discussion`
- `blocking` — release-blocking; use sparingly
- `needs-design` — architecturally significant; needs discussion before code

Don't add more labels until you've felt the absence of one.

### Milestones map to phases

Each phase from the roadmap is a GitHub milestone. Every PR and issue is attached to a milestone. Makes "what's left in Phase 3" a single click.

### Communication channels

Pick a small set and commit:

- **GitHub Discussions** — design questions, "is this a good idea?", showing off work, asking for help. Default-public.
- **GitHub Issues** — concrete bugs and feature requests with reproducible specs.
- **PR threads** — review of specific changes.
- **Discord (recommended) or Matrix** — real-time casual conversation. All *decisions* still happen in writing in public GitHub threads.

**Avoid Slack.** The free tier hides history after 90 days; hostile to async open source.

### Cadence

- **Weekly:** triage new issues (15-30 min). Reply to anything older than a week.
- **Bi-weekly:** public update — a Discussions post summarizing what shipped, what's coming. Five paragraphs max. Builds momentum, signals "this project is alive."
- **Per phase:** retrospective Discussions post when a phase closes. What went well, what didn't, what was learned. Public learning attracts good contributors.

---

## Versioning & code workflow

### Branching: trunk-based with short-lived feature branches

`main` is always green and shippable. Everyone branches off `main`, lands back via PR.

- No long-lived `develop` branch — anti-pattern at this scale; doubles testing surface for no real benefit.
- Feature branches: `feature/short-description` or `fix/short-description`. Delete after merge.
- For phases with genuinely interdependent PRs: one `phase-N/integration` branch where Phase N's work coalesces before landing on main. Most phases won't need this.

### Conventional commits

Use [Conventional Commits](https://www.conventionalcommits.org/) from day one:

```
<type>(<scope>): <description>

<optional body>

<optional footer>
```

**Types:** `feat`, `fix`, `docs`, `refactor`, `perf`, `test`, `build`, `ci`, `chore`.

**Examples:**
```
feat(effects): add LipsEffect with Oklab recolor
fix(perception): handle empty face landmark list without crash
docs(contributing): clarify AI-generated PR policy
perf(render): batch landmark dot rendering into instanced draw call
```

**Why:** enables automated changelog generation, makes git history scannable, makes "what changed in this release" answerable in seconds. Small discipline, large payoff.

### Pull requests

**Required for everything except trivial typo fixes by maintainers.** Even maintainers don't push directly to `main` — normalizes review, catches mistakes.

**PR template** (`.github/pull_request_template.md`):

```markdown
## What this PR does
<one paragraph>

## Why
<context — link to issue, discussion, or describe the motivation>

## How to verify
<steps for a reviewer to test this>

## Notes for reviewers
<things to look at first; what's risky; what you're unsure about>

## Checklist
- [ ] Tested on a real device
- [ ] Verified across at least two skin tones / hair textures (if effect-related)
- [ ] Updated CHANGELOG.md
- [ ] Updated relevant docs
```

**Review SLAs** (publish these):
- First reviewer response within 5 business days
- Merge or substantive blocker within 10 business days

Realistic for a small team; gives contributors a clock. The fastest way to lose contributors is silent inaction.

**Squash-merge by default.** Each PR becomes one clean commit on `main`. PR history preserved in the PR itself for archaeology. Exception: PRs with genuinely atomic multiple changes (rare) can rebase-merge.

### Semantic versioning

Follow [SemVer](https://semver.org/): `MAJOR.MINOR.PATCH`.

- **Pre-1.0:** `0.MINOR.PATCH`. Breaking changes bump MINOR — convention while iterating on the API.
- **1.0** when the public API is stable enough to commit to backward compatibility.
- **Post-1.0:** breaking changes require MAJOR bump and a deprecation period.

**Don't release 1.0 prematurely.** Every API change becomes a real commitment once you're at 1.0.

### Release process

- **Cadence:** every 2-4 weeks during active development, even if changes are small. Regular cadence signals project health.
- **Tagging:** tag releases on GitHub with annotated release notes.
- **Automation:** GitHub Actions builds artifacts, generates changelog from conventional commits, publishes to pub.dev.

### Changelog

[Keep a Changelog](https://keepachangelog.com/) format. Generate bulk from conventional commits via `git-cliff`, `release-please`, or `standard-version`. Hand-edit for narrative flow.

```markdown
# Changelog

## [Unreleased]

### Added
- Multi-band frequency separation skin beautification (#42)

### Fixed
- Iris tracking jitter on rapid head movement (#38)

### Changed
- BREAKING: `LipsEffect.color` now accepts opacity via `opacity` parameter
  rather than `Color.alpha` (#45)

## [0.2.0] — 2026-06-15

### Added
- LipsEffect — first end-to-end effect (#28)
```

The `[Unreleased]` section is updated by every PR. On release, the section gets dated and a fresh `[Unreleased]` opens.

---

## Contributors management

### Governance model — **internal section**

> ⚠️ The framing below is for PM-internal use. The public version of this section should describe roles and decision processes without using the term "BDFL" or implying that current governance is provisional.

The project is currently in a **Benevolent Dictator** phase. The PM has final say on architectural decisions and merges. This is honest, normal for projects at this stage, and provides a clear path for the project to evolve as the contributor base grows.

**Internal trajectory (not to be published verbatim):**

| Stage | Triggers | Governance |
|---|---|---|
| 0-6 months | <5 sustained contributors | BDFL — PM decides everything |
| 6-18 months | 5-15 sustained contributors | Maintainer team — PM + 2-4 trusted contributors share commit access; PM retains tiebreaker |
| 18+ months | 15+ sustained contributors | Formal governance — TSC or similar; PM steps back to lead-maintainer role |

Move between stages based on demonstrated readiness, not calendar pressure. Skipping stages creates instability; staying in a stage too long burns the PM out.

**Public version (safe to publish):**

> This project is currently maintained by [PM name], who has final say on architectural decisions and merges. As the contributor community grows, we will evolve toward shared maintainership with the following structure:
>
> - **Contributors:** anyone who submits a PR
> - **Triagers:** trusted contributors with label/triage permissions
> - **Maintainers:** trusted contributors with merge permissions
> - **Lead maintainer:** holds final decision on architectural disputes

### Path to maintainership

Make the criteria public:

- Demonstrated judgment through several merged PRs
- Comfort with the codebase's architectural invariants (CLAUDE.md compliance)
- Willingness to do reviews (the actual maintainer workload, not the glamorous part)
- Time consistency — open source burns maintainers who join in a sprint and disappear

**Invite by demonstrated work, never by application.** "Hey, you've shipped great work on three PRs and your reviews are thoughtful — want to take on triage?" is the right frame.

### CLA / DCO decision

Three options:

| Option | Friction | Use case |
|---|---|---|
| No CLA | None | Most open source projects |
| **DCO** (Developer Certificate of Origin) | One `-s` flag on commits | Provides legal cover; used by Linux, Docker |
| CLA | High | Only if anticipating future relicensing or dual-licensing |

**Decision: use DCO.** Right balance for a project that wants to stay open source long-term but maintain legal hygiene. Friction is one git flag.

Configure GitHub Actions to enforce `Signed-off-by:` on all commits.

### Code of Conduct enforcement — **internal section**

Adopt the [Contributor Covenant](https://www.contributor-covenant.org/) v2.1.

**Honest framing:** having a CoC without willingness to enforce is worse than not having one. It creates false expectations.

**Operational requirements:**

- Separate email address (e.g., `conduct@<domain>`) — not the PM's personal email
- Written response process: acknowledge within 48 hours, investigation period documented, action documented
- Written appeals process for contributors who feel an enforcement was unfair
- Annual review of any enforcement actions taken

**Mental commitment:** the first hard enforcement is the moment the community decides whether the CoC is real. When the moment comes, act decisively. The project's culture is set by what is tolerated, not what is said.

In the first year, expect enforcement to be rare. When needed, act fast.

### Recognition

Compound practices that build community over time:

- **[All Contributors bot](https://allcontributors.org/)** — recognizes non-code contributions in README. Documentation, testing, design, translations, ideas. **Adopt immediately.** Highest-leverage thing for making non-code contributors visible.
- **Release notes credit** — every release mentions specific contributors and their work by name.
- **`MAINTAINERS.md`** — public listing of current maintainers with areas of focus.
- **Bi-weekly update posts** — name contributors and link their PRs.

GitHub's contributor graph is automatic but uninspiring on its own — don't rely on it.

### Onboarding new contributors

Two practical levers:

#### Good first issues

Curate a steady supply of 5-10 issues labeled `good-first-issue`. They should be:

- Genuinely small (~half a day's work for someone new)
- Self-contained (no architectural questions)
- Useful (not busywork)
- Well-described (what to do, what to avoid, where to look)

**Curating one good first issue takes 30-60 minutes.** It's worth it. Set aside time for this monthly.

#### First-PR experience

When someone opens their first PR, the experience determines whether they ever return:

- Reply within 48 hours, even if just to acknowledge
- First feedback should focus on substance, not style
- Be warm — first-time contributors are nervous
- If they're close but not quite there, offer to pair rather than reject

### Sustainability — **internal section**

Two failure modes to actively guard against:

**Maintainer burnout.** The project will outlast initial PM energy. Plan for a future where the PM is less available than now:
- Build a maintainer team early — first additional maintainer within 6 months of significant external contribution
- Document everything as if leaving tomorrow (CLAUDE.md is a start; expand it)
- Ruthlessly say no to scope creep that isn't on the roadmap
- Take breaks. Publicly. Set "PM offline" status when needed.

**Vendor capture.** As the project gains visibility, companies may want to influence its direction in ways that serve them more than the community. Get ahead of this:
- Maintainers disclose their employer in `MAINTAINERS.md`
- No maintainer represents their employer's interests in maintainer decisions
- Significant feature requests from vendors go through the normal RFC process — no shortcuts
- Vendor financial support is welcome via GitHub Sponsors / OpenCollective, with explicit decoupling from project direction

---

## Action checklist — what to do this week

In priority order:

- [ ] Adopt conventional commits format starting from next commit
- [ ] Configure GitHub Actions to enforce DCO `Signed-off-by:` on commits
- [ ] Write `CODE_OF_CONDUCT.md` using Contributor Covenant template
- [ ] Set up a separate email for CoC reports
- [ ] Create the GitHub Project board with the three views
- [ ] Create GitHub milestones for all 8 phases
- [ ] Write public-facing `GOVERNANCE.md` (separate from this playbook, BDFL framing removed)
- [ ] Curate 5 `good-first-issue`s for Phase 3
- [ ] Set up All Contributors bot
- [ ] Set up GitHub Actions CI (test, format check, build)

## Action checklist — what NOT to do this week

- ❌ Detailed contributor governance document for a non-existent governance body
- ❌ Complex review process for PRs that don't exist yet
- ❌ Discord server with 12 channels before there are 12 active people
- ❌ Weekly community call until enough contributors exist to attend
- ❌ TSC, RFC process, or formal voting before the maintainer team has 3+ people
- ❌ Release 1.0 to feel legitimate (release 1.0 when the API is genuinely stable)

---

## Document maintenance

This playbook should evolve with the project. Update it when:

- A new operational pain point emerges and is solved
- The governance model genuinely changes (e.g., adding first co-maintainer)
- An assumption proves wrong and a rule needs revision

**Quarterly review** — re-read this document every quarter and ask: what's no longer true? What's missing? What's aspirational that should become real?

Last updated: [date]
