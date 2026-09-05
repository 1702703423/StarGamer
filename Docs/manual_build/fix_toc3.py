# -*- coding: utf-8 -*-
"""Wrap the static TOC entries INSIDE the TOC field so a field update
replaces them instead of duplicating them."""
import os
import docx
from docx.oxml import parse_xml
from docx.oxml.ns import nsdecls

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
SRC = os.path.join(ROOT, "Docs", "用户手册.docx")
doc = docx.Document(SRC)
paras = doc.paragraphs

toc_field_para = None
first_entry = None
last_entry = None
hint = None
for i, p in enumerate(paras):
    if ' TOC ' in p._p.xml and 'instrText' in p._p.xml:
        toc_field_para = p
        if first_entry is None:
            first_entry = paras[i - 1] if i > 0 else None
    if p.style.name in ('toc 1', 'toc 2', 'toc 3') and p.text.strip().startswith('附录'):
        last_entry = p
    if p.text.strip().startswith('提示：页码以实际排版为准'):
        hint = p

assert toc_field_para is not None and first_entry is not None and last_entry is not None

NS = nsdecls('w')
begin_runs = ('<w:r %s><w:fldChar w:fldCharType="begin" w:dirty="true"/></w:r>'
              '<w:r><w:instrText xml:space="preserve"> TOC \\o "1-3" \\h \\z \\u </w:instrText></w:r>'
              '<w:r><w:fldChar w:fldCharType="separate"/></w:r>')
for piece in reversed(parse_xml('<w:root %s>%s</w:root>' % (NS, begin_runs % NS))):
    first_entry._p.insert(0 if first_entry._p.find(
        '{http://schemas.openxmlformats.org/wordprocessingml/2006/main}pPr') is None
        else 1, piece)
    # insert after pPr if present
end_run = parse_xml('<w:r %s><w:fldChar w:fldCharType="end"/></w:r>' % NS)
last_entry._p.append(end_run)

toc_field_para._p.getparent().remove(toc_field_para._p)
doc.save(SRC)
print("field wrapped around entries; old field paragraph removed")
