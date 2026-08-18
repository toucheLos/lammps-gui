# Authoring an interactive tutorial

Heuristics for adding a guided tutorial to LAMMPS-GUI, written after building
Tutorial 1 of the soft-matter set (Lennard-Jones fluid, complete) and Tutorial 2
part 1 (carbon nanotube, unbreakable bonds).

Companion documents: `tutorial-mode-design.md` is the original reconnaissance,
`tutorial-mode-redesign.md` records why the model changed. Neither is in the
Sphinx toctree; nor is this one. The user-facing description lives in
`menus.rst` under *Interactive tutorial mode*.

**The one-sentence version.** A tutorial is a data file. Adding one should be a
content file plus one line in `interactiveContentFor()`; if you find yourself
writing C++ to express a tutorial, stop and ask whether the schema is missing
something general, or whether the content is fighting the model.

---

## 1. The objection this feature has to answer

A guided tour that writes the script for you can produce a user who pressed
Next thirty times, finished with a working input file, and learned nothing. The
objection is not hypothetical and it is not hostile -- it is the most serious
design constraint on the whole feature, and it comes from the people who wrote
the tutorials:

> "It encourages people to just click on the next field without paying much
> attention to the text or making edits themselves. In a way that is the same
> reason why I believe that people learn much less from YouTube videos."
>
> "... at least one more of the authors of the tutorial next to me prefers that
> people edit the input manually (even cut-n-paste is discouraged)."

Three things follow, and they should be treated as requirements rather than
opinions.

**Do not treat user enthusiasm as evidence.** Deslauriers et al. (PNAS 2019)
randomized students between active and passive instruction with the same
content and instructor: the active group **learned more and felt they had
learned less**. Passive, fluent, comfortable instruction reliably feels better
than it works. So "users would appreciate this feature and would claim it helps
them" -- which is almost certainly true of the tour -- is not an argument that
it teaches. Judge a tutorial by what the user can do afterwards, not by whether
the walkthrough felt smooth.

**Effort at the right moment is the mechanism, not friction to be removed.**
The generation effect is large and well replicated: producing an answer rather
than reading one is worth roughly half a standard deviation on later recall,
and **the advantage grows the less constrained the production is**. That gives a
direct ordering for the interaction verbs (section 4). It is also why
"type it rather than paste it" is a defensible instruction and not merely a
preference.

**The answer is fading, not friction everywhere.** Piling difficulty on a
novice backfires -- that is the expertise reversal effect, where guidance that
helps a beginner actively hurts someone who already has the schema, and the
reverse. The established structure is the *completion-problem* progression:
worked examples first, then problems with steps missing, then unaided problems,
with self-explanation prompts at the faded steps. Renkl and Atkinson's fading
studies show advantages on both near and far transfer.

So the tour's job is **not** to minimize typing. It is to be a worked example
early, a completion problem in the middle, and to get out of the way at the
end -- across a single tutorial, and across the set.

### What this means concretely

| Rule | Rationale |
|---|---|
| Every act ends with the user doing something that is not pressing Next. | A run, a prediction, an edit, an experiment. |
| **No more than four consecutive accept-only steps.** Count them. | Four is about where attention detaches; if an act needs more, it is really two acts. |
| A checkpoint asks **before** the user looks and answers **after**. | `predict` + `expect`. A stated guess is one the user can be wrong about; an unresolved one is worse than none. |
| Later tutorials fade. Tutorial 1 explains everything; Tutorial 5 hands over a familiar block with a sentence. | Expertise reversal. Grouping (`together`) is the main lever. |
| Never remove the user's ability to type. | The tour offers a line; accepting it is a convenience, not the only path. |

### Where the shipped content stands

Measured with `doc/tutorial-measure.py` -- the longest run of consecutive steps whose only
action is Next, and whether any act ends without asking for one:

| Tutorial | Longest accept-only run | Acts ending passively | Meets the budget |
|---|---|---|---|
| 3 -- polymer in water, part 1 | 2 | none | yes |
| 2 -- carbon nanotube, part 1 | 4 | none | yes |
| 1 -- Lennard-Jones fluid | 8 | 4 of 8 | **no** |

Tutorials 2 and 3 were built to this standard and are the reference. Tutorial 1 was
written before the standard existed and still fails it: it is a build-up
tutorial with long stretches of accept-and-continue and no predictions at all.
The repair is the same one that worked for Tutorial 2 -- run earlier, cap the
reading, put a question before each run -- and it has not been done yet. Do not
copy Tutorial 1's structure.

### On measuring whether it works

Do not ship a satisfaction survey and call it evidence. The cheap honest
measures are: whether the user can complete the closing experiments without
being told the answer, and whether they can do the next tutorial's equivalent
step unaided. If a checkpoint's `expect` is routinely surprising, the step
before it did not teach.

### On where this lives

The tour is currently **not** wanted in the upstream LAMMPS-GUI codebase, and
that is a maintenance judgment rather than a rejection of the idea. The
maintainer offered to advertise it from the documentation and to help package
it, provided it is named and labeled distinctly. Two consequences for authors:

- Keep tutorial content and its machinery separable -- content in
  `resources/tutorials/`, one registration line, no changes to the Tutorials
  menu or the wizard. The current design already satisfies this, and it is
  worth protecting.
- Do not let content depend on unreleased GUI behavior. A content file that
  needs a patched build cannot be distributed separately from it.

---

## 2. Two source styles, and which one you have

The two LiveCoMS tutorial articles take deliberately different approaches, and
the materials-science set says so outright:

> "Readers who are new to LAMMPS are advised to first work through Tutorial 1
> of the companion article, which introduces LAMMPS and LAMMPS-GUI by
> **building up an input file step by step**. The tutorials here take a
> complementary approach: each tutorial provides a **complete, fully functional
> input file that can be opened and run immediately and is then studied,
> discussed, and modified in detail**."

That is the worked-example progression in the source material already.

| Style | Source file arrives | Tour shape | Use for |
|---|---|---|---|
| **Build-up** | a skeleton of `#` headings (`initial.lmp`) | SHOW steps fill sections in order | a reader's first contact with LAMMPS; when the *structure* of an input file is the lesson |
| **Given-and-modify** | complete and runnable (`unbreakable.lmp`, matsci `initial.lmp`) | OBSERVE steps read it, then SHOW steps grow it from the middle, then modifications | when the structure is assumed and the *physics* or a specific technique is the lesson |
| **Partial** | the settings but no system (`water.lmp`) | OBSERVE steps read the settings, then SHOW steps append the rest | a multi-script tutorial where each file starts from the last one's saved state |

The partial style needs no placement machinery at all: with no headings and no
trailing line to insert above, the commands simply append. Tutorial 3 is three
scripts of this kind chained through restart files.

**Read the initial file before designing anything.** It decides the shape, and
you cannot guess which kind you have. Tutorial 1's is six comment headings;
Tutorial 2's is a complete script ending in `run 0 post no`. Section 5 covers
the placement machinery each needs.

**The given-and-modify style is not the easy option.** It only teaches if the
modification is real. The materials-science set does this well: successive
files differ by exactly one deliberate change -- `shifted.lmp` is `minimize.lmp`
with `origin 0.25 0.25 0.25` added, `energy.lmp` is Tutorial 1's input with the
lattice constant turned into a variable -- and the *comparison of the two
results* is the lesson. Copy that pattern: one change, run both, explain the
difference.

**Cross-checks are teaching moments.** The same article computes a lattice
constant twice by independent methods and calls the agreement "a reassuring
consistency check". A tutorial that arrives at the same number two ways teaches
more than one that arrives at it once.

---

## 3. Constructing a tutorial

Base heuristics, drawn from both articles.

**One novelty per tutorial.** The soft-matter article introduces each tutorial
by what is new in it -- bonds, then a long-range solver, then a fluid-solid
interface. Name that novelty in the opening step. Everything else in the
tutorial is scaffolding for it.

**Mirror the article's own subsections as acts.** They are already an
instructional sequence that someone thought about. Deviating is usually a sign
you have misread the order.

**Build on what came before, explicitly.** "Building on Tutorial 1, we make the
lattice constant an adjustable variable." A step that says which earlier
tutorial a command came from lets the reminder budget do its job and tells the
reader they are allowed to have forgotten.

**Runs are the punctuation.** Each act should end at a state the user can run,
and the tour should follow a successful run to whatever it produced. A tutorial
with one run at the end is a lecture.

**Keep runs short enough to sit through.** Both articles say this explicitly and
mark the slow ones. If a step's run takes more than a couple of minutes on a
laptop, say so in the `teach` text and give the user something to look at.

**Close with experiments.** Both articles end their tutorials with a list of
things to change. This is the unaided-problem stage of the progression and it
is the part most likely to be cut for time -- do not cut it. Two or three
items, each one number, each with a stated expectation.

**Say when a result is illustrative rather than converged.** The articles are
careful about this; a tutorial that quietly implies a 5000-step run is a
publishable measurement teaches a bad habit.

---

## 4. The fading ladder

Ranked by how much the user has to produce, which is the ranking the generation
effect gives. Higher is better learning and higher cost; the art is spending it
where it matters.

| Rung | Mechanism | Status | Use |
|---|---|---|---|
| 5 | Free modification -- change a value, run, explain the difference | prose + `tune` | closing experiments; the given-and-modify style |
| 4 | Write it yourself, with Hint and Show-me | `typed` + `hint` | a command already taught, late in a tutorial |
| 3 | Fill in the blank / completion | not built | the natural middle rung; **the remaining gap** |
| 2 | Multiple choice | not built | recognizing a wrong argument among plausible ones |
| 1 | Predict, then reveal | `predict` + `expect` | before every run and every conceptual crux; nearly free |
| 0 | Accept the offered line | default | first contact with a command |

**Rung 1 is the cheapest win.** Asking "what will happen to the energy?" before
Run costs one sentence and converts a passive observation into a testable
prediction. A step carries `predict` (the question, shown before) and `expect`
(the answer, revealed after); a `predict` without an `expect` is a load-time
error, because a question the user answers silently and is never marked on is
worse than no question.

The loop resolves however the user gets there. On a run-anchored step the
answer appears when the run finishes and the tour waits so it can be read. On
any other step the first Next reveals the answer and the second moves on --
including on a run step the user chose to skip, since nothing is ever locked
and a skipped question still has to be resolved.

Use it before every run, and at any conceptual crux where a reader is likely to
hold the wrong model. The best ones in Tutorial 2 are not about the interface:
"if you keep pulling two bonded atoms apart, does the force level off or keep
growing?" is the entire point of the tutorial, asked before the answer is given
away.

**Rung 3 is the missing middle.** Between "here is the line" and "write it from
memory" sits "here is the line with one argument blanked" -- exactly the
completion problem the fading literature is built on. When it is built, it
should be the default for the second half of any tutorial.

**Rung 4 ships with help.** A typed command must carry a `hint`, and the callout
offers **Hint** (the nudge) and **Show me** (writes the answer in as a *pending*
line, so the user still accepts it). A drill with nothing to fall back on is a
memory test the user did not sign up for, and a user who cannot get past it
abandons the tutorial rather than learning from it. The missing hint is a
load-time error.

Pick drills that are *variations* rather than recalls. Tutorial 2's three are
all mirrors of the line immediately above them -- `group cnt_bot region rbot`
after `group cnt_top region rtop`, `velocity cnt_bot set 0 0 0` after the same
for `cnt_top`, and the pulling velocity with the opposite sign. The user has a
model on screen; what is being practiced is the variation, which is the part
that carries the meaning.

**Per-tutorial targets.** Rough, and worth arguing with:

| Tutorial | Accept | Predict | Type / complete | Modify |
|---|---|---|---|---|
| 1 (first contact) | most steps | every run | none | closing experiments |
| 2-3 | grouped blocks | every run | 2-4 commands | one real modification mid-tutorial |
| 4+ | whole familiar sections in one group | every run | the tutorial's novel commands | the tutorial ends in an open question |

---

## 5. Before writing a line of content

Four checks. Every one of them has caught something.

1. **Read the article section end to end.** Not skimmed. The order of commands
   matters, and so does which of them replace earlier ones.

2. **Fetch the upstream manifest.** File names are not guessable and the
   manifest is downloaded at runtime, so it cannot be checked from this
   repository:

   ```
   https://raw.githubusercontent.com/lammpstutorials/lammpstutorials-article/main/files/tutorialN/.manifest
   ```

   Only top-level entries are downloaded for the user; anything under
   `solution/` is fetched only when they ask for solutions. A tour that opens a
   file must name one that reaches disk. The materials-science set has its own
   repository, `lammpstutorials/matsci-tutorials-inputs`, organized the same
   way.

3. **Fetch the initial input file.** It decides whether you are writing a
   build-up or a given-and-modify tour (section 2).

4. **Fetch `solution/<name>.lmp`.** Ground truth for command text and ordering
   -- prefer it over the article's prose, which sometimes shows a simplified
   line. It is also the target for the strongest verification available
   (section 10).

---

## 6. Invariants -- do not break these

Each was violated at some point, produced a bug a user noticed, and was fixed.

| Invariant | Why |
|---|---|
| **Nothing reaches the script that was not shown first.** | Commands appearing from nowhere was the single most-reported problem. The flow harness asserts it. |
| **The callout never shows code.** | Code belongs in the editor, highlighted, where the user will edit it. The callout says what it means. |
| **Back is the inverse of Next.** | It removes what the tour wrote and restores what the tour displaced. |
| **The user's edits are theirs.** | Never overwrite a line the user changed. `seedSkeleton()` refuses a non-empty buffer for this reason. |
| **A step explains; it does not gate.** | Nothing is locked. The recall-gated model was tried and retired -- see `tutorial-mode-redesign.md`. Difficulty is offered, not enforced. |
| **Warn, do not block.** | A missing package, a missing file: say so and carry on. |

---

## 7. Placing commands

Three placements, mutually exclusive answers to "where does this go?".

| Field | Behavior | Use when |
|---|---|---|
| `section` | files under a `#` heading, after anything already there | build-up style |
| `before` | inserts immediately above the named line | given-and-modify style |
| neither | appends at the end of the buffer | the tail of a script, after the structure exists |

`section` and `before` together is a load-time error. A `section` naming a
heading absent from the `skeleton` array is also an error -- otherwise the
commands would silently append at the end.

**The `skeleton` array must mirror the upstream file verbatim.** It seeds an
empty buffer, but when the wizard has opened the real file the tour matches
against the headings *that file* has. Drift means silent appending. For a
given-and-modify tutorial, `skeleton` is `[]`.

**`replaces` supersedes a line.** The tour otherwise only adds, so a step that
tells the user a command "takes the place of" an earlier one has to say so in
the data or the prose is a lie. `replaces` may name a line the tour wrote or one
a step names with `before` -- that is, one that came with the file.

**A replacing command should be its own step** when the step's other commands
are placed relative to the line being replaced. Otherwise the target disappears
part-way through the step and the rest fall back to appending. This bit
Tutorial 2's `run 5000`.

**`highlight` rings a line the tour did not write.** For the opening act of a
given-and-modify tutorial. Requires `anchor: "editor"`.

---

## 8. Writing the explanations

The explanation is the **only** time a command is explained. It used to be shown
repeatedly, and 47 of Tutorial 1's 51 explanations were written under that
assumption -- one line each, useless alone. They were all rewritten.

**On first use, write the full context**: what the command does, what each
argument means, and what a different value would do. That last part is what a
reference manual does not give you and is usually the most valuable sentence in
the step.

**On repeat use, stay terse.** A second `region` does not need re-teaching. The
budget is spent per *group* rather than per line, so two `pair_coeff` lines
travelling together each keep their meaning -- getting this wrong silently
dropped the mixing rules from Tutorial 1.

**Prefer a per-argument note to a longer paragraph.** `notes` carry `arg` (0 is
the command word), `note`, and `alternatives`. They are anchored to the token
they describe, which is where the reader is looking.

**Write the "why", not just the "what".** "`special_bonds lj 0.0 0.0 0.5`
excludes bonded neighbors" is a restatement. "Two bonded atoms are already held
at the right distance by the bond; letting them also feel the full
Lennard-Jones interaction describes the same thing twice" is a reason. Reasons
survive; restatements do not.

**Concepts fade.** A `concept` id draws on a shared reminder budget
(`Cfg::CONCEPT_REMINDER_BUDGET`) that persists across tutorials, so something
learned in Tutorial 1 is not re-taught in Tutorial 2. Declare a concept only if
a command or note references it -- an OBSERVE step has no commands and cannot
reference one, and an unreferenced concept is a load-time warning.

### Two traps worth naming

**What you write is not what LAMMPS prints.** `thermo_style custom step etotal
pe ke` produces columns headed `Step`, `TotEng`, `PotEng`, `KinEng`. The command
text uses the keywords; any step telling the user what to *look at* must use the
printed names. Three of Tutorial 1's observation steps got this wrong and sent
readers hunting for a column called `etotal`.

**American English, plain ASCII.** `CLAUDE.md` requires it for documentation,
and user-facing tutorial prose is documentation. Use `--` rather than an
em-dash. Greek letters and mathematical symbols are the exception, written as
Unicode directly.

### Mathematics

`renderText()` supports a small markdown subset -- `**bold**`, `*italic*`,
`` `code` `` -- plus `^{...}` and `_{...}` for superscript and subscript, which
`QTextBrowser` renders with no new dependency. Escaping happens first, so
content can never inject markup.

**Include only equations the article actually contains**, on the beat where they
belong. Tutorial 1 has four: the Lennard-Jones potential on `pair_style`, the
mixing rules with worked values on `pair_coeff 2 2`, `U + K = E` on the
molecular-dynamics `thermo_style`, and the `1/r^{12}` repulsion where it
explains a positive initial energy.

**Do not ship the article's figures.** Five of Tutorial 1's six are things the
user generates live -- the GUI renders their own system, which is strictly
better than a picture of someone else's -- and the sixth is a screenshot of an
older LAMMPS-GUI that would ship stale. A deliberate choice, **not** a licensing
constraint (section 9). Do not re-open it on licensing grounds.

---

## 9. Attribution and licensing

The `lammpstutorials-article` repository is **CC BY 4.0** -- stated in its
`LICENSE` and repeated in the header of every input file with the DOI to cite.
Adaptation with attribution is permitted. This was long recorded as unresolved
and blocking release; it is neither.

```json
"attribution": {
  "source": "... full citation including the DOI ...",
  "license": "CC BY 4.0",
  "credit": "Adapted from Tutorial N of the LAMMPS Tutorials by ..."
}
```

Adapt the prose; do not paste it. The tour explains commands one at a time in a
different medium, which is a different thing from reproducing an article.

---

## 10. Verifying a tutorial

In rough order of how much each is worth.

**1. Diff the tour's output against the article's solution.** Walk the tour on
Next alone and compare the resulting script with `solution/<name>.lmp`, through
`doc/tutorial-normalize.py`, which drops comments and blanks, joins `&`
continuations and collapses whitespace -- so two scripts that agree are giving
LAMMPS the same commands however they are laid out. Tutorials 2 and 3 both
match their solutions command for command. Nothing else comes close as evidence
that the content is right.

    diff <(python3 doc/tutorial-normalize.py solution/water.lmp) \
         <(python3 doc/tutorial-normalize.py tour-output.lmp)

**2. Lint expecting 0 errors and 0 warnings.** Warnings matter as much as errors
here: a schema key missing from its key set is reported as "written for a newer
schema", which is how two silently-ignored fields were caught. Never accept a
warning.

**3. Walk it with the flow harness.** Every command offered before it was
written, exactly once, in declared order, and the tour terminates on Next alone.

**4. Rewind it with the Back harness.** Groups come out whole, replaced lines
come back, a line the user edited survives.

**5. Screenshot it.** Several bugs were visible only in a render: the callout
sitting on the code, the mixing rules missing from a grouped beat, a wrong
answer accumulating dead lines. Use `QWidget::grab()` rather than GUI
automation, so the result is reproducible.

**6. Count the accept-only runs**, with `python3 doc/tutorial-measure.py
resources/tutorials/<name>.json`. It reports the longest run of steps whose
only action is Next and whether any act ends on one, and exits non-zero when
the run exceeds four, so it can be wired into a check. If the
run exceeds four, restructure (section 1). A step counts as active if it has a
`predict`, a `tune`, a typed drill, or an anchor on the Run button or an output
view.

**Harnesses must connect every signal the real application connects.** The flow
harness went a long time without `retractCommand`, which made every `replaces` a
silent no-op there -- a step that superseded a line looked like it worked while
the line sat untouched. When adding a signal, add it to the harnesses.

**When a harness fails, ask which is wrong.** Several assertions turned out to be
wrong rather than the code: accepting a group legitimately offers the next one,
so a pending line straight afterwards is the tour working; Back correctly
re-offers a step's first command, so its text is on screen again; a superseded
command is meant to be absent at the end.

**Say what you could not verify.** Without `liblammps` on the machine, nothing
downstream of the Run button is exercised -- the runs themselves, and every step
anchored to a chart, image or log.

---

## 11. Registering a new tutorial

1. `resources/tutorials/<name>.json` -- the content.
2. `resources/lammpsgui.qrc` -- one `<file>` entry, so it ships and works
   offline.
3. `LammpsGui::interactiveContentFor()` -- one line mapping collection and
   number to the resource path.
4. `requires_packages` -- what the script needs (`MOLECULE` for bonded styles,
   `MANYBODY` for AIREBO). Checked at start-up; warns without blocking.
5. Nothing else. The *Tutorials* menu, its per-tutorial emblems and the wizard
   pages are unchanged -- `interactiveContentFor()` only decides whether the
   wizard offers its checkbox. If a change to the menu seems necessary, it
   probably is not.

The tour is reachable **only** through the wizard's *Guide me through it step by
step* checkbox, after the files are downloaded into a folder the user chose. A
menu entry that starts a tour directly leaves later steps pointing at files that
do not exist; one existed as a preview and was removed for exactly that reason.

---

## 12. Schema reference

Current `schema_version` is **3**. An unknown key is a warning, not an error, so
a file written against a later minor revision still loads -- safe only because a
key that changes *meaning* comes with a version bump.

### Root

| Key | Notes |
|---|---|
| `schema_version` | required; 3 |
| `id` | required; stable, used as the progress key |
| `title` | required |
| `collection` | collection key, e.g. `softmatter` |
| `tutorial` | 1-based number in the collection |
| `requires_packages` | LAMMPS packages the script needs |
| `skeleton` | headings the script starts from; `[]` for given-and-modify |
| `attribution` | `source`, `license`, `credit` |
| `concepts` | `id`, `term`, `explain` |
| `acts` | `id`, `title`, `steps` |

### Step

| Key | Notes |
|---|---|
| `id`, `kind`, `title`, `teach` | required; `kind` is `SHOW` or `OBSERVE` |
| `doc_link` | `"<command>"` or `"<command> <style>"` |
| `anchor` | `none`, `editor`, `editor_all`, `run`, `snapshot`, `chart`, `image`, `log` |
| `section` / `before` | placement; mutually exclusive |
| `highlight` | ring an existing line; requires `anchor: "editor"` |
| `commands` | SHOW only; an OBSERVE step with commands is an error |
| `call_to_action` | overrides the generic "press Tab" prompt |
| `predict` | question asked before the user acts; requires `expect` |
| `expect` | the answer, revealed after they act |
| `wait_after_run` | Run-anchored only; do not advance automatically |
| `checkpoint` | a milestone worth pausing on |
| `open_file` | open a different script before this step |
| `tune` | `command`, `arg`, `from`, `to`, `min`, `max`, `decimals`, `label` |

### Command

| Key | Notes |
|---|---|
| `text` | required; one command per entry. Newlines allowed only where a trailing `&` continues the command |
| `explain` | required for `typed`, expected for every first use |
| `notes` | `arg`, `note`, `alternatives`, `concept` |
| `concept` | concept this whole line teaches |
| `together` | travels with the command before it; never on the first |
| `typed` | user types it; must stand alone and carry a `hint` |
| `hint` | nudge offered by the Hint button; required for `typed` |
| `replaces` | line this supersedes |

---

## 13. Mistakes that recur

Each cost real time, and most will happen again.

- **A new schema key not added to its key set.** Parsed correctly, warned about
  as "newer schema", ignored by the loader. Happened to `together` and would
  have happened to `replaces`. The lint catches it *if you read the warnings*.
- **Reading a `QTextBlock` number after inserting.** The handle tracks its
  position, so it reports where the old line was pushed to. The pending index
  ends up one line high and withdrawing it deletes the user's own text.
- **Re-entrancy through `pendingLineCommitted`.** The tour writes through the
  same machinery the user's Tab drives, so its own writes echo back. Without
  `InsertGuard` each inserted line looks like the user accepting the next group.
- **Acting on a commit when nothing is on offer.** A step with no pending
  commands reports `allCommandsInserted()`, so a stray commit advanced the tour
  -- pressing Tab in your own script walked the tutorial forward.
- **Deleting a widget from inside its own click handler.** Ending the tour from
  a button on the callout must go through `deleteLater()`.
- **`setDecimals()` after `setValue()`** on a `QDoubleSpinBox` re-quantizes the
  value; 0.005 became 0.0100.
- **Accessors that nothing reads.** `skeletonFile()`, `requiredPackages()` and
  `expect` were all parsed, validated, exposed -- and read by nobody. Three of
  them. When adding a field, write the code that *shows* it in the same change,
  or it will sit there looking implemented for months.
- **Validation rules that assume one tutorial style.** "A Run step must come
  after something has been written" and "`replaces` must name a line the tour
  wrote" were both true of build-up tutorials and both wrong for a script that
  arrives complete. When a rule fires on correct content, check the rule.
- **A widget's size hint that forgets its optional rows.** `sizeForWidth()`
  counted the title, body and buttons but not the prediction, feedback, drill
  or tune rows, so the prose was squeezed into a two-line scrolling box on
  exactly the steps that had most to say.

---

## 14. Sources

- Deslauriers, McCarty, Miller, Callaghan, Kestin, *Measuring actual learning
  versus feeling of learning in response to being actively engaged in the
  classroom*, PNAS 116(39) 19251-19257, 2019.
  <https://www.pnas.org/doi/10.1073/pnas.1821936116>
- Renkl, Atkinson et al., *From studying examples to solving problems: fading
  worked-out solution steps helps learning*; and *How fading worked solution
  steps works -- a cognitive load perspective*, Instructional Science, 2004.
  <https://link.springer.com/article/10.1023/B:TRUC.0000021815.74806.f6>
- Sweller et al., *The guidance fading effect* and the expertise reversal
  effect. <https://cogscisci.wordpress.com/wp-content/uploads/2019/08/sweller-guidance-fading.pdf>
- Nokes, Hausmann, VanLehn, Gershman, *Testing the instructional fit hypothesis:
  the case of self-explanation prompts*, Instructional Science, 2011.
  <https://link.springer.com/article/10.1007/s11251-010-9151-4>
- Gravelle, Alvares, Gissinger, Kohlmeyer, *A Set of Tutorials for the LAMMPS
  Simulation Package*, LiveCoMS 6(1) 3037, 2025.
  <https://doi.org/10.33011/livecoms.6.1.3037>
- The companion materials-science tutorial article, for the given-and-modify
  style and its statement of the complementary approach.
