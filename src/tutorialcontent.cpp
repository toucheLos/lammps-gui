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

#include <QByteArray>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QRegularExpression>
#include <QSet>

#include <cstddef>

namespace {

// ---- enum name tables ----------------------------------------------------
// One table per enum, used in both directions: parsing a content file and
// naming a value in a diagnostic.  Keeping the spelling in exactly one place
// is what stops the file format and the error messages from drifting apart.

struct KindName {
    const char *name;
    StepKind kind;
};
const KindName KIND_NAMES[] = {
    {"SHOW", StepKind::Show},
    {"EXPERIMENT", StepKind::Experiment},
};

struct FigureName {
    const char *name;
    FigureSource source;
};
const FigureName FIGURE_NAMES[] = {
    {"none", FigureSource::None},
    {"snapshot", FigureSource::Snapshot},
};

struct ParamKindName {
    const char *name;
    ParamKind kind;
};
const ParamKindName PARAM_NAMES[] = {
    {"number", ParamKind::Number},
    {"choice", ParamKind::Choice},
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
// minor revision still loads instead of being rejected wholesale.  That is
// only safe because a key that changes *meaning* comes with a version bump.

const QSet<QString> ROOT_KEYS = {
    QStringLiteral("schema_version"), QStringLiteral("id"),
    QStringLiteral("title"),          QStringLiteral("collection"),
    QStringLiteral("tutorial"),       QStringLiteral("requires_packages"),
    QStringLiteral("skeleton_file"),  QStringLiteral("attribution"),
    QStringLiteral("concepts"),       QStringLiteral("acts"),
};
const QSet<QString> ATTRIBUTION_KEYS = {
    QStringLiteral("source"),
    QStringLiteral("license"),
    QStringLiteral("credit"),
};
const QSet<QString> CONCEPT_KEYS = {
    QStringLiteral("id"),
    QStringLiteral("term"),
    QStringLiteral("explain"),
};
const QSet<QString> ACT_KEYS = {
    QStringLiteral("id"),
    QStringLiteral("title"),
    QStringLiteral("steps"),
};
const QSet<QString> STEP_KEYS = {
    QStringLiteral("id"),
    QStringLiteral("kind"),
    QStringLiteral("title"),
    QStringLiteral("teach"),
    QStringLiteral("doc_link"),
    QStringLiteral("figure"),
    QStringLiteral("commands"),
    QStringLiteral("params"),
    QStringLiteral("prediction"),
    QStringLiteral("expect"),
    QStringLiteral("run_after_insert"),
    QStringLiteral("checkpoint"),
};
const QSet<QString> FIGURE_KEYS  = {QStringLiteral("source"), QStringLiteral("caption")};
const QSet<QString> COMMAND_KEYS = {
    QStringLiteral("text"),
    QStringLiteral("explain"),
    QStringLiteral("notes"),
    QStringLiteral("concept"),
};
const QSet<QString> NOTE_KEYS = {
    QStringLiteral("arg"),
    QStringLiteral("note"),
    QStringLiteral("alternatives"),
    QStringLiteral("concept"),
};
const QSet<QString> PARAM_KEYS = {
    QStringLiteral("id"),      QStringLiteral("label"), QStringLiteral("kind"),
    QStringLiteral("command"), QStringLiteral("arg"),   QStringLiteral("min"),
    QStringLiteral("max"),     QStringLiteral("step"),  QStringLiteral("initial"),
    QStringLiteral("choices"), QStringLiteral("unit"),  QStringLiteral("explain"),
};
const QSet<QString> PREDICTION_KEYS = {
    QStringLiteral("question"),
    QStringLiteral("options"),
    QStringLiteral("correct_option"),
};
const QSet<QString> OPTION_KEYS = {QStringLiteral("text"), QStringLiteral("feedback")};

/**
 * @brief Issue collector that knows where in the document it is
 *
 * Threading a path prefix through the parse is what turns "invalid content"
 * into "acts[1].steps[3].params[0].command is missing", which is the whole
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
    /// a logically const operation on a collector passed around as a reference
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

/// read an array of objects, reporting non-objects at their own index
bool readObjectArray(const QJsonObject &obj, const QString &key, const QString &path,
                     const Ctx &ctx, QList<QJsonObject> &out, QStringList &paths)
{
    if (!obj.contains(key)) return false;
    const QJsonValue v = obj.value(key);
    if (!v.isArray()) {
        ctx.error(sub(path, key), QStringLiteral("expected an array"));
        return false;
    }
    const QJsonArray arr  = v.toArray();
    const QString arrpath = sub(path, key);
    for (int i = 0; i < arr.size(); ++i) {
        if (!arr.at(i).isObject()) {
            ctx.error(idx(arrpath, i), QStringLiteral("expected an object"));
            continue;
        }
        out.append(arr.at(i).toObject());
        paths.append(idx(arrpath, i));
    }
    return true;
}

// ---- section parsers -----------------------------------------------------

/// the first word of a command line, used to bind a parameter to a command
QString commandWordOf(const QString &line)
{
    const QString trimmed = line.trimmed();
    const int space       = trimmed.indexOf(QRegularExpression(QStringLiteral("\\s")));
    return space < 0 ? trimmed : trimmed.left(space);
}

CommandLine parseCommand(const QJsonObject &obj, const QString &path, Ctx &ctx,
                         QSet<QString> &usedConcepts)
{
    CommandLine cmd;
    ctx.checkKeys(obj, COMMAND_KEYS, path);

    readString(obj, QStringLiteral("text"), path, ctx, cmd.text, true);
    readString(obj, QStringLiteral("explain"), path, ctx, cmd.explain);
    if (readString(obj, QStringLiteral("concept"), path, ctx, cmd.conceptId))
        usedConcepts.insert(cmd.conceptId);

    if (cmd.text.contains(QLatin1Char('\n')))
        ctx.error(sub(path, QStringLiteral("text")),
                  QStringLiteral("one command per entry: split multi-line blocks so each "
                                 "line can carry its own explanation"));

    QList<QJsonObject> notes;
    QStringList notepaths;
    readObjectArray(obj, QStringLiteral("notes"), path, ctx, notes, notepaths);
    for (int i = 0; i < notes.size(); ++i) {
        const QJsonObject &n = notes.at(i);
        const QString &npath = notepaths.at(i);
        ctx.checkKeys(n, NOTE_KEYS, npath);
        TokenNote note;
        readInt(n, QStringLiteral("arg"), npath, ctx, note.argIndex);
        readString(n, QStringLiteral("note"), npath, ctx, note.note, true);
        readString(n, QStringLiteral("alternatives"), npath, ctx, note.alternatives);
        if (readString(n, QStringLiteral("concept"), npath, ctx, note.conceptId))
            usedConcepts.insert(note.conceptId);
        if (note.argIndex < 0)
            ctx.error(sub(npath, QStringLiteral("arg")),
                      QStringLiteral("argument index must be 0 (the command word) or higher"));
        cmd.notes.append(note);
    }
    return cmd;
}

TutorialParam parseParam(const QJsonObject &obj, const QString &path, Ctx &ctx)
{
    TutorialParam param;
    ctx.checkKeys(obj, PARAM_KEYS, path);

    readString(obj, QStringLiteral("id"), path, ctx, param.id, true);
    readString(obj, QStringLiteral("label"), path, ctx, param.label, true);
    readString(obj, QStringLiteral("command"), path, ctx, param.command, true);
    readString(obj, QStringLiteral("unit"), path, ctx, param.unit);
    readString(obj, QStringLiteral("explain"), path, ctx, param.explain);
    readInt(obj, QStringLiteral("arg"), path, ctx, param.argIndex);

    QString kindstr;
    if (readString(obj, QStringLiteral("kind"), path, ctx, kindstr))
        if (!lookupName(PARAM_NAMES, kindstr, &ParamKindName::kind, param.kind))
            ctx.error(sub(path, QStringLiteral("kind")),
                      QStringLiteral("unknown parameter kind \"%1\"; expected one of: %2")
                          .arg(kindstr, acceptedNames(PARAM_NAMES)));

    if (param.argIndex < 1)
        ctx.error(sub(path, QStringLiteral("arg")),
                  QStringLiteral("a parameter rewrites an argument, so arg must be 1 or higher"));

    switch (param.kind) {
        case ParamKind::Number: {
            const bool hasmin = readDouble(obj, QStringLiteral("min"), path, ctx, param.min);
            const bool hasmax = readDouble(obj, QStringLiteral("max"), path, ctx, param.max);
            readDouble(obj, QStringLiteral("step"), path, ctx, param.step);
            const bool hasinit =
                readDouble(obj, QStringLiteral("initial"), path, ctx, param.initial);
            if (!hasmin || !hasmax) {
                ctx.error(path, QStringLiteral("a number parameter needs both \"min\" and "
                                               "\"max\""));
            } else if (param.min >= param.max) {
                ctx.error(sub(path, QStringLiteral("min")),
                          QStringLiteral("min (%1) must be below max (%2)")
                              .arg(param.min)
                              .arg(param.max));
            } else if (hasinit && (param.initial < param.min || param.initial > param.max)) {
                ctx.error(sub(path, QStringLiteral("initial")),
                          QStringLiteral("initial (%1) lies outside [%2, %3]")
                              .arg(param.initial)
                              .arg(param.min)
                              .arg(param.max));
            }
            if (param.step <= 0.0) param.step = (param.max - param.min) / 100.0;
            if (!hasinit) param.initial = param.min;
            break;
        }
        case ParamKind::Choice:
            readStringList(obj, QStringLiteral("choices"), path, ctx, param.choices);
            readString(obj, QStringLiteral("initial"), path, ctx, param.initialChoice);
            if (param.choices.size() < 2)
                ctx.error(sub(path, QStringLiteral("choices")),
                          QStringLiteral("a choice parameter needs at least two options"));
            else if (param.initialChoice.isEmpty())
                param.initialChoice = param.choices.first();
            else if (!param.choices.contains(param.initialChoice))
                ctx.error(sub(path, QStringLiteral("initial")),
                          QStringLiteral("\"%1\" is not one of the offered choices")
                              .arg(param.initialChoice));
            break;
    }
    return param;
}

TutorialPrediction parsePrediction(const QJsonObject &obj, const QString &path, Ctx &ctx)
{
    TutorialPrediction pred;
    pred.present = true;
    ctx.checkKeys(obj, PREDICTION_KEYS, path);

    readString(obj, QStringLiteral("question"), path, ctx, pred.question, true);
    pred.correctOption = -1;
    readInt(obj, QStringLiteral("correct_option"), path, ctx, pred.correctOption);

    QList<QJsonObject> options;
    QStringList optpaths;
    readObjectArray(obj, QStringLiteral("options"), path, ctx, options, optpaths);
    for (int i = 0; i < options.size(); ++i) {
        ctx.checkKeys(options.at(i), OPTION_KEYS, optpaths.at(i));
        TutorialOption opt;
        readString(options.at(i), QStringLiteral("text"), optpaths.at(i), ctx, opt.text, true);
        readString(options.at(i), QStringLiteral("feedback"), optpaths.at(i), ctx, opt.feedback);
        if (opt.feedback.isEmpty())
            ctx.warn(optpaths.at(i), QStringLiteral("this option teaches nothing; every answer "
                                                    "should explain itself"));
        pred.options.append(opt);
    }

    if (pred.options.size() < 2)
        ctx.error(sub(path, QStringLiteral("options")),
                  QStringLiteral("a prediction needs at least two options"));
    else if (pred.correctOption < 0 || pred.correctOption >= pred.options.size())
        ctx.error(sub(path, QStringLiteral("correct_option")),
                  QStringLiteral("correct_option %1 is out of range for %2 options")
                      .arg(pred.correctOption)
                      .arg(pred.options.size()));
    return pred;
}

TutorialStep parseStep(const QJsonObject &obj, const QString &path, Ctx &ctx,
                       QSet<QString> &usedConcepts)
{
    TutorialStep step;
    ctx.checkKeys(obj, STEP_KEYS, path);

    readString(obj, QStringLiteral("id"), path, ctx, step.id, true);
    readString(obj, QStringLiteral("title"), path, ctx, step.title, true);
    readString(obj, QStringLiteral("teach"), path, ctx, step.teach);
    readString(obj, QStringLiteral("expect"), path, ctx, step.expect);
    readBool(obj, QStringLiteral("checkpoint"), path, ctx, step.checkpoint);
    readBool(obj, QStringLiteral("run_after_insert"), path, ctx, step.runAfterInsert);

    QString kindstr;
    if (readString(obj, QStringLiteral("kind"), path, ctx, kindstr, true))
        if (!lookupName(KIND_NAMES, kindstr, &KindName::kind, step.kind))
            ctx.error(sub(path, QStringLiteral("kind")),
                      QStringLiteral("unknown step kind \"%1\"; expected one of: %2")
                          .arg(kindstr, acceptedNames(KIND_NAMES)));

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

    QJsonObject figobj;
    if (readObject(obj, QStringLiteral("figure"), path, ctx, figobj)) {
        const QString figpath = sub(path, QStringLiteral("figure"));
        ctx.checkKeys(figobj, FIGURE_KEYS, figpath);
        QString src;
        if (readString(figobj, QStringLiteral("source"), figpath, ctx, src, true))
            if (!lookupName(FIGURE_NAMES, src, &FigureName::source, step.figure))
                ctx.error(sub(figpath, QStringLiteral("source")),
                          QStringLiteral("unknown figure source \"%1\"; expected one of: %2")
                              .arg(src, acceptedNames(FIGURE_NAMES)));
        readString(figobj, QStringLiteral("caption"), figpath, ctx, step.figureCaption);
    }

    QList<QJsonObject> cmds;
    QStringList cmdpaths;
    readObjectArray(obj, QStringLiteral("commands"), path, ctx, cmds, cmdpaths);
    for (int i = 0; i < cmds.size(); ++i)
        step.commands.append(parseCommand(cmds.at(i), cmdpaths.at(i), ctx, usedConcepts));

    QList<QJsonObject> params;
    QStringList parampaths;
    readObjectArray(obj, QStringLiteral("params"), path, ctx, params, parampaths);
    for (int i = 0; i < params.size(); ++i)
        step.params.append(parseParam(params.at(i), parampaths.at(i), ctx));

    QJsonObject predobj;
    if (readObject(obj, QStringLiteral("prediction"), path, ctx, predobj))
        step.prediction = parsePrediction(predobj, sub(path, QStringLiteral("prediction")), ctx);

    // ---- cross-checks the schema alone cannot express ----
    switch (step.kind) {
        case StepKind::Show:
            if (step.commands.isEmpty() && step.teach.isEmpty())
                ctx.error(path, QStringLiteral("a SHOW step needs either commands to show or "
                                               "teach text; this one presents nothing"));
            if (!step.params.isEmpty())
                ctx.warn(sub(path, QStringLiteral("params")),
                         QStringLiteral("parameters belong to an EXPERIMENT step and are "
                                        "ignored here"));
            if (step.prediction.present)
                ctx.warn(sub(path, QStringLiteral("prediction")),
                         QStringLiteral("a prediction belongs immediately before a run, so it "
                                        "is ignored on a SHOW step"));
            break;

        case StepKind::Experiment:
            if (step.params.isEmpty())
                ctx.error(sub(path, QStringLiteral("params")),
                          QStringLiteral("an EXPERIMENT step needs at least one parameter to "
                                         "change; otherwise it is just a run"));
            if (step.expect.isEmpty())
                ctx.warn(sub(path, QStringLiteral("expect")),
                         QStringLiteral("no \"expect\" text; the user is told to run but not "
                                        "what to look for"));
            break;
    }

    if (step.teach.isEmpty())
        ctx.warn(sub(path, QStringLiteral("teach")),
                 QStringLiteral("no teach text; a step that explains nothing is busywork"));

    return step;
}

TutorialAct parseAct(const QJsonObject &obj, const QString &path, Ctx &ctx,
                     QSet<QString> &usedConcepts)
{
    TutorialAct act;
    ctx.checkKeys(obj, ACT_KEYS, path);
    readString(obj, QStringLiteral("id"), path, ctx, act.id, true);
    readString(obj, QStringLiteral("title"), path, ctx, act.title, true);

    QList<QJsonObject> steps;
    QStringList steppaths;
    if (!readObjectArray(obj, QStringLiteral("steps"), path, ctx, steps, steppaths)) {
        ctx.error(sub(path, QStringLiteral("steps")), QStringLiteral("expected an array of steps"));
        return act;
    }
    if (steps.isEmpty())
        ctx.error(sub(path, QStringLiteral("steps")),
                  QStringLiteral("an act needs at least one step"));

    for (int i = 0; i < steps.size(); ++i)
        act.steps.append(parseStep(steps.at(i), steppaths.at(i), ctx, usedConcepts));
    return act;
}

} // namespace

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

const TutorialConcept *TutorialContent::conceptFor(const QString &id) const
{
    const auto it = conceptmap.constFind(id);
    return it == conceptmap.constEnd() ? nullptr : &it.value();
}

/* -------------------------------------------------------------------- */

QString stepKindName(StepKind kind)
{
    for (const auto &entry : KIND_NAMES)
        if (entry.kind == kind) return QLatin1String(entry.name);
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
    // version 1 used a different step model (seven verbs, validators, holes);
    // reading it as version 2 would silently misinterpret every step
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

    // ---- concepts ----
    QList<QJsonObject> concepts;
    QStringList conceptpaths;
    readObjectArray(root, QStringLiteral("concepts"), QString(), ctx, concepts, conceptpaths);
    for (int i = 0; i < concepts.size(); ++i) {
        ctx.checkKeys(concepts.at(i), CONCEPT_KEYS, conceptpaths.at(i));
        TutorialConcept c;
        readString(concepts.at(i), QStringLiteral("id"), conceptpaths.at(i), ctx, c.id, true);
        readString(concepts.at(i), QStringLiteral("term"), conceptpaths.at(i), ctx, c.term, true);
        readString(concepts.at(i), QStringLiteral("explain"), conceptpaths.at(i), ctx, c.explain,
                   true);
        if (c.id.isEmpty()) continue;
        if (out.conceptmap.contains(c.id))
            ctx.error(sub(conceptpaths.at(i), QStringLiteral("id")),
                      QStringLiteral("duplicate concept id \"%1\"").arg(c.id));
        out.conceptmap.insert(c.id, c);
    }

    // ---- acts ----
    QSet<QString> usedConcepts;
    QList<QJsonObject> acts;
    QStringList actpaths;
    if (!readObjectArray(root, QStringLiteral("acts"), QString(), ctx, acts, actpaths)) {
        ctx.error(QStringLiteral("acts"), QStringLiteral("expected an array of acts"));
        return out;
    }
    if (acts.isEmpty())
        ctx.error(QStringLiteral("acts"), QStringLiteral("a tutorial needs at least one act"));
    for (int i = 0; i < acts.size(); ++i)
        out.actlist.append(parseAct(acts.at(i), actpaths.at(i), ctx, usedConcepts));

    // an annotation pointing at a concept that was never declared would simply
    // show nothing, which is the kind of silent gap review does not catch
    for (const auto &id : usedConcepts)
        if (!id.isEmpty() && !out.conceptmap.contains(id))
            ctx.error(
                QStringLiteral("concepts"),
                QStringLiteral("an annotation references the undeclared concept \"%1\"").arg(id));
    for (auto it = out.conceptmap.constBegin(); it != out.conceptmap.constEnd(); ++it)
        if (!usedConcepts.contains(it.key()))
            ctx.warn(
                QStringLiteral("concepts"),
                QStringLiteral("concept \"%1\" is declared but never referenced").arg(it.key()));

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

    // a parameter must rewrite a command the tutorial has actually shown by
    // then, or the run silently changes nothing
    QSet<QString> shownCommands;
    for (const auto &act : out.actlist) {
        for (const auto &step : act.steps) {
            for (const auto &cmd : step.commands)
                shownCommands.insert(commandWordOf(cmd.text));
            for (const auto &param : step.params)
                if (!param.command.isEmpty() && !shownCommands.contains(param.command))
                    ctx.error(QStringLiteral("acts"),
                              QStringLiteral("step \"%1\" binds a parameter to \"%2\", which no "
                                             "earlier step puts in the script")
                                  .arg(step.id, param.command));
        }
    }

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
