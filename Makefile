CC=gcc
LOCATE_DB ?= yes

# Auto-detect OS if not provided. Map uname output to expected values.
ifndef OS
    UNAME_S := $(shell uname -s 2>/dev/null)
    ifeq ($(UNAME_S),Darwin)
        OS := BSD
    else ifeq ($(UNAME_S),Linux)
        OS := LINUX
    else ifeq ($(UNAME_S),Windows_NT)
        OS := Windows_NT
    else ifneq (,$(findstring CYGWIN,$(UNAME_S)))
        OS := Windows_NT
    else ifneq (,$(findstring MSYS,$(UNAME_S)))
        OS := Windows_NT
    else ifneq (,$(findstring MINGW,$(UNAME_S)))
        OS := Windows_NT
    else
        $(error Unknown OS. Please run `make OS=LINUX|BSD|Windows_NT`)
    endif
endif

ifeq ($(OS),LINUX)
    FLAGS = -Wall -DUNIX -DNEED_BSDCOMPAT
    EXOBJS = strlcpy.o strlcat.o event_epoll.o 
    LIBS =
    EXEC = tnfsd
endif
ifeq ($(OS),Windows_NT)
    FLAGS = -Wall -DWIN32 -DNEED_BSDCOMPAT
    EXOBJS = strlcpy.o strlcat.o event_select.o
    LIBS = -lwsock32
    EXEC = tnfsd.exe
endif
ifeq ($(OS),BSD)
    FLAGS = -Wall -DUNIX -DBSD
    EXOBJS = event_kqueue.o
    LIBS =
    EXEC = tnfsd
endif

# ENABLE_CHROOT defaults to yes on Unix systems for security
# Disable with: make ENABLE_CHROOT=no
ifeq ($(OS),LINUX)
    ENABLE_CHROOT ?= yes
endif
ifeq ($(OS),BSD)
    ENABLE_CHROOT ?= yes
endif

ifeq ($(ENABLE_CHROOT),yes)
    CHROOTFLAGS = -DENABLE_CHROOT
endif

ifdef DEBUG
    EXFLAGS = -g -DDEBUG
endif

ifdef USAGELOG
    LOGFLAGS = -DUSAGELOG
endif

ifdef LOCATE_DB
    LOCATEDBFLAGS = -DHAVE_SQLITE3
    LOCATEDBOBJS = locatedb.o
    LOCATEDBLIBS = -lsqlite3 -lpthread
endif

CFLAGS=$(FLAGS) $(EXFLAGS) $(LOGFLAGS) -DNEED_ERRTABLE $(LOCATEDBFLAGS)

# Source directory
SRCDIR = src

# Build directory and output
OBJDIR = build
BINDIR = bin

# Object files with build directory path
OBJS = $(OBJDIR)/main.o $(OBJDIR)/datagram.o $(OBJDIR)/event_common.o $(OBJDIR)/log.o \
       $(OBJDIR)/session.o $(OBJDIR)/endian.o $(OBJDIR)/directory.o $(OBJDIR)/errortable.o \
       $(OBJDIR)/tnfs_file.o $(OBJDIR)/chroot.o $(OBJDIR)/fileinfo.o $(OBJDIR)/stats.o \
       $(OBJDIR)/auth.o $(OBJDIR)/traverse.o $(OBJDIR)/match.o $(OBJDIR)/tnfsd.o $(OBJDIR)/atari.o \
       $(EXOBJS:%.o=$(OBJDIR)/%.o) $(LOCATEDBOBJS:%.o=$(OBJDIR)/%.o)

LIBS += $(LOCATEDBLIBS)

.PHONY: all clean directories

# Check if boot sector assembly source exists
ATARI_BOOT_ASM := $(wildcard atari-boot-xex-file.asm)
ifneq ($(ATARI_BOOT_ASM),)
    # Check if xa assembler is available
    XA := $(shell which xa 2>/dev/null)
    ifneq ($(XA),)
        CFLAGS += -DATARI_BOOTSECTOR_EXISTS
        ATARI_BOOT_EXISTS := yes
    else
        $(error xa assembler not found but $(ATARI_BOOT_ASM) exists. Install xa: brew install xa (macOS) or apt-get install xa65 (Linux))
    endif
endif

all: directories $(BINDIR)/$(EXEC)

directories:
	@mkdir -p $(OBJDIR) $(BINDIR)

$(BINDIR)/$(EXEC): $(OBJS)
	$(CC) -o $@ $(OBJS) $(LIBS)

$(OBJDIR)/%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) -c -o $@ $<
	
# Assemble boot sector from assembly source
atari_bootsector.bin: $(SRCDIR)/atari-boot-xex-file.asm
	@echo "Assembling Atari boot sector from assembly source..."
	xa -o $(OBJDIR)/atari_bootsector.bin $(SRCDIR)/atari-boot-xex-file.asm

# Auto-generate boot sector header from binary
atari_bootsector.h: $(OBJDIR)/atari_bootsector.bin
	@echo "Converting Atari boot sector binary to header..."
	xxd -i $(OBJDIR)/atari_bootsector.bin > $(SRCDIR)/atari_bootsector.h

# atari.o depends on the generated header (if boot sector source exists)
ifeq ($(ATARI_BOOT_EXISTS),yes)
atari.o: atari.c atari.h atari_bootsector.h
else
atari.o: atari.c atari.h
endif

clean:
	rm -rf $(OBJDIR) $(BINDIR) $(SRCDIR)/atari_bootsector.h

help:
	@echo "TNFSD Makefile - Usage:"
	@echo ""
	@echo "Build targets:"
	@echo "  make OS=LINUX         - Build for Linux (default if on Linux)"
	@echo "  make OS=BSD           - Build for macOS/BSD (default if on macOS)"
	@echo "  make OS=Windows_NT    - Build for Windows"
	@echo ""
	@echo "Optional flags:"
	@echo "  make ... DEBUG=yes    - Build with debug symbols and logging"
	@echo "  make ... USAGELOG=yes - Include usage logging"
	@echo "  make ... LOCATE_DB=yes - Build with SQLite3 locate database support"
	@echo ""
	@echo "Examples:"
	@echo "  make OS=BSD LOCATE_DB=yes            - macOS with locate DB"
	@echo "  make OS=LINUX LOCATE_DB=yes DEBUG=yes - Linux debug with locate DB"
	@echo ""
	@echo "Other targets:"
	@echo "  make clean            - Remove build artifacts"
	@echo "  make help             - Show this help message"
