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

#include <QRegularExpression>

#include <cmath>

namespace {

/// human readable name of a style category, for a rejection message
QString categoryName(StyleCat cat)
{
    switch (cat) {
        case StyleCat::Command:
            return QStringLiteral("command");
        case StyleCat::Fix:
            return QStringLiteral("fix");
        case StyleCat::Compute:
            return QStringLiteral("compute");
        case StyleCat::Dump:
            return QStringLiteral("dump");
        case StyleCat::Atom:
            return QStringLiteral("atom");
        case StyleCat::Pair:
            return QStringLiteral("pair");
        case StyleCat::Bond:
            return QStringLiteral("bond");
        case StyleCat::Angle:
            return QStringLiteral("angle");
        case StyleCat::Dihedral:
            return QStringLiteral("dihedral");
        case StyleCat::Improper:
            return QStringLiteral("improper");
        case StyleCat::Kspace:
            return QStringLiteral("kspace");
        case StyleCat::Region:
            return QStringLiteral("region");
        case StyleCat::Integrate:
            return QStringLiteral("integrator");
        case StyleCat::Minimize:
            return QStringLiteral("minimizer");
        case StyleCat::Variable:
            return QStringLiteral("variable");
        case StyleCat::Units:
            return QStringLiteral("units");
        default:
            return QStringLiteral("style");
    }
}

/// format a double the way an author would have written it
QString num(double v)
{
    return QString::number(v, 'g', 10);
}

} // namespace

/* -------------------------------------------------------------------- */

QStringList canonicalWords(const QString &line)
{
    // the syntax engine already implements LAMMPS' own lexing: comments,
    // single/double/triple quotes, and "&" continuations including mid-word
    // joins.  Re-deriving any of that here would only be a second, worse copy.
    InputScanner scanner;
    scanner.scan(line);
    if (scanner.commands().isEmpty()) return {};

    QStringList out;
    for (const auto &word : scanner.commands().first().words)
        out << word.text;
    return out;
}

bool parseLammpsNumber(const QString &word, double &value)
{
    if (word.isEmpty()) return false;

    bool ok        = false;
    const double v = word.toDouble(&ok);
    if (ok) {
        value = v;
        return true;
    }

    // LAMMPS also accepts the Fortran style "d" exponent, as in 1.0d-3, which
    // QString::toDouble() does not know about
    if (word.contains(QLatin1Char('d')) || word.contains(QLatin1Char('D'))) {
        QString fixed = word;
        fixed.replace(QLatin1Char('d'), QLatin1Char('e'));
        fixed.replace(QLatin1Char('D'), QLatin1Char('e'));
        const double w = fixed.toDouble(&ok);
        if (ok) {
            value = w;
            return true;
        }
    }
    return false;
}

bool hasSubstitution(const QString &word)
{
    // a backslash escapes the dollar, matching LAMMPS' own substitution rules
    for (int i = 0; i < word.size(); ++i) {
        if (word.at(i) != QLatin1Char('$')) continue;
        if (i > 0 && word.at(i - 1) == QLatin1Char('\\')) continue;
        return true;
    }
    return false;
}

/* -------------------------------------------------------------------- */

QString TutorialEvaluator::positionName(const TutorialRule &rule, int position)
{
    if (!rule.label.isEmpty()) return rule.label;
    return QStringLiteral("argument %1").arg(position + 1);
}

StepResult TutorialEvaluator::applyRule(const TutorialRule &rule, const QString &word,
                                        int position) const
{
    StepResult res;
    res.position       = position;
    const QString what = positionName(rule, position);

    if (word.trimmed().isEmpty()) {
        res.verdict = Verdict::Incorrect;
        res.message = QStringLiteral("%1 is still empty.").arg(what);
        return res;
    }

    // a value that LAMMPS has yet to expand cannot be judged here; saying
    // "wrong" would be a lie, so the caller is told it is undecidable
    if (hasSubstitution(word)) {
        res.verdict = Verdict::Unresolved;
        res.message = QStringLiteral("%1 contains a variable reference (%2), so it cannot be "
                                     "checked until LAMMPS expands it.")
                          .arg(what, word);
        return res;
    }

    const auto sensitivity = rule.caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;

    switch (rule.type) {
        case RuleType::Any:
            res.verdict = Verdict::Correct;
            break;

        case RuleType::Exact:
            if (QString::compare(word, rule.text, sensitivity) == 0) {
                res.verdict = Verdict::Correct;
            } else {
                res.verdict = Verdict::Incorrect;
                res.message =
                    QStringLiteral("%1 should be \"%2\", not \"%3\".").arg(what, rule.text, word);
            }
            break;

        case RuleType::Enumerated: {
            bool found = false;
            for (const auto &choice : rule.choices)
                if (QString::compare(word, choice, sensitivity) == 0) {
                    found = true;
                    break;
                }
            if (found) {
                res.verdict = Verdict::Correct;
            } else {
                res.verdict = Verdict::Incorrect;
                res.message = QStringLiteral("%1 must be one of: %2. \"%3\" is not.")
                                  .arg(what, rule.choices.join(QStringLiteral(", ")), word);
            }
            break;
        }

        case RuleType::Pattern: {
            QRegularExpression re(QRegularExpression::anchoredPattern(rule.pattern));
            if (!rule.caseSensitive)
                re.setPatternOptions(re.patternOptions() |
                                     QRegularExpression::CaseInsensitiveOption);
            if (re.isValid() && re.match(word).hasMatch()) {
                res.verdict = Verdict::Correct;
            } else {
                res.verdict = Verdict::Incorrect;
                res.message = QStringLiteral("%1 does not have the expected form. You gave \"%2\".")
                                  .arg(what, word);
            }
            break;
        }

        case RuleType::NumericRange: {
            double v = 0.0;
            if (!parseLammpsNumber(word, v)) {
                res.verdict = Verdict::Incorrect;
                res.message =
                    QStringLiteral("%1 expects a number; \"%2\" is not one.").arg(what, word);
                break;
            }
            // tolerance widens the accepted band rather than narrowing it, so an
            // author can accept a value that is a hair outside a round bound
            if (v < rule.min - rule.tolerance) {
                res.verdict  = Verdict::Incorrect;
                res.position = position;
                res.message  = QStringLiteral("%1 is %2, below the accepted range [%3, %4].")
                                  .arg(what, num(v), num(rule.min), num(rule.max));
            } else if (v > rule.max + rule.tolerance) {
                res.verdict = Verdict::Incorrect;
                res.message = QStringLiteral("%1 is %2, above the accepted range [%3, %4].")
                                  .arg(what, num(v), num(rule.min), num(rule.max));
            } else {
                res.verdict = Verdict::Correct;
            }
            break;
        }

        case RuleType::NumericValue: {
            double v = 0.0;
            if (!parseLammpsNumber(word, v)) {
                res.verdict = Verdict::Incorrect;
                res.message =
                    QStringLiteral("%1 expects a number; \"%2\" is not one.").arg(what, word);
                break;
            }
            if (std::fabs(v - rule.ideal) <= rule.tolerance) {
                res.verdict = Verdict::Correct;
            } else {
                res.verdict = Verdict::Incorrect;
                res.message =
                    QStringLiteral("%1 should be %2, not %3.").arg(what, num(rule.ideal), num(v));
            }
            break;
        }

        case RuleType::StyleValid:
            // without a registry the answer is unknown, not wrong: the style set
            // depends on which packages this LAMMPS was built with
            if (!syntax || !syntax->isPopulated()) {
                res.verdict = Verdict::Unresolved;
                res.message = QStringLiteral("%1 cannot be checked: the list of available "
                                             "styles is not known yet.")
                                  .arg(what);
            } else if (syntax->knownStyle(rule.cat, word)) {
                res.verdict = Verdict::Correct;
            } else {
                res.verdict = Verdict::Incorrect;
                res.message = QStringLiteral("\"%1\" is not a known %2 style in this LAMMPS "
                                             "build.")
                                  .arg(word, categoryName(rule.cat));
            }
            break;
    }
    return res;
}

/* -------------------------------------------------------------------- */

StepResult TutorialEvaluator::evaluateLine(const TutorialStep &step, const QString &line) const
{
    StepResult res;
    const auto &rules = step.validate.rules;
    res.needsParse    = step.validate.alsoRequireParse ||
                     step.validate.type == ValidatorType::ParsesClean;

    // parses_clean carries no rules of its own: only the real parser can decide
    if (step.validate.type == ValidatorType::ParsesClean) {
        res.verdict = Verdict::Unresolved;
        res.message = QStringLiteral("This step is judged by LAMMPS itself, which is not "
                                     "wired up yet.");
        return res;
    }

    const QStringList words = canonicalWords(line);
    if (words.isEmpty()) {
        res.verdict = Verdict::Incorrect;
        res.message = QStringLiteral("Nothing to check -- enter a command.");
        return res;
    }

    if (words.size() != rules.size()) {
        res.verdict = Verdict::Incorrect;
        res.message = QStringLiteral("This command takes %1 words, but you gave %2.")
                          .arg(rules.size())
                          .arg(words.size());
        // point at the first position that is missing or surplus
        res.position = qMin(words.size(), rules.size());
        return res;
    }

    return evaluateHoles(step, words);
}

StepResult TutorialEvaluator::evaluateHoles(const TutorialStep &step,
                                            const QStringList &values) const
{
    StepResult res;
    const auto &rules = step.validate.rules;
    res.needsParse    = step.validate.alsoRequireParse;

    if (values.size() != rules.size()) {
        res.verdict = Verdict::Incorrect;
        res.message =
            QStringLiteral("Expected %1 values, got %2.").arg(rules.size()).arg(values.size());
        return res;
    }

    // report the first failure rather than a list: one correction at a time is
    // what the user can act on
    bool unresolved = false;
    QString firstUnresolved;
    for (int i = 0; i < rules.size(); ++i) {
        const StepResult one = applyRule(rules.at(i), values.at(i), i);
        if (one.verdict == Verdict::Incorrect) return one;
        if (one.verdict == Verdict::Unresolved && !unresolved) {
            unresolved      = true;
            firstUnresolved = one.message;
        }
    }

    if (unresolved) {
        res.verdict = Verdict::Unresolved;
        res.message = firstUnresolved;
        return res;
    }

    res.verdict = Verdict::Correct;
    return res;
}

StepResult TutorialEvaluator::evaluateChoice(const TutorialStep &step, int option) const
{
    StepResult res;
    res.position = option;

    if (option < 0 || option >= step.options.size()) {
        res.verdict = Verdict::Incorrect;
        res.message = QStringLiteral("Choose one of the options.");
        return res;
    }

    // both branches teach: the chosen option's own explanation is what the user
    // gets back, whether the prediction was right or wrong
    res.feedback = step.options.at(option).feedback;
    res.verdict  = (option == step.validate.correctOption) ? Verdict::Correct : Verdict::Incorrect;
    return res;
}

// Local Variables:
// c-basic-offset: 4
// End:
