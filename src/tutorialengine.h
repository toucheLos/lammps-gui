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

#ifndef TUTORIALENGINE_H
#define TUTORIALENGINE_H

#include "tutorialcontent.h"

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

/**
 * @brief Drives one interactive tutorial: cursor, reminders, and progress
 *
 * Owns the parsed content, tracks where the user is, and decides how much
 * explanation a given concept still deserves.  It holds no widgets, so the
 * rules that matter are unit-testable without a GUI.
 *
 * Two policies live here rather than in the view:
 *
 * - **Nothing is ever locked.** The user can move forward or back at any
 *   point; no step gates on an answer, because the tutorial shows the
 *   commands rather than quizzing for them.
 * - **Explanations fade.** A concept is explained in full the first
 *   @ref Cfg::CONCEPT_REMINDER_BUDGET times it is met and collapses after
 *   that, so a returning user is not re-taught what they already know.
 */
class TutorialEngine : public QObject {
    Q_OBJECT

public:
    /**
     * @brief Constructor
     * @param content Parsed tutorial to drive (copied)
     * @param parent Parent object
     */
    explicit TutorialEngine(const TutorialContent &content, QObject *parent = nullptr);

    /** @brief Destructor */
    ~TutorialEngine() override = default;

    TutorialEngine()                                  = delete;
    TutorialEngine(const TutorialEngine &)            = delete;
    TutorialEngine(TutorialEngine &&)                 = delete;
    TutorialEngine &operator=(const TutorialEngine &) = delete;
    TutorialEngine &operator=(TutorialEngine &&)      = delete;

    /** @brief The tutorial being driven */
    const TutorialContent &content() const { return tutorial; }

    /** @brief The step the cursor points at, or nullptr past the end */
    const TutorialStep *currentStep() const;

    /** @brief 0-based act index of the cursor */
    int actIndex() const { return act; }
    /** @brief 0-based step index within the act */
    int stepIndex() const { return step; }
    /** @brief Number of steps before the cursor, for a progress bar */
    int stepsCompleted() const;
    /** @brief True once the cursor has passed the last step */
    bool isFinished() const { return currentStep() == nullptr; }

    /**
     * @brief How many of the current step's command lines have been inserted
     *
     * Commands are offered one at a time, so the panel shows the next one and
     * the step is done when this reaches the step's command count.
     */
    int insertedCount() const { return inserted; }
    /** @brief The command line the panel should offer next, or nullptr */
    const CommandLine *nextCommand() const;
    /** @brief True when every command of the current step has been inserted */
    bool allCommandsInserted() const;

    /**
     * @brief Whether this is the first time a command has appeared
     * @param command the command word, e.g. "region"
     * @return true until the command has been shown once
     *
     * Commands get a budget of one rather than the concept budget: a command
     * is explained thoroughly when it first appears and not again, because by
     * the sixth `region` the explanation is noise.
     */
    bool firstUseOf(const QString &command) const;

    /** @brief Record that a command's explanation has now been shown */
    void noteCommandShown(const QString &command);

    /**
     * @brief The lines the tour has written for a step
     * @param stepId step to ask about
     */
    QStringList writtenFor(const QString &stepId) const { return written.value(stepId); }

    /** @brief Forget what was written for a step, after removing it from the editor */
    void forgetWritten(const QString &stepId) { written.remove(stepId); }

    /**
     * @brief Whether the saved progress points past the very first step
     *
     * Used to decide whether resuming is worth offering at all.
     */
    bool hasSavedProgress() const;

    /**
     * @brief Whether a concept's explanation should still be shown in full
     * @param id concept id referenced by an annotation
     * @return true while the user has met it fewer than the budget times
     *
     * A collapsed concept is not hidden: the panel keeps it reachable on
     * demand, it simply stops taking up room and attention.
     */
    bool shouldExplain(const QString &id) const;

    /**
     * @brief Times the user has been shown a concept's explanation
     * @param id concept id
     */
    int exposureCount(const QString &id) const { return exposures.value(id, 0); }

    /**
     * @brief Record that the current step showed these concepts
     *
     * Counted once per step rather than once per repaint, so revisiting a step
     * with Back does not burn the budget.
     */
    void noteConceptsShown(const QStringList &ids);

public slots:
    /** @brief Advance to the next step */
    void next();
    /** @brief Step backwards */
    void previous();
    /** @brief Jump to a step by its id; no-op when the id is unknown */
    void goToStep(const QString &id);

    /**
     * @brief Record that the panel inserted the next command
     * @return the command that was consumed, or an empty string at the end
     */
    QString takeNextCommand();

    /** @brief Persist the cursor and the concept budget */
    void saveProgress() const;
    /** @brief Restore a previously saved cursor and concept budget */
    void restoreProgress();
    /** @brief Forget saved progress and start over, reminders included */
    void resetProgress();

signals:
    /** @brief The cursor moved; the view should rebuild itself */
    void stepChanged();
    /** @brief A command was inserted; the view should offer the next one */
    void commandInserted(const QString &text);
    /** @brief The cursor passed the final step */
    void tutorialFinished();

private:
    /// clear the per-step counters after the cursor moves
    void resetStepState();

    TutorialContent tutorial;      ///< the tutorial being driven
    int act      = 0;              ///< 0-based act cursor
    int step     = 0;              ///< 0-based step cursor within the act
    int inserted = 0;              ///< command lines of this step already inserted
    QHash<QString, int> exposures; ///< per-concept exposure counts
    QSet<QString> countedThisStep; ///< concepts already counted for the current step
    QSet<QString> commandsSeen;    ///< command words already explained
    /// what the tour has written into the editor, keyed by step id, so that
    /// stepping back can take it out again
    QHash<QString, QStringList> written;
};

#endif // TUTORIALENGINE_H

// Local Variables:
// c-basic-offset: 4
// End:
