# raptor-common

Shared C library for the Raptor Streaming System (RSS) -- provides logging,
INI configuration, daemonization, signal handling, and utility functions used
by all RSS daemons (RVD, ROD, RAD, RSD). Pure POSIX C11, no vendor dependencies.

## Components

| File | Description |
|---|---|
| `rss_log.c` | Leveled logging (FATAL..TRACE) to stderr, syslog, or file. Millisecond timestamps, thread-safe via `flockfile`. |
| `rss_config.c` | INI parser with `[section]` / `key = value` support. Case-insensitive lookup, inline comment stripping, runtime modification, atomic save. |
| `rss_daemon.c` | Double-fork daemonization, PID file management (`/var/run/rss/<name>.pid`), POSIX signal setup (SIGTERM/SIGINT for shutdown, SIGHUP for reload, SIGPIPE ignored). |
| `rss_util.c` | Monotonic/wall-clock timestamps, `strftime` formatting, safe string copy (`rss_strlcpy`), whitespace trim, file read/atomic write, recursive mkdir. |

## Build

```sh
# Native build
make

# Cross-compile for Ingenic MIPS
make CC=mipsel-linux-gcc AR=mipsel-linux-ar
```

Produces `librss_common.a`. Link with `-lrss_common` and add `-Iinclude`.

## API Overview

### Logging

```c
rss_log_init("rvd", RSS_LOG_DEBUG, RSS_LOG_TARGET_SYSLOG, NULL);
RSS_INFO("encoder started, channel=%d", ch);
rss_log_set_level(RSS_LOG_TRACE);
```

Six levels: `RSS_LOG_FATAL`, `RSS_LOG_ERROR`, `RSS_LOG_WARN`, `RSS_LOG_INFO`, `RSS_LOG_DEBUG`, `RSS_LOG_TRACE`. Convenience macros (`RSS_FATAL` .. `RSS_TRACE`) inject `__FILE__` and `__LINE__` automatically.

### Configuration

```c
rss_config_t *cfg = rss_config_load("/etc/rss/rvd.conf");
int width  = rss_config_get_int(cfg, "encoder", "width", 1920);
bool osd   = rss_config_get_bool(cfg, "osd", "enabled", true);
const char *font = rss_config_get_str(cfg, "osd", "font", "/usr/share/fonts/default.ttf");

// Runtime modification + atomic save
rss_config_set_int(cfg, "encoder", "bitrate", 4000);
rss_config_save(cfg, "/etc/rss/rvd.conf");

rss_config_free(cfg);
```

Boolean values accept `true/false`, `yes/no`, `on/off`, `1/0`.

### Daemon Lifecycle

```c
volatile sig_atomic_t *running = rss_signal_init();
rss_daemonize("rvd", false);

while (*running) {
    if (rss_signal_reload_requested())
        reload_config();
    // ...
}

rss_daemon_cleanup("rvd");
```

`rss_daemon_check("rvd")` returns the PID of a running instance, or 0 if not running.

### Utilities

```c
int64_t now = rss_timestamp_us();          // monotonic microseconds
char ts[20]; rss_format_timestamp(ts, 20); // "2026-03-24 14:30:00"
char *data = rss_read_file("/proc/cmdline", &size);
rss_write_file_atomic("/tmp/state.json", buf, len);
rss_mkdir_p("/var/run/rss");
```

## License

Licensed under the GNU General Public License v3.0.
