/*
 * rss_daemon.c — Raptor Streaming System daemonization and signal handling
 *
 * Standard double-fork daemonization, PID file management,
 * and POSIX signal setup for clean shutdown.
 */

#include "rss_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <getopt.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <errno.h>

#define PID_DIR RSS_RUN_DIR
#define PID_PATH_MAX 128

/* ------------------------------------------------------------------ */
/* Signal state                                                        */
/* ------------------------------------------------------------------ */

static volatile sig_atomic_t g_running = 1;

static void sig_term_handler(int sig)
{
    (void)sig;
    g_running = 0;
}

/* ------------------------------------------------------------------ */
/* PID file helpers                                                    */
/* ------------------------------------------------------------------ */

static void make_pid_path(char *buf, int buf_size, const char *name)
{
    snprintf(buf, (size_t)buf_size, "%s/%s.pid", PID_DIR, name);
}

/* ------------------------------------------------------------------ */
/* Daemonization                                                       */
/* ------------------------------------------------------------------ */

int rss_daemonize(const char *name, bool already_daemon)
{
    char path[PID_PATH_MAX];
    make_pid_path(path, (int)sizeof(path), name);
    rss_mkdir_p(PID_DIR);

    /* Open pidfile and acquire exclusive lock BEFORE forking.
     * This eliminates the TOCTOU race between check and fork —
     * flock is atomic and the lock is inherited through fork. */
    int pid_fd = open(path, O_WRONLY | O_CREAT, 0600);
    if (pid_fd < 0)
        return -1;

    if (flock(pid_fd, LOCK_EX | LOCK_NB) < 0) {
        close(pid_fd);
        int existing = rss_daemon_check(name);
        fprintf(stderr, "%s: already running (pid %d)\n", name, existing > 0 ? existing : 0);
        return -1;
    }

    if (!already_daemon) {
        /* First fork — detach from parent */
        pid_t pid = fork();
        if (pid < 0) {
            close(pid_fd);
            return -1;
        }
        if (pid > 0) {
            fprintf(stderr, "%s launched\n", name);
            _exit(0);
        }

        /* New session leader */
        if (setsid() < 0)
            return -1;

        /* Second fork — prevent reacquiring a controlling terminal */
        pid = fork();
        if (pid < 0)
            return -1;
        if (pid > 0)
            _exit(0);

        /* Redirect stdin/stdout/stderr to /dev/null */
        int fd = open("/dev/null", O_RDWR);
        if (fd >= 0) {
            dup2(fd, STDIN_FILENO);
            dup2(fd, STDOUT_FILENO);
            dup2(fd, STDERR_FILENO);
            if (fd > STDERR_FILENO)
                close(fd);
        }

        /* Detach from the invoker's working directory. A failure here
         * is non-fatal — we still function from whatever cwd we had —
         * but worth logging because it suggests a broken filesystem. */
        if (chdir("/") != 0)
            RSS_WARN("rss_daemon_init: chdir(/) failed: %s", strerror(errno));
        umask(0);
    }

    /* Write final daemon PID (after double-fork this is the grandchild).
     * Use raw I/O — stdio fopen/fprintf internally malloc buffers
     * which can interfere with subsequent mmap on MIPS uclibc.
     *
     * ftruncate failure here is rare but non-fatal: lseek+write below
     * still overwrites the PID prefix. Log so we notice pathological
     * cases (disk full, quota, filesystem gone read-only). */
    if (ftruncate(pid_fd, 0) != 0)
        RSS_WARN("rss_daemon_init: ftruncate(pid_fd) failed: %s", strerror(errno));
    lseek(pid_fd, 0, SEEK_SET);
    char buf[16];
    int len = snprintf(buf, sizeof(buf), "%d\n", (int)getpid());
    if (write(pid_fd, buf, (size_t)len) != len) {
        close(pid_fd);
        return -1;
    }

    /* Intentionally keep pid_fd open — flock held until process exit */
    return 0;
}

void rss_daemon_cleanup(const char *name)
{
    char path[PID_PATH_MAX];
    make_pid_path(path, (int)sizeof(path), name);
    unlink(path);
}

int rss_daemon_check(const char *name)
{
    char path[PID_PATH_MAX];
    make_pid_path(path, (int)sizeof(path), name);

    FILE *fp = fopen(path, "r");
    if (!fp)
        return 0;

    int pid = 0;
    if (fscanf(fp, "%d", &pid) != 1 || pid <= 0) {
        fclose(fp);
        return 0;
    }
    fclose(fp);

    /* Check if process is alive */
    if (kill((pid_t)pid, 0) == 0)
        return pid;

    if (errno == EPERM)
        return pid; /* exists but we lack permission to signal it */

    /* Stale PID file */
    return 0;
}

/* ------------------------------------------------------------------ */
/* Signal handling                                                     */
/* ------------------------------------------------------------------ */

volatile sig_atomic_t *rss_signal_init(void)
{
    struct sigaction sa;

    g_running = 1;

    /* SIGTERM, SIGINT → clean shutdown */
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sig_term_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);

    /* SIGHUP → intentionally ignored. Config changes are applied via
     * the control socket (raptorctl), not signal-based reload. */
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGHUP, &sa, NULL);

    /* SIGPIPE → ignore (broken socket writes) */
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGPIPE, &sa, NULL);

    return &g_running;
}

/* ------------------------------------------------------------------ */
/* Daemon init helper                                                  */
/* ------------------------------------------------------------------ */

int rss_daemon_init(rss_daemon_ctx_t *ctx, const char *name, int argc, char **argv,
                    const char *features)
{
    if (!ctx || !name)
        return -1;

    memset(ctx, 0, sizeof(*ctx));
    ctx->name = name;

    /* Skip leading space from preprocessor concatenation */
    if (features && *features == ' ')
        features++;
    if (features && !*features)
        features = NULL;

    const char *config_path = RSS_CONFIG_PATH;
    bool foreground = false;
    bool debug = false;
    bool show_help = false;
    int opt;

    /* Banner — always first output, before any other action */
    if (features)
        fprintf(stderr, "Raptor Streaming System — %s [%s] built %s (%s)\n",
                name, rss_build_hash, rss_build_time, features);
    else
        fprintf(stderr, "Raptor Streaming System — %s [%s] built %s\n",
                name, rss_build_hash, rss_build_time);

    optind = 1; /* reset getopt */
    while ((opt = getopt(argc, argv, "c:fdhv")) != -1) {
        switch (opt) {
        case 'c':
            config_path = optarg;
            break;
        case 'f':
            foreground = true;
            break;
        case 'd':
            debug = true;
            break;
        case 'v':
            return 1; /* banner already printed */
        case 'h':
            show_help = true;
            break;
        default:
            fprintf(stderr,
                    "Usage: %s [-c config] [-f] [-d] [-v] [-h]\n",
                    name);
            return -1;
        }
    }

    if (show_help) {
        fprintf(stderr,
                "Usage: %s [-c config] [-f] [-d] [-v] [-h]\n"
                "  -c <file>   Config file (default: " RSS_CONFIG_PATH ")\n"
                "  -f          Run in foreground\n"
                "  -d          Debug logging\n"
                "  -v          Show version\n"
                "  -h          Show this help\n",
                name);
        return 1; /* clean exit */
    }

    ctx->config_path = config_path;
    ctx->foreground = foreground;
    ctx->debug = debug;

    /* Initial logging to stderr/syslog so config load errors are visible */
    rss_log_init(name, debug ? RSS_LOG_TRACE : RSS_LOG_INFO,
                 foreground ? RSS_LOG_TARGET_STDERR : RSS_LOG_TARGET_SYSLOG, NULL);

    ctx->cfg = rss_config_load(config_path);
    if (!ctx->cfg) {
        RSS_FATAL("failed to load config: %s", config_path);
        return -1;
    }

    /* Re-init logging from [log] config section.
     * -d flag overrides config level. -f overrides target to stderr. */
    {
        const char *cfg_level = rss_config_get_str(ctx->cfg, "log", "level", "info");
        const char *cfg_target = rss_config_get_str(ctx->cfg, "log", "target", "syslog");
        const char *cfg_file = rss_config_get_str(ctx->cfg, "log", "file", "");

        rss_log_level_t level = RSS_LOG_INFO;
        if (strcmp(cfg_level, "fatal") == 0)
            level = RSS_LOG_FATAL;
        else if (strcmp(cfg_level, "error") == 0)
            level = RSS_LOG_ERROR;
        else if (strcmp(cfg_level, "warn") == 0)
            level = RSS_LOG_WARN;
        else if (strcmp(cfg_level, "info") == 0)
            level = RSS_LOG_INFO;
        else if (strcmp(cfg_level, "debug") == 0)
            level = RSS_LOG_DEBUG;
        else if (strcmp(cfg_level, "trace") == 0)
            level = RSS_LOG_TRACE;
        if (debug)
            level = RSS_LOG_TRACE; /* -d flag includes trace */

        rss_log_target_t target = RSS_LOG_TARGET_SYSLOG;
        if (foreground)
            target = RSS_LOG_TARGET_STDERR; /* -f overrides */
        else if (strcmp(cfg_target, "stderr") == 0)
            target = RSS_LOG_TARGET_STDERR;
        else if (strcmp(cfg_target, "file") == 0)
            target = RSS_LOG_TARGET_FILE;

        rss_log_init(name, level, target, cfg_file[0] ? cfg_file : NULL);
    }

    if (rss_daemonize(name, foreground) < 0) {
        RSS_FATAL("daemonize failed");
        rss_config_free(ctx->cfg);
        ctx->cfg = NULL;
        return -1;
    }

    ctx->running = rss_signal_init();
    if (!foreground)
        RSS_BANNER(name, features);
    RSS_INFO("%s starting", name);
    return 0;
}
