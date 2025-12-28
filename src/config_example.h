/* The MIT License
 *
 * Copyright (c) 2025 TNFS Contributors
 *
 * Configuration file support for TNFS daemon
 */

#ifndef _CONFIG_H_LOCATEDB
#define _CONFIG_H_LOCATEDB

/* This file documents the configuration format for tnfsd.conf
 * 
 * The daemon looks for tnfsd.conf in the root directory.
 * Lines starting with # are comments.
 *
 * Configuration options:
 * 
 *   locate_db_scan_interval = <seconds>
 *     How often to perform full filesystem scans for the locate database.
 *     Set to 0 or comment out to disable periodic scanning.
 *     Example: locate_db_scan_interval = 3600  (scan every hour)
 *
 * Future options can be added as needed.
 */

#endif
