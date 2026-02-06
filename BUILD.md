# Building the tnfs daemon

Run `make` from the project root. The OS is auto-detected, but you can
override it with `make OS=osname`. Valid OS values:

```
   make OS=LINUX       All versions of Linux
   make OS=BSD         macOS and BSD variants (tested on OpenBSD)
   make OS=Windows_NT  Windows (MinGW, Cygwin, MSYS2)
```

On Windows with Cygwin or MSYS2, you can also use the included `build.bat`
script, which auto-detects the Cygwin/MSYS2 installation path:

```
   build.bat
   build.bat DEBUG=yes
   build.bat LOCATE_DB=no
```

## Build options

| Variable | Default | Description |
|---|---|---|
| `DEBUG=yes` | no | Adds `-g` flag and extra debug messages |
| `USAGELOG=yes` | no | Outputs basic usage log on stdout |
| `LOCATE_DB=yes\|no` | yes | Enables SQLite3 locate database (requires libsqlite3) |
| `ENABLE_CHROOT=yes\|no` | yes (Unix only) | Enables chroot support (Linux and BSD only) |

## Make targets

| Target | Description |
|---|---|
| `all` | Build the executable (default) |
| `clean` | Remove all build artifacts |
| `help` | Display available options |

