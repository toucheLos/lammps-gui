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

#ifndef TUTORIALEVAL_H
#define TUTORIALEVAL_H

#include "tutorialcontent.h"

#include <QString>
#include <QStringList>

class LammpsSyntax;

/**
 * @brief Outcome of judging one submitted answer
 */
enum class Verdict : quint8 {
    Correct,   ///< the answer satisfies every rule that could be checked
    Incorrect, ///< a rule rejected the answer; @ref StepResult::position says which
    Unresolved ///< nothing could be decided statically, e.g. a "$" substitution
};

/**
 * @brief The verdict on one submitted answer, with the reason
 *
 * A rejection has to name the argument that is wrong: "argument 3 should be a
 * cutoff distance; you gave a style name" teaches something, "incorrect" does
 * not.  The @ref message is that explanation, generated from the rule; the
 * @ref feedback is the tutorial author's own text for this outcome.
 */
struct StepResult {
    Verdict verdict = Verdict::Incorrect; ///< the outcome
    int position    = -1;    ///< 0-based rule, hole, or token index at fault (-1 if not positional)
    QString message;         ///< generated explanation naming the offending argument
    QString feedback;        ///< the authored feedback text that applies to this outcome
    bool needsParse = false; ///< the answer still has to survive the LAMMPS probe (phase 3)
};

/**
 * @brief Split a line into canonical LAMMPS words
 *
 * Runs the line through the syntax engine's InputScanner, so comments,
 * quoting, and "&" continuations are handled exactly the way LAMMPS handles
 * them rather than by an ad-hoc split.  Surrounding quotes are stripped and
 * leading, trailing, and repeated whitespace collapses out as a side effect
 * of tokenizing.
 *
 * @param line one or more physical lines of LAMMPS input
 * @return the words of the first logical command, empty if there is none
 */
QStringList canonicalWords(const QString &line);

/**
 * @brief Parse a word the way LAMMPS parses a number
 *
 * Accepts what the LAMMPS input parser accepts, including the Fortran style
 * "d" exponent (@c 1.0d-3), so @c 2.5 , @c 2.50 , @c 2.5e0 and @c 2.5d0 all
 * compare equal.
 *
 * @param word word to parse
 * @param value receives the value on success
 * @return true if the word is a number
 */
bool parseLammpsNumber(const QString &word, double &value);

/**
 * @brief True when a word contains a "$" variable reference
 *
 * Such a word cannot be judged before LAMMPS expands it, so it yields
 * Verdict::Unresolved rather than a wrong answer.
 */
bool hasSubstitution(const QString &word);

/**
 * @brief Judges submitted answers against a step's rules
 *
 * Purely syntactic: it compares words against the authored rules and never
 * talks to LAMMPS.  Where a step also demands that the real parser accept the
 * command, the result carries StepResult::needsParse for the caller to follow
 * up with the probe.
 *
 * The style registry is optional; without it, style_valid rules cannot be
 * decided and yield Verdict::Unresolved instead of guessing.
 */
class TutorialEvaluator {
public:
    /**
     * @brief Constructor
     * @param syntax style registry used by style_valid rules (not owned, may be null)
     */
    explicit TutorialEvaluator(const LammpsSyntax *syntax = nullptr) : syntax(syntax) {}
    ~TutorialEvaluator() = default;

    TutorialEvaluator(const TutorialEvaluator &)            = delete;
    TutorialEvaluator(TutorialEvaluator &&)                 = delete;
    TutorialEvaluator &operator=(const TutorialEvaluator &) = delete;
    TutorialEvaluator &operator=(TutorialEvaluator &&)      = delete;

    /**
     * @brief Judge a whole command line, as a TYPE or FIX step submits it
     * @param step the step being answered
     * @param line the command the user typed
     * @return the verdict
     */
    StepResult evaluateLine(const TutorialStep &step, const QString &line) const;

    /**
     * @brief Judge the values filled into a skeleton's "___" holes
     * @param step the step being answered
     * @param values one value per hole, in skeleton order
     * @return the verdict
     */
    StepResult evaluateHoles(const TutorialStep &step, const QStringList &values) const;

    /**
     * @brief Judge a PREDICT answer
     * @param step the step being answered
     * @param option 0-based index into the step's options
     * @return the verdict; the feedback is the chosen option's own text, because
     *         both a right and a wrong prediction have something to teach
     */
    StepResult evaluateChoice(const TutorialStep &step, int option) const;

    /**
     * @brief Apply one rule to one word
     * @param rule the rule to apply
     * @param word the word to judge
     * @param position 0-based index reported in the result
     * @return the verdict for this position alone
     */
    StepResult applyRule(const TutorialRule &rule, const QString &word, int position) const;

private:
    /// name a position for a message: the rule's label, or "argument N"
    static QString positionName(const TutorialRule &rule, int position);

    const LammpsSyntax *syntax; ///< style registry (not owned)
};

#endif // TUTORIALEVAL_H

// Local Variables:
// c-basic-offset: 4
// End:
