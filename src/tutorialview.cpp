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
        coach->setGeometry(host->width() - size.width() - Cfg::COACH_GAP, Cfg::COACH_GAP,
                           size.width(), size.height());
        coach->raise();
        return;
    }

    // prefer left of the target, then right, then above, then below: the views
    // being pointed at are usually on the right hand side of the window
    TutorialCoach::Side side = TutorialCoach::Side::Left;
    QPoint pos;
    if (targetRect.left() - Cfg::COACH_GAP - size.width() >= 0) {
        side = TutorialCoach::Side::Left;
        pos  = {targetRect.left() - Cfg::COACH_GAP - size.width(),
                targetRect.center().y() - size.height() / 2};
    } else if (targetRect.right() + Cfg::COACH_GAP + size.width() <= host->width()) {
        side = TutorialCoach::Side::Right;
        pos  = {targetRect.right() + Cfg::COACH_GAP, targetRect.center().y() - size.height() / 2};
    } else if (targetRect.top() - Cfg::COACH_GAP - size.height() >= 0) {
        side = TutorialCoach::Side::Above;
        pos  = {targetRect.center().x() - size.width() / 2,
                targetRect.top() - Cfg::COACH_GAP - size.height()};
    } else {
        side = TutorialCoach::Side::Below;
        pos  = {targetRect.center().x() - size.width() / 2, targetRect.bottom() + Cfg::COACH_GAP};
    }

    // keep the whole callout inside the window whatever the target's position
    pos.setX(qBound(Cfg::COACH_GAP, pos.x(), host->width() - size.width() - Cfg::COACH_GAP));
    pos.setY(qBound(Cfg::COACH_GAP, pos.y(), host->height() - size.height() - Cfg::COACH_GAP));

    coach->setSide(side);
    coach->setGeometry(QRect(pos, size));
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
        coach->setProgress(engine->content().stepCount(), engine->content().stepCount());
        coach->setNextText(QStringLiteral("&Done"));
        coach->setNextEnabled(true);
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
            emit offerCommands(QStringList(), step->section);
            coach->setCallToAction(
                QStringLiteral("Type it yourself on the highlighted line, then press Tab."));
        } else {
            emit offerCommands(texts, step->section);
            coach->setCallToAction(
                texts.size() > 1
                    ? QStringLiteral("Press Tab in the editor to accept these %1 lines.")
                          .arg(texts.size())
                    : QStringLiteral("Press Tab in the editor to accept this line."));
        }
    } else {
        coach->setCallToAction(step->callToAction);
    }

    reposition();
}

void TutorialView::commandCommitted(const QString &written)
{
    // our own writes echo back through here; only a commit the user made
    // advances the tour
    if (inserting) return;

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
            // leave the line pending so they can correct it in place
            emit offerCommands(QStringList(), engine->currentStep()->section);
            return;
        }
        coach->setFeedback(QStringLiteral("That is it."), true);
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
    // a failed run keeps the user where the failure is; only a clean one moves
    // the tour on to look at the results
    if (success) engine->next();
}

void TutorialView::goNext()
{
    // an offered but unaccepted line is withdrawn rather than left behind
    emit withdrawCommand();

    // Next accepts exactly the group that is on offer and no more.  It used to
    // dump every remaining command of the step at once, which is how commands
    // arrived in the script having never been shown or explained.
    if (!engine->nextGroup().isEmpty()) {
        const TutorialStep *step = engine->currentStep();
        const QStringList texts  = engine->takeNextGroup();
        // writing a line goes through the editor's pending-line machinery, so
        // it comes back as pendingLineCommitted -- which is also how the user
        // accepts a line with Tab.  Without this guard our own insertion is
        // mistaken for the user accepting the *next* group, and that group is
        // written having never been offered.
        const InsertGuard guard(inserting);
        for (const auto &text : texts)
            emit insertCommand(text, step ? step->section : QString());
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

void TutorialView::rewind(const QString &stepId)
{
    const QStringList lines = engine->writtenFor(stepId);
    // last written, first removed: undoing in reverse keeps the matches honest
    for (int i = lines.size() - 1; i >= 0; --i)
        emit retractCommand(lines.at(i));
    engine->forgetWritten(stepId);
}

// Local Variables:
// c-basic-offset: 4
// End:
