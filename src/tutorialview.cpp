// -*- c++ -*- /////////////////////////////////////////////////////////////////////////
// LAMMPS-GUI - A Graphical Tool to Learn and Explore the LAMMPS MD Simulation Software
//
// Copyright (c) 2023, 2024, 2025, 2026  Axel Kohlmeyer
//
// Documentation: https://lammps-gui.lammps.org/
// Contact: akohlmey@gmail.com
//
// This software is distributed under the GNU General Public License version 2 or later.
////////////////////////////////////////////////////////////////////////////////////////

#include "tutorialview.h"

#include "constants.h"
#include "tutorialengine.h"
#include "tutorialtext.h"

#include <QRegularExpression>
#include <QWidget>

TutorialView::TutorialView(TutorialEngine *engine, QWidget *host) :
    QObject(host), engine(engine), host(host)
{
    spotlight = new TutorialSpotlight(host);
    // a sibling, not a child: TutorialSpotlight is transparent for mouse events,
    // and that makes its whole subtree un-hittable -- a callout parented to it
    // would look right and be completely unclickable
    coach = new TutorialCoach(host);

    connect(coach, &TutorialCoach::nextRequested, this, &TutorialView::goNext);
    connect(coach, &TutorialCoach::backRequested, this, &TutorialView::goBack);
    connect(coach, &TutorialCoach::tuneRequested, this, &TutorialView::applyTune);
    connect(coach, &TutorialCoach::extraRequested, this, &TutorialView::nextTutorialRequested);
    connect(engine, &TutorialEngine::stepChanged, this, &TutorialView::showCurrentStep);
}

TutorialView::~TutorialView()
{
    // both are children of the host, so Qt would eventually delete them, but
    // the coach mark has to disappear the moment the tour ends
    delete spotlight;
    delete coach;
}

void TutorialView::setAnchorResolver(std::function<QRect(StepAnchor)> resolver)
{
    resolve = std::move(resolver);
}

void TutorialView::start()
{
    // the script starts as a set of empty headings, so the shape of an input
    // file is visible before any of it is filled in
    emit seedSkeleton(engine->content().skeletonLines());

    spotlight->setGeometry(host->rect());
    spotlight->show();
    spotlight->raise();
    coach->show();
    coach->raise();
    showCurrentStep();
}

void TutorialView::stop()
{
    if (spotlight) spotlight->hide();
    if (coach) coach->hide();
}

/* -------------------------------------------------------------------- */

QString TutorialView::renderText(const QString &text)
{
    // escaping first means a content file can never inject markup of its own
    QString out = text.toHtmlEscaped();
    out.replace(QRegularExpression(QStringLiteral("\\*\\*([^*]+)\\*\\*")),
                QStringLiteral("<b>\\1</b>"));
    out.replace(QRegularExpression(QStringLiteral("(?<![*])\\*([^*]+)\\*(?![*])")),
                QStringLiteral("<i>\\1</i>"));
    out.replace(QRegularExpression(QStringLiteral("`([^`]+)`")),
                QStringLiteral("<code>\\1</code>"));
    // the tutorial's mathematics: superscripts and subscripts are all it needs,
    // and QTextBrowser renders them without any new dependency
    out.replace(QRegularExpression(QStringLiteral("\\^\\{([^}]*)\\}")),
                QStringLiteral("<sup>\\1</sup>"));
    out.replace(QRegularExpression(QStringLiteral("_\\{([^}]*)\\}")),
                QStringLiteral("<sub>\\1</sub>"));
    out.replace(QStringLiteral("\n\n"), QStringLiteral("<p>"));
    out.replace(QStringLiteral("\n"), QStringLiteral("<br>"));
    return out;
}

StepAnchor TutorialView::currentAnchor() const
{
    const TutorialStep *step = engine->currentStep();
    if (!step) return StepAnchor::None;
    // a step still offering a command points at the editor, whatever else it
    // declares; the declared anchor takes over once the lines are all in
    if (engine->nextCommand()) return StepAnchor::Editor;
    return step->anchor;
}

void TutorialView::reposition()
{
    if (!spotlight || !coach) return;
    spotlight->setGeometry(host->rect());

    // clipped to the window, so an anchor in a scrolled or partly hidden view
    // still produces a sane ring
    QRect targetRect = resolve ? resolve(currentAnchor()).intersected(host->rect()) : QRect();
    spotlight->setTarget(targetRect);

    const int width  = qMin(Cfg::COACH_WIDTH, host->width() - 2 * Cfg::COACH_GAP);
    const QSize size = coach->sizeForWidth(width);

    if (targetRect.isEmpty()) {
        // no target: park in the top right, which is where the tour lives by
        // default and where the user learns to look for it
        coach->setSide(TutorialCoach::Side::None);
        coach->setTailOffset(-1);
        coach->setGeometry(host->width() - size.width() - Cfg::COACH_GAP, Cfg::COACH_GAP,
                           size.width(), size.height());
        coach->raise();
        return;
    }

    // Beside the target is the only placement that can point at something
    // without sitting on it, so those are the only two tried.  Above and Below
    // used to be tried next, and for the commonest target of all -- a line of
    // the editor, which spans the full width of the viewport -- they put the
    // callout squarely on top of the surrounding lines, covering the very
    // script the step was talking about.
    const auto clampInside = [&](QPoint p) {
        p.setX(qBound(Cfg::COACH_GAP, p.x(), host->width() - size.width() - Cfg::COACH_GAP));
        p.setY(qBound(Cfg::COACH_GAP, p.y(), host->height() - size.height() - Cfg::COACH_GAP));
        return p;
    };

    struct Placement {
        TutorialCoach::Side side;
        QPoint pos;
    };
    const int midY = targetRect.center().y() - size.height() / 2;
    // Right first: the target is usually a line of the script, and the reading
    // order puts the explanation after the thing it explains rather than before.
    for (const Placement &p :
         {Placement{TutorialCoach::Side::Right, {targetRect.right() + Cfg::COACH_GAP, midY}},
          Placement{TutorialCoach::Side::Left,
                    {targetRect.left() - Cfg::COACH_GAP - size.width(), midY}}}) {
        const QRect placed(clampInside(p.pos), size);
        // the clamp can shove a placement back over the target on a narrow
        // window, so the overlap is re-tested after clamping rather than before
        if (!placed.intersects(targetRect) && host->rect().contains(placed)) {
            coach->setSide(p.side);
            coach->setGeometry(placed);
            // The callout is far taller than the line it points at, and the
            // clamp above may have slid it up or down to stay on screen, so the
            // tail is placed on the target's own row rather than at the
            // bubble's centre -- otherwise it points at a line several rows off.
            coach->setTailOffset(targetRect.center().y() - placed.top());
            coach->raise();
            return;
        }
    }

    // Nothing fits beside it.  Park in a corner and drop the tail: the ring
    // already says what is meant, and a tail pointing clear across the window
    // says less than no tail at all.  The top right is the corner the user
    // learns to look at, so it wins ties; the others are there for when the
    // thing being pointed at is up there too.
    const int left   = Cfg::COACH_GAP;
    const int right  = host->width() - size.width() - Cfg::COACH_GAP;
    const int top    = Cfg::COACH_GAP;
    const int bottom = host->height() - size.height() - Cfg::COACH_GAP;

    QRect best;
    int leastOverlap = -1;
    for (const QPoint &corner : {QPoint(right, top), QPoint(right, bottom), QPoint(left, bottom),
                                 QPoint(left, top)}) {
        const QRect candidate(clampInside(corner), size);
        const QRect shared  = candidate.intersected(targetRect);
        const int overlap   = shared.width() * shared.height();
        if (leastOverlap < 0 || overlap < leastOverlap) {
            leastOverlap = overlap;
            best         = candidate;
        }
    }

    coach->setSide(TutorialCoach::Side::None);
    coach->setTailOffset(-1);
    coach->setGeometry(best);
    coach->raise();
}

void TutorialView::showCurrentStep()
{
    const TutorialStep *step = engine->currentStep();
    if (!step) {
        coach->setContent(engine->content().title(), QStringLiteral("Tutorial complete"),
                          QStringLiteral("<p>That is the end of the tutorial. The script in the "
                                         "editor is yours -- keep changing it and re-running to "
                                         "see what happens.</p>"));
        coach->setCallToAction(QString());
        coach->setFeedback(QString(), true);
        // the last step of Tutorial 1 carries a tune control, and without this
        // it stayed on screen offering to edit a script the tour has finished
        // talking about
        coach->setTune(TuneControl());
        coach->setProgress(engine->content().stepCount(), engine->content().stepCount());
        coach->setNextText(QStringLiteral("&Done"));
        coach->setNextEnabled(true);
        // the caller decides whether there is a next tutorial to offer, and
        // what it is called; an empty label hides the button
        coach->setExtraButton(nextLabel);
        coach->setBackEnabled(true);
        reposition();
        emit finished();
        return;
    }

    // a step may move the tour onto a different input file
    if (!step->openFile.isEmpty()) emit openFileRequested(step->openFile);

    const auto &act = engine->content().acts().at(engine->actIndex());
    coach->setProgress(engine->stepsCompleted() + 1, engine->content().stepCount());
    coach->setBackEnabled(engine->stepsCompleted() > 0);
    coach->setNextText(QStringLiteral("&Next >"));
    coach->setExtraButton(QString());
    coach->setNextEnabled(true);
    coach->setFeedback(QString(), true);

    QString body = renderText(step->teach);

    // a step with commands hands them to the editor one at a time; the callout
    // never shows the code itself, only what it means
    const QList<CommandLine> group = engine->nextGroup();
    // the budget is spent once per group rather than once per line: two
    // pair_coeff lines that travel together each carry their own meaning, and
    // suppressing the second because the first just used up "pair_coeff" threw
    // away the mixing rules
    QStringList wordsThisGroup;
    for (const auto &cmd : group) {
        // the command word gets a budget of one.  By the sixth "region" the
        // prose is noise, so a repeat shows the per-argument notes alone --
        // every group still gets its own beat either way.
        const QString word  = cmd.text.section(QLatin1Char(' '), 0, 0);
        const bool firstUse = engine->firstUseOf(word);
        QStringList shownConcepts;

        if (firstUse && !cmd.explain.isEmpty())
            body += QStringLiteral("<p><b><code>%1</code></b> &mdash; %2</p>")
                        .arg(word.toHtmlEscaped(), renderText(cmd.explain));

        const QStringList words = canonicalWords(cmd.text);
        for (const auto &note : cmd.notes) {
            const QString token = note.argIndex < words.size() ? words.at(note.argIndex)
                                                              : QString::number(note.argIndex);
            body += QStringLiteral("<p><code>%1</code> &mdash; %2")
                        .arg(token.toHtmlEscaped(), renderText(note.note));

            const bool explain =
                note.conceptId.isEmpty() || engine->shouldExplain(note.conceptId);
            if (explain && !note.alternatives.isEmpty())
                body += QStringLiteral("<br><i>%1</i>").arg(renderText(note.alternatives));
            if (explain && !note.conceptId.isEmpty())
                if (const auto *c = engine->content().conceptFor(note.conceptId))
                    body += QStringLiteral("<br><small>%1: %2</small>")
                                .arg(c->term.toHtmlEscaped(), renderText(c->explain));
            if (!note.conceptId.isEmpty()) shownConcepts << note.conceptId;
            body += QStringLiteral("</p>");
        }
        if (!cmd.conceptId.isEmpty()) shownConcepts << cmd.conceptId;
        engine->noteConceptsShown(shownConcepts);
        wordsThisGroup << word;
    }
    for (const auto &word : wordsThisGroup)
        engine->noteCommandShown(word);

    coach->setContent(QStringLiteral("%1 -- %2").arg(engine->content().title(), act.title),
                      step->title, body);

    if (!group.isEmpty()) {
        QStringList texts;
        bool anyTyped = false;
        for (const auto &cmd : group) {
            texts << cmd.text;
            anyTyped = anyTyped || cmd.typed;
        }
        if (anyTyped) {
            // reinforcement: described but never written, so the user produces
            // it themselves.  A blank pending line marks where it goes.
            emit offerCommands(QStringList(), step->section, step->before);
            coach->setCallToAction(
                QStringLiteral("Type it yourself on the highlighted line, then press Tab."));
        } else {
            emit offerCommands(texts, step->section, step->before);
            // a step that has something particular to say about accepting these
            // lines says it; otherwise the generic prompt, which is right almost
            // everywhere and would be tedious to repeat in the content
            coach->setCallToAction(
                !step->callToAction.isEmpty()
                    ? step->callToAction
                    : (texts.size() > 1
                           ? QStringLiteral("Press Tab in the editor to accept these %1 lines.")
                                 .arg(texts.size())
                           : QStringLiteral("Press Tab in the editor to accept this line.")));
        }
    } else {
        coach->setCallToAction(step->callToAction);
    }

    // a step explaining a line the tour did not write rings it instead
    emit markLine(group.isEmpty() ? step->highlight : QString());

    // a step that asks the user to change a number in a line already written
    // carries the control to do it with
    coach->setTune(step->tune);

    reposition();
}

void TutorialView::commandCommitted(const QString &written)
{
    // our own writes echo back through here; only a commit the user made
    // advances the tour
    if (inserting) return;

    // and only a commit against something the tour actually offered.  A step
    // with nothing on offer -- an OBSERVE step waiting on the Run button, or a
    // SHOW step whose commands are all in -- reports allCommandsInserted(), so
    // a stray commit would walk the tour forward a step at a time while the
    // user was only editing their own script.
    if (engine->nextGroup().isEmpty()) return;

    const CommandLine *cmd = engine->nextCommand();
    if (cmd && cmd->typed) {
        // compare word by word after canonicalization, so spacing, letter case
        // and a trailing comment do not decide whether the answer is right
        const QStringList got  = canonicalWords(written);
        const QStringList want = canonicalWords(cmd->text);
        if (got != want) {
            QString why = QStringLiteral("Not quite -- try again.");
            if (got.size() != want.size())
                why = QStringLiteral("That has %1 words; the command takes %2.")
                          .arg(got.size())
                          .arg(want.size());
            else
                for (int i = 0; i < got.size(); ++i)
                    if (got.at(i).compare(want.at(i), Qt::CaseInsensitive) != 0) {
                        why = QStringLiteral("Word %1 should not be \"%2\".")
                                  .arg(i + 1)
                                  .arg(got.at(i));
                        break;
                    }
            coach->setFeedback(why, false);
            // hand the attempt back as the pending line rather than leaving it
            // committed and opening a fresh blank one below it: the feedback
            // names the word that is wrong, so the user wants to edit what they
            // wrote, and a retry must not leave a dead line behind each time
            const InsertGuard guard(inserting);
            emit retractCommand(written);
            emit offerCommands(QStringList() << written, engine->currentStep()->section,
                               engine->currentStep()->before);
            return;
        }
        coach->setFeedback(QStringLiteral("That is it."), true);
    }

    {
        // the user put the new line in themselves; the superseded one still
        // has to come out, and that removal is ours rather than theirs
        const InsertGuard guard(inserting);
        retractSuperseded(engine->nextGroup());
    }
    engine->takeNextGroup();
    // accepting the last group of a step finishes it: waiting for a separate
    // Next press there just looks like nothing happened
    if (engine->allCommandsInserted() && !engine->currentStep()->expect.isEmpty()) {
        showCurrentStep();
        return;
    }
    if (engine->allCommandsInserted()) {
        engine->next();
        return;
    }
    showCurrentStep();
}

void TutorialView::runFinished(bool success)
{
    const TutorialStep *step = engine->currentStep();
    if (!step || step->anchor != StepAnchor::Run) return;
    if (!success) return; // a failed run keeps the user where the failure is

    // Following the run automatically is what makes the tour feel like it is
    // watching with you.  A step can opt out when what comes next would sweep
    // the results away -- writing a data file and opening a different script --
    // and the user has not had a chance to look at them yet.
    if (step->waitAfterRun) {
        coach->setCallToAction(
            QStringLiteral("The run has finished. Look at the results, then press Next when "
                           "you are ready to move on."));
        coach->setFeedback(QStringLiteral("Run complete."), true);
        return;
    }
    engine->next();
}

void TutorialView::goNext()
{
    // on the completion panel the Next button reads "Done", and pressing it
    // means the user is finished rather than that there is anywhere to go: the
    // cursor is already past the last step, so engine->next() would return
    // straight away and the callout would sit there for ever
    if (engine->isFinished()) {
        emit closeRequested();
        return;
    }

    // an offered but unaccepted line is withdrawn rather than left behind
    emit withdrawCommand();

    // Next accepts exactly the group that is on offer and no more.  It used to
    // dump every remaining command of the step at once, which is how commands
    // arrived in the script having never been shown or explained.
    if (!engine->nextGroup().isEmpty()) {
        const TutorialStep *step        = engine->currentStep();
        const QList<CommandLine> group  = engine->nextGroup();
        const QStringList texts         = engine->takeNextGroup();
        // writing a line goes through the editor's pending-line machinery, so
        // it comes back as pendingLineCommitted -- which is also how the user
        // accepts a line with Tab.  Without this guard our own insertion is
        // mistaken for the user accepting the *next* group, and that group is
        // written having never been offered.  It covers the retraction too: a
        // removal must not read as a user edit either.
        const InsertGuard guard(inserting);
        retractSuperseded(group);
        for (const auto &text : texts)
            emit insertCommand(text, step ? step->section : QString(),
                               step ? step->before : QString());
        if (!engine->allCommandsInserted()) {
            showCurrentStep();
            return;
        }
    }
    engine->next();
}

void TutorialView::goBack()
{
    emit withdrawCommand();

    // rewind the script as well as the cursor.  Stepping back used to leave
    // every accepted line in place, so going forward again wrote them twice.
    if (const TutorialStep *leaving = engine->currentStep()) rewind(leaving->id);

    // move without letting the step change repaint yet: showCurrentStep() would
    // offer the landing step's first command straight back into the buffer, and
    // the rewind below would then be working against a line that is in flight
    const bool blocked = engine->blockSignals(true);
    engine->previous();
    engine->blockSignals(blocked);

    // the step we land on replays from its first command, so its lines come out
    if (const TutorialStep *landing = engine->currentStep()) rewind(landing->id);
    showCurrentStep();
}

void TutorialView::applyTune(double value)
{
    const TutorialStep *step = engine->currentStep();
    if (!step || !step->tune.isValid()) return;

    // format at the control's own precision: writing 15000.000000 into a step
    // count, or 0.0050000000000000001 into a timestep, is not what the user
    // dialled in and not what the article shows
    const QString text = QString::number(value, 'f', step->tune.decimals);
    emit tuneParameter(step->tune.command, step->tune.argIndex, text);
    coach->setFeedback(QStringLiteral("%1 is now %2.").arg(step->tune.command, text), true);
}

void TutorialView::retractSuperseded(const QList<CommandLine> &group)
{
    const TutorialStep *step = engine->currentStep();
    if (!step) return;
    for (const auto &cmd : group) {
        if (cmd.replaces.isEmpty()) continue;
        emit retractCommand(cmd.replaces);
        engine->noteReplaced(step->id, cmd.replaces);
    }
}

void TutorialView::rewind(const QString &stepId)
{
    const InsertGuard guard(inserting);

    const QStringList lines = engine->writtenFor(stepId);
    // last written, first removed: undoing in reverse keeps the matches honest
    for (int i = lines.size() - 1; i >= 0; --i)
        emit retractCommand(lines.at(i));
    engine->forgetWritten(stepId);

    // and put back whatever this step displaced, so Back really is the inverse
    // of Next rather than a one-way trim of the script
    const TutorialStep *step = engine->content().stepById(stepId);
    for (const auto &text : engine->replacedFor(stepId))
        emit insertCommand(text, step ? step->section : QString(),
                               step ? step->before : QString());
    engine->forgetReplaced(stepId);
}

// Local Variables:
// c-basic-offset: 4
// End:
