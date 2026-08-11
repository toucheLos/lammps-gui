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

QString rewriteArgument(const QString &line, int argIndex, const QString &value)
{
    if (argIndex < 1) return line;

    const LineTokens lt = tokenizeLine(line);
    // an argument may be several tokens when it contains a quoted section, so
    // splice from the first to the last token carrying this argument index
    int start = -1;
    int end   = -1;
    for (const auto &token : lt.tokens) {
        if (token.argIndex != argIndex) continue;
        if (token.type == TokType::Comment || token.type == TokType::Continuation) continue;
        if (start < 0) start = token.start;
        end = token.start + token.length;
    }
    if (start < 0 || end < start) return line;

    QString out = line;
    out.replace(start, end - start, value);
    return out;
}

bool findCommandLine(const QString &buffer, const QString &command, int &lineNumber)
{
    if (command.isEmpty()) return false;

    InputScanner scanner;
    scanner.scan(buffer);
    for (const auto &cmd : scanner.commands()) {
        if (cmd.words.isEmpty()) continue;
        if (QString::compare(cmd.words.first().text, command, Qt::CaseInsensitive) != 0) continue;
        // InputScanner numbers lines from 1; callers index blocks from 0
        lineNumber = cmd.words.first().line - 1;
        return true;
    }
    return false;
}

// Local Variables:
// c-basic-offset: 4
// End:
