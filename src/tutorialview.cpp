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

#include <QRegularExpression>
#include <QWidget>

TutorialView::TutorialView(TutorialEngine *engine, QWidget *host) :
    QObject(host), engine(engine), host(host)
{
    spotlight = new TutorialSpotlight(host);
    coach     = new TutorialCoach(spotlight);

    connect(coach, &TutorialCoach::nextRequested, this, &TutorialView::goNext);
    connect(coach, &TutorialCoach::backRequested, this, &TutorialView::goBack);
    connect(engine, &TutorialEngine::stepChanged, this, &TutorialView::showCurrentStep);
}

TutorialView::~TutorialView()
{
    // the widgets are children of the host, so Qt would eventually delete them,
    // but the coach mark has to disappear the moment the tour ends
    delete spotlight;
}

void TutorialView::setAnchorResolver(std::function<QWidget *(StepAnchor)> resolver)
{
    resolve = std::move(resolver);
}

void TutorialView::start()
{
    spotlight->setGeometry(host->rect());
    spotlight->show();
    spotlight->raise();
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

    QWidget *target = resolve ? resolve(currentAnchor()) : nullptr;
    QRect targetRect;
    if (target && target->isVisible()) {
        // into the spotlight's coordinates, which match the host's
        targetRect = QRect(target->mapTo(host, QPoint(0, 0)), target->size());
        // clip to what is actually on screen, so an anchor inside a scrolled or
        // partly hidden view still produces a sane ring
        targetRect = targetRect.intersected(host->rect());
    }
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
        emit offerCommand(cmd->text);
        coach->setCallToAction(QStringLiteral("Press Tab in the editor to accept this line."));
    } else {
        coach->setCallToAction(step->callToAction);
    }

    reposition();
}

void TutorialView::commandCommitted()
{
    engine->takeNextCommand();
    // offers the next line of the same step, or moves on to what the step
    // wants the user to look at once the script is complete
    showCurrentStep();
}

void TutorialView::goNext()
{
    // an offered but unaccepted line is withdrawn rather than left behind
    emit withdrawCommand();
    if (engine->nextCommand()) {
        // the user chose to move on rather than accept each line: the remaining
        // lines still have to reach the script, or the next run would not work
        while (const CommandLine *cmd = engine->nextCommand()) {
            emit insertCommand(cmd->text);
            engine->takeNextCommand();
        }
        showCurrentStep();
        return;
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
