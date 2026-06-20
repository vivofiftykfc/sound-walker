#ifndef STORAGE_MOCK_H
#define STORAGE_MOCK_H

#include "config.h"

int storage_mock_init(const char *db_path);
int storage_mock_close(void);
int storage_mock_add_user(const char *name, int *user_id_out);
int storage_mock_delete_user(int user_id);
int storage_mock_save_voiceprint(int user_id, const void *data, size_t size);
int storage_mock_load_voiceprint(int user_id, void *data, size_t *size_out);
int storage_mock_list_users(int *ids, int max, int *count_out);
void storage_mock_print_all(void);

#endif /* STORAGE_MOCK_H */
