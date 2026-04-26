/*
 * rss_ctrl_cmds.c — Common control socket command handlers
 *
 * Handles config-get, config-get-section, config-save, and
 * scheduling commands shared by all daemons.
 */

#include "rss_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sched.h>
#include <unistd.h>
#include "cJSON.h"

/* Callback for config-get-section: add key/value to cJSON object */
static void section_dump_cb(const char *key, const char *value, void *userdata)
{
    cJSON *keys_obj = userdata;
    cJSON_AddStringToObject(keys_obj, key, value);
}

int rss_ctrl_handle_common(const char *cmd_json, char *resp_buf, int resp_buf_size,
                           rss_config_t *cfg, const char *config_path)
{
    cJSON *root = cJSON_Parse(cmd_json);
    if (!root)
        return rss_ctrl_resp_error(resp_buf, resp_buf_size, "malformed JSON");

    cJSON *cmd_obj = cJSON_GetObjectItemCaseSensitive(root, "cmd");
    if (!cJSON_IsString(cmd_obj) || !cmd_obj->valuestring) {
        cJSON_Delete(root);
        return -1; /* no cmd field — let daemon-specific handler try */
    }
    const char *cmd = cmd_obj->valuestring;

    if (strcmp(cmd, "config-get-section") == 0) {
        int len = 0;
        cJSON *sec_obj = cJSON_GetObjectItemCaseSensitive(root, "section");
        if (cJSON_IsString(sec_obj) && sec_obj->valuestring) {
            const char *section = sec_obj->valuestring;
            cJSON *resp = cJSON_CreateObject();
            if (!resp) {
                len = rss_ctrl_resp_error(resp_buf, resp_buf_size, "alloc fail");
                cJSON_Delete(root);
                return len;
            }
            cJSON_AddStringToObject(resp, "status", "ok");
            cJSON_AddStringToObject(resp, "section", section);
            cJSON *keys_obj = cJSON_AddObjectToObject(resp, "keys");
            if (keys_obj)
                rss_config_foreach(cfg, section, section_dump_cb, keys_obj);
            char *s = cJSON_PrintUnformatted(resp);
            if (s) {
                size_t sl = rss_strlcpy(resp_buf, s, (size_t)resp_buf_size);
                if (sl >= (size_t)resp_buf_size)
                    len = rss_ctrl_resp_error(resp_buf, resp_buf_size, "response truncated");
                else
                    len = (int)sl;
                free(s);
            } else {
                len = rss_ctrl_resp_error(resp_buf, resp_buf_size, "alloc fail");
            }
            cJSON_Delete(resp);
        } else {
            len = rss_ctrl_resp_error(resp_buf, resp_buf_size, "need section");
        }
        cJSON_Delete(root);
        return len;
    }

    if (strcmp(cmd, "config-get") == 0) {
        cJSON *sec_obj = cJSON_GetObjectItemCaseSensitive(root, "section");
        cJSON *key_obj = cJSON_GetObjectItemCaseSensitive(root, "key");
        if (cJSON_IsString(sec_obj) && cJSON_IsString(key_obj)) {
            const char *v =
                rss_config_get_str(cfg, sec_obj->valuestring, key_obj->valuestring, NULL);
            cJSON_Delete(root);
            if (v)
                return rss_ctrl_resp(resp_buf, resp_buf_size, "%s", v);
            resp_buf[0] = '\0';
        } else {
            cJSON_Delete(root);
            resp_buf[0] = '\0';
        }
        return 0;
    }

    if (strcmp(cmd, "config-save") == 0) {
        cJSON_Delete(root);
        int ret = rss_config_save(cfg, config_path);
        if (ret == 0)
            RSS_INFO("running config saved to %s", config_path);
        if (ret == 0)
            return rss_ctrl_resp_ok(resp_buf, resp_buf_size);
        return rss_ctrl_resp_error(resp_buf, resp_buf_size, "config save failed");
    }

    if (strcmp(cmd, "set-affinity") == 0) {
        cJSON *cpu_obj = cJSON_GetObjectItemCaseSensitive(root, "cpu");
        if (!cJSON_IsNumber(cpu_obj)) {
            cJSON_Delete(root);
            return rss_ctrl_resp_error(resp_buf, resp_buf_size, "need cpu");
        }
        int cpu = (int)cJSON_GetNumberValue(cpu_obj);
        cJSON_Delete(root);

        long ncpus = sysconf(_SC_NPROCESSORS_ONLN);
        if (ncpus <= 1)
            return rss_ctrl_resp_error(resp_buf, resp_buf_size, "single-core system");
        if (cpu < 0 || cpu >= (int)ncpus)
            return rss_ctrl_resp_error(resp_buf, resp_buf_size, "cpu out of range");

        cpu_set_t set;
        CPU_ZERO(&set);
        CPU_SET(cpu, &set);
        if (sched_setaffinity(0, sizeof(set), &set) != 0)
            return rss_ctrl_resp_error(resp_buf, resp_buf_size, strerror(errno));

        RSS_INFO("cpu affinity changed to CPU%d", cpu);
        return rss_ctrl_resp_ok(resp_buf, resp_buf_size);
    }

    if (strcmp(cmd, "get-affinity") == 0) {
        cJSON_Delete(root);
        int len;
        long ncpus = sysconf(_SC_NPROCESSORS_ONLN);

        cpu_set_t set;
        CPU_ZERO(&set);
        if (sched_getaffinity(0, sizeof(set), &set) != 0) {
            return rss_ctrl_resp_error(resp_buf, resp_buf_size, strerror(errno));
        }

        int policy = sched_getscheduler(0);
        struct sched_param sp;
        sched_getparam(0, &sp);

        cJSON *resp = cJSON_CreateObject();
        if (!resp)
            return rss_ctrl_resp_error(resp_buf, resp_buf_size, "alloc fail");

        cJSON_AddStringToObject(resp, "status", "ok");
        cJSON_AddNumberToObject(resp, "ncpus", (double)ncpus);

        cJSON *cpus = cJSON_AddArrayToObject(resp, "cpus");
        for (int i = 0; i < (int)ncpus; i++) {
            if (CPU_ISSET(i, &set))
                cJSON_AddItemToArray(cpus, cJSON_CreateNumber(i));
        }

        const char *pol_str = "other";
        if (policy == SCHED_FIFO)
            pol_str = "fifo";
        else if (policy == SCHED_RR)
            pol_str = "rr";
        cJSON_AddStringToObject(resp, "policy", pol_str);
        cJSON_AddNumberToObject(resp, "priority", (double)sp.sched_priority);

        char *s = cJSON_PrintUnformatted(resp);
        if (s) {
            len = (int)rss_strlcpy(resp_buf, s, (size_t)resp_buf_size);
            free(s);
        } else {
            len = rss_ctrl_resp_error(resp_buf, resp_buf_size, "alloc fail");
        }
        cJSON_Delete(resp);
        return len;
    }

    cJSON_Delete(root);
    return -1; /* not handled */
}
