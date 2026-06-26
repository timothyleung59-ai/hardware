# 个人事务 Agent — 详细设计文档

> 生成于 2026-06-26,2026-06-26 定稿(v2)。本文是**唯一权威设计文档**,可直接接手开工。
> 前序 `agent-design.md` 为讨论过程记录;与本文冲突处**以本文为准**。

---

## 1. 项目定位

**一句话**:手机 App 为载体的个人 AI agent,管理日常工作与生活事务。

**App = agent 本体**,不是聊天外壳、不是素材输入工具。对话、工具调用、数据管理、主动提醒,全在 App 里闭环。ESP32 是可选的桌面语音外设。

**核心能力**:
1. 提醒/定时(状态机,一次性+重复,snooze/确认)
2. 笔记(归档,向量检索)
3. 待办(带 deadline 自动关联提醒,标完成)
4. 查询/搜索(豆包内置搜索 + 天气,结果可触发后续动作)
5. 主动播报(晨间播报等,agent 按时间主动推送)
6. 上下文感知(系统提示注入当前时间/待办数/时区)

---

## 2. 系统架构

### 2.1 核心原则

**只有一个大脑(后端)。** App 和 ESP 的区别**只在音频怎么进出**;文字进了后端,就是同一套处理。这条"分界线画在文字层"是整个架构的骨架。

### 2.2 架构图

```
┌─────────────────── 客户端 ───────────────────┐
│  Flutter App (iOS + 鸿蒙 NEXT)        ESP32  │
│  PTT录音 / 文字 / 拍照 / 列表         流式语音 │
└────────┬──────────────────────────────┬──────┘
         │ REST / SSE                   │ WebSocket
         │                              │ (流式音频)
         ▼                              ▼
┌──────────────────────────────────────────────┐
│            自建 FastAPI 后端                   │
│  ┌────────────────────────────────────────┐  │
│  │  音频适配层 (都调 Agent Plan 语音 API)   │  │
│  │  ├─ POST /agent/asr   App PTT 整段→文字 │  │
│  │  └─ WS  /ws/voice     ESP 流式 ASR/TTS  │  │
│  │     (复用 ark-voice 的 asr.py / tts.py) │  │
│  └──────────────────┬─────────────────────┘  │
│                     ▼ 文字                     │
│  ┌────────────────────────────────────────┐  │
│  │  唯一大脑  agent_turn(text, user, ...)  │  │
│  │  ├─ LLM (Agent Plan, 豆包内置搜索)       │  │
│  │  ├─ 工具引擎 (function_call 递归)        │  │
│  │  ├─ 提醒调度 (cron 主 + asyncio 辅)      │  │
│  │  └─ 记忆 (messages 短期 + OpenViking 长期)│ │
│  └──────────────────┬─────────────────────┘  │
│            推送分发 (APNs / Push Kit)          │
└──────────┬─────────────────────────┬─────────┘
     写(全部过后端)              读(App 直连)
           ▼                         ▼
┌──────────────────────────────────────────────┐
│  火山 Supabase                                │
│  Postgres + pgvector / Auth / Storage         │
└──────────────────────────────────────────────┘
```

### 2.3 三条管道,一个大脑

| 入口 | 音频进 | 音频出 | 走哪个适配器 |
|------|--------|--------|-------------|
| App | PTT 整段录音 → 上传 ASR | 无(只显示文字) | `POST /agent/asr` + `POST /agent/stream` |
| ESP | VAD 流式采音 → 流式 ASR | 流式 TTS 回播 | `WS /ws/voice` |

两个适配器拿到文字后,都调同一个内部函数 `agent_turn()`(大脑)。ASR/TTS 一律用 **Agent Plan 语音 API**,**不部署小智 server**——流式手法参考小智,但代码在我们自己后端里,直接复用 ark-voice skill 的 `asr.py`/`tts.py`。

### 2.4 数据访问规则

| 操作 | 路径 | 原因 |
|------|------|------|
| **写**(增删改) | App → FastAPI → Supabase | 业务规则集中,不被绕过 |
| **读**(列表/详情/搜索) | App → Supabase 直连 | 无业务逻辑,直读更快 |
| **对话** | App → `/agent/stream` (SSE) | LLM+工具递归在后端 |
| **文件** | App → Supabase Storage → 传 URL 给后端 | Storage 原生上传 |

> App 持有 Supabase Auth JWT:既用于直读(RLS 约束),也用于调 FastAPI(后端验同一个 JWT 提取 user_id)。

---

## 3. 技术栈

| 组件 | 技术 | 角色 |
|------|------|------|
| App | Flutter (iOS + 鸿蒙 NEXT) | Agent 完整载体,主入口 |
| 后端 | Python FastAPI | **唯一大脑**:语音适配 + LLM + 工具 + 调度 + 推送 |
| 数据库 | 火山 Supabase (Postgres + pgvector) | 数据中枢 |
| 文件存储 | Supabase Storage | 图片/文件/文档 |
| 认证 | Supabase Auth | JWT |
| LLM | 火山方舟 Agent Plan (OpenAI 兼容) | 对话 + 工具调度 + 豆包内置搜索 |
| ASR/TTS | Agent Plan 语音 API(后端直接调) | App ASR + ESP 流式 ASR/TTS |
| 长期记忆 | OpenViking | 跨 session 用户级记忆 |
| 推送 iOS | APNs + 本地通知 | 提醒送达 |
| 推送 鸿蒙 | Push Kit + 本地通知 | 提醒送达 |
| 部署 | Docker on 云 VM | 单后端进程 |

> ⚠️ **小智 server 不在技术栈里**。ESP 的语音流式由后端自己实现。

### 3.1 Flutter 双平台注意

flutter-ohos 是华为 fork(非 Google 官方)。核心 Dart 通用,原生层分别处理:

| 能力 | iOS | 鸿蒙 NEXT |
|------|-----|-----------|
| PTT 录音 | record 插件 | ohos 音频 API / platform channel |
| 本地通知 | flutter_local_notifications | ohos_flutter_notification |
| 远程推送 | APNs | Push Kit + agconnect |
| 文件选取 | file_picker | 鸿蒙文件 API |

**待核实**:能否从同一 fork 同时构建 iOS。P0 先 iOS 跑通,鸿蒙并行验证基础能力。

---

## 4. 数据库设计

### 4.1 ER 关系

```
users (Supabase Auth 内置)
  ├── 1:1 → profiles (时区/设置)
  ├── 1:N → devices
  ├── 1:N → sessions → 1:N → messages
  ├── 1:N → reminders → 1:N → notifications
  ├── 1:N → notes
  ├── 1:N → todos → 0:1 → reminders
  └── 1:N → documents → 1:N → chunks (RAG, P4.5 预留)
```

### 4.2 表定义

#### `profiles` — 用户设置(含时区)

```sql
CREATE TABLE profiles (
  user_id          UUID PRIMARY KEY REFERENCES auth.users(id),
  timezone         TEXT NOT NULL DEFAULT 'Asia/Shanghai',  -- 提醒时间计算依据
  broadcast_time   TIME DEFAULT '08:00',   -- 晨间播报时间
  default_model    TEXT DEFAULT 'auto',    -- 默认模型
  notify_prefs     JSONB DEFAULT '{}',     -- 通知偏好
  created_at       TIMESTAMPTZ DEFAULT now()
);
```

> **时区是关键**:LLM 把"明早8点"解析成绝对 `trigger_at` 时,必须知道用户时区。每次 `agent_turn()` 把 `profiles.timezone` 注入 system prompt。

#### `devices` — 设备绑定

```sql
CREATE TABLE devices (
  id           UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  user_id      UUID NOT NULL REFERENCES auth.users(id),
  device_token TEXT,            -- APNs/PushKit token
  platform     TEXT NOT NULL,   -- 'ios' | 'harmony' | 'esp'
  name         TEXT,
  created_at   TIMESTAMPTZ DEFAULT now()
);
```

#### `sessions` — 对话会话

```sql
CREATE TABLE sessions (
  id             UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  user_id        UUID NOT NULL REFERENCES auth.users(id),
  source         TEXT NOT NULL,   -- 'app' | 'esp'
  started_at     TIMESTAMPTZ DEFAULT now(),
  last_active_at TIMESTAMPTZ DEFAULT now()
);
-- 30 分钟无活动视为过期,下次对话开新 session;App 与 ESP 各自独立 session
```

#### `messages` — 对话历史(短期记忆)

```sql
CREATE TABLE messages (
  id          UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  session_id  UUID NOT NULL REFERENCES sessions(id),
  user_id     UUID NOT NULL REFERENCES auth.users(id),
  role        TEXT NOT NULL,   -- 'system'|'user'|'assistant'|'tool'
  content     TEXT,
  tool_calls  JSONB,           -- LLM 返回的 tool_call,可空
  tool_name   TEXT,            -- role='tool' 时
  created_at  TIMESTAMPTZ DEFAULT now()
);
-- 每 session 取最近 30 条喂 LLM
```

#### `reminders` — 提醒(状态机)

```sql
CREATE TABLE reminders (
  id           UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  user_id      UUID NOT NULL REFERENCES auth.users(id),
  content      TEXT NOT NULL,
  trigger_at   TIMESTAMPTZ NOT NULL,
  repeat_rule  TEXT,            -- NULL=一次性; 'daily'|'weekly'|'monthly'|cron
  status       TEXT NOT NULL DEFAULT 'pending',  -- pending→fired→done|snoozed
  snooze_until TIMESTAMPTZ,
  source       TEXT NOT NULL DEFAULT 'app',
  created_at   TIMESTAMPTZ DEFAULT now()
);
```

状态机:
- `pending` 未到点 → `fired` 已触发未确认 → `done` 已完成 / `snoozed` 延后
- 重复提醒:触发后生成 notification,按 `repeat_rule` 算下次 `trigger_at`,status 回 `pending`
- **snooze + repeat 交互**:被 snooze 的那次单独按 `snooze_until` 再触发一次;不影响 `repeat_rule` 的下一周期

#### `notifications` — 推送记录 / 未读列表

```sql
CREATE TABLE notifications (
  id          UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  user_id     UUID NOT NULL REFERENCES auth.users(id),
  content     TEXT NOT NULL,
  ref_type    TEXT,            -- 'reminder'|'broadcast'|'todo_deadline'
  ref_id      UUID,
  created_at  TIMESTAMPTZ DEFAULT now(),
  read_at     TIMESTAMPTZ
);
-- 推送记录 + 未读列表。App 进前台时拉取(不用 Realtime)
```

#### `notes` — 笔记

```sql
CREATE TABLE notes (
  id          UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  user_id     UUID NOT NULL REFERENCES auth.users(id),
  title       TEXT,
  content     TEXT NOT NULL,
  tags        TEXT[],
  embedding   VECTOR(<dim>),   -- 维度待定,见下
  source      TEXT NOT NULL DEFAULT 'app',
  created_at  TIMESTAMPTZ DEFAULT now(),
  updated_at  TIMESTAMPTZ DEFAULT now()
);
CREATE INDEX ON notes USING hnsw (embedding vector_cosine_ops);
```

> **embedding 维度待核实**:取决于 Agent Plan embedding 模型实际输出维度(别默认 1536),P4 接入时确认后建表。
> **生成时机**:后端在建/改笔记后**异步**生成 embedding(不阻塞写返回),避免拖慢交互。

#### `todos` — 待办

```sql
CREATE TABLE todos (
  id           UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  user_id      UUID NOT NULL REFERENCES auth.users(id),
  title        TEXT NOT NULL,
  content      TEXT,
  status       TEXT NOT NULL DEFAULT 'pending',  -- 'pending'|'done'
  deadline     TIMESTAMPTZ,
  reminder_id  UUID REFERENCES reminders(id),
  source       TEXT NOT NULL DEFAULT 'app',
  created_at   TIMESTAMPTZ DEFAULT now(),
  completed_at TIMESTAMPTZ
);
-- 业务规则(后端):
--   建待办 + deadline → 自动 INSERT reminder,回填 reminder_id
--   标完成 → completed_at=now(), 关联 reminder status→'done'
```

#### `documents` + `chunks` — RAG(P4.5,暂不建表)

```sql
-- P4.5 解注释:
-- documents: id, user_id, title, file_url(Storage), doc_type, status, created_at
-- chunks:    id, document_id, user_id, content, embedding VECTOR(<dim>), chunk_index, metadata JSONB
-- chunks 同样建 hnsw 向量索引
```

### 4.3 RLS

所有表统一 `user_id = auth.uid()`。App 只读(写走后端),RLS 主要管直读。后端用 `service_role` key 写,绕过 RLS。

```sql
ALTER TABLE notes ENABLE ROW LEVEL SECURITY;
CREATE POLICY "read_own" ON notes FOR SELECT USING (user_id = auth.uid());
```

> **不开 Realtime**。App 进前台拉一次 `notifications` 即可,省掉 Realtime + Data API 暴露的整套配置。

---

## 5. API 设计(FastAPI 后端)

### 5.1 大脑(内部核心)

```python
async def agent_turn(text, user_id, session_id, source) -> stream:
    # 1. 取 profiles(时区/默认模型)
    # 2. 取 session 最近 30 条 messages + OpenViking 长期记忆
    # 3. 组 system prompt(注入时间/时区/待办数)
    # 4. 选模型(自动路由 or 手动)
    # 5. 调 Agent Plan LLM(流式,带豆包内置搜索 + tools)
    # 6. function_call → 工具引擎递归(MAX_DEPTH=5)
    # 7. 流式 yield 文字 token / tool 事件
    # 8. 落库 messages
```

App 的 SSE 适配器和 ESP 的 WS 适配器都调它。

### 5.2 App 接口

#### `POST /agent/asr` — PTT 音频转文字

```
Headers: Authorization: Bearer <supabase-jwt>
Body:    multipart 音频文件 (整段)
Resp:    { "text": "识别出的文字" }
```
App 先拿到文字显示"你说了:…",再走 `/agent/stream`。

#### `POST /agent/stream` — 文字转流式回复(SSE)

```
Headers: Authorization: Bearer <supabase-jwt>
Body: {
  "text": "帮我设个明早8点的闹钟",
  "session_id": "uuid",
  "model": "auto",              // auto | doubao-seed-2.0-lite | doubao-seed-2.0 | glm-5.2
  "attachments": ["storage-url"] // 可选,拍照/文件
}
Resp: SSE
  event: token        data: {"content":"好"}
  event: tool_call    data: {"name":"add_reminder","arguments":{...}}
  event: tool_result  data: {"name":"add_reminder","result":"已创建 #abc"}
  event: token        data: {"content":"已设置。"}
  event: done         data: {"session_id":"uuid","message_id":"uuid"}
```

### 5.3 ESP 接口(P6)

#### `WS /ws/voice` — 流式语音

```
设备流式上传音频帧 → 后端流式 ASR(Agent Plan)→ 出文字
  → agent_turn() → 流式文字
  → 流式 TTS(Agent Plan)→ 音频帧推回设备
```
低延迟靠流式:边出文字边 TTS,不等整段。

### 5.4 写操作接口(App → 后端 → Supabase)

| 端点 | 方法 | 用途 |
|------|------|------|
| `/notes` `/notes/{id}` | POST/PUT/DELETE | 笔记增改删 |
| `/todos` `/todos/{id}` | POST/PUT/DELETE | 待办增改删 |
| `/todos/{id}/complete` | POST | 标完成(联动提醒) |
| `/reminders` `/reminders/{id}` | POST/PUT/DELETE | 提醒增改删 |
| `/reminders/{id}/snooze` | POST | 延后 |
| `/reminders/{id}/done` | POST | 确认 |

### 5.5 系统/认证接口

| 端点 | 用途 |
|------|------|
| `POST /auth/register-device` | 注册推送 token(device_token + platform) |
| `POST /auth/bind-esp` | App 把 ESP 绑到当前 user_id |
| `POST /internal/broadcast` | cron 调,生成+推送晨间播报 |
| `POST /internal/reminder-check` | cron 每分钟调,扫到点提醒 |

---

## 6. Agent Plan 集成

### 6.1 凭证与铁律

- **主 key**:VS Code `settings.json` → `claudeCode.environmentVariables.ANTHROPIC_AUTH_TOKEN`(已脱敏勿外传)
- **铁律**:专属 key + 带 `/plan` 的 URL,缺一不可
- **完整避坑**:`C:\Users\xiaotim\Documents\Claude\Projects\FZagentplan\Agent-Plan-集成指南.md`

### 6.2 端点速查

| 服务 | 端点 | 鉴权 |
|------|------|------|
| LLM | `POST https://ark.cn-beijing.volces.com/api/plan/v3/chat/completions` | `Authorization: Bearer <key>` |
| ASR | `wss://openspeech.bytedance.com/api/v3/plan/sauc/bigmodel_async` | `X-Api-Key` + `X-Api-Resource-Id: volc.seedasr.sauc.duration` |
| TTS | `POST https://openspeech.bytedance.com/api/v3/plan/tts/unidirectional` | `X-Api-Key` + `X-Api-Resource-Id: seed-tts-2.0` |

ASR/TTS 复用 ark-voice skill 的 `asr.py`/`tts.py`(已实现上述协议)。
**待核实**:ark-voice `asr.py` 是否支持整段输入(App PTT 用);若只支持流式帧,App 侧把整段切帧连发。

### 6.3 豆包内置搜索

网络搜索**不自己实现工具**,直接用 Agent Plan 豆包模型的**内置搜索**能力(在 chat completion 请求里开启搜索参数/工具,模型自己联网检索)。
**待核实**:Agent Plan 开启内置搜索的具体参数,查集成指南。

### 6.4 模型路由(为速度,非成本)

> 成本不作约束。路由纯粹为响应速度——简单任务用快模型,别让大模型干等。

| 级别 | 模型 | 触发 |
|------|------|------|
| 快 | doubao-seed-2.0-lite | 简单意图+参数提取("设个8点闹钟") |
| 标准 | doubao-seed-2.0 | 日常对话/笔记/VLM(默认) |
| 强 | glm-5.2 | 复杂推理/多步规划/长上下文 |

**手动覆盖**:App 对话页有模型选择器(默认"自动")。`model` 参数传具体名时跳过路由。ESP 永远自动。

---

## 7. 工具/函数

LLM 用 OpenAI function_call 调工具,工具引擎支持**递归**(结果回灌→可触发下一个工具),MAX_DEPTH=5。

### 7.1 工具列表

| 工具 | 描述 | 危险等级 |
|------|------|---------|
| `add_reminder` | 创建提醒 | 安全 |
| `list_reminders` | 查提醒 | 安全 |
| `snooze_reminder` / `done_reminder` | 延后/确认 | 安全 |
| `add_note` / `search_notes` | 建/向量检索笔记 | 安全 |
| `add_todo` / `list_todos` / `complete_todo` | 待办增查/完成 | 安全 |
| `delete_todo` / `delete_reminder` / `delete_note` | 删除 | **需确认** |
| `get_weather` | 查天气 | 安全 |

> **搜索不在工具列表**——走豆包内置搜索(§6.3)。
> **不要 `get_current_context` 工具**——时间/时区/待办数固定注入 system prompt(push,不 pull)。

### 7.2 确认机制(对话式,无需独立状态)

"需确认"工具靠**工具描述里写"删除前必须先向用户确认"** + LLM 自然多轮:

```
用户: 把"还信用卡"待办删了
LLM:  将删除待办「还信用卡」,确认吗?     ← 没调工具,只是问
用户: 确认
LLM:  tool_call → delete_todo(id)         ← 这才执行
```
待确认状态就在对话历史里(messages),不需要单独存。

### 7.3 查询→动作链

function_call 天然支持:查询结果回灌 LLM 后可触发下一个工具("明天天气?"→查→"那提醒我带伞"→建提醒)。只需保证上下文带上一轮工具结果。

---

## 8. 提醒调度系统

### 8.1 调度器

```
┌ cron 每分钟 (主调度,绝不丢) ─────────────────────┐
│ SELECT * FROM reminders                           │
│ WHERE (status='pending' AND trigger_at<=now())    │
│    OR (status='snoozed' AND snooze_until<=now())  │
│ 对每条:                                            │
│   1. UPDATE status='fired'                        │
│   2. INSERT notifications                         │
│   3. 推送 APNs / Push Kit                         │
│   4. 有 repeat_rule → 算下次 trigger_at, 新建 pending│
└────────────────────────────────────────────────────┘
┌ asyncio (辅,低延迟) ──────────────────────────────┐
│ 加载未来 1h 的 pending,精确到秒触发               │
│ 进程重启 → 从表恢复,不丢                          │
└────────────────────────────────────────────────────┘
```

cron 兜底(最坏 1 分钟延迟,不丢),asyncio 补秒级即时性。

### 8.2 推送链路

```
触发 → INSERT notifications
     → 查 devices 按 platform 分发:
         ios     → APNs (.p8)
         harmony → Push Kit (agconnect)
```

**本地通知**:App 把已知未来提醒同步到本机调度(iOS UNNotificationRequest / 鸿蒙 Notification Kit),离线也响。iOS 上限 64 条,用滚动窗口(保留最近 60 条)。

> 推送可靠性 = 本地通知(基线)+ 远程推送(动态/跨设备/主动播报)。不依赖 Realtime。

---

## 9. 记忆系统

| 层 | 存储 | 范围 | 生命周期 |
|----|------|------|---------|
| 短期对话 | `messages` 表 | 按 session 隔离 | 最近 30 条;App 与 ESP 不互窜 |
| 长期记忆 | OpenViking | 按 user_id 共享 | 永久,跨 session;App 记的 ESP 知道 |

- 每次 `agent_turn()`:取当前 session 最近 30 条 + OpenViking 摘要 → system prompt
- session 超 30 分钟无活动 → 关旧开新,新 session 注入长期记忆
- 对话中提取关键信息存 OpenViking

---

## 10. 主动播报

- **晨间播报**:cron 按 `profiles.broadcast_time` 触发 → 拉当日 reminders+todos+天气 → LLM 生成 → INSERT notifications → 推送
- **待办到期预警**:cron 每小时扫 deadline 在 24h 内且未完成的 todos

---

## 11. App 设计

### 11.1 页面

```
对话页(首页): 消息流式 / 文字框 / PTT按钮 / 拍照文件 / 模型选择器
提醒页: 待触发 / 已触发未确认 / 确认·snooze·删
待办页: 进行中 / 已完成 / 标完成·编辑·删
笔记页: 列表 / 向量搜索 / 查看编辑删
设置页: 账户 / ESP绑定 / 播报时间 / 通知偏好 / 时区 / 默认模型
```

### 11.2 认证

- **P0**:邮箱+密码(Supabase Auth 原生,最快)
- 后续:手机号验证码、Apple ID(iOS)、华为账号(鸿蒙)

### 11.3 离线(P2+ 预留)

本地 SQLite(`drift`/`sqflite`)缓存列表,断网可看;本地通知不受影响;离线录入暂存,联网补传。

---

## 12. ESP 接入(P6)

### 12.1 角色

ESP 是**可选桌面语音外设**。语音流式由**后端**实现(§5.3 `/ws/voice`),**不部署小智 server**,ASR/TTS 直接调 Agent Plan。大脑、工具、数据与 App 完全共用一套——数据天然一份。

### 12.2 唯一待定细节(P6 再拍)

板子现跑**小智固件**,默认按小智 WebSocket 协议找服务器。不走小智 server,需二选一:

| 方案 | 说明 |
|------|------|
| 后端实现设备协议 | 留 stock 固件,后端 `/ws/voice` 兼容小智设备端协议 |
| 自定义固件 | 刷我们自己的固件,走自定义协议 |

ESP 排 P6,此细节届时定。

### 12.3 增量价值

免提语音 / 常显反射屏(零功耗时钟·天气·下条提醒)/ 唤醒词待命的 ambient 体验 / 独立于手机。

---

## 13. RAG 知识库(P4.5 预留)

上传文档(PDF/文章)→ Storage 存原件 → 后端拆块 + Agent Plan embedding → 写 `chunks` → `search_knowledge` 工具向量检索。表见 §4.2(P4.5 解注释)。架构通,不推翻。

---

## 14. 安全与认证

| 调用方 | 认证 |
|--------|------|
| App → FastAPI | Supabase JWT(后端验签提 user_id) |
| App → Supabase 直读 | Supabase JWT + RLS |
| FastAPI → Supabase 写 | service_role key |
| FastAPI → Agent Plan | Agent Plan key |
| cron → FastAPI | 内部 secret token |
| ESP → `/ws/voice` | 设备 token(P6 定) |

要点:FastAPI 不对公网裸露业务逻辑(HTTPS+JWT);Agent Plan key / service_role key 只在后端,不入 App。

---

## 15. 部署

```
云 VM (Linux)
  ├─ Docker: FastAPI 后端 (HTTPS, 含 /ws/voice)
  └─ cron: 每分钟 /internal/reminder-check
           每天 /internal/broadcast
外部托管: 火山 Supabase / Agent Plan / APNs / Push Kit
```

后端需域名 + HTTPS 证书(Let's Encrypt)。**无小智 server,单进程**。

---

## 16. 错误 / 失败处理

| 失败点 | 应对 |
|--------|------|
| Agent Plan LLM 超时/失败 | 重试 1 次 → 仍失败回友好提示,记日志 |
| ASR 失败 | App:提示重说;ESP:短提示音 |
| 推送失败(APNs/PushKit) | 重试队列;本地通知作基线兜底 |
| 进程重启 | 调度从 `reminders` 表恢复,提醒不丢 |
| embedding 失败 | 笔记照存,embedding 置空,后台补 |

数据清理:`messages`/`notifications` 定期归档(cron 清理超期)。

---

## 17. 分阶段路线图

### P0 — 骨架跑通
- [ ] FastAPI 骨架(`/agent/asr` + `/agent/stream` SSE + `agent_turn()`)
- [ ] Supabase 建表(profiles, devices, sessions, messages)+ Auth
- [ ] Agent Plan LLM 接入(流式)
- [ ] Agent Plan ASR 接入(App PTT,复用 ark-voice)
- [ ] Flutter App 骨架(邮箱登录 + 对话页 + PTT + SSE 显示)
- [ ] 云 VM Docker 部署
- **验收**:登录 → 按住说话 → 看"你说了:…" → 逐字回复

### P1 — 核心数据 CRUD
- [ ] 建表(reminders, notes, todos, notifications)
- [ ] 工具全套 + 确认机制
- [ ] 待办+deadline 自动建提醒;提醒状态机
- [ ] App:提醒/待办/笔记页(直读 Supabase)
- [ ] 模型路由 + 手动选择器
- **验收**:"记个待办周五前还信用卡" → 待办+自动提醒 → 列表可见

### P2 — 提醒与推送
- [ ] 调度器(cron 主 + asyncio 辅)
- [ ] APNs(Apple 账号 + .p8)
- [ ] Push Kit(华为账号 + agconnect)
- [ ] 本地通知同步(iOS 64 条滚动 / 鸿蒙)
- [ ] snooze/确认闭环 UI
- **验收**:设 5 分钟后提醒 → 杀后台 → 手机仍响

### P3 — 记忆
- [ ] OpenViking 长期记忆(user_id)
- [ ] 短期上下文(30 条窗口,30 分钟过期)
- [ ] system prompt 注入记忆+时间+时区
- **验收**:说"我叫小王" → 关 App → 明天新对话问"我叫什么" → 答对

### P4 — 查询
- [ ] 豆包内置搜索开启
- [ ] get_weather + key
- [ ] 笔记向量检索(embedding 维度核实 + hnsw 索引)

### P4.5 — RAG
- [ ] documents+chunks 建表 + 拆块 embedding 管道 + search_knowledge + 上传 UI

### P5 — 主动播报
- [ ] 晨间播报 cron + LLM + 推送;待办到期预警;播报时间配置

### P6 — ESP 接入
- [ ] 后端 `/ws/voice` 流式 ASR/TTS(复用 ark-voice)
- [ ] 定设备连接方案(后端实现设备协议 vs 自定义固件)
- [ ] 设备绑定流程;多入口一致性验证

### 可选 — SPI 摄像头 + VLM
- [ ] ArduCAM Mega 5MP(GPIO1/2/3/17 + I2C)→ 拍照 → VLM

---

## 18. 已知风险

| 风险 | 应对 |
|------|------|
| Flutter 鸿蒙非官方 fork,双平台构建未验证 | P0 先 iOS,鸿蒙并行验证 |
| Agent Plan ASR 整段输入未实测 | P0 实测,备选切帧连发 |
| 豆包内置搜索开启方式未确认 | P4 查集成指南 |
| embedding 维度未知 | P4 接入时确认再建表 |
| iOS 本地通知上限 64 | 滚动窗口 |
| ESP 设备协议自实现工作量 | P6,不阻塞前面 |

---

## 19. 本地资源

### Mac (`~/esp/`)
| 路径 | 内容 |
|------|------|
| `~/esp/xiaozhi-esp32/` | 设备固件 v2.2.6(板子在跑) |
| `~/esp/xiaozhi-esp32-server/` | 社区 server(**本方案不用**,仅参考其流式手法) |
| 本 repo `ESP32-S3-RLCD-4.2-Hardware-Summary.md` | 硬件 pin map |
| 本 repo `agent-design.md` | 前序讨论记录 |

### Windows (`C:\Users\xiaotim\...`)
| 路径 | 内容 |
|------|------|
| `...\FZagentplan\Agent-Plan-集成指南.md` | Agent Plan 完整端点/避坑 |
| `...\ESP\agentplan-tests\ocr_test.py` | VLM-OCR 样例 |
| `.claude\skills\ark-voice\scripts\` | 现成 ASR/TTS 脚本(**后端直接复用**) |

### 账号
| 平台 | 用途 | 状态 |
|------|------|------|
| 火山方舟 Agent Plan | LLM/ASR/TTS | **已有** |
| 火山 Supabase | DB/存储/认证 | 需建 workspace |
| Apple Developer | APNs + iOS 发布 | 需注册 $99/年 |
| 华为开发者联盟 | Push Kit + 鸿蒙发布 | 需注册(免费) |

---

## 20. 协作约定

- 提"讨论"=先讨论不动手;非"独立推进/全力推进"都要先征询;能实测就别凭经验
- Agent Plan key 已脱敏,勿外传
