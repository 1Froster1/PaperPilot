# PaperPilot Python Agent Worker

This folder is the Python side of PaperPilot. It is an independent service,
not a DLL and not a Qt component.

## Boundary between services

```text
Qt -> Drogon (127.0.0.1:8848) -> Python Agent Worker (127.0.0.1:8001)
```

- Qt talks only to Drogon.
- Drogon verifies the logged-in user and reads permitted paper data from MySQL.
- Drogon sends an authorized `PaperChatRequest` to this Worker.
- This Worker runs the Agent and returns structured JSON.
- Drogon saves chat history and sends the final response to Qt.

The Worker should not directly own user accounts, passwords, or MySQL writes.

## Files to complete

- `app/schemas.py`: fixed request/response data contracts.
- `app/agent_service.py`: your main learning file. Complete the TODO functions
  in this order: retrieval -> prompt messages -> LLM -> Agent tools.
- `app/main.py`: HTTP endpoint; normally no need to change it initially.

## Environment setup (when you are ready)

```powershell
cd D:\github\drogon-master\PaperPilot\agent_worker
python -m venv .venv
.\.venv\Scripts\Activate.ps1
pip install -r requirements.txt
Copy-Item .env.example .env
uvicorn app.main:app --host 127.0.0.1 --port 8001 --reload
```

`POST /agent/paper-chat` requires a completed `run_paper_agent()` implementation
and valid LLM environment configuration. `GET /health` does not require an LLM.

## PDF parsing Worker

`POST /worker/parse-pdf` is an internal endpoint for Drogon. It accepts a
local PDF path, extracts text with PyMuPDF, and returns page-aware chunks. It
does **not** write MySQL; Drogon stores the response in `paper_chunks` and
updates `papers` / `ai_tasks` in one database workflow.

Before exposing this endpoint beyond localhost, set `PDF_UPLOAD_ROOT` in
`.env` to the absolute directory in which Drogon stores uploaded PDFs.

## First development milestone

Implement `retrieve_relevant_chunks()` without an LLM: select the 3 most
relevant `paper.chunks`, return them as citations, then build a fixed mock
answer. After that is stable, connect a real model in `create_chat_model()`.
