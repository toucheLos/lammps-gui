# Interactive Tutorial Mode -- Redesign Digest

> Status: approved, implementation in progress.  This document records why the
> interactive tutorial mode changed from a recall-gated model to a guided
> walkthrough with live experiments, and the decisions taken along the way.
> It supersedes the verb model described in `tutorial-mode-design.md`; that
> document remains accurate for the reconnaissance, the validation approach,
> and the constraints of the surrounding code.

## Context

Phase 2 shipped a working tutorial panel (branch `tutorial-mode-design`, through `aa250c8`):
content model, loader, evaluator, engine, and a panel where `TYPE`, `FILL` and `PREDICT`
genuinely judge answers.  Reviewed in use, it read as a quiz.

The intended model instead: the researcher **follows commands shown to them**, gets **small
instructional widgets as they progress**, and reaches **moments where they modify things,
run the whole experiment, and see the result of what they did**.

That reverses a stated non-negotiable of the original brief -- §4.3 said *"the literal
string must never be displayed; if it's on screen, it's copy-paste, and it teaches
nothing"* -- and it retires `PREDICT` as an anti-passivity gate. I think the reversal is
defensible: the payoff moves from *did the user recall the right token* to *look what
changed when they altered it*, which is the thing a PDF genuinely cannot do.  But it
invalidates
about 20 of the 32 authored steps and the verb-mix machinery, so it is worth being
explicit that this is a deliberate change of model rather than a tweak.

## What was specified

1. A widget that **drops in from the top right**, showing the tutorial's graphics and the
   commands attached to them.
2. Commands presented **line by line**, each with an explanation of what it does and what
   different syntax choices would do.
3. The user presses **Insert or Tab** to place the command into the script.
4. **Remind at least three times** what a given command or piece of syntax does, then stop.
5. **Real LAMMPS runs** through the existing machinery.
6. The choice of step kinds was left to implementer discretion, and is recorded below.

---

## Decisions

### D1. Three step kinds replace the seven verbs

| New kind | What happens | Absorbs |
|---|---|---|
| `SHOW` | The command is displayed line by line with per-token annotations. Insert (button or Tab) places it in the script. Nothing is judged. | `READ`, `TYPE`, `FILL`, `INSPECT` |
| `EXPERIMENT` | Named parameters appear as widgets bound to tokens in the script. The user changes them, runs the real simulation, and sees the result. | `TUNE`, `FIX` |
| `CHECKPOINT` | A milestone: run, confirm the system is in the expected state, narrate what was achieved. | the old checkpoint flag |

**Why `FIX` is absorbed rather than kept.** Its value was making the user read a real
LAMMPS error. In the new model that is *better* served by an experiment that actually
runs and actually fails -- they see the genuine failure in the Output window rather than a
hypothetical broken line in a panel. Act 6 ("break it on purpose") becomes two
experiments: raise the timestep and watch the energy diverge, then restore it.

**`PREDICT` survives, demoted.** It is retained *only* as an optional one-line question
attached to an `EXPERIMENT`, shown immediately before the Run button, freely dismissable
and never blocking. A result lands far harder against a stated expectation, and it costs
one click.  Making it optional is what stops it feeling like a quiz.  Removing it
entirely would be a single schema field.

### D2. Do not bundle the article's figures

The brief asked for "the graphics/photos from the tutorial".  They are deliberately not
shipped, on evidence rather than caution. Tutorial 1 has six figures:

| Figure | What it is | Verdict |
|---|---|---|
| 1, 3, 6 | Renders of the binary mixture | The GUI renders these itself via `dump image`; showing **the user's own system** is strictly better |
| 4 | Screenshot of the Charts and Output windows | The user produces this by running |
| 5 | Potential/kinetic energy plots | The user's own run produces these in `ChartWindow` |
| 2 | Screenshot of an older LAMMPS-GUI editor | Would ship **stale and misleading** |

So five of six are things the user generates live, and the sixth should not ship at all.
Bundling them also sharpens the unresolved licensing question -- figures are less
arguable than prose, and we still have no terms from the authors.

**Instead**, the figure slot shows, in priority order: a live snapshot of the user's own
system; a small set of **authored diagrams we own** (the LJ potential curve, the
five-block script anatomy); nothing. The schema carries a `figure` field regardless, so
if licensing later clears, using an article figure is a content edit and not a code
change.

### D3. The slide-in panel, and its one risk

**It would be the first animation in this project.** `grep` for `QPropertyAnimation`,
`QVariantAnimation`, `QGraphicsOpacityEffect`, `QTimeLine` across `src/` returns nothing.
That is a real, if small, upstream-acceptance risk, so:

- One `QPropertyAnimation` on `geometry`, about 180 ms, isolated inside the panel class so
  it can be deleted without touching anything else.
- It slides in from the top right on a step change and then **settles as the right-hand
  panel** -- the animation is the transition, not the residence. A permanent floating
  overlay would cover the editor the user is supposed to be building.
- Honour a "reduce motion" preference by simply skipping the animation.

### D4. Insert key: Tab, but scoped to the panel

**`Tab` is already taken in the editor.** `CodeEditor::keyPressEvent`
(`src/codeeditor.cpp:405-413`) binds `Qt::Key_Tab` to `reformatCurrentLine()` and
`Qt::Key_Backtab` to `runCompletion()`, and forwards both to the completer when its popup
is open. Rebinding it would break existing behaviour that `CLAUDE.md` explicitly protects.

So: Tab inserts **only while focus is in the tutorial panel**, handled in the panel's own
`keyPressEvent`. `CodeEditor` is not touched. A visible **Insert** button is the primary,
discoverable action and is bound to Enter as well.

### D5. Spaced repetition: a per-concept reminder budget

Content declares concepts (`{id, term, explain}`) and step annotations reference them by
id. The engine keeps a persisted per-concept exposure count; below the budget the
explanation is shown expanded, at or above it collapses to a chip the user can click.

`Cfg::CONCEPT_REMINDER_BUDGET = 3`, matching the "remind a minimum of three times"
requirement.

The real content supports this: across Tutorial 1, `timestep` appears 6 times, `region` 5,
`create_atoms` and `units` 4 each -- so later encounters genuinely do collapse.

Storage reuses the pattern already in `TutorialEngine::saveProgress()`
(`src/tutorialengine.cpp:244`): nested `QSettings` groups keyed by id, under
`Keys::GROUP_TUTORIAL`.

**One decision to flag:** counts are **per user and global across tutorials**, not per
tutorial. A concept learned in Tutorial 1 is not re-explained in Tutorial 2. That is the
point of a reminder budget, but it means a user returning after six months gets no
refresher, so the count must be resettable from Preferences.

### D6. Running the experiment needs exactly one new signal

`LammpsGui::runBuffer()` is already a **public slot**, so a panel can start a run. But
completion is unobservable: `runDone()` is `protected` and is not a signal
(`src/lammpsgui.h:276`). The minimal addition:

```cpp
signals:
    void runFinished(bool success);   // emitted at the end of LammpsGui::runDone()
```

No access levels move. (`friend class TutorialWizard;` at `src/lammpsgui.h:92` is the
existing precedent for the alternative, but a signal is cleaner and smaller.)

An `EXPERIMENT` step then: rewrites the bound tokens in the editor buffer, calls
`runBuffer()`, and on `runFinished` shows the result -- the existing `ChartWindow` and
`ImageViewer` already display it, so no new visualisation code.

**Honest limitation:** this cannot be verified end to end on this machine. There is no
`liblammps` here, so I can build it and test everything up to the run, but not the run.

---

## What this costs

- **Content:** 32 steps convert. 13 already carry the command in `editor.skeleton` and 17
  carry it in `reveal`, so most conversions are mechanical rather than rewrites.
- **Code retired:** the verb-mix warning, the non-skippable/reveal rule, and the
  `ExactTokens`/`TokenPattern`/hole-matching paths in `tutorialeval.cpp`. I propose to
  **keep** `canonicalWords()`, `parseLammpsNumber()`, `hasSubstitution()` and choice
  evaluation -- they are still needed for optional predictions and for checking an
  experiment's observation -- and retire the unused paths only once no content uses them,
  rather than deleting speculatively.
- **Schema:** `schema_version` goes to **2**. This is not additive: step kinds change
  meaning, so the loader must reject v1 files rather than misread them.

## Files

| File | Change |
|---|---|
| `src/tutorialcontent.{cpp,h}` | New step kinds, `concepts`, `figure`, `parameters`; schema v2 |
| `src/tutorialengine.{cpp,h}` | Concept exposure counters; experiment lifecycle |
| `src/tutorialview.{cpp,h}` | Rebuilt around annotated command display + Insert |
| `src/tutorialfigure.{cpp,h}` | New: scale-to-fit figure widget (none exists to reuse) |
| `src/lammpsgui.{cpp,h}` | `runFinished(bool)` signal; parameter rewriting |
| `src/constants.h` | `CONCEPT_REMINDER_BUDGET`, new `Keys` |
| `resources/tutorials/lj-fluid.json` | Rewritten to schema v2 |

## Verification

- `cmake --build build` clean; the app still launches and the existing Tutorials wizard is
  untouched.
- Unit tests: content loader rejects v1 files; concept counters increment once per step and
  collapse at 3; the engine's experiment lifecycle transitions correctly. Existing 112
  tutorial tests updated rather than deleted.
- Manual: walk Tutorial 1, confirm commands insert with both button and Tab, confirm Tab
  still reformats when focus is in the editor, confirm a concept stops being explained
  after its third appearance.
- Rendered screenshots of the panel, captured with `QWidget::grab()` rather than GUI
  automation so they are reproducible.
- **Not verifiable here:** the actual LAMMPS run. Needs `liblammps` on the target machine.

## Still open

1. **Figures** -- the article's figures are deliberately not shipped (see D2).  If the
   licensing of the source material is ever settled, using one becomes a content edit
   rather than a code change.
2. **Content licensing** -- unresolved, and still blocks release.  See
   `tutorial-mode-design.md`.
