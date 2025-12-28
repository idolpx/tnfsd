/* The MIT License
 *
 * Copyright (c) 2025 TNFS Contributors
 *
 * SQLite3-based file location database for TNFS daemon
 */

#ifdef HAVE_SQLITE3

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sqlite3.h>
#include <pthread.h>
#include <unistd.h>

#include "locatedb.h"
#include "log.h"

static sqlite3 *locate_db = NULL;
static time_t last_scan_time = 0;
static int scan_interval = 0;
static char *root_path_copy = NULL;
static pthread_t scan_thread;
static volatile int scan_thread_running = 0;
static volatile int force_initial_scan = 0;  /* Flag to force scan on thread startup */
static pthread_mutex_t db_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Scan progress tracking */
static volatile int scan_in_progress = 0;
static volatile int files_scanned = 0;
static volatile int directories_scanned = 0;
static time_t scan_start_time = 0;
static volatile int resume_scan = 0;  /* Flag to resume instead of clearing */

/* Database schema */
static const char *LOCATE_DB_SCHEMA = 
	"CREATE TABLE IF NOT EXISTS files ("
	"  id INTEGER PRIMARY KEY AUTOINCREMENT,"
	"  path TEXT UNIQUE NOT NULL,"
	"  size INTEGER,"
	"  mtime INTEGER,"
	"  mode INTEGER,"
	"  is_dir INTEGER,"
	"  updated_at DATETIME DEFAULT CURRENT_TIMESTAMP"
	");"
	"CREATE INDEX IF NOT EXISTS idx_path ON files(path);"
	"CREATE INDEX IF NOT EXISTS idx_updated ON files(updated_at);"
	"CREATE TABLE IF NOT EXISTS scan_status ("
	"  id INTEGER PRIMARY KEY CHECK (id = 1),"
	"  last_scan_start INTEGER,"
	"  last_scan_end INTEGER,"
	"  last_scan_path TEXT,"
	"  last_scan_files INTEGER,"
	"  last_scan_dirs INTEGER,"
	"  scan_in_progress INTEGER DEFAULT 0"
	");";

/* Background scanning thread function */
static void *_locate_scan_thread_func(void *arg)
{
	const char *root = (const char *)arg;
	
	LOG("Locate DB scan thread started\n");
	
	/* Perform initial scan if database was stale or doesn't exist */
	if (force_initial_scan && root) {
		pthread_mutex_lock(&db_mutex);
		if (locate_db) {
			LOG("Performing initial locate DB scan...\n");
			locatedb_full_scan(root);
			last_scan_time = time(NULL);
			LOG("Initial locate DB scan complete\n");
		}
		pthread_mutex_unlock(&db_mutex);
		force_initial_scan = 0;
	}
	
	while (scan_thread_running) {
		time_t now = time(NULL);
		
		/* Check if periodic scan is needed */
		if (scan_interval > 0 && (now - last_scan_time) >= scan_interval) {
			pthread_mutex_lock(&db_mutex);
			if (locate_db) {
				LOG("Background locate DB scan starting...\n");
				locatedb_full_scan(root);
				last_scan_time = time(NULL);
				LOG("Background locate DB scan complete\n");
			}
			pthread_mutex_unlock(&db_mutex);
		}
		
		/* Sleep in 1-second intervals to allow quick shutdown response */
		for (int i = 0; i < 60 && scan_thread_running; i++) {
			sleep(1);
		}
	}
	
	LOG("Locate DB scan thread stopped\n");
	return NULL;
}

int locatedb_init(const char *root_path, int scan_interval_seconds)
{
	char db_path[2048];
	int rc;
	struct stat sb;
	time_t now;

	if (!root_path)
		return -1;

	/* Save root path for thread use */
	root_path_copy = strdup(root_path);
	if (!root_path_copy)
		return -1;

	scan_interval = scan_interval_seconds;
	
	/* Build path to .locate.db */
	snprintf(db_path, sizeof(db_path), "%s/.locate.db", root_path);

	/* Check if database file exists and how old it is */
	now = time(NULL);
	force_initial_scan = 0;
	int db_exists = 0;
	int db_is_stale = 0;
	
	if (stat(db_path, &sb) == 0) {
		/* Database exists - check if it's stale */
		db_exists = 1;
		time_t db_age = now - sb.st_mtime;
		if (scan_interval > 0 && db_age >= scan_interval) {
			/* Database is older than scan interval - mark for initial scan */
			db_is_stale = 1;
			force_initial_scan = 1;
			last_scan_time = now - scan_interval;  /* Ensure thread scans immediately */
			LOG("Using existing locate database (age: %ld seconds, stale - will rescan)\n", db_age);
		} else {
			/* Database is fresh */
			last_scan_time = now;
			LOG("Using existing locate database (age: %ld seconds, fresh)\n", db_age);
		}
	} else {
		/* Database doesn't exist - will need initial scan */
		if (scan_interval > 0) {
			force_initial_scan = 1;
			last_scan_time = now - scan_interval;  /* Ensure thread scans immediately */
		} else {
			last_scan_time = now;
		}
		LOG("Creating new locate database\n");
	}

	/* Open or create database */
	rc = sqlite3_open(db_path, &locate_db);
	if (rc != SQLITE_OK) {
		LOG("ERROR: Failed to open locate database at %s: %s\n", 
			db_path, sqlite3_errmsg(locate_db));
		sqlite3_close(locate_db);
		locate_db = NULL;
		return -1;
	}

	/* Initialize schema */
	rc = sqlite3_exec(locate_db, LOCATE_DB_SCHEMA, NULL, NULL, NULL);
	if (rc != SQLITE_OK) {
		LOG("ERROR: Failed to initialize locate database schema: %s\n",
			sqlite3_errmsg(locate_db));
		sqlite3_close(locate_db);
		locate_db = NULL;
		return -1;
	}

	/* Log information about previous scan if resuming */
	if (db_exists && db_is_stale) {
		int prev_files = 0, prev_dirs = 0;
		time_t prev_start = 0, prev_end = 0;
		if (locatedb_get_last_scan_stats(&prev_files, &prev_dirs, &prev_start, &prev_end) == 0) {
			LOG("Previous scan indexed %d files in %d directories (scan took %ld seconds)\n",
				prev_files, prev_dirs, prev_end - prev_start);
		}
	}

	/* Check if a scan was interrupted (scan_in_progress is still set in DB) */
	if (db_exists) {
		sqlite3_stmt *stmt;
		rc = sqlite3_prepare_v2(locate_db, 
			"SELECT scan_in_progress FROM scan_status WHERE id = 1",
			-1, &stmt, NULL);
		
		if (rc == SQLITE_OK && sqlite3_step(stmt) == SQLITE_ROW) {
			int in_progress = sqlite3_column_int(stmt, 0);
			if (in_progress) {
				/* Previous scan was interrupted - resume it instead of clearing */
				resume_scan = 1;
				force_initial_scan = 1;  /* Trigger scan thread to run immediately */
				LOG("Found incomplete scan - will resume it\n");
			}
		}
		sqlite3_finalize(stmt);
	}

	LOG("Initialized locate database at %s\n", db_path);

	/* Don't scan here - let the background thread handle it */
	return 0;
}

int locatedb_upsert_entry(const char *path, off_t size, time_t mtime, mode_t mode, bool is_dir)
{
	sqlite3_stmt *stmt;
	const char *sql = 
		"INSERT INTO files (path, size, mtime, mode, is_dir, updated_at) "
		"VALUES (?, ?, ?, ?, ?, CURRENT_TIMESTAMP) "
		"ON CONFLICT(path) DO UPDATE SET "
		"  size=excluded.size, "
		"  mtime=excluded.mtime, "
		"  mode=excluded.mode, "
		"  is_dir=excluded.is_dir, "
		"  updated_at=CURRENT_TIMESTAMP";
	int rc;

	if (!locate_db || !path)
		return -1;

	rc = sqlite3_prepare_v2(locate_db, sql, -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		LOG("ERROR: Failed to prepare SQL statement: %s\n", sqlite3_errmsg(locate_db));
		return -1;
	}

	sqlite3_bind_text(stmt, 1, path, -1, SQLITE_STATIC);
	sqlite3_bind_int64(stmt, 2, (sqlite3_int64)size);
	sqlite3_bind_int64(stmt, 3, (sqlite3_int64)mtime);
	sqlite3_bind_int(stmt, 4, (int)mode);
	sqlite3_bind_int(stmt, 5, is_dir ? 1 : 0);

	rc = sqlite3_step(stmt);
	if (rc != SQLITE_DONE) {
		LOG("ERROR: Failed to insert/update entry %s: %s\n", path, sqlite3_errmsg(locate_db));
		sqlite3_finalize(stmt);
		return -1;
	}

	sqlite3_finalize(stmt);
	return 0;
}

int locatedb_remove_entry(const char *path)
{
	sqlite3_stmt *stmt;
	const char *sql = "DELETE FROM files WHERE path = ?";
	int rc;

	if (!locate_db || !path)
		return -1;

	rc = sqlite3_prepare_v2(locate_db, sql, -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		LOG("ERROR: Failed to prepare delete statement: %s\n", sqlite3_errmsg(locate_db));
		return -1;
	}

	sqlite3_bind_text(stmt, 1, path, -1, SQLITE_STATIC);

	rc = sqlite3_step(stmt);
	if (rc != SQLITE_DONE) {
		LOG("ERROR: Failed to delete entry %s: %s\n", path, sqlite3_errmsg(locate_db));
		sqlite3_finalize(stmt);
		return -1;
	}

	sqlite3_finalize(stmt);
	return 0;
}

/* Recursive function to scan directory and populate database */
static int _scan_directory_recursive(const char *root_path, const char *current_path, const char *rel_prefix)
{
	DIR *dir;
	struct dirent *entry;
	struct stat stat_buf;
	char full_path[4096];
	char rel_path[4096];
	int count = 0;

	dir = opendir(current_path);
	if (!dir)
		return 0;

	while ((entry = readdir(dir)) != NULL) {
		/* Check if scan should stop */
		if (!scan_thread_running)
			break;

		/* Skip . and .. */
		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
			continue;

		/* Skip the locate database itself */
		if (strcmp(entry->d_name, ".locate.db") == 0)
			continue;

		snprintf(full_path, sizeof(full_path), "%s/%s", current_path, entry->d_name);
		snprintf(rel_path, sizeof(rel_path), "%s%s%s", rel_prefix, 
			 (strlen(rel_prefix) > 0 && rel_prefix[strlen(rel_prefix)-1] != '/') ? "/" : "",
			 entry->d_name);

		if (stat(full_path, &stat_buf) != 0)
			continue;

		/* Add entry to database */
		locatedb_upsert_entry(rel_path, stat_buf.st_size, stat_buf.st_mtime, 
							  stat_buf.st_mode, S_ISDIR(stat_buf.st_mode));
		count++;
		files_scanned++;

		/* Log progress every 500 files */
		if (files_scanned % 500 == 0) {
			time_t elapsed = time(NULL) - scan_start_time;
			int rate = elapsed > 0 ? files_scanned / elapsed : 0;
			LOG("Scan progress: %d files, %d directories scanned (rate: %d/sec)\n", 
				files_scanned, directories_scanned, rate);
		}

		/* Recursively scan subdirectories */
		if (S_ISDIR(stat_buf.st_mode)) {
			directories_scanned++;
			count += _scan_directory_recursive(root_path, full_path, rel_path);
		}
	}

	closedir(dir);
	return count;
}

int locatedb_full_scan(const char *root_path)
{
	int count;

	if (!locate_db || !root_path)
		return -1;

	/* Initialize progress tracking */
	files_scanned = 0;
	directories_scanned = 0;
	scan_in_progress = 1;
	scan_start_time = time(NULL);

	if (resume_scan) {
		LOG("Resuming partial filesystem scan for locate database\n");
		resume_scan = 0;  /* Clear flag after use */
	} else {
		LOG("Starting full filesystem scan for locate database\n");
		/* Clear old entries only if not resuming */
		sqlite3_exec(locate_db, "DELETE FROM files", NULL, NULL, NULL);
	}

	/* Mark scan as in progress in database */
	sqlite3_exec(locate_db, "INSERT OR REPLACE INTO scan_status (id, scan_in_progress) VALUES (1, 1)", 
		NULL, NULL, NULL);

	/* Scan from root */
	count = _scan_directory_recursive(root_path, root_path, "");

	/* Save scan status if scan completed (not interrupted) */
	if (scan_thread_running || !scan_in_progress) {
		time_t now = time(NULL);
		char sql[512];
		snprintf(sql, sizeof(sql),
			"INSERT INTO scan_status (id, last_scan_start, last_scan_end, last_scan_files, last_scan_dirs, scan_in_progress) "
			"VALUES (1, %ld, %ld, %d, %d, 0) "
			"ON CONFLICT(id) DO UPDATE SET last_scan_end=%ld, last_scan_files=%d, last_scan_dirs=%d, scan_in_progress=0",
			scan_start_time, now, files_scanned, directories_scanned, now, files_scanned, directories_scanned);
		sqlite3_exec(locate_db, sql, NULL, NULL, NULL);
		LOG("Locate database scan complete: %d files, %d directories indexed in %ld seconds\n", 
			files_scanned, directories_scanned, now - scan_start_time);
		last_scan_time = now;
	} else {
		LOG("Locate database scan interrupted: %d files, %d directories indexed\n", 
			files_scanned, directories_scanned);
		/* Mark as interrupted but don't clear scan_in_progress flag */
	}

	scan_in_progress = 0;
	return count;
}

bool locatedb_check_periodic_scan(const char *root_path)
{
	time_t now;

	if (!locate_db || scan_interval <= 0 || !root_path)
		return false;

	now = time(NULL);
	if ((now - last_scan_time) >= scan_interval) {
		locatedb_full_scan(root_path);
		return true;
	}

	return false;
}

void locatedb_close(void)
{
	if (locate_db) {
		sqlite3_close(locate_db);
		locate_db = NULL;
	}
	
	if (root_path_copy) {
		free(root_path_copy);
		root_path_copy = NULL;
	}
}

int locatedb_get_stats(int *num_entries, long *db_size)
{
	sqlite3_stmt *stmt;
	struct stat stat_buf;
	int rc;

	if (!locate_db)
		return -1;

	/* Get entry count */
	rc = sqlite3_prepare_v2(locate_db, "SELECT COUNT(*) FROM files", -1, &stmt, NULL);
	if (rc != SQLITE_OK)
		return -1;

	if (sqlite3_step(stmt) == SQLITE_ROW && num_entries) {
		*num_entries = sqlite3_column_int(stmt, 0);
	}
	sqlite3_finalize(stmt);

	/* Get database file size */
	if (db_size) {
		/* Get the database filename from sqlite3 */
		const char *filename = sqlite3_db_filename(locate_db, "main");
		if (filename && stat(filename, &stat_buf) == 0) {
			*db_size = stat_buf.st_size;
		}
	}

	return 0;
}

int locatedb_start_scan_thread(const char *root_path)
{
	int rc;
	
	if (!locate_db || !root_path || scan_interval <= 0)
		return -1;
	
	if (scan_thread_running) {
		LOG("WARNING: Locate DB scan thread already running\n");
		return -1;
	}
	
	scan_thread_running = 1;
	rc = pthread_create(&scan_thread, NULL, _locate_scan_thread_func, (void *)root_path);
	
	if (rc != 0) {
		LOG("ERROR: Failed to create locate DB scan thread: %d\n", rc);
		scan_thread_running = 0;
		return -1;
	}
	
	LOG("Locate DB background scan thread started\n");
	return 0;
}

int locatedb_stop_scan_thread(void)
{
	int rc;
	
	if (!scan_thread_running) {
		return 0;  /* Already stopped */
	}
	
	/* Signal thread to stop */
	scan_thread_running = 0;
	
	/* Wait for thread to finish (with timeout) */
	rc = pthread_join(scan_thread, NULL);
	
	if (rc != 0) {
		LOG("ERROR: Failed to join locate DB scan thread: %d\n", rc);
		return -1;
	}
	
	LOG("Locate DB background scan thread stopped\n");
	return 0;
}

int locatedb_force_rescan(void)
{
	if (!locate_db || scan_interval <= 0)
		return -1;
	
	if (!scan_thread_running) {
		LOG("WARNING: Locate DB scan thread is not running\n");
		return -1;
	}
	
	/* Force scan by setting last_scan_time to far in the past */
	pthread_mutex_lock(&db_mutex);
	last_scan_time = 0;
	pthread_mutex_unlock(&db_mutex);
	
	LOG("Locate DB rescan requested\n");
	return 0;
}

int locatedb_get_scan_status(int *files_scanned_out, int *dirs_scanned_out, time_t *scan_start_out)
{
	if (files_scanned_out)
		*files_scanned_out = files_scanned;
	if (dirs_scanned_out)
		*dirs_scanned_out = directories_scanned;
	if (scan_start_out)
		*scan_start_out = scan_start_time;
	
	return scan_in_progress ? 0 : 1;
}

int locatedb_get_last_scan_stats(int *files, int *dirs, time_t *start_time, time_t *end_time)
{
	sqlite3_stmt *stmt;
	int rc;

	if (!locate_db)
		return -1;

	rc = sqlite3_prepare_v2(locate_db, 
		"SELECT last_scan_files, last_scan_dirs, last_scan_start, last_scan_end FROM scan_status WHERE id = 1",
		-1, &stmt, NULL);
	
	if (rc != SQLITE_OK)
		return -1;

	if (sqlite3_step(stmt) == SQLITE_ROW) {
		if (files)
			*files = sqlite3_column_int(stmt, 0);
		if (dirs)
			*dirs = sqlite3_column_int(stmt, 1);
		if (start_time)
			*start_time = (time_t)sqlite3_column_int64(stmt, 2);
		if (end_time)
			*end_time = (time_t)sqlite3_column_int64(stmt, 3);
		sqlite3_finalize(stmt);
		return 0;
	}

	sqlite3_finalize(stmt);
	return -1;
}

#else
/* Stub implementations when HAVE_SQLITE3 is not defined */

int locatedb_init(const char *root_path, int scan_interval_seconds) { return 0; }
int locatedb_upsert_entry(const char *path, off_t size, time_t mtime, mode_t mode, bool is_dir) { return 0; }
int locatedb_remove_entry(const char *path) { return 0; }
int locatedb_full_scan(const char *root_path) { return 0; }
bool locatedb_check_periodic_scan(const char *root_path) { return false; }
int locatedb_start_scan_thread(const char *root_path) { return 0; }
int locatedb_stop_scan_thread(void) { return 0; }
int locatedb_force_rescan(void) { return 0; }
int locatedb_get_scan_status(int *files_scanned_out, int *dirs_scanned_out, time_t *scan_start_out) { return 1; }
int locatedb_get_last_scan_stats(int *files, int *dirs, time_t *start_time, time_t *end_time) { return -1; }
void locatedb_close(void) { }
int locatedb_get_stats(int *num_entries, long *db_size) { return 0; }

#endif /* HAVE_SQLITE3 */
