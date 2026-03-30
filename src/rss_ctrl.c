/*
 * rss_ctrl.c — Common control socket command handlers
 *
 * Handles config-get and config-save commands shared by all daemons.
 */

#include "rss_common.h"

#include <stdio.h>
#include <string.h>

int rss_ctrl_handle_common(const char *cmd_json, char *resp_buf, int resp_buf_size,
			   rss_config_t *cfg, const char *config_path)
{
	if (strstr(cmd_json, "\"config-get\"")) {
		char section[64], key[64];
		if (rss_json_get_str(cmd_json, "section", section, sizeof(section)) == 0 &&
		    rss_json_get_str(cmd_json, "key", key, sizeof(key)) == 0) {
			const char *v = rss_config_get_str(cfg, section, key, NULL);
			if (v)
				snprintf(resp_buf, resp_buf_size, "%s", v);
			else
				resp_buf[0] = '\0';
		} else {
			resp_buf[0] = '\0';
		}
		return (int)strlen(resp_buf);
	}

	if (strstr(cmd_json, "\"config-save\"")) {
		int ret = rss_config_save(cfg, config_path);
		snprintf(resp_buf, resp_buf_size, "{\"status\":\"%s\"}",
			 ret == 0 ? "ok" : "error");
		if (ret == 0)
			RSS_INFO("running config saved to %s", config_path);
		return (int)strlen(resp_buf);
	}

	return -1; /* not handled */
}
