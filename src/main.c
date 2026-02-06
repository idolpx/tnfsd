/* The MIT License
 *
 * Copyright (c) 2010 Dylan Smith
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * The main()
 *
 * */

#ifdef WIN32
#define _WINSOCKAPI_   /* Prevent inclusion of winsock.h in windows.h */
#include <winsock2.h>
#include <windows.h>
#endif

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#ifndef WIN32
#include <unistd.h>
#else
#include <getopt.h>
#define STDERR_FILENO 2
#endif

#include "config.h"
#include "chroot.h"
#include "log.h"
#include "tnfsd.h"
#ifdef HAVE_SQLITE3
#include "locatedb.h"
#endif

/* declare the main() - it won't be used elsewhere so I'll not bother
 * with putting it in a .h file */
int main(int argc, char **argv);

void print_usage();

int main(int argc, char **argv)
{
    int opt;
#ifdef ENABLE_CHROOT
    char *uvalue = NULL;
    char *gvalue = NULL;
#endif
    bool read_only = false;
    bool force_rescan = false;
    bool atari_mode = false;
    char *pvalue = NULL;
    char *root_path = NULL;
    int locate_scan_interval_hours = 24;

    #ifdef ENABLE_CHROOT
    while((opt = getopt(argc, argv, "rfu:g:p:l:")) != -1)
    #else
    while((opt = getopt(argc, argv, "rfp:l:")) != -1)
    while((opt = getopt(argc, argv, "aru:g:p:")) != -1)
    #else
    while((opt = getopt(argc, argv, "arp:")) != -1)
    #endif
    {
        switch(opt)
        {
            case 'p':
                pvalue = optarg;
                break;
            case 'r':
                read_only = true;
                break;
            case 'f':
                force_rescan = true;
                break;
            case 'l':
                locate_scan_interval_hours = atoi(optarg);
                if (locate_scan_interval_hours < 0) {
                    fprintf(stderr, "Invalid locate scan interval\n");
                    exit(-1);
                }
            case 'a':
                atari_mode = true;
                break;
            #ifdef ENABLE_CHROOT
            case 'u':
                uvalue = optarg;
                break;
            case 'g':
                gvalue = optarg;
                break;
            #endif
            case ':':
                fprintf(stderr, "option needs a value\n");
                print_usage();
                exit(-1);
                break;
            default:
            case '?':
                fprintf(stderr, "unknown option: %c\n", optopt);
                print_usage();
                exit(-1);
                break;
        }
    }

    if (optind < argc)
    {
        root_path = argv[optind++];
    }
    if (optind < argc)
    {
        fprintf(stderr, "parameters after the root dir %s are not allowed\n", root_path);
        print_usage();
        exit(-1);
    }
    if (root_path == NULL)
    {
        fprintf(stderr, "please specify root dir\n");
        print_usage();
        exit(-1);
    }

    #ifdef ENABLE_CHROOT
    if (uvalue || gvalue)
    {
        /* chroot into the specified directory and drop privs */
        if (uvalue == NULL)
        {
            fprintf(stderr, "chroot username required\n");
            exit(-1);
        } else if (gvalue == NULL)
        {
            fprintf(stderr, "chroot group required\n");
            exit(-1);
        }
        fprintf(stderr, "tnfsd will be jailed at %s\n", root_path);
        chroot_tnfs(uvalue, gvalue, root_path);
        root_path = strdup("/");
    }
    warn_if_root();
    #endif

    int port = TNFSD_PORT;

    if (pvalue)
    {
        port = atoi(pvalue);
        if (port < 1 || port > 65535)
        {
            fprintf(stderr, "Invalid port\n");
            exit(-1);
        }
    }

    tnfsd_init();
    tnfsd_init_logs(STDERR_FILENO);
    signal(SIGINT, tnfsd_stop);
#ifdef HAVE_SQLITE3
    if (locate_scan_interval_hours > 0) {
        int scan_interval_seconds = locate_scan_interval_hours * 3600;
        if (locatedb_init(root_path, scan_interval_seconds) != 0) {
            fprintf(stderr, "Warning: Failed to initialize locate database\n");
        } else {
            /* Start background scan thread */
            if (locatedb_start_scan_thread(root_path) != 0) {
                fprintf(stderr, "Warning: Failed to start locate database scan thread\n");
            }
            /* Force rescan if requested */
            if (force_rescan && locatedb_force_rescan() != 0) {
                fprintf(stderr, "Warning: Failed to force locate database rescan\n");
            }
        }
    }

    /* Stop background scan thread before shutdown */
    locatedb_stop_scan_thread();
    locatedb_close();
#endif
    tnfsd_start(root_path, port, read_only, atari_mode);

    return 0;
}

void print_usage()
{
    #ifdef ENABLE_CHROOT
    fprintf(stderr, "Usage: tnfsd [-u <username> -g <group> -p <port> -r -f] [-l <scan_interval_hours>] <root dir>\n");
    #else
    fprintf(stderr, "Usage: tnfsd [-p <port> -r -f] [-l <scan_interval_hours>] <root dir>\n");
    #endif
    fprintf(stderr, "  -p <port>        Port number (default: %d)\n", TNFSD_PORT);
    fprintf(stderr, "  -r               Read-only mode\n");
    fprintf(stderr, "  -f               Force locate DB rescan on startup\n");
    #ifdef ENABLE_CHROOT
    fprintf(stderr, "  -u <username>    Username for chroot (requires -g)\n");
    fprintf(stderr, "  -g <group>       Group for chroot (requires -u)\n");
    #endif
    #ifdef HAVE_SQLITE3
    fprintf(stderr, "  -l <hours>       Locate DB scan interval in hours (default: 24, 0=disabled)\n");
    fprintf(stderr, "Usage: tnfsd [-u <username> -g <group> -p <port> -r -a] <root dir>\n");
    fprintf(stderr, "  -a  Enable Atari mode (present binary files as ATR images)\n");
    #else
    fprintf(stderr, "Usage: tnfsd [-p <port> -r -a] <root dir>\n");
    fprintf(stderr, "  -a  Enable Atari mode (present binary files as ATR images)\n");
    #endif
}