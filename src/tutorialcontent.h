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

#ifndef TUTORIALCONTENT_H
#define TUTORIALCONTENT_H

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

/**
 * @brief What a step asks of the user
 *
 * Two kinds, deliberately.  The tutorial is a guided tour rather than a quiz:
 * a step either offers commands for the script or points at something in the
 * window and explains it.  See doc/tutorial-mode-redesign.md for why the
 * earlier verb and experiment models were retired.
 */
enum class StepKind : quint8 {
    Show,   ///< present commands with annotations; the user inserts them
    Observe ///< point at a part of the GUI and explain what is there
};

/**
 * @brief Which part of the GUI a step points the user at
 *
 * The tutorial is a guided tour: the callout moves to whatever the step is
 * talking about and rings it.  Resolved to a live widget when the step opens
 * rather than stored, because some of these views are destroyed and rebuilt.
 */
enum class StepAnchor : quint8 {
    None,   ///< nothing in particular; the callout parks in the top right
    Editor, ///< the input script
    Run,    ///< the Run button
    Chart,  ///< the charts view
    Image,  ///< the snapshot image view
    Log     ///< the output view
};

/**
 * @brief Severity of a single content problem found while loading
 */
enum class ContentSeverity : quint8 {
    Warning, ///< the content loads, but something is suspect or will be ignored
    Error    ///< the content is unusable and must not be presented to a user
};

/**
 * @brief One problem found while loading a tutorial content file
 *
 * Content files are authored by hand, so a rejection has to say *where*.  The
 * path is a dotted, index-bearing address into the document, for example
 * @c acts[1].steps[3].commands[0].text , which is far more useful to an author
 * than a byte offset.
 */
struct ContentIssue {
    QString path;                                      ///< dotted path to the offending value
    ContentSeverity severity = ContentSeverity::Error; ///< how serious the problem is
    QString message;                                   ///< human readable description
};

/**
 * @brief A named idea the tutorial teaches and then stops re-explaining
 *
 * Concepts carry the reminder budget: the first few times a concept appears
 * its explanation is shown in full, and after that it collapses to something
 * the user can expand on demand.  Exposure counts live with the user rather
 * than with the tutorial, so a concept learned once is not re-taught.
 */
struct TutorialConcept {
    QString id;      ///< stable identifier referenced by annotations
    QString term;    ///< short name, e.g. "cutoff"
    QString explain; ///< the reminder text itself
};

/**
 * @brief A note attached to one argument position of a command
 */
struct TokenNote {
    int argIndex = 0; ///< 0 is the command word, 1 and up are its arguments
    QString note;     ///< what this position means here
    /// what a different value would do; the "syntax effects" half of a lesson,
    /// and the part a reference manual does not give you
    QString alternatives;
    QString conceptId; ///< concept whose reminder budget this note draws on
};

/**
 * @brief One command line presented by a Show step
 *
 * Commands are presented and inserted line by line rather than as a block, so
 * each line can carry its own explanation and the user sees the script grow
 * one idea at a time.
 */
struct CommandLine {
    QString text;           ///< the literal command, exactly as it should appear
    QString explain;        ///< what this line does
    QList<TokenNote> notes; ///< per-argument annotations
    QString conceptId;      ///< concept this whole line teaches, if any
    /// make the user type this one instead of accepting it.  Used sparingly,
    /// for reinforcement: the line is not written into the editor, the callout
    /// describes it instead, and what the user types is compared word by word.
    bool typed = false;
};

/**
 * @brief One step of a tutorial
 */
struct TutorialStep {
    QString id;                     ///< unique within the tutorial
    QString title;                  ///< short heading
    QString teach;                  ///< the teach beat, in the markdown subset
    StepKind kind = StepKind::Show; ///< what this step asks of the user
    QString docCommand;             ///< command name for the documentation link
    QString docStyle;               ///< optional style refining the documentation link

    /// comment line this step's commands belong under, e.g. "# 2) System
    /// definition"; empty appends at the end of the buffer
    QString section;
    QList<CommandLine> commands; ///< the lines to present and insert
    /// what the user should see once they act; shown after the run rather than
    /// before it, so it reads as an observation and not as an instruction
    QString expect;

    StepAnchor anchor = StepAnchor::None; ///< what the callout points at
    /// one line telling the user what to do now, shown under the prose; the
    /// callout carries no code, so this is where "click Run" lives
    QString callToAction;
    /// script to open before this step, for a tutorial that works through more
    /// than one input file; empty leaves the current buffer alone
    QString openFile;

    bool runAfterInsert = false; ///< Show: offer a run once the lines are in
    bool checkpoint     = false; ///< a milestone worth pausing on
};

/**
 * @brief A group of steps forming one arc of the tutorial
 */
struct TutorialAct {
    QString id;                ///< unique within the tutorial
    QString title;             ///< short heading
    QList<TutorialStep> steps; ///< steps in presentation order
};

/**
 * @brief Where the tutorial's material came from and under what terms
 *
 * Tutorial content commonly derives from separately published and separately
 * licensed material, so every content file has to carry its provenance.
 */
struct TutorialAttribution {
    QString source;  ///< where the material came from (URL or citation)
    QString license; ///< the terms it is offered under
    QString credit;  ///< the attribution line to display
};

/**
 * @brief A complete interactive tutorial, parsed from a content file
 *
 * Pure data with no Qt Widgets and no LAMMPS dependency: built by the parser,
 * validated on load, then only read.  Adding a tutorial is a content file and
 * never a code change, so nothing here may hardcode anything specific to a
 * particular tutorial.
 */
class TutorialContent {
public:
    TutorialContent()                                   = default;
    ~TutorialContent()                                  = default;
    TutorialContent(const TutorialContent &)            = default; ///< Copy constructor
    TutorialContent(TutorialContent &&)                 = default; ///< Move constructor
    TutorialContent &operator=(const TutorialContent &) = default; ///< Copy assignment
    TutorialContent &operator=(TutorialContent &&)      = default; ///< Move assignment

    /** @brief Schema version the file declared */
    int schemaVersion() const { return schemaver; }
    /** @brief Stable identifier, e.g. "lj-fluid" */
    const QString &id() const { return ident; }
    /** @brief Display title */
    const QString &title() const { return name; }
    /** @brief Key of the TutorialCollection this belongs to, if any */
    const QString &collection() const { return coll; }
    /** @brief 1-based tutorial number within the collection (0 if unset) */
    int tutorialNumber() const { return tutno; }
    /** @brief LAMMPS packages that must be compiled in for this to be offered */
    const QStringList &requiredPackages() const { return packages; }
    /** @brief File name of the stripped skeleton opened when the tutorial starts */
    const QString &skeletonFile() const { return skeleton; }
    /**
     * @brief The empty section headings the script starts from
     *
     * Written into the editor when a tutorial begins on an empty buffer, so the
     * user sees the shape of an input script before any of it is filled in, and
     * each command can then be filed under its own heading.
     */
    const QStringList &skeletonLines() const { return skeletonlines; }
    /** @brief Provenance of the tutorial's material */
    const TutorialAttribution &attribution() const { return credits; }
    /** @brief The acts in presentation order */
    const QList<TutorialAct> &acts() const { return actlist; }

    /** @brief True when nothing usable was parsed */
    bool isEmpty() const { return actlist.isEmpty(); }
    /** @brief Total number of steps across all acts */
    int stepCount() const;
    /** @brief Number of acts */
    int actCount() const { return static_cast<int>(actlist.size()); }

    /**
     * @brief Look up a step by act and step index
     * @param act 0-based act index
     * @param step 0-based step index within the act
     * @return the step, or nullptr when either index is out of range
     */
    const TutorialStep *step(int act, int step) const;

    /**
     * @brief Look up a step by its id
     * @param id step id to find
     * @return the step, or nullptr when no step carries that id
     */
    const TutorialStep *stepById(const QString &id) const;

    /**
     * @brief Look up a declared concept by its id
     * @param id concept id, as referenced by an annotation
     * @return the concept, or nullptr when the id was never declared
     */
    const TutorialConcept *conceptFor(const QString &id) const;

    /** @brief Every concept the tutorial declares, keyed by id */
    const QHash<QString, TutorialConcept> &concepts() const { return conceptmap; }

    /// @cond
    friend TutorialContent parseTutorialJson(const QByteArray &, QList<ContentIssue> *);
    /// @endcond

private:
    int schemaver = 0;                          ///< declared schema version
    QString ident;                              ///< stable identifier
    QString name;                               ///< display title
    QString coll;                               ///< owning collection key
    int tutno = 0;                              ///< 1-based tutorial number in the collection
    QStringList packages;                       ///< required LAMMPS packages
    QString skeleton;                           ///< stripped skeleton file name
    QStringList skeletonlines;                  ///< section headings the script starts from
    TutorialAttribution credits;                ///< provenance
    QList<TutorialAct> actlist;                 ///< the acts
    QHash<QString, TutorialConcept> conceptmap; ///< declared concepts by id
};

class QByteArray;

/**
 * @brief Parse and validate a tutorial content document
 *
 * Parses the JSON, checks it against the schema, and cross-checks what the
 * schema alone cannot express: that ids are unique, that a Show step actually
 * has commands to show, that an Experiment has at least one parameter and a
 * command to bind it to, that a prediction offering answers also names a
 * correct one, and that every annotation references a concept that was
 * declared.
 *
 * Errors leave the tutorial empty, so a file with any error is never presented
 * to a user; check with countContentErrors().  Unknown keys are warnings, so a
 * file written against a later minor revision still loads.
 *
 * @param bytes raw file contents
 * @param issues optional collector for the findings, in document order
 * @return the parsed tutorial; empty when the document was unusable
 */
TutorialContent parseTutorialJson(const QByteArray &bytes, QList<ContentIssue> *issues = nullptr);

/**
 * @brief Read and parse a tutorial content file
 *
 * Accepts both a file system path and a Qt resource path (":/..."), so bundled
 * and downloaded content take the same code path.
 *
 * @param path file system or Qt resource path
 * @param issues optional collector for the findings
 * @return the parsed tutorial; empty when the file could not be read or parsed
 */
TutorialContent loadTutorialFile(const QString &path, QList<ContentIssue> *issues = nullptr);

/** @brief Number of ERROR severity findings in a result list */
int countContentErrors(const QList<ContentIssue> &issues);

/**
 * @brief Format findings as a plain text list, one finding per line
 * @param issues findings to format
 * @param maxShown maximum number listed (-1 = all); a truncated list ends with
 *        an "... and N more" line
 * @return the formatted list
 */
QString formatContentIssues(const QList<ContentIssue> &issues, int maxShown = -1);

/** @brief Name of a step kind as it appears in a content file */
QString stepKindName(StepKind kind);

#endif // TUTORIALCONTENT_H

// Local Variables:
// c-basic-offset: 4
// End:
