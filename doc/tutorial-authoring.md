# Authoring an interactive tutorial

Heuristics for adding a guided tutorial to LAMMPS-GUI, written after building
Tutorial 1 (Lennard-Jones fluid, complete) and Tutorial 2 part 1 (carbon
nanotube, unbreakable bonds).

Companion documents: `tutorial-mode-design.md` is the original reconnaissance,
`tutorial-mode-redesign.md` records why the model changed. Neither is in the
Sphinx toctree; nor is this one. The user-facing description lives in
`menus.rst` under *Interactive tutorial mode*.

**The one-sentence version.** A tutorial is a data file. Adding one should be a
content file plus one line in `interactiveContentFor()`; if you find yourself
writing C++ to express a tutorial, stop and ask whether the schema is missing
something general, or whether the content is fighting the model.

---

## 1. Before writing a line of content

Do these four things first. Every one of them has caught something.

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
   file must name one that reaches disk.

3. **Fetch the initial input file.** This decides the whole shape of the tour.
   Tutorial 1's `initial.lmp` is six comment headings and nothing else, so the
   tour fills in sections. Tutorial 2's `unbreakable.lmp` is a complete,
   runnable script, so the tour reads it aloud first and then grows it from the
   middle. Do not assume which kind you have.

4. **Fetch `solution/<name>.lmp`.** This is the ground truth for command text
   and ordering -- prefer it over the article's prose, which sometimes shows a
   simplified line. It is also the target for the strongest verification step
   available (section 9).

---

## 2. Invariants -- do not break these

These are not style preferences. Each one was violated at some point, produced
a bug a user noticed, and was fixed.

| Invariant | Why |
|---|---|
| **Nothing reaches the script that was not shown first.** | Commands appearing from nowhere was the single most-reported problem. The flow harness asserts it. |
| **The callout never shows code.** | Code belongs in the editor, highlighted, where the user will edit it. The callout says what it means. |
| **Back is the inverse of Next.** | It removes what the tour wrote and restores what the tour displaced. |
| **The user's edits are theirs.** | Never overwrite a line the user changed. `seedSkeleton()` refuses a non-empty buffer for this reason. |
| **A step explains; it does not quiz.** | The recall-gated model was tried and retired. See `tutorial-mode-redesign.md`. |
| **Warn, do not block.** | A missing package, a missing file: say so and carry on. The explanations are worth reading on a build that cannot run the script. |

---

## 3. Shaping the tour

**Open with an OBSERVE step about the file as a whole** (`anchor: "editor_all"`).
The user needs to know what they are looking at before a single line is
highlighted. Writing starts at step 2.

**One idea per step.** A step is a beat: one thing to understand, then one
action. If the `teach` text needs three paragraphs to cover three commands,
that is three steps.

**Group commands that serve one purpose** with `together: true`. The two
`create_atoms` lines, a region and its complement, `thermo` and `thermo_style`.
The group is highlighted and pasted in one action, under one explanation. The
first command of a step cannot be `together` -- there is nothing before it to
group with.

**Group size is a per-tutorial judgment.** In Tutorial 1 nearly everything is
its own beat, because nothing is familiar. By Tutorial 5 a whole Initialization
section can be one group with a casual explanation. Grouping is how a tutorial
gets faster as the reader gets better.

**Put a run where the article puts one**, and follow it with OBSERVE steps
anchored to what it produced -- `log`, `chart`, `image`. A run-anchored step
advances on a successful run rather than on Next, so the tour watches with the
user. Use `wait_after_run: true` when the next step would sweep the results
away, such as opening a different script.

**Close with an experiments step.** Two or three things to change, each one
number, each with what to expect. This is where a tutorial stops being a
transcript.

---

## 4. Placing commands

Three placements, and they are mutually exclusive answers to "where does this
go?".

| Field | Behavior | Use when |
|---|---|---|
| `section` | files under a `#` heading, after anything already there | the file arrives as a skeleton of headings (Tutorial 1) |
| `before` | inserts immediately above the named line | the file arrives complete and grows from the middle (Tutorial 2) |
| neither | appends at the end of the buffer | the tail of a script, after the structure exists |

`section` and `before` together is a load-time error. A `section` naming a
heading absent from the `skeleton` array is also an error -- otherwise the
commands would silently append at the end instead.

**The `skeleton` array must mirror the upstream file verbatim.** It seeds an
empty buffer, but when the wizard has opened the real file the tour matches
against the headings *that file* has. Drift between the two means silent
appending. For a tutorial whose input arrives complete, `skeleton` is `[]`.

**`replaces` supersedes a line.** The tour otherwise only ever adds, so a step
that tells the user a command "takes the place of" an earlier one has to say so
in the data or the prose is a lie. `replaces` may name a line the tour wrote or
one a step names with `before` -- that is, one that came with the file.

**A replacing command should be its own step** when the step's other commands
are placed relative to the line being replaced. Otherwise the target
disappears part-way through the step and the remaining commands fall back to
appending. This bit Tutorial 2's `run 5000`.

**`highlight` rings a line the tour did not write.** For the opening act of a
tutorial whose script arrives complete. It requires `anchor: "editor"`, since a
highlighted line is a line of the script.

---

## 5. Writing the explanations

The explanation is the **only** time a command is explained. It used to be
shown repeatedly, and 47 of Tutorial 1's 51 explanations were written under
that assumption -- one line each, useless on their own. They were all rewritten.

**On first use, write the full context**: what the command does, what each
argument means, and what a different value would do. That last part is what a
reference manual does not give you and is usually the most valuable sentence in
the step.

**On repeat use, stay terse.** A second `region` does not need re-teaching. The
budget is spent per *group* rather than per line, so two `pair_coeff` lines
travelling together each keep their own meaning -- getting this wrong silently
dropped the mixing rules from Tutorial 1.

**Per-argument notes** (`notes`) carry `arg` (0 is the command word), `note`
(what this position means), and `alternatives` (what else it could be). Prefer
a note over a longer paragraph: it is anchored to the token it describes.

**Concepts fade.** A `concept` id draws on a shared reminder budget
(`Cfg::CONCEPT_REMINDER_BUDGET`) that persists across tutorials, so something
learned in Tutorial 1 is not re-taught in Tutorial 2. Declare a concept only if
a command or note references it -- an OBSERVE step has no commands and so
cannot reference one, and an unreferenced concept is a load-time warning.

### Two traps worth naming

**What you write is not what LAMMPS prints.** `thermo_style custom step etotal
pe ke` produces columns headed `Step`, `TotEng`, `PotEng`, `KinEng`. The
command text uses the keywords; any step telling the user what to *look at*
must use the printed names. Three of Tutorial 1's observation steps got this
wrong and sent readers hunting for a column called `etotal`.

**American English, plain ASCII.** `CLAUDE.md` requires it for documentation,
and user-facing tutorial prose is documentation. Use `--` rather than an
em-dash. Greek letters and mathematical symbols are the exception: they are
written as Unicode directly.

### Mathematics

`renderText()` supports a small markdown subset -- `**bold**`, `*italic*`,
`` `code` `` -- plus `^{...}` and `_{...}` for superscript and subscript, which
`QTextBrowser` renders with no new dependency. Escaping happens first, so
content can never inject markup.

**Include only equations the article actually contains**, on the beat where
they belong. Tutorial 1 has four: the Lennard-Jones potential on `pair_style`,
the mixing rules with worked values on `pair_coeff 2 2`, `U + K = E` on the
molecular-dynamics `thermo_style`, and the `1/r^{12}` repulsion where it
explains a positive initial energy.

**Do not ship the article's figures.** Five of Tutorial 1's six are things the
user generates live -- the GUI renders their own system, which is strictly
better than a picture of someone else's -- and the sixth is a screenshot of an
older LAMMPS-GUI that would ship stale. This is a deliberate choice, **not** a
licensing constraint (section 8). Do not re-open it on licensing grounds.

---

## 6. Interaction beyond Tab

| Mechanism | Status | Use when |
|---|---|---|
| Tab / Next accepts | the default | almost always |
| `tune` (spin box + Apply) | works, fixture-tested, **no shipped user** | changing one number *is* the lesson. Two attempts were removed as noise: dialing a step count teaches nothing. |
| `typed` (blank line, word-by-word check) | works, fixture-tested, **deliberately unused** | deferred until there are hint and solve buttons. A blank line with nothing on screen to work from is not reinforcement. |
| multiple choice | not built | planned for later tutorials |

When typed drills return: only on a command already shown and explained, never
in the opening acts, and each must stand alone -- a command travelling with a
drill would be recorded as written without ever being written, which is a
load-time error.

---

## 7. Anchors and the callout

| Anchor | Points at |
|---|---|
| `editor` | the line or group being offered, or a `highlight`ed line |
| `editor_all` | the whole editor, for a step about the file as a whole |
| `run` | the Run button |
| `snapshot` | the status bar's snapshot-image button |
| `chart`, `image`, `log` | the corresponding output view |

Placement is automatic and you should not fight it: the callout goes beside the
target when it fits, with the tail on the target's row, and otherwise parks in
whichever corner overlaps least, with no tail. Above and below are never tried
-- for a target the width of a line of code they put the callout on top of the
script.

The chart, image and log anchors resolve through `WindowLayout::presenter()`,
so they work in the docked layout. With individual windows the view is a
separate top-level window with no rectangle inside the main one, and the
callout parks instead. That is a real limitation, not a bug to route around in
content.

---

## 8. Attribution and licensing

The `lammpstutorials-article` repository is **CC BY 4.0** -- stated in its
`LICENSE` and repeated in the header of every input file with the DOI to cite.
Adaptation with attribution is permitted. This was recorded for a long time as
unresolved and blocking release; it is neither.

Every content file carries the triple:

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

## 9. Verifying a tutorial

In rough order of how much each is worth.

**1. Diff the tour's output against the article's solution.** Walk the tour on
Next alone and compare the resulting script with `solution/<name>.lmp`. For
Tutorial 2 this is identical line for line, ignoring blank lines. Nothing else
comes close to this as evidence that the content is right.

**2. Lint expecting 0 errors and 0 warnings.** Warnings matter as much as
errors here: a schema key missing from its key set is reported as "written for a
newer schema", which is how two silently-ignored fields were caught. Never
accept a warning.

**3. Walk it with the flow harness.** It asserts that every command was offered
before it was written, exactly once, in the order the content declares, and
that the whole tour terminates on Next alone.

**4. Rewind it with the Back harness.** Groups come out whole, replaced lines
come back, and a line the user edited survives.

**5. Screenshot it.** Several bugs were visible only in a render: the callout
sitting on the code, the mixing rules missing from a grouped beat, a wrong
answer accumulating dead lines. Use `QWidget::grab()` rather than GUI
automation, so the result is reproducible.

**Harnesses must connect every signal the real application connects.** The flow
harness went a long time without `retractCommand`, which made every `replaces`
a silent no-op there -- a step that superseded a line looked like it worked
while the line sat untouched. When adding a signal, add it to the harnesses.

**When a harness fails, ask which is wrong.** Several assertions turned out to
be wrong rather than the code: accepting a group legitimately offers the next
one, so a pending line straight afterwards is the tour working; Back correctly
re-offers a step's first command, so its text is on screen again; a superseded
command is meant to be absent at the end.

**Say what you could not verify.** Without `liblammps` on the machine, nothing
downstream of the Run button is exercised -- the runs themselves, and every
step anchored to a chart, image or log.

---

## 10. Registering a new tutorial

1. `resources/tutorials/<name>.json` -- the content.
2. `resources/lammpsgui.qrc` -- one `<file>` entry, so the tutorial ships and
   works offline.
3. `LammpsGui::interactiveContentFor()` -- one line mapping collection and
   number to the resource path.
4. `requires_packages` -- name what the script needs (`MOLECULE` for bonded
   styles, `MANYBODY` for AIREBO). Checked at start-up; warns without blocking.
5. Nothing else. The *Tutorials* menu, its per-tutorial emblems and the wizard
   pages are unchanged -- `interactiveContentFor()` only decides whether the
   wizard offers its checkbox. If a change to the menu seems necessary, it
   probably is not.

The tour is reachable **only** through the wizard's *Guide me through it step
by step* checkbox, after the files are downloaded into a folder the user chose.
A menu entry that starts a tour directly leaves later steps pointing at files
that do not exist; one existed as a preview, and it was removed for exactly
that reason. Do not add another.

---

## 11. Schema reference

Current `schema_version` is **3**. An unknown key is a warning, not an error,
so a file written against a later minor revision still loads -- which is only
safe because a key that changes *meaning* comes with a version bump.

### Root

| Key | Notes |
|---|---|
| `schema_version` | required; 3 |
| `id` | required; stable, used as the progress key |
| `title` | required |
| `collection` | collection key, e.g. `softmatter` |
| `tutorial` | 1-based number in the collection |
| `requires_packages` | LAMMPS packages the script needs |
| `skeleton` | section headings the script starts from; `[]` when the file arrives complete |
| `attribution` | `source`, `license`, `credit` |
| `concepts` | `id`, `term`, `explain` |
| `acts` | `id`, `title`, `steps` |

### Step

| Key | Notes |
|---|---|
| `id`, `kind`, `title`, `teach` | required; `kind` is `SHOW` or `OBSERVE` |
| `doc_link` | `"<command>"` or `"<command> <style>"`, resolved against the shipped help index |
| `anchor` | `none`, `editor`, `editor_all`, `run`, `snapshot`, `chart`, `image`, `log` |
| `section` / `before` | placement; mutually exclusive |
| `highlight` | ring an existing line; requires `anchor: "editor"` |
| `commands` | SHOW only; an OBSERVE step with commands is an error |
| `call_to_action` | overrides the generic "press Tab" prompt |
| `expect` | what the user should see, shown after the action |
| `wait_after_run` | Run-anchored only; do not advance automatically |
| `checkpoint` | a milestone worth pausing on |
| `open_file` | open a different script before this step |
| `tune` | `command`, `arg`, `from`, `to`, `min`, `max`, `decimals`, `label` |

### Command

| Key | Notes |
|---|---|
| `text` | required; one command per entry, no newlines |
| `explain` | required for a `typed` command, expected for every first use |
| `notes` | `arg`, `note`, `alternatives`, `concept` |
| `concept` | concept this whole line teaches |
| `together` | travels with the command before it; never on the first |
| `typed` | user types it; must stand alone |
| `replaces` | line this supersedes |

---

## 12. Mistakes that recur

Kept because each cost real time, and most of them will happen again.

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
  commands reports `allCommandsInserted()`, so a stray commit advanced the
  tour -- pressing Tab in your own script walked the tutorial forward.
- **Deleting a widget from inside its own click handler.** Ending the tour from
  a button on the callout must go through `deleteLater()`.
- **`setDecimals()` after `setValue()`** on a `QDoubleSpinBox` re-quantizes the
  value; 0.005 became 0.0100.
- **Accessors that nothing reads.** `skeletonFile()` and `requiredPackages()`
  were both parsed, exposed and dead. Either make it load-bearing or delete it.
