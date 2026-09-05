# -*- coding: utf-8 -*-
"""Remove leftover hand-written TOC lines between the TOC field and Part 1 H1."""
import os
import docx

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
SRC = os.path.join(ROOT, "Docs", "用户手册.docx")
doc = docx.Document(SRC)

# locate the real Part-1 heading (style Heading 1)
part1 = None
hint_idx = None
paras = doc.paragraphs
for i, p in enumerate(paras):
    if hint_idx is None and p.text.strip().startswith("提示：页码以实际排版为准"):
        hint_idx = i
    if p.style.name == "Heading 1" and p.text.strip().startswith("第一部分"):
        part1 = i
        break

assert hint_idx is not None and part1 is not None and hint_idx < part1, (hint_idx, part1)
removed = 0
for p in paras[hint_idx + 1: part1]:
    p._p.getparent().remove(p._p)
    removed += 1
doc.save(SRC)
print("removed", removed, "leftover TOC lines")
