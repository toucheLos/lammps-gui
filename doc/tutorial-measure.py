#!/usr/bin/env python3
"""Measure how much of a tutorial asks the user to do something.

Reports the longest run of consecutive steps whose only action is pressing
Next, and whether any act ends on one.  See doc/tutorial-authoring.md
section 1 for the budget and why it exists.

    python3 doc/tutorial-measure.py resources/tutorials/*.json
"""
import json
import sys


def passive(step):
    """True when the step's only action is pressing Next."""
    if step.get("predict") or step.get("tune"):
        return False
    if step["kind"] == "SHOW" and any(c.get("typed") for c in step.get("commands", [])):
        return False
    if step.get("anchor") in ("run", "snapshot", "chart", "image", "log"):
        return False
    return True


def main(paths):
    worst = 0
    for path in paths:
        content = json.load(open(path))
        acts = content["acts"]
        run = best = 0
        where = ""
        for act in acts:
            for step in act["steps"]:
                if passive(step):
                    run += 1
                    if run > best:
                        best, where = run, step["id"]
                else:
                    run = 0
        # the final step of a tutorial is "go and experiment", which is terminal
        tail = [a["id"] for a in acts[:-1] if passive(a["steps"][-1])]
        total = sum(len(a["steps"]) for a in acts)
        active = sum(1 for a in acts for s in a["steps"] if not passive(s))
        print(f"{path.split('/')[-1]:26} steps={total:3} active={active:3} "
              f"longest-passive-run={best} ({where or '-'}) "
              f"acts ending passive: {tail or 'none'}")
        worst = max(worst, best)
    return 1 if worst > 4 else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
