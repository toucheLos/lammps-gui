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

#ifndef TUTORIALEVAL_H
#define TUTORIALEVAL_H

#include <QString>
#include <QStringList>

/**
 * @file tutorialeval.h
 * @brief Text utilities the tutorial mode needs to read and edit LAMMPS input
 *
 * All of these delegate the actual lexing to the syntax engine
 * (see lammpssyntax.h), so comments, quoting and "&" continuations behave
 * exactly the way LAMMPS behaves rather than through a second, worse copy.
 */

/**
 * @brief Split a line into canonical LAMMPS words
 *
 * Surrounding quotes are stripped and leading, trailing, and repeated
 * whitespace collapses out as a side effect of tokenizing.
 *
 * @param line one or more physical lines of LAMMPS input
 * @return the words of the first logical command, empty if there is none
 */
QStringList canonicalWords(const QString &line);

/**
 * @brief Parse a word the way LAMMPS parses a number
 *
 * Accepts what the LAMMPS input parser accepts, including the Fortran style
 * "d" exponent (@c 1.0d-3), so @c 2.5 , @c 2.50 , @c 2.5e0 and @c 2.5d0 all
 * compare equal.
 *
 * @param word word to parse
 * @param value receives the value on success
 * @return true if the word is a number
 */
bool parseLammpsNumber(const QString &word, double &value);

/**
 * @brief True when a word contains a "$" variable reference
 *
 * Such a word cannot be interpreted before LAMMPS expands it, so callers must
 * treat it as unknown rather than as a literal value.
 */
bool hasSubstitution(const QString &word);

/**
 * @brief Replace one argument of a command line, leaving the rest untouched
 *
 * Splices the replacement over the argument's own character span rather than
 * rebuilding the line, so the author's spacing, alignment, and any trailing
 * comment survive.  This is how an experiment's parameter widget edits the
 * script without reformatting it underneath the user.
 *
 * @param line the command line to edit
 * @param argIndex 1-based argument position (0 would be the command word)
 * @param value replacement text
 * @return the edited line, or the original when that argument does not exist
 */
QString rewriteArgument(const QString &line, int argIndex, const QString &value);

/**
 * @brief Find the line in a buffer that issues a given command
 *
 * Matches on the command word of each logical line, so a commented-out or
 * quoted occurrence is not mistaken for the real one.
 *
 * @param buffer complete input script
 * @param command command word to find, e.g. "timestep"
 * @param lineNumber receives the 0-based physical line index on success
 * @return true when the command was found
 */
bool findCommandLine(const QString &buffer, const QString &command, int &lineNumber);

#endif // TUTORIALEVAL_H

// Local Variables:
// c-basic-offset: 4
// End:
