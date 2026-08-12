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

#include <QSettings>
#include <QTemporaryDir>

namespace {

/// a three-step tutorial: two shows (one with two commands) and an observe
QByteArray stubDocument()
{
    return QByteArray(R"({
      "schema_version": 3, "id": "stub", "title": "Stub",
      "attribution": { "license": "test" },
      "concepts": [
        { "id": "units", "term": "reduced units", "explain": "dimensionless" },
        { "id": "seed", "term": "random seed", "explain": "fixes the sequence" }
      ],
      "acts": [
        { "id": "a1", "title": "First", "steps": [
          { "id": "s1", "kind": "SHOW", "title": "Units", "teach": "t",
            "commands": [
              { "text": "units lj", "explain": "reduced units", "concept": "units" },
              { "text": "boundary p p p", "explain": "periodic" }
            ] },
          { "id": "s2", "kind": "SHOW", "title": "Atoms", "teach": "t",
            "commands": [
              { "text": "timestep 0.005", "explain": "small steps",
                "notes": [ { "arg": 1, "note": "the step", "concept": "seed" } ] }
            ] }
        ] },
        { "id": "a2", "title": "Second", "steps": [
          { "id": "s3", "kind": "OBSERVE", "title": "Run it", "teach": "t",
            "checkpoint": true, "anchor": "run",
            "call_to_action": "Press the Run button.",
            "expect": "the energy trace" }
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
    EXPECT_FALSE(engine.isFinished());
}

TEST(TutorialEngineTest, NextWalksAcrossActBoundariesAndFinishes)
{
    TutorialEngine engine(stubContent());
    const QStringList expected = {"s1", "s2", "s3"};
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
    engine.next();
    EXPECT_EQ(engine.currentStep()->id, QStringLiteral("s3"));
    engine.previous();
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
    for (int i = 0; i < 3; ++i)
        engine.next();
    EXPECT_EQ(spy.count(), 1);
}

TEST(TutorialEngineTest, GoToStepJumpsByIdAndIgnoresUnknownIds)
{
    TutorialEngine engine(stubContent());
    engine.goToStep(QStringLiteral("s3"));
    EXPECT_EQ(engine.currentStep()->id, QStringLiteral("s3"));
    engine.goToStep(QStringLiteral("nonexistent"));
    EXPECT_EQ(engine.currentStep()->id, QStringLiteral("s3"));
}

// ---- commands are offered one at a time ----------------------------------

TEST(TutorialEngineTest, CommandsAreHandedOutInOrder)
{
    TutorialEngine engine(stubContent());
    ASSERT_NE(engine.nextCommand(), nullptr);
    EXPECT_EQ(engine.nextCommand()->text, QStringLiteral("units lj"));
    EXPECT_FALSE(engine.allCommandsInserted());

    EXPECT_EQ(engine.takeNextCommand(), QStringLiteral("units lj"));
    ASSERT_NE(engine.nextCommand(), nullptr);
    EXPECT_EQ(engine.nextCommand()->text, QStringLiteral("boundary p p p"));

    EXPECT_EQ(engine.takeNextCommand(), QStringLiteral("boundary p p p"));
    EXPECT_TRUE(engine.allCommandsInserted());
    EXPECT_EQ(engine.nextCommand(), nullptr);
    EXPECT_TRUE(engine.takeNextCommand().isEmpty());
}

TEST(TutorialEngineTest, InsertingEmitsTheCommand)
{
    TutorialEngine engine(stubContent());
    QString seen;
    QObject::connect(&engine, &TutorialEngine::commandInserted, &engine,
                     [&seen](const QString &text) {
                         seen = text;
                     });
    engine.takeNextCommand();
    EXPECT_EQ(seen, QStringLiteral("units lj"));
}

TEST(TutorialEngineTest, MovingOnResetsTheCommandCursor)
{
    TutorialEngine engine(stubContent());
    engine.takeNextCommand();
    EXPECT_EQ(engine.insertedCount(), 1);
    engine.next();
    EXPECT_EQ(engine.insertedCount(), 0);
}

// ---- the reminder budget -------------------------------------------------

TEST(TutorialEngineTest, ConceptsAreExplainedUntilTheBudgetRunsOut)
{
    TutorialEngine engine(stubContent());
    const QString id = QStringLiteral("units");

    for (int i = 0; i < Cfg::CONCEPT_REMINDER_BUDGET; ++i) {
        EXPECT_TRUE(engine.shouldExplain(id)) << "exposure " << i;
        engine.noteConceptsShown({id});
        engine.next(); // a new step lets the concept be counted again
    }
    EXPECT_EQ(engine.exposureCount(id), Cfg::CONCEPT_REMINDER_BUDGET);
    EXPECT_FALSE(engine.shouldExplain(id));
}

TEST(TutorialEngineTest, AConceptIsCountedOncePerStepNotPerRepaint)
{
    // stepping back and forth over a step must not exhaust its budget
    TutorialEngine engine(stubContent());
    const QString id = QStringLiteral("units");
    for (int i = 0; i < 10; ++i)
        engine.noteConceptsShown({id});
    EXPECT_EQ(engine.exposureCount(id), 1);
}

TEST(TutorialEngineTest, MovingToANewStepAllowsCountingAgain)
{
    TutorialEngine engine(stubContent());
    const QString id = QStringLiteral("units");
    engine.noteConceptsShown({id});
    engine.next();
    engine.noteConceptsShown({id});
    EXPECT_EQ(engine.exposureCount(id), 2);
}

TEST(TutorialEngineTest, AnEmptyConceptIdIsNeverExplainedOrCounted)
{
    TutorialEngine engine(stubContent());
    EXPECT_FALSE(engine.shouldExplain(QString()));
    engine.noteConceptsShown({QString()});
    EXPECT_EQ(engine.exposureCount(QString()), 0);
}

// ---- progress persistence ------------------------------------------------

TEST(TutorialEngineTest, CursorAndBudgetRoundTripThroughSettings)
{
    SettingsSandbox sandbox;
    {
        TutorialEngine engine(stubContent());
        engine.goToStep(QStringLiteral("s2"));
        engine.noteConceptsShown({QStringLiteral("units")});
        engine.saveProgress();
    }
    {
        TutorialEngine engine(stubContent());
        engine.restoreProgress();
        ASSERT_NE(engine.currentStep(), nullptr);
        EXPECT_EQ(engine.currentStep()->id, QStringLiteral("s2"));
        EXPECT_EQ(engine.exposureCount(QStringLiteral("units")), 1);
    }
}

TEST(TutorialEngineTest, TheReminderBudgetIsSharedAcrossTutorials)
{
    // a concept learned in one tutorial should not be re-taught in the next
    SettingsSandbox sandbox;
    {
        TutorialEngine engine(stubContent());
        for (int i = 0; i < Cfg::CONCEPT_REMINDER_BUDGET; ++i) {
            engine.noteConceptsShown({QStringLiteral("units")});
            engine.next();
        }
        engine.saveProgress();
    }
    {
        // a *different* tutorial id, same user
        QByteArray other = stubDocument();
        other.replace("\"id\": \"stub\"", "\"id\": \"other\"");
        TutorialEngine engine(parseTutorialJson(other));
        engine.restoreProgress();
        EXPECT_FALSE(engine.shouldExplain(QStringLiteral("units")));
    }
}

TEST(TutorialEngineTest, ResetClearsBothTheCursorAndTheReminders)
{
    SettingsSandbox sandbox;
    TutorialEngine engine(stubContent());
    engine.goToStep(QStringLiteral("s3"));
    engine.noteConceptsShown({QStringLiteral("units")});
    engine.saveProgress();

    engine.resetProgress();
    EXPECT_EQ(engine.currentStep()->id, QStringLiteral("s1"));
    EXPECT_EQ(engine.exposureCount(QStringLiteral("units")), 0);

    TutorialEngine fresh(stubContent());
    fresh.restoreProgress();
    EXPECT_EQ(fresh.currentStep()->id, QStringLiteral("s1"));
    EXPECT_TRUE(fresh.shouldExplain(QStringLiteral("units")));
}

TEST(TutorialEngineTest, ASavedStepThatNoLongerExistsStartsOver)
{
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


// A group is the unit the tour advances by: commands marked "together" travel
// with the one before them and are written in a single beat.
TEST(TutorialEngineTest, TogetherCommandsAreOfferedAndTakenAsOneGroup)
{
    const QByteArray doc = R"({
      "schema_version": 3, "id": "grp", "title": "Grouped",
      "attribution": { "license": "test" },
      "acts": [
        { "id": "a1", "title": "First", "steps": [
          { "id": "s1", "kind": "SHOW", "title": "Pairs", "teach": "t",
            "commands": [
              { "text": "mass 1 1.0", "explain": "light" },
              { "text": "mass 2 5.0", "explain": "heavy", "together": true },
              { "text": "pair_style lj/cut 4.0", "explain": "the model" }
            ] }
        ] }
      ]
    })";
    QList<ContentIssue> issues;
    const auto content = parseTutorialJson(doc, &issues);
    ASSERT_EQ(countContentErrors(issues), 0);

    TutorialEngine engine(content);
    // the first group is the two mass lines, not the pair_style that follows
    ASSERT_EQ(engine.nextGroup().size(), 2);
    EXPECT_EQ(engine.nextGroup().at(0).text, QStringLiteral("mass 1 1.0"));
    EXPECT_EQ(engine.nextGroup().at(1).text, QStringLiteral("mass 2 5.0"));

    const QStringList taken = engine.takeNextGroup();
    ASSERT_EQ(taken.size(), 2);
    EXPECT_EQ(engine.insertedCount(), 2);
    EXPECT_FALSE(engine.allCommandsInserted());

    // and the next beat is the single ungrouped command
    ASSERT_EQ(engine.nextGroup().size(), 1);
    EXPECT_EQ(engine.nextGroup().at(0).text, QStringLiteral("pair_style lj/cut 4.0"));
    engine.takeNextGroup();
    EXPECT_TRUE(engine.allCommandsInserted());
    EXPECT_TRUE(engine.nextGroup().isEmpty());
}

// Both lines of a group are recorded, so stepping back takes both out again.
TEST(TutorialEngineTest, AGroupIsRecordedLineByLineForRewinding)
{
    const QByteArray doc = R"({
      "schema_version": 3, "id": "grp2", "title": "Grouped",
      "attribution": { "license": "test" },
      "acts": [
        { "id": "a1", "title": "First", "steps": [
          { "id": "s1", "kind": "SHOW", "title": "Pairs", "teach": "t",
            "commands": [
              { "text": "region cyl_in cylinder z 0 0 10 INF INF side in", "explain": "in" },
              { "text": "region cyl_out cylinder z 0 0 10 INF INF side out",
                "explain": "out", "together": true }
            ] }
        ] }
      ]
    })";
    TutorialEngine engine(parseTutorialJson(doc));
    engine.takeNextGroup();
    EXPECT_EQ(engine.writtenFor(QStringLiteral("s1")).size(), 2);
    engine.forgetWritten(QStringLiteral("s1"));
    EXPECT_TRUE(engine.writtenFor(QStringLiteral("s1")).isEmpty());
}

// Local Variables:
// c-basic-offset: 4
// End:
