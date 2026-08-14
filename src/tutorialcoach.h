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

#ifndef TUTORIALCOACH_H
#define TUTORIALCOACH_H

#include "tutorialcontent.h"

#include <QColor>
#include <QRect>
#include <QWidget>

class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QTextBrowser;

/**
 * @brief The tutorial's fixed palette
 *
 * These live here rather than in constants.h because that header is included
 * by Qt Core only translation units and QColor would pull QtGui in with it.
 *
 * The colors are deliberately fixed rather than derived from the application
 * palette: the coach layer has to read as *not* part of the normal chrome, in
 * both light and dark themes, and a pale yellow that follows the theme would
 * stop doing that in one of them.
 */
namespace Coach {

/** Background of the callout: a pale, warm yellow */
inline QColor background()
{
    return {0xfd, 0xf6, 0xd8};
}
/** Border of the callout and of the highlight ring: a deeper amber */
inline QColor border()
{
    return {0xd4, 0xa3, 0x2c};
}
/** Fill used to ring a target and to mark the pending line in the editor */
inline QColor highlight()
{
    return {0xff, 0xe9, 0x8c};
}
/** Text inside the callout; fixed, because the background is fixed */
inline QColor text()
{
    return {0x2b, 0x25, 0x10};
}

} // namespace Coach

/**
 * @brief Transparent layer that dims nothing and highlights one thing
 *
 * Spans the whole main window, sits above every other child, and paints a
 * ring around whichever widget the tutorial is currently talking about.  It
 * carries Qt::WA_TransparentForMouseEvents so it never swallows a click: the
 * user can still press the very button being pointed at.
 *
 * The callout bubble is a child of this layer, so the two are positioned and
 * raised together.
 */
class TutorialSpotlight : public QWidget {
    Q_OBJECT

public:
    /**
     * @brief Constructor
     * @param parent Main window; the layer resizes itself to match
     */
    explicit TutorialSpotlight(QWidget *parent);

    /** @brief Destructor */
    ~TutorialSpotlight() override = default;

    TutorialSpotlight()                                     = delete;
    TutorialSpotlight(const TutorialSpotlight &)            = delete;
    TutorialSpotlight(TutorialSpotlight &&)                 = delete;
    TutorialSpotlight &operator=(const TutorialSpotlight &) = delete;
    TutorialSpotlight &operator=(TutorialSpotlight &&)      = delete;

    /**
     * @brief Ring the given rectangle
     * @param rect area to highlight, in this layer's coordinates; an empty
     *        rectangle clears the highlight
     */
    void setTarget(const QRect &rect);

    /** @brief The rectangle currently highlighted */
    QRect target() const { return highlight; }

protected:
    /** @brief Paint the ring */
    void paintEvent(QPaintEvent *event) override;

private:
    QRect highlight; ///< area to ring, empty when nothing is highlighted
};

/**
 * @brief The tutorial's callout bubble
 *
 * A pale, opaque panel that carries prose and two buttons and nothing else.
 * It is a plain child widget with no window flags, so it has a real
 * background, cannot be dragged, and always paints inside the main window --
 * the earlier version set window hints and consequently behaved like a
 * detached window floating on nothing.
 *
 * Deliberately holds no code, no inputs and no controls beyond Back and Next:
 * commands belong in the editor, where they are highlighted and committed
 * with Tab.
 */
class TutorialCoach : public QWidget {
    Q_OBJECT

public:
    /// where the bubble sits relative to the thing it points at
    enum class Side : quint8 {
        Above, ///< bubble above the target, tail pointing down
        Below, ///< bubble below the target, tail pointing up
        Left,  ///< bubble left of the target, tail pointing right
        Right, ///< bubble right of the target, tail pointing left
        None   ///< no target; the bubble parks in the top right corner
    };

    /**
     * @brief Constructor
     * @param parent Spotlight layer the bubble lives on
     */
    explicit TutorialCoach(QWidget *parent);

    /** @brief Destructor */
    ~TutorialCoach() override = default;

    TutorialCoach()                                 = delete;
    TutorialCoach(const TutorialCoach &)            = delete;
    TutorialCoach(TutorialCoach &&)                 = delete;
    TutorialCoach &operator=(const TutorialCoach &) = delete;
    TutorialCoach &operator=(TutorialCoach &&)      = delete;

    /**
     * @brief Set the prose the bubble shows
     * @param breadcrumb small line above the title
     * @param title step heading
     * @param body the teach text, already rendered to rich text
     */
    void setContent(const QString &breadcrumb, const QString &title, const QString &body);

    /**
     * @brief Show progress through the tutorial
     * @param done steps completed
     * @param total steps in total
     */
    void setProgress(int done, int total);

    /**
     * @brief Set the call to action shown under the prose
     * @param text what the user should do now; empty hides the line
     */
    void setCallToAction(const QString &text);

    /**
     * @brief Show (or hide) the control for changing one command argument
     * @param tune the control the step asks for; an invalid one hides the row
     */
    void setTune(const TuneControl &tune);

    /** @brief Enable or disable the Back button */
    void setBackEnabled(bool enable);
    /**
     * @brief Show an extra button to the left of Back, or hide it
     * @param text label for the button; empty hides it
     *
     * Used only by the completion panel, which has one more thing to offer
     * than Back and Done.
     */
    void setExtraButton(const QString &text);

    /** @brief Set the Next button's label, so it can read "Done" at the end */
    void setNextText(const QString &text);
    /** @brief Enable or disable the Next button */
    void setNextEnabled(bool enable);

    /**
     * @brief Show a short response to what the user just did
     * @param text the response; empty clears it
     * @param ok true to colour it as accepted, false as not-yet
     */
    void setFeedback(const QString &text, bool ok);

    /** @brief Which side of the target the bubble is drawn on */
    void setSide(Side side);

    /**
     * @brief Put the tail on a particular row (or column) of the bubble's edge
     * @param pos offset along the tail's edge, in this widget's coordinates;
     *        negative centres it
     *
     * A callout beside a single line of the editor is far taller than the line
     * it points at, so a tail at the bubble's own centre points at empty space
     * several rows away.  The caller knows where the line actually is.
     */
    void setTailOffset(int pos);

    /** @brief Preferred size for a given available width */
    QSize sizeForWidth(int width) const;

signals:
    /** @brief The user pressed Next */
    void nextRequested();
    /** @brief The user pressed Back */
    void backRequested();

    /** @brief The user pressed the extra button on the completion panel */
    void extraRequested();

    /**
     * @brief The user applied a new value for the step's tuned argument
     * @param value the number now in the control
     */
    void tuneRequested(double value);

protected:
    /** @brief Paint the panel background, border and pointer tail */
    void paintEvent(QPaintEvent *event) override;

private:
    QLabel *breadcrumbLabel = nullptr; ///< act and step position
    QLabel *titleLabel      = nullptr; ///< step heading
    QTextBrowser *bodyText  = nullptr; ///< the teach text
    QLabel *actionLabel     = nullptr; ///< what to do now
    QLabel *feedbackLabel   = nullptr; ///< response to a typed answer
    QLabel *progressLabel   = nullptr; ///< "step 4 of 21"
    QWidget *tuneRow          = nullptr; ///< prompt + value + Apply, usually hidden
    QLabel *tuneLabel         = nullptr; ///< prompt beside the value
    QDoubleSpinBox *tuneValue = nullptr; ///< the value the user dials in
    QPushButton *tuneApply    = nullptr; ///< writes it into the script
    QPushButton *extraButton = nullptr; ///< completion panel only; usually hidden
    QPushButton *backButton = nullptr; ///< step backwards
    QPushButton *nextButton = nullptr; ///< step forwards

    Side pointing  = Side::None; ///< which edge carries the tail
    int tailOffset = -1;         ///< where along that edge; negative means centred
};

#endif // TUTORIALCOACH_H

// Local Variables:
// c-basic-offset: 4
// End:
