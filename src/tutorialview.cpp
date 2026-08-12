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
    coach->setContent(QStringLiteral("%1 -- %2").arg(engine->content().title(), act.title),
                      step->title, renderText(step->teach));
    coach->setProgress(engine->stepsCompleted() + 1, engine->content().stepCount());
    coach->setBackEnabled(engine->stepsCompleted() > 0);
    coach->setNextText(QStringLiteral("&Next >"));
    coach->setNextEnabled(true);

    // a step with commands hands them to the editor one at a time; the callout
    // itself never shows code, it only says what to do with it
    if (const CommandLine *cmd = engine->nextCommand()) {
        if (cmd->typed) {
            // reinforcement: the line is described but never written, so the
            // user has to produce it themselves.  An empty pending line marks
            // where it goes and gives Tab something to check.
            emit offerCommand(QString(), step->section);
            coach->setCallToAction(
                QStringLiteral("Type it yourself on the highlighted line, then press Tab."));
        } else {
            emit offerCommand(cmd->text, step->section);
            coach->setCallToAction(QStringLiteral("Press Tab in the editor to accept this line."));
        }
    } else {
        coach->setCallToAction(step->callToAction);
    }
    coach->setFeedback(QString(), true);

    reposition();
}

void TutorialView::commandCommitted(const QString &written)
{
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
            emit offerCommand(QString(), engine->currentStep()->section);
            return;
        }
        coach->setFeedback(QStringLiteral("That is it."), true);
    }

    engine->takeNextCommand();
    // accepting the last line of a step finishes it: waiting for a separate
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

    // the user chose to move on rather than accept each line by hand: the
    // remaining lines still have to reach the script, or the next run would not
    // work.  Then advance -- staying on the step is indistinguishable from the
    // button being broken.
    while (const CommandLine *cmd = engine->nextCommand()) {
        emit insertCommand(cmd->text, engine->currentStep()->section);
        engine->takeNextCommand();
    }
    engine->next();
}

void TutorialView::goBack()
{
    emit withdrawCommand();
    engine->previous();
}

// Local Variables:
// c-basic-offset: 4
// End:
