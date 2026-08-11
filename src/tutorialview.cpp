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

#include <QCheckBox>
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
            return QStringLiteral("Your turn -- write the command:");
        case StepVerb::Fill:
            return QStringLiteral("Your turn -- complete the command:");
        case StepVerb::Fix:
            return QStringLiteral("Your turn -- repair this command:");
        case StepVerb::Predict:
            return QStringLiteral("Predict before you run:");
        case StepVerb::Tune:
            return QStringLiteral("Change it, re-run, and report what you see:");
        case StepVerb::Inspect:
            return QStringLiteral("Read each part, then press Next:");
    }
    return QString();
}

/// colour a verdict without assuming a light or dark palette
QString verdictColor(Verdict v)
{
    switch (v) {
        case Verdict::Correct:
            return QStringLiteral("#2e7d32");
        case Verdict::Incorrect:
            return QStringLiteral("#c62828");
        case Verdict::Unresolved:
            return QStringLiteral("#ef6c00");
    }
    return QString();
}

} // namespace

TutorialView::TutorialView(TutorialEngine *engine, QWidget *parent) :
    QWidget(parent), engine(engine)
{
    setWindowTitle(QStringLiteral("LAMMPS-GUI: %1").arg(engine->content().title()));
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
    progress->setMaximum(qMax(engine->content().stepCount(), 1));
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
    // until then the panel names what the step asked for rather than pretending
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
    // capped: an empty verdict box must not compete with the teach text for
    // vertical space, but it still has to hold a real LAMMPS error verbatim
    feedback->setMinimumHeight(Cfg::MINIMUM_HEIGHT / 5);
    feedback->setMaximumHeight(Cfg::MINIMUM_HEIGHT / 2);
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
    expertBox  = new QCheckBox(QStringLiteral("&Expert mode (checkpoints only)"), this);
    expertBox->setToolTip(QStringLiteral("Stop only at the checkpoints. Nothing is hidden; "
                                         "you simply stop being asked."));
    nav->addWidget(prevButton);
    nav->addWidget(expertBox, 1, Qt::AlignHCenter);
    nav->addWidget(nextButton);
    outer->addLayout(nav);

    connect(nextButton, &QPushButton::clicked, engine, &TutorialEngine::next);
    connect(prevButton, &QPushButton::clicked, engine, &TutorialEngine::previous);
    connect(skipButton, &QPushButton::clicked, engine, &TutorialEngine::skip);
    connect(checkButton, &QPushButton::clicked, this, &TutorialView::checkAnswer);
    connect(hintButton, &QPushButton::clicked, this, &TutorialView::requestHint);
    connect(docsButton, &QPushButton::clicked, this, &TutorialView::openDocs);
    connect(expertBox, &QCheckBox::toggled, this, &TutorialView::toggleExpert);
    connect(engine, &TutorialEngine::stepChanged, this, &TutorialView::showCurrentStep);

    resize(Cfg::MINIMUM_WIDTH, Cfg::MAIN_DEFAULT_HEIGHT);
    expertBox->setChecked(engine->expertMode());
    showCurrentStep();
}

/* -------------------------------------------------------------------- */

QString TutorialView::renderTeachText(const QString &text)
{
    // the authored subset is bold, italic, inline code and paragraphs; escaping
    // first means a content file can never inject markup of its own
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
    // drop whatever the previous step put in the prompt area
    holeEdits.clear();
    optionButtons.clear();
    lineEdit = nullptr;
    while (QLayoutItem *item = promptBox->takeAt(0)) {
        delete item->widget();
        delete item;
    }

    const TutorialStep *step = engine->currentStep();
    if (!step) {
        breadcrumb->setText(engine->content().title());
        progress->setValue(progress->maximum());
        titleLabel->setText(QStringLiteral("Tutorial complete"));
        teachText->setHtml(QStringLiteral(
            "<p>That is the end of the tutorial. The script in the editor is yours -- "
            "you wrote it. Keep experimenting with it.</p>"));
        visualNote->hide();
        promptLabel->clear();
        feedback->clear();
        for (auto *b : {checkButton, hintButton, skipButton, docsButton, nextButton})
            b->setEnabled(false);
        prevButton->setEnabled(true);
        return;
    }

    const auto &acts = engine->content().acts();
    const auto &act  = acts.at(engine->actIndex());
    breadcrumb->setText(QStringLiteral("%1  -  Act %2/%3: %4  -  Step %5/%6%7")
                            .arg(engine->content().title())
                            .arg(engine->actIndex() + 1)
                            .arg(engine->content().actCount())
                            .arg(act.title)
                            .arg(engine->stepIndex() + 1)
                            .arg(act.steps.size())
                            .arg(step->checkpoint ? QStringLiteral("   [checkpoint]") : QString()));
    progress->setValue(engine->stepsCompleted() + 1);

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

    const QFont mono = monoFontFromSettings();
    if (step->verb == StepVerb::Predict) {
        for (const auto &option : step->options) {
            auto *button = new QRadioButton(option.text, promptArea);
            promptBox->addWidget(button);
            optionButtons.append(button);
        }
    } else if (step->editor.holeCount() > 0) {
        // one field per hole, so a rejection can point at the hole that is wrong
        const QStringList parts = step->editor.skeleton.split(HOLE);
        auto *row               = new QWidget(promptArea);
        auto *rowbox            = new QHBoxLayout(row);
        rowbox->setContentsMargins(0, 0, 0, 0);
        for (int i = 0; i < parts.size(); ++i) {
            if (!parts.at(i).trimmed().isEmpty()) {
                auto *fixed = new QLabel(parts.at(i).trimmed(), row);
                fixed->setFont(mono);
                rowbox->addWidget(fixed);
            }
            if (i + 1 < parts.size()) {
                auto *edit = new QLineEdit(row);
                edit->setFont(mono);
                edit->setPlaceholderText(step->validate.rules.value(i).label.isEmpty()
                                             ? QStringLiteral("?")
                                             : step->validate.rules.value(i).label);
                connect(edit, &QLineEdit::returnPressed, this, &TutorialView::checkAnswer);
                rowbox->addWidget(edit);
                holeEdits.append(edit);
            }
        }
        rowbox->addStretch(1);
        promptBox->addWidget(row);
        if (!holeEdits.isEmpty())
            holeEdits.at(qBound(0, step->editor.focusPlaceholder, holeEdits.size() - 1))
                ->setFocus();
    } else if (step->verb == StepVerb::Type || step->verb == StepVerb::Fix ||
               step->verb == StepVerb::Inspect) {
        lineEdit = new QLineEdit(promptArea);
        lineEdit->setFont(mono);
        // a FIX shows the broken command to repair; a TYPE must never show the
        // answer, or it stops being anything but transcription
        if (step->verb != StepVerb::Type) lineEdit->setText(step->editor.skeleton);
        if (step->verb == StepVerb::Inspect) lineEdit->setReadOnly(true);
        if (step->verb == StepVerb::Type)
            lineEdit->setPlaceholderText(QStringLiteral("type the command here"));
        connect(lineEdit, &QLineEdit::returnPressed, this, &TutorialView::checkAnswer);
        promptBox->addWidget(lineEdit);
        lineEdit->setFocus();
    }

    feedback->clear();
    if (!engine->canCheckNow() && step->verb != StepVerb::Read && step->verb != StepVerb::Inspect) {
        feedback->setHtml(
            QStringLiteral("<i>This step is judged by LAMMPS itself, which is not wired up "
                           "yet. Use Next or Skip to carry on.</i>"));
    }

    checkButton->setEnabled(engine->canCheckNow());
    hintButton->setEnabled(engine->hasMoreHints() || engine->revealAvailable());
    skipButton->setEnabled(step->skippable);
    docsButton->setEnabled(!step->docCommand.isEmpty());
    prevButton->setEnabled(engine->stepsCompleted() > 0);
    nextButton->setEnabled(true);
}

/* -------------------------------------------------------------------- */

QString TutorialView::assembledLine() const
{
    if (lineEdit) return lineEdit->text();
    return QString();
}

QStringList TutorialView::holeValues() const
{
    QStringList out;
    for (const auto *edit : holeEdits)
        out << edit->text();
    return out;
}

int TutorialView::selectedOption() const
{
    for (int i = 0; i < optionButtons.size(); ++i)
        if (optionButtons.at(i)->isChecked()) return i;
    return -1;
}

void TutorialView::checkAnswer()
{
    const TutorialStep *step = engine->currentStep();
    if (!step || !engine->canCheckNow()) return;

    StepResult result;
    if (step->verb == StepVerb::Predict) {
        result = engine->submitChoice(selectedOption());
    } else if (!holeEdits.isEmpty()) {
        result = engine->submitHoles(holeValues());
    } else {
        result = engine->submitLine(assembledLine());
    }
    showVerdict(result);

    if (result.verdict == Verdict::Correct) {
        // the accepted command joins the script, so the user ends the tutorial
        // holding a real input file they wrote themselves
        if (step->verb == StepVerb::Fill) {
            QString line           = step->editor.skeleton;
            const QStringList vals = holeValues();
            for (const auto &v : vals)
                line.replace(line.indexOf(HOLE), HOLE.size(), v);
            emit commandAccepted(line);
        } else if (step->verb == StepVerb::Type) {
            emit commandAccepted(assembledLine());
        }
        if (step->advance == AdvanceMode::Auto) engine->next();
    }

    // a fresh wrong attempt may have earned the reveal
    hintButton->setEnabled(engine->hasMoreHints() || engine->revealAvailable());
}

void TutorialView::showVerdict(const StepResult &result)
{
    const TutorialStep *step = engine->currentStep();
    QString html;

    static const QHash<int, QString> headline = {
        {static_cast<int>(Verdict::Correct), QStringLiteral("Correct")},
        {static_cast<int>(Verdict::Incorrect), QStringLiteral("Not yet")},
        {static_cast<int>(Verdict::Unresolved), QStringLiteral("Cannot tell")},
    };

    html +=
        QStringLiteral("<p style=\"color:%1;\"><b>%2</b></p>")
            .arg(verdictColor(result.verdict), headline.value(static_cast<int>(result.verdict)));

    // the generated explanation names the argument at fault; the authored text
    // says what it means.  Both matter, and they are not the same thing.
    if (!result.message.isEmpty())
        html += QStringLiteral("<p>%1</p>").arg(result.message.toHtmlEscaped());
    if (!result.feedback.isEmpty())
        html += QStringLiteral("<p>%1</p>").arg(renderTeachText(result.feedback));

    if (step && result.verdict == Verdict::Incorrect && engine->revealAvailable() &&
        !step->reveal.isEmpty())
        html += QStringLiteral("<p><i>Stuck? Press Hint to see the answer.</i></p>");

    feedback->setHtml(html);
}

void TutorialView::requestHint()
{
    const TutorialStep *step = engine->currentStep();
    if (!step) return;

    QString html;
    const QStringList shown = engine->nextHint();
    for (int i = 0; i < shown.size(); ++i)
        html += QStringLiteral("<p>%1. %2</p>").arg(i + 1).arg(shown.at(i).toHtmlEscaped());

    if (engine->revealAvailable() && !step->reveal.isEmpty()) {
        // the reveal explains, rather than just naming the string
        html += QStringLiteral("<p><b>Answer:</b> %1</p>").arg(renderTeachText(step->reveal));
        hintButton->setEnabled(false);
    } else if (!engine->hasMoreHints()) {
        hintButton->setEnabled(false);
    }
    feedback->setHtml(html);
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

void TutorialView::toggleExpert()
{
    engine->setExpertMode(expertBox->isChecked());
}

// Local Variables:
// c-basic-offset: 4
// End:
