# eprj3 工程技术文档读写指南（嘉立创EDA专业版）

面向需要**直接读懂、脚本化修改、或让 AI 代理生成**工程文件的人。结论优先，字段细节以编辑器自带的格式参考为准（见 §7）。

本文的对象是 `.eprj3`（文件夹工程）。旧版 `.eprj` / `.eprj2` 是单文件 SQLite，不在本文范围。

---

## 1. 一句话结构

```
<工程名>/
├── <工程名>.eprj3          # 纯 JSON：工程索引 + 板/原理图/PCB 的 uuid 与排序
├── sch/<原理图名>/
│   ├── <图页名>.esch2       # 原理图图页源码（同文件内还内嵌符号/封装/器件库文档）
│   ├── <原理图名>.ecfg      # 该原理图的设计规则与配置
│   └── <原理图名>.evar      # 装配变量（可能不存在）
├── pcb/<PCB名>.epcb2       # PCB 源码（同文件内还内嵌符号/封装/器件库文档）
└── panel/<面板名>.epan2    # 拼板/面板源码（可选）
```

实测（`hardware/pcb/robot_electronics_v4_100x100_2layer/project`）：

- `.eprj3` 是 5.5 KB 的 JSON，含 `profile.boards/schematics/sheets/pcbs` 四张表，键是 uuid，值是 `{title, board, zIndex, version, updateTime}`；`format` 字段为 `"folder"`。
- `Controller_V2.epcb2` 2.69 MB / 7969 行，含 **82 个 DOCHEAD**：1 个 `PCB` + 30 个 `SYMBOL` + 21 个 `FOOTPRINT` + 30 个 `DEVICE`。**工程库是内嵌的**，没有独立库文件。
- 文件名即文档名；重命名文件 = 重命名文档。

---

## 2. 记录语法（唯一的硬规则）

每个 `.esch2` / `.epcb2` / `.epan2` 都是**一行一条 JSON 记录**：

```
{"type":"LINE","ticket":587,"id":"658a61a828822414"}||{"netName":"STM_PF12","layerId":2,...}|
└──────────────── 外壳 / shell ────────────────┘  └──────── 载荷 / payload ────────┘
```

1. `||` 是外壳与载荷的分界，**行尾还有一个 `|` 表示记录结束**，之后是换行。
2. 换行必须是 **LF**，不是 CRLF。
3. 外壳键序惯例为 `type → ticket → id`（编辑器就是这样写的），但解析时不依赖顺序。
4. 载荷是**已删除**时写空串：`{"type":"STRING","ticket":10238,"id":"..."}|||`（`||` 后直接跟行尾 `|`）。
5. 实测注意：本仓库导出的文件**最后一行常常缺少结尾的 `|`**（编辑器能容忍），解析器应容忍而不是报错。

外壳字段：

| 字段 | 含义 |
|---|---|
| `type` | 记录类型，决定载荷语义 |
| `id` | 文档内唯一标识（形态见 §4） |
| `ticket` | 逻辑时钟，用于最终一致性比较 |
| `client` / `firstTicket` / `pend` / `num` | 框架附加信息，原样透传即可，别当坏数据 |

---

## 3. 最终一致性：为什么"只能追加，不能改写"

文件是一份**追加式日志**，不是快照。同一个对象可以出现多次：

- 规则：`type` 与 `id` 都相同 → 保留 **`ticket` 更大**的那条；
- `ticket` 也相同 → 保留 **`client` 更小**的那条文档头之后的数据；
- 删除 = 追加一条载荷为空串的记录（**历史记录不会被物理抹掉**）；
- 整个文档删除 = 追加 `{"type":"DELETE_DOC"}||{"isDelete":true}|`。

由此得到三条工程结论：

1. **新增/修改对象 = 追加一行，`ticket` 取当前文档最大值 +1**，不要去改旧行。
2. **不要用"行号"定位对象**；定位键是 `(type, id)`。
3. 想让日志"变干净"（去掉删除痕迹）只能用编辑器的**克隆工程**，手工删行会破坏一致性。

`DOCHEAD` 划分文档边界：遇到 `DOCHEAD` 就把后续记录归到该文档，直到下一个 `DOCHEAD`。所以一个 `.epcb2` 里是"多个文档首尾相接"，不是"一个文档".

---

## 4. `id` 的四种形态（判断错了就写不进去）

| 形态 | 例子 | 用在哪 |
|---|---|---|
| 随机 16 位小写 hex | `"658a61a828822414"` | 绝大多数图元：LINE / PAD / VIA / COMPONENT / POUR |
| 固定单例名 | `"CANVAS"` `"PREFERENCE"` `"PANELIZE"` `"RULE_TEMPLATE"` `"D3_ATTRIBUTE"` | 一份文档只有一条的配置 |
| 键式（JSON 数组串） | `["RULE","TRACK","Signal"]`<br>`["NET","GND"]`<br>`["PAD_NET","<元件id>","2","e7"]`<br>`["POURED","<POUR id>"]` | 靠键区分的集合型数据 |
| 复合（前缀拼接） | ATTR：`<元件id>` + `e15` → `"8b03d289a7c042acbfce15"` | 封装/元件内的子图元 |

实测：设计规则记录的 `id` 是**数组串**，且 `ruleType` / `ruleName` **只存在于 id 里，载荷中没有**：

```json
{"type":"RULE","ticket":240,"id":"[\"RULE\",\"TRACK\",\"Signal\"]"}||
{"ruleState":"DEFAULT","ruleContext":{"unit":"mm","track":{"isOpen":true,"content":[{"layerId":1,"stroMin":5,"stroDef":10,"stroMax":100}]}}}|
```

这与官方格式文档里"载荷含 `ruleType`/`ruleName`"的写法不一致——**以真机文件为准**：写盘时字段被编进 id，读盘时再从 id 还原。

---

## 5. 单位与坐标系（最容易错的地方）

| 域 | 长度单位 | 换算 | 角度 | Y 轴 |
|---|---|---|---|---|
| 原理图 `.esch2` / 符号 | **0.01 inch**（=10 mil） | ×0.254 → mm | 度，逆时针为正 | 文件内 Y 向上（带 `yAxisDirection:"up"`） |
| PCB `.epcb2` / 封装 | **1 mil** | ×0.0254 → mm | 度，逆时针为正 | 与 API 同向，不翻转 |

PCB 侧实测校准（`Controller_V2.epcb2`）：

- 板框 `POLY(layerId=11)` 坐标跨度 3937 单位 → **100.0 × 100.0 mm**；
- 过孔 `holeDiameter:11.811 / viaDiameter:23.6` → **0.30 / 0.60 mm**（嘉立创常规孔）；
- 走线 `width:10.05` → **0.255 mm**（≈10 mil 默认线宽）。

> ⚠️ **单位陷阱**：编辑器自带的格式参考里对"设计规则 `ruleContext` 的长度量纲"有一句"1 单位 = 10 mil"的说法，与真机数据矛盾。按上面三条校准，`ruleContext` 里的数值同样是 **mil**（如 `viaSize.minRadius:9.8425` → 0.25 mm 半径；`solderMaskExpansion.padTopExpan:2` → 0.05 mm 扩展）。写规则后务必跑 DRC 复核取值是否被当成 10 倍值。

> ⚠️ **只翻 Y 不翻 X**：原理图族（SCH/SCH_PAGE/SYMBOL）写盘时把 y 取反并打 `yAxisDirection:"up"`，读回来再取反；`FOOTPRINT`/`DEVICE`/`PCB` 段**整段原样透传，不翻**。同一个图元在 `POLY` 上空理图用 `points`、封装域用 `path`，同名不同义。

---

## 6. 关键记录类型速查

### 6.1 原理图（`.esch2`）

| type | 作用 | 要点 |
|---|---|---|
| `DOCHEAD` | 文档头 | `docType` ∈ SYMBOL / FOOTPRINT / DEVICE / BLOB / SCH_PAGE |
| `COMPONENT` | 元件实例 | `partId` 指向 **SYMBOL 文档里 `PART` 行的 id**；`rotation` = (360 − API角度) % 360 |
| `ATTR` | 属性（位号/值/网络名/引脚名） | 靠 `parentId` 挂宿主；`Symbol` / `Device` 两个 key 指向文档 uuid |
| `WIRE` / `BUS` | 导线组 / 总线组 | 自身**没有坐标**，只是组壳 |
| `LINE` | 单线段 | 靠 `lineGroup` 挂到 WIRE/BUS；`startX==endX || startY==endY` |
| `PIN` / `TEXT` / `RECT` / `CIRCLE` / `ARC` / `POLY` / `TABLE` | 图形 | 圆弧是**三点**（start + refer + end），不是角度 |
| `PART` | 部件 | **只存在于 SYMBOL 文档**，图页里一个都没有 |

### 6.2 PCB（`.epcb2`）

| type | 作用 | 要点 |
|---|---|---|
| `LAYER` / `LAYER_PHYS` | 层与叠层 | `status` 位掩码：1 使用 / 2 显示 / 4 锁定；**未启用的内层也会预先写记录** |
| `NET` | 网络属性 | **只有需要特殊类型/颜色时才写**，不是网络清单 |
| `COMPONENT` | 器件实例 | `layerId` 1 顶层 / 2 底层；`angle` 与 API 同向 |
| `ATTR` | 器件属性 | id = 元件 id + 局部 id（`e15`…） |
| `PAD_NET` | 焊盘→网络映射 | id = `["PAD_NET",元件id,焊盘号,焊盘局部id]` |
| `PAD` / `VIA` | 焊盘 / 过孔 | PAD 的 `layerId` 只能取 顶层/底层/多层 三者之一；过孔靠 `ruleName` 指向盲埋孔规则定起止层 |
| `LINE` / `ARC` / `POLY` / `FILL` / `REGION` | 走线与图形 | 板框是 `POLY` 且 `layerId=11`（或 `polyType:"BOARD_OUTLINE"`） |
| `POUR` → `POURED` | 覆铜边框 → 覆铜结果 | 结果是**缓存**，改边框后必须让编辑器重算；删 POUR 必须连带删 POURED |
| `RULE` / `RULE_SELECTOR` / `RULE_TEMPLATE` | 设计规则 | 见 §4、§8 |
| `PANELIZE` / `D3_ATTRIBUTE` / `PREFERENCE` | 拼板 / 3D 外观 / 操作偏好 | 单例；`PREFERENCE` 记的是"上次用的值"，**不是约束** |

---

## 7. 怎么"读"：三条路子

### 7.1 直接解析文本（最快，无需编辑器）

仓库自带脚本：

```powershell
python hardware/pcb/eprj3_inspect.py hardware/pcb/robot_electronics_v4_100x100_2layer/project
python hardware/pcb/eprj3_inspect.py <工程目录> --json
```

它解析 `.eprj3` 索引与全部 `.esch2/.epcb2`，输出板框尺寸、层、器件数、网络数、过孔数、走线总长、设计规则清单、每页位号；并用"取最大 ticket"还原当前有效数据。

实测输出（V4 工程）：

```
project      : RobotElectronicsV4_100x100_2Layer (format=folder, pcbs=2)
boards       : Controller_V2, Upper_Power_V2
== pcb/Controller_V2.epcb2
   PCB f2c322c938ba51f5
     board         : 100.0 x 100.0 mm at (0.0, 0.0)
     components=85 nets=161 vias=105 track=4410.4 mm
```

### 7.2 用编辑器自带的格式参考（最权威）

电子端 CLI 内置 `doc format`，按类型列字段，注释里写了真机行为（单位、id 形态、联动删除约束）：

```powershell
$L = 'E:\Program Files\lceda-pro\lceda-pro.exe'   # 4.1.71
& $L doc format                                   # 列出全部类型
& $L doc format --class-name TPcbBase             # PCB 公共字段 + 单位约定
& $L doc format --class-name TRuleContext         # 设计规则载荷
& $L doc format --class-name TMPcbComponent       # 器件实例线格式
```

### 7.3 用扩展 API 读（读回的是编辑器解码后的对象）

```powershell
& $L open --path 'E:/.../project/RobotElectronicsV4_100x100_2Layer.eprj3'
& $L invoke --session <id> --ext-uuid eda --code "return await eda.pcb_Net.getAllNetsName()"
```

`.eprj3` 工程打开后与编辑器**双向同步**：编辑器保存 → 磁盘立即更新；磁盘改动 → 编辑器重新加载。

---

## 8. 怎么"改"：三条路子与各自风险

### 8.1 首选：在编辑器里改

设计规则、层叠、禁布区、覆铜这些**带联动关系**的数据不要手写。改完 `Ctrl+S`，磁盘文件自动更新，再用 `eprj3_inspect.py` 复核数量。

### 8.2 次选：追加记录（保留历史）

只适用于**新增独立图元**（一条走线、一个丝印字、一个过孔），或**改写已有对象**：

```
1. 读出该文档当前最大 ticket
2. 追加一行，ticket = max + 1，id 新生成 16 位 hex
3. 行尾写 `|` + LF
4. 等 5–15 秒让编辑器重载，再跑 DRC / 数数量
```

改已有对象就写一条 `(type,id)` 相同、`ticket` 更大的记录，旧行留着不动。

### 8.3 兜底：整体重写文件（生成新工程时用）

- 工程骨架用 API 建（`dmt_Project.createProject` + `openProject`），不要手写 `.eprj3` 索引；
- `SYMBOL`/`FOOTPRINT`/`DEVICE` 文档**从编辑器实际导出里整段摘取**，不要手搓；
- 只手写业务记录（`COMPONENT`/`ATTR`/`WIRE`/`LINE`/`NET`/`PAD_NET`/`POLY`），书写顺序与编辑器一致：`SYMBOL → FOOTPRINT → DEVICE → BLOB → SCH_PAGE`；
- 三条约束：`partId` 必须命中某 SYMBOL 文档的 `PART` 行；`id` 唯一；`ticket` 文档内唯一，且**元件 ticket 小于其自身属性的 ticket**。

### 8.4 联动删除清单（只删主体会留下悬空引用）

| 删这个 | 必须一起删 |
|---|---|
| `COMPONENT`（PCB） | 它的 `ATTR` + `PAD_NET` |
| `WIRE` / `BUS` | 组内 `LINE` + 挂在该组上的 `ATTR` |
| `POUR` | 其 `POURED` 结果 |
| `X_NET_GROUP` | 组内全部 `X_NET` |
| 被泪滴引用的焊盘/过孔/走线 | 对应 `TEARDROP` |
| `COMPONENT`（原理图） | 它的 `ATTR` |

---

## 9. 校验清单（写完必跑）

1. 每行能被 `json.loads` 解析，且以 `|` + LF 结尾（**检查是否有 CRLF**）；
2. `(type,id)` 无意外重复；新记录的 `ticket` 大于旧值；
3. 引用完整：`partId` / `Symbol` / `Device` / `lineGroup` / `padId` 都能找到宿主；
4. 走线正交（原理图 `LINE`），`lineGroup` 指向存在的 `WIRE`；
5. 重新打开工程：器件数、网络数、走线数、`eprj3_inspect.py` 输出与预期一致；
6. 跑原生 DRC（原理图 DRC 与 PCB DRC），0 错误；
7. 导出网表并对比原理图网表（`PCB_Drc.check()` 会自动做这一步比对）；
8. 留证据：把返回 JSON 与截图存到 `evidence/`。

---

## 10. 常见坑

- **中文路径**：CLI 返回的 `path` 可能被按 GB18030 误解码。传路径用**正斜杠**，必要时用 `--args` 传值而不是拼命令串。
- **CRLF**：Windows 编辑器另存会把 LF 变 CRLF，编辑器可能整份不认。
- **BOM 头**：UTF-8 BOM 会让第一行 `{` 前多一个字符，解析失败。
- **规则单位**：见 §5 的 10 倍陷阱；改完必须 DRC。
- **`PAD.layerId` 只能三选一**，写成内层号会静默失效。
- **覆铜结果要重算**：只改 `POUR` 不改 `POURED`，画面还是旧铜皮。
- **未启用层也写记录**：数层数要按 `status & 1` 过滤，否则会看到 32 个 Inner。
- **编辑器保存会整体重写文件**：手工追加的记录若编辑器不认，下一次保存就被丢弃——所以顺序是"先改文件、后开编辑器验证"，而不是反过来。
- **删除是追加**：日志会越来越长，属正常现象。

---

## 11. 在本仓库中的落地

| 位置 | 内容 |
|---|---|
| `hardware/pcb/eprj3_inspect.py` | 本文配套的只读解析器（stdlib，无依赖） |
| `hardware/pcb/robot_electronics_v2/evidence/official_eprj3_README.md` | 格式速览（早前整理） |
| `hardware/pcb/robot_electronics_v4_1_bottom_ports/desktop_cli.py` | 官方 CLI 适配器：把命令、返回 JSON、stdout 全部留证 |
| `hardware/pcb/robot_electronics_v4_1_bottom_ports/evidence/official_cli/` | 真实调用证据（open / project-ready / readback / export） |

---

## 12. 参考

- 文件格式总览：https://prodocs.lceda.cn/cn/format/index/
- 工程格式：https://prodocs.lceda.cn/cn/format/project/index/
- 命令行客户端：https://prodocs.lceda.cn/cn/api/guide/cli.html
- 面向 AI 的 CLI 规范：https://prodocs.lceda.cn/storage/texts/api/guide/cli-for-ai.md
- 格式规范包（v3，含样例工程）：https://image.lceda.cn/files/lceda-pro-file-format-v3_2025.10.21.zip
