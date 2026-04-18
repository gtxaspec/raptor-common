/*
 * rss_ctrl_cmds.c — Common control socket command handlers
 *
 * Handles config-get, config-get-section, and config-save commands
 * shared by all daemons.
 */

#include "rss_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
        return -1;

    cJSON *cmd_obj = cJSON_GetObjectItemCaseSensitive(root, "cmd");
    if (!cJSON_IsString(cmd_obj) || !cmd_obj->valuestring) {
        cJSON_Delete(root);
        return -1;
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

    cJSON_Delete(root);
    return -1; /* not handled */
}
