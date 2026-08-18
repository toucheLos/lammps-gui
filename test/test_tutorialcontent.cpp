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

// The grouping flag is part of schema 3, so it must not be reported as a key
// from a newer schema, and a group cannot open a step: "together" means
// "with the command before this one", and there is no such command.
TEST(TutorialContentTest, TogetherParsesAndCannotOpenAStep)
{
    const QByteArray ok = R"({
      "schema_version": 3, "id": "t", "title": "T",
      "attribution": { "license": "test" },
      "acts": [ { "id": "a1", "title": "A", "steps": [
        { "id": "s1", "kind": "SHOW", "title": "S", "teach": "t", "commands": [
          { "text": "mass 1 1.0", "explain": "light" },
          { "text": "mass 2 5.0", "explain": "heavy", "together": true } ] } ] } ]
    })";
    QList<ContentIssue> issues;
    const auto content = parseTutorialJson(ok, &issues);
    EXPECT_EQ(countContentErrors(issues), 0);
    EXPECT_TRUE(issues.isEmpty()) << qPrintable(formatContentIssues(issues));
    const TutorialStep *step = content.step(0, 0);
    ASSERT_NE(step, nullptr);
    ASSERT_EQ(step->commands.size(), 2);
    EXPECT_FALSE(step->commands.at(0).together);
    EXPECT_TRUE(step->commands.at(1).together);

    const QByteArray bad = R"({
      "schema_version": 3, "id": "t", "title": "T",
      "attribution": { "license": "test" },
      "acts": [ { "id": "a1", "title": "A", "steps": [
        { "id": "s1", "kind": "SHOW", "title": "S", "teach": "t", "commands": [
          { "text": "mass 1 1.0", "explain": "light", "together": true } ] } ] } ]
    })";
    QList<ContentIssue> badIssues;
    parseTutorialJson(bad, &badIssues);
    EXPECT_GT(countContentErrors(badIssues), 0);
    EXPECT_TRUE(formatContentIssues(badIssues).contains(QStringLiteral("first command")))
        << qPrintable(formatContentIssues(badIssues));
}

// "replaces" is how a step supersedes an earlier line rather than appending
// beside it.  A name that matches nothing is the dangerous case: the removal
// silently does nothing and the script quietly keeps both lines.
TEST(TutorialContentTest, ReplacesMustNameACommandAnEarlierStepWrites)
{
    const QByteArray ok = R"({
      "schema_version": 3, "id": "t", "title": "T",
      "attribution": { "license": "test" },
      "acts": [ { "id": "a1", "title": "A", "steps": [
        { "id": "s1", "kind": "SHOW", "title": "S", "teach": "t", "commands": [
          { "text": "run 0 post no", "explain": "one point" } ] },
        { "id": "s2", "kind": "SHOW", "title": "S", "teach": "t", "commands": [
          { "text": "minimize 1.0e-6 1.0e-6 1000 10000", "explain": "relax",
            "replaces": "run 0 post no" } ] } ] } ]
    })";
    QList<ContentIssue> issues;
    const auto content = parseTutorialJson(ok, &issues);
    EXPECT_EQ(countContentErrors(issues), 0) << qPrintable(formatContentIssues(issues));
    const TutorialStep *step = content.step(0, 1);
    ASSERT_NE(step, nullptr);
    EXPECT_EQ(step->commands.at(0).replaces, QStringLiteral("run 0 post no"));

    // the same content with the order reversed: nothing has written the line yet
    const QByteArray tooEarly = R"({
      "schema_version": 3, "id": "t", "title": "T",
      "attribution": { "license": "test" },
      "acts": [ { "id": "a1", "title": "A", "steps": [
        { "id": "s1", "kind": "SHOW", "title": "S", "teach": "t", "commands": [
          { "text": "minimize 1.0e-6 1.0e-6 1000 10000", "explain": "relax",
            "replaces": "run 0 post no" } ] } ] } ]
    })";
    QList<ContentIssue> lateIssues;
    parseTutorialJson(tooEarly, &lateIssues);
    EXPECT_GT(countContentErrors(lateIssues), 0);
    EXPECT_TRUE(formatContentIssues(lateIssues).contains(QStringLiteral("no earlier command")))
        << qPrintable(formatContentIssues(lateIssues));
}

// A typed line is offered blank for the user to fill in.  A group is pasted in
// one action.  A command cannot be both.
TEST(TutorialContentTest, ATypedCommandCannotAlsoBeGrouped)
{
    const QByteArray doc = R"({
      "schema_version": 3, "id": "t", "title": "T",
      "attribution": { "license": "test" },
      "acts": [ { "id": "a1", "title": "A", "steps": [
        { "id": "s1", "kind": "SHOW", "title": "S", "teach": "t", "commands": [
          { "text": "mass 1 1.0", "explain": "light" },
          { "text": "mass 2 5.0", "explain": "heavy", "typed": true, "together": true } ] } ] } ]
    })";
    QList<ContentIssue> issues;
    parseTutorialJson(doc, &issues);
    EXPECT_GT(countContentErrors(issues), 0);
    EXPECT_TRUE(formatContentIssues(issues).contains(QStringLiteral("nothing left for the user")))
        << qPrintable(formatContentIssues(issues));

    // and the mirror case: the drill itself stands alone, but the command after
    // it travels with it, so accepting the one typed line would consume both
    const QByteArray trailing = R"({
      "schema_version": 3, "id": "t", "title": "T",
      "attribution": { "license": "test" },
      "acts": [ { "id": "a1", "title": "A", "steps": [
        { "id": "s1", "kind": "SHOW", "title": "S", "teach": "t", "commands": [
          { "text": "thermo 50", "explain": "every fifty steps", "typed": true },
          { "text": "thermo_style custom step temp", "explain": "columns",
            "together": true } ] } ] } ]
    })";
    QList<ContentIssue> trailingIssues;
    parseTutorialJson(trailing, &trailingIssues);
    EXPECT_GT(countContentErrors(trailingIssues), 0);
    EXPECT_TRUE(formatContentIssues(trailingIssues).contains(QStringLiteral("has to stand alone")))
        << qPrintable(formatContentIssues(trailingIssues));
}

// A tune control edits an argument of a line the script already holds, so the
// command has to have been written and the two named values have to be
// reachable with the control the user is given.
TEST(TutorialContentTest, TuneNeedsAnEarlierCommandAndAReachableRange)
{
    const QByteArray ok = R"({
      "schema_version": 3, "id": "t", "title": "T",
      "attribution": { "license": "test" },
      "acts": [ { "id": "a1", "title": "A", "steps": [
        { "id": "s1", "kind": "SHOW", "title": "S", "teach": "t", "commands": [
          { "text": "run 25000", "explain": "dynamics" } ] },
        { "id": "s2", "kind": "OBSERVE", "title": "S", "teach": "t", "anchor": "editor",
          "tune": { "command": "run", "arg": 1, "from": 25000, "to": 15000,
                    "min": 5000, "max": 50000, "label": "Steps:" } } ] } ]
    })";
    QList<ContentIssue> issues;
    const auto content = parseTutorialJson(ok, &issues);
    EXPECT_EQ(countContentErrors(issues), 0) << qPrintable(formatContentIssues(issues));
    const TutorialStep *step = content.step(0, 1);
    ASSERT_NE(step, nullptr);
    EXPECT_TRUE(step->tune.isValid());
    EXPECT_EQ(step->tune.command, QStringLiteral("run"));
    EXPECT_EQ(step->tune.argIndex, 1);
    // a step without one carries an invalid control rather than a null pointer
    EXPECT_FALSE(content.step(0, 0)->tune.isValid());

    // tuning a command nothing has written
    const QByteArray unwritten = R"({
      "schema_version": 3, "id": "t", "title": "T",
      "attribution": { "license": "test" },
      "acts": [ { "id": "a1", "title": "A", "steps": [
        { "id": "s1", "kind": "OBSERVE", "title": "S", "teach": "t", "anchor": "editor",
          "tune": { "command": "timestep", "arg": 1, "from": 1, "to": 2 } } ] } ]
    })";
    QList<ContentIssue> unwrittenIssues;
    parseTutorialJson(unwritten, &unwrittenIssues);
    EXPECT_GT(countContentErrors(unwrittenIssues), 0);
    EXPECT_TRUE(
        formatContentIssues(unwrittenIssues).contains(QStringLiteral("no earlier step")))
        << qPrintable(formatContentIssues(unwrittenIssues));

    // a target the control cannot reach
    const QByteArray unreachable = R"({
      "schema_version": 3, "id": "t", "title": "T",
      "attribution": { "license": "test" },
      "acts": [ { "id": "a1", "title": "A", "steps": [
        { "id": "s1", "kind": "SHOW", "title": "S", "teach": "t", "commands": [
          { "text": "run 25000", "explain": "dynamics" } ] },
        { "id": "s2", "kind": "OBSERVE", "title": "S", "teach": "t", "anchor": "editor",
          "tune": { "command": "run", "arg": 1, "from": 25000, "to": 15000,
                    "min": 20000, "max": 50000 } } ] } ]
    })";
    QList<ContentIssue> rangeIssues;
    parseTutorialJson(unreachable, &rangeIssues);
    EXPECT_GT(countContentErrors(rangeIssues), 0);
    EXPECT_TRUE(formatContentIssues(rangeIssues).contains(QStringLiteral("cannot reach")))
        << qPrintable(formatContentIssues(rangeIssues));
}

// Argument 0 is the command word; editing it would rewrite the command itself.
TEST(TutorialContentTest, TuneCannotTargetTheCommandWord)
{
    const QByteArray doc = R"({
      "schema_version": 3, "id": "t", "title": "T",
      "attribution": { "license": "test" },
      "acts": [ { "id": "a1", "title": "A", "steps": [
        { "id": "s1", "kind": "SHOW", "title": "S", "teach": "t", "commands": [
          { "text": "run 25000", "explain": "dynamics" } ] },
        { "id": "s2", "kind": "OBSERVE", "title": "S", "teach": "t", "anchor": "editor",
          "tune": { "command": "run", "arg": 0, "from": 1, "to": 2, "max": 3 } } ] } ]
    })";
    QList<ContentIssue> issues;
    parseTutorialJson(doc, &issues);
    EXPECT_GT(countContentErrors(issues), 0);
    EXPECT_TRUE(formatContentIssues(issues).contains(QStringLiteral("command word itself")))
        << qPrintable(formatContentIssues(issues));
}

// "section" files a step's commands under a heading; "before" files them above
// a named line.  They are two answers to the same question, and a step that
// gives both leaves the editor to pick one silently.
TEST(TutorialContentTest, SectionAndBeforeAreMutuallyExclusive)
{
    const QByteArray ok = R"({
      "schema_version": 3, "id": "t", "title": "T",
      "attribution": { "license": "test" },
      "acts": [ { "id": "a1", "title": "A", "steps": [
        { "id": "s1", "kind": "SHOW", "title": "S", "teach": "t",
          "before": "run 0 post no",
          "commands": [ { "text": "group carbon_atoms type 1", "explain": "all" } ] } ] } ]
    })";
    QList<ContentIssue> issues;
    const auto content = parseTutorialJson(ok, &issues);
    EXPECT_EQ(countContentErrors(issues), 0) << qPrintable(formatContentIssues(issues));
    EXPECT_TRUE(issues.isEmpty()) << qPrintable(formatContentIssues(issues));
    const TutorialStep *step = content.step(0, 0);
    ASSERT_NE(step, nullptr);
    EXPECT_EQ(step->before, QStringLiteral("run 0 post no"));
    EXPECT_TRUE(step->section.isEmpty());

    const QByteArray both = R"({
      "schema_version": 3, "id": "t", "title": "T",
      "attribution": { "license": "test" },
      "skeleton": [ "# 5) Run" ],
      "acts": [ { "id": "a1", "title": "A", "steps": [
        { "id": "s1", "kind": "SHOW", "title": "S", "teach": "t",
          "section": "# 5) Run", "before": "run 0 post no",
          "commands": [ { "text": "group carbon_atoms type 1", "explain": "all" } ] } ] } ]
    })";
    QList<ContentIssue> bothIssues;
    parseTutorialJson(both, &bothIssues);
    EXPECT_GT(countContentErrors(bothIssues), 0);
    EXPECT_TRUE(formatContentIssues(bothIssues).contains(QStringLiteral("not both")))
        << qPrintable(formatContentIssues(bothIssues));
}

// A question the user answers silently and is never marked on is worse than no
// question, so a prediction has to come with the answer that resolves it.
TEST(TutorialContentTest, APredictionNeedsAnAnswer)
{
    const QByteArray ok = R"({
      "schema_version": 3, "id": "t", "title": "T",
      "attribution": { "license": "test" },
      "acts": [ { "id": "a1", "title": "A", "steps": [
        { "id": "s1", "kind": "SHOW", "title": "S", "teach": "t",
          "commands": [ { "text": "run 100", "explain": "go" } ] },
        { "id": "s2", "kind": "OBSERVE", "title": "S", "teach": "t", "anchor": "run",
          "call_to_action": "Press the Run button.",
          "predict": "will the energy rise or fall?",
          "expect": "It falls, then levels off." } ] } ]
    })";
    QList<ContentIssue> issues;
    const auto content = parseTutorialJson(ok, &issues);
    EXPECT_EQ(countContentErrors(issues), 0) << qPrintable(formatContentIssues(issues));
    EXPECT_TRUE(issues.isEmpty()) << qPrintable(formatContentIssues(issues));
    ASSERT_NE(content.step(0, 1), nullptr);
    EXPECT_FALSE(content.step(0, 1)->predict.isEmpty());

    const QByteArray unresolved = R"({
      "schema_version": 3, "id": "t", "title": "T",
      "attribution": { "license": "test" },
      "acts": [ { "id": "a1", "title": "A", "steps": [
        { "id": "s1", "kind": "SHOW", "title": "S", "teach": "t",
          "commands": [ { "text": "run 100", "explain": "go" } ] },
        { "id": "s2", "kind": "OBSERVE", "title": "S", "teach": "t", "anchor": "run",
          "call_to_action": "Press the Run button.",
          "predict": "will the energy rise or fall?" } ] } ]
    })";
    QList<ContentIssue> bad;
    parseTutorialJson(unresolved, &bad);
    EXPECT_GT(countContentErrors(bad), 0);
    EXPECT_TRUE(formatContentIssues(bad).contains(QStringLiteral("never resolved")))
        << qPrintable(formatContentIssues(bad));
}

// A drill with nothing to fall back on is a memory test the user did not sign
// up for, so a typed command has to carry a hint.
TEST(TutorialContentTest, ATypedCommandNeedsAHint)
{
    const QByteArray doc = R"({
      "schema_version": 3, "id": "t", "title": "T",
      "attribution": { "license": "test" },
      "acts": [ { "id": "a1", "title": "A", "steps": [
        { "id": "s1", "kind": "SHOW", "title": "S", "teach": "t", "commands": [
          { "text": "thermo 50", "explain": "every fifty steps", "typed": true } ] } ] } ]
    })";
    QList<ContentIssue> issues;
    parseTutorialJson(doc, &issues);
    EXPECT_GT(countContentErrors(issues), 0);
    EXPECT_TRUE(formatContentIssues(issues).contains(QStringLiteral("needs a hint")))
        << qPrintable(formatContentIssues(issues));
}

// A script that arrives complete may be run before the tour has written
// anything -- that is the whole point of the given-and-modify style.
TEST(TutorialContentTest, ACompleteScriptMayBeRunBeforeAnyCommand)
{
    const QByteArray doc = R"({
      "schema_version": 3, "id": "t", "title": "T",
      "attribution": { "license": "test" },
      "acts": [ { "id": "a1", "title": "A", "steps": [
        { "id": "s1", "kind": "OBSERVE", "title": "S", "teach": "t", "anchor": "run",
          "call_to_action": "Run it." },
        { "id": "s2", "kind": "SHOW", "title": "S", "teach": "t", "before": "run 0 post no",
          "commands": [ { "text": "group all_atoms type 1", "explain": "everything" } ] } ] } ]
    })";
    QList<ContentIssue> issues;
    parseTutorialJson(doc, &issues);
    EXPECT_EQ(countContentErrors(issues), 0) << qPrintable(formatContentIssues(issues));
}

// A LAMMPS command may span several lines with a trailing "&".  Splitting those
// into separate entries would put half a command on screen with nothing
// sensible to say about it, so a newline is allowed exactly where a
// continuation marker puts one -- and nowhere else.
TEST(TutorialContentTest, ContinuedCommandsMaySpanLines)
{
    const QByteArray ok = R"({
      "schema_version": 3, "id": "t", "title": "T",
      "attribution": { "license": "test" },
      "acts": [ { "id": "a1", "title": "A", "steps": [
        { "id": "s1", "kind": "SHOW", "title": "S", "teach": "t", "commands": [
          { "text": "create_box 8 box &\nbond/types 7 &\nangle/types 8",
            "explain": "room for everything" } ] } ] } ]
    })";
    QList<ContentIssue> issues;
    const auto content = parseTutorialJson(ok, &issues);
    EXPECT_EQ(countContentErrors(issues), 0) << qPrintable(formatContentIssues(issues));
    ASSERT_NE(content.step(0, 0), nullptr);
    EXPECT_TRUE(content.step(0, 0)->commands.at(0).text.contains(QLatin1Char('\n')));

    // the same text without the continuation markers is two commands crammed
    // into one entry, which is what the rule exists to catch
    const QByteArray bad = R"({
      "schema_version": 3, "id": "t", "title": "T",
      "attribution": { "license": "test" },
      "acts": [ { "id": "a1", "title": "A", "steps": [
        { "id": "s1", "kind": "SHOW", "title": "S", "teach": "t", "commands": [
          { "text": "thermo 100\nthermo_style custom step temp", "explain": "two things" } ] } ] } ]
    })";
    QList<ContentIssue> badIssues;
    parseTutorialJson(bad, &badIssues);
    EXPECT_GT(countContentErrors(badIssues), 0);
    EXPECT_TRUE(formatContentIssues(badIssues).contains(QStringLiteral("one command per entry")))
        << qPrintable(formatContentIssues(badIssues));
}

// Local Variables:
// c-basic-offset: 4
// End:
