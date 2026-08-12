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
      "schema_version": 3,
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

const char *OBSERVE_STEP = R"({
  "id": "s-obs", "kind": "OBSERVE", "title": "Run it",
  "teach": "Now run what you have written.",
  "anchor": "run", "call_to_action": "Press the Run button.",
  "expect": "no error and a line of output"
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

TEST(TutorialContentTest, MetadataIsParsed)
{
    const auto content = parseTutorialJson(docWithSteps(SHOW_STEP));
    EXPECT_EQ(content.schemaVersion(), 3);
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

TEST(TutorialContentTest, RejectsOlderSchemaVersions)
{
    // v1 used verbs and "___" holes, v2 had parameter widgets; reading either
    // as v3 would silently misinterpret every step rather than fail
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

TEST(TutorialContentTest, ObserveStepLoadsCleanly)
{
    const QString steps =
        QString::fromLatin1(SHOW_STEP) + QStringLiteral(",") + QString::fromLatin1(OBSERVE_STEP);
    QList<ContentIssue> issues;
    const auto content = parseTutorialJson(docWithSteps(steps), &issues);

    EXPECT_EQ(countContentErrors(issues), 0) << qPrintable(formatContentIssues(issues));
    const TutorialStep *step = content.stepById(QStringLiteral("s-obs"));
    ASSERT_NE(step, nullptr);
    EXPECT_EQ(step->kind, StepKind::Observe);
    EXPECT_EQ(step->anchor, StepAnchor::Run);
    EXPECT_FALSE(step->callToAction.isEmpty());
}

TEST(TutorialContentTest, ObserveStepMayNotCarryCommands)
{
    // an OBSERVE points at something; commands belong to a SHOW
    QList<ContentIssue> issues;
    parseTutorialJson(docWithSteps(R"({"id":"s","kind":"OBSERVE","title":"T","teach":"t",
      "anchor":"chart", "commands":[{"text":"units lj"}]})"),
                      &issues);
    EXPECT_TRUE(messageMentions(issues, QStringLiteral("use SHOW to offer commands")));
}

TEST(TutorialContentTest, ObserveStepNeedsAnAnchor)
{
    QList<ContentIssue> issues;
    parseTutorialJson(docWithSteps(R"({"id":"s","kind":"OBSERVE","title":"T","teach":"t"})"),
                      &issues);
    EXPECT_TRUE(errorAt(issues, QStringLiteral("anchor")));
}

TEST(TutorialContentTest, ShowStepMustCarryCommands)
{
    QList<ContentIssue> issues;
    parseTutorialJson(docWithSteps(R"({"id":"s","kind":"SHOW","title":"T","teach":"t"})"), &issues);
    EXPECT_TRUE(messageMentions(issues, QStringLiteral("use OBSERVE")));
}

TEST(TutorialContentTest, RejectsUnknownAnchor)
{
    QList<ContentIssue> issues;
    parseTutorialJson(docWithSteps(R"({"id":"s","kind":"OBSERVE","title":"T","teach":"t",
      "anchor":"kitchen"})"),
                      &issues);
    EXPECT_TRUE(errorAt(issues, QStringLiteral("anchor")));
    EXPECT_TRUE(messageMentions(issues, QStringLiteral("unknown anchor")));
}

TEST(TutorialContentTest, RunAnchorNeedsSomethingInTheScriptFirst)
{
    // pressing Run on an empty buffer teaches nothing
    QList<ContentIssue> issues;
    parseTutorialJson(docWithSteps(OBSERVE_STEP), &issues);
    EXPECT_TRUE(messageMentions(issues, QStringLiteral("no earlier step has put anything")));
}

TEST(TutorialContentTest, OpenFileIsCarried)
{
    const QString steps =
        QString::fromLatin1(SHOW_STEP) + QStringLiteral(",{\"id\":\"s2\",\"kind\":\"OBSERVE\","
                                                        "\"title\":\"T\",\"teach\":\"t\","
                                                        "\"anchor\":\"editor\","
                                                        "\"open_file\":\"improved.min.lmp\"}");
    const auto content       = parseTutorialJson(docWithSteps(steps));
    const TutorialStep *step = content.stepById(QStringLiteral("s2"));
    ASSERT_NE(step, nullptr);
    EXPECT_EQ(step->openFile, QStringLiteral("improved.min.lmp"));
}

// ---- concepts and the reminder budget ------------------------------------

TEST(TutorialContentTest, ConceptsAreParsedAndLookedUpById)
{
    const auto content = parseTutorialJson(docWithSteps(
        R"({"id":"s","kind":"SHOW","title":"T","teach":"t",
            "commands":[{"text":"units lj","concept":"reduced-units"}]})",
        R"({"id":"reduced-units","term":"reduced units","explain":"everything is dimensionless"})"));

    ASSERT_EQ(content.concepts().size(), 1);
    const TutorialConcept *c = content.conceptFor(QStringLiteral("reduced-units"));
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->term, QStringLiteral("reduced units"));
    EXPECT_EQ(content.conceptFor(QStringLiteral("nope")), nullptr);
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
    parseTutorialJson(QByteArray(R"({"schema_version":3,"id":"x","title":"X",
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
    parseTutorialJson(QByteArray(R"({"schema_version":3,"id":"x","title":"X",
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
    EXPECT_EQ(stepKindName(StepKind::Observe), QStringLiteral("OBSERVE"));
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

TEST(TutorialContentTest, ShippedTutorialOneCoversBothHalves)
{
    // the second half of section 3.1 -- regions, groups, write_data and the
    // restart -- was missing entirely once, and is easy to lose again
    const auto content = loadTutorialFile(QStringLiteral(TUTORIAL_CONTENT_DIR "/lj-fluid.json"));

    QStringList shown;
    for (int a = 0; a < content.actCount(); ++a)
        for (int s = 0;; ++s) {
            const TutorialStep *step = content.step(a, s);
            if (!step) break;
            for (const auto &cmd : step->commands)
                shown << cmd.text.section(QLatin1Char(' '), 0, 0);
        }

    for (const auto &required :
         {"units", "region", "create_box", "create_atoms", "mass", "pair_style", "pair_coeff",
          "thermo", "minimize", "fix", "timestep", "run", "write_data", "read_data", "group",
          "delete_atoms", "variable", "compute", "velocity"})
        EXPECT_TRUE(shown.contains(QLatin1String(required)))
            << "the tutorial no longer teaches " << required;
}

TEST(TutorialContentTest, ShippedTutorialOneAnnotatesWhatItShows)
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
    EXPECT_GT(commands, 40);
    // annotation is the whole point of showing rather than asking.  Not every
    // line needs a note -- a repeated "mass 2 5.0" explains itself -- but a
    // tutorial where most commands arrive unannotated has become a transcript.
    EXPECT_GT(annotations * 2, commands);
}
#endif

// Local Variables:
// c-basic-offset: 4
// End:
