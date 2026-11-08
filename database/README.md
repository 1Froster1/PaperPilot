# PaperPilot 数据库

`schema.sql` 是 PaperPilot 的可重复执行建表/迁移脚本，目标数据库为 MySQL 8。

## 表职责

| 表 | 作用 |
| --- | --- |
| `users` | 登录用户 |
| `papers` | 用户导入的论文及解析元数据 |
| `paper_tags` | 一篇论文的多个标签 |
| `paper_notes` | 用户阅读笔记 |
| `paper_chunks` | 从 PDF 提取的可检索正文块，RAG 的核心数据 |
| `ai_tasks` | PDF 解析、摘要等后台任务与进度 |
| `paper_chat_sessions` | 一篇论文的一次或多次 AI 对话会话 |
| `paper_chat_messages` | 对话消息、引用片段和工具调用记录 |

## 真实 PDF 解析后的写入顺序

```text
1. 上传 PDF：写入 papers，status = parsing。
2. 创建 ai_tasks，task_type = parse_pdf，status = pending/running。
3. Python Worker 提取 PDF 内容并切块。
4. 删除该 paper_id 的旧 paper_chunks，再批量插入新 chunks。
5. 更新 papers：abstract_text、page_count、parsed_at、status = ready。
6. 更新 ai_tasks：progress = 100、status = completed、finished_at。
```

`paper_chunks` 的 `chunk_index` 在同一篇论文中唯一；重新解析时先删旧 chunk，避免旧版本内容参与 RAG 检索。
