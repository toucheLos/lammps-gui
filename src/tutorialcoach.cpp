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

#include "tutorialcoach.h"

#include "constants.h"

#include <QDoubleSpinBox>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QPushButton>
#include <QTextBrowser>
#include <QVBoxLayout>

#include <cmath>

TutorialSpotlight::TutorialSpotlight(QWidget *parent) : QWidget(parent)
{
    // never intercept a click: the whole point is that the user can press the
    // very button the ring is drawn around
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_TranslucentBackground);
}

void TutorialSpotlight::setTarget(const QRect &rect)
{
    if (highlight == rect) return;
    highlight = rect;
    update();
}

void TutorialSpotlight::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    if (highlight.isEmpty()) return;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // a ring rather than a dimming mask: dimming the rest of the window would
    // hide the script the user is supposed to be reading
    const QRectF ring = QRectF(highlight).adjusted(-Cfg::COACH_RING_INSET, -Cfg::COACH_RING_INSET,
                                                   Cfg::COACH_RING_INSET, Cfg::COACH_RING_INSET);
    QColor glow       = Coach::highlight();
    glow.setAlpha(70);
    painter.setPen(Qt::NoPen);
    painter.setBrush(glow);
    painter.drawRoundedRect(ring, Cfg::COACH_RADIUS, Cfg::COACH_RADIUS);

    QColor edge = Coach::border();
    painter.setPen(QPen(edge, Cfg::COACH_RING_WIDTH));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(ring, Cfg::COACH_RADIUS, Cfg::COACH_RADIUS);
}

/* -------------------------------------------------------------------- */

TutorialCoach::TutorialCoach(QWidget *parent) : QWidget(parent)
{
    // a plain child widget: no window flags, so it has a real background, sits
    // inside the main window, and cannot be dragged anywhere
    setAutoFillBackground(false); // the background is painted, tail included

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(Cfg::COACH_MARGIN, Cfg::COACH_MARGIN, Cfg::COACH_MARGIN,
                              Cfg::COACH_MARGIN);

    breadcrumbLabel = new QLabel(this);
    QFont small     = breadcrumbLabel->font();
    small.setPointSize(qMax(small.pointSize() - 1, 8));
    breadcrumbLabel->setFont(small);
    breadcrumbLabel->setWordWrap(true);
    outer->addWidget(breadcrumbLabel);

    titleLabel   = new QLabel(this);
    QFont bigger = titleLabel->font();
    bigger.setPointSize(bigger.pointSize() + 2);
    bigger.setBold(true);
    titleLabel->setFont(bigger);
    titleLabel->setWordWrap(true);
    outer->addWidget(titleLabel);

    bodyText = new QTextBrowser(this);
    bodyText->setFrameShape(QFrame::NoFrame);
    bodyText->setOpenExternalLinks(true);
    // the explanations are worth keeping: selectable by mouse and keyboard, with
    // the ordinary read-only context menu (Copy, Select All) rather than none
    bodyText->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard |
                                      Qt::LinksAccessibleByMouse | Qt::LinksAccessibleByKeyboard);
    bodyText->setContextMenuPolicy(Qt::DefaultContextMenu);
    // the bubble paints its own background, so the text view must not paint one
    bodyText->viewport()->setAutoFillBackground(false);
    bodyText->setStyleSheet(QStringLiteral("background: transparent;"));
    outer->addWidget(bodyText, 1);

    predictLabel = new QLabel(this);
    predictLabel->setWordWrap(true);
    predictLabel->hide();
    outer->addWidget(predictLabel);

    actionLabel = new QLabel(this);
    actionLabel->setWordWrap(true);
    QFont action = actionLabel->font();
    action.setBold(true);
    actionLabel->setFont(action);
    outer->addWidget(actionLabel);

    feedbackLabel = new QLabel(this);
    feedbackLabel->setWordWrap(true);
    feedbackLabel->hide();
    outer->addWidget(feedbackLabel);

    // Hint and Show-me, offered only while a typed drill is on screen.  The
    // drill is a desirable difficulty, not a trap: a user who is stuck has to
    // be able to get moving again without abandoning the tutorial.
    drillRow   = new QWidget(this);
    auto *drl  = new QHBoxLayout(drillRow);
    drl->setContentsMargins(0, 0, 0, 0);
    hintButton   = new QPushButton(QStringLiteral("&Hint"), drillRow);
    revealButton = new QPushButton(QStringLiteral("&Show me"), drillRow);
    drl->addWidget(hintButton);
    drl->addWidget(revealButton);
    drl->addStretch(1);
    drillRow->hide();
    outer->addWidget(drillRow);

    connect(hintButton, &QPushButton::clicked, this, &TutorialCoach::hintRequested);
    connect(revealButton, &QPushButton::clicked, this, &TutorialCoach::revealRequested);

    // the tune row: a prompt, a value, and an Apply.  Hidden unless the step
    // asks the user to change a number in a line that is already written.
    tuneRow   = new QWidget(this);
    auto *trl = new QHBoxLayout(tuneRow);
    trl->setContentsMargins(0, 0, 0, 0);
    tuneLabel = new QLabel(tuneRow);
    tuneValue = new QDoubleSpinBox(tuneRow);
    tuneValue->setKeyboardTracking(false);
    tuneApply = new QPushButton(QStringLiteral("&Apply"), tuneRow);
    trl->addWidget(tuneLabel);
    trl->addWidget(tuneValue);
    trl->addWidget(tuneApply);
    trl->addStretch(1);
    tuneRow->hide();
    outer->addWidget(tuneRow);

    connect(tuneApply, &QPushButton::clicked, this,
            [this]() { emit tuneRequested(tuneValue->value()); });

    auto *row     = new QHBoxLayout;
    progressLabel = new QLabel(this);
    progressLabel->setFont(small);
    extraButton = new QPushButton(this);
    extraButton->hide();
    backButton = new QPushButton(QStringLiteral("< &Back"), this);
    nextButton = new QPushButton(QStringLiteral("&Next >"), this);
    nextButton->setDefault(true);
    row->addWidget(progressLabel);
    row->addStretch(1);
    row->addWidget(extraButton);
    row->addWidget(backButton);
    row->addWidget(nextButton);
    outer->addLayout(row);

    connect(extraButton, &QPushButton::clicked, this, &TutorialCoach::extraRequested);
    connect(backButton, &QPushButton::clicked, this, &TutorialCoach::backRequested);
    connect(nextButton, &QPushButton::clicked, this, &TutorialCoach::nextRequested);

    // the panel is pale, so force readable text regardless of the user's theme
    QPalette pal = palette();
    pal.setColor(QPalette::WindowText, Coach::text());
    pal.setColor(QPalette::Text, Coach::text());
    pal.setColor(QPalette::ButtonText, Coach::text());
    setPalette(pal);
}

void TutorialCoach::setContent(const QString &breadcrumb, const QString &title, const QString &body)
{
    breadcrumbLabel->setText(breadcrumb);
    breadcrumbLabel->setVisible(!breadcrumb.isEmpty());
    titleLabel->setText(title);
    bodyText->setHtml(body);
}

void TutorialCoach::setProgress(int done, int total)
{
    progressLabel->setText(total > 0 ? QStringLiteral("step %1 of %2").arg(done).arg(total)
                                     : QString());
}

void TutorialCoach::setPrediction(const QString &text)
{
    if (text.isEmpty()) {
        predictLabel->hide();
        return;
    }
    // deliberately not the same weight as the call to action: this asks for an
    // answer, and an answer the user gives silently still counts
    predictLabel->setText(QStringLiteral("<span style=\"color:%1;\">%2 %3</span>")
                              .arg(QStringLiteral("#7a5c00"),
                                   QStringLiteral("<b>Before you do:</b>"), text.toHtmlEscaped()));
    predictLabel->show();
}

void TutorialCoach::setCallToAction(const QString &text)
{
    actionLabel->setText(text);
    actionLabel->setVisible(!text.isEmpty());
}

void TutorialCoach::setDrillHelpers(bool enable)
{
    drillRow->setVisible(enable);
}

void TutorialCoach::setTune(const TuneControl &tune)
{
    if (!tune.isValid()) {
        tuneRow->hide();
        return;
    }
    tuneLabel->setText(tune.label.isEmpty()
                           ? QStringLiteral("%1:").arg(tune.command)
                           : tune.label);
    // decimals first: setting it afterwards re-quantizes a value that has
    // already been rounded to the old precision, which is how 0.005 once
    // arrived in the box as 0.0100
    tuneValue->setDecimals(tune.decimals);
    tuneValue->setRange(tune.min, tune.max);
    tuneValue->setSingleStep(tune.decimals > 0 ? std::pow(10.0, -tune.decimals) : 1.0);
    tuneValue->setValue(tune.from);
    tuneRow->show();
}

void TutorialCoach::setFeedback(const QString &text, bool ok)
{
    if (text.isEmpty()) {
        feedbackLabel->hide();
        return;
    }
    // green for accepted, a deep red for not-yet; both readable on the pale
    // background, which is why they are not taken from the palette
    feedbackLabel->setText(
        QStringLiteral("<span style=\"color:%1;\">%2</span>")
            .arg(ok ? QStringLiteral("#2e7d32") : QStringLiteral("#a8321e"), text.toHtmlEscaped()));
    feedbackLabel->show();
}

void TutorialCoach::setBackEnabled(bool enable)
{
    backButton->setEnabled(enable);
}

void TutorialCoach::setExtraButton(const QString &text)
{
    extraButton->setText(text);
    extraButton->setVisible(!text.isEmpty());
}

void TutorialCoach::setNextText(const QString &text)
{
    nextButton->setText(text);
}

void TutorialCoach::setNextEnabled(bool enable)
{
    nextButton->setEnabled(enable);
}

void TutorialCoach::setTailOffset(int pos)
{
    if (tailOffset == pos) return;
    tailOffset = pos;
    update();
}

void TutorialCoach::setSide(Side side)
{
    if (pointing == side) return;
    pointing = side;
    // the tail eats into one edge, so the layout margin has to move with it
    const int m = Cfg::COACH_MARGIN;
    const int t = Cfg::COACH_TAIL;
    layout()->setContentsMargins(
        m + (side == Side::Right ? t : 0), m + (side == Side::Below ? t : 0),
        m + (side == Side::Left ? t : 0), m + (side == Side::Above ? t : 0));
    update();
}

QSize TutorialCoach::sizeForWidth(int width) const
{
    // Every optional row has to be counted, not just the ones that were here
    // when this was written: a prediction, a feedback line, the drill helpers
    // and the tune control all appear and disappear, and leaving them out
    // squeezed the prose into a two-line scrolling box on exactly the steps
    // that had the most to say.
    const int inner = width - 2 * Cfg::COACH_MARGIN;
    int height      = bodyText->document()->size().toSize().height() +
                 nextButton->sizeHint().height() + 6 * Cfg::COACH_MARGIN;

    for (const QWidget *w :
         {static_cast<QWidget *>(breadcrumbLabel), static_cast<QWidget *>(titleLabel),
          static_cast<QWidget *>(predictLabel), static_cast<QWidget *>(actionLabel),
          static_cast<QWidget *>(feedbackLabel), static_cast<QWidget *>(drillRow),
          static_cast<QWidget *>(tuneRow)}) {
        if (!w || w->isHidden()) continue;
        // a wrapping label reports its wrapped height only when asked for one
        // at the width it will actually get
        const int h = w->heightForWidth(inner);
        height += h > 0 ? h : w->sizeHint().height();
    }
    return {width, qBound(Cfg::COACH_MIN_HEIGHT, height, Cfg::COACH_MAX_HEIGHT)};
}

void TutorialCoach::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // the body of the bubble, inset on whichever edge carries the tail
    const int t = Cfg::COACH_TAIL;
    QRectF body =
        QRectF(rect()).adjusted(pointing == Side::Right ? t : 0, pointing == Side::Below ? t : 0,
                                pointing == Side::Left ? -t : 0, pointing == Side::Above ? -t : 0);
    body.adjust(0.5, 0.5, -0.5, -0.5);

    QPainterPath path;
    path.addRoundedRect(body, Cfg::COACH_RADIUS, Cfg::COACH_RADIUS);

    // the tail, pointing at whatever the spotlight is ringing
    if (pointing != Side::None) {
        // the tail sits where the caller asked, clamped so it stays on a
        // straight part of the edge rather than riding over a rounded corner
        const qreal inset = Cfg::COACH_RADIUS + t;
        const qreal cx    = (pointing == Side::Above || pointing == Side::Below) && tailOffset >= 0
                                ? qBound(body.left() + inset, static_cast<qreal>(tailOffset),
                                         body.right() - inset)
                                : body.center().x();
        const qreal cy    = (pointing == Side::Left || pointing == Side::Right) && tailOffset >= 0
                                ? qBound(body.top() + inset, static_cast<qreal>(tailOffset),
                                         body.bottom() - inset)
                                : body.center().y();
        QPolygonF tail;
        switch (pointing) {
            case Side::Above: // bubble above the target: tail on the bottom edge
                tail << QPointF(cx - t, body.bottom()) << QPointF(cx + t, body.bottom())
                     << QPointF(cx, body.bottom() + t);
                break;
            case Side::Below:
                tail << QPointF(cx - t, body.top()) << QPointF(cx + t, body.top())
                     << QPointF(cx, body.top() - t);
                break;
            case Side::Left:
                tail << QPointF(body.right(), cy - t) << QPointF(body.right(), cy + t)
                     << QPointF(body.right() + t, cy);
                break;
            case Side::Right:
                tail << QPointF(body.left(), cy - t) << QPointF(body.left(), cy + t)
                     << QPointF(body.left() - t, cy);
                break;
            case Side::None:
                break;
        }
        QPainterPath tailpath;
        tailpath.addPolygon(tail);
        path = path.united(tailpath);
    }

    painter.setPen(QPen(Coach::border(), Cfg::COACH_BORDER_WIDTH));
    painter.setBrush(Coach::background());
    painter.drawPath(path);
}

// Local Variables:
// c-basic-offset: 4
// End:
