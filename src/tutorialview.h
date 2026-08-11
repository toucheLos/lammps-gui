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
     * @brief Supply the widget an anchor names
     * @param resolver called with an anchor, returns the widget to point at
     *
     * The caller owns the mapping because only the main window knows where its
     * views currently live, and whether they exist at all.
     */
    void setAnchorResolver(std::function<QWidget *(StepAnchor)> resolver);

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
    void offerCommand(const QString &text);

    /** @brief Withdraw a pending command that was never committed */
    void withdrawCommand();

    /**
     * @brief Put a command straight into the script, already committed
     * @param text the command to insert
     *
     * Used when the user skips ahead: the remaining lines of the step still
     * have to reach the script, or what they run next would not work.
     */
    void insertCommand(const QString &text);

    /**
     * @brief Open a different input file
     * @param name file name relative to the tutorial's working directory
     */
    void openFileRequested(const QString &name);

    /** @brief The tour reached its end */
    void finished();

public slots:
    /** @brief Rebuild the callout from the engine's cursor */
    void showCurrentStep();
    /** @brief Reposition the callout; call when the host resizes */
    void reposition();
    /** @brief The user committed the pending line, so move on */
    void commandCommitted();

private slots:
    void goNext(); ///< advance, withdrawing anything uncommitted
    void goBack(); ///< step back, withdrawing anything uncommitted

private:
    /// render the markdown subset (bold, italic, inline code) as rich text
    static QString renderText(const QString &text);
    /// the anchor the current step asks for
    StepAnchor currentAnchor() const;

    TutorialEngine *engine = nullptr;             ///< drives the tutorial (not owned)
    QWidget *host          = nullptr;             ///< main window (not owned)
    QPointer<TutorialSpotlight> spotlight;        ///< highlight layer, child of host
    QPointer<TutorialCoach> coach;                ///< the callout, child of the spotlight
    std::function<QWidget *(StepAnchor)> resolve; ///< anchor lookup
};

#endif // TUTORIALVIEW_H

// Local Variables:
// c-basic-offset: 4
// End:
