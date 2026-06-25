# Handoff v2 — ESP32-S3-RLCD-4.2 / 小智 AI 自建后端

> 生成于 2026-06-25（Mac 端会话）。接上一份 handoff（同目录 `handoff-esp-xiaozhi-20260625.md`，Windows 端生成）。本文不重复已有内容，只补**新进展和已定决策**。

## 一句话现状

三个后端决策**已拍板**，社区 server 源码**已拉到本地并完成架构分析**，下一步是**部署到云 VM + 配置 Agent Plan + 设备连通**。

## 已定决策（本会话敲定）

| 决策 | 结论 | 备注 |
|------|------|------|
| 服务器跑哪 | **云 VM（已有）** | 7×24 在线 |
| 语音服务 | **全走 Agent Plan** | LLM + ASR + TTS 统一 |
| 服务器基座 | **社区 `xinnan-tech/xiaozhi-esp32-server`** | Python，MIT 协议 |

## 新增本地资源

| 路径 | 内容 |
|------|------|
| `~/esp/xiaozhi-esp32-server/` | 社区 server 源码克隆（GitHub: xinnan-tech/xiaozhi-esp32-server） |
| `~/esp/xiaozhi-esp32/` | 设备固件克隆（v2.2.6，stock，已烧录到板子） |
| `~/esp/laoshi-box-backup/` | "张老师盒子"自定义固件备份（patch + files） |
| 本 repo `ESP32-S3-RLCD-4.2-Hardware-Summary.md` | 完整硬件 pin map（含排母 P1 引脚定义） |

## 社区 Server 架构要点（已研读完毕）

### 核心流程
```
设备(OPUS/WebSocket:8000) → VAD → ASR → LLM → TTS → 音频流回设备
                                          ↕
                                  Tool/MCP/Plugin
```

### Provider 插拔机制
- 配置文件：`config.yaml`（默认）+ `data/.config.yaml`（覆盖）
- Provider 类型：`type: openai` → 复用 OpenAI 兼容 provider，**Agent Plan 直接可用**
- 工厂模式：`core/utils/llm.py` / `asr.py` / `tts.py` 动态加载

### 接 Agent Plan 的配置（已验证路径可行，未实际部署）

```yaml
# data/.config.yaml
selected_module:
  ASR: DoubaoStreamASR       # 火山方舟 ASR（已有 provider）
  LLM: AgentPlanLLM          # Agent Plan（用 openai type）
  TTS: DoubaoTTS             # 火山方舟 TTS（已有 provider）

LLM:
  AgentPlanLLM:
    type: openai
    base_url: https://ark.cn-beijing.volces.com/api/plan/v3
    model_name: <endpoint-id>
    api_key: <AGENT_PLAN_KEY>   # 见 VS Code settings.json，已脱敏勿外传
    temperature: 0.7
    max_tokens: 500
```

### 已有 Provider 清单
- **ASR**: FunASR(免费本地)、豆包流式、阿里、讯飞、百度、腾讯、OpenAI Whisper 等 15+
- **TTS**: EdgeTTS(免费)、豆包、阿里、讯飞、FishSpeech、GPT-SoVITS 等 15+
- **LLM**: OpenAI 兼容(含豆包/DeepSeek/智谱/阿里)、Dify、Gemini、Coze、Ollama 等
- **MCP**: 内置 MCP client，可接 Home Assistant、文件系统等
- **Plugin**: 装饰器注册，已有天气/新闻/音乐/搜索/HA 控制等

### 关键文件速查
| 文件 | 用途 |
|------|------|
| `main/xiaozhi-server/app.py` | 入口 |
| `main/xiaozhi-server/config.yaml` | 默认配置 |
| `main/xiaozhi-server/core/providers/llm/openai/openai.py` | OpenAI 兼容 LLM provider |
| `main/xiaozhi-server/core/connection.py` | 单设备连接处理 |
| `main/xiaozhi-server/plugins_func/functions/` | 自定义插件目录 |
| `main/xiaozhi-server/mcp_server_settings.json` | MCP 服务器配置模板 |
| `Dockerfile-server` + `docker-setup.sh` | Docker 部署 |

## 硬件新发现（本会话）

### 排母 P1 完整引脚定义（从原理图确认）
```
Pin 1: VCC3V3    Pin 2: VBUS(5V)
Pin 3: GND       Pin 4: GND
Pin 5: GPIO0     Pin 6: USB D-
Pin 7: GPIO1 ✅  Pin 8: USB D+
Pin 9: GPIO2 ✅  Pin 10: U0TXD(GPIO43)
Pin 11: GPIO3 ✅ Pin 12: U0RXD(GPIO44)
Pin 13: GPIO17 ✅ Pin 14: I2C SDA(GPIO13)
Pin 15: GPIO18   Pin 16: I2C SCL(GPIO14)
```
✅ = 完全空闲，共 4 个。整块板总空闲 GPIO = 6（加 GPIO7、GPIO42 未引出）。

### SPI 摄像头可行性
- DVP 并口摄像头：❌（需 14 脚，只有 6 个空闲）
- **SPI 摄像头（ArduCAM Mega 5MP）：✅**（只需 4 GPIO + 共用 I2C，刚好够）
- 推荐型号：ArduCAM Mega 5MP（自动对焦，~100¥），拍 1600×1200 JPEG → WiFi 传 VLM
- 端到端延迟：拍照 + 上传 + VLM 推理 ≈ 4-5 秒

## 下一步（部署路线）

1. **云 VM 部署 server**（Docker）→ `docker-setup.sh`
2. **配置 `data/.config.yaml`** → Agent Plan LLM + 选定 ASR/TTS
3. **设备固件改 OTA URL** → 指向云 VM（改 `CONFIG_OTA_URL` 或运行期 `ota_url`）
4. **跑通基础语音对话** → 验证 ASR→LLM→TTS 全链路
5. **加自定义插件** → 记账、提醒、笔记等（对应 agent 设计方案中的 P1 阶段）
6. **（可选）接 SPI 摄像头** → ArduCAM Mega 5MP + VLM 识别

## Agent 设计方案（已讨论，待细化）

用户目标：基于板子 + Agent Plan 做**日常工作/生活事务 AI agent**。已设计 4 层架构（感知→理解→决策→执行）、10 个工具、分阶段路线（P0-P4）。方案在本会话上下文中，未落盘为文件。下一个会话如需细化，建议重新讨论而非试图恢复。

## Suggested skills
- `run` — 在 VM 上启动 server 验证
- `claude-api` — Agent Plan 走 OpenAI 兼容端点，调试时参考
- `verify` — 跑通后验证全链路（设备→server→Agent Plan→设备）
- `handoff` — 部署完成后生成新 handoff

## 坑/注意（补充上一份）
- 本机是 **macOS**（`~/esp/`），上一份 handoff 的路径是 **Windows**（`C:\Users\xiaotim\...`）。两台机器的 ESP-IDF 版本和项目路径不同。
- Mac 上 IDF 激活：`. ~/esp/esp-idf-v5.5.2/export.sh`（不是 PowerShell）
- 社区 server 是 Python 项目，不需要 IDF 环境；部署到云 VM 用 Docker 最省事
- 用户协作约定（不变）：提"讨论"=先讨论不动手；非"独立推进/全力推进"都要先征询；能实测就别凭经验
