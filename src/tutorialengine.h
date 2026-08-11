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
#include "tutorialeval.h"

#include <QObject>
#include <QString>
#include <QStringList>

class LammpsSyntax;

/**
 * @brief Drives one interactive tutorial: cursor, verdicts, and progress
 *
 * Owns the parsed content and an evaluator, tracks where the user is, and
 * decides what the panel is allowed to offer.  It holds no widgets, so the
 * rules that matter -- when a reveal becomes available, what expert mode
 * skips, where progress resumes -- are unit-testable without a GUI.
 *
 * Two policies are deliberately encoded here rather than left to the view:
 *
 * - **Nothing is ever locked.** Skip is available on every skippable step and
 *   is never punished, and a wrong answer never blocks or resets anything.
 * - **A stuck user always has a way out.** After @ref REVEAL_AFTER wrong
 *   attempts the reveal becomes available unprompted, with its explanation.
 */
class TutorialEngine : public QObject {
    Q_OBJECT

public:
    /// wrong attempts after which the reveal is offered without being asked for
    static constexpr int REVEAL_AFTER = 3;

    /**
     * @brief Constructor
     * @param content Parsed tutorial to drive (copied)
     * @param syntax Style registry for style_valid rules (not owned, may be null)
     * @param parent Parent object
     */
    explicit TutorialEngine(const TutorialContent &content, const LammpsSyntax *syntax = nullptr,
                            QObject *parent = nullptr);

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

    /** @brief Wrong attempts made on the current step */
    int attempts() const { return wrongAttempts; }
    /** @brief Hint ladder rungs currently visible */
    int hintsShown() const { return shownHints; }
    /** @brief True when the step has another hint to give */
    bool hasMoreHints() const;
    /**
     * @brief True when the reveal may be shown
     *
     * Either the user has worked through every hint, or they have been wrong
     * @ref REVEAL_AFTER times and should be offered the answer unasked.
     */
    bool revealAvailable() const;

    /** @brief True when the current step can be judged without LAMMPS */
    bool canCheckNow() const;

    /**
     * @brief Whether only checkpoints are visited
     *
     * Expert mode is the escape hatch for a user who does not need the
     * step-by-step path; it never hides content, it only stops asking.
     */
    bool expertMode() const { return expert; }
    /** @brief Enable or disable expert mode */
    void setExpertMode(bool enable) { expert = enable; }

public slots:
    /** @brief Advance to the next step, honouring expert mode */
    void next();
    /** @brief Step backwards, honouring expert mode */
    void previous();
    /** @brief Skip the current step; identical to next(), and never punished */
    void skip();
    /** @brief Jump to a step by its id; no-op when the id is unknown */
    void goToStep(const QString &id);
    /** @brief Reveal the next hint rung and return everything shown so far */
    QStringList nextHint();

    /**
     * @brief Judge a whole typed command
     * @param line what the user typed
     * @return the verdict, with the authored feedback filled in
     */
    StepResult submitLine(const QString &line);

    /**
     * @brief Judge the values filled into a skeleton's holes
     * @param values one value per "___" hole, in order
     * @return the verdict, with the authored feedback filled in
     */
    StepResult submitHoles(const QStringList &values);

    /**
     * @brief Judge a prediction
     * @param option 0-based index into the step's options
     * @return the verdict; the feedback is the chosen option's own explanation
     */
    StepResult submitChoice(int option);

    /** @brief Persist the cursor so the next session resumes here */
    void saveProgress() const;
    /** @brief Restore a previously saved cursor, if the step still exists */
    void restoreProgress();
    /** @brief Forget saved progress and return to the first step */
    void resetProgress();

signals:
    /** @brief The cursor moved; the view should rebuild itself */
    void stepChanged();
    /** @brief An answer was judged */
    void verdictReady(const StepResult &result);
    /** @brief The cursor passed the final step */
    void tutorialFinished();

private:
    /// move the cursor one step forward or back, returns false at the ends
    bool advance(int direction);
    /// clear the per-step counters after the cursor moves
    void resetStepState();
    /// fill in the authored feedback text that matches a verdict
    void attachFeedback(StepResult &result) const;

    TutorialContent tutorial;    ///< the tutorial being driven
    TutorialEvaluator evaluator; ///< judges answers syntactically
    int act           = 0;       ///< 0-based act cursor
    int step          = 0;       ///< 0-based step cursor within the act
    int wrongAttempts = 0;       ///< wrong answers on the current step
    int shownHints    = 0;       ///< hint rungs revealed on the current step
    bool expert       = false;   ///< visit only checkpoints
};

#endif // TUTORIALENGINE_H

// Local Variables:
// c-basic-offset: 4
// End:
