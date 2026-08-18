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
    {"OBSERVE", StepKind::Observe},
};

struct AnchorName {
    const char *name;
    StepAnchor anchor;
};
const AnchorName ANCHOR_NAMES[] = {
    {"none", StepAnchor::None},           {"editor", StepAnchor::Editor},
    {"editor_all", StepAnchor::EditorAll}, {"run", StepAnchor::Run},
    {"snapshot", StepAnchor::Snapshot},   {"chart", StepAnchor::Chart},
    {"image", StepAnchor::Image},         {"log", StepAnchor::Log},
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
    QStringLiteral("skeleton"),
    QStringLiteral("attribution"),    QStringLiteral("concepts"),
    QStringLiteral("acts"),
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
    QStringLiteral("id"),         QStringLiteral("kind"),     QStringLiteral("title"),
    QStringLiteral("teach"),      QStringLiteral("doc_link"), QStringLiteral("commands"),
    QStringLiteral("section"),    QStringLiteral("expect"),   QStringLiteral("run_after_insert"),
    QStringLiteral("checkpoint"), QStringLiteral("anchor"),   QStringLiteral("call_to_action"),
    QStringLiteral("open_file"),  QStringLiteral("tune"),
    QStringLiteral("wait_after_run"), QStringLiteral("before"),
    QStringLiteral("highlight"), QStringLiteral("predict"),
};
const QSet<QString> TUNE_KEYS = {
    QStringLiteral("command"), QStringLiteral("arg"), QStringLiteral("from"),
    QStringLiteral("to"),      QStringLiteral("min"), QStringLiteral("max"),
    QStringLiteral("decimals"), QStringLiteral("label"),
};
const QSet<QString> COMMAND_KEYS = {
    QStringLiteral("text"),    QStringLiteral("explain"), QStringLiteral("notes"),
    QStringLiteral("concept"), QStringLiteral("typed"),  QStringLiteral("together"),
    QStringLiteral("replaces"), QStringLiteral("hint"),
};
const QSet<QString> NOTE_KEYS = {
    QStringLiteral("arg"),
    QStringLiteral("note"),
    QStringLiteral("alternatives"),
    QStringLiteral("concept"),
};

/**
 * @brief Issue collector that knows where in the document it is
 *
 * Threading a path prefix through the parse is what turns "invalid content"
 * into "acts[1].steps[3].commands[0].text is not a string", which is the whole
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

CommandLine parseCommand(const QJsonObject &obj, const QString &path, Ctx &ctx,
                         QSet<QString> &usedConcepts)
{
    CommandLine cmd;
    ctx.checkKeys(obj, COMMAND_KEYS, path);

    readString(obj, QStringLiteral("text"), path, ctx, cmd.text, true);
    readString(obj, QStringLiteral("explain"), path, ctx, cmd.explain);
    readBool(obj, QStringLiteral("typed"), path, ctx, cmd.typed);
    readBool(obj, QStringLiteral("together"), path, ctx, cmd.together);
    readString(obj, QStringLiteral("replaces"), path, ctx, cmd.replaces);
    readString(obj, QStringLiteral("hint"), path, ctx, cmd.hint);
    if (readString(obj, QStringLiteral("concept"), path, ctx, cmd.conceptId))
        usedConcepts.insert(cmd.conceptId);

    // a typed line is described rather than shown, so the description is the
    // only thing the user has to go on
    if (cmd.typed && cmd.explain.isEmpty())
        ctx.error(sub(path, QStringLiteral("explain")),
                  QStringLiteral("a typed command is never shown, so it needs an explanation "
                                 "the user can work from"));
    if (cmd.typed && cmd.hint.isEmpty())
        ctx.error(sub(path, QStringLiteral("hint")),
                  QStringLiteral("a typed command needs a hint: a drill with nothing to fall "
                                 "back on is a memory test rather than reinforcement"));

    // One command per entry -- but a LAMMPS command may legitimately span
    // several lines with a trailing "&", and splitting those would put half a
    // command on the screen with nothing sensible to say about it.  A newline
    // is allowed exactly where a continuation marker puts one.
    if (cmd.text.contains(QLatin1Char('\n'))) {
        const QStringList lines = cmd.text.split(QLatin1Char('\n'));
        for (int i = 0; i + 1 < lines.size(); ++i)
            if (!lines.at(i).trimmed().endsWith(QLatin1Char('&'))) {
                ctx.error(sub(path, QStringLiteral("text")),
                          QStringLiteral("one command per entry: split multi-line blocks so each "
                                         "line can carry its own explanation, unless the line "
                                         "ends with \"&\" to continue the command"));
                break;
            }
    }

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

TutorialStep parseStep(const QJsonObject &obj, const QString &path, Ctx &ctx,
                       QSet<QString> &usedConcepts)
{
    TutorialStep step;
    ctx.checkKeys(obj, STEP_KEYS, path);

    readString(obj, QStringLiteral("id"), path, ctx, step.id, true);
    readString(obj, QStringLiteral("title"), path, ctx, step.title, true);
    readString(obj, QStringLiteral("teach"), path, ctx, step.teach);
    readString(obj, QStringLiteral("expect"), path, ctx, step.expect);
    readString(obj, QStringLiteral("predict"), path, ctx, step.predict);
    // a question with no answer is worse than neither: the user commits to a
    // guess and is never told whether it was right
    if (!step.predict.isEmpty() && step.expect.isEmpty())
        ctx.error(sub(path, QStringLiteral("expect")),
                  QStringLiteral("a step that asks the user to predict has to say what actually "
                                 "happens, or the prediction is never resolved"));
    readBool(obj, QStringLiteral("checkpoint"), path, ctx, step.checkpoint);
    readBool(obj, QStringLiteral("wait_after_run"), path, ctx, step.waitAfterRun);
    readBool(obj, QStringLiteral("run_after_insert"), path, ctx, step.runAfterInsert);
    readString(obj, QStringLiteral("call_to_action"), path, ctx, step.callToAction);
    readString(obj, QStringLiteral("open_file"), path, ctx, step.openFile);
    readString(obj, QStringLiteral("section"), path, ctx, step.section);
    readString(obj, QStringLiteral("before"), path, ctx, step.before);
    readString(obj, QStringLiteral("highlight"), path, ctx, step.highlight);
    // two answers to the same question: where do this step's commands go?
    if (!step.section.isEmpty() && !step.before.isEmpty())
        ctx.error(sub(path, QStringLiteral("before")),
                  QStringLiteral("a step files its commands under a heading or above a named "
                                 "line, not both"));

    QString anchorstr;
    if (readString(obj, QStringLiteral("anchor"), path, ctx, anchorstr))
        if (!lookupName(ANCHOR_NAMES, anchorstr, &AnchorName::anchor, step.anchor))
            ctx.error(sub(path, QStringLiteral("anchor")),
                      QStringLiteral("unknown anchor \"%1\"; expected one of: %2")
                          .arg(anchorstr, acceptedNames(ANCHOR_NAMES)));

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

    QJsonObject tuneobj;
    if (readObject(obj, QStringLiteral("tune"), path, ctx, tuneobj)) {
        const QString tpath = sub(path, QStringLiteral("tune"));
        ctx.checkKeys(tuneobj, TUNE_KEYS, tpath);
        readString(tuneobj, QStringLiteral("command"), tpath, ctx, step.tune.command, true);
        readInt(tuneobj, QStringLiteral("arg"), tpath, ctx, step.tune.argIndex);
        readDouble(tuneobj, QStringLiteral("from"), tpath, ctx, step.tune.from);
        readDouble(tuneobj, QStringLiteral("to"), tpath, ctx, step.tune.to);
        readInt(tuneobj, QStringLiteral("decimals"), tpath, ctx, step.tune.decimals);
        readString(tuneobj, QStringLiteral("label"), tpath, ctx, step.tune.label);
        // the range brackets the two named values unless the content widens it,
        // so a control is never born pointing outside its own bounds
        step.tune.min = qMin(step.tune.from, step.tune.to);
        step.tune.max = qMax(step.tune.from, step.tune.to);
        readDouble(tuneobj, QStringLiteral("min"), tpath, ctx, step.tune.min);
        readDouble(tuneobj, QStringLiteral("max"), tpath, ctx, step.tune.max);

        if (step.tune.argIndex < 1)
            ctx.error(sub(tpath, QStringLiteral("arg")),
                      QStringLiteral("argument 0 is the command word itself; the first argument "
                                     "that can be edited is 1"));
        if (step.tune.min > step.tune.max)
            ctx.error(sub(tpath, QStringLiteral("min")),
                      QStringLiteral("the lower bound is above the upper bound"));
        if (step.tune.from < step.tune.min || step.tune.from > step.tune.max ||
            step.tune.to < step.tune.min || step.tune.to > step.tune.max)
            ctx.error(sub(tpath, QStringLiteral("from")),
                      QStringLiteral("\"from\" and \"to\" have to lie inside the range the user "
                                     "is given, or the control cannot reach them"));
    }

    QList<QJsonObject> cmds;
    QStringList cmdpaths;
    readObjectArray(obj, QStringLiteral("commands"), path, ctx, cmds, cmdpaths);
    for (int i = 0; i < cmds.size(); ++i)
        step.commands.append(parseCommand(cmds.at(i), cmdpaths.at(i), ctx, usedConcepts));

    // ---- cross-checks the schema alone cannot express ----
    switch (step.kind) {
        case StepKind::Show:
            if (!step.commands.isEmpty() && step.commands.first().together)
                ctx.error(sub(path, QStringLiteral("commands")),
                          QStringLiteral("the first command of a step cannot be \"together\": "
                                         "there is nothing before it to group with"));
            if (step.commands.isEmpty())
                ctx.error(sub(path, QStringLiteral("commands")),
                          QStringLiteral("a SHOW step exists to offer commands; use OBSERVE "
                                         "for a step that only explains something"));
            // a typed line is offered blank for the user to fill in, which is a
            // single act; it cannot also be pasted as part of a group
            for (int i = 0; i < step.commands.size(); ++i) {
                const auto &cmd = step.commands.at(i);
                if (cmd.typed && cmd.together)
                    ctx.error(sub(cmdpaths.at(i), QStringLiteral("typed")),
                              QStringLiteral("a typed command cannot also be \"together\": the "
                                             "group is pasted in one action, so there would be "
                                             "nothing left for the user to type"));
                // the user types one line, but the whole group is consumed by
                // that single act -- anything travelling with a typed command
                // would be recorded as written without ever being written
                if (cmd.typed && i + 1 < step.commands.size() &&
                    step.commands.at(i + 1).together)
                    ctx.error(sub(cmdpaths.at(i), QStringLiteral("typed")),
                              QStringLiteral("a typed command has to stand alone: the command "
                                             "after it is \"together\", and the user types only "
                                             "one line"));
                if (!cmd.replaces.isEmpty() && cmd.replaces == cmd.text)
                    ctx.error(sub(cmdpaths.at(i), QStringLiteral("replaces")),
                              QStringLiteral("a command cannot replace itself"));
            }
            break;

        case StepKind::Observe:
            if (!step.commands.isEmpty())
                ctx.error(sub(path, QStringLiteral("commands")),
                          QStringLiteral("an OBSERVE step points at something rather than "
                                         "writing to the script; use SHOW to offer commands"));
            if (step.anchor == StepAnchor::None)
                ctx.error(sub(path, QStringLiteral("anchor")),
                          QStringLiteral("an OBSERVE step needs an anchor; without one it "
                                         "points at nothing"));
            if (!step.highlight.isEmpty() && step.anchor != StepAnchor::Editor)
                ctx.error(sub(path, QStringLiteral("highlight")),
                          QStringLiteral("a highlighted line is a line of the script, so the "
                                         "step has to be anchored to the editor"));
            if (step.waitAfterRun && step.anchor != StepAnchor::Run)
                ctx.error(sub(path, QStringLiteral("wait_after_run")),
                          QStringLiteral("only a step anchored to the Run button waits on a "
                                         "run; elsewhere there is no run to wait for"));
            if (step.callToAction.isEmpty() && step.anchor == StepAnchor::Run)
                ctx.warn(sub(path, QStringLiteral("call_to_action")),
                         QStringLiteral("a step pointing at the Run button should say to "
                                        "press it"));
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
    readInt(root, QStringLiteral("tutorial"), QString(), ctx, out.tutno);
    readStringList(root, QStringLiteral("requires_packages"), QString(), ctx, out.packages);
    readStringList(root, QStringLiteral("skeleton"), QString(), ctx, out.skeletonlines);

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

    // a step filing commands under a heading the skeleton never declares would
    // silently append them at the end instead
    for (const auto &act : out.actlist)
        for (const auto &step : act.steps)
            if (!step.section.isEmpty() && !out.skeletonlines.contains(step.section))
                ctx.error(QStringLiteral("acts"),
                          QStringLiteral("step \"%1\" files its commands under \"%2\", which "
                                         "is not one of the skeleton's headings")
                              .arg(step.id, step.section));

    // a "replaces" that names nothing is not an error the user would ever see:
    // the removal silently does nothing and the script quietly keeps both lines,
    // which is exactly the state the field exists to prevent
    {
        // Lines a step names with "before" belong to a script that arrived
        // complete, so they are in the file without the tour ever writing
        // them.  A "replaces" may legitimately name one of those -- Tutorial 2
        // replaces the `run 0 post no` its input file came with -- while a
        // typo still matches nothing and is still caught.
        QSet<QString> fromFile;
        for (const auto &act : out.actlist)
            for (const auto &step : act.steps)
                if (!step.before.isEmpty()) fromFile.insert(step.before);

        QSet<QString> writtenSoFar;
        QSet<QString> wordsSoFar;
        for (const auto &act : out.actlist)
            for (const auto &step : act.steps) {
                // a tune control edits a line that has to already be there
                if (step.tune.isValid() && !wordsSoFar.contains(step.tune.command))
                    ctx.error(QStringLiteral("acts"),
                              QStringLiteral("step \"%1\" tunes \"%2\", which no earlier step "
                                             "puts in the script")
                                  .arg(step.id, step.tune.command));
                for (const auto &cmd : step.commands) {
                    if (!cmd.replaces.isEmpty() && !writtenSoFar.contains(cmd.replaces) &&
                        !fromFile.contains(cmd.replaces))
                        ctx.error(QStringLiteral("acts"),
                                  QStringLiteral("step \"%1\" replaces \"%2\", which no earlier "
                                                 "command writes and no step names with "
                                                 "\"before\"")
                                      .arg(step.id, cmd.replaces));
                    writtenSoFar.insert(cmd.text);
                    wordsSoFar.insert(cmd.text.section(QLatin1Char(' '), 0, 0));
                }
            }
    }

    // A step that hands the user the Run button has to come after something has
    // been put in the script, or they would be running an empty buffer -- unless
    // the tutorial's script arrives complete, in which case running it before
    // touching it is the whole point of the style.  A step naming a line with
    // "before", or ringing one with "highlight", is the signal that it does.
    bool scriptArrivesComplete = false;
    for (const auto &act : out.actlist)
        for (const auto &step : act.steps)
            if (!step.before.isEmpty() || !step.highlight.isEmpty()) scriptArrivesComplete = true;

    int commandsSoFar = scriptArrivesComplete ? 1 : 0;
    for (const auto &act : out.actlist) {
        for (const auto &step : act.steps) {
            if (step.anchor == StepAnchor::Run && commandsSoFar == 0)
                ctx.error(QStringLiteral("acts"),
                          QStringLiteral("step \"%1\" points at the Run button, but no earlier "
                                         "step has put anything in the script")
                              .arg(step.id));
            commandsSoFar += static_cast<int>(step.commands.size());
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
