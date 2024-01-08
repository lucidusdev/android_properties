#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <ctype.h>

#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <getopt.h>
#include <errno.h>

#include <android/log.h>
#include <sys/system_properties.h>

#include <map>
#include <algorithm>

#include "system_properties.h"


std::vector<prop_content> prop_all;


void add_prefix_node(prefix_node *node)
{
    if (g_prefixs == NULL)
    {
        g_prefixs = node;
        return;
    }
    int len = strlen(node->name);
    prefix_node **pp = NULL;
    for (pp = &g_prefixs; *pp != NULL; pp = &((*pp)->next))
    {
        if (strlen((*pp)->name) < len || !strcmp((*pp)->name, "*"))
        {
            node->next = *pp;
            *pp = node;
            return;
        }
    }
    *pp = node;
}

prefix_node *get_prefix_node(const char *prop_name)
{
    if (g_prefixs == NULL)
    {
        return NULL;
    }
    for (prefix_node *node = g_prefixs; node != NULL; node = node->next)
    {
        if (!strncmp(node->name, prop_name, strlen(node->name)) || !strcmp(node->name, "*"))
        {
            return node;
        }
    }
    return NULL;
}

void add_context_node(context_node *node)
{
    if (g_contexts == NULL)
    {
        g_contexts = node;
        return;
    }
    node->next = g_contexts;
    g_contexts = node;
}

context_node *get_context_node(const char *context_name)
{
    if (g_contexts == NULL)
    {
        return NULL;
    }
    for (context_node *node = g_contexts; node != NULL; node = node->next)
    {
        if (!strcmp(node->name, context_name))
        {
            return node;
        }
    }
    return NULL;
}

bool initialize_contexts(const char *context_file)
{
    FILE *file = fopen(context_file, "r");
    if (!file)
    {
        return false;
    }
    char *buffer = NULL;
    char *p = NULL;
    char *prop_prefix = NULL;
    char *prop_context = NULL;
    size_t len = 0;
    while (getline(&buffer, &len, file) > 0)
    {
        p = buffer;
        while (isspace(*p))
            p++;
        if (*p == '#' || *p == '\0')
        {
            continue;
        }
        prop_prefix = p;
        while (!isspace(*p) && *p != '\0')
        {
            p++;
        }
        prefix_node *p_prefix = (prefix_node *)calloc(1, sizeof(prefix_node));
        p_prefix->name = (char *)calloc(1, p - prop_prefix + 1);
        strncpy(p_prefix->name, prop_prefix, p - prop_prefix);

        while (isspace(*p))
            p++;
        if (*p == '\0')
        {
            free(p_prefix->name);
            free(p_prefix);
            continue;
        }
        prop_context = p;
        while (!isspace(*p) && *p != '\0')
        {
            p++;
        }
        *p = '\0';
        context_node *p_context = get_context_node(prop_context);
        if (p_context == NULL)
        {
            p_context = (context_node *)calloc(1, sizeof(context_node));
            p_context->name = (char *)calloc(1, strlen(prop_context) + 1);
            strcpy(p_context->name, prop_context);
            add_context_node(p_context);
        }
        p_prefix->context = p_context;
        add_prefix_node(p_prefix);
    }

    free(buffer);
    fclose(file);
    return true;
}

void cleanup_resource()
{
    prefix_node *p = g_prefixs, *pp;
    while (p != NULL)
    {
        pp = p;
        p = p->next;
        free(pp->name);
        free(pp);
    }
    context_node *q = g_contexts, *qq;
    while (q != NULL)
    {
        qq = q;
        q = q->next;
        free(qq->name);
        free(qq);
    }
}

prop_area *map_prop_area(const char *file_name, bool need_write)
{
    int open_flag;
    int map_prot_flag;
    if (need_write)
    {
        open_flag = O_RDWR;
        map_prot_flag = PROT_READ | PROT_WRITE;
    }
    else
    {
        open_flag = O_RDONLY;
        map_prot_flag = PROT_READ;
    }
    int fd = open(file_name, open_flag);
    if (fd == -1)
    {
        // if (errno == EACCES)
        if (errno == EACCES && geteuid() == 0) // only print when run as root.
        {
            fprintf(stderr, "open file[%s] error[%d]:%s\n", file_name, errno, strerror(errno));
        }
        return NULL;
    }
    struct stat fd_stat;
    if (fstat(fd, &fd_stat) < 0)
    {
        perror("cannot get stat:");
        close(fd);
        return NULL;
    }
    if (fd_stat.st_size != AREA_SIZE)
    {
        print_log("file [%s] size is not equal %x\n", file_name, AREA_SIZE);
        close(fd);
        return NULL;
    }
    void *addr = mmap(NULL, fd_stat.st_size, map_prot_flag, MAP_SHARED, fd, 0);
    if (addr == MAP_FAILED)
    {
        perror("map failed!");
        close(fd);
        return NULL;
    }
    close(fd);
    return (prop_area *)addr;
}

prop_bt *get_prop_bt(prop_area *p_area, uint32_t off)
{
    if (off > AREA_DATA_SIZE)
    {
        fprintf(stderr, "exceed the limit\n");
        return NULL;
    }
    return (prop_bt *)(p_area->data + off);
}

prop_bt *new_prop_bt(prop_area *p_area, const char *name, uint8_t namelen, uint32_t *off)
{
    uint32_t need_size = ALIGN(sizeof(prop_bt) + namelen + 1, sizeof(uint32_t));
    if (p_area->bytes_used + need_size > AREA_DATA_SIZE)
    {
        fprintf(stderr, "no enough space, total:[%u] used:[%u], need:[%u]\n", AREA_DATA_SIZE,
                p_area->bytes_used, need_size);
        return NULL;
    }
    *off = p_area->bytes_used;
    p_area->bytes_used += need_size;
    prop_bt *bt = (prop_bt *)(p_area->data + *off);
    memset(bt, 0, sizeof(prop_bt));
    bt->namelen = namelen;
    memcpy(bt->name, name, namelen);
    bt->name[namelen] = '\0';
    return bt;
}

prop_info *get_prop_info(prop_area *p_area, uint32_t off)
{
    if (off > AREA_DATA_SIZE)
    {
        fprintf(stderr, "exceed the limit\n");
        return NULL;
    }
    prop_info *result = (prop_info *)(p_area->data + off);
    return result;
}

prop_info *new_prop_info(prop_area *p_area, const char *prop_name, uint8_t namelen, uint32_t *off)
{
    uint32_t need_size = ALIGN(sizeof(prop_info) + namelen + 1, sizeof(uint32_t));
    if (p_area->bytes_used + need_size > AREA_DATA_SIZE)
    {
        fprintf(stderr, "no enough space, total:[%u] used:[%u], need:[%u]\n", AREA_DATA_SIZE,
                p_area->bytes_used, need_size);
        return NULL;
    }
    *off = p_area->bytes_used;
    p_area->bytes_used += need_size;
    prop_info *info = (prop_info *)(p_area->data + *off);
    memset(info, 0, sizeof(prop_info));
    memcpy(info->name, prop_name, namelen);
    info->name[namelen] = '\0';
    return info;
}

int get_sdk_version();

char *get_security_context(char *prop_name)
{
    if (g_current_security_context != NULL)
    {
        return g_current_security_context;
    }

    /**
     * below Android N, ingnore "ro." prefix in the property_contexts file
     */
    if (get_sdk_version() < ANDROID_N)
    {
        char *name = strstr(prop_name, "ro.");
        if (name != NULL && strlen(name) == strlen(prop_name))
        {
            prop_name = prop_name + strlen("ro.");
        }
    }
    prefix_node *p_prefix = get_prefix_node(prop_name);
    if (p_prefix == NULL || p_prefix->context == NULL)
    {
        return NULL;
    }
    else
    {
        return p_prefix->context->name;
    }
}

void recursive(prop_area *p_area, uint32_t off)
{
    prop_bt *p_bt = get_prop_bt(p_area, off);
    if (p_bt == NULL)
    {
        return;
    }
    if (p_bt->prop != 0)
    {
        prop_info *p_info = get_prop_info(p_area, p_bt->prop);
        if (p_info != NULL)
        {
            prop_content content;
            content.name = std::string(p_info->name);
            content.value = std::string(p_info->value);
            content.serial = p_info->serial;
            if (get_security_context(p_info->name) != NULL)
                content.security = std::string(get_security_context(p_info->name));
            prop_all.push_back(content);
            // print_log("%s", content.to_string().c_str());
        }
    }
    if (p_bt->left != 0)
    {
        recursive(p_area, p_bt->left);
    }
    if (p_bt->right != 0)
    {
        recursive(p_area, p_bt->right);
    }
    if (p_bt->children != 0)
    {
        recursive(p_area, p_bt->children);
    }
}

bool dump_properties_from_file(const char *file_name)
{
    prop_area *p_area = map_prop_area(file_name, false);
    if (p_area == NULL)
    {
        return false;
    }
    recursive(p_area, 0);

    return true;
}

int get_sdk_version()
{
    static int sdk_version = 0;
    if (sdk_version != 0)
    {
        return sdk_version;
    }
    char sdk_value[PROP_VALUE_MAX] = {0};
    __system_property_get("ro.build.version.sdk", sdk_value);
    if (strlen(sdk_value) > 0)
    {
        sdk_version = atoi(sdk_value);
    }
    return sdk_version;
}

/**
 * 打印所有属性
 *  Android N之间所有属性是在/dev/__properties__文件中
 *  Android N上，每个security context对应一个文件，security context和属性前缀对应关系保存在/property_contexts文件中
 */
void dump_all()
{
    prop_all.clear();
    if (get_sdk_version() < ANDROID_N)
    {
        dump_properties_from_file(PROPERTIES_FILE);
    }
    else
    {
        if (g_use_file)
        {
            for (context_node *p_context = g_contexts; p_context != NULL; p_context = p_context->next)
            {
                char context_file[128] = PROPERTIES_FILE;
                strcat(context_file, "/");
                strcat(context_file, p_context->name);
                if (g_need_security_context)
                {
                    g_current_security_context = (char *)p_context->name;
                }
                dump_properties_from_file(context_file);
            }
        }
        else
        {
            for (uint32_t i = 0; i < g_info.get_context_size(); i++)
            {
                char context_file[128] = PROPERTIES_FILE;
                strcat(context_file, "/");
                strcat(context_file, g_info.get_context(i).c_str());
                if (g_need_security_context)
                {
                    g_current_security_context = (char *)g_info.get_context(i).c_str();
                }
                dump_properties_from_file(context_file);
            }
        }
    }

    return;
}

void filter_all(const char *prop_name)
{
    if (prop_name == NULL || strlen(prop_name) < 2)
    {
        std::sort(prop_all.begin(), prop_all.end());
        return;
    }
    std::string_view sv(prop_name);
    std::vector<prop_content> vec;
    for (auto &p : prop_all)
    {
        if (sv == "**" ||
            (sv.starts_with("*") && p.name.ends_with(sv.substr(1))) ||
            (sv.ends_with("*") && p.name.starts_with(sv.substr(0, sv.size() - 1))) ||
            (sv.starts_with("*") && sv.ends_with("*") && p.name.find(sv.substr(1, sv.size() - 2)) != std::string::npos))
        {
            // print_log("%s\n", p.to_string().c_str());
            vec.push_back(p);
        }
    }
    std::sort(vec.begin(), vec.end());
    prop_all = vec;
    return;
}

int cmp_prop_name(const char *one, uint8_t one_len, const char *two, uint8_t two_len)
{
    if (one_len < two_len)
        return -1;
    else if (one_len > two_len)
        return 1;
    else
        return strncmp(one, two, one_len);
}

 prop_info *find_prop_info(prop_area *area, const char *prop_name, bool need_add, bool need_confirm_add = true)
{
    if (area == NULL || strlen(prop_name) == 0)
    {
        return NULL;
    }
    prop_bt *prev_bt = get_prop_bt(area, 0);
    if (prev_bt->children == 0)
    {
        return NULL;
    }
    prop_bt *p_bt = get_prop_bt(area, prev_bt->children);
    const char *remain_name = prop_name;
    while (true)
    {
        const char *seq = strchr(remain_name, '.');
        bool want_subtree = (seq != NULL);
        uint8_t substr_size = want_subtree ? (seq - remain_name) : strlen(remain_name);

        if (p_bt == NULL && need_add)
        {
            if (need_confirm_add) {
                char ans;
                printf("prop [%s] doesn't exist, create it? y*/n\n", prop_name);
                ans = getchar();
                if (ans == 'n' || ans == 'N')
                    return NULL;
                else
                    p_bt = new_prop_bt(area, remain_name, substr_size, &prev_bt->children);
            } else {
                p_bt = new_prop_bt(area, remain_name, substr_size, &prev_bt->children);
            }            
        }

        prop_bt *current = NULL;
        while (p_bt != NULL)
        {
            int ret = cmp_prop_name(remain_name, substr_size, p_bt->name, p_bt->namelen);
            if (ret == 0)
            {
                current = p_bt;
                break;
            }
            else if (ret < 0)
            {
                if (p_bt->left == 0)
                {
                    if (need_add)
                    {
                        p_bt = new_prop_bt(area, remain_name, substr_size, &p_bt->left);
                    }
                    else
                    {
                        p_bt = NULL;
                    }
                }
                else
                {
                    p_bt = get_prop_bt(area, p_bt->left);
                }
            }
            else
            {
                if (p_bt->right == 0)
                {
                    if (need_add)
                    {
                        p_bt = new_prop_bt(area, remain_name, substr_size, &p_bt->right);
                    }
                    else
                    {
                        p_bt = NULL;
                    }
                }
                else
                {
                    p_bt = get_prop_bt(area, p_bt->right);
                }
            }
        }
        if (current != NULL)
        {
            if (!want_subtree)
            {
                prop_info *info = NULL;
                if (current->prop == 0)
                {
                    if (need_add)
                    {
                        info = new_prop_info(area, prop_name, strlen(prop_name), &current->prop);
                    }
                    else
                    {
                        info = NULL;
                    }
                }
                else
                {
                    info = get_prop_info(area, current->prop);
                }
                return info;
            }
            else
            {
                remain_name = seq + 1;
                if (current->children == 0)
                {
                    p_bt = NULL;
                }
                else
                {
                    p_bt = get_prop_bt(area, current->children);
                }
                prev_bt = current;
            }
        }
        else
        {
            return NULL;
        }
    }
    return NULL;
}

void get_or_set_property_value_count(const char *prop_name, const char *prop_value, uint32_t prop_count)
{
    prop_area *p_area = NULL;
    bool need_write = prop_value != NULL || prop_count != PROP_COUNT_MAX;
    if (get_sdk_version() < ANDROID_N)
    {
        p_area = map_prop_area(PROPERTIES_FILE, need_write);
    }
    else
    {
        char context_file[128] = PROPERTIES_FILE;
        strcat(context_file, "/");
        if (g_use_file)
        {
            prefix_node *p_prefix = get_prefix_node(prop_name);
            if (p_prefix == NULL || p_prefix->context == NULL)
            {
                fprintf(stderr, "can't find security context file!\n");
                return;
            }
            strcat(context_file, p_prefix->context->name);
            if (g_need_security_context)
            {
                g_current_security_context = p_prefix->context->name;
            }
        }
        else
        {
            std::string context_name = g_info.get_context(prop_name);
            strcat(context_file, g_info.get_context(prop_name).c_str());
            if (g_need_security_context)
            {
                g_current_security_context = (char *)context_name.c_str();
            }
        }

        p_area = map_prop_area(context_file, need_write);
    }
    prop_info *p_info = find_prop_info(p_area, prop_name, need_write);
    if (p_info != NULL)
    {
        if (need_write)
        {
            if (p_info->update_value_count(prop_value, prop_count))
            {
                if (g_verbose_mode)
                    print_log("set %s == %s, valuelen %d\n", prop_name, p_info->value, p_info->serial >> 24);
                else
                    print_log("set ");
            }
        }
        print_log("[%s]: [%s] count %d", p_info->name, p_info->value, p_info->get_count());
        if (g_verbose_mode)
            print_log(" serial 0x%08X",  p_info->serial);
        if (g_need_security_context)
        {
            print_log(" context: [%s]", get_security_context(p_info->name));
        }
        print_log("\n");
    }
}

static void usage()
{
    fprintf(stderr,
            "usage: system_properties [-h] [-c] [-l log_level] [-s] [-f] [-v] prop_name prop_value new_count*\n"
            "  -h:                  display this help message\n"
            "  -c:                  set count\n"
            "  -l log_level:        console = 1(default) logcat = 2  console + logcat = 3\n"
            "  -s                   print security context(selabel)\n"
            "  -f                   read property_contexts files to get security context\n"
            "  -v                   verbose mode\n\n"
            "use leading/trailing '*' for wildcard match, or \"all\" to match all props\n");
}

int main(int argc, char *argv[])
{
    bool multi_prop = false;
    char *prop_name = NULL;
    char *prop_value = NULL;
    uint32_t prop_count = PROP_COUNT_MAX;

    for (;;)
    {
        int option_index = 0;
        int ic = getopt(argc, argv, "hvl:c:sf");
        if (ic < 0)
        {
            if (optind < argc)
            {
                prop_name = argv[optind];
                if (strncmp(prop_name, "all", PROP_NAME_MAX) == 0)
                    prop_name =  strdup("**");;
            }
            if (optind + 1 < argc)
            {
                prop_value = argv[optind + 1];
            }
            if (optind + 2 < argc)
            {
                prop_count = atoi(argv[optind + 2]) & PROP_COUNT_MAX;
            }
            break;
        }
        switch (ic)
        {
        case 'h':
            usage();
            return -1;
        case 'l':
            g_log_type = atoi(optarg);
            break;
        case 'c':
            prop_count = atoi(optarg) & PROP_COUNT_MAX;
            break;
        case 's':
            g_need_security_context = true;
            break;
        case 'f':
            g_use_file = true;
            break;
        case 'v':
            g_verbose_mode = true;
            break;
        default:
            usage();
            return -1;
        }
    }

    if (prop_name == NULL)
    {
        multi_prop = true;
        prop_count = PROP_COUNT_MAX; // disable empty name for setting count for all. use wildcard.
    }
    else
    {
        std::string_view sv(prop_name);
        if (sv.starts_with(".") || sv.ends_with(".") || sv.find_first_of("*.") == std::string::npos)
        {
            fprintf(stderr, "Invalid property name!\n");
            return -1;
        }
        if (sv.starts_with("*") || sv.ends_with("*"))
            multi_prop = true;
    }

    if (prop_value != NULL || prop_count != PROP_COUNT_MAX)
    {
        std::string_view sv(prop_name);
        if (prop_value != NULL && strlen(prop_value) >= PROP_VALUE_MAX)
        // https://github.com/liwugang/android_properties/blob/master/jni/system_properties.cpp#L605
        // as in original code, removed to restrict all prop values less than PROP_VALUE_MAX.
        //&& (strlen(prop_name) < strlen("ro.") || strncmp(prop_name, "ro.", strlen("ro.")) != 0))
        {
            fprintf(stderr, "prop_value[%s] is too long, need less %d\n", prop_value, PROP_VALUE_MAX);
            return -1;
        }

        if (geteuid() != 0)
        {
            fprintf(stderr, "set property value/count need root first!\n");
            return -1;
        }
    }

    if (g_use_file || !g_info.is_valid())
    {
        g_use_file = true;
        // https://cs.android.com/android/platform/superproject/main/+/main:system/core/init/property_service.cpp
        if (get_sdk_version() >= ANDROID_O)
        {
            if (access("/system/etc/selinux/plat_property_contexts", R_OK) != -1)
            {
                initialize_contexts("/system/etc/selinux/plat_property_contexts");
                initialize_contexts("/vendor/etc/selinux/nonplat_property_contexts");
                initialize_contexts("/vendor/etc/selinux/vendor_property_contexts");   // name changed in android P
                initialize_contexts("/product/etc/selinux/product_property_contexts"); // Add in Android Q
                initialize_contexts("/odm/etc/selinux/odm_property_contexts");
                initialize_contexts("/system_ext/etc/selinux/system_ext_property_contexts"); // Add in Android R
            }
            else
            {
                initialize_contexts("/plat_property_contexts");
                initialize_contexts("/nonplat_property_contexts");
            }
        }
        else
        {
            initialize_contexts("/property_contexts");
        }
    }


    if (multi_prop)
    {
        dump_all();
        filter_all(prop_name);
        for (auto &p : prop_all)
        {
            if (prop_count != PROP_COUNT_MAX)
                get_or_set_property_value_count(p.name.c_str(), NULL, prop_count);
            else
                //print_log("%s\n", p.to_string().c_str());
                p.output();
        }
    }
    else
    {
        get_or_set_property_value_count(prop_name, prop_value, prop_count);
    }

    cleanup_resource();
    return 0;
}
