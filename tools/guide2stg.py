#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""docs/GUIDE.md -> ST-Guide source (ECAL.STG), for hcp to compile into
ECAL.HYP, the help file ST-Guide and HypView show on the Atari.

    tools/guide2stg.py docs/GUIDE.md build/ECAL.STG VERSION

Each "## " section becomes a page, listed on the first one; links to
other sections (#anchors) become hypertext links. ST-Guide shows text as
it is, so paragraphs are wrapped here, to 72 columns. Written as UTF-8
with only what the Atari character set has; hcp converts it.
"""
import re
import sys
import textwrap

WIDTH = 72

# what the Atari character set has, or plain ASCII for what it hasn't
CHARS = {
    "\u2190": "<-", "\u2192": "->", "\u2191": "Up", "\u2193": "Down",
    "\u2039": "<", "\u203a": ">", "\u2018": "'", "\u2019": "'",
    "\u201c": '"', "\u201d": '"', "\u2013": "-", "\u2014": "-",
    "\u2026": "...", "\u00a0": " ",
}
# kept as they are: the Atari character set has them (hcp converts)
KEEP = "\u2261\u00e9\u00bd\u00e0\u00e8\u00fc\u00f6\u00e4"


def atari(s):
    out = []
    for c in s:
        if c in CHARS:
            out.append(CHARS[c])
        elif ord(c) < 128 or c in KEEP:
            out.append(c)
        else:
            out.append("?")
    return "".join(out)


def anchor(title):
    """GitHub's anchor for a heading"""
    a = title.strip().lower()
    a = re.sub(r"[^\w\- ]", "", a)
    return a.replace(" ", "-")


def inline(s, nodes):
    """markdown inline -> ST-Guide: links, bold, code, images"""
    s = re.sub(r"!\[[^\]]*\]\([^)]*\)", "", s)            # pictures: not here

    def link(m):
        text, target = m.group(1), m.group(2)
        if target.startswith("#") and target[1:] in nodes:
            # spaces in a link are \x01 until finish(): wrap() keeps it whole
            return ("\x00L%s\x00%s\x00" % (text, nodes[target[1:]])).replace(" ", "\x01")
        if target.startswith("http"):
            return "%s (%s)" % (text, target)
        return text
    s = re.sub(r"\[([^\]]+)\]\(([^)]+)\)", link, s)
    s = re.sub(r"`([^`]*)`", r"\1", s)
    s = s.replace("@", "@@")
    s = re.sub(r"\*\*([^*]+)\*\*", "\x00B\\1\x00b", s)
    return s


def finish(s):
    """the markers left by inline() -> ST-Guide commands, after wrapping"""
    s = re.sub("\x00L([^\x00]*)\x00([^\x00]*)\x00", r'@{"\1" LINK "\2"}', s)
    return s.replace("\x00B", "@{B}").replace("\x00b", "@{b}").replace("\x01", " ")


def wrap(text, indent="", hang=""):
    # the markers count as nothing: wrap on the visible width, words with
    # a marker inside are kept whole
    vis = re.sub("\x00L([^\x00]*)\x00[^\x00]*\x00", r"\1", text).replace("\x00B", "").replace("\x00b", "")
    if len(vis) + len(indent) <= WIDTH:
        return [indent + text]
    words = text.split(" ")
    lines, cur, curv = [], indent, len(indent)
    for w in words:
        wv = len(re.sub("\x00L([^\x00]*)\x00[^\x00]*\x00", r"\1", w).replace("\x00B", "").replace("\x00b", ""))
        if curv + wv + (0 if cur.strip() == "" else 1) > WIDTH and cur.strip():
            lines.append(cur)
            cur, curv = hang + w, len(hang) + wv
        else:
            if cur.strip():
                cur += " "
                curv += 1
            cur += w
            curv += wv
    if cur.strip():
        lines.append(cur)
    return lines


def table(rows):
    cells = [[re.sub(r"[`*]", "", c.strip()) for c in r.strip().strip("|").split("|")] for r in rows]
    cells = [r for r in cells if not all(re.fullmatch(r"-+", c) for c in r)]
    n = max(len(r) for r in cells)
    widths = [max(len(r[i]) if i < len(r) else 0 for r in cells) for i in range(n)]
    out = []
    for k, r in enumerate(cells):
        out.append("  " + "  ".join((r[i] if i < len(r) else "").ljust(widths[i]) for i in range(n)).rstrip())
        if k == 0:
            out.append("  " + "  ".join("-" * w for w in widths))
    return out


def convert(md, version):
    lines = atari(md).split("\n")
    title = lines[0].lstrip("# ").strip()
    sections, cur = [], None
    for ln in lines[1:]:
        if ln.startswith("## "):
            cur = [ln[3:].strip(), []]
            sections.append(cur)
        elif cur:
            cur[1].append(ln)
    nodes = {anchor(t): t for t, _ in sections}
    out = ["@inputenc utf8",
           "@charset atarist",
           '@database "ECal - user guide"',
           '@subject "Personal/Calendar"',
           '@author "AERA | Erez Yaary"',
           "@$VER: ECAL.HYP %s" % version,
           "@options +z",
           "@width %d" % (WIDTH + 2),
           "",
           '@node Main "%s"' % title,
           "@{U}%s@{u}" % title,
           "",
           "ECal %s, a calendar for the Atari ST, TT and Falcon." % version,
           ""]
    for t, _ in sections:
        out.append('  @{"%s" LINK "%s"}' % (t, t))
    out += ["", "@endnode", ""]
    for t, body in sections:
        out += ['@node "%s"' % t, "@{U}%s@{u}" % t, ""]
        para, tbl, code, code_ind = [], [], False, 0

        def flush_para():
            if para:
                text = " ".join(p.strip() for p in para)
                lead = para[0]
                m = re.match(r"(\s*)([-*]|\d+\.) (.*)", lead)
                if m:
                    ind = " " * (len(m.group(1)) + 2)
                    text = " ".join([m.group(3).strip()] + [p.strip() for p in para[1:]])
                    bullet = "-" if m.group(2) in "-*" else m.group(2)
                    out.extend(finish(x) for x in wrap(inline(text, nodes), ind[:-2] + bullet + " ", ind + " " * (len(bullet) - 1)))
                else:
                    # a paragraph inside a list item keeps the item's indent
                    ind = " " * (len(lead) - len(lead.lstrip()))
                    out.extend(finish(x) for x in wrap(inline(text, nodes), ind, ind))
                out.append("")
                para.clear()

        for ln in body:
            if ln.lstrip().startswith("```"):
                flush_para()
                code = not code
                code_ind = len(ln) - len(ln.lstrip())   # inside a list item: indented as it is
                if not code:
                    out.append("")
                continue
            if code:
                out.append(" " * (code_ind + 2) + ln[code_ind:].replace("@", "@@"))
                continue
            if ln.startswith("|"):
                flush_para()
                tbl.append(ln)
                continue
            if tbl:
                out.extend(x.replace("@", "@@") for x in table(tbl))
                out.append("")
                tbl = []
            if not ln.strip() or re.match(r"!\[", ln.strip()):
                flush_para()
                continue
            # a new bullet starts a new paragraph
            if re.match(r"\s*([-*]|\d+\.) ", ln) and para:
                flush_para()
            para.append(ln)
        flush_para()
        if tbl:
            out.extend(x.replace("@", "@@") for x in table(tbl))
        while out and out[-1] == "":
            out.pop()
        out += ["", "@endnode", ""]
    return "\r\n".join(out) + "\r\n"


def main():
    md = open(sys.argv[1], encoding="utf-8").read()
    version = open(sys.argv[3]).read().strip() if len(sys.argv) > 3 else ""
    with open(sys.argv[2], "w", encoding="utf-8", newline="") as f:
        f.write(convert(md, version))


if __name__ == "__main__":
    main()
