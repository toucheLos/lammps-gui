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
// Every test starts from a document that loads cleanly and changes exactly
// one thing, so a failure names the rule that broke rather than a whole file.

QByteArray docWithSteps(const QString &steps)
{
    return QStringLiteral(R"({
      "schema_version": 1,
      "id": "lj-fluid",
      "title": "Tutorial 1",
      "collection": "softmatter",
      "tutorial": 1,
      "skeleton_file": "in.lj.skeleton",
      "attribution": { "source": "https://example.org", "license": "CC-BY-4.0",
                       "credit": "The tutorial authors" },
      "acts": [ { "id": "act1", "title": "Define the world", "steps": [ %1 ] } ]
    })")
        .arg(steps)
        .toUtf8();
}

const char *READ_STEP = R"({
  "id": "s-read", "verb": "READ", "title": "What LAMMPS does",
  "teach": "LAMMPS integrates Newton's equations for a set of particles."
})";

const char *TYPE_STEP = R"({
  "id": "s-type", "verb": "TYPE", "title": "Choose reduced units",
  "teach": "Reduced units make the LJ parameters exactly one.",
  "doc_link": "units",
  "validate": { "type": "exact_tokens",
                "rules": [ { "type": "exact", "text": "units" },
                           { "type": "exact", "text": "lj" } ] },
  "feedback": { "correct": "Reduced units it is." }
})";

const char *FILL_STEP = R"({
  "id": "s-fill", "verb": "FILL", "title": "Set the cutoff",
  "teach": "LJ forces decay as r^-6, so we truncate.",
  "doc_link": "pair_style lj/cut",
  "editor": { "target_line": "append", "skeleton": "pair_style lj/cut ___" },
  "validate": { "type": "numeric_range",
                "rules": [ { "type": "numeric_range", "min": 2.0, "max": 5.0,
                             "ideal": 2.5, "label": "cutoff distance" } ] },
  "feedback": { "below": "Too short.", "above": "Wasteful." }
})";

const char *FIX_STEP = R"({
  "id": "s-fix", "verb": "FIX", "title": "Repair the pair_coeff",
  "teach": "Read the error and see which argument is missing.",
  "editor": { "target_line": "append", "skeleton": "pair_coeff 1 1 1.0" },
  "validate": { "type": "parses_clean" },
  "feedback": { "parse_error": "use_lammps_message" }
})";

const char *PREDICT_STEP = R"({
  "id": "s-predict", "verb": "PREDICT", "title": "Will energy drift?",
  "teach": "NVE conserves total energy in exact arithmetic.",
  "options": [ { "text": "It stays flat", "feedback": "Only with a small enough timestep." },
               { "text": "It drifts upward", "feedback": "Right, integration error accumulates." } ],
  "validate": { "type": "choice", "correct_option": 1 }
})";

const char *TUNE_STEP = R"({
  "id": "s-tune", "verb": "TUNE", "title": "Raise the timestep",
  "teach": "Push the timestep until the integrator fails.",
  "validate": { "type": "observation", "observation": "etotal", "tolerance": 0.05 }
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

TEST(TutorialContentTest, MinimalDocumentLoadsCleanly)
{
    QList<ContentIssue> issues;
    const auto content = parseTutorialJson(docWithSteps(READ_STEP), &issues);

    EXPECT_EQ(countContentErrors(issues), 0) << qPrintable(formatContentIssues(issues));
    EXPECT_FALSE(content.isEmpty());
    EXPECT_EQ(content.actCount(), 1);
    EXPECT_EQ(content.stepCount(), 1);
}

TEST(TutorialContentTest, MetadataIsParsed)
{
    const auto content = parseTutorialJson(docWithSteps(READ_STEP));

    EXPECT_EQ(content.schemaVersion(), 1);
    EXPECT_EQ(content.id(), QStringLiteral("lj-fluid"));
    EXPECT_EQ(content.title(), QStringLiteral("Tutorial 1"));
    EXPECT_EQ(content.collection(), QStringLiteral("softmatter"));
    EXPECT_EQ(content.tutorialNumber(), 1);
    EXPECT_EQ(content.skeletonFile(), QStringLiteral("in.lj.skeleton"));
    EXPECT_EQ(content.attribution().license, QStringLiteral("CC-BY-4.0"));
    EXPECT_EQ(content.attribution().credit, QStringLiteral("The tutorial authors"));
}

TEST(TutorialContentTest, EveryVerbLoadsWithItsMatchingValidator)
{
    const QList<QByteArray> docs = {
        docWithSteps(READ_STEP), docWithSteps(TYPE_STEP),    docWithSteps(FILL_STEP),
        docWithSteps(FIX_STEP),  docWithSteps(PREDICT_STEP), docWithSteps(TUNE_STEP),
    };
    for (const auto &doc : docs) {
        QList<ContentIssue> issues;
        const auto content = parseTutorialJson(doc, &issues);
        EXPECT_EQ(countContentErrors(issues), 0) << qPrintable(formatContentIssues(issues));
        EXPECT_EQ(content.stepCount(), 1);
    }
}

TEST(TutorialContentTest, StepLookupByIndexAndById)
{
    const QString steps =
        QString::fromLatin1(READ_STEP) + QStringLiteral(",") + QString::fromLatin1(TYPE_STEP);
    const auto content = parseTutorialJson(docWithSteps(steps));
    ASSERT_EQ(content.stepCount(), 2);

    const TutorialStep *first = content.step(0, 0);
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(first->verb, StepVerb::Read);

    const TutorialStep *byid = content.stepById(QStringLiteral("s-type"));
    ASSERT_NE(byid, nullptr);
    EXPECT_EQ(byid->verb, StepVerb::Type);
    EXPECT_EQ(byid->docCommand, QStringLiteral("units"));

    EXPECT_EQ(content.step(0, 99), nullptr);
    EXPECT_EQ(content.step(99, 0), nullptr);
    EXPECT_EQ(content.stepById(QStringLiteral("nope")), nullptr);
}

TEST(TutorialContentTest, DocLinkSplitsCommandAndStyle)
{
    const auto content       = parseTutorialJson(docWithSteps(FILL_STEP));
    const TutorialStep *step = content.stepById(QStringLiteral("s-fill"));
    ASSERT_NE(step, nullptr);
    EXPECT_EQ(step->docCommand, QStringLiteral("pair_style"));
    EXPECT_EQ(step->docStyle, QStringLiteral("lj/cut"));
}

TEST(TutorialContentTest, SkeletonHolesAreCounted)
{
    TutorialEditorAction ed;
    ed.skeleton = QStringLiteral("region box block 0 ___ 0 ___ 0 ___");
    EXPECT_EQ(ed.holeCount(), 3);
    ed.skeleton = QStringLiteral("run 250");
    EXPECT_EQ(ed.holeCount(), 0);
}

// ---- document level rejections -------------------------------------------

TEST(TutorialContentTest, RejectsMalformedJson)
{
    QList<ContentIssue> issues;
    const auto content = parseTutorialJson(QByteArray("{ not json"), &issues);
    EXPECT_TRUE(content.isEmpty());
    EXPECT_GT(countContentErrors(issues), 0);
    EXPECT_TRUE(messageMentions(issues, QStringLiteral("not valid JSON")));
}

TEST(TutorialContentTest, RejectsNonObjectDocument)
{
    QList<ContentIssue> issues;
    parseTutorialJson(QByteArray("[1, 2, 3]"), &issues);
    EXPECT_TRUE(messageMentions(issues, QStringLiteral("must be a JSON object")));
}

TEST(TutorialContentTest, RejectsMissingSchemaVersion)
{
    QList<ContentIssue> issues;
    parseTutorialJson(QByteArray(R"({"id":"x","title":"X","acts":[]})"), &issues);
    EXPECT_TRUE(errorAt(issues, QStringLiteral("schema_version")));
}

TEST(TutorialContentTest, RejectsUnsupportedSchemaVersion)
{
    QList<ContentIssue> issues;
    parseTutorialJson(QByteArray(R"({"schema_version": 99, "id":"x","title":"X","acts":[]})"),
                      &issues);
    EXPECT_TRUE(errorAt(issues, QStringLiteral("schema_version")));
    EXPECT_TRUE(messageMentions(issues, QStringLiteral("unsupported schema version")));
}

TEST(TutorialContentTest, UnknownKeysWarnButStillLoad)
{
    QByteArray doc = docWithSteps(READ_STEP);
    doc.replace("\"id\": \"lj-fluid\"", "\"id\": \"lj-fluid\", \"future_key\": 42");

    QList<ContentIssue> issues;
    const auto content = parseTutorialJson(doc, &issues);
    EXPECT_EQ(countContentErrors(issues), 0) << qPrintable(formatContentIssues(issues));
    EXPECT_TRUE(warningAt(issues, QStringLiteral("future_key")));
    EXPECT_FALSE(content.isEmpty());
}

TEST(TutorialContentTest, MissingLicenseIsWarnedAbout)
{
    QList<ContentIssue> issues;
    parseTutorialJson(QByteArray(R"({
      "schema_version": 1, "id": "x", "title": "X",
      "acts": [ { "id": "a", "title": "A", "steps": [ {
        "id": "s", "verb": "READ", "title": "T", "teach": "t" } ] } ] })"),
                      &issues);
    EXPECT_EQ(countContentErrors(issues), 0);
    EXPECT_TRUE(warningAt(issues, QStringLiteral("attribution.license")));
}

TEST(TutorialContentTest, RejectsEmptyActList)
{
    QList<ContentIssue> issues;
    parseTutorialJson(QByteArray(R"({"schema_version":1,"id":"x","title":"X","acts":[]})"),
                      &issues);
    EXPECT_TRUE(errorAt(issues, QStringLiteral("acts")));
}

TEST(TutorialContentTest, RejectsActWithNoSteps)
{
    QList<ContentIssue> issues;
    parseTutorialJson(QByteArray(R"({"schema_version":1,"id":"x","title":"X",
      "acts":[{"id":"a","title":"A","steps":[]}]})"),
                      &issues);
    EXPECT_TRUE(errorAt(issues, QStringLiteral("steps")));
}

TEST(TutorialContentTest, RejectsDuplicateStepIds)
{
    const QString steps =
        QString::fromLatin1(READ_STEP) + QStringLiteral(",") + QString::fromLatin1(READ_STEP);
    QList<ContentIssue> issues;
    parseTutorialJson(docWithSteps(steps), &issues);
    EXPECT_TRUE(messageMentions(issues, QStringLiteral("duplicate step id")));
}

TEST(TutorialContentTest, RejectsUnknownVerb)
{
    QList<ContentIssue> issues;
    parseTutorialJson(docWithSteps(R"({"id":"s","verb":"DANCE","title":"T","teach":"t"})"),
                      &issues);
    EXPECT_TRUE(errorAt(issues, QStringLiteral("verb")));
    EXPECT_TRUE(messageMentions(issues, QStringLiteral("unknown verb")));
}

// ---- the verb / validator compatibility matrix ---------------------------

TEST(TutorialContentTest, TypeStepRejectsParsesCleanValidator)
{
    // parses_clean would accept *any* syntactically valid command, so it
    // cannot tell whether the user typed the command that was asked for
    QList<ContentIssue> issues;
    parseTutorialJson(docWithSteps(R"({"id":"s","verb":"TYPE","title":"T","teach":"t",
      "validate": {"type":"parses_clean"}})"),
                      &issues);
    EXPECT_TRUE(errorAt(issues, QStringLiteral("validate")));
}

TEST(TutorialContentTest, GatedStepWithoutValidatorIsRejected)
{
    QList<ContentIssue> issues;
    parseTutorialJson(docWithSteps(R"({"id":"s","verb":"TYPE","title":"T","teach":"t"})"), &issues);
    EXPECT_TRUE(messageMentions(issues, QStringLiteral("needs a validator")));
}

TEST(TutorialContentTest, FillStepRequiresOneRulePerHole)
{
    QList<ContentIssue> issues;
    parseTutorialJson(docWithSteps(R"({"id":"s","verb":"FILL","title":"T","teach":"t",
      "editor": {"skeleton":"region box block 0 ___ 0 ___ 0 ___"},
      "validate": {"type":"numeric_range",
                   "rules":[{"type":"numeric_range","min":1,"max":10}]}})"),
                      &issues);
    EXPECT_TRUE(messageMentions(issues, QStringLiteral("one to one")));
}

TEST(TutorialContentTest, FillStepRequiresAHole)
{
    QList<ContentIssue> issues;
    parseTutorialJson(docWithSteps(R"({"id":"s","verb":"FILL","title":"T","teach":"t",
      "editor": {"skeleton":"pair_style lj/cut 2.5"},
      "validate": {"type":"numeric_range",
                   "rules":[{"type":"numeric_range","min":1,"max":10}]}})"),
                      &issues);
    EXPECT_TRUE(errorAt(issues, QStringLiteral("skeleton")));
}

TEST(TutorialContentTest, FixStepRequiresParsesClean)
{
    QList<ContentIssue> issues;
    parseTutorialJson(docWithSteps(R"({"id":"s","verb":"FIX","title":"T","teach":"t",
      "editor": {"skeleton":"pair_coeff 1 1 1.0"},
      "validate": {"type":"exact_tokens","rules":[{"type":"exact","text":"pair_coeff"}]}})"),
                      &issues);
    EXPECT_TRUE(messageMentions(issues, QStringLiteral("parses_clean")));
}

TEST(TutorialContentTest, FixStepRejectsSkeletonHoles)
{
    // holes make it a FILL; a FIX presents a complete but broken command
    QList<ContentIssue> issues;
    parseTutorialJson(docWithSteps(R"({"id":"s","verb":"FIX","title":"T","teach":"t",
      "editor": {"skeleton":"pair_coeff 1 1 ___"},
      "validate": {"type":"parses_clean"}})"),
                      &issues);
    EXPECT_TRUE(messageMentions(issues, QStringLiteral("use FILL for holes")));
}

TEST(TutorialContentTest, PredictStepNeedsTwoOptions)
{
    QList<ContentIssue> issues;
    parseTutorialJson(docWithSteps(R"({"id":"s","verb":"PREDICT","title":"T","teach":"t",
      "options":[{"text":"only one","feedback":"f"}],
      "validate": {"type":"choice","correct_option":0}})"),
                      &issues);
    EXPECT_TRUE(messageMentions(issues, QStringLiteral("at least two options")));
}

TEST(TutorialContentTest, PredictStepRejectsOutOfRangeCorrectOption)
{
    QList<ContentIssue> issues;
    parseTutorialJson(docWithSteps(R"({"id":"s","verb":"PREDICT","title":"T","teach":"t",
      "options":[{"text":"a","feedback":"f"},{"text":"b","feedback":"g"}],
      "validate": {"type":"choice","correct_option":7}})"),
                      &issues);
    EXPECT_TRUE(errorAt(issues, QStringLiteral("correct_option")));
}

TEST(TutorialContentTest, PredictOptionWithoutFeedbackWarns)
{
    QList<ContentIssue> issues;
    parseTutorialJson(docWithSteps(R"({"id":"s","verb":"PREDICT","title":"T","teach":"t",
      "options":[{"text":"a"},{"text":"b","feedback":"g"}],
      "validate": {"type":"choice","correct_option":1}})"),
                      &issues);
    EXPECT_EQ(countContentErrors(issues), 0) << qPrintable(formatContentIssues(issues));
    EXPECT_TRUE(warningAt(issues, QStringLiteral("options[0]")));
}

TEST(TutorialContentTest, TuneStepNeedsObservationAndTolerance)
{
    QList<ContentIssue> issues;
    parseTutorialJson(docWithSteps(R"({"id":"s","verb":"TUNE","title":"T","teach":"t",
      "validate": {"type":"observation"}})"),
                      &issues);
    EXPECT_TRUE(errorAt(issues, QStringLiteral("observation")));
    EXPECT_TRUE(errorAt(issues, QStringLiteral("tolerance")));
}

TEST(TutorialContentTest, ScriptStateOnlyOnCheckpointSteps)
{
    QList<ContentIssue> issues;
    parseTutorialJson(docWithSteps(R"({"id":"s","verb":"READ","title":"T","teach":"t",
      "validate": {"type":"script_state","assertion":"natoms > 0"}})"),
                      &issues);
    EXPECT_TRUE(messageMentions(issues, QStringLiteral("checkpoint step")));

    QList<ContentIssue> ok;
    parseTutorialJson(docWithSteps(R"({"id":"s","verb":"READ","title":"T","teach":"t",
      "checkpoint": true,
      "validate": {"type":"script_state","assertion":"natoms > 0"}})"),
                      &ok);
    EXPECT_EQ(countContentErrors(ok), 0) << qPrintable(formatContentIssues(ok));
}

TEST(TutorialContentTest, ValidatorOnReadStepIsWarnedAndIgnored)
{
    QList<ContentIssue> issues;
    parseTutorialJson(docWithSteps(R"({"id":"s","verb":"READ","title":"T","teach":"t",
      "validate": {"type":"parses_clean"}})"),
                      &issues);
    EXPECT_EQ(countContentErrors(issues), 0) << qPrintable(formatContentIssues(issues));
    EXPECT_TRUE(warningAt(issues, QStringLiteral("validate")));
}

// ---- the anti-coercion rule ----------------------------------------------

TEST(TutorialContentTest, NonSkippableGatedStepMustProvideReveal)
{
    const QByteArray doc = docWithSteps(QString::fromLatin1(TYPE_STEP).replace(
        QStringLiteral("\"id\": \"s-type\""), QStringLiteral("\"id\": \"s-type\", "
                                                             "\"skippable\": false")));
    QList<ContentIssue> issues;
    parseTutorialJson(doc, &issues);
    EXPECT_TRUE(messageMentions(issues, QStringLiteral("no way forward")));
}

TEST(TutorialContentTest, NonSkippableStepWithRevealIsAccepted)
{
    const QByteArray doc = docWithSteps(QString::fromLatin1(TYPE_STEP).replace(
        QStringLiteral("\"id\": \"s-type\""),
        QStringLiteral("\"id\": \"s-type\", \"skippable\": false, "
                       "\"reveal\": \"units lj -- reduced units\"")));
    QList<ContentIssue> issues;
    parseTutorialJson(doc, &issues);
    EXPECT_EQ(countContentErrors(issues), 0) << qPrintable(formatContentIssues(issues));
}

// ---- rule level checks ---------------------------------------------------

TEST(TutorialContentTest, RejectsInvertedNumericRange)
{
    QList<ContentIssue> issues;
    parseTutorialJson(docWithSteps(R"({"id":"s","verb":"FILL","title":"T","teach":"t",
      "editor": {"skeleton":"pair_style lj/cut ___"},
      "validate": {"type":"numeric_range",
                   "rules":[{"type":"numeric_range","min":5.0,"max":2.0}]}})"),
                      &issues);
    EXPECT_TRUE(messageMentions(issues, QStringLiteral("greater than max")));
}

TEST(TutorialContentTest, RejectsIdealOutsideItsOwnRange)
{
    // the round-trip test plays every step with its ideal answer, so this
    // would make the tutorial fail its own regression test
    QList<ContentIssue> issues;
    parseTutorialJson(docWithSteps(R"({"id":"s","verb":"FILL","title":"T","teach":"t",
      "editor": {"skeleton":"pair_style lj/cut ___"},
      "validate": {"type":"numeric_range",
                   "rules":[{"type":"numeric_range","min":2.0,"max":5.0,"ideal":9.0}]}})"),
                      &issues);
    EXPECT_TRUE(errorAt(issues, QStringLiteral("ideal")));
}

TEST(TutorialContentTest, RejectsInvalidRegularExpression)
{
    QList<ContentIssue> issues;
    parseTutorialJson(docWithSteps(R"({"id":"s","verb":"FILL","title":"T","teach":"t",
      "editor": {"skeleton":"fix 1 all ___"},
      "validate": {"type":"token_pattern",
                   "rules":[{"type":"pattern","pattern":"nv[e"}]}})"),
                      &issues);
    EXPECT_TRUE(messageMentions(issues, QStringLiteral("invalid regular expression")));
}

TEST(TutorialContentTest, RejectsEmptyEnumRule)
{
    QList<ContentIssue> issues;
    parseTutorialJson(docWithSteps(R"({"id":"s","verb":"FILL","title":"T","teach":"t",
      "editor": {"skeleton":"pair_style ___ 2.5"},
      "validate": {"type":"token_pattern","rules":[{"type":"enum","enum":[]}]}})"),
                      &issues);
    EXPECT_TRUE(errorAt(issues, QStringLiteral("enum")));
}

TEST(TutorialContentTest, RejectsStyleValidRuleWithoutCategory)
{
    QList<ContentIssue> issues;
    parseTutorialJson(docWithSteps(R"({"id":"s","verb":"FILL","title":"T","teach":"t",
      "editor": {"skeleton":"pair_style ___ 2.5"},
      "validate": {"type":"style_valid","rules":[{"type":"style_valid"}]}})"),
                      &issues);
    EXPECT_TRUE(errorAt(issues, QStringLiteral("category")));
}

TEST(TutorialContentTest, StyleCategoryIsParsedIntoTheSyntaxEngineEnum)
{
    const auto content = parseTutorialJson(docWithSteps(R"({"id":"s","verb":"FILL","title":"T",
      "teach":"t", "editor": {"skeleton":"pair_style ___ 2.5"},
      "validate": {"type":"style_valid",
                   "rules":[{"type":"style_valid","category":"pair"}]}})"));
    const TutorialStep *step = content.stepById(QStringLiteral("s"));
    ASSERT_NE(step, nullptr);
    ASSERT_EQ(step->validate.rules.size(), 1);
    EXPECT_EQ(step->validate.rules.at(0).cat, StyleCat::Pair);
}

TEST(TutorialContentTest, RejectsOutOfRangeFocusPlaceholder)
{
    QList<ContentIssue> issues;
    parseTutorialJson(docWithSteps(R"({"id":"s","verb":"FILL","title":"T","teach":"t",
      "editor": {"skeleton":"pair_style lj/cut ___","focus_placeholder":3},
      "validate": {"type":"numeric_range",
                   "rules":[{"type":"numeric_range","min":2.0,"max":5.0}]}})"),
                      &issues);
    EXPECT_TRUE(errorAt(issues, QStringLiteral("focus_placeholder")));
}

TEST(TutorialContentTest, ReplaceMarkerNeedsAMarker)
{
    QList<ContentIssue> issues;
    parseTutorialJson(docWithSteps(R"({"id":"s","verb":"FIX","title":"T","teach":"t",
      "editor": {"target_line":"replace_marker","skeleton":"pair_coeff 1 1 1.0"},
      "validate": {"type":"parses_clean"}})"),
                      &issues);
    EXPECT_TRUE(errorAt(issues, QStringLiteral("marker")));
}

TEST(TutorialContentTest, WrongTypeForAFieldIsReportedAtItsPath)
{
    QList<ContentIssue> issues;
    parseTutorialJson(docWithSteps(R"({"id":"s","verb":"READ","title":"T","teach":"t",
      "skippable": "yes"})"),
                      &issues);
    EXPECT_TRUE(errorAt(issues, QStringLiteral("skippable")));
    EXPECT_TRUE(messageMentions(issues, QStringLiteral("expected true or false")));
}

// ---- content quality warnings --------------------------------------------

TEST(TutorialContentTest, TwoReadStepsInARowWarn)
{
    const QString steps =
        QString::fromLatin1(READ_STEP) + QStringLiteral(",") +
        QString::fromLatin1(READ_STEP).replace(QStringLiteral("s-read"), QStringLiteral("s-read2"));
    QList<ContentIssue> issues;
    parseTutorialJson(docWithSteps(steps), &issues);
    EXPECT_EQ(countContentErrors(issues), 0) << qPrintable(formatContentIssues(issues));
    EXPECT_TRUE(messageMentions(issues, QStringLiteral("two READ steps in a row")));
}

TEST(TutorialContentTest, MostlyTypeTutorialIsFlaggedAsTranscription)
{
    QStringList steps;
    for (int i = 0; i < 3; ++i)
        steps << QString::fromLatin1(TYPE_STEP).replace(QStringLiteral("s-type"),
                                                        QStringLiteral("s-type%1").arg(i));
    steps << QString::fromLatin1(FILL_STEP);

    QList<ContentIssue> issues;
    parseTutorialJson(docWithSteps(steps.join(QStringLiteral(","))), &issues);
    EXPECT_EQ(countContentErrors(issues), 0) << qPrintable(formatContentIssues(issues));
    EXPECT_TRUE(messageMentions(issues, QStringLiteral("transcription")));
}

TEST(TutorialContentTest, StepWithoutTeachTextWarns)
{
    QList<ContentIssue> issues;
    parseTutorialJson(docWithSteps(R"({"id":"s","verb":"READ","title":"T"})"), &issues);
    EXPECT_EQ(countContentErrors(issues), 0) << qPrintable(formatContentIssues(issues));
    EXPECT_TRUE(warningAt(issues, QStringLiteral("teach")));
}

// ---- feedback ------------------------------------------------------------

TEST(TutorialContentTest, LammpsMessageSentinelLeavesTheGlossEmpty)
{
    const auto content       = parseTutorialJson(docWithSteps(FIX_STEP));
    const TutorialStep *step = content.stepById(QStringLiteral("s-fix"));
    ASSERT_NE(step, nullptr);
    EXPECT_TRUE(step->feedback.parseError.isEmpty());
    EXPECT_TRUE(step->feedback.useLammpsMessage);
}

TEST(TutorialContentTest, ParseErrorGlossIsKeptAlongsideTheRealMessage)
{
    const auto content = parseTutorialJson(docWithSteps(R"({"id":"s","verb":"FIX","title":"T",
      "teach":"t", "editor": {"skeleton":"pair_coeff 1 1 1.0"},
      "validate": {"type":"parses_clean"},
      "feedback": {"parse_error":"pair_coeff needs epsilon and sigma."}})"));
    const TutorialStep *step = content.stepById(QStringLiteral("s"));
    ASSERT_NE(step, nullptr);
    EXPECT_EQ(step->feedback.parseError, QStringLiteral("pair_coeff needs epsilon and sigma."));
    EXPECT_TRUE(step->feedback.useLammpsMessage);
}

// ---- issue reporting -----------------------------------------------------

TEST(TutorialContentTest, ErroneousContentIsNotHandedOut)
{
    // a file with any error must never reach a user half-parsed
    QList<ContentIssue> issues;
    const auto content = parseTutorialJson(
        docWithSteps(R"({"id":"s","verb":"TYPE","title":"T","teach":"t"})"), &issues);
    EXPECT_GT(countContentErrors(issues), 0);
    EXPECT_TRUE(content.isEmpty());
}

TEST(TutorialContentTest, FormatIssuesTruncatesAndCounts)
{
    const QList<ContentIssue> issues = {
        {QStringLiteral("a"), ContentSeverity::Error, QStringLiteral("first")},
        {QStringLiteral("b"), ContentSeverity::Warning, QStringLiteral("second")},
        {QStringLiteral("c"), ContentSeverity::Error, QStringLiteral("third")},
    };
    EXPECT_EQ(countContentErrors(issues), 2);

    const QString all = formatContentIssues(issues);
    EXPECT_TRUE(all.contains(QStringLiteral("ERROR: a: first")));
    EXPECT_TRUE(all.contains(QStringLiteral("WARNING: b: second")));

    const QString cut = formatContentIssues(issues, 1);
    EXPECT_TRUE(cut.contains(QStringLiteral("first")));
    EXPECT_FALSE(cut.contains(QStringLiteral("third")));
    EXPECT_TRUE(cut.contains(QStringLiteral("... and 2 more")));
}

TEST(TutorialContentTest, MissingFileIsReportedNotCrashed)
{
    QList<ContentIssue> issues;
    const auto content = loadTutorialFile(QStringLiteral("/nonexistent/tutorial.json"), &issues);
    EXPECT_TRUE(content.isEmpty());
    EXPECT_GT(countContentErrors(issues), 0);
    EXPECT_TRUE(messageMentions(issues, QStringLiteral("cannot read")));
}

TEST(TutorialContentTest, ParsingWithoutAnIssueSinkDoesNotCrash)
{
    const auto content = parseTutorialJson(QByteArray("{ broken"), nullptr);
    EXPECT_TRUE(content.isEmpty());
}

// ---- the shipped content ------------------------------------------------
// Content rot is the failure mode these guard against: a schema change that
// invalidates an authored tutorial has to fail here, not in front of a user.

#ifdef TUTORIAL_CONTENT_DIR
TEST(TutorialContentTest, ShippedTutorialOneLoadsWithoutIssues)
{
    const QString path = QStringLiteral(TUTORIAL_CONTENT_DIR "/lj-fluid.json");
    QList<ContentIssue> issues;
    const auto content = loadTutorialFile(path, &issues);

    EXPECT_EQ(countContentErrors(issues), 0) << qPrintable(formatContentIssues(issues));
    // warnings are advisory, but shipped content should be clean of them too
    EXPECT_TRUE(issues.isEmpty()) << qPrintable(formatContentIssues(issues));
    EXPECT_FALSE(content.isEmpty());
    EXPECT_EQ(content.id(), QStringLiteral("lj-fluid"));
    EXPECT_GT(content.stepCount(), 20);
}

TEST(TutorialContentTest, ShippedTutorialOneCoversTheControlledFailure)
{
    // Act 6 is the reason this feature exists: the user causes and repairs the
    // most common failure in MD.  If it ever disappears from the content, the
    // tutorial has lost its point.
    const QString path = QStringLiteral(TUTORIAL_CONTENT_DIR "/lj-fluid.json");
    const auto content = loadTutorialFile(path);

    const TutorialStep *breakit = content.stepById(QStringLiteral("a6-s2"));
    ASSERT_NE(breakit, nullptr);
    EXPECT_EQ(breakit->verb, StepVerb::Tune);

    const TutorialStep *repair = content.stepById(QStringLiteral("a6-s4"));
    ASSERT_NE(repair, nullptr);
    EXPECT_TRUE(repair->checkpoint);
}

TEST(TutorialContentTest, ShippedTutorialOneIsNotTranscription)
{
    // every gated step must be answerable without the answer being on screen,
    // and the tutorial must not have drifted into mostly-TYPE
    const QString path = QStringLiteral(TUTORIAL_CONTENT_DIR "/lj-fluid.json");
    const auto content = loadTutorialFile(path);

    int gated = 0;
    int typed = 0;
    for (int a = 0; a < content.actCount(); ++a) {
        for (int s = 0;; ++s) {
            const TutorialStep *step = content.step(a, s);
            if (!step) break;
            if (step->verb == StepVerb::Read || step->verb == StepVerb::Inspect) continue;
            ++gated;
            if (step->verb == StepVerb::Type) ++typed;
            // a gated step the user can get stuck on must offer a way out
            EXPECT_TRUE(step->skippable || !step->reveal.isEmpty())
                << qPrintable(step->id) << " has no way forward";
        }
    }
    EXPECT_GT(gated, 10);
    EXPECT_LE(typed * 2, gated) << "the tutorial has drifted into transcription";
}
#endif

// ---- enum naming ---------------------------------------------------------

TEST(TutorialContentTest, EnumNamesMatchTheContentFileSpelling)
{
    EXPECT_EQ(stepVerbName(StepVerb::Predict), QStringLiteral("PREDICT"));
    EXPECT_EQ(stepVerbName(StepVerb::Fill), QStringLiteral("FILL"));
    EXPECT_EQ(validatorTypeName(ValidatorType::ParsesClean), QStringLiteral("parses_clean"));
    EXPECT_EQ(validatorTypeName(ValidatorType::ExactTokens), QStringLiteral("exact_tokens"));
    EXPECT_EQ(ruleTypeName(RuleType::Enumerated), QStringLiteral("enum"));
    EXPECT_EQ(ruleTypeName(RuleType::NumericRange), QStringLiteral("numeric_range"));
}

TEST(TutorialContentTest, UnknownNamesListTheAcceptedSpellings)
{
    QList<ContentIssue> issues;
    parseTutorialJson(docWithSteps(R"({"id":"s","verb":"TYPE","title":"T","teach":"t",
      "validate": {"type":"vibes","rules":[{"type":"exact","text":"units"}]}})"),
                      &issues);
    EXPECT_TRUE(messageMentions(issues, QStringLiteral("exact_tokens")));
    EXPECT_TRUE(messageMentions(issues, QStringLiteral("parses_clean")));
}

// Local Variables:
// c-basic-offset: 4
// End:
