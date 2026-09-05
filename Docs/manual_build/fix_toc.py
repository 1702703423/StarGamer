# -*- coding: utf-8 -*-
"""Replace the hand-written text TOC with a real Word TOC field (levels 1-3)."""
import os
import re
import docx
from docx.oxml import parse_xml
from docx.oxml.ns import nsdecls, qn

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
SRC = os.path.join(ROOT, "Docs", "用户手册.docx")
doc = docx.Document(SRC)
body = doc.element.body

RPR_BODY = ('<w:rFonts w:ascii="Calibri" w:hAnsi="Calibri" w:eastAsia="Microsoft YaHei"/>'
            '<w:sz w:val="22"/><w:lang w:val="zh-CN" w:eastAsia="zh-CN"/>')
RPR_HINT = ('<w:rFonts w:ascii="Calibri" w:hAnsi="Calibri" w:eastAsia="Microsoft YaHei"/>'
            '<w:i/><w:color w:val="8A94A6"/><w:sz w:val="19"/>'
            '<w:lang w:val="zh-CN" w:eastAsia="zh-CN"/>')

paras = doc.paragraphs
toc_head = next(p for p in paras if p.text.strip() == "目录")
part1 = next(p for p in paras if p.text.strip().startswith("第一部分"))

# keep the intro paragraph (update its stale trailing clause), drop the rest
intro = None
to_delete = []
seen_head = False
for p in paras:
    if p._p is toc_head._p:
        seen_head = True
        continue
    if p._p is part1._p:
        break
    if seen_head:
        if intro is None and p.text.strip().startswith("本目录按用户操作"):
            intro = p
        else:
            to_delete.append(p)

for p in to_delete:
    p._p.getparent().remove(p._p)

# update intro text (rebuild runs, keep formatting of first run)
first_rpr = intro._p.find(qn('w:r') + '/' + qn('w:rPr'))
import copy
rpr_copy = copy.deepcopy(first_rpr) if first_rpr is not None else None
for r in list(intro._p.findall(qn('w:r'))):
    intro._p.remove(r)
new_r = parse_xml('<w:r %s><w:t xml:space="preserve">%s</w:t></w:r>' % (
    nsdecls('w'),
    "本手册按用户操作、二次开发和系统架构三部分组织。目录由标题样式自动生成，"
    "也可使用 Word 导航窗格按 Heading 1/2/3 浏览。"))
if rpr_copy is not None:
    new_r.insert(0, rpr_copy)
intro._p.append(new_r)

toc_field = parse_xml(
    '<w:p %s><w:pPr><w:spacing w:before="120" w:after="120" w:line="300" '
    'w:lineRule="auto"/></w:pPr>'
    '<w:r><w:fldChar w:fldCharType="begin" w:dirty="true"/></w:r>'
    '<w:r><w:instrText xml:space="preserve"> TOC \\o "1-3" \\h \\z \\u </w:instrText></w:r>'
    '<w:r><w:fldChar w:fldCharType="separate"/></w:r>'
    '<w:r><w:rPr><w:i/><w:color w:val="8A94A6"/><w:sz w:val="21"/></w:rPr>'
    '<w:t>（目录内容将在打开文档时自动更新；若未显示，请右键此处选择“更新域”）</w:t></w:r>'
    '<w:r><w:fldChar w:fldCharType="end"/></w:r></w:p>' % nsdecls('w'))

hint = parse_xml(
    '<w:p %s><w:pPr><w:spacing w:before="0" w:after="200" w:line="240" '
    'w:lineRule="auto"/></w:pPr>'
    '<w:r><w:rPr>%s</w:rPr><w:t>提示：页码以实际排版为准，更新域后自动刷新。</w:t></w:r></w:p>'
    % (nsdecls('w'), RPR_HINT))

intro._p.addnext(toc_field)
toc_field.addnext(hint)

doc.save(SRC)
print("TOC field installed; removed", len(to_delete), "manual TOC lines")
