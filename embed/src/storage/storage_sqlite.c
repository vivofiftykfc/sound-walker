
#include "storage.h"
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Include SQLite directly (amalgamation) */
#include "sqlite3.h"

struct storage {
    sqlite3* db;
    char* db_path;
};

/* SQL statements */
static const char* CREATE_TABLES =
    "CREATE TABLE IF NOT EXISTS users ("
    "   user_id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "   name TEXT NOT NULL,"
    "   is_active INTEGER DEFAULT 1,"
    "   enrolled_at INTEGER DEFAULT (strftime('%s', 'now'))"
    ");"

    "CREATE TABLE IF NOT EXISTS voiceprints ("
    "   id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "   user_id INTEGER NOT NULL,"
    "   feature_type TEXT NOT NULL,"
    "   feature_data BLOB NOT NULL,"
    "   created_at INTEGER DEFAULT (strftime('%s', 'now')),"
    "   FOREIGN KEY (user_id) REFERENCES users(user_id)"
    ");"

    "CREATE TABLE IF NOT EXISTS operation_log ("
    "   id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "   timestamp INTEGER DEFAULT (strftime('%s', 'now')),"
    "   operation TEXT NOT NULL,"
    "   user_id INTEGER,"
    "   result TEXT NOT NULL,"
    "   confidence REAL,"
    "   duration_ms INTEGER"
    ");"

    "CREATE INDEX IF NOT EXISTS idx_voiceprints_user "
    "ON voiceprints(user_id);";

storage_t* storage_init(const char* db_path) {
    storage_t* st = calloc(1, sizeof(storage_t));
    if (!st) return NULL;

    st->db_path = strdup(db_path ? db_path : CFG_DB_PATH);

    int rc = sqlite3_open(st->db_path, &st->db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(st->db));
        free(st->db_path);
        free(st);
        return NULL;
    }

    sqlite3_exec(st->db, "PRAGMA journal_mode=WAL;", NULL, NULL, NULL);

    char* err_msg = NULL;
    rc = sqlite3_exec(st->db, CREATE_TABLES, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n", err_msg);
        sqlite3_free(err_msg);
        storage_close(st);
        return NULL;
    }

    return st;
}

void storage_close(storage_t* st) {
    if (!st) return;
    if (st->db) sqlite3_close(st->db);
    free(st->db_path);
    free(st);
}

/* User management */
int storage_add_user(storage_t* st, const char* name) {
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(st->db,
        "INSERT INTO users (name) VALUES (?)", -1, &stmt, NULL);
    if (rc != SQLITE_OK) return -1;

    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc == SQLITE_DONE) {
        return (int)sqlite3_last_insert_rowid(st->db);
    }
    return -1;
}

int storage_delete_user(storage_t* st, int user_id) {
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(st->db,
        "DELETE FROM users WHERE user_id = ?", -1, &stmt, NULL);
    if (rc != SQLITE_OK) return -1;

    sqlite3_bind_int(stmt, 1, user_id);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return (rc == SQLITE_DONE) ? 0 : -1;
}

int storage_get_user(storage_t* st, int user_id, user_info_t* info) {
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(st->db,
        "SELECT user_id, name, is_active, enrolled_at FROM users WHERE user_id = ?",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) return -1;

    sqlite3_bind_int(stmt, 1, user_id);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        info->user_id = sqlite3_column_int(stmt, 0);
        strncpy(info->name, (const char*)sqlite3_column_text(stmt, 1), MAX_NAME_LEN - 1);
        info->is_active = sqlite3_column_int(stmt, 2) != 0;
        info->enrolled_at = sqlite3_column_int(stmt, 3);
        sqlite3_finalize(stmt);
        return 0;
    }

    sqlite3_finalize(stmt);
    return -1;
}

int storage_list_users(storage_t* st, user_info_t* users, int* count) {
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(st->db,
        "SELECT user_id, name, is_active, enrolled_at FROM users WHERE is_active = 1",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) return -1;

    int n = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW && n < *count) {
        users[n].user_id = sqlite3_column_int(stmt, 0);
        strncpy(users[n].name, (const char*)sqlite3_column_text(stmt, 1), MAX_NAME_LEN - 1);
        users[n].is_active = sqlite3_column_int(stmt, 2) != 0;
        users[n].enrolled_at = sqlite3_column_int(stmt, 3);
        n++;
    }
    sqlite3_finalize(stmt);

    *count = n;
    return 0;
}

/* Voiceprint storage */
int storage_save_voiceprint(storage_t* st, int user_id,
                            const char* type,
                            const uint8_t* data, size_t len) {
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(st->db,
        "INSERT INTO voiceprints (user_id, feature_type, feature_data) VALUES (?, ?, ?)",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) return -1;

    sqlite3_bind_int(stmt, 1, user_id);
    sqlite3_bind_text(stmt, 2, type, -1, SQLITE_TRANSIENT);
    sqlite3_bind_blob(stmt, 3, data, len, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return (rc == SQLITE_DONE) ? 0 : -1;
}

int storage_load_voiceprint(storage_t* st, int user_id,
                             const char* type,
                             uint8_t* data, size_t* len) {
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(st->db,
        "SELECT feature_data FROM voiceprints WHERE user_id = ? AND feature_type = ? "
        "ORDER BY created_at DESC LIMIT 1",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) return -1;

    sqlite3_bind_int(stmt, 1, user_id);
    sqlite3_bind_text(stmt, 2, type, -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const void* blob = sqlite3_column_blob(stmt, 0);
        size_t blob_len = sqlite3_column_bytes(stmt, 0);

        if (blob_len <= *len) {
            memcpy(data, blob, blob_len);
            *len = blob_len;
            sqlite3_finalize(stmt);
            return 0;
        }
    }

    sqlite3_finalize(stmt);
    return -1;
}

/* Logging */
int storage_log_operation(storage_t* st, const char* operation,
                          int user_id, const char* result,
                          float confidence, uint32_t duration_ms) {
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(st->db,
        "INSERT INTO operation_log (operation, user_id, result, confidence, duration_ms) "
        "VALUES (?, ?, ?, ?, ?)",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) return -1;

    sqlite3_bind_text(stmt, 1, operation, -1, SQLITE_TRANSIENT);
    if (user_id >= 0) sqlite3_bind_int(stmt, 2, user_id);
    else sqlite3_bind_null(stmt, 2);
    sqlite3_bind_text(stmt, 3, result, -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 4, confidence);
    sqlite3_bind_int(stmt, 5, duration_ms);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return (rc == SQLITE_DONE) ? 0 : -1;
}
