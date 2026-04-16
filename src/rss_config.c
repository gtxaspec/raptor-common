/*
 * rss_config.c — Raptor Streaming System INI config parser
 *
 * Simple [section] / key = value parser. Linked list storage,
 * case-insensitive section and key lookup, inline comment stripping.
 */

#include "rss_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h> /* strcasecmp */
#include <ctype.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/file.h>

/* ------------------------------------------------------------------ */
/* Internal data structures                                            */
/* ------------------------------------------------------------------ */

#define MAX_LINE 512
#define MAX_SECN 64
#define MAX_KEY 64
#define MAX_VAL 256

typedef struct rss_config_entry {
    char key[MAX_KEY];
    char value[MAX_VAL];
    bool dirty; /* modified at runtime via set_str/set_int */
    struct rss_config_entry *next;
} rss_config_entry_t;

typedef struct rss_config_section {
    char name[MAX_SECN];
    rss_config_entry_t *entries;
    struct rss_config_section *next;
} rss_config_section_t;

struct rss_config {
    rss_config_section_t *sections;
};

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

/* Find or create a section. Empty name ("") is the global section. */
static rss_config_section_t *find_or_create_section(rss_config_t *cfg, const char *name)
{
    rss_config_section_t *s;
    for (s = cfg->sections; s; s = s->next) {
        if (strcasecmp(s->name, name) == 0)
            return s;
    }

    s = calloc(1, sizeof(*s));
    if (!s)
        return NULL;
    rss_strlcpy(s->name, name, sizeof(s->name));
    s->next = cfg->sections;
    cfg->sections = s;
    return s;
}

static void add_entry_ex(rss_config_section_t *sec, const char *key, const char *value,
                         bool dirty)
{
    /* Overwrite if key already exists */
    rss_config_entry_t *e;
    for (e = sec->entries; e; e = e->next) {
        if (strcasecmp(e->key, key) == 0) {
            if (rss_strlcpy(e->value, value, sizeof(e->value)) >= sizeof(e->value))
                RSS_WARN("config: value truncated for key '%s' (max %d)", key, MAX_VAL - 1);
            e->dirty = e->dirty || dirty;
            return;
        }
    }

    e = calloc(1, sizeof(*e));
    if (!e) {
        RSS_WARN("config: alloc failed for key '%s'", key);
        return;
    }
    rss_strlcpy(e->key, key, sizeof(e->key));
    if (rss_strlcpy(e->value, value, sizeof(e->value)) >= sizeof(e->value))
        RSS_WARN("config: value truncated for key '%s' (max %d)", key, MAX_VAL - 1);
    e->dirty = dirty;
    e->next = sec->entries;
    sec->entries = e;
}

static void add_entry(rss_config_section_t *sec, const char *key, const char *value)
{
    add_entry_ex(sec, key, value, false);
}

/* Strip inline comment: look for ' #' or '\t#'.
 * Does NOT handle quoted values — "foo # bar" will be truncated at #.
 * Config files currently use no quoted values. */
static void strip_inline_comment(char *s)
{
    char *p = s;
    while ((p = strchr(p, '#')) != NULL) {
        if (p > s && (*(p - 1) == ' ' || *(p - 1) == '\t')) {
            /* Trim trailing whitespace before the comment marker */
            char *end = p - 1;
            while (end > s && (*end == ' ' || *end == '\t'))
                end--;
            *(end + 1) = '\0';
            return;
        }
        p++;
    }
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

rss_config_t *rss_config_load(const char *path)
{
    FILE *fp = fopen(path, "r");
    if (!fp)
        return NULL;

    rss_config_t *cfg = calloc(1, sizeof(*cfg));
    if (!cfg) {
        fclose(fp);
        return NULL;
    }

    char current_section[MAX_SECN] = "";
    char line[MAX_LINE];

    while (fgets(line, (int)sizeof(line), fp)) {
        char *s = rss_trim(line);

        /* Skip empty lines and full-line comments */
        if (*s == '\0' || *s == '#' || *s == ';')
            continue;

        /* Section header: [name] */
        if (*s == '[') {
            char *end = strchr(s, ']');
            if (end) {
                *end = '\0';
                rss_strlcpy(current_section, rss_trim(s + 1), sizeof(current_section));
            }
            continue;
        }

        /* Key = value */
        char *eq = strchr(s, '=');
        if (!eq) {
            RSS_WARN("config: malformed line (no '='): %s", s);
            continue;
        }

        *eq = '\0';
        char *key = rss_trim(s);
        char *val = rss_trim(eq + 1);

        /* Strip inline comments from value */
        strip_inline_comment(val);
        val = rss_trim(val);

        if (*key == '\0')
            continue;

        rss_config_section_t *sec = find_or_create_section(cfg, current_section);
        if (sec)
            add_entry(sec, key, val);
    }

    fclose(fp);
    return cfg;
}

void rss_config_free(rss_config_t *cfg)
{
    if (!cfg)
        return;

    rss_config_section_t *s = cfg->sections;
    while (s) {
        rss_config_section_t *ns = s->next;
        rss_config_entry_t *e = s->entries;
        while (e) {
            rss_config_entry_t *ne = e->next;
            free(e);
            e = ne;
        }
        free(s);
        s = ns;
    }
    free(cfg);
}

const char *rss_config_get_str(rss_config_t *cfg, const char *section, const char *key,
                               const char *default_val)
{
    if (!cfg || !key)
        return default_val;

    const char *sec_name = section ? section : "";

    rss_config_section_t *s;
    for (s = cfg->sections; s; s = s->next) {
        if (strcasecmp(s->name, sec_name) != 0)
            continue;
        rss_config_entry_t *e;
        for (e = s->entries; e; e = e->next) {
            if (strcasecmp(e->key, key) == 0)
                return e->value;
        }
    }

    /* Auto-populate default so config-get-section shows all resolved values.
     * This mutates the config on read — acceptable because all daemon access
     * is single-threaded (init + ctrl handler both on main thread via epoll).
     * Not safe for concurrent readers on the same config object.
     * Only when default_val is non-NULL (callers with NULL default don't want storage).
     * Uses add_entry (not set_str) so defaults are NOT marked dirty. */
    if (default_val) {
        rss_config_section_t *ds = find_or_create_section(cfg, sec_name);
        if (ds) {
            add_entry(ds, key, default_val);
            rss_config_entry_t *de;
            for (de = ds->entries; de; de = de->next) {
                if (strcasecmp(de->key, key) == 0)
                    return de->value;
            }
        }
    }
    return default_val;
}

int rss_config_get_int(rss_config_t *cfg, const char *section, const char *key, int default_val)
{
    const char *val = rss_config_get_str(cfg, section, key, NULL);
    if (!val) {
        /* Store default so config-get-section shows it (not dirty) */
        if (cfg) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%d", default_val);
            rss_config_section_t *sec = find_or_create_section(cfg, section ? section : "");
            if (sec)
                add_entry(sec, key, buf);
        }
        return default_val;
    }

    char *end;
    long v = strtol(val, &end, 0);
    if (end == val)
        return default_val;
    return (int)v;
}

bool rss_config_get_bool(rss_config_t *cfg, const char *section, const char *key, bool default_val)
{
    const char *val = rss_config_get_str(cfg, section, key, NULL);
    if (!val) {
        /* Store default so config-get-section shows it (not dirty) */
        if (cfg) {
            rss_config_section_t *sec = find_or_create_section(cfg, section ? section : "");
            if (sec)
                add_entry(sec, key, default_val ? "true" : "false");
        }
        return default_val;
    }

    if (strcasecmp(val, "true") == 0 || strcasecmp(val, "yes") == 0 || strcasecmp(val, "on") == 0 ||
        strcmp(val, "1") == 0)
        return true;

    if (strcasecmp(val, "false") == 0 || strcasecmp(val, "no") == 0 ||
        strcasecmp(val, "off") == 0 || strcmp(val, "0") == 0)
        return false;

    return default_val;
}

int rss_config_foreach(rss_config_t *cfg, const char *section,
                       void (*callback)(const char *key, const char *value, void *userdata),
                       void *userdata)
{
    if (!cfg || !callback)
        return 0;

    const char *sec_name = section ? section : "";
    int count = 0;

    rss_config_section_t *s;
    for (s = cfg->sections; s; s = s->next) {
        if (strcasecmp(s->name, sec_name) != 0)
            continue;
        rss_config_entry_t *e;
        for (e = s->entries; e; e = e->next) {
            callback(e->key, e->value, userdata);
            count++;
        }
    }
    return count;
}

/* ------------------------------------------------------------------ */
/* Config modification (running-config support)                        */
/* ------------------------------------------------------------------ */

void rss_config_set_str(rss_config_t *cfg, const char *section, const char *key, const char *value)
{
    if (!cfg || !key || !value)
        return;
    rss_config_section_t *sec = find_or_create_section(cfg, section ? section : "");
    if (sec)
        add_entry_ex(sec, key, value, true);
}

void rss_config_set_int(rss_config_t *cfg, const char *section, const char *key, int value)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", value);
    rss_config_set_str(cfg, section, key, buf);
}

/* Write a config to a file (no merging) */
static int config_write(rss_config_t *cfg, const char *path)
{
    /* Count sections to reverse linked-list order (restore file order) */
    int nsec = 0;
    rss_config_section_t *s;
    for (s = cfg->sections; s; s = s->next)
        nsec++;

    if (nsec == 0)
        return rss_write_file_atomic(path, "", 0);

    /* Collect section pointers for reverse traversal */
    rss_config_section_t **secs = malloc(nsec * sizeof(*secs));
    if (!secs)
        return -1;

    int i = 0;
    for (s = cfg->sections; s; s = s->next)
        secs[i++] = s;

    /* Build output into a dynamic buffer */
    int buf_size = 4096;
    char *buf = malloc(buf_size);
    if (!buf) {
        free(secs);
        return -1;
    }
    int off = 0;
    int ret = -1;

    for (i = nsec - 1; i >= 0; i--) {
        s = secs[i];

        /* Count entries for reverse traversal */
        int nent = 0;
        rss_config_entry_t *e;
        for (e = s->entries; e; e = e->next)
            nent++;

        if (nent == 0)
            continue;

        rss_config_entry_t **ents = malloc(nent * sizeof(*ents));
        if (!ents)
            goto out;

        int j = 0;
        for (e = s->entries; e; e = e->next)
            ents[j++] = e;

        /* Section header (skip for global section) */
        if (s->name[0] != '\0') {
            int need = off + (int)strlen(s->name) + 4;
            if (need > buf_size) {
                int new_size = need + 4096;
                char *nb = realloc(buf, new_size);
                if (!nb) {
                    free(ents);
                    goto out;
                }
                buf = nb;
                buf_size = new_size;
            }
            if (off > 0)
                buf[off++] = '\n'; /* blank line between sections */
            off += snprintf(buf + off, buf_size - off, "[%s]\n", s->name);
        }

        /* Entries in original order */
        for (j = nent - 1; j >= 0; j--) {
            int need = off + (int)strlen(ents[j]->key) + (int)strlen(ents[j]->value) + 8;
            if (need > buf_size) {
                int new_size = need + 4096;
                char *nb = realloc(buf, new_size);
                if (!nb) {
                    free(ents);
                    goto out;
                }
                buf = nb;
                buf_size = new_size;
            }
            off += snprintf(buf + off, buf_size - off, "%s = %s\n", ents[j]->key, ents[j]->value);
        }

        free(ents);
    }

    ret = rss_write_file_atomic(path, buf, off);

out:
    free(secs);
    free(buf);
    return ret;
}

int rss_config_save(rss_config_t *cfg, const char *path)
{
    if (!cfg || !path)
        return -1;

    /* Serialize saves across daemons sharing the same config file.
     * flock on a .lock sidecar prevents concurrent load-merge-write
     * cycles from losing updates via the atomic rename. */
    char lockpath[512];
    snprintf(lockpath, sizeof(lockpath), "%s.lock", path);
    int lock_fd = open(lockpath, O_WRONLY | O_CREAT, 0644);
    if (lock_fd >= 0)
        flock(lock_fd, LOCK_EX);

    /* Only merge entries modified at runtime (dirty) into the existing
     * file.  Each daemon shares /etc/raptor.conf but owns different
     * sections — writing the entire in-memory config would clobber
     * changes made by other daemons. */
    int ret;
    rss_config_t *disk = rss_config_load(path);
    if (disk) {
        rss_config_section_t *s;
        for (s = cfg->sections; s; s = s->next) {
            rss_config_entry_t *e;
            for (e = s->entries; e; e = e->next) {
                if (e->dirty)
                    rss_config_set_str(disk, s->name, e->key, e->value);
            }
        }
        ret = config_write(disk, path);
        rss_config_free(disk);
    } else {
        /* No existing file — write everything */
        ret = config_write(cfg, path);
    }

    if (lock_fd >= 0) {
        flock(lock_fd, LOCK_UN);
        close(lock_fd);
    }
    return ret;
}
