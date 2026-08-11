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

#include "tutorialengine.h"

#include "constants.h"
#include "tutorialcontent.h"

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QSettings>
#include <QTemporaryDir>

namespace {

/// a four-step tutorial: two ordinary steps, a checkpoint, and a final step
QByteArray stubDocument()
{
    return QByteArray(R"({
      "schema_version": 1, "id": "stub", "title": "Stub",
      "attribution": { "license": "test" },
      "acts": [
        { "id": "a1", "title": "First", "steps": [
          { "id": "s1", "verb": "TYPE", "title": "Units", "teach": "t",
            "hints": ["first nudge", "second nudge"],
            "reveal": "units lj -- reduced units",
            "validate": { "type": "exact_tokens",
                          "rules": [ { "type": "exact", "text": "units" },
                                     { "type": "exact", "text": "lj" } ] },
            "feedback": { "correct": "well done", "wrong": "not that" } },
          { "id": "s2", "verb": "FILL", "title": "Cutoff", "teach": "t",
            "editor": { "skeleton": "pair_style lj/cut ___" },
            "validate": { "type": "numeric_range",
                          "rules": [ { "type": "numeric_range", "min": 2.0, "max": 5.0,
                                       "ideal": 2.5 } ] },
            "feedback": { "below": "too short", "above": "wasteful" } }
        ] },
        { "id": "a2", "title": "Second", "steps": [
          { "id": "s3", "verb": "READ", "title": "Checkpoint", "teach": "t",
            "checkpoint": true },
          { "id": "s4", "verb": "PREDICT", "title": "Guess", "teach": "t",
            "options": [ { "text": "no", "feedback": "because no" },
                         { "text": "yes", "feedback": "because yes" } ],
            "validate": { "type": "choice", "correct_option": 1 } }
        ] }
      ]
    })");
}

TutorialContent stubContent()
{
    return parseTutorialJson(stubDocument());
}

/// count emissions of a signal without pulling in Qt Test for QSignalSpy
template <typename Signal> class SignalCounter {
public:
    SignalCounter(TutorialEngine *engine, Signal sig)
    {
        QObject::connect(engine, sig, engine, [this]() {
            ++seen;
        });
    }
    int count() const { return seen; }

private:
    int seen = 0;
};

/// point QSettings at a scratch directory so tests never touch a real profile
class SettingsSandbox {
public:
    SettingsSandbox()
    {
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
        QSettings::setDefaultFormat(QSettings::IniFormat);
    }

private:
    QTemporaryDir dir;
};

} // namespace

// ---- cursor --------------------------------------------------------------

TEST(TutorialEngineTest, StartsOnTheFirstStep)
{
    TutorialEngine engine(stubContent());
    ASSERT_NE(engine.currentStep(), nullptr);
    EXPECT_EQ(engine.currentStep()->id, QStringLiteral("s1"));
    EXPECT_EQ(engine.stepsCompleted(), 0);
    EXPECT_FALSE(engine.isFinished());
}

TEST(TutorialEngineTest, NextWalksAcrossActBoundaries)
{
    TutorialEngine engine(stubContent());
    const QStringList expected = {"s1", "s2", "s3", "s4"};
    for (const auto &id : expected) {
        ASSERT_NE(engine.currentStep(), nullptr) << qPrintable(id);
        EXPECT_EQ(engine.currentStep()->id, id);
        engine.next();
    }
    EXPECT_TRUE(engine.isFinished());
}

TEST(TutorialEngineTest, PreviousWalksBackAcrossActBoundaries)
{
    TutorialEngine engine(stubContent());
    engine.next();
    engine.next(); // s3, first step of act 2
    ASSERT_NE(engine.currentStep(), nullptr);
    EXPECT_EQ(engine.currentStep()->id, QStringLiteral("s3"));

    engine.previous();
    ASSERT_NE(engine.currentStep(), nullptr);
    EXPECT_EQ(engine.currentStep()->id, QStringLiteral("s2"));
}

TEST(TutorialEngineTest, PreviousAtTheStartIsANoOp)
{
    TutorialEngine engine(stubContent());
    SignalCounter spy(&engine, &TutorialEngine::stepChanged);
    engine.previous();
    EXPECT_EQ(engine.currentStep()->id, QStringLiteral("s1"));
    EXPECT_EQ(spy.count(), 0);
}

TEST(TutorialEngineTest, FinishingEmitsTheFinishedSignal)
{
    TutorialEngine engine(stubContent());
    SignalCounter spy(&engine, &TutorialEngine::tutorialFinished);
    for (int i = 0; i < 4; ++i)
        engine.next();
    EXPECT_TRUE(engine.isFinished());
    EXPECT_EQ(spy.count(), 1);
}

TEST(TutorialEngineTest, GoToStepJumpsByIdAndIgnoresUnknownIds)
{
    TutorialEngine engine(stubContent());
    engine.goToStep(QStringLiteral("s4"));
    EXPECT_EQ(engine.currentStep()->id, QStringLiteral("s4"));

    engine.goToStep(QStringLiteral("nonexistent"));
    EXPECT_EQ(engine.currentStep()->id, QStringLiteral("s4"));
}

// ---- skipping is free ----------------------------------------------------

TEST(TutorialEngineTest, SkipAdvancesAndCostsNothing)
{
    TutorialEngine engine(stubContent());
    engine.submitLine(QStringLiteral("units real")); // one wrong attempt
    EXPECT_EQ(engine.attempts(), 1);

    engine.skip();
    EXPECT_EQ(engine.currentStep()->id, QStringLiteral("s2"));
    // the new step starts clean; nothing about having skipped is carried over
    EXPECT_EQ(engine.attempts(), 0);
    EXPECT_EQ(engine.hintsShown(), 0);
}

TEST(TutorialEngineTest, SkippingDoesNotBreakLaterSteps)
{
    TutorialEngine engine(stubContent());
    engine.skip();
    engine.skip();
    engine.skip();
    ASSERT_NE(engine.currentStep(), nullptr);
    EXPECT_EQ(engine.currentStep()->id, QStringLiteral("s4"));
    // a skipped-past step is still fully answerable when reached again
    const auto res = engine.submitChoice(1);
    EXPECT_EQ(res.verdict, Verdict::Correct);
}

// ---- hints and the reveal ------------------------------------------------

TEST(TutorialEngineTest, HintLadderRevealsOneRungAtATime)
{
    TutorialEngine engine(stubContent());
    EXPECT_TRUE(engine.hasMoreHints());

    auto shown = engine.nextHint();
    EXPECT_EQ(shown.size(), 1);
    EXPECT_EQ(shown.at(0), QStringLiteral("first nudge"));

    shown = engine.nextHint();
    EXPECT_EQ(shown.size(), 2);
    EXPECT_FALSE(engine.hasMoreHints());
}

TEST(TutorialEngineTest, RevealBecomesAvailableAfterTheLadderIsExhausted)
{
    TutorialEngine engine(stubContent());
    EXPECT_FALSE(engine.revealAvailable());
    engine.nextHint();
    EXPECT_FALSE(engine.revealAvailable());
    engine.nextHint();
    EXPECT_TRUE(engine.revealAvailable());
}

TEST(TutorialEngineTest, RevealIsOfferedUnpromptedAfterThreeWrongAttempts)
{
    // the requirement: nobody gets stranded on a step they cannot pass
    TutorialEngine engine(stubContent());
    for (int i = 0; i < TutorialEngine::REVEAL_AFTER - 1; ++i) {
        engine.submitLine(QStringLiteral("units real"));
        EXPECT_FALSE(engine.revealAvailable()) << i;
    }
    engine.submitLine(QStringLiteral("units real"));
    EXPECT_EQ(engine.attempts(), TutorialEngine::REVEAL_AFTER);
    EXPECT_TRUE(engine.revealAvailable());
}

TEST(TutorialEngineTest, UndecidableAnswersDoNotCountAsWrong)
{
    // a "$" substitution cannot be judged, so it must not push the user
    // towards the reveal as though they had failed
    TutorialEngine engine(stubContent());
    engine.next(); // s2, a numeric fill
    const auto res = engine.submitHoles({QStringLiteral("${cut}")});
    EXPECT_EQ(res.verdict, Verdict::Unresolved);
    EXPECT_EQ(engine.attempts(), 0);
}

TEST(TutorialEngineTest, HintStateResetsWhenTheCursorMoves)
{
    TutorialEngine engine(stubContent());
    engine.nextHint();
    engine.next();
    EXPECT_EQ(engine.hintsShown(), 0);
}

// ---- verdicts and authored feedback --------------------------------------

TEST(TutorialEngineTest, CorrectAnswerCarriesTheAuthoredPraise)
{
    TutorialEngine engine(stubContent());
    const auto res = engine.submitLine(QStringLiteral("units lj"));
    EXPECT_EQ(res.verdict, Verdict::Correct);
    EXPECT_EQ(res.feedback, QStringLiteral("well done"));
    EXPECT_EQ(engine.attempts(), 0);
}

TEST(TutorialEngineTest, NumericMissPicksTheBelowOrAboveText)
{
    TutorialEngine engine(stubContent());
    engine.next(); // s2

    const auto low = engine.submitHoles({QStringLiteral("1.0")});
    EXPECT_EQ(low.verdict, Verdict::Incorrect);
    EXPECT_EQ(low.feedback, QStringLiteral("too short"));

    const auto high = engine.submitHoles({QStringLiteral("9.0")});
    EXPECT_EQ(high.feedback, QStringLiteral("wasteful"));
}

TEST(TutorialEngineTest, PredictionKeepsTheChosenOptionsOwnExplanation)
{
    TutorialEngine engine(stubContent());
    engine.goToStep(QStringLiteral("s4"));

    const auto wrong = engine.submitChoice(0);
    EXPECT_EQ(wrong.verdict, Verdict::Incorrect);
    EXPECT_EQ(wrong.feedback, QStringLiteral("because no"));

    const auto right = engine.submitChoice(1);
    EXPECT_EQ(right.verdict, Verdict::Correct);
    EXPECT_EQ(right.feedback, QStringLiteral("because yes"));
}

TEST(TutorialEngineTest, VerdictSignalIsEmitted)
{
    TutorialEngine engine(stubContent());
    SignalCounter spy(&engine, &TutorialEngine::verdictReady);
    engine.submitLine(QStringLiteral("units lj"));
    EXPECT_EQ(spy.count(), 1);
}

// ---- what can be checked yet ---------------------------------------------

TEST(TutorialEngineTest, StepsNeedingLammpsReportThatTheyCannotBeCheckedYet)
{
    TutorialEngine engine(stubContent());
    EXPECT_TRUE(engine.canCheckNow()); // TYPE
    engine.next();
    EXPECT_TRUE(engine.canCheckNow()); // FILL
    engine.next();
    EXPECT_FALSE(engine.canCheckNow()); // READ has no gate
    engine.next();
    EXPECT_TRUE(engine.canCheckNow()); // PREDICT
}

// ---- expert mode ---------------------------------------------------------

TEST(TutorialEngineTest, ExpertModeStopsOnlyAtCheckpoints)
{
    TutorialEngine engine(stubContent());
    engine.setExpertMode(true);
    EXPECT_EQ(engine.currentStep()->id, QStringLiteral("s1"));

    engine.next();
    ASSERT_NE(engine.currentStep(), nullptr);
    EXPECT_EQ(engine.currentStep()->id, QStringLiteral("s3")) << "should have skipped s2";
}

TEST(TutorialEngineTest, ExpertModeHidesNothingWhenTurnedOff)
{
    TutorialEngine engine(stubContent());
    engine.setExpertMode(true);
    engine.next();
    engine.setExpertMode(false);
    engine.previous();
    EXPECT_EQ(engine.currentStep()->id, QStringLiteral("s2"));
}

// ---- progress persistence ------------------------------------------------

TEST(TutorialEngineTest, ProgressRoundTripsThroughSettings)
{
    SettingsSandbox sandbox;

    {
        TutorialEngine engine(stubContent());
        engine.goToStep(QStringLiteral("s3"));
        engine.setExpertMode(true);
        engine.saveProgress();
    }
    {
        TutorialEngine engine(stubContent());
        engine.restoreProgress();
        ASSERT_NE(engine.currentStep(), nullptr);
        EXPECT_EQ(engine.currentStep()->id, QStringLiteral("s3"));
        EXPECT_TRUE(engine.expertMode());
    }
}

TEST(TutorialEngineTest, ResetProgressReturnsToTheStart)
{
    SettingsSandbox sandbox;

    TutorialEngine engine(stubContent());
    engine.goToStep(QStringLiteral("s4"));
    engine.saveProgress();
    engine.resetProgress();
    EXPECT_EQ(engine.currentStep()->id, QStringLiteral("s1"));

    TutorialEngine fresh(stubContent());
    fresh.restoreProgress();
    EXPECT_EQ(fresh.currentStep()->id, QStringLiteral("s1"));
}

TEST(TutorialEngineTest, ASavedStepThatNoLongerExistsStartsOver)
{
    // content changed under the user: resuming at a guessed position would be
    // worse than starting again
    SettingsSandbox sandbox;
    {
        QSettings settings;
        settings.beginGroup(Keys::GROUP_TUTORIAL);
        settings.beginGroup(QStringLiteral("stub"));
        settings.setValue(Keys::PROGRESS_STEP, QStringLiteral("s-removed"));
        settings.endGroup();
        settings.endGroup();
    }

    TutorialEngine engine(stubContent());
    engine.restoreProgress();
    EXPECT_EQ(engine.currentStep()->id, QStringLiteral("s1"));
}

// Local Variables:
// c-basic-offset: 4
// End:
