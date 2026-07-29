# GUI SRAM 优化方案

> 历史说明：本文的 48 KB普通 SRAM / 80 KB视频池 / CCM 栈描述已被新内存规划取代。当前地址划分和分配规则见 [内存规划重构.md](内存规划重构.md)。GUI 上下文迁移到 `SRAMEX` 的原则仍然有效。

## 结论

`GUI_IMAGE`、`GUI_FONT`、`GUI_TEXT`、`GUI_NES`、`GUI_PLAYER` 占用主 128KB SRAM 高的主因不是 LVGL 对象、图片解码缓冲、文本渲染缓冲或线程栈，而是这些模块把完整页面上下文定义成了文件级 `static` 全局变量，且每个上下文都内嵌一份 `GUI_FileManage_t`。

当前链接脚本 `System/StarGamer_CCM.sct` 将所有 `+RW +ZI` 放入：

```text
RW_IRAM1 0x20000000 0x00020000  {
  .ANY (+RW +ZI)
}
```

因此 `static GUI_ImageContext_t s_image;`、`static GUI_FontContext_t s_font;`、`static GUI_TextContext_t s_text;`、`static GUI_NES_Context_t s_nes;`、`static GUI_Player_Context_t s_player;` 这类对象全部常驻 0x20000000 起始的主内部 SRAM。

按当前结构估算，单个 `GUI_FileManage_t` 约 12.6KB；上述五个页面每页约 13KB，合计约 65KB+ 主 SRAM 常驻，占 128KB 的一半左右。

## 当前内存池现状

| 内存区 | 当前用途 | 结论 |
|---|---:|---|
| 主 SRAM 0x20000000，128KB | `.data/.bss`、FreeRTOS `ucHeap[1024]`、普通静态全局对象 | 当前 GUI 页面上下文主要压这里 |
| CCM 0x10000000，64KB | `sys_thread_stack_alloc()` 分配线程栈 | 不适合继续塞 GUI 大状态；也不能放 DMA 缓冲 |
| 外部 SRAM 0x68000000，1MB | `sys_malloc(SRAMEX, ...)`、LVGL 自定义堆、帧缓冲、大 I/O 缓冲 | 应承载 GUI 页面状态和文件列表缓存 |

已确认的正确设计：

- `System/Malloc.c` 中 `sys_thread_stack_alloc()` 分配到 `SRAMCCM`。
- `System/Malloc.h` 中 `LVGL_MEM_POOL` 为 `SRAMEX`，LVGL 动态对象走外部 SRAM。
- `GUI_Image` 解码结果、行缓冲、`FIL` 已用 `SRAMEX`。
- `GUI_Font` 安装 I/O/verify/bitmap 缓冲已用 `SRAMEX`。
- `GUI_Text` I/O 和渲染缓冲已用 `SRAMEX`。

所以优化重点应放在 `.bss` 静态页面上下文和文件管理器缓存，而不是盲目削线程栈或 LVGL 堆。

## 主要占用来源

### 1. `GUI_FileManage_t` 体积过大且被每页复制

`GUI/GUI_FileManage.h` 中：

```c
typedef struct
{
    GUI_FileManage_t *manager;
    GUI_FileManageItemType_t type;
    char name[GUI_FILEMANAGE_NAME_SIZE];    /* 96 */
    char path[GUI_FILEMANAGE_PATH_SIZE];    /* 256 */
    u32 size;
    u8 attr;
} GUI_FileManageItem_t;

struct GUI_FileManage
{
    ...
    GUI_FileManageItem_t items[GUI_FILEMANAGE_MAX_ITEMS]; /* 32 */
    DIR dir;
    char root_path[GUI_FILEMANAGE_PATH_SIZE];    /* 256 */
    char current_path[GUI_FILEMANAGE_PATH_SIZE]; /* 256 */
    char pending_path[GUI_FILEMANAGE_PATH_SIZE]; /* 256 */
    char dir_path[GUI_FILEMANAGE_PATH_SIZE];     /* 256 */
    ...
};
```

在 ARM 32-bit ABI 下，`GUI_FileManageItem_t` 约 368B：

- `name[96]`
- `path[256]`
- 指针、枚举、size、attr 和对齐填充

因此：

```text
items[32] ~= 32 * 368B = 11776B ~= 11.5KB
四个路径缓存 ~= 1024B
DIR + LVGL 指针 + config + 状态 ~= 数百字节
GUI_FileManage_t 总体 ~= 12.6KB
```

### 2. 五个页面上下文均内嵌 `GUI_FileManage_t`

| 模块 | 静态对象 | 关键问题 | 估算主 SRAM 常驻 |
|---|---|---|---:|
| `GUI_Image.c` | `static GUI_ImageContext_t s_image;` | 内嵌 `GUI_FileManage_t`，另有路径/状态/解码描述符 | ~13.5KB |
| `GUI_Font.c` | `static GUI_FontContext_t s_font;` | 内嵌 `GUI_FileManage_t`，另有安装路径/状态 | ~13.3KB |
| `GUI_Text.c` | `static GUI_TextContext_t s_text;` | 内嵌 `GUI_FileManage_t`，另有当前路径/历史记录 | ~13.4KB |
| `GUI_NES.c` | `static GUI_NES_Context_t s_nes;` | 内嵌 `GUI_FileManage_t`，另有 ROM 路径 | ~13.2KB |
| `GUI_Player.c` | `static GUI_Player_Context_t s_player;` | 内嵌 `GUI_FileManage_t`，另有当前路径/播放控件指针 | ~13.3KB |

合计约 65KB+ 主 SRAM 常驻。

注意：`GUI_Font` 即使不是 `GUI_Main_Init()` 启动时注册，只要 `GUI_Font.o` 被链接，`static s_font` 仍然进入 `.bss`。是否调用 `GUI_Font_Register()` 不改变静态全局对象占用。

### 3. `GUI_Main_Init()` 当前会提前注册多个页面

`GUI/GUI_Main.c` 当前启动时注册：

- `GUI_NES_Register()`
- `GUI_Arduboy_Register()`
- `GUI_Player_Register()`
- `GUI_Image_Register()`
- `GUI_Text_Register()`
- `GUI_Settings_Register()`

这会提前创建页面和 LVGL 对象。LVGL 动态对象走 `SRAMEX`，所以它不是主 SRAM 的第一大问题；但提前注册会增加启动时外部 SRAM 压力和初始化耗时。

### 4. FatFs LFN 也有运行时主 SRAM 风险

`FATFS/Target/ffconf.h`：

```c
#define _USE_LFN     3
#define _MAX_LFN     255
#define ff_malloc    pvPortMalloc
#define ff_free      vPortFree
```

`_USE_LFN=3` 时每次 FatFs 长文件名操作会申请约 `(_MAX_LFN + 1) * 2 = 512B` 工作区。当前 FreeRTOS `configTOTAL_HEAP_SIZE` 只有 1024B，且 `heap_4.c` 的 `ucHeap[1024]` 位于主 SRAM。

这不是页面静态常驻的第一大项，但会造成运行时文件浏览、图片打开、文本打开时的主 SRAM 堆压力。两个并发 FatFs 操作或同步对象动态分配就可能接近上限。

## 优化方案优先级

## P0：将五个页面上下文从 `.bss` 改为 SRAMEX 运行期分配

收益最大，改动风险相对可控。目标是把：

```c
static GUI_ImageContext_t s_image;
```

改为：

```c
static GUI_ImageContext_t *s_image;

static GUI_ImageContext_t *GUI_Image_GetContext(void)
{
    if (s_image == NULL)
    {
        s_image = (GUI_ImageContext_t *)sys_malloc(SRAMEX, sizeof(GUI_ImageContext_t));
        if (s_image != NULL)
        {
            memset(s_image, 0, sizeof(GUI_ImageContext_t));
        }
    }
    return s_image;
}
```

同类修改应用到：

- `GUI_Image.c`
- `GUI_Font.c`
- `GUI_Text.c`
- `GUI_NES.c`
- `GUI_Player.c`

建议策略：

1. 每个模块保留一个 4 字节静态指针在主 SRAM。
2. 页面上下文首次 `Register()` 或 `Show()` 时从 `SRAMEX` 分配。
3. 分配后不急着释放，避免 LVGL timer、event callback、异步线程还有悬挂指针。
4. 后续如果要进一步释放外部 SRAM，再给 `Page_Manager` 增加页面析构回调统一处理。

预期收益：

```text
主 SRAM 释放：约 65KB+
SRAMEX 增加：约 65KB+
```

注意事项：

- 只能在外部 SRAM 初始化完成后分配。当前 `System_init()` 会初始化 `SRAMEX`，GUI 初始化在其后执行，此路径可行。
- 不要直接把这些对象通过 scatter 文件放到 0x68000000 的 `.bss`。C runtime 清零 `.bss` 发生在 FSMC/外部 SRAM 初始化之前，除非重写启动流程，否则会出错。
- 不建议迁到 CCM。CCM 只有 64KB，当前用于线程栈池，而且 DMA 不能访问。
- `osStaticThreadDef_t`、`osThreadAttr_t` 等小对象可以暂时保留在主 SRAM，收益不大；TCB 高频访问，留在内部 SRAM也合理。

## P1：压缩 `GUI_FileManage_t`，移除每个列表项里的完整路径

当前 `items[32]` 中每项保存 `path[256]`，这是最大浪费。文件浏览器已有 `current_path`，列表项只需要保存文件名和类型。打开文件或进入目录时再拼接路径即可。

建议把：

```c
typedef struct
{
    GUI_FileManage_t *manager;
    GUI_FileManageItemType_t type;
    char name[GUI_FILEMANAGE_NAME_SIZE];
    char path[GUI_FILEMANAGE_PATH_SIZE];
    u32 size;
    u8 attr;
} GUI_FileManageItem_t;
```

改为：

```c
typedef struct
{
    GUI_FileManage_t *manager;
    GUI_FileManageItemType_t type;
    char name[GUI_FILEMANAGE_NAME_SIZE];
    u32 size;
    u8 attr;
} GUI_FileManageItem_t;
```

然后新增路径构造 API：

```c
u8 GUI_FileManage_BuildItemPath(const GUI_FileManage_t *manager,
                                const GUI_FileManageItem_t *item,
                                char *dst,
                                u32 dst_size);
```

页面回调中原来使用：

```c
item->path
```

改为：

```c
char path[GUI_FILEMANAGE_PATH_SIZE];

if (GUI_FileManage_BuildItemPath(manager, item, path, sizeof(path)) == 0U)
{
    return OPERATE_FAILED;
}
```

如果担心 256B 栈占用，可以使用 `manager->pending_path` 或新增 `manager->selected_path[256]` 作为路径临时区，避免在 GUI 线程栈上放大数组。

预期收益：

```text
每个 GUI_FileManage_t 释放：256B * 32 = 8192B
五个页面合计释放：约 40KB
```

如果 P0 已完成，这 40KB 主要释放的是 `SRAMEX`；如果 P0 未做，则直接释放主 SRAM。

## P2：按页面需求降低 `GUI_FILEMANAGE_MAX_ITEMS`

当前屏幕 320x240，列表高度约 146px，行高 30px，实际同时显示约 4~5 行。`GUI_FILEMANAGE_MAX_ITEMS=32` 偏大。

可选方案：

- 全局改为 16：代码简单，单个文件管理器节省约 5.75KB（按当前含 path 结构估算）。
- 每个页面独立配置：例如 NES/Image/Text/Font/Music 都用 16，保留需要快速浏览的页面为 32。
- 若 P1 已移除 `path[256]`，`MAX_ITEMS` 的收益下降，但仍可减少扫描和 LVGL 列表对象创建量。

建议顺序：先做 P1，再评估是否需要降到 16。否则容易同时改变内存和交互体验，验证成本更高。

## P3：页面懒注册，减少启动时 SRAMEX 和初始化压力

当前主菜单点击事件直接：

```c
Page_Show_Anim(item->page_id, ...);
```

因此 `GUI_Main_Init()` 必须提前注册页面。可以改为按页面 ID 调用对应 `GUI_*_Show()`，让页面首次打开时再注册：

```c
static int GUI_Main_ShowPage(Page_ID_t id)
{
    switch (id)
    {
        case GUI_PAGE_NES:
            return GUI_NES_Show();
        case GUI_PAGE_MUSIC:
            return GUI_Player_Show();
        case GUI_PAGE_IMAGE:
            return GUI_Image_Show();
        case GUI_PAGE_TEXT:
            return GUI_Text_Show();
        case GUI_PAGE_SETTINGS:
            return GUI_Settings_Show();
        default:
            return Page_Show_Anim(id, LV_SCR_LOAD_ANIM_MOVE_LEFT, 120U, 0U);
    }
}
```

然后主菜单事件中调用 `GUI_Main_ShowPage(item->page_id)`。

这个方案单独做不能释放 `.bss` 中的静态 `s_*`，但结合 P0 后可以做到：

- 未打开的页面不分配页面上下文。
- 未打开的页面不创建 LVGL screen/object。
- 启动更快，外部 SRAM 初始压力更低。

## P4：将 FatFs LFN 工作区从 FreeRTOS heap 改到 SRAMEX

当前 `_USE_LFN=3` 使用 `ff_malloc -> pvPortMalloc`，会消耗 FreeRTOS heap，也就是主 SRAM 中的 `ucHeap[1024]`。

建议改为 SRAMEX 分配。可选方式：

方案 A：修改 `ffconf.h` 宏：

```c
#include "Malloc.h"

#define ff_malloc(size)  sys_malloc(SRAMEX, (u32)(size))
#define ff_free(ptr)     sys_free(SRAMEX, (ptr))
```

方案 B：修改 `Middlewares/Third_Party/FatFs/src/option/syscall.c` 中 `ff_memalloc()` / `ff_memfree()`：

```c
void* ff_memalloc(UINT msize)
{
    return sys_malloc(SRAMEX, (u32)msize);
}

void ff_memfree(void *mblock)
{
    sys_free(SRAMEX, mblock);
}
```

建议优先方案 B，语义更明确；但要注意 CubeMX 或中间件更新是否会覆盖文件。

预期效果：

- FatFs LFN 工作区不再挤占主 SRAM FreeRTOS heap。
- 文件浏览、文本打开、图片打开时更稳定。
- `ucHeap[1024]` 仍然常驻主 SRAM；后续若确认没有动态 FreeRTOS 对象，再考虑缩小或取消动态分配。

## P5：增加页面析构能力，真正按需释放 SRAMEX

P0 建议“分配一次、不释放”，这是为了快速释放主 SRAM 并降低悬挂指针风险。如果后续 SRAMEX 也吃紧，可以扩展页面管理器：

```c
typedef void (*PageDestroy)(Page_Struct_t *page, void *user_data);
```

页面删除时执行：

1. 停止 LVGL timer。
2. 等待异步线程退出，例如 ImageDecode、FontInstall、NES_Run。
3. 释放页面私有缓冲和上下文。
4. 删除 LVGL screen。

需要特别小心的模块：

- `GUI_Image`：解码线程可能还在写 `pending_decoded`。
- `GUI_Font`：安装线程可能正在操作 W25Q64 和进度状态。
- `GUI_NES`：NES 运行线程和音频/输入状态需要先停。
- `GUI_Player`：不能在播放中释放 player 页面状态，至少要停止轮询 timer。

此项收益是释放 SRAMEX，不是优先解决主 SRAM；建议放到 P0/P1/P4 之后。

## 不建议的方案

### 不建议把 GUI 大对象放到 CCM

CCM 不能被 DMA 访问，且只有 64KB。当前线程栈已经使用 CCM：

- defaultTask 4KB
- LVGL 8KB
- ImageDecode 8KB
- NES_Run 8KB
- FontInstall 4KB
- key/shell/idle/timer 等

把 65KB GUI 上下文搬进 CCM 会挤爆线程栈池，方向错误。

### 不建议直接 scatter 到外部 SRAM `.bss`

外部 SRAM 需要 FSMC 初始化后才能访问，而 C runtime 在进入 `main()` 前会清零 `.bss`。如果没有改启动文件，在 scatter 文件里把普通 ZI 段放到 0x68000000 很可能启动即异常。

运行期 `sys_malloc(SRAMEX, ...)` 是更安全的路径。

### 不建议优先削线程栈

线程栈当前来自 CCM，不是主 128KB SRAM 的主要占用。可以用 `uxTaskGetStackHighWaterMark()` 做后续优化，但它不能解决本问题的主矛盾。

### 不建议使用 `-fshort-enums` 这类全局 ABI 选项

即使能节省部分枚举字段填充，收益远小于移除 `path[256]` 和迁移上下文，而且可能影响 HAL/LVGL/FatFs ABI，风险不成比例。

## 推荐实施顺序

1. P0：五个页面上下文改为 SRAMEX 懒分配。
2. 构建并检查 map，确认 `RW_IRAM1` 降低约 60KB+。
3. P4：FatFs LFN 工作区改到 SRAMEX，降低运行时 FreeRTOS heap 压力。
4. P1：移除 `GUI_FileManageItem_t.path[256]`，新增路径构造 API。
5. P2：根据体验把 `GUI_FILEMANAGE_MAX_ITEMS` 从 32 降到 16 或做页面级配置。
6. P3：主菜单改为页面懒注册，减少启动时 SRAMEX 和 LVGL 对象压力。
7. P5：如果 SRAMEX 也出现压力，再扩展页面析构和释放策略。

## 验证方法

### 1. map 文件验证

构建后检查 map 文件中这些符号是否还在 `RW_IRAM1`：

- `s_image`
- `s_font`
- `s_text`
- `s_nes`
- `s_player`

P0 完成后，主 SRAM 中应只剩对应指针，完整上下文不应再作为大块 `.bss` 出现在 `RW_IRAM1`。

预期：

```text
Before: RW_IRAM1 中五个 GUI 上下文约 65KB+
After P0: RW_IRAM1 减少约 65KB，SRAMEX 运行期增加约 65KB
After P1: 每个文件管理器再减少约 8KB
```

### 2. 运行时验证

建议增加或复用调试命令输出：

- `SRAMCCM` 剩余空间和最大连续块。
- `SRAMEX` 剩余空间和最大连续块。
- FreeRTOS heap 剩余空间。
- 关键线程 `uxTaskGetStackHighWaterMark()`。

重点场景：

1. 开机进主菜单。
2. 打开 NES 列表并进入/退出 ROM。
3. 打开 Music 并播放/停止 WAV。
4. 打开 Image 并解码最大 320x240 JPEG。
5. 打开 Text 并翻页。
6. 打开 Font 安装页面并执行校验/安装。
7. 多次在页面间切换，确认 SRAMEX 无持续泄漏。

## 最终目标

合理目标：

```text
主 128KB SRAM：释放 60KB 以上，保留给 HAL/FatFs/FreeRTOS 小对象和其他静态状态。
CCM 64KB：继续专用于线程栈，避免 DMA 和大 GUI 状态进入。
SRAMEX 1MB：承载 GUI 页面状态、LVGL 对象、文件列表、图片/文本/字体/NES 大缓冲。
```

核心原则：

```text
小而频繁访问的控制状态可留内部 SRAM；
大块、非 DMA、非实时强依赖的数据迁到 SRAMEX；
线程栈继续走 CCM；
DMA 缓冲绝不放 CCM。
```
