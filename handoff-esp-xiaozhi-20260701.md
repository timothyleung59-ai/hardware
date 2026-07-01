# Handoff — ESP32-S3-RLCD-4.2 设备 UI 重设计（2026-07-01）

> 接 `handoff-esp-xiaozhi-20260629.md`。本文只记本会话（设备三屏重设计）的进展。
> 固件完整备份：本仓 `firmware-patches/rlcd42-ui-redesign-20260701.patch`（含新文件 font_clock_76.c）+ `~/esp/laoshi-box-backup/`。

## 一句话现状

设备三屏中**待命屏 + 提醒触发屏已重设计完成并验证**；**对话屏还没做**（仍是学习盒子版式）。

## 待命屏（完成 ✅）

```
随手                    wifi 电池      ← 顶条(2px 下划线)
 14:32       6月30日 周一             ← 大钟(左) + 日期/温湿度(右)
─────────────────────────────        ← 中线(2px)
提醒： • 18:30 收衣服                  ← 提醒区(带时间)
       • 明天09:00 项目周会
待办： • 交季度报告                    ← 待办区(不带时间)
       • 写周报
```

- **大钟**：LVGL 内置最大才 30px→不够。自生成 **1-bit 76px 数字字体** `font_clock_76`（只含 `0-9 : -`，用 `npx lv_font_conv` 从 Montserrat-Medium 生成；1-bit 在反射屏最锐）。垂直居中于两条 2px 线之间。
- **提醒/待办两分区**：每区 = `[提醒：|编号列表]` 两栏（flex row，列表顶对齐 → 续行自然缩进对齐）；**圆点 `•`**（编号会被嫌），20px common 全字集。行距 3、段距 4，塞下 6 行不溢出。
- **数据**：后端 `push_agenda` 推 `{type:agenda, reminders, todos}`（连上即推 + 每 30s 刷）；提醒带时间(`_fmt_when`)、待办不带；各最多 3 条。

## 提醒触发屏（完成 ✅）

- **反白全屏接管**（黑底白字）：`提醒`(20px) 在上、内容(30px) 居中，flex 整体居中。~15s 自动隐；**唤醒即撤**（UpdateIdleScreen 离开 idle 时隐藏）。
- 到点：`application.cc` alert 分支 → `Alert()`(声音 OGG_POPUP) + `ShowReminderAlert(status,message)`（新虚函数，板子无关）。

## ⚠️ 踩坑记录（重要）

1. **30px 字缺字**：app 内 `big_font`(theme text_font) 30px 是 **basic 子集**，中文缺字（"该起身活动一下"只显出"活一下"）。→ 必须用 **assets 里的 common 全字集**。现 assets 打了 common_16/20/30 三个全字集（`CommonFont16/20/30()` 按文件名 `GetAssetData` 懒加载）。
2. **assets_size 4MB 上限**：`scripts/build_default_assets.py` 里 `assets_size` 硬编码 `0x400000`(4MB)，比 8MB 分区小。已改 `0x800000`。（注：common_30 与 text_font 同文件→mmap 打包会**去重**，故加它 image 只 +38 字节。）
3. **改 assets 后必须强制重建**：`rm build/generated_assets.bin` 再 `idf.py build`，否则缓存不更新；改 assets 要 **全量烧录**（`idf.py flash`，含 0x800000）不是 app-flash。
4. **设备烧录偶发卡死**：`idf.py flash` 报 "No serial data received" / 按 KEY 唤不动 → **拔插 USB 重启**即可。串口名会变(usbmodem3101/21101/21201…)，烧录前 `ls /dev/cu.usbmodem*` 重新探。

## 改了什么

**固件**（`~/esp/xiaozhi-esp32`，detached HEAD）：
- `custom_lcd_display.{cc,h}`：大钟字体、提醒/待办两栏议程、`ShowReminderAlert` 全屏提醒、`CommonFont20/30`、唤醒撤提醒屏。
- `application.cc`：alert 分支改调 `ShowReminderAlert`；`agenda` 消息读 reminders/todos。
- `display.h`：`SetAgenda(reminders,todos)`、`ShowReminderAlert(status,message)` 虚函数。
- `main/CMakeLists.txt`：common_20/30 打进 assets。
- `scripts/build_default_assets.py`：assets_size 4M→8M。
- 新增 `font_clock_76.c`。

**后端**（`myagent`，`redesign-prototype-b`，commit `b11260a`）：`xiaozhi.py` agenda 计算/推送、`main.py` refresh_agendas。已部署 VM。

## 下一步 / 遗留
1. **对话屏重设计**（最后一块）：去掉"本节课：待开课"等学习盒子残留；显示 你说的 + 随手回的 + 状态。
2. 设备卡死 bug（掉线后 KEY 唤不动，见 0629 handoff）仍未根治。
3. 测试数据：设备账号里有几条测试提醒/待办（买菜/项目周会/写周报等），其中提醒会到点真响，需要时清理。
4. `ESP_USER_ID` 换真实 user_id；提醒低音量发闷。
