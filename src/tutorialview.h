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

#include "tutorialeval.h"

#include <QList>
#include <QWidget>

class TutorialEngine;

class QCheckBox;
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
 * Lays out the step top to bottom: a breadcrumb with a progress bar, the step
 * title, the teach text, a slot for an auxiliary visual, the prompt and its
 * input, a feedback area, and the navigation buttons.
 *
 * The panel is never modal.  The user can edit the script, run it, and wander
 * off at any point, and every step carries a visible Skip control -- the
 * tutorial is for a volunteer adult learner, so engagement comes from the
 * shape of the task rather than from taking control away.
 *
 * All judging and cursor movement belongs to TutorialEngine; this class only
 * collects what the user entered, hands it over, and renders the verdict.
 */
class TutorialView : public QWidget {
    Q_OBJECT

public:
    /**
     * @brief Constructor
     * @param engine Engine driving the tutorial; must outlive this view and
     *               is reparented to it when it has no parent of its own
     * @param parent Parent widget
     */
    explicit TutorialView(TutorialEngine *engine, QWidget *parent = nullptr);

    /** @brief Destructor */
    ~TutorialView() override = default;

    TutorialView()                                = delete;
    TutorialView(const TutorialView &)            = delete;
    TutorialView(TutorialView &&)                 = delete;
    TutorialView &operator=(const TutorialView &) = delete;
    TutorialView &operator=(TutorialView &&)      = delete;

signals:
    /**
     * @brief An answer was accepted and should be added to the script
     * @param text the accepted command line
     *
     * The main window appends it to the editor, so the tutorial builds a real,
     * runnable input file rather than a transcript.
     */
    void commandAccepted(const QString &text);

private slots:
    void checkAnswer();     ///< judge whatever the prompt currently holds
    void requestHint();     ///< reveal the next rung of the hint ladder
    void openDocs();        ///< open the LAMMPS documentation page for this step
    void toggleExpert();    ///< switch between every step and checkpoints only
    void showCurrentStep(); ///< rebuild every widget from the engine's cursor

private:
    /// the text the user has entered, assembled from the prompt widgets
    QString assembledLine() const;
    /// the values entered into the skeleton's "___" holes, in order
    QStringList holeValues() const;
    /// index of the selected option, or -1
    int selectedOption() const;
    /// render the verdict, its explanation, and the authored feedback
    void showVerdict(const StepResult &result);
    /// render the markdown subset (bold, italic, inline code) as rich text
    static QString renderTeachText(const QString &text);

    TutorialEngine *engine = nullptr; ///< drives the tutorial (not owned)

    QLabel *breadcrumb       = nullptr; ///< "Tutorial 1 - Act 2 - Step 3/7"
    QProgressBar *progress   = nullptr; ///< thin progress bar over the whole tutorial
    QLabel *titleLabel       = nullptr; ///< step title
    QTextBrowser *teachText  = nullptr; ///< the teach beat
    QLabel *visualNote       = nullptr; ///< placeholder naming the visual a step asks for
    QLabel *promptLabel      = nullptr; ///< "Your turn:" and what to do
    QWidget *promptArea      = nullptr; ///< holds the inline inputs or the option buttons
    QVBoxLayout *promptBox   = nullptr; ///< layout of the prompt area
    QTextBrowser *feedback   = nullptr; ///< verdict, explanation, and authored text
    QCheckBox *expertBox     = nullptr; ///< checkpoints-only toggle
    QPushButton *checkButton = nullptr; ///< primary action; Enter is bound to it
    QPushButton *hintButton  = nullptr; ///< progressive hint ladder
    QPushButton *skipButton  = nullptr; ///< always present, never punished
    QPushButton *docsButton  = nullptr; ///< open the LAMMPS documentation
    QPushButton *prevButton  = nullptr; ///< step backwards
    QPushButton *nextButton  = nullptr; ///< step forwards

    /// one editable field per "___" hole, empty for a whole-line prompt
    QList<QLineEdit *> holeEdits;
    /// the single field a TYPE or FIX step types into, null otherwise
    QLineEdit *lineEdit = nullptr;
    /// the option buttons of a PREDICT step, empty otherwise
    QList<QRadioButton *> optionButtons;
};

#endif // TUTORIALVIEW_H

// Local Variables:
// c-basic-offset: 4
// End:
