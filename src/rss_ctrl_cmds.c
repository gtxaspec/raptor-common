/*
 * rss_ctrl_cmds.c — Common control socket command handlers
 *
 * Handles config-get, config-get-section, and config-save commands
 * shared by all daemons.
 */

#include "rss_common.h"

#include <stdio.h>
#include <string.h>

/* Callback for config-get-section: append "key":"value" to JSON buffer */
typedef struct {
    char *buf;
    int off;
    int size;
    int first;
} section_dump_ctx_t;

static void section_dump_cb(const char *key, const char *value, void *userdata)
{
    section_dump_ctx_t *c = userdata;
    if (c->off >= c->size - 4)
        return;
    int n = snprintf(c->buf + c->off, c->size - c->off, "%s\"%s\":\"%s\"",
                     c->first ? "" : ",", key, value);
    if (n > 0 && c->off + n < c->size)
        c->off += n;
    c->first = 0;
}

int rss_ctrl_handle_common(const char *cmd_json, char *resp_buf, int resp_buf_size,
                           rss_config_t *cfg, const char *config_path)
{
    /* config-get-section must be checked BEFORE config-get (substring match) */
    if (strstr(cmd_json, "\"config-get-section\"")) {
        char section[64];
        if (rss_json_get_str(cmd_json, "section", section, sizeof(section)) == 0) {
            int off = snprintf(resp_buf, resp_buf_size,
                               "{\"status\":\"ok\",\"section\":\"%s\",\"keys\":{", section);
            section_dump_ctx_t ctx = {resp_buf, off, resp_buf_size, 1};
            rss_config_foreach(cfg, section, section_dump_cb, &ctx);
            off = ctx.off;
            if (off < resp_buf_size - 2)
                snprintf(resp_buf + off, resp_buf_size - off, "}}");
        } else {
            rss_ctrl_resp_error(resp_buf, resp_buf_size, "need section");
        }
        return (int)strlen(resp_buf);
    }

    if (strstr(cmd_json, "\"config-get\"")) {
        char section[64], key[64];
        if (rss_json_get_str(cmd_json, "section", section, sizeof(section)) == 0 &&
            rss_json_get_str(cmd_json, "key", key, sizeof(key)) == 0) {
            const char *v = rss_config_get_str(cfg, section, key, NULL);
            if (v)
                return rss_ctrl_resp(resp_buf, resp_buf_size, "%s", v);
            resp_buf[0] = '\0';
        } else {
            resp_buf[0] = '\0';
        }
        return 0;
    }

    if (strstr(cmd_json, "\"config-save\"")) {
        int ret = rss_config_save(cfg, config_path);
        if (ret == 0)
            RSS_INFO("running config saved to %s", config_path);
        return rss_ctrl_resp(resp_buf, resp_buf_size, "{\"status\":\"%s\"}",
                             ret == 0 ? "ok" : "error");
    }

    return -1; /* not handled */
}
