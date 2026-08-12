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

TutorialEngine::TutorialEngine(const TutorialContent &content, QObject *parent) :
    QObject(parent), tutorial(content)
{
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

const CommandLine *TutorialEngine::nextCommand() const
{
    const TutorialStep *s = currentStep();
    if (!s || inserted >= s->commands.size()) return nullptr;
    return &s->commands.at(inserted);
}

bool TutorialEngine::allCommandsInserted() const
{
    const TutorialStep *s = currentStep();
    return s && inserted >= s->commands.size();
}

/* -------------------------------------------------------------------- */

bool TutorialEngine::shouldExplain(const QString &id) const
{
    if (id.isEmpty()) return false;
    return exposures.value(id, 0) < Cfg::CONCEPT_REMINDER_BUDGET;
}

bool TutorialEngine::firstUseOf(const QString &command) const
{
    return !command.isEmpty() && !commandsSeen.contains(command);
}

void TutorialEngine::noteCommandShown(const QString &command)
{
    if (!command.isEmpty()) commandsSeen.insert(command);
}

bool TutorialEngine::hasSavedProgress() const
{
    if (tutorial.id().isEmpty()) return false;
    QSettings settings;
    settings.beginGroup(Keys::GROUP_TUTORIAL);
    settings.beginGroup(tutorial.id());
    const QString id = settings.value(Keys::PROGRESS_STEP).toString();
    settings.endGroup();
    settings.endGroup();
    // the first step is not progress worth offering to resume
    const TutorialStep *first = tutorial.step(0, 0);
    return !id.isEmpty() && tutorial.stepById(id) && (!first || id != first->id);
}

void TutorialEngine::noteConceptsShown(const QStringList &ids)
{
    for (const auto &id : ids) {
        if (id.isEmpty()) continue;
        // once per step, not once per repaint: stepping back and forth over a
        // step must not exhaust the budget for a concept seen only once
        if (countedThisStep.contains(id)) continue;
        countedThisStep.insert(id);
        exposures[id] = exposures.value(id, 0) + 1;
    }
}

/* -------------------------------------------------------------------- */

void TutorialEngine::resetStepState()
{
    inserted = 0;
    countedThisStep.clear();
}

void TutorialEngine::next()
{
    if (isFinished()) return;

    const auto &steps = tutorial.acts().at(act).steps;
    if (step + 1 < steps.size()) {
        ++step;
    } else if (act + 1 < tutorial.actCount()) {
        ++act;
        step = 0;
    } else {
        // park the cursor past the final step so isFinished() holds
        ++step;
    }
    resetStepState();
    emit stepChanged();
    if (isFinished()) emit tutorialFinished();
}

void TutorialEngine::previous()
{
    if (step > 0) {
        --step;
    } else if (act > 0) {
        --act;
        step = qMax(static_cast<int>(tutorial.acts().at(act).steps.size()) - 1, 0);
    } else {
        return;
    }
    resetStepState();
    emit stepChanged();
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

QString TutorialEngine::takeNextCommand()
{
    const CommandLine *cmd = nextCommand();
    if (!cmd) return {};
    const QString text = cmd->text;
    ++inserted;
    // remember what we put in the script, so stepping back can take it out
    if (const TutorialStep *s = currentStep()) written[s->id].append(text);
    emit commandInserted(text);
    return text;
}

/* -------------------------------------------------------------------- */

void TutorialEngine::saveProgress() const
{
    if (tutorial.id().isEmpty()) return;

    QSettings settings;
    settings.beginGroup(Keys::GROUP_TUTORIAL);

    // the cursor is per tutorial, keyed by the step *id* rather than an index,
    // so inserting or reordering content cannot resume someone in the wrong place
    settings.beginGroup(tutorial.id());
    const TutorialStep *s = currentStep();
    settings.setValue(Keys::PROGRESS_STEP, s ? s->id : QString());
    settings.endGroup();

    // the concept budget is per user and deliberately shared across tutorials:
    // something learned in tutorial 1 should not be re-taught in tutorial 2
    settings.beginGroup(Keys::GROUP_CONCEPTS);
    for (auto it = exposures.constBegin(); it != exposures.constEnd(); ++it)
        settings.setValue(it.key(), it.value());
    settings.endGroup();

    // which commands have been explained is per tutorial, not per user: the
    // same command met again in a different tutorial deserves its explanation
    settings.beginGroup(tutorial.id());
    settings.setValue(Keys::COMMANDS_SEEN, QStringList(commandsSeen.begin(), commandsSeen.end()));
    settings.endGroup();

    settings.endGroup();
}

void TutorialEngine::restoreProgress()
{
    QSettings settings;
    settings.beginGroup(Keys::GROUP_TUTORIAL);

    settings.beginGroup(Keys::GROUP_CONCEPTS);
    exposures.clear();
    for (const auto &key : settings.childKeys())
        exposures.insert(key, settings.value(key).toInt());
    settings.endGroup();

    QString id;
    if (!tutorial.id().isEmpty()) {
        settings.beginGroup(tutorial.id());
        id                     = settings.value(Keys::PROGRESS_STEP).toString();
        const QStringList seen = settings.value(Keys::COMMANDS_SEEN).toStringList();
        commandsSeen           = QSet<QString>(seen.begin(), seen.end());
        settings.endGroup();
    }
    settings.endGroup();

    // a saved id that no longer exists means the content changed under the
    // user; starting over is the only honest option
    if (!id.isEmpty() && tutorial.stepById(id)) goToStep(id);
}

void TutorialEngine::resetProgress()
{
    QSettings settings;
    settings.beginGroup(Keys::GROUP_TUTORIAL);
    if (!tutorial.id().isEmpty()) settings.remove(tutorial.id());
    // reminders reset too: a user asking to start over usually means it
    settings.remove(Keys::GROUP_CONCEPTS);
    settings.endGroup();

    exposures.clear();
    commandsSeen.clear();
    written.clear();
    act  = 0;
    step = 0;
    resetStepState();
    emit stepChanged();
}

// Local Variables:
// c-basic-offset: 4
// End:
