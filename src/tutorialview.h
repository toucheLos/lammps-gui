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

#include <QList>
#include <QString>
#include <QWidget>

class TutorialEngine;
struct TutorialParam;

class QAbstractButton;
class QGroupBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QTextBrowser;
class QVBoxLayout;

/**
 * @brief Panel that walks the user through one tutorial
 *
 * The tutorial is a guided walkthrough rather than a quiz: each step shows the
 * commands to write, one line at a time, annotated with what each argument
 * means and what a different value would do.  Insert (or Tab, while the panel
 * has focus) places the line in the script.  Experiment steps replace the
 * command display with parameter widgets and a Run button, so the user changes
 * something and watches the real simulation respond.
 *
 * Annotations fade: a concept is explained in full only while the engine's
 * reminder budget for it lasts, and collapses to a term afterwards.
 *
 * The panel slides in from the top right on the first show and then stays put.
 * That animation is the only one in the application, and it is confined to
 * this class so it can be removed without touching anything else.
 */
class TutorialView : public QWidget {
    Q_OBJECT

public:
    /**
     * @brief Constructor
     * @param engine Engine driving the tutorial; must outlive this view
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
     * @brief Put a command into the script
     * @param text the command line to append
     */
    void insertCommand(const QString &text);

    /**
     * @brief Rewrite one argument of a command already in the script
     * @param command command word to find, e.g. "timestep"
     * @param argIndex 1-based argument position
     * @param value replacement text
     */
    void applyParameter(const QString &command, int argIndex, const QString &value);

    /** @brief Run the current script */
    void runRequested();

    /** @brief Render a snapshot of the current system for the figure slot */
    void snapshotRequested();

protected:
    /**
     * @brief Claim Tab as the insert key, but only inside this panel
     *
     * The editor binds Tab to reformat-line and Shift+Tab to completion, so
     * the key can only be taken here, where focus is on the tutorial rather
     * than on the script.
     */
    void keyPressEvent(QKeyEvent *event) override;

    /** @brief Start the slide-in on the first show */
    void showEvent(QShowEvent *event) override;

private slots:
    void insertNext();      ///< insert the command currently on offer
    void runExperiment();   ///< apply the parameters and run
    void openDocs();        ///< open the LAMMPS documentation page for this step
    void showCurrentStep(); ///< rebuild every widget from the engine's cursor

private:
    /// build the annotated display of the command on offer
    void buildCommandSection();
    /// build the parameter widgets and Run button of an experiment
    void buildExperimentSection();
    /// render the markdown subset (bold, italic, inline code) as rich text
    static QString renderText(const QString &text);

    TutorialEngine *engine = nullptr; ///< drives the tutorial (not owned)

    QLabel *breadcrumb      = nullptr; ///< act and step position
    QProgressBar *progress  = nullptr; ///< progress over the whole tutorial
    QLabel *titleLabel      = nullptr; ///< step title
    QTextBrowser *teachText = nullptr; ///< the teach beat
    QLabel *figureLabel     = nullptr; ///< illustration, scaled to fit
    QLabel *figureCaption   = nullptr; ///< caption under the illustration
    QGroupBox *stageBox     = nullptr; ///< holds the command or the experiment
    QVBoxLayout *stageBox_  = nullptr; ///< layout of the stage
    QPushButton *primary    = nullptr; ///< Insert, or Run on an experiment
    QPushButton *docsButton = nullptr; ///< open the LAMMPS documentation
    QPushButton *prevButton = nullptr; ///< step backwards
    QPushButton *nextButton = nullptr; ///< step forwards

    /// one editable widget per experiment parameter, in declaration order
    QList<QWidget *> paramWidgets;
    /// the prediction's answer buttons, empty when the step has no prediction
    QList<QAbstractButton *> predictionButtons;
    QLabel *predictionFeedback = nullptr; ///< what the chosen prediction teaches

    bool slidIn = false; ///< the entry animation has already played
};

#endif // TUTORIALVIEW_H

// Local Variables:
// c-basic-offset: 4
// End:
