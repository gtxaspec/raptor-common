/*
 * rss_daemon.c — Raptor Streaming System daemonization and signal handling
 *
 * Standard double-fork daemonization, PID file management,
 * and POSIX signal setup for clean shutdown / config reload.
 */

#include "rss_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>

#define PID_DIR "/var/run/rss"
#define PID_PATH_MAX 128

/* ------------------------------------------------------------------ */
/* Signal state                                                        */
/* ------------------------------------------------------------------ */

static volatile sig_atomic_t g_running = 1;
static volatile sig_atomic_t g_reload = 0;

static void sig_term_handler(int sig)
{
    (void)sig;
    g_running = 0;
}

static void sig_hup_handler(int sig)
{
    (void)sig;
    g_reload = 1;
}

/* ------------------------------------------------------------------ */
/* PID file helpers                                                    */
/* ------------------------------------------------------------------ */

static void make_pid_path(char *buf, int buf_size, const char *name)
{
    snprintf(buf, (size_t)buf_size, "%s/%s.pid", PID_DIR, name);
}

static int write_pid_file(const char *name)
{
    char path[PID_PATH_MAX];
    make_pid_path(path, (int)sizeof(path), name);

    rss_mkdir_p(PID_DIR);

    FILE *fp = fopen(path, "w");
    if (!fp)
        return -1;
    fprintf(fp, "%d\n", (int)getpid());
    fclose(fp);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Daemonization                                                       */
/* ------------------------------------------------------------------ */

int rss_daemonize(const char *name, bool already_daemon)
{
    if (!already_daemon) {
        /* First fork — detach from parent */
        pid_t pid = fork();
        if (pid < 0)
            return -1;
        if (pid > 0)
            _exit(0); /* parent exits */

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

        chdir("/");
        umask(0);
    }

    return write_pid_file(name);
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
    g_reload = 0;

    /* SIGTERM, SIGINT → clean shutdown */
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sig_term_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);

    /* SIGHUP → config reload */
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sig_hup_handler;
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

bool rss_signal_reload_requested(void)
{
    if (g_reload) {
        g_reload = 0;
        return true;
    }
    return false;
}
