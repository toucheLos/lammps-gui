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

#include "tutorialcontent.h"

#include <QWidget>

class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QRadioButton;
class QTextBrowser;
class QVBoxLayout;

/**
 * @brief Panel that presents one interactive tutorial step at a time
 *
 * Lays out the step the way the design calls for, top to bottom: a
 * breadcrumb with a progress bar, the step title, the teach text, a slot for
 * an auxiliary visual, the prompt and its input, a feedback area, and the
 * navigation buttons.
 *
 * The panel is never modal.  The user can edit the script, run it, and wander
 * off at any point, and every step carries a visible Skip control -- the
 * tutorial is for a volunteer adult learner, so engagement is enforced by the
 * shape of the task rather than by taking control away.
 *
 * This is presentation only: it owns no validation logic and holds the
 * content read-only.  Judging answers moves to TutorialEvaluator and the step
 * cursor to TutorialEngine.
 */
class TutorialView : public QWidget {
    Q_OBJECT

public:
    /**
     * @brief Constructor
     * @param content Parsed tutorial to present (copied)
     * @param parent Parent widget
     */
    explicit TutorialView(const TutorialContent &content, QWidget *parent = nullptr);

    /** @brief Destructor */
    ~TutorialView() override = default;

    TutorialView()                                = delete;
    TutorialView(const TutorialView &)            = delete;
    TutorialView(TutorialView &&)                 = delete;
    TutorialView &operator=(const TutorialView &) = delete;
    TutorialView &operator=(TutorialView &&)      = delete;

private slots:
    void nextStep();     ///< advance one step, clamped at the end
    void previousStep(); ///< go back one step, clamped at the start
    void skipStep();     ///< skip the current step; never punished, never blocked
    void requestHint();  ///< reveal the next rung of the hint ladder
    void openDocs();     ///< open the LAMMPS documentation page for this step

private:
    /// rebuild every widget from the step the cursor points at
    void showCurrentStep();
    /// the step the cursor points at, or nullptr when the tutorial is empty
    const TutorialStep *currentStep() const;
    /// render the markdown subset (bold, italic, inline code) as rich text
    static QString renderTeachText(const QString &text);

    TutorialContent tutorial; ///< the tutorial being presented
    int actIndex   = 0;       ///< 0-based act cursor
    int stepIndex  = 0;       ///< 0-based step cursor within the act
    int hintsShown = 0;       ///< how many rungs of the hint ladder are visible

    QLabel *breadcrumb       = nullptr; ///< "Tutorial 1 - Act 2 - Step 3/7"
    QProgressBar *progress   = nullptr; ///< thin progress bar over the whole tutorial
    QLabel *titleLabel       = nullptr; ///< step title
    QTextBrowser *teachText  = nullptr; ///< the teach beat
    QLabel *visualNote       = nullptr; ///< placeholder naming the visual a step asks for
    QLabel *promptLabel      = nullptr; ///< "Your turn:" and what to do
    QWidget *promptArea      = nullptr; ///< holds the inline input or the option buttons
    QVBoxLayout *promptBox   = nullptr; ///< layout of the prompt area
    QTextBrowser *feedback   = nullptr; ///< verdict, real LAMMPS error, and the gloss
    QPushButton *checkButton = nullptr; ///< primary action; Enter is bound to it
    QPushButton *hintButton  = nullptr; ///< progressive hint ladder
    QPushButton *skipButton  = nullptr; ///< always present, never punished
    QPushButton *docsButton  = nullptr; ///< open the LAMMPS documentation
    QPushButton *prevButton  = nullptr; ///< step backwards
    QPushButton *nextButton  = nullptr; ///< step forwards
};

#endif // TUTORIALVIEW_H

// Local Variables:
// c-basic-offset: 4
// End:
