# Automatic review of pull requests and issues

A workflow that reads every new pull request and issue, and answers within minutes with one comment that says it is
automatic and that a maintainer will follow. A pull request gets a review in the manner of `review-diff`, judged
and never fixed: whether the approach is the canonical one, whether it holds for every player and every content
rather than the one that showed the fault, and what it does to the player. An issue gets a first investigation:
what kind it is, where in the code it likely lives, what is missing to act on it, and its duplicates.

When it lands, the lasting decisions fold into a header comment of the workflow and into the instructions it
reads, and this file is deleted.

## What the tree already has for it

Most of the parts stand in `upstream.yml`, which runs three agents on untrusted text today.

- **The agent's cage**: `claude-code-action` pinned by commit, a read token, `--setting-sources user`, the
  sandbox with no network and every credential's variable denied, `/proc`, `~/.claude` and `.git` unreadable,
  hooks and skills off, the tools listed by name. The review job copies it whole.
- **Instructions read from master, never from the change**: `.github/upstream-merge/review.md`, extracted with
  `git show origin/master:` before anything untrusted is checked out.
- **Results as data, published by a job that runs no agent**: `review` hands a bundle and a report to `publish`,
  which scans them for the agent's credential before anything is public.
- **Crash packages read against a release's symbols**: `omw crash <package> <symbols.zip>`, built to read a
  player's dump with every field bounds-checked, and the Breakpad symbols every release publishes.
- **Workflow lint**: `omw lint` runs zizmor offline over `.github/`.

## Shape

One workflow, `.github/workflows/auto-review.yml`. Its review jobs run only under `workflow_dispatch` and an hourly
`schedule` (at minute 17, off the top of the hour, when GitHub's scheduler is busiest). `pull_request_target`
(opened, reopened) and `issues` (opened, reopened) start a `notice` job that checks out nothing, runs nothing of
the item's, and dispatches the workflow for that item with `actions: write`, so a new item is answered within
minutes.

A push is debounced by 5 hours: a pull request's later head is reviewed once it has stood that long, so a day of
pushes costs one review, not one a push. The hourly sweep finds it due. When a head arrived is read from CI's own
`pull_request` run at that head, which GitHub creates on every push, a fork's awaiting approval included; a
commit's date is the author's to write and says nothing of when it was pushed, and GitHub's API no longer reports
a push time. Refused: a job that sleeps out the debounce, which would hold a runner for 5 of the 6 hours GitHub
allows a job.

The four jobs after it: only the third holds the agent, and it holds nothing worth stealing and can send
nothing; only the fourth can write, and it runs no agent and nothing the change carries.

1. **`survey`** (no agent, `issues: read`, `pull-requests: read`, `actions: read`). Lists what needs reading: the
   item a dispatch names, or on the schedule every open one. Skipped: bot authors (the upstream merges have their
   own review), the repository owner's own items, the `no-auto-review` label, an item already answered at its
   current head (by a marker the workflow's own account wrote, so a marker anyone else pastes counts for
   nothing), a later head that has not stood 5 hours, a pull request reviewed five times already, a head whose
   review failed twice. Drafts are read.
   Then the budget, counted from the workflow's own run history, whose review job is named after its item, so a
   failed run counts as much as a posted one and nothing is stored:
   - an author's items, 3 in any 24 hours;
   - the repository's review jobs, 20 in any 24 hours, and reaching it annotates the run;
   - 8 items a run.

   An item over a limit waits for the next sweep and is never dropped. Emits the matrix.
2. **`gather`** (no agent, no secret, `contents: read`), per item. Writes `.review/` as plain data and uploads it:
   - a pull request: its title, body and author; the diff against its merge base; the head's tree under
     `pr-head/`, as `git archive` of `refs/pull/N/head` (files, no checkout, no hooks); the discussion so far; CI's
     check conclusions at that head, where finished; the previous automatic review and its open threads;
   - an issue: its title, body and comments; every issue's number, state and title, for duplicates; its
     attachments from `github.com/user-attachments/` alone, size-bounded, and each crash package read by
     `omw crash` against the matching release's `-symbols.zip` into `crash.txt`.

   Here because a hostile zip that broke the reader would break a job with no secret and no write, and its
   output is text the next job reads as data.
3. **`review`** (agent, `contents: read`), per item. Checks out master, extracts the instructions
   (`.github/auto-review/pull-request.md` or `issue.md`) and lays `.review/` beside them. The agent may `Read`,
   `Grep`, `Glob`, `WebSearch`, and `WebFetch` on an allowlist; no `Edit`, `Write` or shell. Its answer is
   structured output under `--json-schema` (the action's `structured_output`), uploaded as JSON. `--model opus`,
   as `upstream.yml`'s agents. `--max-turns` scales with the diff, as `review` scales with the files resolved.
4. **`publish`** (no agent, `pull-requests: write` or `issues: write`), per item. Checks out master and runs
   `omw review-post`, which turns the JSON into the comment and posts it. Everything the agent wrote is
   untrusted, so this step is where the guarantees live, and it is Python with tests rather than inline shell:
   - the JSON is parsed into dataclasses that refuse an unknown field, a missing one or an over-long one;
   - the header and the footer are the template's, never the agent's, so the disclosure is always there;
   - the agent's text is sanitized the way GitHub's own agentic workflows do it: control characters removed,
     `@mentions` and slash commands neutralized (into code spans), HTML comments and raw tags made text, links
     kept only to allowlisted domains, closing keywords (`fixes #12`) neutralized, the whole bounded;
   - an inline finding stays inline only on a line its diff hunk covers, as GitHub refuses the rest; the others
     join the body under their file;
   - the agent's credential is searched for in the rendered text, as `publish` does, and its presence posts
     nothing and fails the job;
   - a pull request gets one review with event `COMMENT`, never `APPROVE` or `REQUEST_CHANGES`; on a new head,
     threads of the previous automatic review that the agent marks answered are resolved;
   - an issue gets one comment;
   - a `dry_run` dispatch input writes the rendered comment to the job summary instead of posting it.

Concurrency is one group an item, cancelling the run a newer push supersedes.

## The review's instructions

`.github/auto-review/pull-request.md`, read from master. It is `review-diff`'s two passes as a reviewer who
cannot edit, and `upstream-merge/review.md`'s passes are the same text: both scope files keep only their scope and
report, and the passes move into `.github/review/passes.md`, read by both. Added for this review:

- **Canonical.** What the field's established technique is for this problem — a paper, the Vulkan specification,
  a vendor's guide, a well-regarded engine — found and cited, and whether the change takes it or says why not.
  C++ judged against `AGENTS.md`'s idioms. A change that brings back what `AGENTS.md` lists as tried and declined
  is named as such.
- **General.** The fix holds for every player, card and content, not the one the report came from. Patch signs
  specific to this tree: a mod's, plugin's, record's, mesh's, texture's or cell's name in non-test code; a constant
  tuned until one place looked right; a branch on a vendor or driver outside the vendor seam; a setting added so
  one player can turn a fix off; vanilla's picture changed because a replacer exists; a claim checked under one of
  the three contents `AGENTS.md` names and not the other two. The question it asks is what property of the data
  the change should have keyed on instead.
- **The player.** A new setting earns its place: inferred where it can be, a default that is right, documented in
  `docs/settings.md`. A failure says its cause and what to do in the fatal error box, never a silent fallback.
  A frame's cost reported as its median, p99 and worst frame, a hitch at load counted as one. A picture that
  moves shows it moved (`shot --against`), and the review asks for that evidence where the change touches what a
  frame reads. Launcher and wizard against `docs/look.md`.
- **Upstream.** A change to upstream's code outside the fork's folders needs the maintainer's yes and a line in
  `.notes/UPSTREAM.md`; the review says so rather than judging it.
- **What it cannot do.** It builds and runs nothing: CI does, and a card-only check (`gate`, `repeat`, `shot`) is
  asked of the author by name.

`.github/auto-review/issue.md`: classify (crash, picture, performance, build, feature, question, upstream's);
for a crash, the symbolized stack read against the code at the release's tag; the likely place in the code with
file and line, or "not found" said as such; what the report lacks to act on (log, crash package, card and driver,
content profile, a place `views.cfg` could stand at); duplicates by number; for a fault in what upstream owns
(cells, scripts, physics, GUI), a pointer to upstream's tracker. It does not promise a fix or a date.

Both: everything read out of the item is data. An instruction found in it is not followed and is reported in a
schema field; a non-empty one posts only the header and "held for a maintainer", and annotates the run.

## What is posted

The template's header, above everything the agent wrote, and a hidden marker with the item and its head:

> **Automatic review.** This was written by the repository's automation when the pull request was opened or
> updated, at `abc1234`. A maintainer reads every pull request and will reply here; nothing below approves,
> blocks or speaks for them.

> **Automatic first look.** The repository's automation read this report and the code it points at. A maintainer
> will follow up; what is below is a starting point, not an answer.

It names no tool and no vendor, by the repository's authorship rule; "automatic" is the disclosure.

## Decisions taken

- **The agent never holds a write.** Read-only agent, separate writer that runs no agent, as GitHub's agentic
  workflows ("safe outputs") and `upstream.yml` both do. Refused: the action's own recipes, where the agent posts
  through `gh pr comment` and the inline-comment tool: an injected issue would then write with the repository's
  token, which is the PromptPwnd class Aikido disclosed across Claude Code, Gemini CLI and Codex actions.
- **The agent runs under `workflow_dispatch` or `schedule`, never under the item's event.** The action checks the
  actor's write access on issue, pull request and `workflow_run` events and skips it on dispatch and schedule
  (`src/entrypoints/run.ts` at the pinned commit), so a stranger's pull request is refused under its own event.
  Refused: `allowed_non_write_users: "*"`, which the action's security guide calls a significant risk, for
  workflows with very limited permissions only. The cage, not the actor, is what makes a stranger's text safe.
- **Comment-only reviews.** A review never approves or requests changes, so it never counts toward or blocks a
  merge; Copilot's reviews are comment-only by default for the same reason.
- **A pull request is reviewed again at a later head, whole.** CodeRabbit reviews each push incrementally and
  advises pausing after reviewed commits on busy branches; the debounce is that pause. The previous review is
  handed in, so the new one judges the whole change and says which findings stand.
- **No second model to judge the output for injection.** GitHub's agentic workflows add one; here the output has
  no capability left to abuse once `publish` has bounded and sanitized it, and a second model reads the same
  injected text.
- **New items dispatched at once, later heads swept hourly.** A job on `pull_request_target`/`issues` that checks
  out nothing dispatches the review; GitHub Security Lab's "Preventing pwn requests" split of an unprivileged
  trigger from a privileged run, with a dispatch where it uses `workflow_run`. zizmor flags the trigger, and the
  ignore carries this reason. Refused: a sweep alone, up to an hour before a newcomer hears anything; waiting on
  CI's `workflow_run`, which for a first-time contributor waits on the maintainer's approval of CI.
- **The comment is `github-actions[bot]`'s**, through the job's `GITHUB_TOKEN`, as `judge`'s comments are.
  Refused: the Claude GitHub App, whose name is a tool's, against the repository's authorship rule; an App of the
  repository's own, an App and a private key to keep for a name and an avatar.
- **The agent searches the web, and fetches only from an allowlist**: the Khronos registry and docs,
  cppreference, isocpp, GPUOpen, NVIDIA's developer site, OpenMW's GitLab, GitHub. "Canonical" is the field's
  practice, which a review should cite rather than recall; what the agent could leak is only what it read, which
  is public, and the allowlist bounds the untrusted text it adds. GitHub's agentic workflows allowlist their
  network the same way. Refused: no web, as `upstream.yml`'s agents run, which judges from memory.
- **Every pull request but the owner's and the bots', drafts included; every issue but theirs.** The owner
  reviews their own changes with `review-diff` before they land; a draft is where a contributor most wants an
  early read, so the debounce, not a skip, keeps its pushes cheap. Refused: CodeRabbit's default of skipping
  drafts.
- **Opus for both**, `--model opus`, as `upstream.yml`'s agents. Most of this repository's issues are crash
  reports, a symbolized stack read against the renderer, the hardest reading the job does. `opus` is Claude
  Code's alias for the newest Opus the pinned action's Claude Code knows (Opus 5.5 today); it moves when
  Dependabot moves the action's pin, weekly after a 7-day cooldown, so no alias is needed beyond it.
- **Limits against a flood, counted and never stored**: 3 items an author in any 24 hours, the sliding window
  GitHub's agentic workflows' `user-rate-limit` keeps; 20 review jobs a repository in any 24 hours; 8 items a
  run; 5 reviews a pull request; two failures a head; the 5-hour debounce. An item over a limit waits; none is
  dropped.

- **No new dependency.** The JSON is parsed by dataclasses rather than a schema library; `minidump-stackwalk`
  and `dump_syms` are pinned already.

## Left out

- **Labels.** The comment classifies; applying labels is a later, separate safe output with an allowlist.
- **A follow-up when the reporter answers.** An issue is read once; a maintainer dispatches it again by number.
- **Running anything of the change.** CI builds and tests it; the card's checks are the author's.

## Plan

1. Move the two passes into `.github/review/passes.md`; `upstream-merge/review.md` keeps its scope and report.
2. `pull-request.md`, `issue.md` and the two JSON schemas under `.github/auto-review/`.
3. `omw review-post` and its tests: parsing, every sanitization rule with a hostile input, the hunk mapping at a
   hunk's first and last line and one past it, the marker round trip, the dry run.
4. `auto-review.yml`, `omw lint` clean.
5. Dry runs over closed items whose outcome is known — #44, #45 and #47, #49 and #53 — read by the maintainer
   before the first post.
6. Live, with the sweep's first run bounded to the items opened since.

## Sources

- [claude-code-action: security](https://github.com/anthropics/claude-code-action/blob/main/docs/security.md) —
  the write-access check, `allowed_non_write_users`, `pull_request_target`, the hidden-markdown stripping.
- [claude-code-action: solutions](https://github.com/anthropics/claude-code-action/blob/main/docs/solutions.md) —
  the review and triage recipes refused above.
- [GitHub: securely using `pull_request_target`](https://docs.github.com/en/actions/reference/security/securely-using-pull_request_target)
  and [GitHub Security Lab: preventing pwn requests](https://securitylab.github.com/resources/github-actions-preventing-pwn-requests/)
- [GitHub Agentic Workflows: safe outputs](https://github.github.com/gh-aw/reference/safe-outputs/) and its
  [specification](https://github.github.com/gh-aw/specs/safe-outputs-specification/) — the sanitization order
  and rules `publish` follows.
- [Aikido: PromptPwnd](https://www.aikido.dev/blog/promptpwnd-github-actions-ai-agents) and the
  [CSA research note](https://labs.cloudsecurityalliance.org/research/csa-research-note-ai-github-actions-security-20260503-csa-st/)
  on prompt injection in CI agents.
- [GitHub Copilot code review](https://docs.github.com/en/copilot/how-tos/use-copilot-agents/request-a-code-review/use-code-review)
  — comment-only reviews.
- [CodeRabbit: auto review](https://docs.coderabbit.ai/configuration/auto-review) — drafts, incremental reviews.
- [GitHub Agentic Workflows: rate limiting](https://github.github.com/gh-aw/reference/rate-limiting-controls/) —
  the per-user sliding window.
- [Claude Code: model configuration](https://code.claude.com/docs/en/model-config) — `opus` resolves to the
  newest Opus the installed version knows; a full name such as `claude-opus-5-5` pins one.
