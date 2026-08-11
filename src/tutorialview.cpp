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

#include "tutorialview.h"

#include "constants.h"
#include "helpers.h"
#include "tutorialengine.h"
#include "tutorialeval.h"

#include <QComboBox>
#include <QDesktopServices>
#include <QDoubleSpinBox>
#include <QEasingCurve>
#include <QFont>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QProgressBar>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QShowEvent>
#include <QTextBrowser>
#include <QUrl>
#include <QVBoxLayout>

namespace {

/// duration of the entry slide, short enough not to be in the way
constexpr int SLIDE_MS = 180;

/// a monospace label carrying one word of a command
QLabel *commandChip(const QString &word, bool isCommandWord, QWidget *parent)
{
    auto *chip = new QLabel(word, parent);
    QFont mono = monoFontFromSettings();
    mono.setBold(isCommandWord);
    chip->setFont(mono);
    chip->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return chip;
}

} // namespace

TutorialView::TutorialView(TutorialEngine *engine, QWidget *parent) :
    QWidget(parent), engine(engine)
{
    setWindowTitle(QStringLiteral("LAMMPS-GUI: %1").arg(engine->content().title()));
    applyWindowFlags(this);
    // Tab must reach keyPressEvent() rather than move the focus chain
    setFocusPolicy(Qt::StrongFocus);

    auto *outer = new QVBoxLayout(this);

    breadcrumb  = new QLabel(this);
    QFont small = breadcrumb->font();
    small.setPointSize(qMax(small.pointSize() - 1, 8));
    breadcrumb->setFont(small);
    outer->addWidget(breadcrumb);

    progress = new QProgressBar(this);
    progress->setTextVisible(false);
    progress->setFixedHeight(4);
    progress->setMaximum(qMax(engine->content().stepCount(), 1));
    outer->addWidget(progress);

    titleLabel   = new QLabel(this);
    QFont bigger = titleLabel->font();
    bigger.setPointSize(bigger.pointSize() + 3);
    bigger.setBold(true);
    titleLabel->setFont(bigger);
    titleLabel->setWordWrap(true);
    outer->addWidget(titleLabel);

    // ---- illustration ----
    figureLabel = new QLabel(this);
    figureLabel->setAlignment(Qt::AlignCenter);
    figureLabel->setFrameShape(QFrame::StyledPanel);
    figureLabel->setMinimumHeight(Cfg::MINIMUM_HEIGHT / 3);
    outer->addWidget(figureLabel);

    figureCaption = new QLabel(this);
    figureCaption->setWordWrap(true);
    figureCaption->setFont(small);
    figureCaption->setAlignment(Qt::AlignCenter);
    outer->addWidget(figureCaption);

    teachText = new QTextBrowser(this);
    teachText->setOpenExternalLinks(true);
    teachText->setFrameShape(QFrame::NoFrame);
    // the teach beat is usually short; let it take what it needs and give the
    // rest of the room to the stage, which is where the user actually works
    teachText->setMinimumHeight(Cfg::MINIMUM_HEIGHT / 4);
    teachText->setMaximumHeight(Cfg::MINIMUM_HEIGHT / 2);
    outer->addWidget(teachText);

    // ---- the stage: either the command on offer, or the experiment ----
    stageBox  = new QGroupBox(this);
    stageBox_ = new QVBoxLayout(stageBox);
    // the stage takes the slack: it is where the user reads and acts
    outer->addWidget(stageBox, 1);

    auto *buttons = new QHBoxLayout;
    primary       = new QPushButton(this);
    primary->setDefault(true);
    docsButton = new QPushButton(QStringLiteral("&Docs"), this);
    buttons->addWidget(primary, 1);
    buttons->addWidget(docsButton);
    outer->addLayout(buttons);

    auto *nav  = new QHBoxLayout;
    prevButton = new QPushButton(QStringLiteral("< &Back"), this);
    nextButton = new QPushButton(QStringLiteral("&Next >"), this);
    nav->addWidget(prevButton);
    nav->addStretch(1);
    nav->addWidget(nextButton);
    outer->addLayout(nav);

    connect(nextButton, &QPushButton::clicked, engine, &TutorialEngine::next);
    connect(prevButton, &QPushButton::clicked, engine, &TutorialEngine::previous);
    connect(docsButton, &QPushButton::clicked, this, &TutorialView::openDocs);
    connect(engine, &TutorialEngine::stepChanged, this, &TutorialView::showCurrentStep);

    resize(Cfg::MINIMUM_WIDTH + 120, Cfg::MAIN_DEFAULT_HEIGHT);
    showCurrentStep();
}

/* -------------------------------------------------------------------- */

void TutorialView::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (slidIn) return;
    slidIn = true;

    // the panel arrives from the top right and settles; the animation is the
    // transition, not the residence, so it never covers the editor for long
    const QRect target = geometry();
    QRect start        = target;
    start.moveLeft(target.left() + target.width() / 3);
    start.moveTop(target.top() - target.height() / 4);

    auto *slide = new QPropertyAnimation(this, "geometry", this);
    slide->setDuration(SLIDE_MS);
    slide->setStartValue(start);
    slide->setEndValue(target);
    slide->setEasingCurve(QEasingCurve::OutCubic);
    slide->start(QAbstractAnimation::DeleteWhenStopped);
}

void TutorialView::keyPressEvent(QKeyEvent *event)
{
    // Tab inserts, but only here: CodeEditor binds it to reformat-line and
    // Shift+Tab to completion, and neither may change
    if (event->key() == Qt::Key_Tab && primary->isEnabled()) {
        primary->click();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

/* -------------------------------------------------------------------- */

QString TutorialView::renderText(const QString &text)
{
    // escaping first means a content file can never inject markup of its own
    QString out = text.toHtmlEscaped();
    out.replace(QRegularExpression(QStringLiteral("\\*\\*([^*]+)\\*\\*")),
                QStringLiteral("<b>\\1</b>"));
    out.replace(QRegularExpression(QStringLiteral("(?<![*])\\*([^*]+)\\*(?![*])")),
                QStringLiteral("<i>\\1</i>"));
    out.replace(QRegularExpression(QStringLiteral("`([^`]+)`")),
                QStringLiteral("<code style=\"background-color:rgba(128,128,128,0.18);\">"
                               "\\1</code>"));
    out.replace(QStringLiteral("\n\n"), QStringLiteral("<p>"));
    out.replace(QStringLiteral("\n"), QStringLiteral("<br>"));
    return out;
}

void TutorialView::buildCommandSection()
{
    const CommandLine *cmd = engine->nextCommand();
    const TutorialStep *st = engine->currentStep();

    if (!cmd) {
        stageBox->setTitle(QStringLiteral("Done with this step"));
        auto *done = new QLabel(
            st && st->commands.isEmpty()
                ? QStringLiteral("Nothing to insert here -- read on.")
                : QStringLiteral("Every line of this step is in your script. Press Next."),
            stageBox);
        done->setWordWrap(true);
        stageBox_->addWidget(done);
        primary->setText(QStringLiteral("&Next >"));
        primary->setEnabled(true);
        disconnect(primary, &QPushButton::clicked, nullptr, nullptr);
        connect(primary, &QPushButton::clicked, engine, &TutorialEngine::next);
        return;
    }

    stageBox->setTitle(QStringLiteral("Command %1 of %2")
                           .arg(engine->insertedCount() + 1)
                           .arg(st ? st->commands.size() : 0));

    // the command itself, word by word
    auto *row    = new QWidget(stageBox);
    auto *rowbox = new QHBoxLayout(row);
    rowbox->setContentsMargins(0, 0, 0, 0);
    const QStringList words = canonicalWords(cmd->text);
    for (int i = 0; i < words.size(); ++i)
        rowbox->addWidget(commandChip(words.at(i), i == 0, row));
    rowbox->addStretch(1);
    stageBox_->addWidget(row);

    if (!cmd->explain.isEmpty()) {
        auto *why = new QLabel(stageBox);
        why->setWordWrap(true);
        why->setTextFormat(Qt::RichText);
        why->setText(renderText(cmd->explain));
        stageBox_->addWidget(why);
    }

    // per-argument annotations, faded by the reminder budget
    QStringList shownConcepts;
    if (!cmd->conceptId.isEmpty()) shownConcepts << cmd->conceptId;
    for (const auto &note : cmd->notes) {
        const QString word = note.argIndex < words.size() ? words.at(note.argIndex)
                                                          : QString::number(note.argIndex);
        QString body       = QStringLiteral("<b><code>%1</code></b> &mdash; %2")
                           .arg(word.toHtmlEscaped(), note.note.toHtmlEscaped());

        // the reminder budget: explain in full while it lasts, then shrink to
        // the term itself so a returning user is not re-taught.  A note with no
        // concept has no budget to spend, so it always shows in full.
        const bool explain = note.conceptId.isEmpty() || engine->shouldExplain(note.conceptId);
        if (explain && !note.alternatives.isEmpty())
            body += QStringLiteral("<br><i>%1</i>").arg(note.alternatives.toHtmlEscaped());
        if (explain && !note.conceptId.isEmpty()) {
            if (const auto *c = engine->content().concept(note.conceptId))
                body += QStringLiteral("<br><small>%1: %2</small>")
                            .arg(c->term.toHtmlEscaped(), c->explain.toHtmlEscaped());
        }
        if (!note.conceptId.isEmpty()) shownConcepts << note.conceptId;

        auto *ann = new QLabel(body, stageBox);
        ann->setWordWrap(true);
        ann->setTextFormat(Qt::RichText);
        ann->setContentsMargins(12, 0, 0, 0);
        stageBox_->addWidget(ann);
    }
    engine->noteConceptsShown(shownConcepts);

    primary->setText(QStringLiteral("&Insert  (Tab)"));
    primary->setEnabled(true);
    disconnect(primary, &QPushButton::clicked, nullptr, nullptr);
    connect(primary, &QPushButton::clicked, this, &TutorialView::insertNext);
}

void TutorialView::buildExperimentSection()
{
    const TutorialStep *st = engine->currentStep();
    stageBox->setTitle(QStringLiteral("Try it"));

    for (const auto &param : st->params) {
        auto *row    = new QWidget(stageBox);
        auto *rowbox = new QHBoxLayout(row);
        rowbox->setContentsMargins(0, 0, 0, 0);

        auto *label = new QLabel(param.label, row);
        rowbox->addWidget(label);

        QWidget *editor = nullptr;
        if (param.kind == ParamKind::Number) {
            auto *spin = new QDoubleSpinBox(row);
            // decimals first: QDoubleSpinBox rounds to its current precision on
            // setValue(), and the default of 2 would quietly turn 0.005 into 0.01
            spin->setDecimals(param.step < 0.01 ? 4 : 3);
            spin->setRange(param.min, param.max);
            spin->setSingleStep(param.step);
            spin->setValue(param.initial);
            if (!param.unit.isEmpty()) spin->setSuffix(QStringLiteral(" ") + param.unit);
            editor = spin;
        } else {
            auto *combo = new QComboBox(row);
            combo->addItems(param.choices);
            combo->setCurrentText(param.initialChoice);
            editor = combo;
        }
        editor->setFont(monoFontFromSettings());
        rowbox->addWidget(editor, 1);
        paramWidgets.append(editor);
        stageBox_->addWidget(row);

        if (!param.explain.isEmpty()) {
            auto *why = new QLabel(renderText(param.explain), stageBox);
            why->setWordWrap(true);
            why->setTextFormat(Qt::RichText);
            why->setContentsMargins(12, 0, 0, 8);
            stageBox_->addWidget(why);
        }
    }

    // the optional prediction: never a gate, and dismissable by simply running
    if (st->prediction.present) {
        auto *ask = new QLabel(renderText(st->prediction.question), stageBox);
        ask->setWordWrap(true);
        ask->setTextFormat(Qt::RichText);
        stageBox_->addWidget(ask);

        for (int i = 0; i < st->prediction.options.size(); ++i) {
            auto *button = new QRadioButton(st->prediction.options.at(i).text, stageBox);
            stageBox_->addWidget(button);
            predictionButtons.append(button);
            connect(button, &QRadioButton::clicked, this, [this, st, i]() {
                // both branches teach: show the chosen answer's own explanation
                // whether it was right or not, and never block on it
                const auto &opt = st->prediction.options.at(i);
                predictionFeedback->setText(
                    QStringLiteral("<i>%1</i>").arg(renderText(opt.feedback)));
            });
        }
        predictionFeedback = new QLabel(stageBox);
        predictionFeedback->setWordWrap(true);
        predictionFeedback->setTextFormat(Qt::RichText);
        stageBox_->addWidget(predictionFeedback);
    }

    if (!st->expect.isEmpty()) {
        auto *expect = new QLabel(
            QStringLiteral("<b>Watch for:</b> %1").arg(renderText(st->expect)), stageBox);
        expect->setWordWrap(true);
        expect->setTextFormat(Qt::RichText);
        stageBox_->addWidget(expect);
    }

    primary->setText(QStringLiteral("&Run it"));
    primary->setEnabled(true);
    disconnect(primary, &QPushButton::clicked, nullptr, nullptr);
    connect(primary, &QPushButton::clicked, this, &TutorialView::runExperiment);
}

void TutorialView::showCurrentStep()
{
    paramWidgets.clear();
    predictionButtons.clear();
    predictionFeedback = nullptr;
    while (QLayoutItem *item = stageBox_->takeAt(0)) {
        delete item->widget();
        delete item;
    }

    const TutorialStep *step = engine->currentStep();
    if (!step) {
        breadcrumb->setText(engine->content().title());
        progress->setValue(progress->maximum());
        titleLabel->setText(QStringLiteral("Tutorial complete"));
        teachText->setHtml(QStringLiteral(
            "<p>That is the end of the tutorial. The script in the editor is yours -- keep "
            "changing it and re-running to see what happens.</p>"));
        figureLabel->hide();
        figureCaption->hide();
        stageBox->hide();
        primary->setEnabled(false);
        nextButton->setEnabled(false);
        docsButton->setEnabled(false);
        return;
    }

    stageBox->show();
    const auto &act = engine->content().acts().at(engine->actIndex());
    breadcrumb->setText(QStringLiteral("%1  -  Act %2/%3: %4  -  Step %5/%6%7")
                            .arg(engine->content().title())
                            .arg(engine->actIndex() + 1)
                            .arg(engine->content().actCount())
                            .arg(act.title)
                            .arg(engine->stepIndex() + 1)
                            .arg(act.steps.size())
                            .arg(step->checkpoint ? QStringLiteral("   [checkpoint]") : QString()));
    progress->setValue(engine->stepsCompleted() + 1);
    titleLabel->setText(step->title);
    teachText->setHtml(renderText(step->teach));

    if (step->figure == FigureSource::Snapshot) {
        // a render of the user's own system, not a picture from a paper
        figureLabel->setText(QStringLiteral("[ snapshot of your system ]"));
        figureLabel->show();
        figureCaption->setText(step->figureCaption);
        figureCaption->setVisible(!step->figureCaption.isEmpty());
        emit snapshotRequested();
    } else {
        figureLabel->hide();
        figureCaption->hide();
    }

    if (step->kind == StepKind::Experiment)
        buildExperimentSection();
    else
        buildCommandSection();

    docsButton->setEnabled(!step->docCommand.isEmpty());
    prevButton->setEnabled(engine->stepsCompleted() > 0);
    nextButton->setEnabled(true);
    setFocus();
}

/* -------------------------------------------------------------------- */

void TutorialView::insertNext()
{
    const QString text = engine->takeNextCommand();
    if (!text.isEmpty()) emit insertCommand(text);
    showCurrentStep();
}

void TutorialView::runExperiment()
{
    const TutorialStep *st = engine->currentStep();
    if (!st) return;

    // rewrite each bound argument in place, then run what the user now has
    for (int i = 0; i < st->params.size() && i < paramWidgets.size(); ++i) {
        const auto &param = st->params.at(i);
        QString value;
        if (auto *spin = qobject_cast<QDoubleSpinBox *>(paramWidgets.at(i)))
            value = QString::number(spin->value(), 'g', 6);
        else if (auto *combo = qobject_cast<QComboBox *>(paramWidgets.at(i)))
            value = combo->currentText();
        if (!value.isEmpty()) emit applyParameter(param.command, param.argIndex, value);
    }
    emit runRequested();
}

void TutorialView::openDocs()
{
    const TutorialStep *step = engine->currentStep();
    if (!step || step->docCommand.isEmpty()) return;
    // the help index maps a command and optional style to a page; until that
    // lookup is factored out of CodeEditor, link to the command's own page
    const QString page = QStringLiteral("%1.html").arg(step->docCommand);
    QDesktopServices::openUrl(QUrl(QStringLiteral("%1/%2").arg(Cfg::DOCS_URL, page)));
}

// Local Variables:
// c-basic-offset: 4
// End:
