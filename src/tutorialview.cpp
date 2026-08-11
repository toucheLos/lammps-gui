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

#include <QDesktopServices>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QHash>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QTextBrowser>
#include <QUrl>
#include <QVBoxLayout>

namespace {

/// hole marker in a Fill skeleton
const QString HOLE = QStringLiteral("___");

/// what the prompt asks for, per verb
QString promptFor(StepVerb verb)
{
    switch (verb) {
        case StepVerb::Read:
            return QStringLiteral("Read on, then press Next.");
        case StepVerb::Type:
            return QStringLiteral("Your turn -- type the command in the editor:");
        case StepVerb::Fill:
            return QStringLiteral("Your turn -- complete the command:");
        case StepVerb::Fix:
            return QStringLiteral("Your turn -- repair this command:");
        case StepVerb::Predict:
            return QStringLiteral("Predict before you run:");
        case StepVerb::Tune:
            return QStringLiteral("Change it, re-run, and report what you see:");
        case StepVerb::Inspect:
            return QStringLiteral("Click each part to see what it does:");
    }
    return QString();
}

} // namespace

TutorialView::TutorialView(const TutorialContent &content, QWidget *parent) :
    QWidget(parent), tutorial(content)
{
    setWindowTitle(QStringLiteral("LAMMPS-GUI: %1").arg(tutorial.title()));
    applyWindowFlags(this);

    auto *outer = new QVBoxLayout(this);

    // ---- breadcrumb and progress ----
    breadcrumb  = new QLabel(this);
    QFont small = breadcrumb->font();
    small.setPointSize(qMax(small.pointSize() - 1, 8));
    breadcrumb->setFont(small);
    outer->addWidget(breadcrumb);

    progress = new QProgressBar(this);
    progress->setTextVisible(false);
    progress->setFixedHeight(4);
    progress->setMaximum(qMax(tutorial.stepCount(), 1));
    outer->addWidget(progress);

    auto *rule = new QFrame(this);
    rule->setFrameShape(QFrame::HLine);
    rule->setFrameShadow(QFrame::Sunken);
    outer->addWidget(rule);

    // ---- step title ----
    titleLabel   = new QLabel(this);
    QFont bigger = titleLabel->font();
    bigger.setPointSize(bigger.pointSize() + 3);
    bigger.setBold(true);
    titleLabel->setFont(bigger);
    titleLabel->setWordWrap(true);
    outer->addWidget(titleLabel);

    // ---- teach text ----
    teachText = new QTextBrowser(this);
    teachText->setOpenExternalLinks(true);
    teachText->setFrameShape(QFrame::NoFrame);
    teachText->setMinimumHeight(Cfg::MINIMUM_HEIGHT / 3);
    outer->addWidget(teachText, 1);

    // ---- visual slot ----
    // the concept plot, anatomy strip and lattice view arrive in later phases;
    // until then the panel says what the step asked for rather than pretending
    visualNote = new QLabel(this);
    visualNote->setAlignment(Qt::AlignCenter);
    visualNote->setFrameShape(QFrame::StyledPanel);
    visualNote->setWordWrap(true);
    outer->addWidget(visualNote);

    // ---- prompt ----
    promptLabel = new QLabel(this);
    promptLabel->setWordWrap(true);
    outer->addWidget(promptLabel);

    promptArea = new QWidget(this);
    promptBox  = new QVBoxLayout(promptArea);
    promptBox->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(promptArea);

    // ---- feedback ----
    feedback = new QTextBrowser(this);
    feedback->setFrameShape(QFrame::StyledPanel);
    feedback->setMaximumHeight(Cfg::MINIMUM_HEIGHT / 3);
    outer->addWidget(feedback);

    // ---- buttons ----
    auto *buttons = new QHBoxLayout;
    checkButton   = new QPushButton(QStringLiteral("&Check"), this);
    checkButton->setDefault(true);
    hintButton = new QPushButton(QStringLiteral("&Hint"), this);
    skipButton = new QPushButton(QStringLiteral("&Skip"), this);
    docsButton = new QPushButton(QStringLiteral("&Docs"), this);
    buttons->addWidget(checkButton);
    buttons->addWidget(hintButton);
    buttons->addWidget(skipButton);
    buttons->addWidget(docsButton);
    outer->addLayout(buttons);

    auto *nav  = new QHBoxLayout;
    prevButton = new QPushButton(QStringLiteral("< &Back"), this);
    nextButton = new QPushButton(QStringLiteral("&Next >"), this);
    nav->addWidget(prevButton);
    nav->addStretch(1);
    nav->addWidget(nextButton);
    outer->addLayout(nav);

    connect(nextButton, &QPushButton::clicked, this, &TutorialView::nextStep);
    connect(prevButton, &QPushButton::clicked, this, &TutorialView::previousStep);
    connect(skipButton, &QPushButton::clicked, this, &TutorialView::skipStep);
    connect(hintButton, &QPushButton::clicked, this, &TutorialView::requestHint);
    connect(docsButton, &QPushButton::clicked, this, &TutorialView::openDocs);
    // validation is not wired yet: the evaluator lands with the phase 2 engine
    connect(checkButton, &QPushButton::clicked, this, &TutorialView::nextStep);

    resize(Cfg::MINIMUM_WIDTH, Cfg::MAIN_DEFAULT_HEIGHT);
    showCurrentStep();
}

const TutorialStep *TutorialView::currentStep() const
{
    return tutorial.step(actIndex, stepIndex);
}

QString TutorialView::renderTeachText(const QString &text)
{
    // the authored subset is bold, italic, inline code and paragraphs; anything
    // else stays literal, so a content file can never inject markup
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

void TutorialView::showCurrentStep()
{
    hintsShown = 0;

    // clear whatever the previous step put in the prompt area
    while (QLayoutItem *item = promptBox->takeAt(0)) {
        delete item->widget();
        delete item;
    }

    const TutorialStep *step = currentStep();
    if (!step) {
        titleLabel->setText(QStringLiteral("Tutorial complete"));
        teachText->setHtml(QStringLiteral("<p>You reached the end of the tutorial.</p>"));
        breadcrumb->clear();
        visualNote->hide();
        promptLabel->clear();
        feedback->clear();
        checkButton->setEnabled(false);
        hintButton->setEnabled(false);
        skipButton->setEnabled(false);
        nextButton->setEnabled(false);
        return;
    }

    // how many steps precede this one, for the breadcrumb and the bar
    int done = 0;
    for (int a = 0; a < actIndex; ++a)
        done += static_cast<int>(tutorial.acts().at(a).steps.size());
    done += stepIndex;

    const auto &act = tutorial.acts().at(actIndex);
    breadcrumb->setText(QStringLiteral("%1  -  Act %2/%3: %4  -  Step %5/%6%7")
                            .arg(tutorial.title())
                            .arg(actIndex + 1)
                            .arg(tutorial.actCount())
                            .arg(act.title)
                            .arg(stepIndex + 1)
                            .arg(act.steps.size())
                            .arg(step->checkpoint ? QStringLiteral("   [checkpoint]") : QString()));
    progress->setValue(done + 1);

    titleLabel->setText(QStringLiteral("[%1]  %2").arg(stepVerbName(step->verb), step->title));
    teachText->setHtml(renderTeachText(step->teach));

    if (step->visual.widget == VisualWidget::None) {
        visualNote->hide();
    } else {
        static const QHash<int, QString> names = {
            {static_cast<int>(VisualWidget::ConceptPlot), QStringLiteral("potential energy curve")},
            {static_cast<int>(VisualWidget::Anatomy), QStringLiteral("command anatomy strip")},
            {static_cast<int>(VisualWidget::Lattice), QStringLiteral("lattice preview")},
        };
        visualNote->setText(
            QStringLiteral("[ %1 -- arrives in a later phase ]")
                .arg(names.value(static_cast<int>(step->visual.widget), QStringLiteral("visual"))));
        visualNote->show();
    }

    promptLabel->setText(promptFor(step->verb));

    // the prompt itself: options for a prediction, an editable skeleton for a
    // fill or a repair, and nothing at all for a step with no user action
    if (step->verb == StepVerb::Predict) {
        for (const auto &option : step->options)
            promptBox->addWidget(new QRadioButton(option.text, promptArea));
    } else if (!step->editor.skeleton.isEmpty()) {
        auto *line = new QLineEdit(step->editor.skeleton, promptArea);
        line->setFont(monoFontFromSettings());
        line->setReadOnly(step->verb == StepVerb::Inspect);
        promptBox->addWidget(line);
        if (step->editor.holeCount() > 0) {
            // put the cursor on the hole the author wants filled first
            const int pos = step->editor.skeleton.indexOf(HOLE);
            if (pos >= 0) {
                line->setSelection(pos, static_cast<int>(HOLE.size()));
                line->setFocus();
            }
        }
    } else if (step->verb == StepVerb::Type) {
        auto *line = new QLineEdit(promptArea);
        line->setFont(monoFontFromSettings());
        line->setPlaceholderText(QStringLiteral("type the command here"));
        promptBox->addWidget(line);
    }

    feedback->setHtml(QStringLiteral("<i>Answer checking arrives with the phase 2 engine; "
                                     "Check currently just advances.</i>"));

    const bool gated = step->verb != StepVerb::Read && step->verb != StepVerb::Inspect;
    checkButton->setEnabled(gated);
    hintButton->setEnabled(!step->hints.isEmpty());
    // Skip is always offered on a gated step, and nothing about skipping is punished
    skipButton->setEnabled(gated && step->skippable);
    docsButton->setEnabled(!step->docCommand.isEmpty());
    prevButton->setEnabled(actIndex > 0 || stepIndex > 0);
    nextButton->setEnabled(true);
}

void TutorialView::nextStep()
{
    if (!currentStep()) return;
    const auto &act = tutorial.acts().at(actIndex);
    if (stepIndex + 1 < act.steps.size()) {
        ++stepIndex;
    } else if (actIndex + 1 < tutorial.actCount()) {
        ++actIndex;
        stepIndex = 0;
    } else {
        // past the last step: showCurrentStep() renders the completion state
        ++stepIndex;
    }
    showCurrentStep();
}

void TutorialView::previousStep()
{
    if (stepIndex > 0) {
        --stepIndex;
    } else if (actIndex > 0) {
        --actIndex;
        stepIndex = qMax(static_cast<int>(tutorial.acts().at(actIndex).steps.size()) - 1, 0);
    } else {
        return;
    }
    showCurrentStep();
}

void TutorialView::skipStep()
{
    nextStep();
}

void TutorialView::requestHint()
{
    const TutorialStep *step = currentStep();
    if (!step) return;

    QString shown;
    if (hintsShown < step->hints.size()) {
        ++hintsShown;
        for (int i = 0; i < hintsShown; ++i)
            shown +=
                QStringLiteral("<p>%1. %2</p>").arg(i + 1).arg(step->hints.at(i).toHtmlEscaped());
        if (hintsShown == step->hints.size() && !step->reveal.isEmpty())
            shown += QStringLiteral("<p><i>Press Hint once more to reveal the answer.</i></p>");
    } else if (!step->reveal.isEmpty()) {
        // the reveal explains rather than just naming the string
        for (int i = 0; i < step->hints.size(); ++i)
            shown +=
                QStringLiteral("<p>%1. %2</p>").arg(i + 1).arg(step->hints.at(i).toHtmlEscaped());
        shown += QStringLiteral("<p><b>Answer:</b> %1</p>").arg(renderTeachText(step->reveal));
        hintButton->setEnabled(false);
    }
    feedback->setHtml(shown);
}

void TutorialView::openDocs()
{
    const TutorialStep *step = currentStep();
    if (!step || step->docCommand.isEmpty()) return;
    // the help index maps a command and optional style to a page; until that
    // lookup is factored out of CodeEditor, link to the command's own page
    const QString page = QStringLiteral("%1.html").arg(step->docCommand);
    QDesktopServices::openUrl(QUrl(QStringLiteral("%1/%2").arg(Cfg::DOCS_URL, page)));
}

// Local Variables:
// c-basic-offset: 4
// End:
