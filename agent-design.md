# Agent 设计方案 — 个人事务中枢

> 生成于 2026-06-25（Windows 端会话）。本文是项目的**架构与功能基准**，后续所有工作的依据。
> 接续 `handoff-esp-xiaozhi-20260625-v2.md`，但在讨论中**重心从 ESP 转到了手机+服务器+Supabase**，架构有重大调整。
> ⚠️ **最新决策见文末「今日收口（2026-06-25）」**，与前文冲突处以收口为准。

## 一句话定位

**手机优先的个人 agent 中枢**。原生 App 是主入口，云 VM 自建后端是唯一大脑，Supabase 是数据中枢，ESP32 是可选语音外设。管理提醒、笔记/待办、信息查询。

## 设计演进脉络（为什么是现在这样）

1. 起初按 ESP 为主设计（瘦设备+服务器，小智 server 当后端）。
2. 讨论中发现：要随时随地记事，必须有手机入口；绕微信生态（公众号认证/企业微信 App）都不干净。
3. 结论：**自己做个 App**，App 和 ESP 是两个对等入口，共用一套后端，只是 App 能多传图/文件。
4. 进一步发现：手机语音走 **PTT**（按住说话），ESP 走 **VAD**（自动端点检测），两者的语音管道**本质不同，不能共用**。
5. 由此定调：**App 为主，ESP 为辅**。围绕手机+服务器+Supabase 设计，小智 server 降为 ESP 专属的协议适配层。

## 核心架构

**关键原则：公用与不公用的分界线，画在"文字"这一层。**

- 分界线之上（音频进出）：各走各的
  - ESP：VAD 流式采音 → ASR；TTS 流式回播
  - App：PTT 整段录音 → 上传 ASR；无 TTS（只显示文字）
- 分界线之下（意图处理）：**完全统一，只有一份**
  - LLM、工具、Supabase、OpenViking、提醒调度

```
ESP32 ──小智WS协议──▶ 小智server ──HTTP(文字)──┐
   (VAD流式)           (仅ESP协议适配,            │
   (TTS流式回播)        不含业务逻辑)              │
                                                ▼
App ────REST────▶ 自建FastAPI后端 ◀── 共享核心 ──┘
   (PTT整段)         │
   (无TTS,纯文字)    ├─ LLM: Agent Plan (OpenAI兼容 /api/plan/v3)
                     ├─ ASR: Agent Plan (整段/流式,见下)
                     ├─ TTS: Agent Plan (仅ESP线用)
                     ├─ 工具: 笔记/待办/提醒CRUD + 查询
                     ├─ 调度: asyncio + cron兜底
                     ├─ 记忆: OpenViking
                     └─ 推送: iOS 本地通知+APNs(弃 Server酱)
                            │
                            ▼
                     火山Supabase (Postgres+pgvector / Realtime / Storage / Auth)
```

### 为什么是 A（自建后端是核心，小智 server 调用它）

核心逻辑**只有一份**，活在自建后端。小智 server 退化成纯粹的"ESP 协议适配器"：负责 ESP 那套 VAD 流式管道，把语音转成文字后，通过一个 plugin **HTTP 调自建后端**拿结果，再 TTS 回播给设备。符合"App 为主"的重心，核心单一来源，升级只改一处。

## 已定决策清单

| 维度 | 决策 | 备注 |
|---|---|---|
| 定位 | 综合事务中枢 | |
| 主入口 | 原生 App（Flutter） | PTT 语音 + 拍照 + 文件 + 文字 |
| 次入口 | ESP32（可选外设） | 走小智 server，VAD 流式 |
| 后端 | 自建 FastAPI（唯一大脑） | App 直连后端(不直写 Supabase 业务表,选项1)；小智 server HTTP 调它 |
| App 语音模式 | PTT 整段 | 按住录音，松手提交 |
| ESP 语音模式 | VAD 流式 | 小智 server 现成管道 |
| App 的 TTS | 不要，只显示文字 | 省 AFP，交互更快 |
| 存储 | 火山 Supabase | Postgres+pgvector / Realtime / Storage / Auth |
| 长期记忆 | OpenViking | 两线共用 |
| LLM | Agent Plan（OpenAI 兼容 `/api/plan/v3`） | |
| ASR/TTS | 全 Agent Plan | ESP 线需改小智 provider 端点+鉴权（见下） |
| 提醒推送 | **iOS 本地通知(基线) + APNs(动态/主动播报)**；弃用 Server 酱 | iOS 前提；详见今日收口 |
| 调度 | 进程内 asyncio + cron 兜底 | |
| 记账 | ❌ 不要 | |

## 技术栈角色

| 组件 | 角色 |
|---|---|
| 手机 App（Flutter） | 主入口 |
| 自建 FastAPI 后端 | 唯一大脑：LLM+工具+存储+调度 |
| 小智 server | 仅 ESP 协议适配层（provider 改接 Agent Plan） |
| 火山 Supabase | 数据中枢：**后端写**(App 不直连业务表,选项1) + App 读/Realtime 订阅 + Storage 存图/文件 + Auth |
| OpenViking | 对话长期记忆 |
| Agent Plan | LLM + ASR（App 的 PTT ASR 也走它）+ TTS（仅 ESP） |
| ESP32 | 可选语音外设 |
| Server 酱 | 提醒兜底推微信 |

## 功能域

1. **提醒/定时** — 一次性/重复；状态机(pending→fired→done/snoozed)；到点推 App（Realtime，前台即时）+ Server 酱兜底（App 被杀/离线时推微信，保可靠性）
2. **笔记/待办（结构化）** — 笔记归档；待办带 deadline 时自动生成关联提醒；向量检索；标完成；`source` 字段标来源（device/app）
3. **查询/搜索** — 复用 web_search / weather / 专业数据集（datapro）；查询结果可触发后续动作（设提醒/记笔记/改待办）
4. **上下文感知** — 系统提示词注入时间/待办数等
5. **主动播报** — 晨间播报等，agent 按时间主动生成内容并推送（非用户发起）

## Supabase 表设计（草案）

**数据以 `user_id` 为主键维度**（见 F1），device_id/source 只是来源标记。App 登录 Supabase Auth 拿 user_id，ESP 绑定到同一 user_id。提醒/笔记/待办都是"用户的"，不是"设备的"。

- `reminders`：id, **user_id**, device_id, source, content, trigger_at, repeat_rule, **status**(pending/fired/done/snoozed), **snooze_until**, created_at
  - status 是状态机（见 F2），不是简单"触发/未触发"
- `notes`：id, **user_id**, device_id, source, title, content, tags, type(note/todo), status, **deadline**(可空), **reminder_id**(可空,关联提醒), embedding, created_at
  - 待办带 deadline 时自动生成关联 reminder（见 F3）
- `todos`（待办独立，见 F3）：id, **user_id**, device_id, source, title, content, status(todo/done), deadline, reminder_id, created_at, completed_at
- `notifications`：id, **user_id**, content, ref_type(reminder/...), ref_id, created_at, read_at
  - 提醒推送专用表。调度器到点 INSERT 一条，App 通过 Realtime 订阅收到
- `devices`：id, **user_id**, device_id, name(type: app/esp), created_at
  - 设备绑定表，把 ESP 的 device_id 归属到 user_id

## 提醒推送链路（已验证 Realtime 可用）

**验证结论**（来自 byted-supabase skill 官方文档，非假设）：火山版 Supabase 支持 Realtime 三模式——Postgres Changes（监听表变更）/ Broadcast / Presence。我们要的"插表→推 App"= Postgres Changes 模式，官方有标准用法。

链路：
```
调度器到点 → INSERT notifications 表(user_id, content, ...)
                    │
                    ▼  (Postgres Changes, 受 RLS 约束)
              Supabase Realtime (WebSocket)
                    │
                    ▼  (App 已订阅 user_id 自己的行)
              App 前台/后台未杀 → 即时收到 → 响铃/通知

App 被杀/手机关屏久 → Realtime WS 断 → Server酱推微信兜底
```

**Realtime 的 3 个配置前提**（文档明确警告，缺一不可）：
1. 表建好后要在平台**显式开启该表的 Realtime**（默认不一定开）
2. 要配好 **RLS**——Postgres Changes 受 RLS 约束，App 只能收到 RLS 允许它看到的行（如 `user_id = auth.uid()`）
3. 表要**暴露给 Data API + 授权 anon/authenticated 角色**，否则客户端订阅不到

**可靠性认知**：Realtime 是 WebSocket 长连，负责**即时性**（App 在前台/后台未杀时秒到）；App 被系统杀掉/关屏久 WS 断开时，靠 **Server 酱推微信保可靠性**。两者互补非冗余——这验证了"两者都要（可靠优先）"决策的正确性。

**待实测**（放 P2）：Realtime 延迟、断线重连行为。App 后台被杀的缺口已知用 Server 酱兜底，不需实测。

## 深化点（设计层面待定/已定）

### D1. 工具调度归谁 —— A 方案的命门（待定）

小智 server 通过 plugin HTTP 调自建后端（A 方案），但**谁做 LLM 工具调度**没定。两种可能：

- **a. 小智只传文字、不做工具调度**（倾向）：小智把 ASR 文字 HTTP POST 给自建后端，自建后端跑完整 LLM+工具递归（MAX_DEPTH），返回最终文字，小智拿去 TTS。→ 工具调度**只在自建后端**，小智的 `func_handler` 形同虚设。干净，核心单一来源，符合"自建后端是唯一大脑"。代价：小智那套 function_call 能力浪费（但本就不该两处重复）。
- **b. 小智自己做调度，工具转发自建后端**：小智 LLM 决定调 `add_reminder`，plugin HTTP 转发自建后端执行。→ 调度在小智、执行在自建。但 App 线要再实现一遍调度，**核心逻辑分裂成两份**，违背 A 的初衷。

→ **倾向 a**：小智 server 退化成"ASR→POST文字→拿结果→TTS"的哑管道。需用户拍板。

### D2. 调度可靠性 —— 状态全在表，cron 是主调度（已定方向）

自建后端是单进程 FastAPI，进程重启内存定时器全丢。设计要求：
- 调度状态**全在 Supabase `reminders` 表**（trigger_at + status），进程重启从表恢复未触发的
- **cron 是真正的主调度**（每分钟扫表到点的），进程内 asyncio 只是"低延迟加速"
- 这样进程挂了不丢提醒，重启自动恢复。cron 最坏 1 分钟延迟，asyncio 补即时性

### D3. 多入口记忆边界 —— 长期共享，短期隔离（已定方向）

- **长期记忆（OpenViking）共享**：scope=user，App 和 ESP 共享用户级记忆，App 记的事 ESP 知道
- **短期对话隔离**：App 和 ESP 是不同 session，对话上下文不互窜，否则 App 对话污染 ESP

### D4. ASR 共享层 —— 两条线底层同协议（已定方向）

- App PTT 整段 + ESP VAD 流式，调用方式不同（整段 vs 流式帧），**共用不了底层 ASR 调用代码**
- 但 Agent Plan ASR 端点 `wss://.../plan/sauc/bigmodel_async` 本身是 WS 二进制帧协议，ark-voice 的 `asr.py` 已实现
- → App 的 ASR 也走 Agent Plan WS ASR 端点（复用 ark-voice asr.py），两条线底层同一套协议，只是 App 一次性喂整段、ESP 流式喂。需确认 ark-voice asr.py 是否支持整段输入模式

### D5. 部署拓扑与认证（已定方向）

- 小智 server 和自建后端**同机部署**（云 VM），小智调自建后端走 `localhost` + 共享 secret token
- 自建后端业务接口不对公网裸露：App 走 Supabase Auth + 后端校验；小智走 localhost + token
- 参考范例：小智 plugin 调外部 API 的标准模式见 `plugins_func/functions/call_device.py`（`@register_function` + `requests` + Bearer secret）

### D6. 小智↔自建后端的接口契约（依赖 D1=a，待定细节）

若 D1 定为 a（小智当哑管道），小智 server 通过一个 plugin HTTP 调自建后端，契约草案：

```
小智 ASR 得到文字
  → POST localhost:<port>/agent/chat
     headers: Authorization: Bearer <shared-secret>
     body: { "text": "<ASR文字>", "device_id": "<设备id>", "session_id": "<会话id>" }
  ← 自建后端跑完整 LLM+工具递归，返回:
     { "reply": "<最终文字>" }
小智拿 reply → TTS 流式回播
```

关键点：
- 小智**不传 tools、不做 function_call**，纯哑管道。工具调度全在自建后端
- 自建后端这个 `/agent/chat` 端点 **App 也复用**——App 的 PTT 流程是"整段音频→ASR→拿到文字→同样 POST /agent/chat→拿 reply 显示"。两条线在 `/agent/chat` 汇合，这正是"分界线画在文字层"的落地
- session_id 用于短期对话隔离（D3）：App 和 ESP 各自独立 session，不互窜上下文
- 若 D1 定为 b，此契约作废，需重新设计（小智传 tools、自建后端只执行单个工具）

## 功能深化点（F1-F7，产品/功能层面）

### F1. 用户身份是一等公民（影响数据模型）

现在表按 device_id/source 标来源，但**没有 user_id**。App 和 ESP 是两个设备、同一个你——提醒"明早9点开会"不管从哪设，两个入口都该收到、都能查。按 device_id 隔离等于 App 设的提醒 ESP 查不到，违背"中枢"定位。

→ **整个数据模型以 user_id 为主键维度**，device_id 只是来源标记。App 登录 Supabase Auth 拿 user_id，ESP 通过 devices 绑定表归属到同一 user_id。提醒/笔记/待办都是"用户的"，不是"设备的"。这是 D3（记忆共享）的数据层基础。表设计已据此更新。

### F2. 提醒是状态机，不是一次性事件（影响交互闭环）

现在提醒定义太单薄——trigger_at + content + 到点推。真实痛点没覆盖：
- **确认/snooze**：提醒响了，"5分钟后再提醒"——没有这个闭环
- **完成态**：吃药提醒响了要能标"吃了"，否则重复提醒一直触发
- **未读堆积**：多条没确认时怎么呈现？"你有3条未读提醒"
- **重复提醒语义**："每天8点"是每天生成一条还是一条永远循环？过期重复提醒要不要补发？

→ reminder 是状态机：`pending(未到点) → fired(已触发未确认) → done(已确认/完成) / snoozed(延后，新的 trigger_at)`。重复提醒每次触发生成一条 notification，reminder 本身按 repeat_rule 计算下次 trigger_at。

### F3. 待办带 deadline = 待办 + 自动提醒（影响功能边界）

笔记和待办交互预期不同：笔记是"归档"（记和翻），待办是"任务"（记/做/标完成/过期处理）。待办带时间（"还信用卡"——什么时候前还？）时，本质就是一条提醒。

→ 待办独立成 `todos` 表，带可选 deadline。**设待办带 deadline 时自动生成一条关联 reminder**（reminder_id 外键）。待办和提醒不是两个独立功能，是关联的。这个关联现在完全没有。

### F4. 查询是动作的起点，不是终点（影响 agent 性）

现在"查询/搜索"孤立——问完答完就结束。真实场景查询常带后续动作：
- "明天天气" → "那提醒我带伞"（查询→设提醒）
- "下周待办" → "把周二那个改周三"（查询→修改）
- "搜XX新闻" → "记成笔记"（查询→归档）

→ 查询结果回灌 LLM 后可触发下一个工具。LLM 的 function_call 天然支持（REQLLM 模式），但要把"查询→动作"作为一条显式能力，而非孤立的搜索框。这是 agent 区别于"语音搜索框"的核心。

### F5. 主动性：agent 不能只被动响应（影响架构）

现在所有功能都是"用户说→agent做"。"中枢"暗示该有主动性：
- 晨间播报："今天3个待办，9点开会，广州有雨带伞"
- 待办快到期："信用卡明天到期还没还"

→ 需要**主动触发机制**——非用户发起，agent 按规则/时间主动产生内容。晨间播报最典型：cron 触发"生成播报"任务（拉当日待办+提醒+天气→LLM生成→推送）。技术上对应 cron 调度 + `/agent/chat` 的变体（无用户输入，用系统 prompt 驱动）。现在完全没设计，补为功能域第5项。

### F6. 多入口状态一致性（影响数据访问）

App 设的待办，ESP 问"我有啥待办"该能答。要求所有工具的数据访问都走 user_id（F1），不能 App 走一套、ESP 走一套。两个入口的"当前状态视图"一致。

→ D6 的 `/agent/chat` 接口除 session_id 外，必须带 **user_id 上下文**。这是 D1/D6 要补的——session 隔离短期对话，user_id 统一数据视图。

### F7. 短期对话上下文 vs 长期记忆，层次要分清（影响记忆设计）

定了 OpenViking 做长期记忆，但**短期对话历史**没设计：早上说"我叫小王"，下午问"我叫什么"该记得。这不是 OpenViking（结构化抽取的长期记忆），是**对话历史窗口**。

→ 两层记忆：短期对话历史（最近 N 轮，按 session）+ OpenViking 长期记忆（跨 session 结构化）。功能定义只提了 OpenViking，漏了短期上下文。D3 补充：长期共享(user_id)、短期隔离(session_id)。

## 阶段路线

| 阶段 | 内容 |
|---|---|
| P0 | 自建 FastAPI 后端骨架 + Supabase 建表(含 user_id/devices 绑定) + App 基础(登录+PTT 录音→ASR→LLM→文字返回)跑通。**先定 D1（工具调度归谁）** |
| P1 | 笔记/待办/提醒 CRUD + App UI 列表。待办带 deadline 自动生成关联提醒(F3)；提醒状态机(F2) |
| P2 | 提醒子系统：建 `notifications` 表 + 开 Realtime + 配 RLS + 实测延迟/重连；Server 酱兜底；cron 主调度 + asyncio 加速（D2）；snooze/确认闭环(F2) |
| P3 | OpenViking 对话记忆(D3/F7 长期共享/短期隔离) + 上下文注入(F4 查询→动作) |
| P4 | 查询插件配 key（web_search/weather/datapro） |
| P5 | 主动播报(F5)：晨间播报 cron + LLM 生成 + 推送 |
| P6 | ESP 接入（小智 server + 改 provider），作为可选外设挂上来。按 D1 实现小智→自建后端的哑管道；多入口一致性(F6) |
| 可选 | SPI 摄像头 + VLM |

## 关键遗留问题：小智 server 的 ASR/TTS provider 不认 Agent Plan key

v2 handoff 写"语音全走 Agent Plan"，但实测发现现成 provider 对不上：

| 模块 | Agent Plan 端点（集成指南实测） | 小智现成 provider 用的端点 | 鉴权 |
|---|---|---|---|
| TTS | `openspeech.bytedance.com/api/v3/plan/tts/unidirectional`，`X-Api-Key`+`X-Api-Resource-Id: seed-tts-2.0` | `doubao.py`→配置 `api_url`（默认 `/api/v1/tts`），`Authorization: Bearer;<access_token>` | 不同 |
| ASR | `wss://.../api/v3/plan/sauc/bigmodel_async`，`X-Api-Key`+`X-Api-Resource-Id` | `doubao_stream.py`→**硬编码** `/api/v3/sauc/bigmodel_async`（缺 `/plan/`），`token_auth()` 用 `X-Api-App-Key/X-Api-Access-Key` | 不同 |

→ ESP 线（P5）要改 `doubao.py` 和 `doubao_stream.py`：换 `/plan/` 端点、鉴权改 `X-Api-Key`+主key、去 appid/token。这是 P5 的前置工作。
→ App 线的 ASR 在自建后端里直接调 Agent Plan ASR 端点，不经过小智 provider，无此问题。

## Agent Plan 凭证与端点速查

- **主 key 位置**：VS Code `settings.json` → `claudeCode.environmentVariables.ANTHROPIC_AUTH_TOKEN`（已脱敏勿外传）
- **LLM**：`POST https://ark.cn-beijing.volces.com/api/plan/v3/chat/completions`，`Authorization: Bearer <key>`
- **ASR**：`wss://openspeech.bytedance.com/api/v3/plan/sauc/bigmodel_async`，header `X-Api-Key`+`X-Api-Resource-Id: volc.seedasr.sauc.duration`（二进制帧 WS，复用 ark-voice 的 asr.py）
- **TTS**：`POST https://openspeech.bytedance.com/api/v3/plan/tts/unidirectional`，header `X-Api-Key`+`X-Api-Resource-Id: seed-tts-2.0`（流式多 JSON 块，复用 ark-voice 的 tts.py）
- **计费**：TTS 1350 AFP/万字符、ASR 450 AFP/小时、LLM glm-5.2 系数 4.5(限时1.125)；Large 档 25000 AFP/5h 限速
- **铁律**：专属 key + 带 `/plan` 的 url 缺一不可，否则额外计费/失败
- 完整端点/避坑：`C:\Users\xiaotim\Documents\Claude\Projects\FZagentplan\Agent-Plan-集成指南.md`

## 今日收口（2026-06-25）

> 本节为**最新决策**，与前文冲突处**以本节为准**。

### 今日已定

1. **数据路径 = 选项1**：App **不直连业务表**；所有写入（手动 + agent）都过 FastAPI 后端；Supabase 退为"**后端写 + App 读/Realtime 订阅**"。
   - 连带：评审 B(数据模型)、G(RLS) 大幅简化——App 不直连，RLS 压力大减；业务规则(F2 状态机 / F3 待办自动建提醒)只在后端一处，不会被手动写绕过（这正是选项1 优于"App 直连 Supabase"的核心原因）。

2. **提醒推送 = iOS 原生通知，弃用 Server 酱**：
   - **本地通知**做可靠基线（已知定时提醒：App 同步到本机、由 iOS 本地调度，离线也响；注意 iOS 上限 64 条待调度，用滚动窗口）；
   - **APNs** 处理动态/跨设备/主动播报(F5)（需 Apple 开发者账号 $99/年 + 后端接 `.p8`）；
   - iOS 15+ **Time Sensitive** 通知可突破勿扰，适合提醒。
   - Realtime **保留但降级**：仅用于 App 前台实时刷新，不再当送达可靠性手段。
   - ⚠️ **前提是 iOS**；若将来要 Android，国内推送是硬问题（FCM 不可用，需厂商推送：极光/个推/小米华为）。
   - 连带：强化"原生 App"选择(评审 H)——iOS PWA Web Push 扛不起可靠提醒，可靠提醒必须原生通知。

3. **微信输入通道 = 个人号 bot（wcferry）**（见下节）。

### 微信输入通道：wcferry 个人号 bot（已选方案）

- **定位**：输入/捕获通道（把内容喂给 agent），与提醒输出无关。选它的唯一理由——要"**一键转发任意消息（含别人发的聊天消息）给 agent**"，这点官方公众号 / iOS 分享扩展都做不到。
- **原理**：bot 登录在一个**小号**上；主号加小号好友，转发任意消息给小号 → wcferry 在 Windows 上 hook PC 微信截获 → POST 到自建后端 → agent 处理 → 小号把回复发回微信。
- **已核实（2026-06-25，查官方仓库）**：wcferry/WeChatFerry 仍维护（最新 `v39.5.2`，2026-03-28）；**锁定微信 PC 版本 `3.9.12.51`**（3.9.x 老客户端，**非 4.x**，必须装该版并禁自动更新，否则 hook 失效）；`pip install wcferry`；能收消息、发文字/图片/文件。
- **基础设施**：PC Hook 必须 **Windows 常开 + 登录的 PC 微信**；后端在 Linux 云 VM —— 两处分开、HTTP 对接。
- **风险（已知并接受）**：违反微信 ToS，**小号随时可能被封**（务必用小号，别用主号）；微信版本锁死、更新即坏；需 7×24 常开 + 偶尔重扫码；**语音消息微信本身不能转发**。
- **前置待办（卡点，需用户）**：① 准备一个**小号**（不能替你注册账号）；② 定**跑在哪台 Windows**（PC 微信一次只登一个号，会和主号冲突）；③ 装微信 3.9.12.51 + 扫码登录（你的操作）。
- **先行 PoC**：先做独立验证——转一条消息给小号 → bot 能收到并回一句，证明 wcferry 在本环境可用、能接住转发，**再接后端**（后端 FastAPI 目前还没建，是 P0）。临写桥接代码前，再从 wcferry 文档/源码 pin 准确切收发 API。

### 评审遗留（明天处理）

- **B 数据模型**：`notes` 与 `todos` 字段重复 → 择一（建议待办独立成表、notes 收干净）。
- **D 短期对话历史**：表设计缺 `messages(session_id, user_id, role, content, created_at)` → 补上（F7 两层记忆的短期那层）。
- **E 模型分层 + AFP 估算**：路由/简单工具用 `doubao-seed-2.0-lite`(0.5)/mini，难推理才上 `glm-5.2`；补日/月 AFP 粗估对照 Large 额度（25 万/月、25000/5h）。
- **F App 的 PTT ASR**：用 Agent Plan ASR 的 `_nostream` 变体跑整段（别套流式帧）；确认 ark-voice `asr.py` 整段输入模式。
- **I ESP 增量价值**：App 全包后 ESP 仅剩"免提语音 + 常显反射屏"价值，P6 provider 改造前再确认值不值。

### 下一步（明天）

1. 定 **B**（数据模型）+ 补 **D**（messages 表）→ Supabase 表设计定稿。
2. 微信 bot：你备**小号** + 定 **Windows 主机** → 我做 **wcferry PoC**。
3. （可选）E/F 细化。

## 本地资源

| 路径 | 内容 |
|---|---|
| `C:\Users\xiaotim\esp\xiaozhi-esp32-server\` | 小智 server 源码（本次会话 clone，已读架构） |
| `compare\upstream-v2.2.4\` | 上游小智固件 v2.2.4（ESP 板定义） |
| `hardware\ESP32-S3-RLCD-4.2-Hardware-Summary.md` | 板子硬件 pin map |
| `agentplan-tests\ocr_test.py` | VLM-OCR 调用样例 |
| `C:\Users\xiaotim\.claude\skills\ark-voice\scripts\` | 现成 TTS/ASR 脚本（可复用） |
