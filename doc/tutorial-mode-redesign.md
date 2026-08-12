# Interactive Tutorial Mode -- Redesign Digest

> Status: implementation in progress.  This document records why the interactive
> tutorial mode changed from a recall-gated quiz, first to a guided walkthrough,
> and then to the coach-mark tour it is now.  It supersedes the verb model
> described in `tutorial-mode-design.md`; that document remains accurate for the
> reconnaissance, the validation approach, and the constraints of the
> surrounding code.
>
> **Read section 3 first if you only want the current model.**  Sections 1 and 2
> record the intermediate design and why it was abandoned; they are kept because
> the reasoning still applies, but D1, D3 and D4 were each revised afterwards.

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

## 1. What was specified for the walkthrough

1. A widget that **drops in from the top right**, showing the tutorial's graphics and the
   commands attached to them.
2. Commands presented **line by line**, each with an explanation of what it does and what
   different syntax choices would do.
3. The user presses **Insert or Tab** to place the command into the script.
4. **Remind at least three times** what a given command or piece of syntax does, then stop.
5. **Real LAMMPS runs** through the existing machinery.
6. The choice of step kinds was left to implementer discretion, and is recorded below.

---

## 2. Decisions of the walkthrough model

### D1. Three step kinds replace the seven verbs  *(partly revised -- see D10)*

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

### D3. The slide-in panel, and its one risk  *(revised -- see D7)*

**It would be the first animation in this project.** `grep` for `QPropertyAnimation`,
`QVariantAnimation`, `QGraphicsOpacityEffect`, `QTimeLine` across `src/` returns nothing.
That is a real, if small, upstream-acceptance risk, so:

- One `QPropertyAnimation` on `geometry`, about 180 ms, isolated inside the panel class so
  it can be deleted without touching anything else.
- It slides in from the top right on a step change and then **settles as the right-hand
  panel** -- the animation is the transition, not the residence. A permanent floating
  overlay would cover the editor the user is supposed to be building.
- Honour a "reduce motion" preference by simply skipping the animation.

### D4. Insert key: Tab, but scoped to the panel  *(revised -- see D9)*

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

### D6. Running needs exactly one new signal  *(implemented; consumer revised by D8)*

`LammpsGui::runBuffer()` is already a **public slot**, so a panel can start a run. But
completion is unobservable: `runDone()` is `protected` and is not a signal
(`src/lammpsgui.h:276`). The minimal addition:

```cpp
signals:
    void runFinished(bool success);   // emitted at the end of LammpsGui::runDone()
```

No access levels move. (`friend class TutorialWizard;` at `src/lammpsgui.h:92` is the
existing precedent for the alternative, but a signal is cleaner and smaller.)

The consumer changed with the coach-mark model: rather than an `EXPERIMENT` step
rewriting bound tokens and running, the tour points at the Run button, waits for the user
to press it, and on `runFinished` moves the callout to the chart and then the snapshot.
The signal itself is unchanged.  It is emitted at the end of `runDone()`, after the
window is back in its resting state so a listener cannot race the cleanup, and a dry run
is deliberately not announced: nothing waiting on results treats an input check as a run.
A failed run does not advance the tour either -- the user is left looking at the error,
which is where the interesting thing just happened.

**Honest limitation:** this cannot be verified end to end on this machine. There is no
`liblammps` here, so I can build it and test everything up to the run, but not the run.

---

## What the walkthrough model cost

- **Content:** 32 steps convert. 13 already carry the command in `editor.skeleton` and 17
  carry it in `reveal`, so most conversions are mechanical rather than rewrites.
- **Code retired:** the verb-mix warning, the non-skippable/reveal rule, and the
  `ExactTokens`/`TokenPattern`/hole-matching paths in what is now `tutorialtext.cpp`.  The
  plan was to **keep** `canonicalWords()`, `parseLammpsNumber()`, `hasSubstitution()` and choice
  evaluation -- they are still needed for optional predictions and for checking an
  experiment's observation -- and retire the unused paths only once no content uses them,
  rather than deleting speculatively.
- **Schema:** `schema_version` goes to **2**. This is not additive: step kinds change
  meaning, so the loader must reject v1 files rather than misread them.

---

## 3. The coach-mark tour (current model)

Reviewed in use, the walkthrough panel was still wrong: it looked like it was floating on
nothing, sat awkwardly, and could be dragged around.  The intended shape is a **pale
callout that anchors itself to real parts of the GUI and walks the user around them**.
Code lives in the editor, highlighted, committed with Tab; the callout carries prose and
nothing else.

### D7. The panel was a window pretending to be a panel  *(supersedes D3)*

`TutorialView` called `applyWindowFlags()` (`src/helpers.cpp:825`), which sets
`Qt::CustomizeWindowHint`.  That makes Qt treat the widget as its own top-level window
with its own OS geometry and title bar, despite it having the main window as parent.
That single call is why it had no background and could be dragged.

It is now a plain child widget with a painted background, so there is nothing to drag.
The slide-in animation went with it: the callout moves between anchors instead, and
animating a child widget's geometry on every step change would be noise rather than
information.  This project still contains no animation.

### D8. Anchors resolve live, never from a cached pointer

Each step declares an anchor (`editor`, `run`, `chart`, `image`, `log`, `none`), resolved
to a widget when the step opens:

- **Run** is a real `QPushButton` in the status bar, not only a menu action, so it can be
  ringed.  It gained an object name (`Cfg::RUN_BUTTON_NAME`) so the tour can find it
  without the main window handing out a pointer.
- **Chart, image and log** resolve through `WindowLayout::view(ViewSlot)`.  This matters:
  `imagewindow` is deleted and rebuilt on *every* render (`lammpsgui.cpp:2632`), so a
  cached pointer would dangle.  `WindowLayout` already watches its views for destruction
  and never hands out a stale one, so reusing it is both safer and less code.

Two widgets implement the effect: `TutorialSpotlight`, a transparent layer spanning the
main window carrying `Qt::WA_TransparentForMouseEvents` so it never swallows a click, and
`TutorialCoach`, the callout, as its child.  The spotlight rings rather than dims: dimming
the rest of the window would hide the script the user is reading.

### D9. The pending line, and Tab  *(supersedes D4)*

The callout shows **no code at all**.  A step offers its command to the editor as a
*pending* line: written into the buffer, painted in the tutorial's highlight colour, not
yet accepted.  Tab commits it; moving on withdraws it and restores the buffer exactly.

This required claiming Tab inside `CodeEditor` after all, which D4 had tried to avoid.
The guard is narrow: Tab commits **only** while a line is pending *and* the cursor is on
it.  With nothing pending it still calls `reformatCurrentLine()`, and Shift+Tab still
runs completion, so the editor behaves exactly as before outside a tutorial.  That
condition is the whole reason this is acceptable under the "do not break existing GUI
behavior" rule, and it is covered by a test that asserts reformat still happens.

Painting follows the existing idiom in `CodeEditor::paintEvent` (the rounded frames around
overridden index variables), filling the block rectangle *before* the base class paints so
text and syntax highlighting draw on top.

### D10. What this leaves vestigial  *(revises D1)*

The callout has no controls beyond Back and Next, so the `EXPERIMENT` step kind's
parameter widgets and its optional prediction no longer have any UI behind them.
`TutorialParam`, `TutorialPrediction`, `TutorialOption` and `ParamKind` are still parsed
and validated but consumed by nothing.

**They are scheduled for deletion with the schema v3 content rewrite**, not kept.  The
user changes values in the editor, guided by the tour, and runs from the Run button the
tour points at.  Until that rewrite lands, the shipped content is still schema v2 and does
not exercise the tour properly.

## Files

| File | Change |
|---|---|
| `src/tutorialcontent.{cpp,h}` | Step kinds, `concepts`, `anchor`, `call_to_action`, `open_file` |
| `src/tutorialengine.{cpp,h}` | Step cursor, per-command offers, concept exposure counters |
| `src/tutorialcoach.{cpp,h}` | The spotlight layer and the callout, plus the fixed palette |
| `src/tutorialview.{cpp,h}` | Drives the coach mark: anchor resolution and placement |
| `src/tutorialtext.{cpp,h}` | Tokenizing, number parsing, argument rewriting |
| `src/codeeditor.{cpp,h}` | Pending line, its painting, and the guarded Tab branch |
| `src/lammpsgui.{cpp,h}` | Anchor resolver, Run button object name, tutorial file opening |
| `resources/tutorials/lj-fluid.json` | The authored content |

The coach palette lives in `tutorialcoach.h` rather than `constants.h` on purpose:
`constants.h` is included by Qt Core only translation units, and `QColor` would drag QtGui
in with it.

## Verification

- `cmake --build build` clean, and the Sphinx build free of new warnings.
- Unit tests: content loader, engine cursor and reminder budget, text helpers.  The
  pending-line cycle is covered by a harness linked against the built application objects,
  since `CodeEditor` depends on `LammpsGui` and cannot be linked alone.
- Manual: the callout cannot be dragged and has a solid background; Tab on the pending
  line commits it while Tab elsewhere still reformats; the Run button is ringed and the
  callout sits beside it.
- Screenshots captured with `QWidget::grab()` rather than GUI automation, so they are
  reproducible.
- **Not verifiable on a machine without `liblammps`:** the actual run, and everything the
  tour does after it.

## Still open

1. **Schema v3 and the content.**  Delete the vestigial parameter and prediction types,
   and rewrite Tutorial 1 to cover both halves of section 3.1 -- the second half
   (cylinder regions, `write_data`, restarting from a data file, groups, `delete_atoms`)
   is not covered at all yet, and works across three input files.
2. **Figures** -- the article's figures are deliberately not shipped (see D2).  If the
   licensing of the source material is ever settled, using one becomes a content edit
   rather than a code change.
3. **Content licensing** -- unresolved, and still blocks release.  See
   `tutorial-mode-design.md`.
