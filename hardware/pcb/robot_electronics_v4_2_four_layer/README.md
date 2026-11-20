# RobotElectronics V4.2 四层修订版

打开工程：`project/RobotElectronicsV4_2_FourLayer.eprj3`。这是包含原理图、PCB和内嵌库的文件夹工程，请保留整个`project`目录。V4.1原工程保留未覆盖。

## 本次完成的修改

- 控制底板和独立电源板均为100×100 mm、标称1.6 mm的四层板。
- 采用嘉立创现有JLC04161H-7628叠层，外层1 oz、内层0.5 oz；顶、底层走线，两个内层铺地。控制板内层网络为`GND`，电源板内层网络为`P_GND`。内层使用信号层上的显式网络铺铜，便于编辑、重建并避免隐式内电层网络绑定错误。
- ESP32天线禁铜同步到全部四个铜层；四个控制板安装孔、四个电源板安装孔以及模块调节槽继续避让。
- USB1/2保留原有USB母座、Hub输入排针和USBLC6-2SC6保护芯片；重新布置两侧差分线和电源跨层。数据线仍全程在底层，没有数据过孔，D+/D−长度均约17.006 mm。
- 耦合段线宽10.4 mil（0.26416 mm），线边距7 mil（0.1778 mm）。依据嘉立创官方计算服务、7628介质厚度0.2104 mm及其当前铜厚/阻焊参数，差分截面计算为90.46 Ω。连接器及ESD焊盘处保留必要的短扇出过渡；计算结果不等于整板USB眼图或实测认证。
- 电源板内层地铜覆盖整板，并保持`P_LOGIC_GND`通过原有R7连接`P_GND`的网络关系。没有合并不同网络，没有改变电源输出分配。
- 电源板上的“2 LAYER”丝印已更正为“4 LAYER”。接口功能、针序、电压及原板GPIO名称丝印保留。
- 另存工程导致的18个内部Channel ID变化已与新工程原理图同步；它们不是硬件针号，未改变程序GPIO或接线。

## 验证结果

| 项目 | 控制底板 | 独立电源板 |
|---|---:|---:|
| 铜层数 | 4 | 4 |
| 板框尺寸 | 100×100 mm | 100×100 mm |
| 原生DRC，保存关闭重开后 | 0 | 0 |
| 元件 | 85 | 42 |
| 焊盘 | 497 | 111 |
| 与原理图逐引脚网络差异 | 0 | 0 |
| 保留的商城3D模型绑定 | 85 | 42 |

两板共608个焊盘的网络、原有元件位置、连接器正反面、安装孔和板框均已核对。144个MCU引脚丝印及此前16组背面电源/控制接口保留。对原生重建后的内层铜皮进行了几何检查，确认天线区无铜、USB耦合段下方有连续地参考，两个内层实际有铜且网络正确。

检查记录：`evidence/release_validation.json`；冷重开原生数据：`evidence/v42-cold-final.json`；原理图和PCB导出网表：`evidence/v42-final-netlists.json`；官方USB计算请求及结果：`evidence/jlc_calc/calculation-request.json`、`calculation-result.json`。

## BOM（2026-11-15 重新导出）

`release/bom/` 里是官方 API 从工程直接导出的 BOM，不是手抄：

| 文件 | 内容 |
|---|---|
| `Controller_V42_BOM.csv` | 控制板，30 行 / 81 件（UTF-16，Excel 直接打开） |
| `Controller_V42_BOM.xlsx` | 同上，嘉立创专业版 BOM 模板 |
| `UpperPower_V42_BOM.csv` / `.xlsx` | 电源板，25 行 / 41 件 |
| `*_UTF8.csv` | UTF-8 副本，给脚本或下单系统上传用 |
| `BOM_MERGED_PURCHASE.csv` | 两板合并采购表，47 种 / 122 件，含位号来源 |

- 列是嘉立创下单格式：`No. / Quantity / Comment / Designator / Footprint / Value / Manufacturer Part / Manufacturer / Supplier Part / Supplier`，每行带立创 C 编号。
- **DNP 元件已自动排除**：控制板少 R22–R25（IRF 下拉，等确认触发极性再贴），电源板少 R7。81 + 41 = 122，与 `BOM.csv` 的 FIT 122 / DNP 5 一致。
- 导出的 CSV 是 UTF-16LE（带 BOM），是为了 Excel 双击不乱码；上传到 SMT 下单页时用 `*_UTF8.csv` 更稳。
- 注意：R1–R6 此前是 `DNP=yes` + `Add into BOM=no` 双关，本次一起打开，BOM 里才出现这 6 只 4.7 kΩ。

## 叠层顺序（已写入工程）

完整叠层表见 [`STACKUP.md`](STACKUP.md)。要点：

`JLC04161H-7628` / 4 层 / 1.6 mm / 外层 1 oz / 内层 0.5 oz

```
L1 TOP      铜 0.035
  PP 7628   0.2104  Er 4.4
L2 INNER1   铜 0.0152  GND
  CORE     1.065   Er 4.38
L3 INNER2   铜 0.0152  GND
  PP 7628   0.2104  Er 4.4
L4 BOTTOM   铜 0.035
```

叠层表**写进了 PCB 的文档层（Document Layer 13）**，会随 Gerber 导出到 `Gerber_DocumentLayer.GDL`；两个制板 zip 里另附 `叠层顺序.txt`。`LAYER_PHYS` 记录里有材料名、厚度、εr、tanδ，是编辑器"工具 → 图层管理器 → 物理堆叠"的数据源。

## 2026-11-15 整改（审查后）

- **ToF 上拉 R1–R6 改为贴装**：原理图与 PCB 的 `DNP` 属性由 `yes` 改为 `no`，BOM 由 FIT 116/DNP 11 变为 **FIT 122/DNP 5**。三路 I²C 不再依赖 TOF050C 模块自带的上拉。
- **电源板补 IRF540 模块外框**：顶层丝印增加 65×65 外框（17.5–82.5 mm）与“IRF540 模块 65x65”标注，四角长槽与模块边界现在可以直接对齐。制板文件已重新导出：`release/power_V42_4Layer_Gerber.zip`（GTO 491,422 B）。
- **固件不再默认 legacy**：`build_stm32.py` / `build_integrated.py` / `build_esp32.py`（含 sensor_test）的 `--profile` 改为必填；`robot_pinmap.h` 在宏未定义时直接 `#error`，不会静默编出与四层板不匹配的固件。
- 改动后已用官方 CLI 重新打开工程复跑 DRC：**控制板 0 违规、电源板 0 违规**。
- 仍未处理：控制板 5V 输入与舵机 5V 无保险/TVS（需要新增元件与布线，留给下一版）。

## 生产和查看文件


- 控制底板：`release/controller_V42_4Layer_Gerber.zip`
- 独立电源板：`release/power_V42_4Layer_Gerber.zip`
- 原生装配PDF和正反面预览：`review/`
- 内层铜皮审核图：`review/controller_V42_L2.png`、`controller_V42_L3.png`、`power_V42_L2.png`、`power_V42_L3.png`
- 接线：`WIRING.md`；完整针序：`PINOUT.csv`；元件采购：`BOM.csv`、`BOM_PURCHASE.csv`

生产订单应选择四层、1.6 mm、外层1 oz、内层0.5 oz及JLC04161H-7628叠层；USB差分阻抗目标90 Ω、容差±10%，把上述USB线宽、间距和L4参考L3写入订单阻抗说明。不要混用V4.1旧的两层或早期四层生产包。

电池、搅拌器、水泵、舵机等负载总电流仍按此前约定未定，本版没有完成整板载流、温升或实板USB测试；不能据四层铺铜和DRC通过认定额定输出电流。

## 参数来源

- [嘉立创官方叠层参数](https://jlcpcb.com/impedance)
- [嘉立创阻抗计算工具](https://tools.jlc.com/jlcTools/index.html#/impedanceCalculateNew)
- [嘉立创阻抗工具说明](https://www.jlc.com/portal/q7i37381.html)
- [乐鑫ESP32-S3布局指南：天线净空与USB参考地](https://docs.espressif.com/projects/esp-hardware-design-guidelines/en/latest/esp32s3/pcb-layout-design.html)

## 2026-11-15 品牌与工程丝印

两板已加入中英文板名、栗山未来图标、“千板工作室”和等宽网址、板号/版本/日期及 SN/QC 留白。主控板的展示正面为 BOTTOM，位于开发板插入面的另一侧；电源板为 TOP。图标与右侧两行文字块等高，网址与中文行左右齐边。详见 `branding/README.md`，新增图形均内嵌于 PCB。电源板 IRF540 参考框及标题移至装配层 9 保留，避免穿过制造丝印。

本次最终预览为 `review/controller_V42_BRANDED_FRONT.png`、`review/power_V42_BRANDED_FRONT.png`，原生装配导出为同目录的 `*_V42_branded_native.pdf`。`release/` 内两个既有名称的四层 Gerber 已按新丝印重新导出。完整带丝印工程包为上一级 `RobotElectronicsV4_2_FourLayer_Branded.zip`。最终核验见 `evidence/branding_validation.json`；没有改动程序针脚。
