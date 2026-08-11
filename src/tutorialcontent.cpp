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

#include "constants.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>
#include <QRegularExpression>
#include <QSet>

#include <cstddef>

namespace {

/// the placeholder marking a hole in a Fill skeleton
const QString HOLE = QStringLiteral("___");

/// value of "parse_error" meaning "show only the verbatim LAMMPS message"
const QString LAMMPS_MESSAGE = QStringLiteral("use_lammps_message");

/// share of gated steps above which a tutorial has regressed into transcription
constexpr double TYPE_SHARE_WARN = 0.5;

// ---- enum name tables ----------------------------------------------------
// One table per enum, used in both directions: parsing a content file and
// naming a value in a diagnostic.  Keeping the spelling in exactly one place
// is what stops the file format and the error messages from drifting apart.

struct VerbName {
    const char *name;
    StepVerb verb;
};
const VerbName VERB_NAMES[] = {
    {"READ", StepVerb::Read},       {"TYPE", StepVerb::Type},       {"FILL", StepVerb::Fill},
    {"FIX", StepVerb::Fix},         {"PREDICT", StepVerb::Predict}, {"TUNE", StepVerb::Tune},
    {"INSPECT", StepVerb::Inspect},
};

struct ValidatorName {
    const char *name;
    ValidatorType type;
};
const ValidatorName VALIDATOR_NAMES[] = {
    {"exact_tokens", ValidatorType::ExactTokens},
    {"token_pattern", ValidatorType::TokenPattern},
    {"numeric_range", ValidatorType::NumericRange},
    {"numeric_value", ValidatorType::NumericValue},
    {"style_valid", ValidatorType::StyleValid},
    {"parses_clean", ValidatorType::ParsesClean},
    {"choice", ValidatorType::Choice},
    {"observation", ValidatorType::Observation},
    {"script_state", ValidatorType::ScriptState},
};

struct RuleName {
    const char *name;
    RuleType type;
};
const RuleName RULE_NAMES[] = {
    {"any", RuleType::Any},
    {"exact", RuleType::Exact},
    {"enum", RuleType::Enumerated},
    {"pattern", RuleType::Pattern},
    {"numeric_range", RuleType::NumericRange},
    {"numeric_value", RuleType::NumericValue},
    {"style_valid", RuleType::StyleValid},
};

struct TargetName {
    const char *name;
    TargetLine target;
};
const TargetName TARGET_NAMES[] = {
    {"append", TargetLine::Append},
    {"replace_marker", TargetLine::ReplaceMarker},
    {"line_number", TargetLine::LineNumber},
};

struct WidgetName {
    const char *name;
    VisualWidget widget;
};
const WidgetName WIDGET_NAMES[] = {
    {"none", VisualWidget::None},
    {"concept_plot", VisualWidget::ConceptPlot},
    {"anatomy", VisualWidget::Anatomy},
    {"lattice", VisualWidget::Lattice},
};

struct StyleCatName {
    const char *name;
    StyleCat cat;
};
// only the categories a content author can sensibly ask about; the injected
// and static keyword sets (Color, ImageKw, Extra) are deliberately omitted
const StyleCatName STYLE_CAT_NAMES[] = {
    {"command", StyleCat::Command},     {"fix", StyleCat::Fix},
    {"compute", StyleCat::Compute},     {"dump", StyleCat::Dump},
    {"atom", StyleCat::Atom},           {"pair", StyleCat::Pair},
    {"bond", StyleCat::Bond},           {"angle", StyleCat::Angle},
    {"dihedral", StyleCat::Dihedral},   {"improper", StyleCat::Improper},
    {"kspace", StyleCat::Kspace},       {"region", StyleCat::Region},
    {"integrate", StyleCat::Integrate}, {"minimize", StyleCat::Minimize},
    {"variable", StyleCat::Variable},   {"units", StyleCat::Units},
};

/// generic lookup over one of the name tables above
template <typename T, std::size_t N, typename Enum, typename Member>
bool lookupName(const T (&table)[N], const QString &name, Member T::*field, Enum &out)
{
    for (const auto &entry : table) {
        if (name == QLatin1String(entry.name)) {
            out = entry.*field;
            return true;
        }
    }
    return false;
}

/// the accepted spellings of one name table, for an error message
template <typename T, std::size_t N> QString acceptedNames(const T (&table)[N])
{
    QStringList out;
    for (const auto &entry : table)
        out << QLatin1String(entry.name);
    return out.join(QStringLiteral(", "));
}

// ---- known keys per object ----------------------------------------------
// Unknown keys are warnings, not errors, so a file authored against a later
// schema version still loads on an older build instead of being rejected
// wholesale.  That is only safe because every key that changes *meaning*
// rather than adding one must come with a schema_version bump.

const QSet<QString> ROOT_KEYS = {
    QStringLiteral("schema_version"), QStringLiteral("id"),
    QStringLiteral("title"),          QStringLiteral("collection"),
    QStringLiteral("tutorial"),       QStringLiteral("requires_packages"),
    QStringLiteral("skeleton_file"),  QStringLiteral("attribution"),
    QStringLiteral("acts"),
};
const QSet<QString> ATTRIBUTION_KEYS = {
    QStringLiteral("source"),
    QStringLiteral("license"),
    QStringLiteral("credit"),
};
const QSet<QString> ACT_KEYS = {
    QStringLiteral("id"),
    QStringLiteral("title"),
    QStringLiteral("steps"),
};
const QSet<QString> STEP_KEYS = {
    QStringLiteral("id"),      QStringLiteral("verb"),      QStringLiteral("title"),
    QStringLiteral("teach"),   QStringLiteral("doc_link"),  QStringLiteral("visual"),
    QStringLiteral("editor"),  QStringLiteral("validate"),  QStringLiteral("feedback"),
    QStringLiteral("options"), QStringLiteral("hints"),     QStringLiteral("reveal"),
    QStringLiteral("advance"), QStringLiteral("skippable"), QStringLiteral("checkpoint"),
};
const QSet<QString> VISUAL_KEYS = {QStringLiteral("widget"), QStringLiteral("config")};
const QSet<QString> EDITOR_KEYS = {
    QStringLiteral("target_line"), QStringLiteral("skeleton"),          QStringLiteral("marker"),
    QStringLiteral("line_number"), QStringLiteral("focus_placeholder"),
};
const QSet<QString> VALIDATE_KEYS = {
    QStringLiteral("type"),
    QStringLiteral("rules"),
    QStringLiteral("also_require_parse"),
    QStringLiteral("correct_option"),
    QStringLiteral("observation"),
    QStringLiteral("assertion"),
    QStringLiteral("tolerance"),
};
const QSet<QString> RULE_KEYS = {
    QStringLiteral("type"),  QStringLiteral("text"),     QStringLiteral("pattern"),
    QStringLiteral("enum"),  QStringLiteral("category"), QStringLiteral("min"),
    QStringLiteral("max"),   QStringLiteral("ideal"),    QStringLiteral("tolerance"),
    QStringLiteral("label"), QStringLiteral("hint"),     QStringLiteral("case_sensitive"),
};
const QSet<QString> FEEDBACK_KEYS = {
    QStringLiteral("correct"), QStringLiteral("wrong"),       QStringLiteral("below"),
    QStringLiteral("above"),   QStringLiteral("parse_error"),
};
const QSet<QString> OPTION_KEYS = {QStringLiteral("text"), QStringLiteral("feedback")};

/**
 * @brief Issue collector that knows where in the document it is
 *
 * Threading a path prefix through the parse is what turns "invalid content"
 * into "acts[1].steps[3].validate.min is not a number", which is the whole
 * point of validating an authored file.
 */
class Ctx {
public:
    explicit Ctx(QList<ContentIssue> *sink) : sink(sink) {}

    void error(const QString &path, const QString &message) const
    {
        add(path, ContentSeverity::Error, message);
    }
    void warn(const QString &path, const QString &message) const
    {
        add(path, ContentSeverity::Warning, message);
    }

    /// report every key of @p obj that the schema does not know
    void checkKeys(const QJsonObject &obj, const QSet<QString> &known, const QString &path) const
    {
        for (auto it = obj.constBegin(); it != obj.constEnd(); ++it)
            if (!known.contains(it.key()))
                warn(path.isEmpty() ? it.key() : path + QLatin1Char('.') + it.key(),
                     QStringLiteral("unknown key, ignored (written for a newer schema?)"));
    }

    /// number of ERROR findings reported so far; mutable because reporting is
    /// a logically const operation on a collector that is passed around as a
    /// const reference by the field readers
    mutable int errors = 0;

private:
    void add(const QString &path, ContentSeverity sev, const QString &message) const
    {
        if (sev == ContentSeverity::Error) ++errors;
        if (sink) sink->append({path, sev, message});
    }

    QList<ContentIssue> *sink;
};

/// join a path prefix with a key
QString sub(const QString &path, const QString &key)
{
    return path.isEmpty() ? key : path + QLatin1Char('.') + key;
}

/// join a path prefix with an array index
QString idx(const QString &path, int i)
{
    return path + QLatin1Char('[') + QString::number(i) + QLatin1Char(']');
}

// ---- typed field readers -------------------------------------------------
// Each reports a typed error at its own path and leaves the output untouched
// when the key is absent, so callers can pre-seed defaults.

bool readString(const QJsonObject &obj, const QString &key, const QString &path, const Ctx &ctx,
                QString &out, bool required = false)
{
    if (!obj.contains(key)) {
        if (required) ctx.error(sub(path, key), QStringLiteral("required key is missing"));
        return false;
    }
    const QJsonValue v = obj.value(key);
    if (!v.isString()) {
        ctx.error(sub(path, key), QStringLiteral("expected a string"));
        return false;
    }
    out = v.toString();
    if (required && out.isEmpty()) {
        ctx.error(sub(path, key), QStringLiteral("must not be empty"));
        return false;
    }
    return true;
}

bool readBool(const QJsonObject &obj, const QString &key, const QString &path, const Ctx &ctx,
              bool &out)
{
    if (!obj.contains(key)) return false;
    const QJsonValue v = obj.value(key);
    if (!v.isBool()) {
        ctx.error(sub(path, key), QStringLiteral("expected true or false"));
        return false;
    }
    out = v.toBool();
    return true;
}

bool readDouble(const QJsonObject &obj, const QString &key, const QString &path, const Ctx &ctx,
                double &out)
{
    if (!obj.contains(key)) return false;
    const QJsonValue v = obj.value(key);
    if (!v.isDouble()) {
        ctx.error(sub(path, key), QStringLiteral("expected a number"));
        return false;
    }
    out = v.toDouble();
    return true;
}

bool readInt(const QJsonObject &obj, const QString &key, const QString &path, const Ctx &ctx,
             int &out)
{
    double d = 0.0;
    if (!readDouble(obj, key, path, ctx, d)) return false;
    out = static_cast<int>(d);
    return true;
}

bool readStringList(const QJsonObject &obj, const QString &key, const QString &path, const Ctx &ctx,
                    QStringList &out)
{
    if (!obj.contains(key)) return false;
    const QJsonValue v = obj.value(key);
    if (!v.isArray()) {
        ctx.error(sub(path, key), QStringLiteral("expected an array of strings"));
        return false;
    }
    const QJsonArray arr = v.toArray();
    for (int i = 0; i < arr.size(); ++i) {
        if (!arr.at(i).isString()) {
            ctx.error(idx(sub(path, key), i), QStringLiteral("expected a string"));
            continue;
        }
        out << arr.at(i).toString();
    }
    return true;
}

/// read a key that must be an object; returns false (without an error) when absent
bool readObject(const QJsonObject &obj, const QString &key, const QString &path, const Ctx &ctx,
                QJsonObject &out)
{
    if (!obj.contains(key)) return false;
    const QJsonValue v = obj.value(key);
    if (!v.isObject()) {
        ctx.error(sub(path, key), QStringLiteral("expected an object"));
        return false;
    }
    out = v.toObject();
    return true;
}

// ---- section parsers -----------------------------------------------------

TutorialRule parseRule(const QJsonObject &obj, const QString &path, Ctx &ctx)
{
    TutorialRule rule;
    ctx.checkKeys(obj, RULE_KEYS, path);

    QString typestr;
    if (readString(obj, QStringLiteral("type"), path, ctx, typestr, true)) {
        if (!lookupName(RULE_NAMES, typestr, &RuleName::type, rule.type))
            ctx.error(sub(path, QStringLiteral("type")),
                      QStringLiteral("unknown rule type \"%1\"; expected one of: %2")
                          .arg(typestr, acceptedNames(RULE_NAMES)));
    }

    readString(obj, QStringLiteral("text"), path, ctx, rule.text);
    readString(obj, QStringLiteral("pattern"), path, ctx, rule.pattern);
    readStringList(obj, QStringLiteral("enum"), path, ctx, rule.choices);
    readString(obj, QStringLiteral("label"), path, ctx, rule.label);
    readString(obj, QStringLiteral("hint"), path, ctx, rule.hint);
    readBool(obj, QStringLiteral("case_sensitive"), path, ctx, rule.caseSensitive);
    readDouble(obj, QStringLiteral("min"), path, ctx, rule.min);
    readDouble(obj, QStringLiteral("max"), path, ctx, rule.max);
    readDouble(obj, QStringLiteral("tolerance"), path, ctx, rule.tolerance);
    rule.hasIdeal = readDouble(obj, QStringLiteral("ideal"), path, ctx, rule.ideal);

    QString catstr;
    if (readString(obj, QStringLiteral("category"), path, ctx, catstr))
        if (!lookupName(STYLE_CAT_NAMES, catstr, &StyleCatName::cat, rule.cat))
            ctx.error(sub(path, QStringLiteral("category")),
                      QStringLiteral("unknown style category \"%1\"; expected one of: %2")
                          .arg(catstr, acceptedNames(STYLE_CAT_NAMES)));

    // per-type requirements: a rule that cannot decide anything is a content bug
    switch (rule.type) {
        case RuleType::Exact:
            if (rule.text.isEmpty())
                ctx.error(sub(path, QStringLiteral("text")),
                          QStringLiteral("an \"exact\" rule needs the expected word"));
            break;
        case RuleType::Enumerated:
            if (rule.choices.isEmpty())
                ctx.error(sub(path, QStringLiteral("enum")),
                          QStringLiteral("an \"enum\" rule needs at least one allowed word"));
            break;
        case RuleType::Pattern:
            if (rule.pattern.isEmpty()) {
                ctx.error(sub(path, QStringLiteral("pattern")),
                          QStringLiteral("a \"pattern\" rule needs a regular expression"));
            } else {
                // compile now: a broken expression must fail at load, not mid-tutorial
                const QRegularExpression re(QRegularExpression::anchoredPattern(rule.pattern));
                if (!re.isValid())
                    ctx.error(
                        sub(path, QStringLiteral("pattern")),
                        QStringLiteral("invalid regular expression: %1").arg(re.errorString()));
            }
            break;
        case RuleType::NumericRange:
            if (!obj.contains(QStringLiteral("min")) || !obj.contains(QStringLiteral("max"))) {
                ctx.error(path, QStringLiteral(
                                    "a \"numeric_range\" rule needs both \"min\" and \"max\""));
            } else if (rule.min > rule.max) {
                ctx.error(sub(path, QStringLiteral("min")),
                          QStringLiteral("min (%1) is greater than max (%2)")
                              .arg(rule.min)
                              .arg(rule.max));
            } else if (rule.hasIdeal && (rule.ideal < rule.min || rule.ideal > rule.max)) {
                // the round-trip test plays every step with its ideal answer, so an
                // ideal outside the accepted range would fail the tutorial's own test
                ctx.error(sub(path, QStringLiteral("ideal")),
                          QStringLiteral("ideal (%1) lies outside [min, max] = [%2, %3]")
                              .arg(rule.ideal)
                              .arg(rule.min)
                              .arg(rule.max));
            }
            break;
        case RuleType::NumericValue:
            if (!rule.hasIdeal)
                ctx.error(sub(path, QStringLiteral("ideal")),
                          QStringLiteral("a \"numeric_value\" rule needs the expected number "
                                         "in \"ideal\""));
            break;
        case RuleType::StyleValid:
            if (rule.cat == StyleCat::None)
                ctx.error(sub(path, QStringLiteral("category")),
                          QStringLiteral("a \"style_valid\" rule needs a style category"));
            break;
        case RuleType::Any:
            break;
    }
    return rule;
}

TutorialValidator parseValidator(const QJsonObject &obj, const QString &path, Ctx &ctx)
{
    TutorialValidator val;
    ctx.checkKeys(obj, VALIDATE_KEYS, path);

    QString typestr;
    if (readString(obj, QStringLiteral("type"), path, ctx, typestr, true)) {
        if (!lookupName(VALIDATOR_NAMES, typestr, &ValidatorName::type, val.type))
            ctx.error(sub(path, QStringLiteral("type")),
                      QStringLiteral("unknown validator type \"%1\"; expected one of: %2")
                          .arg(typestr, acceptedNames(VALIDATOR_NAMES)));
    }

    readBool(obj, QStringLiteral("also_require_parse"), path, ctx, val.alsoRequireParse);
    readInt(obj, QStringLiteral("correct_option"), path, ctx, val.correctOption);
    readString(obj, QStringLiteral("observation"), path, ctx, val.observation);
    readString(obj, QStringLiteral("assertion"), path, ctx, val.assertion);
    readDouble(obj, QStringLiteral("tolerance"), path, ctx, val.tolerance);

    if (obj.contains(QStringLiteral("rules"))) {
        const QJsonValue v = obj.value(QStringLiteral("rules"));
        if (!v.isArray()) {
            ctx.error(sub(path, QStringLiteral("rules")), QStringLiteral("expected an array"));
        } else {
            const QJsonArray arr    = v.toArray();
            const QString rulespath = sub(path, QStringLiteral("rules"));
            for (int i = 0; i < arr.size(); ++i) {
                if (!arr.at(i).isObject()) {
                    ctx.error(idx(rulespath, i), QStringLiteral("expected an object"));
                    continue;
                }
                val.rules.append(parseRule(arr.at(i).toObject(), idx(rulespath, i), ctx));
            }
        }
    }

    // validators that cannot work without rules
    switch (val.type) {
        case ValidatorType::ExactTokens:
        case ValidatorType::TokenPattern:
        case ValidatorType::NumericRange:
        case ValidatorType::NumericValue:
        case ValidatorType::StyleValid:
            if (val.rules.isEmpty())
                ctx.error(sub(path, QStringLiteral("rules")),
                          QStringLiteral("a \"%1\" validator needs at least one rule")
                              .arg(validatorTypeName(val.type)));
            break;
        case ValidatorType::Observation:
            if (val.observation.isEmpty())
                ctx.error(sub(path, QStringLiteral("observation")),
                          QStringLiteral("an \"observation\" validator needs the thermo "
                                         "keyword to compare against"));
            if (val.tolerance <= 0.0)
                ctx.error(sub(path, QStringLiteral("tolerance")),
                          QStringLiteral("an \"observation\" validator needs a positive "
                                         "tolerance; exact float equality never holds"));
            break;
        case ValidatorType::ScriptState:
            if (val.assertion.isEmpty())
                ctx.error(sub(path, QStringLiteral("assertion")),
                          QStringLiteral("a \"script_state\" validator needs an assertion"));
            break;
        case ValidatorType::Choice:
        case ValidatorType::ParsesClean:
            break;
    }
    return val;
}

TutorialEditorAction parseEditor(const QJsonObject &obj, const QString &path, Ctx &ctx)
{
    TutorialEditorAction ed;
    ed.hasEditorSection = true;
    ctx.checkKeys(obj, EDITOR_KEYS, path);

    QString targetstr;
    if (readString(obj, QStringLiteral("target_line"), path, ctx, targetstr))
        if (!lookupName(TARGET_NAMES, targetstr, &TargetName::target, ed.target))
            ctx.error(sub(path, QStringLiteral("target_line")),
                      QStringLiteral("unknown target \"%1\"; expected one of: %2")
                          .arg(targetstr, acceptedNames(TARGET_NAMES)));

    readString(obj, QStringLiteral("skeleton"), path, ctx, ed.skeleton);
    readString(obj, QStringLiteral("marker"), path, ctx, ed.marker);
    readInt(obj, QStringLiteral("line_number"), path, ctx, ed.lineNumber);
    readInt(obj, QStringLiteral("focus_placeholder"), path, ctx, ed.focusPlaceholder);

    if (ed.target == TargetLine::ReplaceMarker && ed.marker.isEmpty())
        ctx.error(sub(path, QStringLiteral("marker")),
                  QStringLiteral("\"replace_marker\" needs the marker text to look for"));
    if (ed.target == TargetLine::LineNumber && ed.lineNumber < 1)
        ctx.error(sub(path, QStringLiteral("line_number")),
                  QStringLiteral("\"line_number\" needs a 1-based line number"));

    const int holes = ed.holeCount();
    if (ed.focusPlaceholder < 0 || (holes > 0 && ed.focusPlaceholder >= holes))
        ctx.error(sub(path, QStringLiteral("focus_placeholder")),
                  QStringLiteral("focus_placeholder %1 is out of range; the skeleton has %2 holes")
                      .arg(ed.focusPlaceholder)
                      .arg(holes));
    return ed;
}

TutorialFeedback parseFeedback(const QJsonObject &obj, const QString &path, Ctx &ctx)
{
    TutorialFeedback fb;
    ctx.checkKeys(obj, FEEDBACK_KEYS, path);
    readString(obj, QStringLiteral("correct"), path, ctx, fb.correct);
    readString(obj, QStringLiteral("wrong"), path, ctx, fb.wrong);
    readString(obj, QStringLiteral("below"), path, ctx, fb.below);
    readString(obj, QStringLiteral("above"), path, ctx, fb.above);

    QString parse;
    if (readString(obj, QStringLiteral("parse_error"), path, ctx, parse)) {
        // the real LAMMPS error text is always shown; this string is the gloss
        // printed beneath it, and the sentinel means "no gloss, just the error"
        if (parse != LAMMPS_MESSAGE) fb.parseError = parse;
    }
    return fb;
}

TutorialVisual parseVisual(const QJsonObject &obj, const QString &path, Ctx &ctx)
{
    TutorialVisual vis;
    ctx.checkKeys(obj, VISUAL_KEYS, path);

    QString widgetstr;
    if (readString(obj, QStringLiteral("widget"), path, ctx, widgetstr, true))
        if (!lookupName(WIDGET_NAMES, widgetstr, &WidgetName::widget, vis.widget))
            ctx.error(sub(path, QStringLiteral("widget")),
                      QStringLiteral("unknown visual \"%1\"; expected one of: %2")
                          .arg(widgetstr, acceptedNames(WIDGET_NAMES)));

    QJsonObject cfg;
    if (readObject(obj, QStringLiteral("config"), path, ctx, cfg)) vis.config = cfg;
    return vis;
}

/// true when the verb gates progress, i.e. the user can be told "not yet"
bool isGated(StepVerb verb)
{
    return verb != StepVerb::Read && verb != StepVerb::Inspect;
}

/// cross-check that the verb and its validator can actually gate each other
void checkVerbValidator(const TutorialStep &step, bool hasValidate, const QString &path, Ctx &ctx)
{
    const QString vpath    = sub(path, QStringLiteral("validate"));
    const ValidatorType vt = step.validate.type;

    // a checkpoint may additionally assert whole-script state whatever its verb
    if (hasValidate && vt == ValidatorType::ScriptState) {
        if (!step.checkpoint)
            ctx.error(vpath, QStringLiteral("a \"script_state\" validator is only meaningful on "
                                            "a checkpoint step"));
        return;
    }

    switch (step.verb) {
        case StepVerb::Read:
        case StepVerb::Inspect:
            if (hasValidate)
                ctx.warn(vpath, QStringLiteral("a %1 step is gated by the Next button; the "
                                               "validator is ignored")
                                    .arg(stepVerbName(step.verb)));
            break;

        case StepVerb::Type:
            if (!hasValidate) {
                ctx.error(vpath, QStringLiteral("a TYPE step needs a validator"));
            } else if (vt != ValidatorType::ExactTokens && vt != ValidatorType::TokenPattern) {
                ctx.error(vpath,
                          QStringLiteral("a TYPE step needs \"exact_tokens\" or "
                                         "\"token_pattern\"; \"%1\" cannot tell which command "
                                         "was meant")
                              .arg(validatorTypeName(vt)));
            }
            break;

        case StepVerb::Fill:
            if (!hasValidate) {
                ctx.error(vpath, QStringLiteral("a FILL step needs a validator"));
            } else if (vt == ValidatorType::Choice || vt == ValidatorType::Observation ||
                       vt == ValidatorType::ParsesClean) {
                ctx.error(vpath, QStringLiteral("a FILL step cannot be gated by \"%1\"")
                                     .arg(validatorTypeName(vt)));
            } else if (step.editor.holeCount() == 0) {
                ctx.error(sub(path, QStringLiteral("editor.skeleton")),
                          QStringLiteral("a FILL step needs a skeleton containing at least one "
                                         "\"___\" hole"));
            } else if (step.validate.rules.size() != step.editor.holeCount()) {
                ctx.error(vpath, QStringLiteral("the skeleton has %1 \"___\" holes but %2 rules "
                                                "were given; they must correspond one to one")
                                     .arg(step.editor.holeCount())
                                     .arg(step.validate.rules.size()));
            }
            break;

        case StepVerb::Fix:
            if (!hasValidate) {
                ctx.error(vpath, QStringLiteral("a FIX step needs a validator"));
            } else if (vt != ValidatorType::ParsesClean) {
                ctx.error(vpath, QStringLiteral("a FIX step is gated by \"parses_clean\"; the "
                                                "point is that LAMMPS itself accepts the "
                                                "repaired command"));
            }
            if (step.editor.skeleton.isEmpty())
                ctx.error(sub(path, QStringLiteral("editor.skeleton")),
                          QStringLiteral("a FIX step needs the broken command to repair"));
            else if (step.editor.holeCount() > 0)
                ctx.error(sub(path, QStringLiteral("editor.skeleton")),
                          QStringLiteral("a FIX step presents a broken command, not \"___\" "
                                         "holes; use FILL for holes"));
            break;

        case StepVerb::Predict:
            if (!hasValidate) {
                ctx.error(vpath, QStringLiteral("a PREDICT step needs a validator"));
            } else if (vt != ValidatorType::Choice) {
                ctx.error(vpath, QStringLiteral("a PREDICT step is gated by \"choice\""));
            } else if (step.options.size() < 2) {
                ctx.error(sub(path, QStringLiteral("options")),
                          QStringLiteral("a PREDICT step needs at least two options; one option "
                                         "can be clicked past"));
            } else if (step.validate.correctOption < 0 ||
                       step.validate.correctOption >= step.options.size()) {
                ctx.error(sub(vpath, QStringLiteral("correct_option")),
                          QStringLiteral("correct_option %1 is out of range for %2 options")
                              .arg(step.validate.correctOption)
                              .arg(step.options.size()));
            }
            break;

        case StepVerb::Tune:
            if (!hasValidate) {
                ctx.error(vpath, QStringLiteral("a TUNE step needs a validator"));
            } else if (vt != ValidatorType::Observation) {
                ctx.error(vpath, QStringLiteral("a TUNE step is gated by \"observation\": the "
                                                "user re-runs and reports what changed"));
            }
            break;
    }

    // both branches of a PREDICT have to teach, or the question is a coin flip
    if (step.verb == StepVerb::Predict)
        for (int i = 0; i < step.options.size(); ++i)
            if (step.options.at(i).feedback.isEmpty())
                ctx.warn(idx(sub(path, QStringLiteral("options")), i),
                         QStringLiteral("this option teaches nothing; every PREDICT answer "
                                        "should explain itself"));
}

TutorialStep parseStep(const QJsonObject &obj, const QString &path, Ctx &ctx)
{
    TutorialStep step;
    ctx.checkKeys(obj, STEP_KEYS, path);

    readString(obj, QStringLiteral("id"), path, ctx, step.id, true);
    readString(obj, QStringLiteral("title"), path, ctx, step.title, true);
    readString(obj, QStringLiteral("teach"), path, ctx, step.teach);
    readString(obj, QStringLiteral("reveal"), path, ctx, step.reveal);
    readStringList(obj, QStringLiteral("hints"), path, ctx, step.hints);
    readBool(obj, QStringLiteral("skippable"), path, ctx, step.skippable);
    readBool(obj, QStringLiteral("checkpoint"), path, ctx, step.checkpoint);

    QString verbstr;
    if (readString(obj, QStringLiteral("verb"), path, ctx, verbstr, true))
        if (!lookupName(VERB_NAMES, verbstr, &VerbName::verb, step.verb))
            ctx.error(sub(path, QStringLiteral("verb")),
                      QStringLiteral("unknown verb \"%1\"; expected one of: %2")
                          .arg(verbstr, acceptedNames(VERB_NAMES)));

    QString advstr;
    if (readString(obj, QStringLiteral("advance"), path, ctx, advstr)) {
        if (advstr == QLatin1String("auto")) {
            step.advance = AdvanceMode::Auto;
        } else if (advstr == QLatin1String("manual")) {
            step.advance = AdvanceMode::Manual;
        } else {
            ctx.error(sub(path, QStringLiteral("advance")),
                      QStringLiteral("expected \"auto\" or \"manual\""));
        }
    }

    // "pair_style lj/cut" resolves against the shipped help index, which is
    // keyed by command and optionally by style
    QString doclink;
    if (readString(obj, QStringLiteral("doc_link"), path, ctx, doclink) && !doclink.isEmpty()) {
        const QStringList words =
            doclink.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        if (words.size() > 2) {
            ctx.error(sub(path, QStringLiteral("doc_link")),
                      QStringLiteral("expected \"<command>\" or \"<command> <style>\""));
        } else {
            step.docCommand = words.value(0);
            step.docStyle   = words.value(1);
        }
    }

    QJsonObject secobj;
    if (readObject(obj, QStringLiteral("visual"), path, ctx, secobj))
        step.visual = parseVisual(secobj, sub(path, QStringLiteral("visual")), ctx);
    if (readObject(obj, QStringLiteral("editor"), path, ctx, secobj))
        step.editor = parseEditor(secobj, sub(path, QStringLiteral("editor")), ctx);
    if (readObject(obj, QStringLiteral("feedback"), path, ctx, secobj))
        step.feedback = parseFeedback(secobj, sub(path, QStringLiteral("feedback")), ctx);

    if (obj.contains(QStringLiteral("options"))) {
        const QJsonValue v = obj.value(QStringLiteral("options"));
        if (!v.isArray()) {
            ctx.error(sub(path, QStringLiteral("options")), QStringLiteral("expected an array"));
        } else {
            const QJsonArray arr   = v.toArray();
            const QString optspath = sub(path, QStringLiteral("options"));
            for (int i = 0; i < arr.size(); ++i) {
                if (!arr.at(i).isObject()) {
                    ctx.error(idx(optspath, i), QStringLiteral("expected an object"));
                    continue;
                }
                const QJsonObject o = arr.at(i).toObject();
                ctx.checkKeys(o, OPTION_KEYS, idx(optspath, i));
                TutorialOption opt;
                readString(o, QStringLiteral("text"), idx(optspath, i), ctx, opt.text, true);
                readString(o, QStringLiteral("feedback"), idx(optspath, i), ctx, opt.feedback);
                step.options.append(opt);
            }
        }
    }

    bool hasValidate = false;
    if (readObject(obj, QStringLiteral("validate"), path, ctx, secobj)) {
        step.validate = parseValidator(secobj, sub(path, QStringLiteral("validate")), ctx);
        hasValidate   = true;
    }
    checkVerbValidator(step, hasValidate, path, ctx);

    if (step.teach.isEmpty())
        ctx.warn(sub(path, QStringLiteral("teach")),
                 QStringLiteral("no teach text; a step that explains nothing is busywork"));

    // The anti-coercion rule, enforced mechanically rather than by review: a
    // step that can reject an answer must offer a way past it.
    if (isGated(step.verb) && !step.skippable && step.reveal.isEmpty())
        ctx.error(sub(path, QStringLiteral("skippable")),
                  QStringLiteral("a non-skippable %1 step must provide \"reveal\", otherwise a "
                                 "stuck user has no way forward")
                      .arg(stepVerbName(step.verb)));

    return step;
}

TutorialAct parseAct(const QJsonObject &obj, const QString &path, Ctx &ctx)
{
    TutorialAct act;
    ctx.checkKeys(obj, ACT_KEYS, path);
    readString(obj, QStringLiteral("id"), path, ctx, act.id, true);
    readString(obj, QStringLiteral("title"), path, ctx, act.title, true);

    const QJsonValue v = obj.value(QStringLiteral("steps"));
    if (!v.isArray()) {
        ctx.error(sub(path, QStringLiteral("steps")), QStringLiteral("expected an array of steps"));
        return act;
    }
    const QJsonArray arr    = v.toArray();
    const QString stepspath = sub(path, QStringLiteral("steps"));
    if (arr.isEmpty()) ctx.error(stepspath, QStringLiteral("an act needs at least one step"));

    StepVerb prev = StepVerb::Inspect; // anything but Read, so step 0 never trips the check
    bool haveprev = false;
    for (int i = 0; i < arr.size(); ++i) {
        if (!arr.at(i).isObject()) {
            ctx.error(idx(stepspath, i), QStringLiteral("expected an object"));
            continue;
        }
        const TutorialStep step = parseStep(arr.at(i).toObject(), idx(stepspath, i), ctx);
        if (haveprev && step.verb == StepVerb::Read && prev == StepVerb::Read)
            ctx.warn(idx(stepspath, i),
                     QStringLiteral("two READ steps in a row; the user is reading, not doing"));
        prev     = step.verb;
        haveprev = true;
        act.steps.append(step);
    }
    return act;
}

} // namespace

/* -------------------------------------------------------------------- */

int TutorialEditorAction::holeCount() const
{
    return static_cast<int>(skeleton.count(HOLE));
}

/* -------------------------------------------------------------------- */

int TutorialContent::stepCount() const
{
    int n = 0;
    for (const auto &act : actlist)
        n += static_cast<int>(act.steps.size());
    return n;
}

const TutorialStep *TutorialContent::step(int act, int step) const
{
    if (act < 0 || act >= actlist.size()) return nullptr;
    const auto &steps = actlist.at(act).steps;
    if (step < 0 || step >= steps.size()) return nullptr;
    return &steps.at(step);
}

const TutorialStep *TutorialContent::stepById(const QString &id) const
{
    for (const auto &act : actlist)
        for (const auto &step : act.steps)
            if (step.id == id) return &step;
    return nullptr;
}

/* -------------------------------------------------------------------- */

QString stepVerbName(StepVerb verb)
{
    for (const auto &entry : VERB_NAMES)
        if (entry.verb == verb) return QLatin1String(entry.name);
    return QStringLiteral("?");
}

QString validatorTypeName(ValidatorType type)
{
    for (const auto &entry : VALIDATOR_NAMES)
        if (entry.type == type) return QLatin1String(entry.name);
    return QStringLiteral("?");
}

QString ruleTypeName(RuleType type)
{
    for (const auto &entry : RULE_NAMES)
        if (entry.type == type) return QLatin1String(entry.name);
    return QStringLiteral("?");
}

/* -------------------------------------------------------------------- */

TutorialContent parseTutorialJson(const QByteArray &bytes, QList<ContentIssue> *issues)
{
    TutorialContent out;
    Ctx ctx(issues);

    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &perr);
    if (doc.isNull()) {
        ctx.error(QString(), QStringLiteral("not valid JSON: %1 (at offset %2)")
                                 .arg(perr.errorString())
                                 .arg(perr.offset));
        return out;
    }
    if (!doc.isObject()) {
        ctx.error(QString(), QStringLiteral("the document must be a JSON object"));
        return out;
    }

    const QJsonObject root = doc.object();
    ctx.checkKeys(root, ROOT_KEYS, QString());

    if (!readInt(root, QStringLiteral("schema_version"), QString(), ctx, out.schemaver)) {
        ctx.error(QStringLiteral("schema_version"),
                  QStringLiteral("required key is missing; refusing to guess the format"));
        return out;
    }
    if (out.schemaver != Cfg::TUTORIAL_SCHEMA_VERSION) {
        ctx.error(QStringLiteral("schema_version"),
                  QStringLiteral("unsupported schema version %1; this build reads version %2")
                      .arg(out.schemaver)
                      .arg(Cfg::TUTORIAL_SCHEMA_VERSION));
        return out;
    }

    readString(root, QStringLiteral("id"), QString(), ctx, out.ident, true);
    readString(root, QStringLiteral("title"), QString(), ctx, out.name, true);
    readString(root, QStringLiteral("collection"), QString(), ctx, out.coll);
    readString(root, QStringLiteral("skeleton_file"), QString(), ctx, out.skeleton);
    readInt(root, QStringLiteral("tutorial"), QString(), ctx, out.tutno);
    readStringList(root, QStringLiteral("requires_packages"), QString(), ctx, out.packages);

    QJsonObject attrib;
    if (readObject(root, QStringLiteral("attribution"), QString(), ctx, attrib)) {
        ctx.checkKeys(attrib, ATTRIBUTION_KEYS, QStringLiteral("attribution"));
        readString(attrib, QStringLiteral("source"), QStringLiteral("attribution"), ctx,
                   out.credits.source);
        readString(attrib, QStringLiteral("license"), QStringLiteral("attribution"), ctx,
                   out.credits.license);
        readString(attrib, QStringLiteral("credit"), QStringLiteral("attribution"), ctx,
                   out.credits.credit);
    }
    // content commonly derives from separately licensed material, so shipping a
    // file without provenance is a licensing problem, not a cosmetic one
    if (out.credits.license.isEmpty())
        ctx.warn(QStringLiteral("attribution.license"),
                 QStringLiteral("no license recorded; tutorial material is often separately "
                                "licensed and must carry its terms"));

    const QJsonValue actsval = root.value(QStringLiteral("acts"));
    if (!actsval.isArray()) {
        ctx.error(QStringLiteral("acts"), QStringLiteral("expected an array of acts"));
        return out;
    }
    const QJsonArray arr = actsval.toArray();
    if (arr.isEmpty())
        ctx.error(QStringLiteral("acts"), QStringLiteral("a tutorial needs at "
                                                         "least one act"));
    for (int i = 0; i < arr.size(); ++i) {
        if (!arr.at(i).isObject()) {
            ctx.error(idx(QStringLiteral("acts"), i), QStringLiteral("expected an object"));
            continue;
        }
        out.actlist.append(parseAct(arr.at(i).toObject(), idx(QStringLiteral("acts"), i), ctx));
    }

    // ids address steps in saved progress, so a duplicate would silently
    // resume the wrong step
    QSet<QString> seenacts;
    QSet<QString> seensteps;
    for (int a = 0; a < out.actlist.size(); ++a) {
        const auto &act = out.actlist.at(a);
        if (!act.id.isEmpty()) {
            if (seenacts.contains(act.id))
                ctx.error(sub(idx(QStringLiteral("acts"), a), QStringLiteral("id")),
                          QStringLiteral("duplicate act id \"%1\"").arg(act.id));
            seenacts.insert(act.id);
        }
        for (int s = 0; s < act.steps.size(); ++s) {
            const auto &step = act.steps.at(s);
            if (step.id.isEmpty()) continue;
            if (seensteps.contains(step.id))
                ctx.error(sub(idx(sub(idx(QStringLiteral("acts"), a), QStringLiteral("steps")), s),
                              QStringLiteral("id")),
                          QStringLiteral("duplicate step id \"%1\"").arg(step.id));
            seensteps.insert(step.id);
        }
    }

    // a tutorial that is mostly TYPE has regressed into transcription
    int gated = 0;
    int typed = 0;
    for (const auto &act : out.actlist)
        for (const auto &step : act.steps) {
            if (!isGated(step.verb)) continue;
            ++gated;
            if (step.verb == StepVerb::Type) ++typed;
        }
    if (gated > 0 && static_cast<double>(typed) > TYPE_SHARE_WARN * gated)
        ctx.warn(QStringLiteral("acts"),
                 QStringLiteral("%1 of %2 gated steps are TYPE; a tutorial that is mostly TYPE "
                                "is transcription, not practice")
                     .arg(typed)
                     .arg(gated));

    if (ctx.errors > 0) out.actlist.clear();
    return out;
}

TutorialContent loadTutorialFile(const QString &path, QList<ContentIssue> *issues)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (issues)
            issues->append(
                {path, ContentSeverity::Error,
                 QStringLiteral("cannot read the content file: %1").arg(file.errorString())});
        return {};
    }
    const QByteArray bytes = file.readAll();
    file.close();
    return parseTutorialJson(bytes, issues);
}

int countContentErrors(const QList<ContentIssue> &issues)
{
    int n = 0;
    for (const auto &issue : issues)
        if (issue.severity == ContentSeverity::Error) ++n;
    return n;
}

QString formatContentIssues(const QList<ContentIssue> &issues, int maxShown)
{
    QString out;
    const int limit = (maxShown < 0) ? static_cast<int>(issues.size())
                                     : qMin(maxShown, static_cast<int>(issues.size()));
    for (int i = 0; i < limit; ++i) {
        const auto &issue = issues.at(i);
        out += QStringLiteral("%1: %2%3\n")
                   .arg(issue.severity == ContentSeverity::Error ? QStringLiteral("ERROR")
                                                                 : QStringLiteral("WARNING"),
                        issue.path.isEmpty() ? QString() : issue.path + QStringLiteral(": "),
                        issue.message);
    }
    if (limit < issues.size())
        out += QStringLiteral("... and %1 more\n").arg(issues.size() - limit);
    return out;
}

// Local Variables:
// c-basic-offset: 4
// End:
