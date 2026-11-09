-- PaperPilot database schema / incremental migration.
-- Target: MySQL 8.0.37+; database: paper.
-- This script is designed to preserve the tables and data that already exist.

CREATE DATABASE IF NOT EXISTS `paper`
    DEFAULT CHARACTER SET utf8mb4
    COLLATE utf8mb4_0900_ai_ci;

USE `paper`;

-- MySQL 8.0 does not support ADD COLUMN IF NOT EXISTS. These two small
-- helpers keep the migration repeatable without dropping user data.
DROP PROCEDURE IF EXISTS `paperpilot_add_column_if_absent`;
DROP PROCEDURE IF EXISTS `paperpilot_add_index_if_absent`;

DELIMITER $$
CREATE PROCEDURE `paperpilot_add_column_if_absent`(
    IN table_name_in VARCHAR(64),
    IN column_name_in VARCHAR(64),
    IN column_definition_in TEXT
)
BEGIN
    IF NOT EXISTS (
        SELECT 1
        FROM information_schema.columns
        WHERE table_schema = DATABASE()
          AND table_name = table_name_in
          AND column_name = column_name_in
    ) THEN
        SET @paperpilot_sql = CONCAT(
            'ALTER TABLE `', table_name_in, '` ADD COLUMN `', column_name_in,
            '` ', column_definition_in
        );
        PREPARE paperpilot_stmt FROM @paperpilot_sql;
        EXECUTE paperpilot_stmt;
        DEALLOCATE PREPARE paperpilot_stmt;
    END IF;
END$$

CREATE PROCEDURE `paperpilot_add_index_if_absent`(
    IN table_name_in VARCHAR(64),
    IN index_name_in VARCHAR(64),
    IN index_columns_in TEXT
)
BEGIN
    IF NOT EXISTS (
        SELECT 1
        FROM information_schema.statistics
        WHERE table_schema = DATABASE()
          AND table_name = table_name_in
          AND index_name = index_name_in
    ) THEN
        SET @paperpilot_sql = CONCAT(
            'CREATE INDEX `', index_name_in, '` ON `', table_name_in,
            '` ', index_columns_in
        );
        PREPARE paperpilot_stmt FROM @paperpilot_sql;
        EXECUTE paperpilot_stmt;
        DEALLOCATE PREPARE paperpilot_stmt;
    END IF;
END$$
DELIMITER ;

-- Existing application tables -------------------------------------------------

CREATE TABLE IF NOT EXISTS `users` (
    `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    `account` VARCHAR(64) NOT NULL,
    `password_hash` VARCHAR(255) NOT NULL,
    `display_name` VARCHAR(100) NOT NULL,
    `created_at` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    `updated_at` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP
        ON UPDATE CURRENT_TIMESTAMP,
    PRIMARY KEY (`id`),
    UNIQUE KEY `uk_users_account` (`account`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS `papers` (
    `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    `user_id` BIGINT UNSIGNED NOT NULL,
    `title` VARCHAR(500) NOT NULL,
    `authors` VARCHAR(1000) DEFAULT NULL,
    `publish_year` SMALLINT UNSIGNED DEFAULT NULL,
    `source` VARCHAR(100) DEFAULT NULL,
    `source_url` VARCHAR(1000) DEFAULT NULL,
    `abstract_text` TEXT DEFAULT NULL,
    `pdf_path` VARCHAR(1000) NOT NULL,
    `status` ENUM('uploaded', 'parsing', 'indexing', 'ready', 'failed')
        NOT NULL DEFAULT 'uploaded',
    `is_favorite` TINYINT(1) NOT NULL DEFAULT 0,
    `created_at` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    `updated_at` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP
        ON UPDATE CURRENT_TIMESTAMP,
    PRIMARY KEY (`id`),
    KEY `idx_papers_user_created` (`user_id`, `created_at`),
    KEY `idx_papers_user_year` (`user_id`, `publish_year`),
    KEY `idx_papers_user_favorite` (`user_id`, `is_favorite`),
    CONSTRAINT `fk_papers_user`
        FOREIGN KEY (`user_id`) REFERENCES `users` (`id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

-- PDF 上传与真实解析的补充元数据。NULL 表示旧数据或当前步骤尚未产生该值。
CALL paperpilot_add_column_if_absent(
    'papers', 'original_filename',
    "VARCHAR(512) DEFAULT NULL COMMENT '用户上传时的原始 PDF 文件名'"
);
CALL paperpilot_add_column_if_absent(
    'papers', 'file_size',
    "BIGINT UNSIGNED DEFAULT NULL COMMENT 'PDF 文件字节数'"
);
CALL paperpilot_add_column_if_absent(
    'papers', 'page_count',
    "INT UNSIGNED DEFAULT NULL COMMENT '解析得到的 PDF 页数'"
);
CALL paperpilot_add_column_if_absent(
    'papers', 'parsed_at',
    "DATETIME DEFAULT NULL COMMENT '真实 PDF 解析成功的时间'"
);
CALL paperpilot_add_column_if_absent(
    'papers', 'parse_error',
    "TEXT DEFAULT NULL COMMENT '最近一次解析失败原因'"
);
CALL paperpilot_add_index_if_absent(
    'papers', 'idx_papers_user_status', '(`user_id`, `status`)'
);

CREATE TABLE IF NOT EXISTS `paper_tags` (
    `paper_id` BIGINT UNSIGNED NOT NULL,
    `tag_name` VARCHAR(100) NOT NULL,
    PRIMARY KEY (`paper_id`, `tag_name`),
    CONSTRAINT `fk_paper_tags_paper`
        FOREIGN KEY (`paper_id`) REFERENCES `papers` (`id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS `paper_notes` (
    `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    `paper_id` BIGINT UNSIGNED NOT NULL,
    `title` VARCHAR(255) DEFAULT NULL COMMENT '笔记标题；旧笔记允许为空',
    `content` TEXT NOT NULL,
    `created_at` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    `updated_at` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP
        ON UPDATE CURRENT_TIMESTAMP,
    PRIMARY KEY (`id`),
    KEY `idx_notes_paper_created` (`paper_id`, `created_at`),
    CONSTRAINT `fk_notes_paper`
        FOREIGN KEY (`paper_id`) REFERENCES `papers` (`id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CALL paperpilot_add_column_if_absent(
    'paper_notes', 'title',
    "VARCHAR(255) DEFAULT NULL COMMENT '笔记标题；旧笔记允许为空'"
);

-- Real PDF parsing output -----------------------------------------------------
-- One row is one retrievable text chunk. This is the source of
-- PaperChatRequest.paper.chunks for the Python RAG Agent.
CREATE TABLE IF NOT EXISTS `paper_chunks` (
    `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    `paper_id` BIGINT UNSIGNED NOT NULL,
    `chunk_index` INT UNSIGNED NOT NULL COMMENT '该论文内从 0 开始的文本块序号',
    `content` MEDIUMTEXT NOT NULL COMMENT '从 PDF 提取并清洗后的原文',
    `page_start` INT UNSIGNED DEFAULT NULL COMMENT '文本块起始页码，从 1 开始',
    `page_end` INT UNSIGNED DEFAULT NULL COMMENT '文本块结束页码，从 1 开始',
    `section` VARCHAR(255) DEFAULT NULL COMMENT '章节名，例如 Introduction',
    `char_count` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'content 字符数',
    `token_count` INT UNSIGNED DEFAULT NULL COMMENT '可选：模型分词后的 token 数',
    `content_hash` CHAR(64) DEFAULT NULL COMMENT 'SHA-256，用于避免重复写入',
    `created_at` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    `updated_at` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP
        ON UPDATE CURRENT_TIMESTAMP,
    PRIMARY KEY (`id`),
    UNIQUE KEY `uk_paper_chunks_index` (`paper_id`, `chunk_index`),
    KEY `idx_paper_chunks_paper_page` (`paper_id`, `page_start`),
    KEY `idx_paper_chunks_hash` (`paper_id`, `content_hash`),
    CONSTRAINT `fk_paper_chunks_paper`
        FOREIGN KEY (`paper_id`) REFERENCES `papers` (`id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci
  COMMENT='PDF 解析后的可检索文本块';

-- Background task records -----------------------------------------------------
CREATE TABLE IF NOT EXISTS `ai_tasks` (
    `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    `paper_id` BIGINT UNSIGNED DEFAULT NULL,
    `task_type` VARCHAR(64) NOT NULL,
    `status` VARCHAR(32) NOT NULL DEFAULT 'pending',
    `progress` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `result_text` MEDIUMTEXT DEFAULT NULL,
    `error_message` TEXT DEFAULT NULL,
    `created_at` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    `updated_at` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP
        ON UPDATE CURRENT_TIMESTAMP,
    PRIMARY KEY (`id`),
    KEY `idx_tasks_status_created` (`status`, `created_at`),
    KEY `fk_tasks_paper` (`paper_id`),
    CONSTRAINT `fk_tasks_paper`
        FOREIGN KEY (`paper_id`) REFERENCES `papers` (`id`) ON DELETE SET NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CALL paperpilot_add_column_if_absent(
    'ai_tasks', 'task_payload',
    "JSON DEFAULT NULL COMMENT '任务输入，例如 PDF 路径、解析选项'"
);
CALL paperpilot_add_column_if_absent(
    'ai_tasks', 'worker_name',
    "VARCHAR(64) DEFAULT NULL COMMENT '实际执行任务的 Worker 名称'"
);
CALL paperpilot_add_column_if_absent(
    'ai_tasks', 'started_at', 'DATETIME DEFAULT NULL'
);
CALL paperpilot_add_column_if_absent(
    'ai_tasks', 'finished_at', 'DATETIME DEFAULT NULL'
);
CALL paperpilot_add_column_if_absent(
    'ai_tasks', 'retry_count', 'INT UNSIGNED NOT NULL DEFAULT 0'
);

-- Future Agent chat persistence ----------------------------------------------
CREATE TABLE IF NOT EXISTS `paper_chat_sessions` (
    `id` CHAR(36) CHARACTER SET ascii NOT NULL COMMENT 'UUID，由 Drogon 创建',
    `user_id` BIGINT UNSIGNED NOT NULL,
    `paper_id` BIGINT UNSIGNED NOT NULL,
    `title` VARCHAR(255) DEFAULT NULL COMMENT '会话标题，可由首个问题生成',
    `created_at` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    `updated_at` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP
        ON UPDATE CURRENT_TIMESTAMP,
    PRIMARY KEY (`id`),
    KEY `idx_chat_sessions_user_paper_updated` (`user_id`, `paper_id`, `updated_at`),
    CONSTRAINT `fk_chat_sessions_user`
        FOREIGN KEY (`user_id`) REFERENCES `users` (`id`) ON DELETE CASCADE,
    CONSTRAINT `fk_chat_sessions_paper`
        FOREIGN KEY (`paper_id`) REFERENCES `papers` (`id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci
  COMMENT='单篇论文的多轮 AI 对话会话';

CREATE TABLE IF NOT EXISTS `paper_chat_messages` (
    `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    `paper_id` BIGINT UNSIGNED NOT NULL,
    `user_id` BIGINT UNSIGNED NOT NULL,
    `role` ENUM('user', 'assistant') NOT NULL,
    `content` MEDIUMTEXT NOT NULL,
    `citations_json` JSON DEFAULT NULL COMMENT 'AI 回答使用的 Citation 数组',
    `created_at` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,

    -- 以下三项为将来接入多轮会话和 Agent 调试保留；普通消息写入时可为 NULL。
    `session_id` CHAR(36) CHARACTER SET ascii DEFAULT NULL,
    `request_id` VARCHAR(64) DEFAULT NULL COMMENT '一次 Python Agent 调用的链路追踪 ID',
    `tool_traces_json` JSON DEFAULT NULL COMMENT 'Agent ToolTrace 数组',
    PRIMARY KEY (`id`),
    KEY `idx_chat_messages_paper_created` (`paper_id`, `created_at`),
    KEY `idx_chat_messages_user_created` (`user_id`, `created_at`),
    KEY `idx_chat_messages_session_created` (`session_id`, `created_at`),
    KEY `idx_chat_messages_request` (`request_id`),
    CONSTRAINT `fk_chat_messages_paper`
        FOREIGN KEY (`paper_id`) REFERENCES `papers` (`id`) ON DELETE CASCADE,
    CONSTRAINT `fk_chat_messages_user`
        FOREIGN KEY (`user_id`) REFERENCES `users` (`id`) ON DELETE CASCADE,
    CONSTRAINT `fk_chat_messages_session`
        FOREIGN KEY (`session_id`) REFERENCES `paper_chat_sessions` (`id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci
  COMMENT='论文 AI 对话消息与引用记录';

-- Upgrade databases created before chat sessions and Agent traces existed.
CALL paperpilot_add_column_if_absent(
    'paper_chat_messages', 'session_id',
    'CHAR(36) CHARACTER SET ascii DEFAULT NULL'
);
CALL paperpilot_add_column_if_absent(
    'paper_chat_messages', 'request_id',
    "VARCHAR(64) DEFAULT NULL COMMENT '一次 Python Agent 调用的链路追踪 ID'"
);
CALL paperpilot_add_column_if_absent(
    'paper_chat_messages', 'tool_traces_json',
    "JSON DEFAULT NULL COMMENT 'Agent ToolTrace 数组'"
);
CALL paperpilot_add_index_if_absent(
    'paper_chat_messages', 'idx_chat_messages_session_created',
    '(`session_id`, `created_at`)'
);
CALL paperpilot_add_index_if_absent(
    'paper_chat_messages', 'idx_chat_messages_request',
    '(`request_id`)'
);

DROP PROCEDURE IF EXISTS `paperpilot_add_column_if_absent`;
DROP PROCEDURE IF EXISTS `paperpilot_add_index_if_absent`;
