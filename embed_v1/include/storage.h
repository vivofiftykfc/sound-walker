/**
 * storage.h - Storage interface for voiceprint data
 *
 * Supports:
 *   - SQLite (primary, for full features)
 *   - Flat file (fallback for BusyBox minimal system)
 */

#ifndef STORAGE_H
#define STORAGE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define MAX_USERS      50
#define MAX_NAME_LEN   32
#define DB_PATH        "/sdcard/voiceprint/voiceprint.db"

/* User information */
typedef struct {
    int user_id;
    char name[MAX_NAME_LEN];
    bool is_active;
    uint32_t enrolled_at;
} user_info_t;

/* Voiceprint data */
typedef struct {
    int id;
    int user_id;
    char feature_type[16];  /* "supervector" or "gmm_params" */
    uint8_t* feature_data;
    size_t feature_len;
} voiceprint_data_t;

/* Operation log */
typedef struct {
    int id;
    char operation[16];     /* "enroll", "verify", "delete" */
    int user_id;
    char result[16];        /* "success", "failed" */
    float confidence;
    uint32_t duration_ms;
    uint32_t timestamp;
} operation_log_t;

/* Storage handle */
typedef struct storage storage_t;

/**
 * Initialize storage
 * @param db_path  Path to database file
 * @return         Storage handle or NULL on error
 */
storage_t* storage_init(const char* db_path);

/**
 * Close storage
 */
void storage_close(storage_t* st);

/* ========== User Management ========== */

/**
 * Add new user
 * @param st   Storage handle
 * @param name User name
 * @return     User ID or -1 on error
 */
int storage_add_user(storage_t* st, const char* name);

/**
 * Delete user
 * @param st      Storage handle
 * @param user_id User ID
 * @return        0 on success, -1 on error
 */
int storage_delete_user(storage_t* st, int user_id);

/**
 * Get user information
 * @param st      Storage handle
 * @param user_id User ID
 * @param info   Output user info
 * @return        0 on success, -1 on error
 */
int storage_get_user(storage_t* st, int user_id, user_info_t* info);

/**
 * List all users
 * @param st     Storage handle
 * @param users  Output array
 * @param count  Input: max users, Output: actual count
 * @return       0 on success, -1 on error
 */
int storage_list_users(storage_t* st, user_info_t* users, int* count);

/* ========== Voiceprint Storage ========== */

/**
 * Save voiceprint
 * @param st   Storage handle
 * @param user_id User ID
 * @param type  Feature type
 * @param data  Feature data
 * @param len   Data length
 * @return      0 on success, -1 on error
 */
int storage_save_voiceprint(storage_t* st, int user_id,
                            const char* type,
                            const uint8_t* data, size_t len);

/**
 * Load voiceprint
 * @param st      Storage handle
 * @param user_id User ID
 * @param type    Feature type
 * @param data   Output buffer
 * @param len    Input: max len, Output: actual len
 * @return        0 on success, -1 on error
 */
int storage_load_voiceprint(storage_t* st, int user_id,
                             const char* type,
                             uint8_t* data, size_t* len);

/* ========== Logging ========== */

/**
 * Log operation
 * @param st         Storage handle
 * @param operation  Operation name
 * @param user_id    User ID (-1 if none)
 * @param result     Result string
 * @param confidence Confidence score (-1 if N/A)
 * @param duration_ms Duration in ms
 * @return           0 on success, -1 on error
 */
int storage_log_operation(storage_t* st, const char* operation,
                          int user_id, const char* result,
                          float confidence, uint32_t duration_ms);

#endif /* STORAGE_H */
