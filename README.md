# PaperPilot · 论文阅读 AI 助手

PaperPilot 是一个面向论文阅读场景的桌面应用：导入 PDF 论文后，自动完成结构化解析与向量化索引，支持针对单篇论文的 **RAG 多 Agent 问答、总结、阅读笔记与批判性阅读**，并配套文献管理、笔记与解析任务中心。

## 系统架构

项目由三个进程协作组成：

```
┌─────────────────────┐         ┌──────────────────────────┐
│  desktop/           │  HTTP   │  server/                 │
│  Qt 6 桌面客户端     │ ──────▶ │  Drogon C++ 后端          │
│  (GUI, 127.0.0.1)   │  JSON   │  :8848                   │
└─────────────────────┘         │   · 登录 / 文献管理 / 笔记 │
                                │   · 任务进度 / 会话持久化  │
                                └───────────┬──────────────┘
                                            │ HTTP
                                            ▼
                 ┌──────────────────────────────────────────┐
                 │  agent_worker/                           │
                 │  Python Agent Worker (FastAPI, :8001)    │
                 │   · GROBID 解析 PDF → 文本块             │
                 │   · Embedding + Qdrant 向量检索          │
                 │   · LangGraph 多 Agent 问答              │
                 └──────┬──────────┬───────────┬────────────┘
                        │          │           │
                 ┌──────▼───┐ ┌────▼─────┐ ┌───▼────────┐
                 │  Qdrant  │ │  GROBID  │ │ LLM /      │
                 │  向量库   │ │  PDF解析  │ │ Embedding  │
                 └──────────┘ └──────────┘ └────────────┘
                                ┌───────────────┐
                                │  MySQL 8      │  ← 由 Drogon 后端直接读写
                                │  (database/   │
                                │   schema.sql) │
                                └───────────────┘
```

**职责边界**：Drogon 后端负责鉴权、业务接口与 MySQL 持久化；Python Worker 只负责 PDF 解析、向量索引与 Agent 问答，不直接访问 MySQL。

## 功能特性

### 桌面端（`desktop/`，Qt 6 Widgets）

- **登录**：账号/密码登录（`/api/auth/login`）
- **论文库**：按关键词 / 领域 / 年份 / 排序筛选论文，卡片式列表，收藏星标，详情页展示摘要与标签
- **PDF 导入**：multipart 流式上传，上传后每 2 秒轮询解析进度，侧边栏任务卡片实时展示
- **AI 对话**：气泡式聊天界面，支持四种模式 —— 论文问答（qa）/ 论文总结（summary）/ 阅读笔记（note）/ 批判性阅读（critique），回答带 `【证据 n】` 引用标注
- **阅读笔记**：论文笔记的新建 / 编辑 / 删除
- **任务中心**：解析任务的进度、状态与结果列表（自动刷新）

### 后端（`server/`，Drogon C++）

| 方法 | 路径 | 说明 |
|---|---|---|
| POST | `/api/auth/login` | 登录 |
| GET | `/api/papers` | 论文列表（搜索 / 筛选 / 分页） |
| POST | `/api/papers` | 上传 PDF（multipart，触发异步解析任务） |
| GET / DELETE | `/api/papers/{paperId}` | 论文详情 / 删除 |
| GET / POST | `/api/papers/{paperId}/notes` | 查询 / 新增笔记 |
| PUT / DELETE | `/api/notes/{noteId}` | 修改 / 删除笔记 |
| PATCH | `/api/papers/{paperId}/favorite` | 切换收藏 |
| GET | `/api/tasks`、`/api/tasks/{taskId}` | 解析任务列表 / 进度 |
| POST | `/api/papers/{paperId}/chat` | AI 对话（携带最近 10 条历史，转发 Worker，结果持久化） |
| WS | `/ws/echo` | WebSocket 回显示例 |
| GET | `/learn/*` | Drogon 特性学习端点（过滤器 / Session / 协程 / Redis / CSP 视图） |

响应统一约定：`{success, message}`，列表用 `items`，详情用 `item`。

### Agent Worker（`agent_worker/`，Python FastAPI）

| 方法 | 路径 | 说明 |
|---|---|---|
| GET | `/health` | 存活检查 |
| POST | `/agent/paper-chat` | 多 Agent 论文问答（四种模式） |
| POST | `/worker/parse-pdf` | GROBID 解析 PDF → 带页码/章节的文本块 |
| POST | `/worker/index-paper` | 文本块 Embedding 后写入 Qdrant |
| GET | `/worker/vector-health` | Qdrant / 向量维度健康检查 |

核心链路：

```
PDF → GROBID(TEI XML) 解析分块 → Drogon 存入 MySQL paper_chunks
    → /worker/index-paper → DashScope text-embedding-v3（1024 维）→ Qdrant
    → 提问 → 同模型编码问题 → 按 paper_id 过滤检索 Top-K 证据
    → LangGraph 多 Agent（coordinator → researcher → writer）→ 带引用的回答
```

- **多 Agent 协作**（`app/multi_agent/`，LangGraph）：coordinator 初始化状态 → researcher 检索本轮证据 → writer 依据证据生成回答；回答经 `answer_validator` 校验 `【证据 n】` 引用编号，无证据时拒绝编造
- **证据约束**：只依据当前论文的检索证据回答，每个事实性结论必须标注证据编号，默认中文
- **可选增强**：`mcp_servers/openalex_server.py` 提供 OpenAlex 学术检索 MCP 工具（stdio），失败时自动降级，不影响论文内问答

### 数据库（`database/`）

MySQL 8 初始化脚本 [`database/schema.sql`](database/schema.sql)：8 张表（`users`、`papers`、`paper_tags`、`paper_notes`、`paper_chunks`、`ai_tasks`、`paper_chat_sessions`、`paper_chat_messages`），设计为**可重复执行的增量迁移脚本**。详见 [`database/README.md`](database/README.md)。

## 目录结构

```
PaperPilot/
├── desktop/          Qt 6 桌面客户端（Widgets + Network，C++17）
│   └── PaperPilot/   登录窗口 / 主窗口（mainfamework）、VS 工程文件
├── server/           Drogon C++ 后端（:8848）
│   ├── controllers/  LearningController、WebSocketEchoController
│   ├── filters/      DemoTokenFilter（请求头 Token 示例）
│   ├── include/      vendored nlohmann/json
│   ├── test/         Drogon 测试框架用例（myapp_test）
│   ├── routes.cpp    全部业务路由（MySQL C API 实现）
│   └── tools.cpp     工具函数（HTTP 客户端封装等）
├── agent_worker/     Python Agent Worker（:8001）
│   ├── app/          FastAPI 应用、RAG 检索、向量服务、多 Agent（LangGraph）
│   ├── app/test/     本地调试 / 冒烟脚本
│   └── mcp_servers/  OpenAlex MCP stdio 服务器
└── database/         MySQL 8 建表脚本与说明
```

各子模块还有更详细的文档：`agent_worker/README.md`、`database/README.md`。

## 技术栈

| 模块 | 技术 |
|---|---|
| 桌面端 | C++17 · Qt 6（Widgets、Network）· CMake / Qt Creator / VS2022 |
| 后端 | C++17/20 · Drogon（HttpController、Filter、Session、WebSocket、协程）· MySQL C API · hiredis（Redis）· nlohmann/json |
| Agent Worker | Python 3.11 · FastAPI · LangChain 1.x · LangGraph 1.x · Qdrant · GROBID · PyMuPDF · Pydantic v2 |
| 数据库 | MySQL 8（utf8mb4） |

## 快速开始

### 环境依赖

- **MySQL 8**（`127.0.0.1:3306`，库名 `paper`）——先执行 `database/schema.sql`
- **Redis**（`127.0.0.1:6379`）
- **Qdrant** 与 **GROBID**（Docker 一键启动，见下）
- **Drogon**（从源码构建并安装，`find_package(Drogon)` 可找到即可）
- **Qt 6**（Widgets、Network 组件；本项目在 Qt 6.12 + MSVC 2022 下开发）
- **Python 3.11**

### 1. 初始化数据库

```sql
-- 在 MySQL 中执行
source database/schema.sql;
```

### 2. 启动 Qdrant 与 GROBID

```bash
# GROBID（PDF 结构化解析，端口 8070）
docker run --rm --init --ulimit core=0 -p 8070:8070 --name paperpilot-grobid grobid/grobid:0.9.1-crf

# Qdrant（向量数据库，宿主机端口 6335/6336）
docker run -d --name paperpilot-qdrant --restart unless-stopped \
  -p 127.0.0.1:6335:6333 -p 127.0.0.1:6336:6334 \
  -v paperpilot_qdrant_data:/qdrant/storage qdrant/qdrant:latest
```

### 3. 启动 Agent Worker（:8001）

```powershell
cd agent_worker
python -m venv .venv
.\.venv\Scripts\Activate.ps1
pip install -r requirements.txt
copy app\.env.example app\.env   # 填入 LLM / Embedding API Key 等配置
uvicorn app.main:app --host 127.0.0.1 --port 8001
```

启动时会强制检查 Qdrant 与 Embedding 服务可用性，任一不可用则拒绝启动。

### 4. 构建并启动 Drogon 后端（:8848）

```powershell
cd server
copy config.local.example.json config.local.json  # 填入本机 MySQL 与 Worker 地址
cmake -B build -G "Visual Studio 17 2022" -A x64 \
      -DDrogon_DIR=D:/path/to/drogon/_install/lib/cmake/Drogon
cmake --build build --config Debug
build\Debug\myapp.exe     # 工作目录须为 server/，配置文件为相对路径
```

可选 CMake 开关：`MYAPP_REDIS_READY`（默认 ON，控制 Redis 示例端点编译）。MySQL 与 Python Worker 地址由 `config.local.json` 读取。

### 5. 构建桌面端

```powershell
cd desktop
copy PaperPilot\desktop.local.example.json PaperPilot\desktop.local.json
cmake -B build -DCMAKE_PREFIX_PATH=D:/Qt/6.12.0/msvc2022_64
cmake --build build --config Debug
```

或用 Qt Creator / Visual Studio（`PaperPilot.sln`）直接打开构建。构建时会把本地桌面配置复制到可执行文件目录。

### 6. 使用

启动桌面端 → 使用数据库中的账号登录 → 导入 PDF → 等待解析完成 → 在「AI 助手」中选择模式开始提问。

## 配置说明

| 文件 | 说明 |
|---|---|
| `server/config.json` | 后端监听端口、Session、Redis 连接、请求体大小限制 |
| `server/config.local.example.json` | 后端本地 MySQL 与 Python Worker 配置模板；复制为 `config.local.json` 后填写，禁止提交 |
| `server/config.redis.example.json` | Redis 客户端配置示例 |
| `desktop/PaperPilot/desktop.local.example.json` | Qt 桌面端后端地址模板；复制为 `desktop.local.json`，禁止提交 |
| `agent_worker/app/.env.example` | Worker 环境变量模板（LLM / Embedding / Qdrant / GROBID / OpenAlex / Tavily），复制为 `.env` 使用 |

## 注意事项

- 本项目为个人学习/演示项目：登录接口为明文密码比对、部分 SQL 为字符串拼接，生产环境应改用密码哈希 + Session/JWT + 参数化查询（代码中已注明）。
- 所有服务默认仅监听/访问 `127.0.0.1`，端口：桌面端→后端 8848，后端→Worker 8001，Qdrant 6335，GROBID 8070。
- 向量索引可随时由 MySQL 中的 `paper_chunks` 重建，Qdrant 不是唯一事实源。
- 上传的 PDF 保存在 `server/uploads/`（运行时数据，已被 `.gitignore` 忽略）。
