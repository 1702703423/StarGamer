# -*- coding: utf-8 -*-
"""Insert technical-detail sections and flowcharts into Docs/用户手册.docx.

Matching the manual's existing formatting exactly:
  body:  Calibri/Microsoft YaHei sz22, after=120 line=300
  h3:    Heading3, bold 1F4D78 sz24, keepNext
  bullet/num: ListBullet/ListNumber sz22, after=80 line=300
  code:  CodeBlock style, Consolas sz18 243447, shd F2F4F7, ind 180
  caption: centered italic 687385 sz19, after=160
  image: centered, before/after=40
  table: fixed 9360 dxa, CBD5E1 borders, header F4F6F9 bold 1F3A5F sz21,
         body 273444 sz21, tcMar 80/120, line 288, tblHeader + cantSplit
Figure captions get provisional tokens 图@nnn@ and are renumbered at the end.
"""
import copy
import os
import re
import shutil
import sys

import docx
from docx.oxml import parse_xml
from docx.oxml.ns import nsdecls, qn
from docx.shared import Emu

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
SRC = os.path.join(ROOT, "Docs", "用户手册.docx")
BAK = os.path.join(ROOT, "Docs", "manual_build", "用户手册.backup.docx")
DIAG = os.path.join(ROOT, "Docs", "images", "diagrams")

shutil.copyfile(SRC, BAK) if not os.path.exists(BAK) else None
doc = docx.Document(SRC)
body = doc.element.body

RPR_BODY = ('<w:rFonts w:ascii="Calibri" w:hAnsi="Calibri" w:eastAsia="Microsoft YaHei"/>'
            '<w:sz w:val="22"/><w:lang w:val="zh-CN" w:eastAsia="zh-CN"/>')
RPR_LEAD = ('<w:rFonts w:ascii="Calibri" w:hAnsi="Calibri" w:eastAsia="Microsoft YaHei"/>'
            '<w:b/><w:color w:val="1F3A5F"/><w:sz w:val="22"/>'
            '<w:lang w:val="zh-CN" w:eastAsia="zh-CN"/>')
RPR_H3 = ('<w:rFonts w:ascii="Calibri" w:hAnsi="Calibri" w:eastAsia="Microsoft YaHei"/>'
          '<w:b/><w:color w:val="1F4D78"/><w:sz w:val="24"/>'
          '<w:lang w:val="zh-CN" w:eastAsia="zh-CN"/>')
RPR_CAP = ('<w:rFonts w:ascii="Calibri" w:hAnsi="Calibri" w:eastAsia="Microsoft YaHei"/>'
           '<w:i/><w:color w:val="687385"/><w:sz w:val="19"/>'
           '<w:lang w:val="zh-CN" w:eastAsia="zh-CN"/>')
RPR_CODE = ('<w:rFonts w:ascii="Consolas" w:hAnsi="Consolas" w:eastAsia="Microsoft YaHei"/>'
            '<w:color w:val="243447"/><w:sz w:val="18"/>'
            '<w:lang w:val="en-US" w:eastAsia="en-US"/>')


def esc(t):
    return (t.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
             .replace('"', "&quot;"))


def xml_p(ppr, runs):
    return parse_xml('<w:p %s>%s</w:p>' % (nsdecls('w'), ppr + runs))


def run(text, rpr=RPR_BODY):
    return '<w:r><w:rPr>%s</w:rPr><w:t xml:space="preserve">%s</w:t></w:r>' % (rpr, esc(text))


def para(text, lead=None):
    """Body paragraph; optional bold lead like '标题：'."""
    runs = run(lead, RPR_LEAD) + run(text) if lead else run(text)
    ppr = '<w:pPr><w:spacing w:before="0" w:after="120" w:line="300" w:lineRule="auto"/></w:pPr>'
    return xml_p(ppr, runs)


def h3(text):
    ppr = '<w:pPr><w:pStyle w:val="Heading3"/><w:keepNext/></w:pPr>'
    return xml_p(ppr, run(text, RPR_H3))


def bullet(text):
    ppr = ('<w:pPr><w:pStyle w:val="ListBullet"/>'
           '<w:spacing w:before="0" w:after="80" w:line="300" w:lineRule="auto"/></w:pPr>')
    return xml_p(ppr, run(text))


def numbered(text):
    ppr = ('<w:pPr><w:pStyle w:val="ListNumber"/>'
           '<w:spacing w:before="0" w:after="80" w:line="300" w:lineRule="auto"/></w:pPr>')
    return xml_p(ppr, run(text))


def code_block(code):
    lines = code.strip("\n").split("\n")
    runs = ['<w:r><w:rPr>%s</w:rPr>' % RPR_CODE]
    for i, ln in enumerate(lines):
        if i:
            runs.append('<w:br/>')
        runs.append('<w:t xml:space="preserve">%s</w:t>' % esc(ln.replace("\t", "    ")))
    runs.append('</w:r>')
    ppr = ('<w:pPr><w:pStyle w:val="CodeBlock"/>'
           '<w:spacing w:before="40" w:after="160" w:line="240" w:lineRule="auto"/>'
           '<w:shd w:fill="F2F4F7"/><w:ind w:left="180" w:right="180"/></w:pPr>')
    return xml_p(ppr, "".join(runs))


def image_para(png_name, width_emu=5486400):
    from PIL import Image
    path = os.path.join(DIAG, png_name)
    with Image.open(path) as im:
        w, h = im.size
    height = int(width_emu * h / w)
    p = doc.add_paragraph()  # appended at end, moved later
    ppr = ('<w:pPr><w:spacing w:before="40" w:after="40" w:line="240" w:lineRule="auto"/>'
           '<w:jc w:val="center"/></w:pPr>')
    p._p.replace(p._p.get_or_add_pPr(), parse_xml('<w:pPr %s>%s</w:pPr>' % (nsdecls('w'), ppr)))
    r = p.add_run()
    r.add_picture(path, width=Emu(width_emu), height=Emu(height))
    return p._p


FIG_TOKENS = {}


def caption(token, text):
    FIG_TOKENS[token] = None
    ppr = ('<w:pPr><w:spacing w:before="0" w:after="160" w:line="240" w:lineRule="auto"/>'
           '<w:jc w:val="center"/></w:pPr>')
    return xml_p(ppr, run('图@%d@  %s' % (token, text), RPR_CAP))


TBL_BORDERS = ('<w:tblBorders>'
               '<w:top w:val="single" w:sz="4" w:space="0" w:color="CBD5E1"/>'
               '<w:left w:val="single" w:sz="4" w:space="0" w:color="CBD5E1"/>'
               '<w:bottom w:val="single" w:sz="4" w:space="0" w:color="CBD5E1"/>'
               '<w:right w:val="single" w:sz="4" w:space="0" w:color="CBD5E1"/>'
               '<w:insideH w:val="single" w:sz="4" w:space="0" w:color="CBD5E1"/>'
               '<w:insideV w:val="single" w:sz="4" w:space="0" w:color="CBD5E1"/>'
               '</w:tblBorders>')
TCMAR = ('<w:tcMar><w:top w:w="80" w:type="dxa"/><w:start w:w="120" w:type="dxa"/>'
         '<w:bottom w:w="80" w:type="dxa"/><w:end w:w="120" w:type="dxa"/></w:tcMar>')
RPR_TH = ('<w:rFonts w:ascii="Calibri" w:hAnsi="Calibri" w:eastAsia="Microsoft YaHei"/>'
          '<w:b/><w:color w:val="1F3A5F"/><w:sz w:val="21"/>'
          '<w:lang w:val="zh-CN" w:eastAsia="zh-CN"/>')
RPR_TD = ('<w:rFonts w:ascii="Calibri" w:hAnsi="Calibri" w:eastAsia="Microsoft YaHei"/>'
          '<w:color w:val="273444"/><w:sz w:val="21"/>'
          '<w:lang w:val="zh-CN" w:eastAsia="zh-CN"/>')


def cell_para(text, rpr, line="288"):
    return ('<w:p><w:pPr><w:spacing w:before="0" w:after="0" w:line="%s" '
            'w:lineRule="auto"/></w:pPr>%s</w:p>' % (line, run(text, rpr)))


def make_table(headers, rows, widths):
    assert len(widths) == len(headers) == len(rows[0])
    cols = "".join('<w:gridCol w:w="%d"/>' % w for w in widths)
    xml = ['<w:tbl %s><w:tblPr><w:tblW w:type="dxa" w:w="9360"/><w:jc w:val="left"/>'
           '<w:tblLayout w:type="fixed"/>'
           '<w:tblLook w:firstColumn="1" w:firstRow="1" w:lastColumn="0" w:lastRow="0" '
           'w:noHBand="0" w:noVBand="1" w:val="04A0"/>'
           '<w:tblInd w:w="120" w:type="dxa"/>' % nsdecls('w'),
           TBL_BORDERS, '</w:tblPr><w:tblGrid>', cols, '</w:tblGrid>']
    hdr_cells = "".join(
        '<w:tc><w:tcPr><w:tcW w:type="dxa" w:w="%d"/>%s<w:vAlign w:val="center"/>'
        '<w:shd w:val="clear" w:color="auto" w:fill="F4F6F9"/></w:tcPr>%s</w:tc>'
        % (w, TCMAR, cell_para(t, RPR_TH)) for t, w in zip(headers, widths))
    xml.append('<w:tr><w:trPr><w:tblHeader/><w:cantSplit/></w:trPr>%s</w:tr>' % hdr_cells)
    for row in rows:
        cells = "".join(
            '<w:tc><w:tcPr><w:tcW w:type="dxa" w:w="%d"/>%s<w:vAlign w:val="center"/></w:tcPr>%s</w:tc>'
            % (w, TCMAR, cell_para(t, RPR_TD)) for t, w in zip(row, widths))
        xml.append('<w:tr><w:trPr><w:cantSplit/></w:trPr>%s</w:tr>' % cells)
    xml.append('</w:tbl>')
    return parse_xml("".join(xml))


def callout(title, text):
    return make_table([title + "  "], [[text]], [9360])


# ------------------------------------------------------------- anchor lookup
def find_para(prefix):
    for p in doc.paragraphs:
        if p.text.strip().startswith(prefix):
            return p._p
    raise RuntimeError("paragraph not found: " + prefix)


def find_table(header_prefix):
    for t in doc.tables:
        if t.rows[0].cells[0].text.strip().startswith(header_prefix):
            return t._tbl
    raise RuntimeError("table not found: " + header_prefix)


class Inserter:
    """Chain-insert elements after an anchor element."""

    def __init__(self, anchor_el):
        self.cursor = anchor_el

    def add(self, el):
        self.cursor.addnext(el)
        self.cursor = el
        return el

    def para(self, text, lead=None):
        return self.add(para(text, lead))

    def h3(self, text):
        return self.add(h3(text))

    def bullet(self, text):
        return self.add(bullet(text))

    def numbered(self, text):
        return self.add(numbered(text))

    def code(self, code):
        return self.add(code_block(code))

    def table(self, headers, rows, widths):
        return self.add(make_table(headers, rows, widths))

    def callout(self, title, text):
        return self.add(callout(title, text))

    def image(self, png):
        return self.add(image_para(png))

    def caption(self, token, text):
        return self.add(caption(token, text))

    def spacer(self):
        ppr = ('<w:pPr><w:spacing w:before="0" w:after="0" w:line="240" '
               'w:lineRule="auto"/></w:pPr>')
        return self.add(xml_p(ppr, ""))


# =============================================================== insertions
# -- 1.2 硬件平台技术参数 -------------------------------------------------
ins = Inserter(find_table("导航约定"))
ins.h3("1.2　硬件平台技术参数")
ins.para("Star Gamer 的硬件平台围绕 STM32F407ZGT6 构建，板载 192 KB 片内 SRAM 并外扩 1 MB SRAM。"
         "下表汇总与日常使用和二次开发最相关的关键参数。")
ins.table(["项目", "规格"], [
    ["主控", "STM32F407ZGT6（Cortex-M4F，1 MB Flash）"],
    ["主频档位", "192 / 216 / 240 MHz 三档，设置页切换，默认 240 MHz"],
    ["内存", "128 KB 主 SRAM + 64 KB CCM（CPU 专用）+ 1 MB 外部 SRAM（FSMC）"],
    ["显示", "ST7789 320×240 RGB565，SPI + DMA 传输，背光 PWM 调光"],
    ["音频", "I2S3 + DMA 双缓冲输出至 PCM5102 DAC"],
    ["存储", "SD 卡（SDIO 4-bit + DMA）、W25Q64 8 MB SPI Flash、AT24C02 256 B EEPROM"],
    ["USB", "OTG FS，MSC 设备模式（SD 卡模拟 U 盘）"],
    ["调试", "UART1 控制台（Letter Shell），printf 重定向"],
], [1600, 7760])

# -- 1.3 时钟档位 ----------------------------------------------------------
ins = Inserter(ins.cursor)
ins.h3("1.3　时钟档位与主频切换")
ins.para("系统时钟由 8 MHz 外部晶振经 PLL 倍频得到。固件提供 192/216/240 MHz 三档：默认 240 MHz "
         "保证视频解码与 GUI 流畅度；对续航或发热敏感的场景可切换到低速档。")
ins.image("fig_clock.png")
ins.caption(101, "系统时钟树与三档主频。")
ins.para("档位在设置页切换，选择后立即保存到系统设置并提示复位生效（Clock saved; reset MCU to apply）；"
         "重启后固件按新档位重配 PLL 与总线分频。档位与音量、主题等设置一同持久化，掉电不丢失（见 6.1 节）。")

# -- 2.2 文件浏览器实现 ----------------------------------------------------
ins = Inserter(find_table("页面"))
ins.h3("2.2　文件浏览器的实现细节")
ins.para("所有媒体与应用页面共用同一个文件浏览器组件（GUI_FileManage），行为一致：")
ins.bullet("按页面声明的扩展名过滤，目录排在文件之前；媒体页面从 SD 卡根目录 0:/ 开始浏览。")
ins.bullet("文件名支持 OEM（GBK）与 UTF-8 双编码，界面侧完成转码，中文与长文件名不乱码。")
ins.bullet("列表采用异步分页加载，单页最多 32 项（GUI_FILEMANAGE_MAX_ITEMS），大目录不会阻塞界面。")
ins.bullet("浏览器上下文从 SRAMEX 动态分配，进入页面时创建、退出时归还，降低主 SRAM 占用。")

# -- 3.1 NES 技术细节 ------------------------------------------------------
ins = Inserter(find_table("异常提示"))
ins.image("fig_nes.png")
ins.caption(102, "NES 模拟器运行时数据流：视频经双 stripe 直写 LCD，音频走系统 I2S 回调链。")
ins.para("模拟器与硬件协同的几个关键实现：")
ins.bullet("跳帧策略 NES_SKIP_FRAME=1：每两帧渲染一次，把 CPU 预算让给 PPU/APU 时序。")
ins.bullet("渲染输出使用 16 行 × 2 的 LCD 双 stripe，缓冲从 SRAMEX 以 32 字节对齐分配，退出时回收。")
ins.bullet("APU 音频经 audio_card_register_tx_index(nes_i2s_dma_tx_callback) 挂入系统 I2S 回调链，与音乐/视频共享同一声卡架构。")
ins.bullet("内置 118 个 mapper（含常见 MMC1/MMC3 等），不支持的卡带会在加载时提示。")
ins.bullet("提供 NES_AudioStats_t 统计（回调/欠载计数），便于定位声音断续问题。")

# -- 4.1 音乐数据流 --------------------------------------------------------
ins = Inserter(find_table("操作"))
ins.para("播放器核心（Player.c）把解码器抽象为统一的 codec 接口（read_pcm / seek / tell_seconds / close）："
         "WAV 直接解析 PCM 数据块，MP3 由 Helix 定点库以 5120 B 窗口解码。解码出的 PCM 帧进入播放管道，"
         "由声卡回调推入 I2S DMA 双缓冲连续输出；进度与时长来自解码位置回调，Seek 在解码器内部完成文件定位。")
ins.image("fig_music.png")
ins.caption(103, "音乐播放数据流：解码管道直通声卡回调，格式差异被 codec 接口隔离。")

# -- 4.2 视频格式要求 ------------------------------------------------------
ins = Inserter(find_para("图 5  AVI 视频播放器界面"))
ins.para("当前 AVI 管线只接受 MJPEG 视频轨 + PCM 音频轨的组合，分辨率上限 320×240，"
         "不满足的文件会在解析阶段被直接拒绝。制作或转码视频时建议：")
ins.table(["项目", "建议值"], [
    ["容器", "AVI；解析器打开时建立流索引，Seek 基于索引与 chunk 边界容错实现"],
    ["视频", "MJPEG，≤320×240，23.976 fps 或 25 fps"],
    ["视频码率", "≤2 Mbps 可全帧率流畅播放；10 Mbps 仍可播放，但会持续丢帧（见 17 章实测）"],
    ["音频", "PCM 22050 Hz / 16 bit，建议立体声，与视频交错存储"],
    ["SD 卡", "优先选择高速卡；读取吞吐不足先表现为丢帧增多，而不是声音卡顿"],
], [1600, 7760])

# -- 5 AXF 生命周期 --------------------------------------------------------
ins = Inserter(find_table("AXF 约束"))
ins.para("加载器把一次运行抽象为 IDLE → LOADING → STARTING → RUNNING → STOPPING 的状态机："
         "选择文件后由 dl_load_file() 完成 ELF 解析与重定位；随后按 Settings 中的 AXF stack 档位"
         "（4/8/12/16/24 KB）分配应用栈，创建应用线程与监控线程；运行中按 PB7（Select/Exit）或应用自行返回"
         "进入 STOPPING，监控线程回收加载段与栈后回到 IDLE。任何阶段失败都会进入 ERROR 并提示原因，"
         "已分配的资源同样被回收。架构细节见第 15 章。")

# -- 6.2 字库安装流程图 ----------------------------------------------------
ins = Inserter(find_para("重新进入 Text 页面"))
ins.image("fig_font.png")
ins.caption(104, "字库安装流程：校验、擦写、回读验证与设置保存全部完成后才会提示成功。")

# -- 6.3 升级细节 ----------------------------------------------------------
ins = Inserter(find_table("升级警告"))
ins.para("升级文件与流程的技术约束：")
ins.bullet("update.bin 必须放在 SD 卡根目录；固件先检查大小（应用区上限 960 KB）与中断向量表合法性，再计算 CRC32。")
ins.bullet("校验通过后写入 W25Q64 暂存区，并在 EEPROM 写入升级记录（双槽容错），重启后由 BootLoader 完成烧写。")
ins.bullet("任意时刻断电都是安全的：BootLoader 上电读取记录，未完成的升级会被继续或回滚（见 14.1 节）。")

# -- 7.1 诊断命令 ----------------------------------------------------------
ins = Inserter(find_table("命令"))
ins.h3("7.1　诊断命令使用建议")
ins.para("诊断命令的价值在于“对比”：先记录基线，再对比场景中的变化。推荐的使用模式：")
ins.bullet("开机空载先执行 sysstats 与 memfrag 记录基线；进入场景（播放/NES/AXF）后再执行一次，差值即场景净消耗。")
ins.bullet("退出场景后复查各池占用是否回到基线，判断是否存在资源泄漏。")
ins.bullet("videostat / audiostat 需在播放中执行才有意义；可先执行 videostat r 清零累计计数，再连续采样三次看趋势。")
ins.bullet("内存问题看 memfrag 的碎片率、最大连续块与分配失败计数（失败计数非零必须处理）；"
           "存储瓶颈看 videostat 的 io ticks 与 read_err；音频问题看 underrun 与 collide。")

# -- 9.1 内存与线程规范 ----------------------------------------------------
ins = Inserter(find_table("CubeMX 安全区"))
ins.h3("9.1　内存分配与线程创建规范")
ins.para("两个内存池由统一分配器管理，选错池是最常见的移植错误：DMA 无法访问 CCM，任务栈也不能用业务堆代替。"
         "选型决策流程如下：")
ins.image("fig_mem.png")
ins.caption(105, "内存池选型决策：按“DMA / 任务栈 / CPU 热数据”三分。")
ins.para("线程一律使用 CMSIS-RTOS V2 创建，栈通过 sys_thread_stack_alloc() 取自 CCM（SRAMCCM）：")
ins.code("""osThreadAttr_t attr = {0};
attr.name = "worker";
attr.stack_size = 2048;
attr.stack_mem = sys_thread_stack_alloc(attr.stack_size); /* 栈取自 CCM */
attr.priority = osPriorityNormal;
osThreadId_t tid = osThreadNew(worker_entry, NULL, &attr);
/* 线程退出后，由父上下文调用 sys_thread_stack_free(attr.stack_mem) 回收；
   禁止在线程自身仍在运行时释放其栈 */""")

# -- 10.1 SVC 机制 ---------------------------------------------------------
ins = Inserter(find_para("SG_KEY_* 位掩码"))
ins.h3("10.1　SVC 调用机制与资源模型")
ins.para("AXF 应用不直接链接 libc，也不直接触碰外设：所有跨边界调用都经 SVC 进入宿主。通道有两条：")
ins.bullet("C 标准库：memcpy / strlen / snprintf 等 22 个函数经 SVC 8 查宿主向量表（dl_vector.c），"
           "AXF 内的调用点在重定位时被指向统一的 SVC 桩。")
ins.bullet("设备 API：stargamer_axf_api.h 中的 SG_* 内联函数触发 SVC，落到宿主 ExternAPI_* 实现；"
           "LCD 系列函数内部先 Acquire/Release 显示所有权，AXF 无法绕过 LVGL 互斥直接写屏。")
ins.bullet("加载段与运行时内存由宿主从 SRAMEX 分配，随卸载整体回收；应用退出后不残留任何分配。")
ins.bullet("构建产物建议先在 PC 端运行 Tests/AXF_Loader_Host/validate_axf.py 预检 ELF 兼容性。")

# -- 11 启动链路 -----------------------------------------------------------
ins = Inserter(find_table("内存选择原则"))
ins.image("fig_boot.png")
ins.caption(106, "从上电到主菜单的完整启动链路：BootLoader 交权后，应用按固定顺序初始化。")
ins.para("应用侧初始化顺序有明确依赖：内存池先于一切动态分配；设置加载先于 GUI（主题、字库、时钟档位都来自设置）；"
         "文件系统挂载先于任何页面浏览。某一步失败时，错误经 Shell 或启动页提示，不会带着错误状态继续初始化。")
ins.h3("11.1　存储介质映射")
ins.para("固件同时管理三块存储介质，用途划分固定，二次开发时不得挪用他区：")
ins.image("fig_storage.png")
ins.caption(107, "内部 Flash、W25Q64 与 AT24C02 的分区映射。")
ins.bullet("内部 Flash 低 64 KB 属于 BootLoader，应用从 0x08010000 链接与执行。")
ins.bullet("W25Q64 各分区边界由 IAP_Protocol.h 宏定义并有编译期断言保护；暂存区与备份区仅 IAP 使用。")
ins.bullet("设置写入走“双槽 + 序列号”协议：每次写入交替使用两个槽并递增序列号，读取取最新有效槽，"
           "写入中途掉电不影响旧数据。直接读写设置区会破坏该协议。")

# -- 12 SDIO ---------------------------------------------------------------
ins = Inserter(find_para("FatFs/SDIO 日志默认关闭"))
ins.image("fig_sdio.png")
ins.caption(108, "SDIO 防御性 I/O 队列：请求串行化，拥有者提交并消费结果。")
ins.para("队列把“谁在用 SD 卡”变成显式状态：每个请求绑定拥有者（AVI 解析器、JPEG 管线或 PCM 补数），"
         "完成结果由拥有者消费，任何时刻只有一个 DMA 传输在途，从机制上避免多模块并发读卡冲突。"
         "拥有者线程等待完成信号并带超时，错误统一记入 read_err 统计，可经 videostat 观察。")

# -- 13 GUI ----------------------------------------------------------------
ins = Inserter(find_para("GUI_Manage 以页面 ID"))
ins.image("fig_page.png")
ins.caption(109, "页面生命周期与历史栈：创建、显示、回退、销毁由 GUI_Manage 统一编排。")
ins.para("线程分工：GUI_Task 以 4096 字节栈运行于 osPriorityAboveNormal，负责页面编排；"
         "LVGL 渲染独立成任务（8192 字节栈，osPriorityNormal，每 1 ms 调用 lv_timer_handler）。"
         "页面配色统一经 GUI_Theme 获取（深/浅两套主题、20+ 颜色 token），页面代码不写死颜色值。")

# -- 13.1 握手 -------------------------------------------------------------
ins = Inserter(find_table("阶段"))
ins.image("fig_handshake.png")
ins.caption(110, "LVGL 与独占任务的显示所有权握手时序。")

# -- 14.1 IAP 状态机 -------------------------------------------------------
ins = Inserter(find_table("BootLoader 菜单"))
ins.h3("14.1　升级状态机与断电恢复")
ins.image("fig_iap.png")
ins.caption(111, "IAP 升级状态机：EEPROM 记录驱动，BootLoader 每次上电接管未完成的升级。")
ins.para("升级记录的关键设计：")
ins.bullet("记录共 36 字节，magic“BLI2”、版本 2，存于 EEPROM 双槽：SLOT0 @128、SLOT1 @168；"
           "写入按序列号递增交替，读取取序列号新的有效槽。")
ins.bullet("状态码：0xAA 暂存区就绪（READY）、0x55 烧写中（PROGRAMMING）、0xBB 请求恢复备份、0xCC 备份有效。")
ins.bullet("记录结构由 IAP_RECORD_MEMBERS 宏定义，App 与 BootLoader 共享 IAP_Protocol.h；"
           "扩展字段必须两侧同步，并保持 IAP_RECORD_SIZE 一致。")

# -- 15 AXF 架构 -----------------------------------------------------------
ins = Inserter(find_para("动态应用不应依赖"))
ins.image("fig_axf.png")
ins.caption(112, "AXF 加载器状态机与 SVC 通道：加载、运行、回收全程由宿主监督。")
ins.para("加载器组件分工：dl_lib 负责装载、符号查找与销毁；dl_elf.h 定义 ELF 结构解析；"
         "dl_arch 处理 Cortex-M 重定位；dl_port 抽象文件与内存；dl_vector / dl_vector_api 组成宿主函数向量表。"
         "宿主侧以“会话”管理一次运行（ExternAPI_AXF_Begin / End / ForceEnd），LCD API 通过 acquire/release "
         "与 LVGL 互斥。用户 API 编号定义于 dl_user_api.h，宿主与 demo 工程（Star Gamer ELF）两侧保持同步。")

# -- 16 视频流水线 ---------------------------------------------------------
ins = Inserter(find_para("当前 MJPEG 条带高度"))
ins.image("fig_video.png")
ins.caption(113, "AVI 播放双通道流水线：音频时钟驱动帧 deadline，解码与 LCD 传输重叠执行。")
ins.para("播放循环由音频时钟驱动：每个视频帧以“音频已播放帧数”换算 deadline，解码提前完成则等待送显，"
         "落后则丢帧追赶（计入 drop late）。LCD 写入走双 stripe DMA，与 JPEG 解码在片上 SRAM 中的计算重叠执行；"
         "偶发的大帧解码超时表现为一次性连丢数帧，而声音始终连续——音频 FIFO 的 24 KB 余量提供了缓冲。")

# -- 17.2 实测对比 ---------------------------------------------------------
ins = Inserter(find_para("建议在同一文件的开头"))
ins.h3("17.2　两组实测数据对比")
ins.para("下表摘取两组样片各三次 videostat 采样中的累计值（同机、同卡），便于直观感受码率的影响：")
ins.table(["指标", "23.976 fps / 2 Mbps", "25 fps / 10 Mbps", "解读"], [
    ["累计丢帧", "46 帧 / 已解码 2407（约 1.9%）", "293 帧 / 已解码 1226（约 19%）", "高码率下以丢帧换实时"],
    ["drop late", "40", "287", "绝大多数丢帧源于追赶音频时钟"],
    ["jpeg_max 单帧解码", "102 ms", "69 ms（单次峰值 273 ms）", "个别大帧仍会瞬时阻塞流水线"],
    ["PCM underrun", "9（开局预缓冲后不再增加）", "16 → 27 缓增", "音频 FIFO 始终有余量垫底"],
    ["audio DMA collide", "0", "0", "本批次固件未复现碰撞"],
    ["SD 读取累计", "34.3 MB / 9390 次（进度 60%）", "31.0 MB / 6575 次（进度 26%）", "高码率单位时间吞吐显著更高"],
], [1750, 2450, 2450, 2710])
ins.para("两组数据说明：2 Mbps 样片全程音画同步、几乎无可感丢帧；10 Mbps 样片画面持续以丢帧保持实时，"
         "但声音始终连续、无 DMA 碰撞。瓶颈在 JPEG 解码与 SD 吞吐，音频通路余量充足。")
ins.h3("17.3　选片与转码建议")
ins.bullet("编码：MJPEG + PCM，分辨率 ≤320×240；超过上限的文件会被解析器直接拒绝。")
ins.bullet("帧率与码率：23.976 或 25 fps；视频码率 ≤2 Mbps 可获得全帧率体验，10 Mbps 可播放但持续丢帧。")
ins.bullet("音频：PCM 22050 Hz / 16 bit，建议立体声；音频参数异常会导致预缓冲失败而无法开播。")
ins.bullet("视频来源建议自行转码压制，网络片源直接拷贝通常码率远超预算，只能以丢帧模式播放。")

# -- 18.1 HardFault --------------------------------------------------------
ins = Inserter(find_table("现象"))
ins.h3("18.1　HardFault 与内存转储")
ins.para("发生 HardFault 时无需调试器：固件内嵌的 Handler 以独立重初始化的 SPI1 直接驱动 LCD，"
         "在屏幕上显示 R0–R3、R12、LR、PC、xPSR，以及 CFSR / HFSR / DFSR / AFSR / MMFAR / BFAR "
         "和 MSP / PSP（5×7 字体，320×240）。由 CFSR 可区分总线/用法/存储器错误，PC 与出错地址可直接在 .map "
         "文件中定位函数。")
ins.para("若开启了内存转储，系统会按设置的区域开关把 CCM、主 SRAM、外部 SRAM 的内存映像写入 W25Q64 "
         "转储分区（3.5 MB，FNV-1a 校验，分区边界有编译期检查）；下次开机经 Shell 命令导出到 SD 卡根目录 "
         "hardfault_dump.bin，配合同版本 .map 文件回溯现场。")

print("insertions done")

# =============================================== figure renumbering & refs
fig_re = re.compile(r"^图(?:@(\d+)@|\s*(\d+))\s")
ref_re = re.compile(r"图\s*(\d+)")

# collect caption paragraphs in document order
captions = []
for p in doc.paragraphs:
    m = fig_re.match(p.text.strip())
    if m and p.style.name == "Normal":
        old = int(m.group(1) or m.group(2))
        captions.append((p, old))

remap = {}
for new_no, (p, old) in enumerate(captions, start=1):
    remap[old] = new_no

for new_no, (p, old) in enumerate(captions, start=1):
    full = p.text.strip()
    rest = fig_re.sub("", full, count=1)          # caption body after the number
    rest = ref_re.sub(lambda m: "图 %d" % remap[int(m.group(1))]
                      if int(m.group(1)) in remap else m.group(0), rest)
    # rebuild runs: keep first run's rPr
    first_rpr = None
    for r in p.runs:
        first_rpr = r._r.find(qn('w:rPr'))
        if first_rpr is not None:
            first_rpr = copy.deepcopy(first_rpr)
            break
    for r in list(p._p.findall(qn('w:r'))):
        p._p.remove(r)
    new_r = parse_xml('<w:r %s><w:t xml:space="preserve">图 %d  %s</w:t></w:r>'
                      % (nsdecls('w'), new_no, esc(rest)))
    if first_rpr is not None:
        new_r.insert(0, first_rpr)
    p._p.append(new_r)

# fix in-text references (skip caption paragraphs)
cap_ids = {id(p._p) for p, _ in captions}


def fix_refs_in_para(p):
    if id(p._p) in cap_ids:
        return
    for r in p.runs:
        t = r.text
        if "图" in t and ref_re.search(t):
            r.text = ref_re.sub(
                lambda m: "图 %d" % remap[int(m.group(1))]
                if int(m.group(1)) in remap else m.group(0), t)


def fix_refs_in_cell(cell):
    for p in cell.paragraphs:
        fix_refs_in_para(p)


for p in doc.paragraphs:
    fix_refs_in_para(p)
for t in doc.tables:
    for row in t.rows:
        for cell in row.cells:
            fix_refs_in_cell(cell)

doc.save(SRC)
print("saved:", SRC)
print("figures:", len(captions))
