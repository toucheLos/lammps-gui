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

#include "lammpssyntax.h"

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

/**
 * @brief What the user is asked to do in one step
 *
 * Every step carries exactly one verb.  The set is deliberately closed and
 * small: each verb has its own prompt shape, its own gate, and its own
 * pedagogical job, and a new verb means new UI, so growing this enum is a
 * design decision rather than a content decision.
 */
enum class StepVerb : quint8 {
    Read,    ///< nothing to do; gated by the Next button only.  Use sparingly.
    Type,    ///< type a command from a described intent; the literal is never shown
    Fill,    ///< complete a skeleton with "___" holes; the workhorse verb
    Fix,     ///< repair a deliberately broken command
    Predict, ///< answer a multiple-choice question before running
    Tune,    ///< change a parameter, re-run, and report what changed
    Inspect  ///< click each token of a command to reveal its meaning
};

/**
 * @brief How a step's submitted answer is judged
 *
 * The step-level check.  Token- and hole-level checks are expressed with
 * TutorialRule, which a validator may carry a list of.
 */
enum class ValidatorType : quint8 {
    ExactTokens,  ///< canonicalized token-by-token comparison against the rules
    TokenPattern, ///< per-position pattern or enumeration
    NumericRange, ///< min / max / ideal with a tolerance
    NumericValue, ///< a single expected number within a tolerance
    StyleValid,   ///< the word names a style LAMMPS actually has (package-aware)
    ParsesClean,  ///< LAMMPS accepts the command with no error (the Fix verb's gate)
    Choice,       ///< Predict: an index into the step's option list
    Observation,  ///< Tune: a reported value compared against real thermo output
    ScriptState   ///< a whole-script assertion evaluated after a checkpoint run
};

/**
 * @brief A check applied to one token, one skeleton hole, or one argument
 *
 * Rules are the leaf of the validation tree.  Keeping them separate from
 * TutorialValidator is what lets a single Fill step carry one independent
 * rule per "___" hole and still report which hole was wrong.
 */
enum class RuleType : quint8 {
    Any,          ///< accept anything non-empty; used for holes checked only by parsing
    Exact,        ///< match one expected word after canonicalization
    Enumerated,   ///< the word must be one of an allowed list
    Pattern,      ///< the word must match an anchored regular expression
    NumericRange, ///< the number must lie within [min, max]
    NumericValue, ///< the number must equal a value within a tolerance
    StyleValid    ///< the word must be a known style of a given category
};

/**
 * @brief Where a step's skeleton text goes in the editor
 */
enum class TargetLine : quint8 {
    Append,        ///< append at the end of the buffer
    ReplaceMarker, ///< replace the line holding a marker comment
    LineNumber     ///< replace a fixed, 1-based line number
};

/**
 * @brief Whether a successful step advances on its own
 */
enum class AdvanceMode : quint8 {
    Auto,  ///< move to the next step once the answer is accepted
    Manual ///< wait for the user to press Next, so they can read the payoff
};

/**
 * @brief Which auxiliary visual a step shows, if any
 *
 * The widgets themselves arrive in later phases; the content format names
 * them from the start so authored tutorials do not need reworking.
 */
enum class VisualWidget : quint8 {
    None,        ///< no visual for this step
    ConceptPlot, ///< live potential/force curve
    Anatomy,     ///< token-annotated view of the current command
    Lattice      ///< unit cell and Miller plane preview
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
 * Content files are authored by hand, so a rejection has to say *where*.
 * The path is a dotted, index-bearing address into the document, for
 * example @c acts[1].steps[3].validate.min , which is far more useful to an
 * author than a byte offset.
 */
struct ContentIssue {
    QString path;                                      ///< dotted path to the offending value
    ContentSeverity severity = ContentSeverity::Error; ///< how serious the problem is
    QString message;                                   ///< human readable description
};

/**
 * @brief A check on a single token, hole, or argument
 *
 * Which members are meaningful depends on @ref type; the rest are ignored.
 * Deliberately an aggregate so the parser and the tests can brace-initialize
 * it the way the syntax engine's ArgSpec is used.
 */
struct TutorialRule {
    RuleType type = RuleType::Any; ///< which check to apply
    QString text;                  ///< Exact: the expected word
    QString pattern;               ///< Pattern: anchored regular expression
    QStringList choices;           ///< Enumerated: the allowed words
    StyleCat cat = StyleCat::None; ///< StyleValid: which style category the word must be in
    double min   = 0.0;            ///< NumericRange: lower bound (inclusive)
    double max   = 0.0;            ///< NumericRange: upper bound (inclusive)
    double ideal = 0.0;            ///< the conventional answer, used for the round-trip test
    /// NumericValue: absolute tolerance; NumericRange: slack allowed outside the bounds
    double tolerance = 0.0;
    bool hasIdeal    = false; ///< whether @ref ideal was given
    QString label;            ///< what this position means, e.g. "cutoff distance"
    QString hint;             ///< nudge shown for this position before the full reveal
};

/**
 * @brief How one step judges the answer it was given
 */
struct TutorialValidator {
    ValidatorType type = ValidatorType::ParsesClean; ///< the step-level check
    /// per-position checks: one entry per "___" hole for Fill, one per token
    /// position for ExactTokens and TokenPattern, and a single entry for the
    /// scalar numeric and style checks
    QList<TutorialRule> rules;
    /// also feed the candidate to LAMMPS and require that it parses; this is
    /// how a numeric answer additionally gains the real parser as an oracle
    bool alsoRequireParse = false;
    int correctOption     = -1; ///< Choice: index into TutorialStep::options
    QString observation;        ///< Observation: the thermo keyword to compare against
    QString assertion;          ///< ScriptState: the assertion expression to evaluate
    double tolerance = 0.0;     ///< Observation: relative tolerance on the reported value
};

/**
 * @brief One answer offered by a Predict step
 */
struct TutorialOption {
    QString text;     ///< the answer as shown to the user
    QString feedback; ///< what this answer teaches, shown whether or not it is correct
};

/**
 * @brief The auxiliary visual shown alongside a step
 */
struct TutorialVisual {
    VisualWidget widget = VisualWidget::None; ///< which widget, if any
    /// widget-specific settings, kept opaque on purpose: the loader must not
    /// need updating every time a visual gains a knob
    QJsonObject config;
};

/**
 * @brief What a step puts into the editor before the user acts
 */
struct TutorialEditorAction {
    TargetLine target = TargetLine::Append; ///< where the text goes
    QString skeleton;                       ///< text to insert; "___" marks a hole
    QString marker;                         ///< ReplaceMarker: the marker to look for
    int lineNumber        = -1;             ///< LineNumber: 1-based target line
    int focusPlaceholder  = 0;              ///< which hole gets the cursor
    bool hasEditorSection = false;          ///< whether the step had an "editor" object at all

    /** @brief Number of "___" holes in the skeleton */
    int holeCount() const;
};

/**
 * @brief The prose shown after an answer is judged
 *
 * The rejection texts are per-outcome rather than a single "wrong" string so
 * that a too-small and a too-large answer can teach different things.
 */
struct TutorialFeedback {
    QString correct;              ///< shown when the answer is accepted
    QString wrong;                ///< generic rejection text
    QString below;                ///< numeric answer under the accepted range
    QString above;                ///< numeric answer over the accepted range
    QString parseError;           ///< gloss printed beneath a real LAMMPS error
    bool useLammpsMessage = true; ///< show the verbatim LAMMPS error above the gloss
};

/**
 * @brief One step: a teach beat, one action, and its verdict
 */
struct TutorialStep {
    QString id;                     ///< unique within the tutorial
    QString title;                  ///< short heading
    QString teach;                  ///< the teach beat, in the markdown subset
    StepVerb verb = StepVerb::Read; ///< what the user is asked to do
    QString docCommand;             ///< command name for the documentation link
    QString docStyle;               ///< optional style name refining the documentation link
    TutorialVisual visual;          ///< auxiliary visual, if any
    TutorialEditorAction editor;    ///< what goes into the editor
    TutorialValidator validate;     ///< how the answer is judged
    TutorialFeedback feedback;      ///< what the verdict says
    QList<TutorialOption> options;  ///< Predict: the offered answers
    QStringList hints;              ///< progressive hint ladder, narrowing with each request
    QString reveal;                 ///< the answer plus its explanation, offered after 3 tries
    AdvanceMode advance = AdvanceMode::Auto; ///< whether success advances on its own
    bool skippable      = true;              ///< a false value must be justified in review
    bool checkpoint     = false;             ///< an Expert-mode stop and a progress anchor
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
 * The model is pure data with no Qt Widgets and no LAMMPS dependency: it is
 * built by the parser, validated on load, and then only read.  Adding a
 * tutorial is a content file, never a code change, so nothing here may
 * hardcode anything specific to a particular tutorial.
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

    /// @cond
    // populated by the parser in tutorialcontent.cpp
    friend TutorialContent parseTutorialJson(const QByteArray &, QList<ContentIssue> *);
    /// @endcond

private:
    int schemaver = 0;           ///< declared schema version
    QString ident;               ///< stable identifier
    QString name;                ///< display title
    QString coll;                ///< owning collection key
    int tutno = 0;               ///< 1-based tutorial number in the collection
    QStringList packages;        ///< required LAMMPS packages
    QString skeleton;            ///< stripped skeleton file name
    TutorialAttribution credits; ///< provenance
    QList<TutorialAct> actlist;  ///< the acts
};

/**
 * @brief Parse and validate a tutorial content document
 *
 * Parses the JSON, checks it against the schema, and cross-checks the parts
 * the schema alone cannot express: that ids are unique, that each verb is
 * paired with a validator that can actually gate it, that a Fill skeleton has
 * as many "___" holes as it has rules, that a Predict step has at least two
 * options and names a correct one, and that every step which can reject an
 * answer offers a way forward.
 *
 * Errors leave the corresponding step or act out of the result, so a file
 * with any error must not be presented to a user; check with
 * countContentErrors().  Unknown keys are reported as warnings rather than
 * errors, so a file written for a later schema version still loads.
 *
 * @param bytes raw file contents
 * @param issues optional collector for the findings, in document order
 * @return the parsed tutorial; empty when the document was unusable
 */
TutorialContent parseTutorialJson(const QByteArray &bytes, QList<ContentIssue> *issues = nullptr);

/**
 * @brief Read and parse a tutorial content file
 *
 * Accepts both a file system path and a Qt resource path (":/..."), so
 * bundled and downloaded content take the same code path.
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
 * @param maxShown maximum number listed (-1 = all); a truncated list ends
 *        with an "... and N more" line
 * @return the formatted list
 */
QString formatContentIssues(const QList<ContentIssue> &issues, int maxShown = -1);

/** @brief Name of a step verb as it appears in a content file */
QString stepVerbName(StepVerb verb);
/** @brief Name of a validator type as it appears in a content file */
QString validatorTypeName(ValidatorType type);
/** @brief Name of a rule type as it appears in a content file */
QString ruleTypeName(RuleType type);

#endif // TUTORIALCONTENT_H

// Local Variables:
// c-basic-offset: 4
// End:
