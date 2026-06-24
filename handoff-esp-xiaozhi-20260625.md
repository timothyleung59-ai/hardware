# Handoff — ESP32-S3-RLCD-4.2 / 小智 AI（ESP 会话）

> 生成于 2026-06-25。给接手本 ESP/小智会话的新 agent。**项目记忆已存**（见下），本文只补记忆/文档没覆盖的状态与下一步，不重复。

## 一句话现状
在 Waveshare **ESP32-S3-RLCD-4.2** 上做**小智 AI(xiaozhi-esp32) 二次开发**；环境已就绪（**IDF 5.5.2**），方向已定（**后端线**），但**后端尚未开搭**。另有一个独立的"合同结构化"小项目已交接给 `pro` 会话。

## 已存的项目记忆（先读，避免重复）
路径：`C:\Users\xiaotim\.claude\projects\C--Users-xiaotim-Documents-Claude-Projects-ESP\memory\`（新会话会自动加载 MEMORY.md）
- `xiaozhi-rlcd-project.md` — 硬件、二次开发基线=上游 78/xiaozhi-esp32 **v2.2.4**、走后端线、瘦设备+服务器架构
- `esp-idf-env-setup.md` — **当前 IDF=5.5.2**（6.0.1 已清）、激活方式、VS Code 配置
- `agent-plan-capabilities.md` — 火山方舟 Agent Plan 总览（key 位置、端点、集成指南路径）
- `agent-plan-vlm-ocr.md` — 实测：doubao-seed-2.0 全系支持图片输入/OCR
- `huoshan-supabase-limits.md` — 火山版 Supabase 能/不能做什么

## 关键环境（接手即用）
- 激活 IDF：`. C:\Users\xiaotim\esp\esp-idf-v5.5.2\export.ps1`（PowerShell 每次新 shell，激活+命令须同一次执行）
- 板级源码（二次开发起点）：上游 v2.2.4 克隆在 `C:\Users\xiaotim\Documents\Claude\Projects\ESP\compare\upstream-v2.2.4`（板定义 `main/boards/waveshare/esp32-s3-rlcd-4.2`）；Waveshare V2.1.0 快照在 `compare\waveshare-repo\...`（仅参考，旧）
- 板子 demo + 硬件文档：`ESP\hardware\`（`ESP32-S3-RLCD-4.2-Hardware-Summary.md`、`hello_world`）
- Agent Plan 集成指南（端点/鉴权/避坑/模型系数，实测）：`C:\Users\xiaotim\Documents\Claude\Projects\FZagentplan\Agent-Plan-集成指南.md`
- Agent Plan key：在 VS Code `settings.json` 的 `ANTHROPIC_AUTH_TOKEN`（**已脱敏，勿外传**）
- VLM-OCR 调用样例：`ESP\agentplan-tests\ocr_test.py`

## 后端线 —— 待你推进（核心）
架构：设备(WS/OPUS) → 自建服务器(ASR→LLM→TTS) → 设备。设备靠改 `CONFIG_OTA_URL`/运行期 ota_url 指向自建服务器（OTA 返回真正 WS 地址）。
**3 个待定决策（尚未拍板，需问用户）：**
1. 服务器跑哪：本机 PC（同 WiFi 最省事）还是云 VM？（Supabase 托不了长连 WS）
2. 语音(ASR/TTS)：全 Agent Plan(自定义 provider) / 先 EdgeTTS+FunASR 跑通 / 标准火山 key
3. 服务器基座：社区 `xinnan-tech/xiaozhi-esp32-server`（自包含、推荐）vs 从零
**建议下一步**：拉社区 server 源码 → 读其 provider 配置架构 → 阶段1 用 Agent Plan LLM(OpenAI 兼容 `/api/plan/v3`) + 临时 EdgeTTS 跑通设备连自建后端。

## 旁支：合同结构化小项目（已交接，不在本会话做）
- 交接文档：`C:\Users\xiaotim\Documents\Claude\Projects\pro\docs\合同信息结构化提取-agent-交接.md`
- 用户将在 `pro` 会话开干；要点：pro 后端已有氚云同步+OCR+AI，**大概率不用 Supabase**。

## Suggested skills
- `brainstorming` / `writing-plans` — 后端线开搭前把 3 个决策和阶段计划理清
- `run` 或项目自带启动方式 — 跑/烧录固件验证（接板子后）
- `claude-api` — 若写代码调 Anthropic/OpenAI 兼容 LLM 接口（Agent Plan 走 OpenAI 兼容端点）
- `verification-before-completion` — 声称"能用"前先实测（本会话风格）

## 坑/注意
- IDF 环境不跨 PowerShell 调用保留；每条命令自带 `. export.ps1`。
- 沙箱会拦 `Remove-Item $变量`（误判根路径）——用字面绝对路径或管道写法。
- `.espressif` 是多版本 IDF **共用**目录，删任何东西前先确认归属（本会话差点整删废掉 5.5.2）。
- `idf.py --version` 显示 `v5.5.2-dirty` 是源码树有改动，正常。
- 用户协作约定：提"讨论"=先讨论不动手；非"独立推进/全力推进"都要先征询；能实测就别凭经验。
