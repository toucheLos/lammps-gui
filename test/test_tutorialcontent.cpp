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

#include "tutorialcontent.h"

#include <gtest/gtest.h>

#include <QByteArray>
#include <QString>
#include <QStringList>

namespace {

// ---- document builders ---------------------------------------------------
// Every test starts from a document that loads cleanly and changes exactly one
// thing, so a failure names the rule that broke rather than a whole file.

QByteArray docWithSteps(const QString &steps, const QString &concepts = QString())
{
    return QStringLiteral(R"({
      "schema_version": 2,
      "id": "lj-fluid",
      "title": "Tutorial 1",
      "collection": "softmatter",
      "tutorial": 1,
      "skeleton_file": "initial.lmp",
      "attribution": { "source": "https://example.org", "license": "CC-BY-4.0",
                       "credit": "The tutorial authors" },
      "concepts": [ %2 ],
      "acts": [ { "id": "act1", "title": "Build the world", "steps": [ %1 ] } ]
    })")
        .arg(steps, concepts)
        .toUtf8();
}

const char *SHOW_STEP = R"({
  "id": "s-show", "kind": "SHOW", "title": "Choose units",
  "teach": "Reduced units make the LJ parameters exactly one.",
  "doc_link": "units",
  "commands": [
    { "text": "units lj", "explain": "Work in reduced units.",
      "notes": [ { "arg": 1, "note": "the unit system",
                   "alternatives": "real gives angstroms and kcal/mol" } ] }
  ]
})";

const char *EXPERIMENT_STEP = R"({
  "id": "s-exp", "kind": "EXPERIMENT", "title": "Push the timestep",
  "teach": "Raise it until the integrator fails.",
  "params": [
    { "id": "dt", "label": "timestep", "kind": "number",
      "command": "units", "arg": 1,
      "min": 0.001, "max": 0.06, "step": 0.005, "initial": 0.005 }
  ],
  "expect": "the total energy trace"
})";

/// true when some finding of ERROR severity mentions @p fragment in its path
bool errorAt(const QList<ContentIssue> &issues, const QString &fragment)
{
    for (const auto &issue : issues)
        if (issue.severity == ContentSeverity::Error && issue.path.contains(fragment)) return true;
    return false;
}

/// true when some finding of WARNING severity mentions @p fragment in its path
bool warningAt(const QList<ContentIssue> &issues, const QString &fragment)
{
    for (const auto &issue : issues)
        if (issue.severity == ContentSeverity::Warning && issue.path.contains(fragment))
            return true;
    return false;
}

/// true when any finding's message contains @p fragment
bool messageMentions(const QList<ContentIssue> &issues, const QString &fragment)
{
    for (const auto &issue : issues)
        if (issue.message.contains(fragment)) return true;
    return false;
}

} // namespace

// ---- happy path ----------------------------------------------------------

TEST(TutorialContentTest, ShowStepLoadsCleanly)
{
    QList<ContentIssue> issues;
    const auto content = parseTutorialJson(docWithSteps(SHOW_STEP), &issues);

    EXPECT_EQ(countContentErrors(issues), 0) << qPrintable(formatContentIssues(issues));
    EXPECT_FALSE(content.isEmpty());
    EXPECT_EQ(content.stepCount(), 1);

    const TutorialStep *step = content.stepById(QStringLiteral("s-show"));
    ASSERT_NE(step, nullptr);
    EXPECT_EQ(step->kind, StepKind::Show);
    ASSERT_EQ(step->commands.size(), 1);
    EXPECT_EQ(step->commands.at(0).text, QStringLiteral("units lj"));
    ASSERT_EQ(step->commands.at(0).notes.size(), 1);
    EXPECT_EQ(step->commands.at(0).notes.at(0).argIndex, 1);
    EXPECT_FALSE(step->commands.at(0).notes.at(0).alternatives.isEmpty());
}

TEST(TutorialContentTest, ExperimentStepLoadsCleanly)
{
    const QString steps =
        QString::fromLatin1(SHOW_STEP) + QStringLiteral(",") + QString::fromLatin1(EXPERIMENT_STEP);
    QList<ContentIssue> issues;
    const auto content = parseTutorialJson(docWithSteps(steps), &issues);

    EXPECT_EQ(countContentErrors(issues), 0) << qPrintable(formatContentIssues(issues));
    const TutorialStep *step = content.stepById(QStringLiteral("s-exp"));
    ASSERT_NE(step, nullptr);
    EXPECT_EQ(step->kind, StepKind::Experiment);
    ASSERT_EQ(step->params.size(), 1);
    EXPECT_EQ(step->params.at(0).kind, ParamKind::Number);
    EXPECT_DOUBLE_EQ(step->params.at(0).initial, 0.005);
}

TEST(TutorialContentTest, MetadataIsParsed)
{
    const auto content = parseTutorialJson(docWithSteps(SHOW_STEP));
    EXPECT_EQ(content.schemaVersion(), 2);
    EXPECT_EQ(content.id(), QStringLiteral("lj-fluid"));
    EXPECT_EQ(content.tutorialNumber(), 1);
    EXPECT_EQ(content.skeletonFile(), QStringLiteral("initial.lmp"));
    EXPECT_EQ(content.attribution().license, QStringLiteral("CC-BY-4.0"));
}

TEST(TutorialContentTest, DocLinkSplitsCommandAndStyle)
{
    const auto content       = parseTutorialJson(docWithSteps(R"({
      "id": "s", "kind": "SHOW", "title": "T", "teach": "t",
      "doc_link": "pair_style lj/cut",
      "commands": [ { "text": "pair_style lj/cut 4.0" } ] })"));
    const TutorialStep *step = content.stepById(QStringLiteral("s"));
    ASSERT_NE(step, nullptr);
    EXPECT_EQ(step->docCommand, QStringLiteral("pair_style"));
    EXPECT_EQ(step->docStyle, QStringLiteral("lj/cut"));
}

// ---- schema version ------------------------------------------------------

TEST(TutorialContentTest, RejectsTheVersionOneFormat)
{
    // v1 used verbs, validators and "___" holes; reading such a file as v2
    // would silently misinterpret every step rather than fail
    QList<ContentIssue> issues;
    parseTutorialJson(QByteArray(R"({"schema_version": 1, "id":"x","title":"X",
      "acts":[{"id":"a","title":"A","steps":[
        {"id":"s","verb":"TYPE","title":"T","teach":"t"}]}]})"),
                      &issues);
    EXPECT_TRUE(errorAt(issues, QStringLiteral("schema_version")));
    EXPECT_TRUE(messageMentions(issues, QStringLiteral("unsupported schema version")));
}

TEST(TutorialContentTest, RejectsMissingSchemaVersion)
{
    QList<ContentIssue> issues;
    parseTutorialJson(QByteArray(R"({"id":"x","title":"X","acts":[]})"), &issues);
    EXPECT_TRUE(errorAt(issues, QStringLiteral("schema_version")));
}

TEST(TutorialContentTest, RejectsMalformedJson)
{
    QList<ContentIssue> issues;
    const auto content = parseTutorialJson(QByteArray("{ not json"), &issues);
    EXPECT_TRUE(content.isEmpty());
    EXPECT_TRUE(messageMentions(issues, QStringLiteral("not valid JSON")));
}

TEST(TutorialContentTest, UnknownKeysWarnButStillLoad)
{
    QByteArray doc = docWithSteps(SHOW_STEP);
    doc.replace("\"id\": \"lj-fluid\"", "\"id\": \"lj-fluid\", \"future_key\": 42");

    QList<ContentIssue> issues;
    const auto content = parseTutorialJson(doc, &issues);
    EXPECT_EQ(countContentErrors(issues), 0) << qPrintable(formatContentIssues(issues));
    EXPECT_TRUE(warningAt(issues, QStringLiteral("future_key")));
    EXPECT_FALSE(content.isEmpty());
}

// ---- step kinds ----------------------------------------------------------

TEST(TutorialContentTest, RejectsUnknownStepKind)
{
    QList<ContentIssue> issues;
    parseTutorialJson(docWithSteps(R"({"id":"s","kind":"QUIZ","title":"T","teach":"t"})"), &issues);
    EXPECT_TRUE(errorAt(issues, QStringLiteral("kind")));
    EXPECT_TRUE(messageMentions(issues, QStringLiteral("unknown step kind")));
}

TEST(TutorialContentTest, ShowStepMustPresentSomething)
{
    QList<ContentIssue> issues;
    parseTutorialJson(docWithSteps(R"({"id":"s","kind":"SHOW","title":"T"})"), &issues);
    EXPECT_TRUE(messageMentions(issues, QStringLiteral("presents nothing")));
}

TEST(TutorialContentTest, ExperimentNeedsAParameter)
{
    QList<ContentIssue> issues;
    parseTutorialJson(docWithSteps(R"({"id":"s","kind":"EXPERIMENT","title":"T","teach":"t"})"),
                      &issues);
    EXPECT_TRUE(errorAt(issues, QStringLiteral("params")));
    EXPECT_TRUE(messageMentions(issues, QStringLiteral("just a run")));
}

TEST(TutorialContentTest, MultiLineCommandTextIsRejected)
{
    // commands are presented one line at a time so each can be annotated
    QList<ContentIssue> issues;
    parseTutorialJson(docWithSteps(R"({"id":"s","kind":"SHOW","title":"T","teach":"t",
      "commands":[{"text":"units lj\nboundary p p p"}]})"),
                      &issues);
    EXPECT_TRUE(messageMentions(issues, QStringLiteral("one command per entry")));
}

// ---- parameters ----------------------------------------------------------

TEST(TutorialContentTest, ParameterMustBindToACommandThatWasShown)
{
    // otherwise the run silently changes nothing
    QList<ContentIssue> issues;
    parseTutorialJson(docWithSteps(R"({"id":"s","kind":"EXPERIMENT","title":"T","teach":"t",
      "params":[{"id":"p","label":"L","command":"nosuchcommand","arg":1,
                 "min":0,"max":1}], "expect":"e"})"),
                      &issues);
    EXPECT_TRUE(messageMentions(issues, QStringLiteral("no earlier step puts in the script")));
}

TEST(TutorialContentTest, ParameterCannotRewriteTheCommandWord)
{
    QList<ContentIssue> issues;
    parseTutorialJson(docWithSteps(R"({"id":"s","kind":"EXPERIMENT","title":"T","teach":"t",
      "params":[{"id":"p","label":"L","command":"units","arg":0,
                 "min":0,"max":1}], "expect":"e"})"),
                      &issues);
    EXPECT_TRUE(errorAt(issues, QStringLiteral("arg")));
}

TEST(TutorialContentTest, RejectsInvertedParameterRange)
{
    QList<ContentIssue> issues;
    parseTutorialJson(docWithSteps(QString::fromLatin1(EXPERIMENT_STEP)
                                       .replace(QStringLiteral("\"min\": 0.001"),
                                                QStringLiteral("\"min\": 9.0"))),
                      &issues);
    EXPECT_TRUE(messageMentions(issues, QStringLiteral("must be below max")));
}

TEST(TutorialContentTest, RejectsInitialOutsideTheRange)
{
    QList<ContentIssue> issues;
    parseTutorialJson(docWithSteps(QString::fromLatin1(EXPERIMENT_STEP)
                                       .replace(QStringLiteral("\"initial\": 0.005"),
                                                QStringLiteral("\"initial\": 5.0"))),
                      &issues);
    EXPECT_TRUE(errorAt(issues, QStringLiteral("initial")));
}

TEST(TutorialContentTest, ChoiceParameterNeedsTwoChoices)
{
    QList<ContentIssue> issues;
    parseTutorialJson(docWithSteps(R"({"id":"s","kind":"EXPERIMENT","title":"T","teach":"t",
      "params":[{"id":"p","label":"L","kind":"choice","command":"units","arg":1,
                 "choices":["lj"]}], "expect":"e"})"),
                      &issues);
    EXPECT_TRUE(errorAt(issues, QStringLiteral("choices")));
}

// ---- concepts and the reminder budget ------------------------------------

TEST(TutorialContentTest, ConceptsAreParsedAndLookedUpById)
{
    const auto content = parseTutorialJson(docWithSteps(
        R"({"id":"s","kind":"SHOW","title":"T","teach":"t",
            "commands":[{"text":"units lj","concept":"reduced-units"}]})",
        R"({"id":"reduced-units","term":"reduced units","explain":"everything is dimensionless"})"));

    ASSERT_EQ(content.concepts().size(), 1);
    const TutorialConcept *c = content.concept(QStringLiteral("reduced-units"));
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->term, QStringLiteral("reduced units"));
    EXPECT_EQ(content.concept(QStringLiteral("nope")), nullptr);
}

TEST(TutorialContentTest, AnnotationReferencingAnUndeclaredConceptIsRejected)
{
    // it would silently show nothing, which review does not catch
    QList<ContentIssue> issues;
    parseTutorialJson(docWithSteps(R"({"id":"s","kind":"SHOW","title":"T","teach":"t",
      "commands":[{"text":"units lj","concept":"never-declared"}]})"),
                      &issues);
    EXPECT_TRUE(messageMentions(issues, QStringLiteral("undeclared concept")));
}

TEST(TutorialContentTest, UnusedConceptWarns)
{
    QList<ContentIssue> issues;
    parseTutorialJson(
        docWithSteps(SHOW_STEP, R"({"id":"orphan","term":"orphan","explain":"never referenced"})"),
        &issues);
    EXPECT_EQ(countContentErrors(issues), 0) << qPrintable(formatContentIssues(issues));
    EXPECT_TRUE(messageMentions(issues, QStringLiteral("never referenced")));
}

TEST(TutorialContentTest, RejectsDuplicateConceptIds)
{
    QList<ContentIssue> issues;
    parseTutorialJson(docWithSteps(
                          R"({"id":"s","kind":"SHOW","title":"T","teach":"t",
                              "commands":[{"text":"units lj","concept":"dup"}]})",
                          R"({"id":"dup","term":"a","explain":"a"},
                              {"id":"dup","term":"b","explain":"b"})"),
                      &issues);
    EXPECT_TRUE(messageMentions(issues, QStringLiteral("duplicate concept id")));
}

// ---- structure -----------------------------------------------------------

TEST(TutorialContentTest, RejectsDuplicateStepIds)
{
    const QString steps =
        QString::fromLatin1(SHOW_STEP) + QStringLiteral(",") + QString::fromLatin1(SHOW_STEP);
    QList<ContentIssue> issues;
    parseTutorialJson(docWithSteps(steps), &issues);
    EXPECT_TRUE(messageMentions(issues, QStringLiteral("duplicate step id")));
}

TEST(TutorialContentTest, RejectsActWithNoSteps)
{
    QList<ContentIssue> issues;
    parseTutorialJson(QByteArray(R"({"schema_version":2,"id":"x","title":"X",
      "acts":[{"id":"a","title":"A","steps":[]}]})"),
                      &issues);
    EXPECT_TRUE(errorAt(issues, QStringLiteral("steps")));
}

TEST(TutorialContentTest, ErroneousContentIsNotHandedOut)
{
    QList<ContentIssue> issues;
    const auto content = parseTutorialJson(
        docWithSteps(R"({"id":"s","kind":"EXPERIMENT","title":"T","teach":"t"})"), &issues);
    EXPECT_GT(countContentErrors(issues), 0);
    EXPECT_TRUE(content.isEmpty());
}

TEST(TutorialContentTest, MissingLicenseIsWarnedAbout)
{
    QList<ContentIssue> issues;
    parseTutorialJson(QByteArray(R"({"schema_version":2,"id":"x","title":"X",
      "acts":[{"id":"a","title":"A","steps":[
        {"id":"s","kind":"SHOW","title":"T","teach":"t"}]}]})"),
                      &issues);
    EXPECT_TRUE(warningAt(issues, QStringLiteral("attribution.license")));
}

TEST(TutorialContentTest, MissingFileIsReportedNotCrashed)
{
    QList<ContentIssue> issues;
    const auto content = loadTutorialFile(QStringLiteral("/nonexistent/tutorial.json"), &issues);
    EXPECT_TRUE(content.isEmpty());
    EXPECT_TRUE(messageMentions(issues, QStringLiteral("cannot read")));
}

TEST(TutorialContentTest, FormatIssuesTruncatesAndCounts)
{
    const QList<ContentIssue> issues = {
        {QStringLiteral("a"), ContentSeverity::Error, QStringLiteral("first")},
        {QStringLiteral("b"), ContentSeverity::Warning, QStringLiteral("second")},
        {QStringLiteral("c"), ContentSeverity::Error, QStringLiteral("third")},
    };
    EXPECT_EQ(countContentErrors(issues), 2);
    EXPECT_TRUE(formatContentIssues(issues).contains(QStringLiteral("ERROR: a: first")));

    const QString cut = formatContentIssues(issues, 1);
    EXPECT_FALSE(cut.contains(QStringLiteral("third")));
    EXPECT_TRUE(cut.contains(QStringLiteral("... and 2 more")));
}

TEST(TutorialContentTest, StepKindNamesMatchTheFileSpelling)
{
    EXPECT_EQ(stepKindName(StepKind::Show), QStringLiteral("SHOW"));
    EXPECT_EQ(stepKindName(StepKind::Experiment), QStringLiteral("EXPERIMENT"));
}

// ---- the shipped content -------------------------------------------------
// Content rot is the failure mode these guard against: a schema change that
// invalidates the authored tutorial has to fail here, not in front of a user.

#ifdef TUTORIAL_CONTENT_DIR
TEST(TutorialContentTest, ShippedTutorialOneLoadsWithoutIssues)
{
    const QString path = QStringLiteral(TUTORIAL_CONTENT_DIR "/lj-fluid.json");
    QList<ContentIssue> issues;
    const auto content = loadTutorialFile(path, &issues);

    EXPECT_EQ(countContentErrors(issues), 0) << qPrintable(formatContentIssues(issues));
    EXPECT_TRUE(issues.isEmpty()) << qPrintable(formatContentIssues(issues));
    EXPECT_EQ(content.id(), QStringLiteral("lj-fluid"));
    EXPECT_GT(content.stepCount(), 5);
    EXPECT_GT(content.concepts().size(), 5);
}

TEST(TutorialContentTest, ShippedTutorialOneKeepsTheControlledFailure)
{
    // the deliberate instability is the reason this feature exists; if it ever
    // disappears from the content the tutorial has lost its point
    const auto content = loadTutorialFile(QStringLiteral(TUTORIAL_CONTENT_DIR "/lj-fluid.json"));

    const TutorialStep *breakit = content.stepById(QStringLiteral("a4-s1"));
    ASSERT_NE(breakit, nullptr);
    EXPECT_EQ(breakit->kind, StepKind::Experiment);
    ASSERT_FALSE(breakit->params.isEmpty());
    EXPECT_EQ(breakit->params.at(0).command, QStringLiteral("timestep"));
    // the range must actually reach the unstable region, or nothing breaks
    EXPECT_GT(breakit->params.at(0).max, 0.02);
}

TEST(TutorialContentTest, ShippedTutorialOneShowsCommandsRatherThanQuizzing)
{
    const auto content = loadTutorialFile(QStringLiteral(TUTORIAL_CONTENT_DIR "/lj-fluid.json"));
    int commands       = 0;
    int annotations    = 0;
    for (int a = 0; a < content.actCount(); ++a) {
        for (int s = 0;; ++s) {
            const TutorialStep *step = content.step(a, s);
            if (!step) break;
            commands += static_cast<int>(step->commands.size());
            for (const auto &cmd : step->commands)
                annotations += static_cast<int>(cmd.notes.size());
        }
    }
    EXPECT_GT(commands, 15);
    // annotation is the whole point of showing rather than asking
    EXPECT_GT(annotations, commands);
}
#endif

// Local Variables:
// c-basic-offset: 4
// End:
