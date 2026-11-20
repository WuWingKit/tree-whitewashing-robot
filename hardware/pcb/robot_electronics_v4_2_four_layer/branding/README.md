# 千板工作室 PCB 丝印

2026-11-15：板名、品牌标志和工程标识已写入原生 PCB，不依赖外部图片链接。

- 主控板正面指插入 STM32/ESP32 开发板的另一面，即底层丝印 BOTTOM（层 4）；电源板使用顶层丝印 TOP（层 3）。从各自板外侧观看均为正向文字。
- 左侧栗山未来图标与右侧“千板工作室 / qianban.online”两行文字整体等高，上下对齐。
- 中文行与网址使用相同宽度、相同左边界和右边界。主控板图标/文字块高度为 8.5 mm，两行文字宽度为 18.0196 mm；电源板高度为 12 mm，两行宽度为 25.4394 mm。
- 两块板均增加中英文名称、板号、REV4.2、四层标识、日期和 SN/QC 留白。接口原有针名与用途标注保留；主控板 5V 输入明确标注“5V输入(仅5V)”。
- 电源板既有 IRF540 65×65 mm 参考外框与标志相交，因此将其四条边及标题移至顶层装配层 9，保留尺寸和位置，避免压住制造丝印。
- 新增中文和网址均为原生矢量 IMAGE 图元：避免本版本中文 PDF 转义问题，且不依赖接收方电脑安装字体。它们可整体移动/缩放；修改文字时应重新生成对应矢量。

独立标志：`qianban_studio_lockup.svg`（全矢量、无字体依赖）、`qianban_studio_lockup.png`（预览）。`native_objects.json` 记录实际 PCB 图元 ID、层和从对应外侧观看的坐标；`lockup_specification.json` 记录标志尺寸。

栗山未来头像通过内置 image_gen 生成黑白线稿，原始提示词见 [generation_prompt.txt](generation_prompt.txt)，源图为 `mirai_kuriyama_source.png`。头像、中文及网址随后转换为矢量轮廓，再通过嘉立创官方桌面 CLI/API 写入。没有使用鼠标键盘控制。

复查记录：`../evidence/branding_validation.json`。预览：`../review/controller_V42_BRANDED_FRONT.png` 与 `../review/power_V42_BRANDED_FRONT.png`。

板框、安装孔、元件封装及位置、焊盘网络和铜层连接按原工程保留；本次图形修改不改变程序 GPIO 配置。
