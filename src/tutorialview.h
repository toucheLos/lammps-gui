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

#ifndef TUTORIALVIEW_H
#define TUTORIALVIEW_H

#include "tutorialcoach.h"
#include "tutorialcontent.h"

#include <QObject>
#include <QRect>

#include <QPointer>
#include <QString>
#include <functional>

class TutorialEngine;
class QWidget;

/**
 * @brief Drives the coach mark around the main window
 *
 * Owns the spotlight layer and the callout, resolves each step's anchor to a
 * live widget, positions the callout beside it, and rings it.  It is a
 * QObject rather than a widget: the widgets it manages are children of the
 * main window, and this only decides what they show and where they sit.
 *
 * Anchors are resolved through the caller's lookup on every step rather than
 * held as pointers.  The snapshot viewer in particular is deleted and rebuilt
 * on every render, so a cached pointer would dangle.
 */
class TutorialView : public QObject {
    Q_OBJECT

public:
    /**
     * @brief Constructor
     * @param engine Engine driving the tutorial; must outlive this
     * @param host Main window the coach mark lives inside
     */
    TutorialView(TutorialEngine *engine, QWidget *host);

    /** @brief Destructor; removes the coach mark from the host */
    ~TutorialView() override;

    TutorialView()                                = delete;
    TutorialView(const TutorialView &)            = delete;
    TutorialView(TutorialView &&)                 = delete;
    TutorialView &operator=(const TutorialView &) = delete;
    TutorialView &operator=(TutorialView &&)      = delete;

    /**
     * @brief Supply the area an anchor names
     * @param resolver called with an anchor, returns the rectangle to ring, in
     *        the host's coordinates; an empty rectangle means "nothing to point
     *        at" and parks the callout in the corner
     *
     * A rectangle rather than a widget, because the most common anchor is a
     * single line of the editor rather than the editor itself.  The caller owns
     * the mapping: only the main window knows where its views currently are,
     * and whether they exist at all.
     */
    void setAnchorResolver(std::function<QRect(StepAnchor)> resolver);

    /** @brief Show the coach mark and display the current step */
    void start();

signals:
    /**
     * @brief Put the step's command into the editor as a pending line
     * @param text the command to offer
     *
     * Pending means visible and highlighted but not yet accepted; the user
     * commits it with Tab, or it is withdrawn when they move on.
     */
    void offerCommand(const QString &text, const QString &section);

    /**
     * @brief Offer a whole group of commands at once
     * @param texts the commands, in order; empty offers one blank line
     * @param section heading to file them under
     *
     * Commands serving one purpose are shown, highlighted and accepted
     * together, so nothing reaches the script without being visible first.
     */
    void offerCommands(const QStringList &texts, const QString &section);

    /** @brief Withdraw a pending command that was never committed */
    void withdrawCommand();

    /**
     * @brief Take a line the tour wrote back out of the script
     * @param text the line as it was written
     */
    void retractCommand(const QString &text);

    /**
     * @brief Put a command straight into the script, already committed
     * @param text the command to insert
     *
     * Used when the user skips ahead: the remaining lines of the step still
     * have to reach the script, or what they run next would not work.
     */
    void insertCommand(const QString &text, const QString &section);

    /**
     * @brief Open a different input file
     * @param name file name relative to the tutorial's working directory
     */
    void openFileRequested(const QString &name);

    /** @brief Write the section headings into an empty editor */
    void seedSkeleton(const QStringList &lines);

    /** @brief The tour reached its end */
    void finished();

public slots:
    /** @brief Rebuild the callout from the engine's cursor */
    void showCurrentStep();
    /** @brief Reposition the callout; call when the host resizes */
    void reposition();
    /**
     * @brief The user accepted the pending line
     * @param written the text of the line as it now stands
     *
     * For a line the user was asked to type, this is where it is checked; for
     * one they merely accepted, the text is the command that was offered.
     */
    void commandCommitted(const QString &written);

    /**
     * @brief A simulation run finished
     * @param success true when LAMMPS reported no error
     *
     * A step that hands the user the Run button waits here rather than on
     * Next, so the tour follows the result to the chart on its own.  A failed
     * run does not advance: the user is left looking at the error, which is
     * where the interesting thing just happened.
     */
    void runFinished(bool success);

private slots:
    void goNext(); ///< advance, withdrawing anything uncommitted
    void goBack(); ///< step back, withdrawing anything uncommitted

private:
    /// render the markdown subset (bold, italic, inline code) as rich text
    static QString renderText(const QString &text);
    /// the anchor the current step asks for
    StepAnchor currentAnchor() const;
    /// take every line the tour wrote for a step back out of the script, and
    /// restore anything that step displaced
    void rewind(const QString &stepId);
    /// remove the lines a group supersedes, recording them for the rewind
    void retractSuperseded(const QList<CommandLine> &group);

    /**
     * @brief RAII flag marking a write the tour made itself
     *
     * Insertion runs through the same pending-line machinery the user drives
     * with Tab, so the editor cannot tell the two apart and reports both as
     * CodeEditor::pendingLineCommitted.  The flag lets commandCommitted()
     * ignore the echo of its own writes; without it each inserted line looked
     * like the user accepting the following group, which was then written
     * without ever having been offered.
     */
    class InsertGuard {
    public:
        explicit InsertGuard(bool &flag) : ref(flag) { ref = true; }
        ~InsertGuard() { ref = false; }
        InsertGuard()                               = delete;
        InsertGuard(const InsertGuard &)            = delete;
        InsertGuard(InsertGuard &&)                 = delete;
        InsertGuard &operator=(const InsertGuard &) = delete;
        InsertGuard &operator=(InsertGuard &&)      = delete;

    private:
        bool &ref; ///< the flag being held true
    };

    bool inserting         = false;           ///< true while the tour writes its own lines
    TutorialEngine *engine = nullptr;         ///< drives the tutorial (not owned)
    QWidget *host          = nullptr;         ///< main window (not owned)
    QPointer<TutorialSpotlight> spotlight;    ///< highlight layer, child of host
    QPointer<TutorialCoach> coach;            ///< the callout, child of the spotlight
    std::function<QRect(StepAnchor)> resolve; ///< anchor lookup
};

#endif // TUTORIALVIEW_H

// Local Variables:
// c-basic-offset: 4
// End:
