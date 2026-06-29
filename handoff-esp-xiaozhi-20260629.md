# Handoff — ESP32-S3-RLCD-4.2 / 小智「暖待命 + 提醒推送」

> 生成于 2026-06-29（Mac 端会话）。接 `handoff-esp-xiaozhi-20260625-v2.md`。本文只记本会话新进展。
> 大背景：单一大脑后端（FastAPI@云 VM 124.222.32.27:8011，Caddy 终结 TLS 于 `api.aidio.site:8443`）。
> 设备 = stock 小智固件（xiaozhi-esp32 v2.2.6）连后端实现的**小智兼容端点** `/xiaozhi/v1/` + `/xiaozhi/ota/`。

## 一句话现状

**暖待命 + 提醒推送 已打通并验证**：设备空闲不断连（关麦显时钟）、提醒到点后端推 `alert` → 设备弹卡片 + 出声。
40 秒必掉线的根因（uvicorn 传输层 ping 超时）已定位修复。**遗留一个设备侧卡死**：异常掉线后按 KEY 唤不动，需重启。

## 本会话成果

| 能力 | 做法 | 状态 |
|------|------|------|
| 暖待命（idle 不断连） | idle 60s → 后端发 `{"type":"system","command":"standby"}`，设备 `SetDeviceState(Idle)` 但**不关 WS** | ✅ 验证 |
| 提醒推到设备 | 调度器 `fire_due_reminders` → `push_alert` → 已连设备发 `{"type":"alert",...}` | ✅ 验证 |
| 提醒卡片（时钟屏可见） | 固件重写 `Display::ShowNotification` → 居中黑卡 + common 中文字 + `move_foreground` 压在时钟之上，~10s 自动隐 | ✅ 看到 |
| 提醒声 | `Alert()` 的 `OGG_VIBRATION`（很闷）→ 改 `OGG_POPUP`（清亮） | ✅ 高音量听到 |

## 关键技术结论

### 1) 40s 必掉线 = uvicorn 传输层 WS ping/pong 超时（根因）
- uvicorn 默认 `ws_ping_interval=20s` + `ws_ping_timeout=20s`，连上约 **40s** 后若没收到传输层 pong 就掐（设备日志侧表现 `code=1006`）。
- 小智 ESP32 WS 客户端**不回传输层 ping 帧**，故每次连上 ~40s 必被误掐。早期那条「撑了 2.6 分钟」是活跃对话期设备在回数据掩盖了它。
- **修复**：程序化启动 uvicorn，`ws_ping_interval=None, ws_ping_timeout=None`（`backend/app/serve.py`，Dockerfile `CMD` 改 `python -m app.serve`）。保活交给应用层（设备自带 ping）+ TCP。
- **验证**：VM 本地起「不回 pong 的裸 WS 客户端」，修复前 40s 掐、修复后**撑过 55s 仍在线且零传输层 ping**。

### 2) 暖待命的固件依据（已确认）
- `SetDeviceState(Idle)` **不**关 WS；Idle = 语音处理关 + wakenet 开（无热麦）；从「Idle+通道开」唤醒会复用原连接。
- 故暖待命 = 收到 `standby` 时进 Idle、连接保持；唤醒/`listen start` 退出暖待命（后端 `st["standby"]` 标志同步）。

### 3) 音量
- 默认 `output_volume_ = 70`，开机从 NVS 读上次值（用户调低过 → 提醒闷）。提醒声走**当前音量**。
- 可选改进（**未做**）：提醒时临时把音量拉到可听底线再恢复。需 esp_timer 一次性恢复，注意 `SetOutputVolume` 会写 NVS。

## ⚠️ 遗留 BUG：异常掉线后设备卡死、KEY 唤不动
- 现象：1006 掉线后，设备主循环还活（`SystemInfo` 心跳在跑），但**按 KEY 零串口日志、连不上**，需拔插/RESET 重启才恢复。
- 影响：ping 修复后掉线应已罕见，故优先级降低；但「任何掉线→需手动重启」对长时暖待命仍是隐患。
- 待查方向：固件 `OnNetworkDisconnect`/`CloseAudioChannel` 后的重连状态机；是否 wifi modem-sleep。

## 改了什么

**后端**（repo `myagent`，分支 `redesign-prototype-b`）：
- `backend/app/xiaozhi.py`：`_DEVICE_CONNS` 注册表、`push_alert()`、idle→发 standby 不关连接、`listen start` 清 standby 标志、注册/注销连接。
- `backend/app/scheduler.py`：`_push_device()` + 在 `fire_due_reminders` 调用。
- `backend/app/serve.py`（新）：程序化启动禁传输层 ping。
- `backend/Dockerfile`：`CMD` → `python -m app.serve`。
- 部署：已 `docker compose up -d --build` 到 VM，`/health` ok。

**固件**（`~/esp/xiaozhi-esp32`，detached HEAD，stock v2.2.6 上二开）：
- `main/application.cc`：`OnIncomingJson` 加 `standby` system 命令；`alert` 分支声音改 `OGG_POPUP` + 调 `ShowNotification(message,10000)`。
- `main/boards/waveshare/esp32-s3-rlcd-4.2/custom_lcd_display.{cc,h}`：提醒卡 + 重写 `ShowNotification`。
- 已 `idf.py app-flash` 到板子（`/dev/cu.usbmodem21201`）。
- **备份**：本仓 `firmware-patches/rlcd42-warmstandby-20260629.patch`（= 完整工作区 diff，含整套 RLCD-4.2 板级移植 + 本会话改动）；同步存 `~/esp/laoshi-box-backup/`。

## 设备连接小抄
- 端点：`wss://api.aidio.site:8443/xiaozhi/v1/`（OTA：`POST /xiaozhi/ota/`）。
- 设备账号：后端 `ESP_USER_ID=1d726af3-2f8c-4d8b-aaa9-a28dde5709fd`（**待换真实 user_id**）。
- 设备连语音 WS 只在**唤醒/按 KEY** 时开（开机不自动连）。
- 编译/烧录（system python 3.14 会废 IDF，用 3.9 shim）：
  ```
  SHIM=$(mktemp -d); ln -sf /usr/bin/python3 "$SHIM/python3"; export PATH="$SHIM:$PATH"
  . ~/esp/esp-idf/export.sh
  cd ~/esp/xiaozhi-esp32 && idf.py build && idf.py -p /dev/cu.usbmodem21201 app-flash
  ```

## 下一步 / 遗留清单
1. **修设备卡死**：异常掉线后 KEY 唤不动（见上）。
2. **soak 实测**：真机待命数分钟后提醒是否仍到（ping 修复后预期 OK，本会话因设备卡死未完成）。
3. **待命屏重设计**：现仍是「学习助手」版式（只改了标题为「随手」）；用户想重设计（见 `UI_DESIGN_BRIEF.md`）。
4. **去学习盒子残留**：`custom_lcd_display.cc` line ~466「本节课：待开课」。
5. **提醒音量**：低音量时闷，考虑提醒临时拉高音量。
6. **设备账号**：`ESP_USER_ID` 换成真实 user_id。
