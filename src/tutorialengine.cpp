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

#include "tutorialengine.h"

#include "constants.h"

#include <QSettings>

TutorialEngine::TutorialEngine(const TutorialContent &content, const LammpsSyntax *syntax,
                               QObject *parent) :
    QObject(parent), tutorial(content), evaluator(syntax)
{
    // start on the first step that expert mode allows, so enabling it before
    // the first stepChanged() does not leave the cursor on a skipped step
    if (expert && currentStep() && !currentStep()->checkpoint) advance(1);
}

/* -------------------------------------------------------------------- */

const TutorialStep *TutorialEngine::currentStep() const
{
    return tutorial.step(act, step);
}

int TutorialEngine::stepsCompleted() const
{
    int done = 0;
    for (int a = 0; a < act && a < tutorial.actCount(); ++a)
        done += static_cast<int>(tutorial.acts().at(a).steps.size());
    return done + step;
}

bool TutorialEngine::hasMoreHints() const
{
    const TutorialStep *s = currentStep();
    return s && shownHints < s->hints.size();
}

bool TutorialEngine::revealAvailable() const
{
    const TutorialStep *s = currentStep();
    if (!s || s->reveal.isEmpty()) return false;
    // either they worked through the ladder, or they have been wrong enough
    // times that leaving them stuck would be the only thing we achieved
    return !hasMoreHints() || wrongAttempts >= REVEAL_AFTER;
}

bool TutorialEngine::canCheckNow() const
{
    const TutorialStep *s = currentStep();
    if (!s) return false;
    switch (s->verb) {
        case StepVerb::Read:
        case StepVerb::Inspect:
            return false;
        case StepVerb::Fix:
            // gated by parses_clean, which needs the LAMMPS probe (phase 3)
            return false;
        case StepVerb::Tune:
            // gated by a real thermo observation, which needs a completed run
            return false;
        case StepVerb::Type:
        case StepVerb::Fill:
        case StepVerb::Predict:
            return true;
    }
    return false;
}

/* -------------------------------------------------------------------- */

bool TutorialEngine::advance(int direction)
{
    int a = act;
    int s = step;

    while (true) {
        s += direction;
        if (s < 0) {
            if (a == 0) return false;
            --a;
            s = static_cast<int>(tutorial.acts().at(a).steps.size()) - 1;
            if (s < 0) continue;
        } else if (a < tutorial.actCount() &&
                   s >= static_cast<int>(tutorial.acts().at(a).steps.size())) {
            if (a + 1 >= tutorial.actCount()) {
                // past the final step: park the cursor there so isFinished() holds
                act  = tutorial.actCount() - 1;
                step = static_cast<int>(tutorial.acts().at(act).steps.size());
                return true;
            }
            ++a;
            s = -1;
            continue;
        }

        const TutorialStep *candidate = tutorial.step(a, s);
        if (!candidate) return false;
        // expert mode stops only at checkpoints; it hides nothing, it just
        // stops asking a user who does not need to be asked
        if (!expert || candidate->checkpoint) {
            act  = a;
            step = s;
            return true;
        }
    }
}

void TutorialEngine::resetStepState()
{
    wrongAttempts = 0;
    shownHints    = 0;
}

void TutorialEngine::next()
{
    if (isFinished()) return;
    const bool moved = advance(1);
    resetStepState();
    if (moved) emit stepChanged();
    if (isFinished()) emit tutorialFinished();
}

void TutorialEngine::previous()
{
    if (!advance(-1)) return;
    resetStepState();
    emit stepChanged();
}

void TutorialEngine::skip()
{
    // deliberately identical to next(): skipping costs nothing and is recorded
    // nowhere, which is what makes the rest of the tutorial feel non-coercive
    next();
}

void TutorialEngine::goToStep(const QString &id)
{
    for (int a = 0; a < tutorial.actCount(); ++a) {
        const auto &steps = tutorial.acts().at(a).steps;
        for (int s = 0; s < steps.size(); ++s) {
            if (steps.at(s).id != id) continue;
            act  = a;
            step = s;
            resetStepState();
            emit stepChanged();
            return;
        }
    }
}

QStringList TutorialEngine::nextHint()
{
    const TutorialStep *s = currentStep();
    if (!s) return {};

    if (shownHints < s->hints.size()) ++shownHints;

    QStringList shown;
    for (int i = 0; i < shownHints; ++i)
        shown << s->hints.at(i);
    return shown;
}

/* -------------------------------------------------------------------- */

void TutorialEngine::attachFeedback(StepResult &result) const
{
    const TutorialStep *s = currentStep();
    if (!s) return;

    // a choice carries the chosen option's own explanation, which the
    // evaluator already filled in; do not overwrite it
    if (!result.feedback.isEmpty()) return;

    if (result.verdict == Verdict::Correct) {
        result.feedback = s->feedback.correct;
        return;
    }
    if (result.verdict == Verdict::Unresolved) return;

    // a numeric miss can say something more useful than "wrong"
    if (result.message.contains(QStringLiteral("below")) && !s->feedback.below.isEmpty())
        result.feedback = s->feedback.below;
    else if (result.message.contains(QStringLiteral("above")) && !s->feedback.above.isEmpty())
        result.feedback = s->feedback.above;
    else
        result.feedback = s->feedback.wrong;
}

StepResult TutorialEngine::submitLine(const QString &line)
{
    StepResult res;
    const TutorialStep *s = currentStep();
    if (!s) return res;

    res = evaluator.evaluateLine(*s, line);
    // an undecidable answer is not a wrong one, so it must not count against
    // the user or push them towards the reveal
    if (res.verdict == Verdict::Incorrect) ++wrongAttempts;
    attachFeedback(res);
    emit verdictReady(res);
    return res;
}

StepResult TutorialEngine::submitHoles(const QStringList &values)
{
    StepResult res;
    const TutorialStep *s = currentStep();
    if (!s) return res;

    res = evaluator.evaluateHoles(*s, values);
    if (res.verdict == Verdict::Incorrect) ++wrongAttempts;
    attachFeedback(res);
    emit verdictReady(res);
    return res;
}

StepResult TutorialEngine::submitChoice(int option)
{
    StepResult res;
    const TutorialStep *s = currentStep();
    if (!s) return res;

    res = evaluator.evaluateChoice(*s, option);
    if (res.verdict == Verdict::Incorrect) ++wrongAttempts;
    attachFeedback(res);
    emit verdictReady(res);
    return res;
}

/* -------------------------------------------------------------------- */

void TutorialEngine::saveProgress() const
{
    if (tutorial.id().isEmpty()) return;

    QSettings settings;
    settings.beginGroup(Keys::GROUP_TUTORIAL);
    settings.beginGroup(tutorial.id());
    const TutorialStep *s = currentStep();
    // the step id, not the index: inserting or reordering content must never
    // resume a returning user somewhere else entirely
    settings.setValue(Keys::PROGRESS_STEP, s ? s->id : QString());
    settings.setValue(Keys::EXPERTMODE, expert);
    settings.endGroup();
    settings.endGroup();
}

void TutorialEngine::restoreProgress()
{
    if (tutorial.id().isEmpty()) return;

    QSettings settings;
    settings.beginGroup(Keys::GROUP_TUTORIAL);
    settings.beginGroup(tutorial.id());
    const QString id = settings.value(Keys::PROGRESS_STEP).toString();
    expert           = settings.value(Keys::EXPERTMODE, false).toBool();
    settings.endGroup();
    settings.endGroup();

    if (id.isEmpty()) return;
    // a saved id that no longer exists means the content changed under the
    // user; starting over is the only honest option
    if (!tutorial.stepById(id)) return;
    goToStep(id);
}

void TutorialEngine::resetProgress()
{
    if (!tutorial.id().isEmpty()) {
        QSettings settings;
        settings.beginGroup(Keys::GROUP_TUTORIAL);
        settings.remove(tutorial.id());
        settings.endGroup();
    }
    act  = 0;
    step = 0;
    resetStepState();
    emit stepChanged();
}

// Local Variables:
// c-basic-offset: 4
// End:
