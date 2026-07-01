# Handoff — 无超时死锁根治 + 气泡精确测量 + KEY 上下文键（2026-07-01 续2）

> 接 `handoff-esp-xiaozhi-20260701-b.md`。本文记：① 一个关键的底层网络死锁 bug 根治
> （很可能就是 6/29 就发现、当时没根治的"设备卡死需拔插"），② 气泡尺寸计算改精确测量，
> ③ KEY 单击改上下文语义（说话中打断 / 聆听中真静音）。

## 一句话现状

**找到并修复了"设备偶发彻底冻结、只能拔电源"的根因**——TCP/TLS 发送 socket 无超时，
网络一卡（今天反复重启后端就会触发）就会把主任务永久阻塞。已加 8 秒发送超时，
**没加接收超时**（会误杀暖待命长空闲连接）。

## ⚠️ 关键修复：socket 发送无超时导致主任务死锁

### 症状
设备卡在"聆听中"页面不动，既不完成对话也不回待机屏；串口疯狂刷
`AFE: Ringbuffer of AFE(FEED) is full`（输入缓冲区没人读、一直溢出）。

### 根因链（从症状一路追到底层 socket）
```
下游网络发送卡住(如后端重启把连接强断/网络瞬断)
  → send()/esp_tls_conn_write() 无超时，永久阻塞
  → 该调用在 Application::Run() 主事件循环里同步执行(main/application.cc:222
     protocol_->SendAudio(...))，主任务被卡死
  → 主任务卡死 = 所有 Schedule() 回调(状态切换/KEY处理)全部停摆
  → 音频处理任务(AfeAudioProcessor::AudioProcessorTask)的 output_callback_
     链路(PushTaskToEncodeQueue 的 cv.wait 无超时)也跟着堆满堵死
  → 不再读取 AFE 数据 → 输入缓冲区溢出(即那些疯狂告警)
```
最底层：`managed_components/78__esp-ml307/src/esp/esp_ssl.cc`(TLS，我们实际路径)和
`esp_tcp.cc`(明文 WS 备用路径)里，socket 建立后**从未设置过任何超时**——`send()`
可以在网络卡顿时无限期阻塞。

### 为什么这次会话特别容易触发
今天为了测试改动，反复 `docker compose up --build` 重启后端很多次——**每次都会把
设备正连着的 WS 强制掐断**，是这个死锁最容易复现的场景。正常使用(后端不这么频繁
重启)应该少见很多，但只要真实网络卡顿一次，同样的死锁会复现。

### 修复
`EspSsl::Connect()` / `EspTcp::Connect()`：拿到 socket fd 后 `setsockopt(SO_SNDTIMEO, 8s)`。
- **只设发送超时，绝不设接收超时**——接收侧长时间无数据是**暖待命的正常状态**
  (设备空闲仍保持连接以待推送提醒)，误加接收超时会把它当断线，误杀那整个功能。
- 发送超时后现有代码本就会走"失败→跳出循环"的路径，不用改任何上层队列/回调逻辑，
  是最小改动、最高把握的修法。

### ⚠️⚠️ 特别注意：这个补丁在 `managed_components/`，不受固件仓 git 追踪！
`.gitignore` 里 `managed_components/` 被整个排除——这是第三方库目录，正常会被
`idf_component_manager` 自动拉取/覆盖。**如果这两个文件被组件管理器重新拉取
(例如 `rm -rf managed_components && idf.py reconfigure`，或迁移到新机器)，
这个救命补丁会静默消失，bug 会在毫无征兆的情况下复发。**
- 已把两个文件的**完整内容**（非 diff）额外备份在：
  - 本仓 `firmware-patches/vendor-patches/78__esp-ml307/src/esp/{esp_ssl.cc,esp_tcp.cc}`
  - `~/esp/laoshi-box-backup/vendor-patches/78__esp-ml307/src/esp/`
- **若发现这个 bug 复发**，第一件事就是检查这两个文件里 `SO_SNDTIMEO` 还在不在，
  不在就从上面路径复制回 `managed_components/78__esp-ml307/src/esp/` 重新编译。

### 验证状态
已编译烧录到真机，逻辑验证过（build 通过，代码路径确认对应现有错误处理分支）。
**未能人为复现原始卡死场景来做实测验证**（网络卡顿是偶发的），后续观察：如果设备
再次冻结，这次预期最多卡 8 秒自动恢复，而不是永久卡死——这是可以直接感受验证的点。

## 其他修复（同一批改动）

### 气泡尺寸改精确测量（治"长文本只剩第一行"）
之前用 `LV_SIZE_CONTENT` + `max_width` style 判断宽度，`LONG_WRAP` 在未知宽度下
不会正确换行，长文本被压缩成一行后截断。改用 `lv_text_get_size()` **提前测量**
文字在最大气泡宽度内换行后的实际尺寸，据此决定：
- 尺寸 ≤4行高 → 宽度贴合最宽行、高度用测出的自然值（短消息不留空白）
- 尺寸 >4行高 → 定宽(76%) + `LV_LABEL_LONG_DOT` 截断到4行高

### 待机不清对话历史
`kDeviceStateIdle` 转换时基类会调 `ClearChatMessages()`(原版"一节课结束清零"逻辑)。
`CustomLcdDisplay` 覆盖成空实现——微信式体验，待机不清历史，回来时还能看到上次聊到哪。

### KEY 单击改上下文语义
原来 KEY 单击发的是 `self.pause.toggle` MCP 消息——张老师盒子"暂停讲课音频"的
语义，我们后端压根没实现这条消息(会被当未知消息忽略)，导致"点了暂停但啥也没停"。
改为：
- **说话中**按 KEY → `AbortSpeaking()` 打断当前回复(设备发 `{"type":"abort"}` →
  后端 `xiaozhi.py` 新增处理：cancel 当前任务、回 `tts stop`)
- **聆听中**按 KEY → 真·静音麦克风(`Application::SetMicMuted` → 
  `audio_service_.EnableVoiceProcessing(false)`)，顶栏明确显示"已静音"文字反馈
  (原来静音了也看不出来，只有个含糊的 ⏸ 图标)
- 加了 guard：静音期间每轮对话结束会重入 Listening 状态，之前会被自动重新打开
  麦克风(取消静音)，现在 `mic_muted_` 标志位会拦住这次自动重开

## 改了什么

**固件**(`~/esp/xiaozhi-esp32`，detached HEAD)：
- `main/application.{cc,h}`：`mic_muted_` + `SetMicMuted()`；Listening 重入加静音 guard
- `main/boards/waveshare/esp32-s3-rlcd-4.2/{waveshare-s3-rlcd-4.2.cc,custom_lcd_display.{cc,h}}`：
  KEY 上下文键、气泡精确测量、`ClearChatMessages` 空实现
- `managed_components/78__esp-ml307/src/esp/{esp_ssl.cc,esp_tcp.cc}`：**socket 发送超时**
  (⚠️不受 git 追踪，见上)
- 已 `app-flash`(应用) 到真机；socket 修复需要 app-flash 生效(已做)。
- 备份：本仓 `firmware-patches/rlcd42-socket-timeout-fix-20260701.patch`(追踪文件完整diff) +
  `firmware-patches/vendor-patches/`(不受追踪的 vendor 文件完整内容)。

**后端**(`myagent`)：本轮无新改动(abort 处理已在上一份 handoff 的 commit `210be1d` 里)。

## 下一步 / 遗留
1. **观察 socket 超时修复是否根治死锁**——需要真实使用中遇到网络卡顿/重启场景验证；
   最坏情况从"永久冻结"降级为"卡顿≤8秒自动恢复"，这本身就是可感知的验证点。
2. 若这个 bug 后续仍复现，检查方向：`EspSsl::Send()` 里 `ESP_TLS_ERR_SSL_WANT_WRITE`
   分支的 `continue` 是否会热循环(理论上低风险，本轮未改动，是潜在的第二处可疑点)。
3. 待命屏/提醒屏/对话屏三块视觉已全部完成，剩下都是功能性打磨(语音质量持续调优、
   连接速度、真实 user_id 切换等)，见前几份 handoff 的遗留清单。
