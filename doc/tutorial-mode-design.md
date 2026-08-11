# Interactive Tutorial Mode — Design Digest

> Status: Phase 0 (reconnaissance) complete, no implementation started. This
> document is the durable "memory" for the interactive tutorial mode feature.
> Update it as decisions firm up. Like `ai-assistant-design.md`, it is design
> memory and is deliberately **not** part of the Sphinx `toctree`.

## Project context

- **Goal:** an in-application, data-driven, step-by-step interactive tutorial
  mode, where all instructional content lives in the GUI and the user is
  required to *do* things (type, fix, predict, tune) rather than copy-paste
  from an external PDF.
- **Current state it replaces:** the existing Tutorials wizard downloads
  template input files into a user-chosen folder and expects the user to follow
  along in the separately published *LAMMPS Tutorials* series.
- **Reference model:** a video game tutorial. Short teach beat -> one small
  verified action -> immediate visible payoff, looping every 60-90 seconds.
  Including the part where a good tutorial lets you fail safely in a controlled
  room before you meet that failure in the wild.
- **Additive, not a replacement.** The existing Tutorials wizard stays.

### Non-goals

- Not a tour of the GUI's own chrome ("this is the Run button").
- Not a replacement for the LAMMPS documentation. Link to it, do not restate it.
- Not a gamification layer. No points, streaks, badges, or XP. The reward is a
  working simulation and a visual.
- Not a network service. Everything must work offline once content is present.

### The design constraint that matters most

The user is a **volunteer adult learner** who opened this on their own
initiative. Every interaction must be justified by "this teaches something,"
never by "this proves they are paying attention."

- **Forbidden:** minimum time-on-step timers, artificially disabled Next
  buttons, forced retyping of long blocks, progress that resets, modal nagging,
  any lockout after wrong answers.
- **Required:** a visible **Skip step** control on every step, an **Expert
  mode** that drops to checkpoints only, and **Reveal answer** after 3 wrong
  attempts (with an explanation, not just the string).

Engagement is enforced by *task shape* -- a question you cannot guess past, a
broken command you must actually read -- never by a watchdog.

---

## 1. What already exists and must be reused

This is the most valuable result of the reconnaissance: a large fraction of the
originally proposed engine is already implemented, unit-tested, and shipping.
Reimplementing any of it would be a regression.

| Existing component | What it already provides |
|---|---|
| `src/lammpssyntax.{cpp,h}` | `tokenizeLine()` -- a faithful reimplementation of LAMMPS `input.cpp` lexing (comments, single/double/triple quotes, `&` continuations including mid-word, quoted strings spanning continuations), with per-token `start/length/type/argIndex/isNumber/hasSubst/fragment`. Plus `findVarRefs()`, `argumentTexts()`, `isNumberWord()`. |
| `class LammpsSyntax` | Per-category name sets (`StyleCat`: Command, Fix, Compute, Dump, Atom, Pair, Bond, Angle, Dihedral, Improper, Kspace, Region, Integrate, Minimize, Variable, Units, Extra, Color, ImageKw) **and** a per-argument-position role table (`CommandSpec` / `ArgSpec` / `ArgRole`), loaded from `resources/command_specs.table`. |
| `class InputScanner` | Whole-buffer assembly of logical commands with line numbers and tokenizer diagnostics. |
| `src/syntaxcheck.{cpp,h}` | `SyntaxChecker::check(buffer, presetVariables, cwd)` -> `QList<LintIssue>`; a deterministic linter over the registry, explicitly engineered for zero false positives. |
| `LammpsGui::populateSyntax()` (`lammpsgui.cpp:3305`) | Fills the registry from `styleCount()`/`styleName()`, therefore **package-aware by construction**. |
| `resources/help_index.table` + `CodeEditor::findHelp()` (`codeeditor.h:427`) | Resolves the command *and* the style under the cursor to a documentation page. |
| `CodeEditor::setHighlight(int, bool)` / `setCursor(int)` | Gutter line marking (green/red fill, `>N<` bracketing) drawn by `lineNumberAreaPaintEvent()`. |
| `CodeEditor::setVariableOverrides()` + `paintEvent()` | A working precedent for a *second*, independent per-line decoration channel. |
| `PlotWidget::addSeries(const PlotSeries *)` | Generic caller-owned XY line/scatter rendering. Nothing is hardwired to thermo output. |
| `WindowLayout` / `ViewSlot` | The sole authority on how an output view is presented, under `LayoutMode::Windows` or `LayoutMode::Docked`. |

**Consequence for the anatomy strip.** Token roles, style validity, package
awareness, and doc links are all solved. The genuinely missing piece is
**human-readable per-token descriptions and unit annotations**, which exist
nowhere in the repository and must be authored -- proposed as a new
`resources/token_help.table`, in the same plain-text, comment-headed style as
the existing tables.

---

## 2. Feasibility probes

> **Limitation recorded honestly:** no `liblammps` shared library exists on the
> development machine used for this reconnaissance, there is no `build/`
> directory, and the project has never been built there. **P1-P3 below are
> source-derived, not empirical.** P2 in particular is unmeasured, and it gates
> the validation design.

### P1 -- Two concurrent LAMMPS instances: structurally yes, three shared globals

`lammps_handle` and `plugin_handle` are plain instance members
(`lammpswrapper.h:350-352`); there is no static or global state in the class.
`LammpsWrapper::open()` guards with `if (lammps_handle) return;`, which is
**per-object**, not a process singleton. `liblammpsplugin_load()` mallocs a
fresh dispatch struct per call, `lammps_open_no_mpi` is exposed
(`plugin/liblammpsplugin.h:129`), and multiple instances per process is a
documented, supported LAMMPS library pattern.

Today exactly one wrapper exists: `LammpsWrapper lammps;`, a by-value member of
`LammpsGui` (`lammpsgui.h:673`); every other class holds a pointer to it.

Three genuinely process-global hazards constrain a second instance:

1. **stdout** -- `StdCapture` does `dup2(m_pipe[WRITE], fileno(stdout))`
   (`stdcapture.cpp:123`). Anything in the process writing to stdout during a
   capture is intermixed into the user's log.
2. **working directory** -- see P5.
3. **`finalize()`** (`lammpswrapper.cpp:392-403`) calls `lammps_mpi_finalize()`,
   `lammps_kokkos_finalize()`, and `lammps_python_finalize()`, all
   process-global teardown. A hidden instance must *never* call it; only the
   last survivor may.

### P2 -- Cost of instance creation: UNKNOWN

Cannot be answered without a LAMMPS build. `lammps_open_no_mpi()` is normally
milliseconds, but a plugin-mode first `dlopen` of a full-package `liblammps` is
far heavier. **This must be measured before any rebuild-per-validation design is
committed to.**

### P3 -- Error recovery: the GUI already depends on it

`lastErrorMessage()` both reads **and clears** the pending error, and the GUI
keeps using the same handle -- it never closes and reopens after a command
error. The decisive precedent is the KSpace probe at `lammpsgui.cpp:1522-1541`:

```cpp
// ... Probe with a silenced no-op run and let LAMMPS be the oracle:
// ... lastErrorMessage() both reads and clears the error, so the probe leaves
// LAMMPS clean.
QString kspaceerr;
{
    StdoutSilencer guard;
    lammps.command("run 0 post no");
    kspaceerr = lammps.lastErrorMessage();
}
if (kspaceerr.contains("requires a KSpace style")) { ... }
```

This is already the verify-and-repair oracle pattern, shipping, on the primary
instance, under `StdoutSilencer`. Individual error classes (bad style name,
wrong argument count, out-of-range numeric, wrong command order) were not
tested, as no library was available.

### P4 -- Style enumeration: yes, and already wired

`lammps_style_count` / `lammps_style_name`
(`plugin/liblammpsplugin.h:240-241`) -> `LammpsWrapper::styleCount()/styleName()`
-> `LammpsGui::populateSyntax()`, covering 13 categories plus commands.
Per-handle, therefore package-aware. `lammps_config_has_package` (`:231`) is
available for a `requires_packages` content field.

### P5 -- Filesystem sandboxing: NO

The working directory is set with **`QDir::setCurrent()`**, a process-wide
`chdir`: `lammpsgui.cpp:889` (constructor), `:1290` (`openFile`), `:1582`
(`writeFile`). LAMMPS's C API has no per-instance directory concept either.

**A same-process validation instance cannot have its own working directory.**
Confining it to a `QTemporaryDir` is not implementable in-process. The options
are: `chdir` the whole process around each validation (racy against a live run),
rely on a deny-list so nothing writes at all, or move the validator out of
process entirely (which needs a LAMMPS *binary*, not guaranteed in plugin mode).

---

## 3. Settled decisions

| Topic | Decision | Rationale |
|---|---|---|
| **Content format** | **JSON**, parsed with `QJsonDocument`. Authors write YAML in the content repository and convert to JSON at authoring time -- not at build time, not at runtime. | Zero new dependencies; already linked; works unchanged in the flatpak, macOS bundle, and MinGW-cross NSIS builds. Making yaml-cpp a *runtime GUI* dependency would have to be threaded through all three. |
| **Validation engine** | **Probe the primary LAMMPS instance**, not a separate shadow instance. | P5 rules out the sandboxed shadow, and the primary-instance probe is already a shipping, proven pattern (P3). Also sidesteps the unmeasured P2. |
| **Starting buffer** | **Stripped skeleton** -- structure and comments preserved, commands removed, so the user fills it in. | The existing `setupTutorial()` downloads the template and `openFile()`s it. If that template contains the commands, "the literal string must never be displayed" is violated before step 1. |
| **Anatomy strip metadata** | Runtime + shipped tables: roles from `LammpsSyntax`/`command_specs.table`, validity and package awareness from `styleCount`/`styleName`, doc links from `help_index.table`, and a new authored `token_help.table` for descriptions and units. | Scraping the documentation would rot. Everything except the prose already exists. |
| **Plot widget** | Reuse **`PlotWidget`** directly. | It is the low-level generic renderer with caller-owned series -- the right granularity for a panel. `ChartWindow`/`ChartViewer` carry thermo-session machinery that a concept plot does not want. |
| **Menu integration** | Extend the existing wizard: an "Interactive" checkbox on the `tutorialDirectory()` page (object name `t_interactive`, matching the `t_*` convention) plus a parameter on `setupTutorial()`. | Keeps template download and tutorial content coupled. See the delivery caveat in section 6. |
| **Feature flag** | **No CMake option.** Gate at runtime on content availability. | The project has only four, deliberately coarse options; a fifth doubles the matrix that CodeQL and CI compile. Hiding the menu entry when no content resolves keeps the code on the compiled path where the compiler and CodeQL can see it. |

---

## 4. Validation design

Replaces the originally proposed separate sandboxed instance.

- Validate on the **primary** `LammpsWrapper`, guarded by `StdoutSilencer`, and
  only when `!lammps.isRunning()`.
- "Rebuild from the verified prefix" is `clear` + replay of the verified prefix,
  **not** close/open. Far cheaper, and independent of P2.
- Keep a **deny-list** -- `run`, `minimize`, `write_*`, `shell`, `python`,
  `dump`. With no `run`, essentially nothing writes files, which substitutes for
  the impossible working-directory sandbox.
- Keep a hard wall-clock timeout per validation (start at 2 s); on timeout, kill
  and rebuild.
- Validation fires **on submit only** -- never per keystroke.
- **Error messages are content.** When a command is rejected, show the *actual*
  LAMMPS error string verbatim in a styled block, then add the tutorial's
  plain-language gloss beneath it. Users will meet these strings for the rest of
  their career; teaching them to read one is a feature.

**Accepted cost:** validation replays commands on the same instance the user's
own run uses, so tutorial validation and a user run cannot interleave. Since
validation fires only on submit, this serialization is acceptable.

**Never string-compare a whole command.** Tokenize, compare positionally, and
report *which argument* is wrong. "Argument 3 should be a cutoff distance; you
gave a style name" is a lesson. "Incorrect." is not.

---

## 5. Content model

Content is **data, not code**: adding a tutorial must never require recompiling
the GUI.

### Verbs (closed, small set; each step is exactly one verb)

| Verb | User action | Gate |
|---|---|---|
| `READ` | Nothing | Next button. Use sparingly, never two in a row. |
| `TYPE` | Types a command from a *described intent* | Token match + parse. The literal string is never displayed. |
| `FILL` | Completes a skeleton with `___` holes | Per-hole rule + parse. The workhorse verb. |
| `FIX` | Repairs a deliberately broken command | Parses clean. Highest pedagogical value. |
| `PREDICT` | Answers before running | Answer submitted; both branches teach. Cannot be clicked past. |
| `TUNE` | Changes a parameter, re-runs, reports what changed | Run completes + observation matches. |
| `INSPECT` | Clicks each token to reveal meaning | All tokens visited. |

Target mix across a tutorial: roughly 25% TYPE, 20% FILL, 15% FIX, 20% PREDICT,
15% TUNE, 5% INSPECT. A draft that is 70% TYPE has regressed into transcription.

### Validator types

`exact_tokens`, `token_pattern`, `numeric_range`, `style_valid`, `parses_clean`,
`choice`, `observation`, `script_state`.

**Canonicalization before every comparison:** strip comments, collapse internal
whitespace, trim, case-normalize command and style names (leaving file paths and
variable names case-sensitive), treat `2.5` / `2.50` / `2.5e0` as equal.

### Content resolution order

1. **User override directory** (a preferences path) -- for authors iterating.
2. **Downloaded content** -- alongside the files the wizard already fetches.
3. **Bundled fallback** -- at least Tutorial 1 in the Qt resource system, so the
   feature is never a dead menu item on a fresh offline install.

Dropped from the schema as surface with no consumer: `estimated_minutes`,
`unlock_concepts`, and the concept map.

---

## 6. Open questions and objections

- **Mergeability target.** LAMMPS-GUI is now a standalone GPL-2.0-or-later
  project (`github.com/akohlmey/lammps-gui`, v3.0.99), not `tools/lammps-gui/`
  inside the LAMMPS source tree. An upstream PR targets *that* repository and
  its conventions.
- **The tutorial panel must be a `ViewSlot`, not a hardcoded `QDockWidget`.**
  Under `LayoutMode::Windows` -- a user preference -- output views are free
  top-level windows, not docks. `WindowLayout` is deliberately the only place
  that decides presentation; a hardcoded dock would be the first violation of
  that invariant.
- **Editor integration needs no new public methods** for the early phases.
  `setHighlight()` + `setCursor()` cover the target line, and skeleton insertion
  uses the inherited `QTextCursor` API. Only *multi-line* "verified line"
  marking needs an additive `CodeEditor` method (a `QSet<int>` plus a gutter
  paint branch), modeled on the existing `setVariableOverrides()` channel.
- **The help index needs extraction.** The command/style -> doc page maps
  (`cmdMap`, `pairMap`, `fixMap`, ...) are private members of `CodeEditor`
  (`codeeditor.h:457-464`). Reuse by the anatomy strip requires factoring them
  into a small shared `HelpIndex` -- an additive refactor.
- **The round-trip content test cannot exist in the current harness.** Tests are
  Linux-only, off by default, and **no test target links against LAMMPS**.
  Playing a tutorial with ideal answers and confirming the resulting script runs
  to completion requires a LAMMPS library in CI -- new infrastructure, not a
  test file. The schema and validator tests, which are the high-value ones, are
  pure logic and follow the `test_lammpssyntax` pattern exactly.
- **Content delivery is coupled to third-party repositories.** The elegant
  long-term delivery is to list `interactive.json` in a tutorial's `.manifest`,
  needing *zero* new download plumbing. But those manifests live in the
  `lammpstutorials` repositories, which this project does not control. Until
  that is coordinated, bundle Tutorial 1 in `lammpsgui.qrc`.
- **Content attribution and licensing is UNRESOLVED and blocks authoring.**
  Tutorial 1's material derives from the third-party `lammpstutorials`
  repositories; their license is not recorded anywhere in this repository, while
  the GUI is GPL-2.0-or-later. `TutorialCollection::author` is the existing
  attribution precedent. Every content file should carry a
  `source` / `license` / `attribution` triple, and terms must be confirmed with
  the tutorial authors before substantial content is written.

---

## 7. Phase plan

Each phase ends at a stop-gate; report, then wait.

| Phase | Deliverable | Gate |
|---|---|---|
| 0 | Reconnaissance report, objections, decisions | **Complete** -- this document |
| 1 | Content schema, loader, model classes, JSON validation, unit tests | Schema review |
| 2 | Tutorial panel as a `ViewSlot`, engine, step cursor, navigation, progress persistence, `READ`/`TYPE`/`FILL` with **syntactic** validation only | Runs end to end with a 5-step stub |
| 3 | Primary-instance probe validation, `parses_clean`, real error surfacing, deny-list, timeout | **P2 measured on a real LAMMPS build** |
| 4 | Anatomy strip, standalone-toggleable, plus `token_help.table` and the `HelpIndex` extraction | Demo on an arbitrary script |
| 5 | `FIX` and `PREDICT` verbs, hint ladder, skip and expert mode | Tutorial 1 Acts 0-2 playable; blocked on the attribution answer |
| 6 | Concept plot, `TUNE`, diff-on-tune, unit badge | Tutorial 1 complete and playable |
| 7 | Lattice view, second tutorial | Only if 0-6 are solid |

Ship phases 1-5 to real users before building 6-7.

### Reference content shape

Tutorial 1 (Lennard-Jones fluid) is the proof that the schema works. If the
schema cannot express it cleanly, the schema is wrong.

```
ACT 0 -- Orientation           (READ, INSPECT -- no gates)
ACT 1 -- Define the world      units, lattice, region, create_box/create_atoms
                               CHECKPOINT: run 0 succeeds, atoms exist
ACT 2 -- Define the physics    mass, pair_style, pair_coeff, cutoff
                               CHECKPOINT: energy computes, finite, negative
ACT 3 -- Make it move          velocity, fix nve, thermo
                               CHECKPOINT: run 250, live plot
ACT 4 -- Break it on purpose   timestep 0.005 -> 0.05, watch energy explode,
                               understand "lost atoms", then repair it
ACT 5 -- Free play             sandbox, three suggested experiments, no gates
```

**Act 4 is the pattern to replicate in every future tutorial:** controlled
failure before uncontrolled failure. It is the thing a PDF structurally cannot
do, and it is the entire justification for this feature.

## 8. Definition of done

1. A user with a fresh install, no internet, and no PDF can complete Tutorial 1
   end to end and finish with a working LJ fluid simulation they typed
   themselves.
2. No step can be completed by pressing Enter repeatedly.
3. Every step can be skipped, and skipping never breaks the following steps.
4. Adding a new tutorial requires **zero C++ changes** -- one content file.
5. Existing GUI behavior, including the current Tutorials wizard, is unchanged.
6. The build works with the feature present and with no content available.
