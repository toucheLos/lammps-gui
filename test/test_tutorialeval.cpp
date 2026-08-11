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

// ---- rewriting one argument in place -------------------------------------

TEST(TutorialEvalTest, RewriteArgumentReplacesOnlyThatArgument)
{
    EXPECT_EQ(rewriteArgument(QStringLiteral("timestep 0.005"), 1, QStringLiteral("0.05")),
              QStringLiteral("timestep 0.05"));
    EXPECT_EQ(rewriteArgument(QStringLiteral("pair_coeff 2 2 0.5 3.0"), 3, QStringLiteral("2.0")),
              QStringLiteral("pair_coeff 2 2 2.0 3.0"));
}

TEST(TutorialEvalTest, RewriteArgumentKeepsSpacingAndComments)
{
    // the author's alignment and any trailing comment must survive, or the
    // tutorial reformats the script underneath the user
    EXPECT_EQ(
        rewriteArgument(QStringLiteral("timestep   0.005   # stable"), 1, QStringLiteral("0.02")),
        QStringLiteral("timestep   0.02   # stable"));
}

TEST(TutorialEvalTest, RewriteArgumentLeavesTheLineAloneWhenTheArgumentIsMissing)
{
    const QString line = QStringLiteral("run 25000");
    EXPECT_EQ(rewriteArgument(line, 7, QStringLiteral("x")), line);
    EXPECT_EQ(rewriteArgument(line, 0, QStringLiteral("x")), line);
}

// ---- locating a command in a buffer --------------------------------------

TEST(TutorialEvalTest, FindCommandLineReturnsTheZeroBasedLine)
{
    const QString buffer = QStringLiteral("units lj\nboundary p p p\ntimestep 0.005\n");
    int line             = -1;
    ASSERT_TRUE(findCommandLine(buffer, QStringLiteral("timestep"), line));
    EXPECT_EQ(line, 2);
}

TEST(TutorialEvalTest, FindCommandLineIgnoresCommentedOccurrences)
{
    // a commented-out line is not a command, so it must not be rewritten
    const QString buffer = QStringLiteral("# timestep 0.001\ntimestep 0.005\n");
    int line             = -1;
    ASSERT_TRUE(findCommandLine(buffer, QStringLiteral("timestep"), line));
    EXPECT_EQ(line, 1);
}

TEST(TutorialEvalTest, FindCommandLineReportsAbsence)
{
    int line = -1;
    EXPECT_FALSE(findCommandLine(QStringLiteral("units lj\n"), QStringLiteral("timestep"), line));
    EXPECT_FALSE(findCommandLine(QStringLiteral("units lj\n"), QString(), line));
}

// Local Variables:
// c-basic-offset: 4
// End:
