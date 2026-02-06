/* The MIT License
 *
 * Copyright (c) 2025 TNFS Contributors
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
 * SQLite3-based file location database for TNFS daemon
 */

#ifndef _LOCATEDB_H
#define _LOCATEDB_H

#include <time.h>
#include <stdbool.h>

#ifdef HAVE_SQLITE3

/* Initialize the locate database at the specified root path.
 * Creates locate.db if it doesn't exist and initializes the schema.
 * scan_interval_seconds: how often to perform full filesystem scans (0 = no periodic scan)
 * Returns 0 on success, -1 on error */
int locatedb_init(const char *root_path, int scan_interval_seconds);

/* Add or update a file entry in the database.
 * path: relative path from root
 * size: file size in bytes
 * mtime: last modified time (seconds since epoch)
 * mode: file permissions/mode bits
 * is_dir: true if this is a directory
 * Returns 0 on success, -1 on error */
int locatedb_upsert_entry(const char *path, off_t size, time_t mtime, mode_t mode, bool is_dir);

/* Remove a file/directory entry from the database.
 * path: relative path from root
 * Returns 0 on success, -1 on error */
int locatedb_remove_entry(const char *path);

/* Perform a full filesystem scan and update the database.
 * This scans the entire root_path and updates all entries.
 * Returns number of entries updated, -1 on error */
int locatedb_full_scan(const char *root_path);

/* Check if database needs periodic scan based on scan_interval.
 * Should be called periodically by the main loop.
 * Returns true if a scan was performed, false otherwise */
bool locatedb_check_periodic_scan(const char *root_path);

/* Start the background scanning thread.
 * The thread will periodically perform filesystem scans based on scan_interval.
 * Call this after locatedb_init() if you want background scanning.
 * Returns 0 on success, -1 on error */
int locatedb_start_scan_thread(const char *root_path);

/* Stop the background scanning thread and wait for it to finish.
 * Call this before locatedb_close() to cleanly shutdown the thread.
 * Returns 0 on success, -1 on error */
int locatedb_stop_scan_thread(void);

/* Force an immediate rescan in the background thread.
 * The scan will start as soon as the thread wakes up (within 60 seconds).
 * Returns 0 on success, -1 on error */
int locatedb_force_rescan(void);

/* Get current scan status (files and directories scanned).
 * Returns 0 if scan is in progress, 1 if idle, -1 on error */
int locatedb_get_scan_status(int *files_scanned, int *dirs_scanned, time_t *scan_start);

/* Get previous scan statistics from database.
 * Returns 0 on success, -1 if no previous scan or error */
int locatedb_get_last_scan_stats(int *files, int *dirs, time_t *start_time, time_t *end_time);

/* Close the database and free resources.
 * Should be called on server shutdown */
void locatedb_close(void);

/* Get database statistics (for logging/monitoring).
 * num_entries: pointer to store number of entries
 * db_size: pointer to store database file size
 * Returns 0 on success, -1 on error */
int locatedb_get_stats(int *num_entries, long *db_size);

#endif /* HAVE_SQLITE3 */

#endif /* _LOCATEDB_H */
