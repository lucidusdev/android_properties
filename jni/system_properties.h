#pragma once

#include "property_info.h"

#define PROP_NAME_MAX 32
#define PROP_VALUE_MAX 92

#define PROP_COUNT_MAX 0xFFFF // lower 2 bytes in serial

#define AREA_SIZE (128 * 1024)
#define AREA_DATA_SIZE (AREA_SIZE - (int)sizeof(prop_area))

#define ANDROID_N 24
#define ANDROID_O 26

#define LOG_TYPE_CONSOLE 1
#define LOG_TYPE_LOGCAT 2
#define LOG_BUFFER 1024

#define ALIGN(x, alignment) ((x) + (sizeof(alignment) - 1) & ~(sizeof(alignment) - 1))

#define LOG_TAG "properties"
#define LOGD(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

#define PROPERTIES_FILE "/dev/__properties__"

// int g_log_type = LOG_TYPE_CONSOLE + LOG_TYPE_LOGCAT; // 默认输出到logcat和console
int g_log_type = LOG_TYPE_CONSOLE; // set default output to console

bool g_need_security_context = false;
char *g_current_security_context = NULL;
bool g_use_file = false;
bool g_verbose_mode = false;


void print_log(const char *format, ...)
{
    char buffer[LOG_BUFFER];
    va_list args;
    va_start(args, format);
    vsprintf(buffer, format, args);
    if ((g_log_type & LOG_TYPE_LOGCAT) != 0)
    {
        LOGD("%s", buffer);
    }
    if ((g_log_type & LOG_TYPE_CONSOLE) != 0)
    {
        printf("%s", buffer);
    }
    va_end(args);
}


/** 属性前缀 */
typedef struct prefix_node
{
    char *name;
    struct context_node *context;
    struct prefix_node *next;
} prefix_node;

/** 属性对应的security context */
typedef struct context_node
{
    char *name;
    void *mem;
    struct context_node *next;
} context_node;

typedef struct prop_bt
{
    uint8_t namelen;
    uint8_t reserved[3];
    uint32_t prop;
    uint32_t left;
    uint32_t right;
    uint32_t children;
    char name[0];
} prop_bt;

/** 保存属性 key value */
typedef struct prop_info
{
    uint32_t serial;
    // uint8_t valuelen
    // uint8_t kLongFlag
    // uint16_t count
    char value[PROP_VALUE_MAX];
    char name[0];

    bool set_count(uint32_t count)
    {
        if (count == PROP_COUNT_MAX || get_count() == count)
            return false;
        serial = (serial & 0xFFFF0000) | (count & PROP_COUNT_MAX);
        return true;
    }

    bool set_value(const char *prop_value)
    {
        if (prop_value == NULL || strncmp(prop_value, value, PROP_VALUE_MAX) == 0)
            return false;

        uint8_t valuelen = strlen(prop_value);
        memset(value, 0, strlen(value));
        memcpy(value, prop_value, valuelen);
        value[valuelen] = '\0';
        serial = serial & 0xFFFFFF | valuelen << 24;
        return true;
    }

    uint32_t get_count() { return serial & PROP_COUNT_MAX; }

    bool is_long() { return serial & (1 << 16); }

    bool update_value_count(const char *prop_value, uint32_t prop_count)
    {
        return set_value(prop_value) | set_count(prop_count);
    }
} prop_info;

typedef struct prop_area
{
    uint32_t bytes_used;
    uint32_t serial;
    uint32_t magic;
    uint32_t version;
    uint32_t reserved[28];
    char data[0];
} prop_area;


struct prop_content
{
    std::string name;
    std::string value;
    std::string security;
    uint32_t serial;
    bool operator<(const prop_content &x) const
    {
        return name < x.name;
    }
    void output()
    {
        print_log("[%s]: [%s]", name.c_str(), value.c_str());
        if (get_count() != 0)
            print_log(" count: %d",  get_count());
        if (g_verbose_mode)
            print_log(" serial: 0x%08X",  serial);
        if (!security.empty())
            print_log(" s_context: %s", security.c_str());
        print_log("\n");
    }
    uint32_t get_count() {return serial & PROP_COUNT_MAX;}
};



property_info g_info;

prefix_node *g_prefixs = NULL;
context_node *g_contexts = NULL;
