/**
 * storage_mock.c - 存储模块Mock实现 (PC测试用)
 *
 * 使用二进制文件模拟嵌入式存储，支持最多MAX_USERS个用户
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#include "../include/config.h"

/* ============================================================
 * 数据结构定义
 * ============================================================ */

/* 用户结构体 */
typedef struct {
    uint32_t id;
    char name[MAX_NAME_LEN];
    uint8_t is_enrolled;       /* 是否已注册声纹 */
    uint8_t reserved[3];       /* 对齐填充 */
    time_t created_at;
    time_t updated_at;
} user_t;

/* 声纹模板结构体 (可变长) */
typedef struct {
    uint32_t user_id;
    uint32_t template_size;    /* 模板数据字节数 */
    float covariance[GMM_N_COMPONENTS * GMM_DIM * GMM_DIM];  /* GMM协方差矩阵 */
    float means[GMM_N_COMPONENTS * GMM_DIM];                 /* GMM均值 */
    float weights[GMM_N_COMPONENTS];                         /* GMM权重 */
    float super_vector[GMM_DIM * GMM_N_COMPONENTS];          /* 超向量 */
    time_t created_at;
} voiceprint_template_t;

/* 数据库文件头 */
typedef struct {
    char magic[8];             /* "VOICEDB\0" */
    uint32_t version;          /* 版本号 */
    uint32_t user_count;       /* 用户数量 */
    uint32_t flags;            /* 保留标志 */
} db_header_t;

#define DB_MAGIC "VOICEDB"
#define DB_VERSION 1

/* ============================================================
 * 全局状态
 * ============================================================ */

static user_t g_users[MAX_USERS];
static uint32_t g_user_count = 0;
static uint8_t g_initialized = 0;
static char g_db_path[256] = DATABASE_PATH;

/* ============================================================
 * 内部函数声明
 * ============================================================ */

static int save_db(void);
static int load_db(void);
static user_t* find_user_by_id(uint32_t id);
static user_t* find_empty_slot(void);
static int generate_user_id(void);

/* ============================================================
 * 调试打印函数
 * ============================================================ */

static void print_user_info(const user_t* user) {
    if (!user) {
        LOG_DEBUG("User: NULL");
        return;
    }
    LOG_DEBUG("User[id=%u, name=%s, enrolled=%u, created=%ld]",
              user->id, user->name, user->is_enrolled, (long)user->created_at);
}

static void print_db_status(void) {
    LOG_DEBUG("Database status: users=%u, path=%s", g_user_count, g_db_path);
}

/* ============================================================
 * 数据库文件操作
 * ============================================================ */

/**
 * 保存数据库到文件
 */
static int save_db(void) {
    FILE* fp = fopen(g_db_path, "wb");
    if (!fp) {
        LOG_ERROR("Failed to open database for writing: %s", g_db_path);
        return ERR_FILE_WRITE;
    }

    /* 写文件头 */
    db_header_t header;
    memset(&header, 0, sizeof(header));
    memcpy(header.magic, DB_MAGIC, 7);
    header.version = DB_VERSION;
    header.user_count = g_user_count;

    if (fwrite(&header, sizeof(header), 1, fp) != 1) {
        LOG_ERROR("Failed to write database header");
        fclose(fp);
        return ERR_FILE_WRITE;
    }

    /* 写用户数据 */
    if (g_user_count > 0) {
        if (fwrite(g_users, sizeof(user_t), g_user_count, fp) != g_user_count) {
            LOG_ERROR("Failed to write user data");
            fclose(fp);
            return ERR_FILE_WRITE;
        }
    }

    /* 写声纹模板数据 */
    for (uint32_t i = 0; i < g_user_count; i++) {
        if (g_users[i].is_enrolled) {
            voiceprint_template_t tpl;
            memset(&tpl, 0, sizeof(tpl));
            tpl.user_id = g_users[i].id;
            tpl.template_size = sizeof(tpl);

            /* 模拟生成随机模板数据 (实际应用中这里加载真实模板) */
            for (int j = 0; j < GMM_N_COMPONENTS * GMM_DIM; j++) {
                tpl.means[j] = (float)(rand() % 100) / 100.0f;
                if (j < GMM_N_COMPONENTS) {
                    tpl.weights[j] = 1.0f / GMM_N_COMPONENTS;
                }
            }
            for (int j = 0; j < GMM_N_COMPONENTS * GMM_DIM * GMM_DIM; j++) {
                tpl.covariance[j] = (j % (GMM_DIM + 1)) == 0 ? 1.0f : 0.0f;
            }
            tpl.created_at = time(NULL);

            if (fwrite(&tpl, sizeof(tpl), 1, fp) != 1) {
                LOG_ERROR("Failed to write voiceprint template for user %u", g_users[i].id);
                fclose(fp);
                return ERR_FILE_WRITE;
            }
        }
    }

    fclose(fp);
    LOG_INFO("Database saved: %s (users=%u)", g_db_path, g_user_count);
    return ERR_OK;
}

/**
 * 从文件加载数据库
 */
static int load_db(void) {
    FILE* fp = fopen(g_db_path, "rb");
    if (!fp) {
        LOG_INFO("No existing database found, starting fresh: %s", g_db_path);
        return ERR_OK;  /* 不是错误，只是首次运行 */
    }

    /* 读文件头 */
    db_header_t header;
    if (fread(&header, sizeof(header), 1, fp) != 1) {
        LOG_ERROR("Failed to read database header");
        fclose(fp);
        return ERR_FILE_READ;
    }

    /* 验证魔数 */
    if (memcmp(header.magic, DB_MAGIC, 7) != 0) {
        LOG_ERROR("Invalid database magic number");
        fclose(fp);
        return ERR_FILE_READ;
    }

    /* 验证版本 */
    if (header.version != DB_VERSION) {
        LOG_WARN("Database version mismatch: expected %d, got %u",
                 DB_VERSION, header.version);
    }

    /* 读取用户数据 */
    if (header.user_count > MAX_USERS) {
        LOG_ERROR("Database user count %u exceeds MAX_USERS %u",
                  header.user_count, MAX_USERS);
        fclose(fp);
        return ERR_FILE_READ;
    }

    if (header.user_count > 0) {
        if (fread(g_users, sizeof(user_t), header.user_count, fp) != header.user_count) {
            LOG_ERROR("Failed to read user data");
            fclose(fp);
            return ERR_FILE_READ;
        }
        g_user_count = header.user_count;
    }

    /* 跳过声纹模板数据 (PC测试时不需要实际加载) */
    /* 实际实现中应该加载模板到内存 */

    fclose(fp);
    LOG_INFO("Database loaded: %s (users=%u)", g_db_path, g_user_count);
    return ERR_OK;
}

/* ============================================================
 * 辅助函数
 * ============================================================ */

/**
 * 根据ID查找用户
 */
static user_t* find_user_by_id(uint32_t id) {
    for (uint32_t i = 0; i < g_user_count; i++) {
        if (g_users[i].id == id && g_users[i].name[0] != '\0') {
            return &g_users[i];
        }
    }
    return NULL;
}

/**
 * 查找空槽位
 */
static user_t* find_empty_slot(void) {
    if (g_user_count >= MAX_USERS) {
        return NULL;
    }
    return &g_users[g_user_count];
}

/**
 * 生成新用户ID
 */
static int generate_user_id(void) {
    uint32_t max_id = 0;
    for (uint32_t i = 0; i < g_user_count; i++) {
        if (g_users[i].id > max_id) {
            max_id = g_users[i].id;
        }
    }
    return (int)(max_id + 1);
}

/* ============================================================
 * API实现
 * ============================================================ */

/**
 * 初始化存储模块
 */
int storage_init(void) {
    if (g_initialized) {
        LOG_WARN("Storage already initialized");
        return ERR_OK;
    }

    LOG_INFO("Initializing storage module...");
    memset(g_users, 0, sizeof(g_users));
    g_user_count = 0;

    int ret = load_db();
    if (ret != ERR_OK) {
        LOG_ERROR("Failed to load database: %s", err_str(ret));
        return ret;
    }

    g_initialized = 1;
    print_db_status();
    LOG_INFO("Storage module initialized successfully");
    return ERR_OK;
}

/**
 * 添加用户
 * @param name 用户名
 * @param out_id 输出用户ID
 */
int storage_add_user(const char* name, uint32_t* out_id) {
    if (!g_initialized) {
        LOG_ERROR("Storage not initialized");
        return ERR_DATABASE;
    }

    if (!name || name[0] == '\0') {
        LOG_ERROR("Invalid user name");
        return ERR_INVALID_PARAM;
    }

    if (strlen(name) >= MAX_NAME_LEN) {
        LOG_ERROR("User name too long (max %d)", MAX_NAME_LEN - 1);
        return ERR_INVALID_PARAM;
    }

    /* 检查是否已存在同名用户 */
    for (uint32_t i = 0; i < g_user_count; i++) {
        if (strcmp(g_users[i].name, name) == 0) {
            LOG_ERROR("User already exists: %s", name);
            return ERR_USER_EXISTS;
        }
    }

    /* 查找空槽位 */
    user_t* user = find_empty_slot();
    if (!user) {
        LOG_ERROR("Maximum user limit reached: %d", MAX_USERS);
        return ERR_DATABASE;
    }

    /* 初始化用户 */
    memset(user, 0, sizeof(user_t));
    user->id = generate_user_id();
    strncpy(user->name, name, MAX_NAME_LEN - 1);
    user->name[MAX_NAME_LEN - 1] = '\0';
    user->is_enrolled = 0;
    user->created_at = time(NULL);
    user->updated_at = time(NULL);

    g_user_count++;

    /* 保存到文件 */
    int ret = save_db();
    if (ret != ERR_OK) {
        LOG_ERROR("Failed to save database after adding user");
        return ret;
    }

    if (out_id) {
        *out_id = user->id;
    }

    LOG_INFO("User added: id=%u, name=%s", user->id, user->name);
    return ERR_OK;
}

/**
 * 保存声纹模板
 * @param user_id 用户ID
 * @param template 模板数据指针
 * @param template_size 模板大小
 */
int storage_save_voiceprint(uint32_t user_id, const void* template, uint32_t template_size) {
    if (!g_initialized) {
        LOG_ERROR("Storage not initialized");
        return ERR_DATABASE;
    }

    if (!template || template_size == 0) {
        LOG_ERROR("Invalid template data");
        return ERR_INVALID_PARAM;
    }

    /* 查找用户 */
    user_t* user = find_user_by_id(user_id);
    if (!user) {
        LOG_ERROR("User not found: id=%u", user_id);
        return ERR_USER_NOT_FOUND;
    }

    /* 标记为已注册 (实际应用中这里应该保存模板数据) */
    user->is_enrolled = 1;
    user->updated_at = time(NULL);

    /* 保存到文件 */
    int ret = save_db();
    if (ret != ERR_OK) {
        LOG_ERROR("Failed to save database after saving voiceprint");
        return ret;
    }

    LOG_INFO("Voiceprint saved: user_id=%u, size=%u", user_id, template_size);
    return ERR_OK;
}

/**
 * 加载声纹模板
 * @param user_id 用户ID
 * @param template 输出模板数据缓冲区
 * @param template_size 输入:缓冲区大小, 输出:实际模板大小
 */
int storage_load_voiceprint(uint32_t user_id, void* template, uint32_t* template_size) {
    if (!g_initialized) {
        LOG_ERROR("Storage not initialized");
        return ERR_DATABASE;
    }

    if (!template_size) {
        LOG_ERROR("template_size pointer is NULL");
        return ERR_INVALID_PARAM;
    }

    /* 查找用户 */
    user_t* user = find_user_by_id(user_id);
    if (!user) {
        LOG_ERROR("User not found: id=%u", user_id);
        return ERR_USER_NOT_FOUND;
    }

    if (!user->is_enrolled) {
        LOG_ERROR("User not enrolled: id=%u", user_id);
        return ERR_ENROLLMENT;
    }

    /* PC测试用: 生成模拟模板数据 */
    uint32_t needed_size = sizeof(voiceprint_template_t);
    if (*template_size < needed_size) {
        LOG_ERROR("Buffer too small: need %u, got %u", needed_size, *template_size);
        *template_size = needed_size;
        return ERR_INVALID_PARAM;
    }

    voiceprint_template_t* tpl = (voiceprint_template_t*)template;
    memset(tpl, 0, sizeof(*tpl));
    tpl->user_id = user_id;
    tpl->template_size = needed_size;
    tpl->created_at = user->created_at;

    /* 模拟生成随机模板数据 */
    for (int i = 0; i < GMM_N_COMPONENTS * GMM_DIM; i++) {
        tpl->means[i] = (float)(rand() % 100) / 100.0f;
        if (i < GMM_N_COMPONENTS) {
            tpl->weights[i] = 1.0f / GMM_N_COMPONENTS;
        }
    }
    for (int i = 0; i < GMM_N_COMPONENTS * GMM_DIM * GMM_DIM; i++) {
        tpl->covariance[i] = (i % (GMM_DIM + 1)) == 0 ? 1.0f : 0.0f;
    }

    *template_size = needed_size;

    LOG_INFO("Voiceprint loaded: user_id=%u, size=%u", user_id, *template_size);
    return ERR_OK;
}

/**
 * 删除用户
 * @param user_id 用户ID
 */
int storage_delete_user(uint32_t user_id) {
    if (!g_initialized) {
        LOG_ERROR("Storage not initialized");
        return ERR_DATABASE;
    }

    /* 查找用户 */
    int found_index = -1;
    for (uint32_t i = 0; i < g_user_count; i++) {
        if (g_users[i].id == user_id) {
            found_index = i;
            break;
        }
    }

    if (found_index < 0) {
        LOG_ERROR("User not found: id=%u", user_id);
        return ERR_USER_NOT_FOUND;
    }

    /* 打印删除的用户信息 */
    LOG_INFO("Deleting user: id=%u, name=%s", g_users[found_index].id, g_users[found_index].name);

    /* 移动后续用户数据 (覆盖删除的位置) */
    for (uint32_t i = found_index; i < g_user_count - 1; i++) {
        memcpy(&g_users[i], &g_users[i + 1], sizeof(user_t));
    }

    /* 清除最后一个位置 */
    memset(&g_users[g_user_count - 1], 0, sizeof(user_t));
    g_user_count--;

    /* 保存到文件 */
    int ret = save_db();
    if (ret != ERR_OK) {
        LOG_ERROR("Failed to save database after deleting user");
        return ret;
    }

    LOG_INFO("User deleted: id=%u, remaining users=%u", user_id, g_user_count);
    return ERR_OK;
}

/**
 * 列出所有用户
 * @param users 输出用户数组 (可为NULL只获取数量)
 * @param max_count 最大输出数量
 * @param out_count 实际输出数量
 */
int storage_list_users(user_t* users, uint32_t max_count, uint32_t* out_count) {
    if (!g_initialized) {
        LOG_ERROR("Storage not initialized");
        return ERR_DATABASE;
    }

    if (!out_count) {
        LOG_ERROR("out_count pointer is NULL");
        return ERR_INVALID_PARAM;
    }

    *out_count = g_user_count;

    if (users && max_count > 0) {
        uint32_t copy_count = (g_user_count < max_count) ? g_user_count : max_count;
        memcpy(users, g_users, copy_count * sizeof(user_t));

        LOG_DEBUG("Listed %u users (requested max %u)", copy_count, max_count);
    } else {
        LOG_DEBUG("User count: %u", g_user_count);
    }

    return ERR_OK;
}

/* ============================================================
 * 测试/调试接口
 * ============================================================ */

/**
 * 打印所有用户信息 (调试用)
 */
void storage_print_all_users(void) {
    if (!g_initialized) {
        LOG_ERROR("Storage not initialized");
        return;
    }

    LOG_SEPARATOR();
    LOG_PLAIN("=== All Users (total=%u) ===", g_user_count);

    for (uint32_t i = 0; i < g_user_count; i++) {
        user_t* u = &g_users[i];
        printf("  [%2u] id=%u, name=%-32s, enrolled=%s, created=%s",
               i, u->id, u->name, u->is_enrolled ? "YES" : "NO",
               ctime(&u->created_at));
    }

    LOG_SEPARATOR();
}

/**
 * 获取数据库路径
 */
const char* storage_get_db_path(void) {
    return g_db_path;
}

/**
 * 设置数据库路径 (必须在init之前调用)
 */
void storage_set_db_path(const char* path) {
    if (path && path[0] != '\0') {
        strncpy(g_db_path, path, sizeof(g_db_path) - 1);
        g_db_path[sizeof(g_db_path) - 1] = '\0';
        LOG_INFO("Database path set to: %s", g_db_path);
    }
}

/**
 * 重置数据库 (清空所有数据)
 */
int storage_reset(void) {
    if (!g_initialized) {
        LOG_ERROR("Storage not initialized");
        return ERR_DATABASE;
    }

    LOG_WARN("Resetting database...");

    memset(g_users, 0, sizeof(g_users));
    g_user_count = 0;

    int ret = save_db();
    if (ret != ERR_OK) {
        LOG_ERROR("Failed to save database after reset");
        return ret;
    }

    LOG_INFO("Database reset complete");
    return ERR_OK;
}
