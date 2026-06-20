/**
 * storage.c - Storage interface (routes to SQLite implementation)
 */

#include "storage.h"

/* Forward to sqlite implementation */
storage_t* storage_init(const char* db_path);
void storage_close(storage_t* st);
int storage_add_user(storage_t* st, const char* name);
int storage_delete_user(storage_t* st, int user_id);
int storage_get_user(storage_t* st, int user_id, user_info_t* info);
int storage_list_users(storage_t* st, user_info_t* users, int* count);
int storage_save_voiceprint(storage_t* st, int user_id,
                            const char* type,
                            const uint8_t* data, size_t len);
int storage_load_voiceprint(storage_t* st, int user_id,
                             const char* type,
                             uint8_t* data, size_t* len);
int storage_log_operation(storage_t* st, const char* operation,
                          int user_id, const char* result,
                          float confidence, uint32_t duration_ms);

/* Re-export with wrapper */
#include "storage_sqlite.c"
