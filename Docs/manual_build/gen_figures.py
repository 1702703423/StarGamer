# -*- coding: utf-8 -*-
"""Generate flowcharts / architecture diagrams for the Star Gamer user manual.

All figures share one visual language:
  dark navy  #1F3A5F  start/end & emphasis fill
  accent     #2E74B5  arrows / borders
  node       #EAF1F8  process fill,  #9DB8D2 edge
  decision   #FFF4E0 fill,  #D97706 edge
  data       #E8F5EE fill,  #3E8E6D edge
  error      #FBEAEA fill,  #C25450 edge
  note       #F4F6F9 fill,  #CBD5E1 edge
"""
import os
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch, FancyArrowPatch, Rectangle
from matplotlib.font_manager import FontProperties

FONT = FontProperties(fname="C:/Windows/Fonts/msyh.ttc")
FONT_B = FontProperties(fname="C:/Windows/Fonts/msyhbd.ttc")

INK = "#273444"
ACCENT = "#2E74B5"
DARK = "#1F3A5F"
DARK2 = "#33517A"
N_FILL, N_EDGE = "#EAF1F8", "#9DB8D2"
D_FILL, D_EDGE = "#FFF4E0", "#D97706"
G_FILL, G_EDGE = "#E8F5EE", "#3E8E6D"
E_FILL, E_EDGE = "#FBEAEA", "#C25450"
NOTE_FILL, NOTE_EDGE = "#F4F6F9", "#CBD5E1"
ARROW = "#64748B"
WHITE = "#FFFFFF"

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "images", "diagrams")
OUT = os.path.normpath(OUT)
os.makedirs(OUT, exist_ok=True)


def new_ax(w=12.0, h=6.0):
    fig = plt.figure(figsize=(w, h), dpi=200)
    fig.patch.set_facecolor("white")
    ax = fig.add_axes([0, 0, 1, 1])
    ax.set_xlim(0, 100)
    ax.set_ylim(0, 100 * h / w)
    ax.axis("off")
    return fig, ax


def H(ax):
    return ax.get_ylim()[1]


def box(ax, cx, cy, w, h, text, fill=N_FILL, edge=N_EDGE, fs=10.5,
        tc=INK, bold=False, lw=1.4, rounding=1.4):
    p = FancyBboxPatch((cx - w / 2, cy - h / 2), w, h,
                       boxstyle=f"round,pad=0,rounding_size={rounding}",
                       linewidth=lw, facecolor=fill, edgecolor=edge, zorder=2)
    ax.add_patch(p)
    ax.text(cx, cy, text, ha="center", va="center", fontsize=fs,
            fontproperties=(FONT_B if bold else FONT), color=tc, zorder=3,
            linespacing=1.45)


def diamond(ax, cx, cy, w, h, text, fs=10, fill=D_FILL, edge=D_EDGE, tc=INK):
    pts = [(cx, cy + h / 2), (cx + w / 2, cy), (cx, cy - h / 2), (cx - w / 2, cy)]
    ax.add_patch(plt.Polygon(pts, closed=True, facecolor=fill, edgecolor=edge,
                             linewidth=1.4, zorder=2))
    ax.text(cx, cy, text, ha="center", va="center", fontsize=fs,
            fontproperties=FONT_B, color=tc, zorder=3, linespacing=1.4)


def arrow(ax, p1, p2, label=None, color=ARROW, lw=1.6, rad=0.0, fs=9,
          lofs=(0, 0), ls="-", label_color=None, mut=15):
    a = FancyArrowPatch(p1, p2, arrowstyle="-|>", mutation_scale=mut,
                        linewidth=lw, color=color, zorder=1.5,
                        connectionstyle=f"arc3,rad={rad}", linestyle=ls,
                        shrinkA=2, shrinkB=2)
    ax.add_patch(a)
    if label:
        mx, my = (p1[0] + p2[0]) / 2 + lofs[0], (p1[1] + p2[1]) / 2 + lofs[1]
        ax.text(mx, my, label, ha="center", va="center", fontsize=fs,
                fontproperties=FONT, color=label_color or ACCENT, zorder=4,
                fontweight="bold",
                bbox=dict(boxstyle="round,pad=0.18", fc="white", ec="none", alpha=0.92))


def line(ax, p1, p2, color=ARROW, lw=1.4, ls="-"):
    ax.plot([p1[0], p2[0]], [p1[1], p2[1]], color=color, lw=lw, ls=ls,
            zorder=1.4, solid_capstyle="round")


def note(ax, cx, cy, w, h, text, fs=11, fill=NOTE_FILL, edge=NOTE_EDGE, tc="#55627A"):
    box(ax, cx, cy, w, h, text, fill=fill, edge=edge, fs=fs, tc=tc, lw=1.1, rounding=0.8)


def bar(ax, x, y, w, h, label_top, segs, fs=9.5, title_fs=10.5, addr_fs=8.2):
    """Horizontal partition bar. segs = [(frac, name, sub, fill, edge, textcolor)]"""
    total = sum(s[0] for s in segs)
    x0 = x
    for frac, name, sub, fill, edge, tcol in segs:
        sw = w * frac / total
        ax.add_patch(Rectangle((x0, y), sw, h, facecolor=fill, edgecolor=edge,
                               linewidth=1.2, zorder=2))
        ax.text(x0 + sw / 2, y + h * 0.60, name, ha="center", va="center",
                fontsize=fs, fontproperties=FONT_B, color=tcol, zorder=3)
        if sub:
            ax.text(x0 + sw / 2, y + h * 0.22, sub, ha="center", va="center",
                    fontsize=fs - 1.4, fontproperties=FONT, color=tcol, zorder=3)
        x0 += sw
    ax.text(x, y + h + 2.2, label_top, ha="left", va="bottom", fontsize=title_fs,
            fontproperties=FONT_B, color=DARK)


def save(fig, name):
    path = os.path.join(OUT, name)
    fig.savefig(path, facecolor="white")
    plt.close(fig)
    print("saved", path)


# ---------------------------------------------------------------- fig 1 clock
def fig_clock():
    fig, ax = new_ax(12, 5.2)
    h = H(ax)
    box(ax, 8, h * 0.62, 13, 10, "HSE\n外部晶振 8 MHz", fill=G_FILL, edge=G_EDGE, bold=True)
    box(ax, 27, h * 0.62, 14, 10, "PLL 预分频\nPLLM = 4", fill=N_FILL, edge=N_EDGE)
    box(ax, 46, h * 0.62, 14, 10, "VCO 倍频\nPLLN", fill=N_FILL, edge=N_EDGE)
    box(ax, 65, h * 0.62, 14, 10, "PLL 输出分频\nPLLP = 2", fill=N_FILL, edge=N_EDGE)
    arrow(ax, (14.7, h * 0.62), (19.8, h * 0.62))
    arrow(ax, (34.2, h * 0.62), (38.8, h * 0.62))
    arrow(ax, (53.2, h * 0.62), (57.8, h * 0.62))
    ys = [h * 0.86, h * 0.62, h * 0.34]
    labels = [("SYSCLK 192 MHz", "低速档：续航优先"), ("SYSCLK 216 MHz", "中间档"),
              ("SYSCLK 240 MHz", "默认档：性能优先")]
    for i, (y, (t, s)) in enumerate(zip(ys, labels)):
        fill = G_FILL if i == 2 else N_FILL
        edge = G_EDGE if i == 2 else N_EDGE
        box(ax, 88, y, 21, 8.6, t + "\n" + s, fill=fill, edge=edge,
            bold=(i == 2), fs=10)
        arrow(ax, (72.2, h * 0.62), (77.3, y), rad=-0.16 if i == 0 else (0.16 if i == 2 else 0))
    ax.text(77.3, h * 0.62, "三档任选", ha="center", va="center", fontsize=10.5,
            fontproperties=FONT_B, color=ACCENT,
            bbox=dict(boxstyle="round,pad=0.25", fc="white", ec=ACCENT, lw=1.0))
    note(ax, 50, 3.2, 92, 5.4,
         "AHB / APB 分频器随档位一并切换；档位保存于系统设置（AT24C02 + W25Q64 双槽），掉电保持，设置页切换后即时生效")
    save(fig, "fig_clock.png")


# ------------------------------------------------------------- fig 2 nes flow
def fig_nes():
    fig, ax = new_ax(12, 5.6)
    h = H(ax)
    box(ax, 9, h * 0.70, 14, 11, ".nes 文件\nSD 卡根目录", fill=G_FILL, edge=G_EDGE, bold=True)
    box(ax, 27, h * 0.70, 16, 11, "卡带加载\nmapper 初始化\n（内置 118 种）", fs=10.3)
    box(ax, 46, h * 0.70, 15, 11, "CPU 6502\n逐帧执行", fs=10.3)
    box(ax, 65, h * 0.70, 15, 11, "PPU 渲染\n（SKIP_FRAME=1\n跳帧）", fs=10.3)
    box(ax, 88, h * 0.70, 19, 11, "LCD 双 stripe\n16 行 × 2\nSRAMEX 32B 对齐", fs=9.5, fill=D_FILL, edge=DARK, bold=True)
    for x1, x2 in [(16.2, 18.8), (35.2, 38.3), (53.7, 57.3), (72.7, 78.3)]:
        arrow(ax, (x1, h * 0.70), (x2, h * 0.70))
    box(ax, 62, h * 0.26, 26, 10, "APU 音频合成\nnes_i2s_dma_tx_callback 推送", fs=10.3)
    box(ax, 88, h * 0.26, 19, 10, "I2S3 + DMA\n→ PCM5102", fill=D_FILL, edge=DARK, bold=True, fs=10.3)
    arrow(ax, (65, h * 0.645), (62, h * 0.315), rad=0.0)
    arrow(ax, (75.2, h * 0.26), (78.3, h * 0.26))
    note(ax, 27, h * 0.26, 42, 10,
         "退出时回收 stripe / 注销音频回调，\n恢复 GUI 渲染；含 NES_AudioStats_t 统计", fs=10.3)
    save(fig, "fig_nes.png")


# --------------------------------------------------------- fig 3 music pipeline
def fig_music():
    fig, ax = new_ax(12, 5.0)
    h = H(ax)
    y = h * 0.62
    box(ax, 8, y, 13.5, 12, "SD 卡\nWAV / MP3", fill=G_FILL, edge=G_EDGE, bold=True, fs=10.3)
    box(ax, 24.7, y, 15, 12, "FatFs\nSDIO DMA\n512B 对齐缓冲", fs=9.8)
    box(ax, 42.4, y, 16.5, 12, "codec 抽象层\nWAV：PCM 直读\nMP3：Helix 定点", fs=9.8)
    box(ax, 60.1, y, 16.5, 12, "Player 管道\nPCM 帧 + 进度回调\n（支持 Seek）", fs=9.8)
    box(ax, 77.6, y, 14.5, 12, "I2S3 + DMA\n双缓冲", fill=D_FILL, edge=DARK, bold=True, fs=9.8)
    box(ax, 94, y, 11, 12, "DAC\nPCM5102\n→ 扬声器", fill=G_FILL, edge=G_EDGE, bold=True, fs=9.8)
    for x1, x2 in [(14.9, 17.1), (32.3, 34.1), (50.7, 51.8), (68.4, 70.3), (84.9, 88.4)]:
        arrow(ax, (x1, y), (x2, y), mut=12)
    note(ax, 38, h * 0.18, 66, 9.5,
         "codec 统一接口：read_pcm / seek / tell_seconds / close\n"
         "MP3 使用 5120 B 解码窗口，播放器统计解码耗时与错误", fs=10.3)
    save(fig, "fig_music.png")


# --------------------------------------------------------- fig 4 font install
def fig_font():
    fig, ax = new_ax(9.5, 9.2)
    h = H(ax)
    x = 36
    steps = [
        ("SD 卡根目录放入 .bin / .xbf 字库文件", G_FILL, G_EDGE, True),
        ("Settings → Install Font 选择文件并确认", N_FILL, N_EDGE, False),
        ("校验字库文件格式", N_FILL, N_EDGE, False),
        ("擦除 W25Q64 字库区\n（0x000000 起，预留 2 MB）", N_FILL, N_EDGE, False),
        ("分块写入并回读校验", N_FILL, N_EDGE, False),
        ("保存字库参数到系统设置\n（AT24C02 + W25Q64 双槽镜像）", N_FILL, N_EDGE, False),
        ("重新进入 Text 页面验证显示", G_FILL, G_EDGE, True),
    ]
    n = len(steps)
    top, bottom = h - 4, 6
    ys = [top - i * (top - bottom) / (n - 1) for i in range(n)]
    for i, ((t, f, e, b), y) in enumerate(zip(steps, ys)):
        box(ax, x, y, 42, 8.2, t, fill=f, edge=e, bold=b, fs=10)
        if i:
            arrow(ax, (x, ys[i - 1] - 4.2), (x, y + 4.2))
    box(ax, 79, ys[2], 26, 8.0, "格式非法：\n提示错误并中止", fill=E_FILL, edge=E_EDGE, fs=10.3)
    arrow(ax, (57.2, ys[2]), (66, ys[2]), color=E_EDGE)
    arrow(ax, (79, ys[2] + 4.1), (79, ys[1] - 4.1), rad=0, color=E_EDGE, ls="--")
    ax.text(83.5, (ys[1] + ys[2]) / 2, "可重选\n文件重试", ha="center", va="center", fontsize=9.6,
            fontproperties=FONT, color=E_EDGE)
    note(ax, 79, ys[4], 30, 9.0, "回读校验失败\n同样中止并提示", fs=9)
    arrow(ax, (57.2, ys[4]), (63.5, ys[4]), color=E_EDGE)
    save(fig, "fig_font.png")


# ------------------------------------------------------- fig 5 memory decision
def fig_mem():
    fig, ax = new_ax(12, 6.4)
    h = H(ax)
    diamond(ax, 50, h * 0.88, 30, 10, "需要动态分配内存", fs=11)
    diamond(ax, 26, h * 0.66, 26, 10, "缓冲区是否交给\nDMA 控制器？", fs=10.3)
    diamond(ax, 74, h * 0.66, 26, 10, "是否为 RTOS\n任务栈？", fs=10.3)
    arrow(ax, (44, h * 0.845), (30, h * 0.715))
    arrow(ax, (56, h * 0.845), (70, h * 0.715))
    arrow(ax, (13, h * 0.66), (13, h * 0.47), label="是", lofs=(-4, 0))
    arrow(ax, (39, h * 0.66), (44, h * 0.49), rad=-0.25, label="否", lofs=(8, 4))
    arrow(ax, (74, h * 0.61), (74, h * 0.47), label="是", lofs=(-4, 0))
    arrow(ax, (87, h * 0.66), (92.5, h * 0.435), rad=0.25, label="否", lofs=(7, -2))
    box(ax, 13, h * 0.38, 24, 9.5, "SRAMEX（外部 SRAM）\n或片上 Video_Memory 池", fill=G_FILL, edge=G_EDGE, bold=True, fs=10.3)
    box(ax, 44, h * 0.44, 26, 9.5, "CPU-only 热数据 → SRAMCCM\n或片上视频池（MJPEG arena / LibJPEG）", fs=10.3)
    box(ax, 73, h * 0.38, 22, 9.5, "SRAMCCM（CCM）\nsys_thread_stack_alloc()", fill=G_FILL, edge=G_EDGE, bold=True, fs=10.3)
    box(ax, 92.5, h * 0.38, 14, 9.5, "LVGL 堆\nLVGL_MEM_POOL\n= SRAMEX", fs=9.8)
    note(ax, 50, h * 0.14, 94, 9,
         "严禁从 SRAMCCM 分配 DMA 缓冲（DMA 无法访问 CCM）·  接口：sys_malloc(memx, size) / sys_free / sys_malloc_aligned\n"
         "临时线程退出后由父上下文调用 sys_thread_stack_free() 回收，禁止在线程自身运行中释放", fs=10.3)
    save(fig, "fig_mem.png")


# --------------------------------------------------------- fig 6 boot sequence
def fig_boot():
    fig, ax = new_ax(12, 6.2)
    h = H(ax)
    top_hdr = h * 0.93
    box(ax, 15, top_hdr, 26, 5.6, "BootLoader 阶段", fill=DARK, edge=DARK, tc=WHITE, bold=True, fs=10.5)
    box(ax, 47, top_hdr, 20, 5.6, "应用进入", fill=DARK, edge=DARK, tc=WHITE, bold=True, fs=10.5)
    box(ax, 81, top_hdr, 34, 5.6, "init() 初始化序列（defaultTask）", fill=DARK, edge=DARK, tc=WHITE, bold=True, fs=10.5)
    col1 = [
        ("读取 EEPROM IAP 记录\n双槽容错，取序列号新者", N_FILL, N_EDGE, False),
        ("有待完成升级 → 续升 / 回滚\n否则校验 App 中断向量表", N_FILL, N_EDGE, False),
        ("跳转应用 0x08010000", G_FILL, G_EDGE, True),
    ]
    col2 = [
        ("main()：HAL、时钟档位\nGPIO/DMA/FSMC/SDIO/SPI/I2S", N_FILL, N_EDGE, False),
        ("osKernelStart() → defaultTask\n（静态栈 4 KB）调用 init()", N_FILL, N_EDGE, False),
    ]
    col3 = [
        ("System_init()：内存池 → 设置加载\n→ Shell（可选）→ 资源表/帧缓冲", N_FILL, N_EDGE, False),
        ("mount_fs()：SD 卡 FatFs 挂载\nbsp_init()：按键 / W25Q64", N_FILL, N_EDGE, False),
        ("Memory_Dump_ExportPending()\n（存在待导出转储时）", N_FILL, N_EDGE, False),
        ("USB_DEVICE_MSC_ApplySetting()", N_FILL, N_EDGE, False),
        ("lvgl_start_task() → 启动动画 → 主菜单", G_FILL, G_EDGE, True),
    ]
    hdr_bot = top_hdr - 2.8
    bot = 4.5

    def draw_col(cx, cw, items):
        n = len(items)
        slot = (hdr_bot - bot) / n
        bh = slot - 1.6
        ys = [hdr_bot - slot / 2 - i * slot for i in range(n)]
        for i, ((t, f, e, b), y) in enumerate(zip(items, ys)):
            box(ax, cx, y, cw - 2, bh, t, fill=f, edge=e, bold=b, fs=10.2)
            if i:
                arrow(ax, (cx, ys[i - 1] - bh / 2), (cx, y + bh / 2), mut=11)
        return ys, bh

    ys1, bh1 = draw_col(15, 26, col1)
    ys2, bh2 = draw_col(47, 20, col2)
    ys3, bh3 = draw_col(81, 34, col3)
    # snake: col1 bottom -> bottom rail -> up into col2 bottom
    rail = 1.6
    ax.plot([15, 15], [ys1[-1] - bh1 / 2, rail], color=ACCENT, lw=1.7)
    ax.plot([15, 47], [rail, rail], color=ACCENT, lw=1.7)
    arrow(ax, (47, rail), (47, ys2[-1] - bh2 / 2), color=ACCENT, lw=1.7)
    # col2 bottom -> gap between col2/col3 -> col3 first box left edge
    gx = 60.5
    y3_first = ys3[0]
    ax.plot([47, gx], [ys2[-1] - bh2 / 2, ys2[-1] - bh2 / 2], color=ACCENT, lw=1.7)
    ax.plot([gx, gx], [ys2[-1] - bh2 / 2, y3_first], color=ACCENT, lw=1.7)
    arrow(ax, (gx, y3_first), (81 - 34 / 2, y3_first), color=ACCENT, lw=1.7)
    save(fig, "fig_boot.png")


# ------------------------------------------------------------ fig 7 storage map
def fig_storage():
    fig, ax = new_ax(12, 6.6)
    h = H(ax)

    def annotate(x, y_bar, text, fs=8.2, color=INK):
        ax.plot([x, x], [y_bar - 0.6, y_bar - 2.6], color="#8A94A6", lw=0.9, zorder=1)
        ax.text(x, y_bar - 3.1, text, ha="center", va="top", fontsize=fs,
                fontproperties=FONT, color=color, zorder=4, linespacing=1.3)

    y1 = 41
    bar(ax, 4, y1, 92, 9, "内部 Flash 1 MB（0x08000000）",
        [(64, "", "", DARK, DARK, WHITE),
         (936, "应用程序 + 资源", "960 KB · 0x08010000 起", "#DCE7F3", ACCENT, INK)])
    annotate(4 + 92 * 64 / 1000 / 2, y1, "BootLoader\n64 KB · 扇区 0–3")
    y2 = 22
    bar(ax, 4, y2, 92, 9, "W25Q64 SPI Flash 8 MB",
        [(2, "字库区", "2 MB", "#E8F5EE", G_EDGE, INK),
         (3.5, "HardFault 转储区", "3.5 MB", "#FBEAEA", E_EDGE, INK),
         (0.5, "设置区", "", "#FFF4E0", D_EDGE, INK),
         (1, "IAP 暂存区", "1 MB · 0x600000", "#EAF1F8", N_EDGE, INK),
         (1, "IAP 备份区", "1 MB · 0x700000", "#EAF1F8", N_EDGE, INK)])
    annotate(4 + 92 * (2 + 3.5 + 0.25) / 8, y2, "512 KB · 双 4 KB 槽\n0x580000")
    y3 = 4
    bar(ax, 4, y3, 92, 7.5, "AT24C02 EEPROM 256 B",
        [(36, "IAP 记录 槽 0", "地址 128 · 36 B", "#FFF4E0", D_EDGE, INK),
         (36, "IAP 记录 槽 1", "地址 168 · 36 B", "#FFF4E0", D_EDGE, INK),
         (184, "系统设置（音量/背光/主题/字库/时钟/AXF 栈…）", "", "#EAF1F8", N_EDGE, INK)])
    ax.text(50, 0.9, "设置区与 IAP 记录均为双槽镜像：写入按序列号递增，读取取最新有效槽，天然抗断电",
            ha="center", fontsize=10.8, fontproperties=FONT, color="#55627A")
    save(fig, "fig_storage.png")


# ------------------------------------------------------------ fig 8 sdio queue
def fig_sdio():
    fig, ax = new_ax(12, 5.8)
    h = H(ax)
    lanes = [
        ("请求方\nAVI 解析 / MJPEG / PCM 补数", DARK),
        ("I/O 队列\n（串行化）", DARK2),
        ("SDIO + DMA\nRX: Stream3 / TX: Stream6", DARK2),
        ("SD 卡", DARK),
    ]
    lx = [13, 38, 63, 88]
    for (t, c), x in zip(lanes, lx):
        box(ax, x, h * 0.94, 22, 7.5, t, fill=c, edge=c, tc=WHITE, bold=True, fs=10.3)
    y0, y1_ = h * 0.88, h * 0.18
    for x in lx:
        line(ax, (x, y0), (x, y1_), color="#B6C2D1", lw=1.2, ls=(0, (4, 3)))
    msgs = [
        (0, 1, "提交请求：方向 / 偏移 / 长度"),
        (1, 2, "按序取出，启动 DMA"),
        (2, 3, "命令 + 数据传输"),
        (3, 2, "完成 / 超时"),
        (2, 1, "DMA 完成中断"),
        (1, 0, "写回结果，唤醒拥有者"),
    ]
    gap = (y0 - y1_) / (len(msgs) + 1)
    for i, (a, b, t) in enumerate(msgs):
        y = y0 - gap * (i + 1)
        x1, x2 = lx[a], lx[b]
        arrow(ax, (x1, y), (x2, y), color=ACCENT if a < b else "#8A94A6", lw=1.5, mut=13)
        mid = (x1 + x2) / 2
        ax.text(mid, y + 1.6, t, ha="center", fontsize=10.2, fontproperties=FONT, color=INK)
    note(ax, 50, 4.6, 94, 8.2,
         "防御要点：FatFs 512 B 对齐 scratch 缓冲规避 DMA 字对齐覆盖  ·  DMA 流中断优先级高于 SDIO 中断（Tests/SDIO_Config_Host 验证）\n"
         "错误经 read_err / 重试统计暴露，videostat 的 io active owner / io ticks 可判断 SDIO 是否成为瓶颈", fs=10.2)
    save(fig, "fig_sdio.png")


# ------------------------------------------------------- fig 9 page lifecycle
def fig_page():
    fig, ax = new_ax(12, 6.0)
    h = H(ax)
    box(ax, 10, h * 0.80, 15, 8.5, "未创建\nGUI_PAGE_NONE", fill=E_FILL, edge=E_EDGE, fs=10.3)
    box(ax, 33, h * 0.80, 21, 8.5, "已注册\nPage_Create(id, init, cb)", fs=10.3)
    box(ax, 62, h * 0.80, 20, 8.5, "显示中\nPage_Show(id) / _Anim()", fill=G_FILL, edge=G_EDGE, bold=True, fs=10.3)
    box(ax, 62, h * 0.45, 20, 8.5, "运行中\nLVGL 事件回调", fill=D_FILL, edge=DARK, bold=True, fs=10.3)
    box(ax, 33, h * 0.45, 21, 8.5, "返回上一页\nPage_Back()（历史栈弹出）", fs=10.3)
    box(ax, 10, h * 0.45, 15, 8.5, "已删除\nPage_Delete(id)", fill=E_FILL, edge=E_EDGE, fs=10.3)
    box(ax, 88, h * 0.62, 17, 9.5, "页面历史栈\n深度 8\nGUI_Manage 维护", fill=N_FILL, edge=ACCENT, fs=9.5, bold=True)
    arrow(ax, (17.7, h * 0.80), (22.3, h * 0.80), label="注册", lofs=(0, 3.4))
    arrow(ax, (43.7, h * 0.80), (51.8, h * 0.80), label="显示", lofs=(0, 3.4))
    arrow(ax, (62, h * 0.755), (62, h * 0.493), label="进入交互", lofs=(-9.5, 0), fs=9.8)
    arrow(ax, (51.8, h * 0.45), (43.7, h * 0.45), label="按返回", lofs=(0, 3.4))
    arrow(ax, (33, h * 0.493), (10, h * 0.493), rad=0)
    arrow(ax, (10, h * 0.493), (10, h * 0.755), rad=0, label="再次进入时重建", lofs=(12.5, 0), fs=9.8)
    arrow(ax, (72.2, h * 0.68), (79.3, h * 0.66), rad=0.0, color=ACCENT, ls="--", label="Page_Show 入栈", lofs=(-6, 4.2), fs=9.6)
    arrow(ax, (79.3, h * 0.57), (43.7, h * 0.49), rad=-0.15, color=ACCENT, ls="--", label="Page_Back 出栈", lofs=(14, -1), fs=9.6)
    note(ax, 50, h * 0.13, 94, 9,
         "页面上下文优先从 SRAMEX 动态分配，删除时归还；历史栈记录访问顺序，Page_Back 依栈回退（深度 8）\n"
         "GUI_Task 负责页面编排（创建/显示/删除），与 LVGL 渲染任务分离", fs=10.3)
    save(fig, "fig_page.png")


# ------------------------------------------------------ fig 10 LVGL handshake
def fig_handshake():
    fig, ax = new_ax(12, 5.8)
    h = H(ax)
    lanes = [("独占任务\nVideo / NES / AXF", DARK), ("LVGL 任务\nlvgl_timer_task", DARK2), ("LCD + SPI1", DARK2)]
    lx = [16, 52, 86]
    for (t, c), x in zip(lanes, lx):
        box(ax, x, h * 0.95, 22, 7, t, fill=c, edge=c, tc=WHITE, bold=True, fs=10.3)
    y0, y1_ = h * 0.90, h * 0.20
    for x in lx:
        line(ax, (x, y0), (x, y1_), color="#B6C2D1", lw=1.2, ls=(0, (4, 3)))
    msgs = [
        (0, 1, "lvgl_stop_ui()：请求暂停渲染"),
        (1, 0, "完成当前帧后置 LVGL_UI_FLAG_PAUSED"),
        (0, 2, "获得 LCD/SPI1 所有权，直接写屏（DMA）"),
        (2, 0, "传输完成"),
        (0, 1, "lvgl_start_ui()：清除 PAUSED"),
        (1, 0, "恢复 1 ms 周期渲染"),
    ]
    gap = (y0 - y1_) / (len(msgs) + 1)
    for i, (a, b, t) in enumerate(msgs):
        y = y0 - gap * (i + 1)
        arrow(ax, (lx[a], y), (lx[b], y), color=ACCENT, lw=1.5, mut=13)
        ax.text((lx[a] + lx[b]) / 2, y + 1.7, t, ha="center", fontsize=10.5, fontproperties=FONT, color=INK)
    note(ax, 50, 5.4, 94, 6.8,
         "等待确认后才会触碰 SPI1 —— 独占方绝不绕过握手直接操作 LCD；GUI 任务在暂停期间仅维护状态，不产生渲染压力", fs=10.2)
    save(fig, "fig_handshake.png")


# ------------------------------------------------------------- fig 11 IAP FSM
def fig_iap():
    fig, ax = new_ax(12, 6.2)
    h = H(ax)
    box(ax, 10, h * 0.78, 15, 8.5, "IDLE (0x00)\n无升级记录", fill=E_FILL, edge=E_EDGE, fs=10.3)
    box(ax, 36, h * 0.78, 21, 8.5, "READY (0xAA)\n暂存区就绪", fill=D_FILL, edge=DARK, bold=True, fs=10.3)
    box(ax, 65, h * 0.78, 21, 8.5, "PROGRAMMING (0x55)\nBootLoader 烧写中", fill=D_FILL, edge=DARK, bold=True, fs=10.3)
    box(ax, 91, h * 0.78, 15, 8.5, "升级完成\n清除记录→跳 App", fill=G_FILL, edge=G_EDGE, bold=True, fs=10.3)
    arrow(ax, (17.7, h * 0.78), (25.3, h * 0.78), color=ACCENT)
    ax.text(21.5, h * 0.845, "设置页 Install Update\nCRC32 校验 → 写 W25Q64 暂存区\n写 EEPROM 记录", ha="center",
            fontsize=9.8, fontproperties=FONT, color=INK)
    arrow(ax, (46.7, h * 0.78), (54.3, h * 0.78), color=ACCENT)
    ax.text(50.5, h * 0.845, "重启\nBootLoader 接管", ha="center", fontsize=9.8,
            fontproperties=FONT, color=INK)
    arrow(ax, (75.7, h * 0.78), (83.3, h * 0.78), color=ACCENT)
    ax.text(79.5, h * 0.845, "擦除 App 扇区\n暂存区烧写 + CRC", ha="center", fontsize=9.8,
            fontproperties=FONT, color=INK)
    box(ax, 50, h * 0.30, 46, 11,
        "断电恢复：上电后 BootLoader 读取 EEPROM 双槽记录\n（SLOT0 @128 / SLOT1 @168，各 36 B，magic “BLI2” v2）\n取序列号新的有效槽 → READY 则继续烧写，PROGRAMMING 则从头重烧",
        fill=NOTE_FILL, edge=ACCENT, fs=9.5, tc=INK)
    arrow(ax, (65, h * 0.735), (58, h * 0.358), rad=0.12, color=ACCENT, ls="--")
    box(ax, 14, h * 0.30, 24, 11, "校验失败 →\n从 W25Q64 备份区恢复\n（0x700000，1 MB）", fill=E_FILL, edge=E_EDGE, fs=10.2)
    arrow(ax, (27, h * 0.30), (36.5, h * 0.30), rad=0, color=E_EDGE, ls="--")
    note(ax, 50, h * 0.08, 94, 5.6,
         "升级期间断电不会变砖：记录保证 BootLoader 下次上电必然接管，直至 App 向量表与 CRC 双重校验通过才放行", fs=10.2)
    save(fig, "fig_iap.png")


# ------------------------------------------------------------- fig 12 AXF FSM
def fig_axf():
    fig, ax = new_ax(12, 6.2)
    h = H(ax)
    box(ax, 8, h * 0.82, 12, 7.5, "IDLE", fill=E_FILL, edge=E_EDGE, bold=True, fs=10.5)
    box(ax, 27, h * 0.82, 15, 9.5, "LOADING\ndl_load_file()\nELF 解析·重定位", fs=10.2)
    box(ax, 50, h * 0.82, 16, 9.5, "STARTING\n按档位分配栈\n4/8/12/16/24 KB", fs=10.2)
    box(ax, 74, h * 0.82, 14, 9.5, "RUNNING\n应用线程 +\n监控线程", fill=G_FILL, edge=G_EDGE, bold=True, fs=10.2)
    box(ax, 93, h * 0.82, 11, 9.5, "STOPPING\n回收资源", fill=D_FILL, edge=DARK, bold=True, fs=10.2)
    for x1, x2 in [(14.2, 19.3), (34.7, 41.8), (58.3, 66.8), (81.3, 87.3)]:
        arrow(ax, (x1, h * 0.82), (x2, h * 0.82))
    ax.text(27, h * 0.90, "选择 .axf", ha="center", fontsize=10, fontproperties=FONT, color=ACCENT)
    ax.text(93, h * 0.94, "PB7 / 运行结束", ha="center", fontsize=10, fontproperties=FONT, color=ACCENT)
    box(ax, 50, h * 0.48, 62, 11.5,
        "LOADING 细节：读 ELF 段 → 分配运行内存（SRAMEX）→ dl_arch 重定位\n"
        "→ 绑定宿主向量：SVC 8 调 C 标准库（22 个函数），SVC 扩展调 SG_* 设备 API\n"
        "→ entry 入口取得后创建应用线程与监控线程",
        fill=NOTE_FILL, edge=ACCENT, fs=9.5, tc=INK)
    box(ax, 14, h * 0.48, 20, 9.5, "ERROR\n任意阶段失败\n提示并回收", fill=E_FILL, edge=E_EDGE, fs=10.2)
    arrow(ax, (27, h * 0.772), (20, h * 0.532), rad=0.1, color=E_EDGE, ls="--")
    note(ax, 50, h * 0.16, 94, 8.6,
         "STOPPING：dl_destroy_lib() 释放加载段 → sys_thread_stack_free() 回收栈 → 状态回 IDLE；\n"
         "宿主把 AXF 视为可回收运行时资源，监控线程负责退出请求与超时强收（ExternAPI_AXF_ForceEnd）", fs=10.2)
    save(fig, "fig_axf.png")


# ----------------------------------------------------------- fig 13 video pipe
def fig_video():
    fig, ax = new_ax(12, 6.6)
    h = H(ax)
    # video lane
    yv = h * 0.80
    ax.text(2.5, yv + 6.5, "视频通道", fontsize=10.5, fontproperties=FONT_B, color=DARK)
    box(ax, 11, yv, 15, 9.5, "AVI chunk\n00dc (MJPEG)\nAVI_Parser 解析", fs=9.8)
    box(ax, 31, yv, 15.5, 9.5, "MJPEG source\n8 KB\n主 SRAM 视频池", fs=9.8)
    box(ax, 52, yv, 17, 9.5, "LibJPEG 解码\n视频池 arena 38 KB\n条带 20 行", fill=D_FILL, edge=DARK, fs=9.8)
    box(ax, 73, yv, 15.5, 9.5, "RGB 行缓冲\n→ LCD DMA\n双 stripe", fs=9.8)
    box(ax, 92, yv, 13, 9.5, "ST7789\n320×240", fill=G_FILL, edge=G_EDGE, bold=True, fs=9.8)
    for x1, x2 in [(18.7, 23.1), (38.9, 43.3), (60.7, 65.1), (80.9, 85.3)]:
        arrow(ax, (x1, yv), (x2, yv), mut=12)
    # audio lane
    ya = h * 0.42
    ax.text(2.5, ya + 6.5, "音频通道", fontsize=10.5, fontproperties=FONT_B, color=DARK)
    box(ax, 11, ya, 15, 9.5, "AVI chunk\n01wb (PCM)", fs=9.8)
    box(ax, 33, ya, 17, 9.5, "PCM 解码\n预缓冲水位线\n低 16K / 高 28K", fs=9.8)
    box(ax, 56, ya, 16, 9.5, "kfifo 24 KB\nSRAMEX\n（欠载统计）", fill=D_FILL, edge=DARK, fs=9.8)
    box(ax, 78, ya, 16, 9.5, "I2S DMA\n双半缓冲补数", fs=9.8)
    box(ax, 94.2, ya, 9.6, 9.5, "DAC\nPCM5102", fill=G_FILL, edge=G_EDGE, bold=True, fs=9.8)
    for x1, x2 in [(18.7, 24.3), (41.7, 47.9), (64.3, 69.9), (86.3, 89.2)]:
        arrow(ax, (x1, ya), (x2, ya), mut=12)
    # sync
    box(ax, 50, h * 0.15, 78, 10,
        "音视频同步（Video.c 单任务状态机）：音频播放位置为系统时钟 → 逐帧计算 deadline\n"
        "解码完成早于 deadline → 准点送显；落后 → 丢帧追赶（drop late / prebuffer 统计）\n"
        "phase 序列：LAUNCH → OPEN → PLAY_LOOP { DEMUX_NEXT → JPEG_DECODE → VIDEO_WAIT_CLOCK → 送显 }",
        fill=NOTE_FILL, edge=ACCENT, fs=9.3, tc=INK)
    arrow(ax, (94.2, ya + 4.9), (89, h * 0.202), rad=-0.1, color=ACCENT, ls="--")
    ax.text(96.5, h * 0.30, "时钟", fontsize=10, fontproperties=FONT_B, color=ACCENT)
    arrow(ax, (52, h * 0.745), (52, h * 0.202), rad=0, color=ACCENT, ls="--")
    ax.text(54, h * 0.47, "帧 deadline 判定", fontsize=10, fontproperties=FONT_B, color=ACCENT)
    save(fig, "fig_video.png")


if __name__ == "__main__":
    fig_clock()
    fig_nes()
    fig_music()
    fig_font()
    fig_mem()
    fig_boot()
    fig_storage()
    fig_sdio()
    fig_page()
    fig_handshake()
    fig_iap()
    fig_axf()
    fig_video()
    print("all done")
