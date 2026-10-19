# Agent instructions

## The project

Sonic Advance Remaster is a native fan remaster of Sonic Advance 1 & 2, built from the SAT-R decompilation (`SAT-R/sa2`): widescreen, high-refresh rendering, shader-based visual upgrades and 2.5D lighting on top of the decompiled game logic. The games run from the user's own ROM dumps; no ROM, BIOS file, save or extracted asset is ever part of this repository.

Phases, in order: **M0** the repository foundation and tracking system; **M1** both ports (Sonic Advance 1 Europe, Sonic Advance 2 USA) building and running from source with a documented capture path; **M2** the remaster renderer and lighting; **M3** packaging and the first release. Status: the port and the renderer are not committed yet; do not describe planned work as present.

It is an unofficial fan project, not affiliated with Sega. The project's own files are GPL-3.0-or-later (`LICENSE`). Upstream `SAT-R/sa2` has no license and a strict no-LLM contributions policy: work produced here, with or without AI assistance, stays in this repository and is never proposed upstream. Never vendor upstream sources, assets or diffs into commits; keep upstream checkouts outside the repository (for example under `/tmp` or a sibling folder).

Never describe something as working unless a recorded build or run establishes it. A compile log is not a frame; a frame is not the whole game.

## Layout

| Path | Contents |
| :--- | :--- |
| `.github/` | Work-item form, PR template and the metadata-only tracking workflow |
| `docs/TRACKING.md` | The issue → branch → milestone → PR rules |
| `docs/CAPTURE.md` | How frames and clips are captured from a build (M1) |
| `docs/images/` | Committed frames and clips, each captured from a recorded run |
| `tools/` | Project tooling (capture helpers, asset pipelines) as it lands |
| `.local/` | The user's local ROMs and scratch data; git ignores it and it is never committed |

## Commands

- `gh issue list --repo NahuelGenchi/sonic-advance-remaster --state open` lists the work.
- `git switch -c <type>/<issue>-<slug> origin/main` starts a work item.
- Source and port checks: the upstream build and the capture path are recorded in `docs/CAPTURE.md`; the port sources themselves are not in this repository.
- Capture review: `python3 tools/make_contact_sheet.py <capture-dir> <sheet-dir>` tiles a run into labeled sheets.

## Working well here

- Start from the active issue; read its scope, completion criteria and constraints before touching files.
- One branch per issue, one issue per PR; never commit to `main`.
- Frames, clips and screenshots in the repository come from recorded runs of a build, never from memory, imagination or an unrelated project. Say in the PR what produced each one.
- The ROM, BIOS files, saves and extracted assets stay out of every commit, log, screenshot and artifact.
- Record what was checked and what was not in the PR's delivery record.

## Tracking and delivery

Every change (code, fixes, enhancements, docs, research and repository operations) belongs to an issue with a milestone in `NahuelGenchi/sonic-advance-remaster`, reused or created before editing. Branches are `<type>/<issue>-<slug>` (`feat`, `fix`, `enh`, `docs`, `research`, `ops`), new from the latest `origin/main`, never reused, deleted after merge. Commits read `type: summary (#N)`. PRs follow `.github/pull_request_template.md`: a standalone `Closes #N` line, the issue's milestone, and `Refs #N` for unfinished parent work. See `CONTRIBUTING.md` and `docs/TRACKING.md`.

## Final report

Report the issue, the PR and what changed. Keep these separate: source checks, port or capture checks (exact commands and outcomes, or "none run"), and the review scope.
