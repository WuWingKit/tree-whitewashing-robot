#!/usr/bin/env python3
"""Inspect a JLCEDA / EasyEDA Pro `.eprj3` folder project without the editor.

Reads the plain-text record files (`*.esch2`, `*.epcb2`, `*.epan2`) and the
`.eprj3` manifest directly. No third-party package, no GUI, no network.

Usage:
    python eprj3_inspect.py <project-dir-or-dot-eprj3> [--json]
    python eprj3_inspect.py <single.epcb2>

Units: PCB records store lengths in 1 mil (0.0254 mm); schematic records store
them in 0.01 inch (0.254 mm). This script prints mm for both.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

MIL_TO_MM = 0.0254
SCH_UNIT_TO_MM = 0.254


def parse_records(path: Path, problems: list[str] | None = None) -> list[tuple[dict, object, dict]]:
    """Return [(shell, payload, meta)] for every record line.

    A record is `{shell}||{payload}|` terminated by LF. A deleted record carries
    an empty payload (`{shell}|||`). A final line without the closing `|` is
    tolerated with a warning: real projects sometimes end that way and the
    editor still opens them.
    """
    problems = problems if problems is not None else []
    out: list[tuple[dict, object, dict]] = []
    text = path.read_text(encoding="utf-8")
    for lineno, line in enumerate(text.splitlines(), 1):
        line = line.strip()
        if not line:
            continue
        if not line.endswith("|"):
            problems.append(f"{path.name}:{lineno}: record does not end with '|' (truncated tail?)")
        else:
            line = line[:-1]
        if "||" not in line:
            problems.append(f"{path.name}:{lineno}: missing '||' separator, skipped")
            continue
        head, payload = line.split("||", 1)
        try:
            shell = json.loads(head)
        except json.JSONDecodeError as exc:
            problems.append(f"{path.name}:{lineno}: bad shell JSON ({exc}), skipped")
            continue
        if payload in ("", "|"):
            data: object = ""
        else:
            try:
                data = json.loads(payload.rstrip("|"))
            except json.JSONDecodeError as exc:
                problems.append(f"{path.name}:{lineno}: bad payload JSON ({exc}), skipped")
                continue
        out.append((shell, data, {"line": lineno}))
    return out


def documents(records):
    """Split a record list into documents at every DOCHEAD boundary."""
    docs: list[dict] = []
    current: dict | None = None
    for shell, payload, meta in records:
        if shell.get("type") == "DOCHEAD":
            head = payload if isinstance(payload, dict) else {}
            current = {
                "docType": head.get("docType"),
                "uuid": head.get("uuid"),
                "client": head.get("client"),
                "records": [],
            }
            docs.append(current)
            continue
        if current is None:
            # Records before the first DOCHEAD must not happen in real files.
            current = {"docType": None, "uuid": None, "client": None, "records": []}
            docs.append(current)
        current["records"].append((shell, payload, meta))
    return docs


def live(records):
    """Apply the final-consistency rule: keep max ticket per (type, id)."""
    best: dict[tuple, tuple] = {}
    for shell, payload, meta in records:
        key = (shell.get("type"), shell.get("id"))
        ticket = shell.get("ticket") or 0
        if key not in best or ticket >= (best[key][0].get("ticket") or 0):
            best[key] = (shell, payload, meta)
    return [v for v in best.values() if v[1] != ""]


def summarize_pcb(doc) -> dict:
    recs = live(doc["records"])
    counts: dict[str, int] = {}
    for shell, _payload, _meta in recs:
        counts[shell["type"]] = counts.get(shell["type"], 0) + 1

    layers: list[str] = []
    tracks_mm = 0.0
    pads = vias = 0
    components = 0
    nets: set[str] = set()
    outline_pts: list[tuple[float, float]] = []
    rules: list[dict] = []

    for shell, payload, _meta in recs:
        t = shell["type"]
        if not isinstance(payload, dict):
            continue
        if t == "LAYER" and payload.get("layerType") in ("TOP", "BOTTOM", "SIGNAL", "PLANE"):
            # newer files use boolean `use`; older ones use a status bitmask (1 = in use)
            in_use = payload.get("use")
            if in_use is None:
                in_use = int(payload.get("status") or 0) & 1
            if in_use:
                layers.append(f"{payload.get('layerName')}({payload.get('layerType')})")
        elif t == "LINE" and payload.get("netName"):
            dx = (payload.get("endX", 0) - payload.get("startX", 0)) * MIL_TO_MM
            dy = (payload.get("endY", 0) - payload.get("startY", 0)) * MIL_TO_MM
            tracks_mm += (dx * dx + dy * dy) ** 0.5
        elif t == "PAD":
            pads += 1
        elif t == "VIA":
            vias += 1
        elif t == "NET":
            nets.add(str(shell.get("id")))
        elif t == "COMPONENT":
            components += 1
        elif t == "POLY" and payload.get("layerId") == 11:
            path = payload.get("path") or []
            for i in range(0, len(path) - 1, 2):
                if isinstance(path[i], (int, float)) and isinstance(path[i + 1], (int, float)):
                    outline_pts.append((path[i] * MIL_TO_MM, path[i + 1] * MIL_TO_MM))
        elif t == "RULE":
            rid = shell.get("id") or ""
            try:
                key = json.loads(rid)
            except (json.JSONDecodeError, TypeError):
                key = rid
            rules.append(
                {
                    "id": key,
                    "state": payload.get("ruleState"),
                    "keys": sorted((payload.get("ruleContext") or {}).keys()),
                }
            )

    bbox = None
    if outline_pts:
        xs = [p[0] for p in outline_pts]
        ys = [p[1] for p in outline_pts]
        bbox = {
            "x_mm": round(min(xs), 2),
            "y_mm": round(min(ys), 2),
            "w_mm": round(max(xs) - min(xs), 2),
            "h_mm": round(max(ys) - min(ys), 2),
        }

    return {
        "docType": doc["docType"],
        "uuid": doc["uuid"],
        "recordCounts": dict(sorted(counts.items(), key=lambda kv: -kv[1])),
        "copperLayers": layers,
        "boardBox": bbox,
        "components": components,
        "nets": len(nets),
        "pads": pads,
        "vias": vias,
        "trackLength_mm": round(tracks_mm, 1),
        "rules": rules[:40],
    }


def summarize_sch(doc) -> dict:
    recs = live(doc["records"])
    counts: dict[str, int] = {}
    for shell, _payload, _meta in recs:
        counts[shell["type"]] = counts.get(shell["type"], 0) + 1
    designators = []
    for shell, payload, _meta in recs:
        if shell["type"] == "ATTR" and isinstance(payload, dict) and payload.get("key") == "Designator":
            designators.append(str(payload.get("value")))
    return {
        "docType": doc["docType"],
        "uuid": doc["uuid"],
        "recordCounts": dict(sorted(counts.items(), key=lambda kv: -kv[1])),
        "designators": designators,
    }


def inspect(target: Path) -> dict:
    if target.is_dir():
        manifests = sorted(target.glob("*.eprj3"))
        root = target
    else:
        manifests = [target] if target.suffix == ".eprj3" else []
        root = target.parent

    report: dict = {"root": str(root), "manifest": None, "files": []}

    if manifests:
        manifest = json.loads(manifests[0].read_text(encoding="utf-8"))
        report["manifest"] = {
            "name": manifest.get("name"),
            "format": manifest.get("format"),
            "pcb_count": manifest.get("pcb_count"),
            "boards": [b.get("title") for b in (manifest.get("profile", {}).get("boards") or {}).values()],
            "settings": manifest.get("config", {}).get("settings", {}),
        }

    for path in sorted(list(root.rglob("*.esch2")) + list(root.rglob("*.epcb2")) + list(root.rglob("*.epan2"))):
        problems: list[str] = []
        docs = documents(parse_records(path, problems))
        entry = {"path": str(path.relative_to(root)), "documents": [], "problems": problems}
        for doc in docs:
            if doc["docType"] == "PCB":
                entry["documents"].append(summarize_pcb(doc))
            elif doc["docType"] in ("SCH_PAGE",):
                entry["documents"].append(summarize_sch(doc))
            else:
                entry["documents"].append(
                    {
                        "docType": doc["docType"],
                        "uuid": doc["uuid"],
                        "records": len(doc["records"]),
                    }
                )
        report["files"].append(entry)
    return report


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("target", type=Path, help="project folder, .eprj3 file, or one .epcb2/.esch2")
    ap.add_argument("--json", action="store_true", help="emit raw JSON instead of a text report")
    args = ap.parse_args()

    if not args.target.exists():
        print(f"not found: {args.target}", file=sys.stderr)
        return 2

    report = inspect(args.target)
    if args.json:
        print(json.dumps(report, ensure_ascii=False, indent=2))
        return 0

    m = report["manifest"]
    if m:
        print(f"project      : {m['name']} (format={m['format']}, pcbs={m['pcb_count']})")
        if m["boards"]:
            print(f"boards       : {', '.join(str(b) for b in m['boards'])}")
    print(f"root         : {report['root']}")
    for entry in report["files"]:
        print(f"\n== {entry['path']}")
        for problem in entry.get("problems") or []:
            print(f"   WARN {problem}")
        for doc in entry["documents"]:
            kind = doc.get("docType")
            if kind == "PCB":
                print(f"   PCB {doc['uuid']}")
                if doc["copperLayers"]:
                    print(f"     copper layers : {' / '.join(doc['copperLayers'])}")
                if doc["boardBox"]:
                    b = doc["boardBox"]
                    print(f"     board         : {b['w_mm']} x {b['h_mm']} mm at ({b['x_mm']}, {b['y_mm']})")
                print(
                    f"     components={doc['components']} nets={doc['nets']} "
                    f"pads={doc['pads']} vias={doc['vias']} track={doc['trackLength_mm']} mm"
                )
                for rule in doc["rules"]:
                    print(f"     rule {rule['id']} state={rule['state']} ctx={','.join(rule['keys'])}")
            elif kind == "SCH_PAGE":
                refs = [d for d in doc["designators"] if d]
                print(f"   SCH_PAGE {doc['uuid']}  components(refs)={len(refs)}  e.g. {', '.join(refs[:8])}")
            else:
                print(f"   {kind} {doc.get('uuid')} records={doc.get('records')}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
