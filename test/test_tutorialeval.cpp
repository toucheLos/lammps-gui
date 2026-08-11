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

#include "tutorialeval.h"

#include "lammpssyntax.h"

#include <gtest/gtest.h>

#include <QString>
#include <QStringList>

namespace {

TutorialRule exactRule(const QString &text, bool caseSensitive = false)
{
    TutorialRule r;
    r.type          = RuleType::Exact;
    r.text          = text;
    r.caseSensitive = caseSensitive;
    return r;
}

TutorialRule rangeRule(double lo, double hi, const QString &label = QString())
{
    TutorialRule r;
    r.type  = RuleType::NumericRange;
    r.min   = lo;
    r.max   = hi;
    r.label = label;
    return r;
}

TutorialRule valueRule(double ideal, double tol = 1.0e-9)
{
    TutorialRule r;
    r.type      = RuleType::NumericValue;
    r.ideal     = ideal;
    r.tolerance = tol;
    return r;
}

TutorialRule enumRule(const QStringList &choices)
{
    TutorialRule r;
    r.type    = RuleType::Enumerated;
    r.choices = choices;
    return r;
}

TutorialRule patternRule(const QString &pattern)
{
    TutorialRule r;
    r.type    = RuleType::Pattern;
    r.pattern = pattern;
    return r;
}

TutorialRule styleRule(StyleCat cat)
{
    TutorialRule r;
    r.type = RuleType::StyleValid;
    r.cat  = cat;
    return r;
}

/// a TYPE-shaped step whose validator carries the given rules
TutorialStep lineStep(const QList<TutorialRule> &rules)
{
    TutorialStep s;
    s.verb           = StepVerb::Type;
    s.validate.type  = ValidatorType::ExactTokens;
    s.validate.rules = rules;
    return s;
}

Verdict verdictOfLine(const QList<TutorialRule> &rules, const QString &line)
{
    const TutorialEvaluator ev;
    return ev.evaluateLine(lineStep(rules), line).verdict;
}

} // namespace

// ---- canonicalization (section 4.4) --------------------------------------

TEST(TutorialEvalTest, CanonicalWordsSplitsOnWhitespace)
{
    EXPECT_EQ(canonicalWords(QStringLiteral("units lj")), (QStringList{"units", "lj"}));
}

TEST(TutorialEvalTest, CanonicalWordsCollapsesRepeatedWhitespaceAndTabs)
{
    EXPECT_EQ(canonicalWords(QStringLiteral("  units \t\t  lj   ")), (QStringList{"units", "lj"}));
}

TEST(TutorialEvalTest, CanonicalWordsStripsTrailingComments)
{
    EXPECT_EQ(canonicalWords(QStringLiteral("units lj  # reduced units")),
              (QStringList{"units", "lj"}));
}

TEST(TutorialEvalTest, CanonicalWordsJoinsLineContinuations)
{
    EXPECT_EQ(canonicalWords(QStringLiteral("pair_style lj/cut &\n  2.5")),
              (QStringList{"pair_style", "lj/cut", "2.5"}));
}

TEST(TutorialEvalTest, CanonicalWordsStripsSurroundingQuotes)
{
    EXPECT_EQ(canonicalWords(QStringLiteral("print \"hello world\"")),
              (QStringList{"print", "hello world"}));
}

TEST(TutorialEvalTest, CanonicalWordsOnEmptyAndCommentOnlyInput)
{
    EXPECT_TRUE(canonicalWords(QString()).isEmpty());
    EXPECT_TRUE(canonicalWords(QStringLiteral("   ")).isEmpty());
    EXPECT_TRUE(canonicalWords(QStringLiteral("# just a comment")).isEmpty());
}

// ---- LAMMPS number parsing -----------------------------------------------

TEST(TutorialEvalTest, EquivalentNumberSpellingsCompareEqual)
{
    // the case section 4.4 calls out explicitly
    const QList<QString> spellings = {"2.5", "2.50", "2.5e0", "2.5E0", "2.5d0", "0.25e1"};
    for (const auto &s : spellings) {
        double v = 0.0;
        EXPECT_TRUE(parseLammpsNumber(s, v)) << qPrintable(s);
        EXPECT_DOUBLE_EQ(v, 2.5) << qPrintable(s);
    }
}

TEST(TutorialEvalTest, NegativeAndExponentForms)
{
    double v = 0.0;
    EXPECT_TRUE(parseLammpsNumber(QStringLiteral("-20"), v));
    EXPECT_DOUBLE_EQ(v, -20.0);
    EXPECT_TRUE(parseLammpsNumber(QStringLiteral("1.0e-6"), v));
    EXPECT_DOUBLE_EQ(v, 1.0e-6);
    EXPECT_TRUE(parseLammpsNumber(QStringLiteral("1.0d-3"), v));
    EXPECT_DOUBLE_EQ(v, 1.0e-3);
}

TEST(TutorialEvalTest, NonNumbersAreRejected)
{
    double v = 0.0;
    EXPECT_FALSE(parseLammpsNumber(QStringLiteral("lj/cut"), v));
    EXPECT_FALSE(parseLammpsNumber(QStringLiteral("simbox"), v));
    EXPECT_FALSE(parseLammpsNumber(QString(), v));
}

TEST(TutorialEvalTest, SubstitutionDetection)
{
    EXPECT_TRUE(hasSubstitution(QStringLiteral("${cut}")));
    EXPECT_TRUE(hasSubstitution(QStringLiteral("$x")));
    EXPECT_TRUE(hasSubstitution(QStringLiteral("$(2*3)")));
    EXPECT_FALSE(hasSubstitution(QStringLiteral("2.5")));
    // an escaped dollar is a literal, per LAMMPS' own substitution rules
    EXPECT_FALSE(hasSubstitution(QStringLiteral("\\$notavar")));
}

// ---- Exact rules ---------------------------------------------------------

TEST(TutorialEvalTest, ExactRuleIsCaseInsensitiveByDefault)
{
    // command and style names are case-insensitive in LAMMPS
    const TutorialEvaluator ev;
    EXPECT_EQ(ev.applyRule(exactRule("lj"), QStringLiteral("lj"), 0).verdict, Verdict::Correct);
    EXPECT_EQ(ev.applyRule(exactRule("lj"), QStringLiteral("LJ"), 0).verdict, Verdict::Correct);
    EXPECT_EQ(ev.applyRule(exactRule("lj"), QStringLiteral("Lj"), 0).verdict, Verdict::Correct);
}

TEST(TutorialEvalTest, ExactRuleCanDemandCaseWhenItMatters)
{
    // file names and variable names are case-sensitive
    const TutorialEvaluator ev;
    const auto rule = exactRule(QStringLiteral("Data.lmp"), true);
    EXPECT_EQ(ev.applyRule(rule, QStringLiteral("Data.lmp"), 0).verdict, Verdict::Correct);
    EXPECT_EQ(ev.applyRule(rule, QStringLiteral("data.lmp"), 0).verdict, Verdict::Incorrect);
}

TEST(TutorialEvalTest, ExactRejectionNamesTheArgumentAndBothValues)
{
    const TutorialEvaluator ev;
    auto rule      = exactRule(QStringLiteral("lj"));
    rule.label     = QStringLiteral("the unit system");
    const auto res = ev.applyRule(rule, QStringLiteral("real"), 1);
    EXPECT_EQ(res.verdict, Verdict::Incorrect);
    EXPECT_TRUE(res.message.contains(QStringLiteral("the unit system")));
    EXPECT_TRUE(res.message.contains(QStringLiteral("\"lj\"")));
    EXPECT_TRUE(res.message.contains(QStringLiteral("\"real\"")));
}

TEST(TutorialEvalTest, UnlabelledPositionsFallBackToArgumentNumber)
{
    const TutorialEvaluator ev;
    const auto res = ev.applyRule(exactRule(QStringLiteral("all")), QStringLiteral("none"), 2);
    EXPECT_TRUE(res.message.contains(QStringLiteral("argument 3")));
}

// ---- numeric rules -------------------------------------------------------

TEST(TutorialEvalTest, NumericRangeAcceptsEquivalentSpellings)
{
    const auto rules = QList<TutorialRule>{rangeRule(2.0, 5.0)};
    const TutorialEvaluator ev;
    TutorialStep step;
    step.validate.type  = ValidatorType::NumericRange;
    step.validate.rules = rules;
    for (const auto &s : {"2.5", "2.50", "2.5e0", "2.5d0"})
        EXPECT_EQ(ev.evaluateHoles(step, QStringList{QString::fromLatin1(s)}).verdict,
                  Verdict::Correct)
            << s;
}

TEST(TutorialEvalTest, NumericRangeDistinguishesBelowFromAbove)
{
    const TutorialEvaluator ev;
    const auto rule = rangeRule(2.0, 5.0, QStringLiteral("the cutoff"));

    const auto low = ev.applyRule(rule, QStringLiteral("1.0"), 0);
    EXPECT_EQ(low.verdict, Verdict::Incorrect);
    EXPECT_TRUE(low.message.contains(QStringLiteral("below")));
    EXPECT_TRUE(low.message.contains(QStringLiteral("the cutoff")));

    const auto high = ev.applyRule(rule, QStringLiteral("9.0"), 0);
    EXPECT_EQ(high.verdict, Verdict::Incorrect);
    EXPECT_TRUE(high.message.contains(QStringLiteral("above")));
}

TEST(TutorialEvalTest, NumericRangeBoundsAreInclusive)
{
    const TutorialEvaluator ev;
    const auto rule = rangeRule(2.0, 5.0);
    EXPECT_EQ(ev.applyRule(rule, QStringLiteral("2.0"), 0).verdict, Verdict::Correct);
    EXPECT_EQ(ev.applyRule(rule, QStringLiteral("5.0"), 0).verdict, Verdict::Correct);
}

TEST(TutorialEvalTest, NumericRuleRejectsAWordWithAUsefulMessage)
{
    const TutorialEvaluator ev;
    auto rule      = rangeRule(2.0, 5.0);
    rule.label     = QStringLiteral("the cutoff distance");
    const auto res = ev.applyRule(rule, QStringLiteral("lj/cut"), 2);
    EXPECT_EQ(res.verdict, Verdict::Incorrect);
    EXPECT_TRUE(res.message.contains(QStringLiteral("the cutoff distance")));
    EXPECT_TRUE(res.message.contains(QStringLiteral("expects a number")));
}

TEST(TutorialEvalTest, NumericValueHonoursItsTolerance)
{
    const TutorialEvaluator ev;
    const auto rule = valueRule(0.3, 0.0001);
    EXPECT_EQ(ev.applyRule(rule, QStringLiteral("0.3"), 0).verdict, Verdict::Correct);
    EXPECT_EQ(ev.applyRule(rule, QStringLiteral("0.30000"), 0).verdict, Verdict::Correct);
    EXPECT_EQ(ev.applyRule(rule, QStringLiteral("0.4"), 0).verdict, Verdict::Incorrect);
}

// ---- enum, pattern, style ------------------------------------------------

TEST(TutorialEvalTest, EnumRuleListsTheAllowedWordsOnRejection)
{
    const TutorialEvaluator ev;
    const auto rule = enumRule({QStringLiteral("p"), QStringLiteral("f")});
    EXPECT_EQ(ev.applyRule(rule, QStringLiteral("p"), 0).verdict, Verdict::Correct);
    EXPECT_EQ(ev.applyRule(rule, QStringLiteral("P"), 0).verdict, Verdict::Correct);

    const auto res = ev.applyRule(rule, QStringLiteral("x"), 0);
    EXPECT_EQ(res.verdict, Verdict::Incorrect);
    EXPECT_TRUE(res.message.contains(QStringLiteral("p, f")));
}

TEST(TutorialEvalTest, PatternRuleIsAnchored)
{
    const TutorialEvaluator ev;
    const auto rule = patternRule(QStringLiteral("nve"));
    EXPECT_EQ(ev.applyRule(rule, QStringLiteral("nve"), 0).verdict, Verdict::Correct);
    // anchored: a substring match must not pass
    EXPECT_EQ(ev.applyRule(rule, QStringLiteral("nvenve"), 0).verdict, Verdict::Incorrect);
}

TEST(TutorialEvalTest, StyleRuleIsUnresolvedWithoutARegistry)
{
    // the style set depends on which packages LAMMPS was built with, so
    // guessing would be worse than admitting we do not know
    const TutorialEvaluator ev(nullptr);
    const auto res = ev.applyRule(styleRule(StyleCat::Pair), QStringLiteral("lj/cut"), 0);
    EXPECT_EQ(res.verdict, Verdict::Unresolved);
}

TEST(TutorialEvalTest, StyleRuleUsesThePopulatedRegistry)
{
    LammpsSyntax syntax;
    syntax.setCommands({QStringLiteral("pair_style")});
    syntax.setStyles(StyleCat::Pair, {QStringLiteral("lj/cut"), QStringLiteral("eam")});

    const TutorialEvaluator ev(&syntax);
    EXPECT_EQ(ev.applyRule(styleRule(StyleCat::Pair), QStringLiteral("lj/cut"), 0).verdict,
              Verdict::Correct);

    const auto res = ev.applyRule(styleRule(StyleCat::Pair), QStringLiteral("nosuchstyle"), 0);
    EXPECT_EQ(res.verdict, Verdict::Incorrect);
    EXPECT_TRUE(res.message.contains(QStringLiteral("pair style")));
}

// ---- variable references are undecidable, not wrong ----------------------

TEST(TutorialEvalTest, VariableReferenceIsUnresolvedRatherThanWrong)
{
    const TutorialEvaluator ev;
    const auto res = ev.applyRule(rangeRule(2.0, 5.0), QStringLiteral("${cut}"), 0);
    EXPECT_EQ(res.verdict, Verdict::Unresolved);
    EXPECT_TRUE(res.message.contains(QStringLiteral("variable reference")));
}

TEST(TutorialEvalTest, OneUnresolvedWordMakesTheWholeLineUnresolved)
{
    EXPECT_EQ(verdictOfLine({exactRule("pair_style"), exactRule("lj/cut"), rangeRule(2.0, 5.0)},
                            QStringLiteral("pair_style lj/cut ${cut}")),
              Verdict::Unresolved);
}

TEST(TutorialEvalTest, AWrongWordBeatsAnUnresolvedOne)
{
    // a definite error is more useful to report than an undecidable one
    EXPECT_EQ(verdictOfLine({exactRule("pair_style"), exactRule("lj/cut"), rangeRule(2.0, 5.0)},
                            QStringLiteral("pair_coeff lj/cut ${cut}")),
              Verdict::Incorrect);
}

// ---- whole-line evaluation -----------------------------------------------

TEST(TutorialEvalTest, LineIsAcceptedRegardlessOfSpacingCaseAndComments)
{
    const auto rules = QList<TutorialRule>{exactRule("units"), exactRule("lj")};
    EXPECT_EQ(verdictOfLine(rules, QStringLiteral("units lj")), Verdict::Correct);
    EXPECT_EQ(verdictOfLine(rules, QStringLiteral("  UNITS   LJ  ")), Verdict::Correct);
    EXPECT_EQ(verdictOfLine(rules, QStringLiteral("units lj # reduced")), Verdict::Correct);
    EXPECT_EQ(verdictOfLine(rules, QStringLiteral("units\tlj")), Verdict::Correct);
}

TEST(TutorialEvalTest, WrongWordCountIsReportedWithBothCounts)
{
    const TutorialEvaluator ev;
    const auto step = lineStep({exactRule("units"), exactRule("lj")});
    const auto res  = ev.evaluateLine(step, QStringLiteral("units lj extra"));
    EXPECT_EQ(res.verdict, Verdict::Incorrect);
    EXPECT_TRUE(res.message.contains(QStringLiteral("2")));
    EXPECT_TRUE(res.message.contains(QStringLiteral("3")));
}

TEST(TutorialEvalTest, EmptyLineIsRejectedNotCrashed)
{
    const TutorialEvaluator ev;
    const auto step = lineStep({exactRule("units"), exactRule("lj")});
    EXPECT_EQ(ev.evaluateLine(step, QString()).verdict, Verdict::Incorrect);
    EXPECT_EQ(ev.evaluateLine(step, QStringLiteral("# nothing")).verdict, Verdict::Incorrect);
}

TEST(TutorialEvalTest, ParsesCleanIsDeferredToTheRealParser)
{
    // the FIX verb's gate cannot be decided syntactically, and must say so
    TutorialStep step;
    step.verb          = StepVerb::Fix;
    step.validate.type = ValidatorType::ParsesClean;

    const TutorialEvaluator ev;
    const auto res = ev.evaluateLine(step, QStringLiteral("pair_coeff 1 1 1.0"));
    EXPECT_EQ(res.verdict, Verdict::Unresolved);
    EXPECT_TRUE(res.needsParse);
}

TEST(TutorialEvalTest, AlsoRequireParseIsPropagated)
{
    auto step                      = lineStep({exactRule("units"), exactRule("lj")});
    step.validate.alsoRequireParse = true;
    const TutorialEvaluator ev;
    const auto res = ev.evaluateLine(step, QStringLiteral("units lj"));
    EXPECT_EQ(res.verdict, Verdict::Correct);
    EXPECT_TRUE(res.needsParse);
}

// ---- hole evaluation -----------------------------------------------------

TEST(TutorialEvalTest, HolesAreJudgedPositionallyAndReportTheFailingOne)
{
    TutorialStep step;
    step.verb           = StepVerb::Fill;
    step.validate.type  = ValidatorType::NumericValue;
    step.validate.rules = {valueRule(-20.0), valueRule(20.0), valueRule(-20.0)};

    const TutorialEvaluator ev;
    EXPECT_EQ(ev.evaluateHoles(step, {"-20", "20", "-20"}).verdict, Verdict::Correct);

    const auto res = ev.evaluateHoles(step, {"-20", "20", "99"});
    EXPECT_EQ(res.verdict, Verdict::Incorrect);
    EXPECT_EQ(res.position, 2);
}

TEST(TutorialEvalTest, HoleCountMismatchIsReported)
{
    TutorialStep step;
    step.verb           = StepVerb::Fill;
    step.validate.rules = {valueRule(1.0), valueRule(2.0)};
    const TutorialEvaluator ev;
    EXPECT_EQ(ev.evaluateHoles(step, {"1.0"}).verdict, Verdict::Incorrect);
}

TEST(TutorialEvalTest, EmptyHoleIsReportedAsEmptyNotWrong)
{
    TutorialStep step;
    step.verb           = StepVerb::Fill;
    step.validate.rules = {rangeRule(2.0, 5.0)};
    const TutorialEvaluator ev;
    const auto res = ev.evaluateHoles(step, {QString()});
    EXPECT_EQ(res.verdict, Verdict::Incorrect);
    EXPECT_TRUE(res.message.contains(QStringLiteral("empty")));
}

// ---- choice evaluation ---------------------------------------------------

TEST(TutorialEvalTest, BothBranchesOfAPredictionTeach)
{
    TutorialStep step;
    step.verb                   = StepVerb::Predict;
    step.validate.type          = ValidatorType::Choice;
    step.validate.correctOption = 1;
    step.options = {{QStringLiteral("wrong answer"), QStringLiteral("why it is wrong")},
                    {QStringLiteral("right answer"), QStringLiteral("why it is right")}};

    const TutorialEvaluator ev;
    const auto right = ev.evaluateChoice(step, 1);
    EXPECT_EQ(right.verdict, Verdict::Correct);
    EXPECT_EQ(right.feedback, QStringLiteral("why it is right"));

    // a wrong prediction still returns its own explanation
    const auto wrong = ev.evaluateChoice(step, 0);
    EXPECT_EQ(wrong.verdict, Verdict::Incorrect);
    EXPECT_EQ(wrong.feedback, QStringLiteral("why it is wrong"));
}

TEST(TutorialEvalTest, OutOfRangeChoiceIsRejected)
{
    TutorialStep step;
    step.verb                   = StepVerb::Predict;
    step.validate.correctOption = 0;
    step.options                = {{QStringLiteral("a"), QString()}};
    const TutorialEvaluator ev;
    EXPECT_EQ(ev.evaluateChoice(step, 7).verdict, Verdict::Incorrect);
    EXPECT_EQ(ev.evaluateChoice(step, -1).verdict, Verdict::Incorrect);
}

// Local Variables:
// c-basic-offset: 4
// End:
