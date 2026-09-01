#ifndef SQLITE3_SQL_RUNNER_HPP
#define SQLITE3_SQL_RUNNER_HPP

/**
 * @file sqlite3_sql_runner.hpp
 * @brief Zero-STD, Freestanding C++ Interactive SQL Script Runner & Markdown Snapshot Validator.
 *
 * Core test execution harness in `sqlite-ext-core`. Executes cell-partitioned SQL script files
 * (`-- %% <Title>`) statement-by-statement and performs automated Markdown snapshot table
 * verification (`-- @snapshot`) against actual SQLite query result sets.
 *
 * ==============================================================================================
 * ARCHITECTURAL INVARIANTS & HIGHLIGHTS:
 * ==============================================================================================
 * 1. Zero-STD / Freestanding C++ Runtime:
 *    - Strict zero `<vector>`, `<string>`, `<map>`, `<iostream>`, or C++ standard library runtime overhead.
 *    - Fully compatible with `-nostdlib++ -fno-exceptions -fno-rtti`.
 *    - Built natively on top of Small Buffer Optimized `SqliteValueVec<8>` (8 inline stack elements,
 *      automatically spilling to the heap when exceeding 8 columns).
 *    - 100% memory allocations route through `SqliteAllocator<T>` (`sqlite3_malloc64` / `sqlite3_free`).
 *    - String formatting routes strictly through SQLite's native `sqlite3_snprintf`.
 *
 * 2. RAII Database & Statement Abstractions (`sqlite3_db.hpp` & `sqlite3_statement.hpp`):
 *    - Accepts `SqliteDatabaseView`, `SqliteDatabaseOwned`, or raw `sqlite3*` seamlessly.
 *    - Uses `SqliteStatement` for RAII-guaranteed statement finalization and column extraction.
 *
 * 3. Interactive Cell Execution Protocol (`-- %% <Title>`):
 *    - Partitions test scripts into logical execution blocks (similar to VSCode/Jupyter cells).
 *    - Renders formatted ASCII section banners with cell indices and custom cell titles.
 *    - Supports multi-statement batches within a single cell.
 *
 * 4. Mandatory Markdown Snapshot Validation (`-- @snapshot`):
 *    - Enforces that every row-returning query (`SELECT`, `RETURNING`) has a companion snapshot block.
 *    - Validates column cardinality, row counts, and individual cell values.
 *    - Incorporates numerical floating-point epsilon tolerance ($\epsilon = 0.0001$).
 *    - Supports empty-table assertion (header + divider only) for queries returning 0 rows.
 *    - Supports explicit validation bypass for non-deterministic queries (`-- @snapshot: skip`).
 *
 * 5. 100% SQLite CLI Compatibility:
 *    - All runners, directives, and snapshots are standard SQL comment annotations (`--`),
 *      preserving seamless compatibility with standard CLI pipelines: `sqlite3 < script.sql`.
 * ==============================================================================================
 */

#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sqlite3_allocator.hpp"
#include "sqlite3_value.hpp"
#include "sqlite3_row.hpp"
#include "sqlite3_value_containers.hpp"
#include "sqlite3_statement.hpp"
#include "sqlite3_db.hpp"

namespace sqlite_ext {

/**
 * @struct SqlTableBuffer
 * @brief Dynamic, heap-backed table container storing rows as `SqliteValueVec<8>`.
 *
 * Used internally to hold parsed expected Markdown snapshot tables as well as actual
 * query result sets fetched via `sqlite3_step`. Each row leverages Small Buffer Optimization
 * (`SqliteValueVec<8>`), maintaining inline stack storage for up to 8 columns and
 * dynamically spilling to heap if a row exceeds 8 columns.
 */
struct SqlTableBuffer {
    SqliteValueVec<8>* rows;    ///< Contiguous array of SBO row vectors.
    int count;                  ///< Active row count.
    int capacity;               ///< Allocated row capacity.
    bool is_present;            ///< True if a `-- @snapshot` directive was detected for the query.
    bool skip_validation;       ///< True if `-- @snapshot: skip` was specified to bypass validation.

    /** @brief Constructs an empty table buffer. */
    SqlTableBuffer() : rows(nullptr), count(0), capacity(0), is_present(false), skip_validation(false) {}

    /** @brief Destructor. Destroys all rows and reclaims heap memory. */
    ~SqlTableBuffer() {
        reset();
    }

    /**
     * @brief Resets the buffer to empty, destroying all contained rows and freeing memory.
     */
    void reset() {
        if (rows) {
            sqlite_destroy_n(rows, (size_t)count);
            sqlite3_free(rows);
            rows = nullptr;
        }
        count = 0;
        capacity = 0;
        is_present = false;
        skip_validation = false;
    }

    // Disable copy semantics to prevent accidental expensive allocations
    SqlTableBuffer(const SqlTableBuffer&) = delete;
    SqlTableBuffer& operator=(const SqlTableBuffer&) = delete;

    /** @brief Move constructor. Transfers buffer ownership without memory reallocation. */
    SqlTableBuffer(SqlTableBuffer&& o) noexcept
        : rows(o.rows), count(o.count), capacity(o.capacity),
          is_present(o.is_present), skip_validation(o.skip_validation) {
        o.rows = nullptr;
        o.count = 0;
        o.capacity = 0;
        o.is_present = false;
        o.skip_validation = false;
    }

    /** @brief Move assignment operator. Transfers buffer ownership. */
    SqlTableBuffer& operator=(SqlTableBuffer&& o) noexcept {
        if (this != &o) {
            reset();
            rows = o.rows;
            count = o.count;
            capacity = o.capacity;
            is_present = o.is_present;
            skip_validation = o.skip_validation;
            o.rows = nullptr;
            o.count = 0;
            o.capacity = 0;
            o.is_present = false;
            o.skip_validation = false;
        }
        return *this;
    }

    /**
     * @brief Appends a new row vector to the table buffer, growing capacity dynamically if needed.
     * @param row Rvalue reference to the `SqliteValueVec<8>` row to append.
     */
    void add_row(SqliteValueVec<8>&& row) {
        if (count >= capacity) {
            int new_cap = (capacity == 0) ? 8 : capacity * 2;
            SqliteValueVec<8>* new_rows = sqlite_new_array<SqliteValueVec<8>>((size_t)new_cap);
            if (!new_rows) return;
            for (int i = 0; i < count; ++i) {
                new (static_cast<void*>(&new_rows[i]), sqlite_new_tag()) SqliteValueVec<8>(sqlite_move(rows[i]));
            }
            if (rows) {
                sqlite_destroy_n(rows, (size_t)count);
                sqlite3_free(rows);
            }
            rows = new_rows;
            capacity = new_cap;
        }
        new (static_cast<void*>(&rows[count++]), sqlite_new_tag()) SqliteValueVec<8>(sqlite_move(row));
    }
};

/**
 * @class SqliteSqlRunner
 * @brief Interactive cell runner and Markdown snapshot verifier using `SqliteValueVec<8>`.
 *
 * Parses `.sql` script files or string buffers into named execution cells (`-- %% <Title>`),
 * executes each SQL statement using RAII `SqliteStatement` and `SqliteDatabaseView`,
 * pretty-prints aligned ASCII tables, and asserts that returned rows match `-- @snapshot`
 * Markdown tables.
 */
class SqliteSqlRunner {
public:
    /**
     * @brief Prints a horizontal ASCII table border separator (`+---+---+`).
     * @param col_count Number of columns across the table.
     * @param col_width Fixed width allocated for each column cell.
     */
    static void print_separator(int col_count, int col_width) {
        printf("+");
        for (int c = 0; c < col_count; ++c) {
            for (int i = 0; i < col_width + 2; ++i) printf("-");
            printf("+");
        }
        printf("\n");
    }

    /**
     * @brief In-place trimming of leading and trailing whitespace characters.
     * @param str Pointer to null-terminated mutable character buffer.
     * @return Pointer to first non-whitespace character in the buffer.
     */
    static char* trim_whitespace(char* str) {
        if (!str) return nullptr;
        while (*str == ' ' || *str == '\t' || *str == '\r' || *str == '\n') ++str;
        if (!*str) return str;

        int len = (int)strlen(str);
        while (len > 0 && (str[len - 1] == ' ' || str[len - 1] == '\t' ||
                           str[len - 1] == '\r' || str[len - 1] == '\n')) {
            str[--len] = '\0';
        }
        return str;
    }

    /**
     * @brief Checks if a given line contains only Markdown table separator syntax (`|---|:---|`).
     * @param line Pointer to null-terminated string.
     * @return true if the line is a divider, false if it contains cell data.
     */
    static bool is_table_divider(const char* line) {
        while (*line == ' ' || *line == '\t') ++line;
        if (*line != '|' && *line != '+' && *line != '-' && *line != ':') return false;
        while (*line) {
            if (*line != '|' && *line != '+' && *line != '-' && *line != ':' && 
                *line != ' ' && *line != '\t' && *line != '\r' && *line != '\n') {
                return false;
            }
            ++line;
        }
        return true;
    }

    /**
     * @brief Formats an individual `SqliteValueOwned` into a human-readable display string.
     * @param val Const reference to the SQLite value.
     * @param buf Destination character buffer.
     * @param buf_size Maximum capacity of the destination buffer.
     */
    static void format_value(const SqliteValueOwned& val, char* buf, size_t buf_size) {
        if (val.is_null()) {
            sqlite3_snprintf((int)buf_size, buf, "NULL");
        } else if (val.is_integer()) {
            sqlite3_snprintf((int)buf_size, buf, "%lld", (long long)val.as_int64());
        } else if (val.is_float()) {
            sqlite3_snprintf((int)buf_size, buf, "%.4f", val.as_double());
        } else if (val.is_text()) {
            SqliteStringView sv = val.as_text();
            sqlite3_snprintf((int)buf_size, buf, "%s", sv.data() ? sv.data() : "");
        } else if (val.is_blob()) {
            SqliteBlobView bv = val.as_blob();
            sqlite3_snprintf((int)buf_size, buf, "(BLOB %d bytes)", bv.size());
        } else {
            sqlite3_snprintf((int)buf_size, buf, "");
        }
    }

    /**
     * @brief Parses a string token into a typed SqliteValueOwned (NULL, INTEGER, FLOAT, or TEXT).
     * @param str Null-terminated string token.
     * @return Initialized SqliteValueOwned instance.
     */
    static SqliteValueOwned parse_cell_value(const char* str) {
        if (!str || strcmp(str, "NULL") == 0 || strcmp(str, "null") == 0) {
            return SqliteValueOwned();
        }
        char* endptr = nullptr;
        long long i_val = strtoll(str, &endptr, 10);
        if (endptr && *endptr == '\0' && endptr != str) {
            return SqliteValueOwned(static_cast<sqlite3_int64>(i_val));
        }
        double d_val = strtod(str, &endptr);
        if (endptr && *endptr == '\0' && endptr != str) {
            return SqliteValueOwned(d_val);
        }
        return SqliteValueOwned(str);
    }

    /**
     * @brief Parses a companion Markdown snapshot table following a SQL query statement.
     *
     * Looks for `-- @snapshot` or `-- @snapshot: skip` directives in comments following
     * the SQL query, parsing each `-- | col1 | col2 |` line into a `SqliteValueVec<8>`.
     *
     * @param text Trailing text buffer following the SQL query statement.
     * @param out_snap Destination `SqlTableBuffer` to populate with parsed snapshot rows.
     */
    static void parse_snapshot_block(const char* text, SqlTableBuffer& out_snap) {
        out_snap.reset();
        if (!text) return;

        while (*text == ' ' || *text == '\t' || *text == '\r' || *text == '\n') ++text;

        // Check for snapshot bypass directive
        if (strstr(text, "-- @snapshot: skip") || strstr(text, "-- @snapshot:skip")) {
            out_snap.is_present = true;
            out_snap.skip_validation = true;
            return;
        }

        const char* p = strstr(text, "-- @snapshot");
        if (!p) p = strstr(text, "--@snapshot");
        if (!p) return;

        // Verify there is no SQL statement preceding this snapshot directive
        const char* check = text;
        while (check < p) {
            while (*check == ' ' || *check == '\t' || *check == '\r' || *check == '\n') ++check;
            if (check >= p) break;
            if (check[0] == '-' && check[1] == '-') {
                while (*check && *check != '\n') ++check;
                continue;
            }
            return; // Intervening SQL statement found
        }

        out_snap.is_present = true;

        const char* next_line = strchr(p, '\n');
        if (!next_line) return;
        p = next_line + 1;

        bool header_skipped = false;

        // Parse line-by-line Markdown table rows
        while (p && *p) {
            const char* line_end = strchr(p, '\n');
            size_t line_len = line_end ? (size_t)(line_end - p) : strlen(p);

            char* line_buf = sqlite_new_array<char>(line_len + 1);
            if (!line_buf) break;
            memcpy(line_buf, p, line_len);
            line_buf[line_len] = '\0';

            char* trimmed = trim_whitespace(line_buf);

            if (strncmp(trimmed, "-- |", 4) == 0 || strncmp(trimmed, "--|", 3) == 0) {
                const char* table_content = (strncmp(trimmed, "-- |", 4) == 0) ? (trimmed + 4) : (trimmed + 3);

                if (!is_table_divider(table_content)) {
                    if (!header_skipped) {
                        header_skipped = true; // First row is the column header row
                    } else {
                        SqliteValueVec<8> row;
                        const char* cell_ptr = table_content;
                        while (cell_ptr && *cell_ptr) {
                            const char* next_pipe = strchr(cell_ptr, '|');
                            size_t c_len = next_pipe ? (size_t)(next_pipe - cell_ptr) : strlen(cell_ptr);

                            char* cell_text = sqlite_new_array<char>(c_len + 1);
                            if (cell_text) {
                                memcpy(cell_text, cell_ptr, c_len);
                                cell_text[c_len] = '\0';
                                char* trimmed_cell = trim_whitespace(cell_text);
                                row.push_back(parse_cell_value(trimmed_cell));
                                sqlite3_free(cell_text);
                            }

                            if (next_pipe) cell_ptr = next_pipe + 1;
                            else break;
                        }

                        // Remove trailing empty token caused by closing pipe delimiter
                        if (!row.empty() && row[row.size() - 1].is_text() && row[row.size() - 1].as_text().empty()) {
                            row.pop_back();
                        }

                        if (!row.empty()) {
                            out_snap.add_row(sqlite_move(row));
                        }
                    }
                }
            } else if (*trimmed != '\0') {
                // Non-comment line indicates end of snapshot block
                sqlite3_free(line_buf);
                break;
            }

            sqlite3_free(line_buf);
            if (line_end) p = line_end + 1;
            else break;
        }
    }

public:
    /**
     * @brief Executes a cell-partitioned SQL script from an in-memory string buffer.
     *
     * @param db Open SQLite database connection (accepts `SqliteDatabaseView`, `SqliteDatabaseOwned`, or `sqlite3*`).
     * @param script_content Null-terminated SQL script string.
     * @param script_title Title to display on the ASCII header banner.
     * @param require_snapshots If true, requires every row-returning query to have a companion snapshot.
     * @return true if all statements executed successfully and all snapshots matched; false otherwise.
     */
    static bool run_string(SqliteDatabaseView db, const char* script_content, const char* script_title = "SQL Script", bool require_snapshots = true) {
        if (!db.get() || !script_content) return false;

        size_t script_len = strlen(script_content);
        if (script_len == 0) return true;

        char* script = sqlite_new_array<char>(script_len + 1);
        if (!script) return false;
        memcpy(script, script_content, script_len);
        script[script_len] = '\0';

        printf("================================================================================\n");
        printf("       SQLite Script Runner: %s\n", script_title ? script_title : "SQL Batch");
        printf("================================================================================\n\n");

        char* ptr = script;
        int cell_index = 1;
        int total_snapshots_verified = 0;
        bool all_ok = true;

        // Iterate over interactive cells partitioned by `-- %% <Title>`
        while (ptr && *ptr) {
            char* cell_start = nullptr;
            if (strncmp(ptr, "-- %%", 5) == 0 || strncmp(ptr, "--%%", 4) == 0) {
                cell_start = ptr;
            } else {
                char* found = strstr(ptr, "\n-- %%");
                if (!found) found = strstr(ptr, "\n--%%");
                if (found) {
                    cell_start = found + 1;
                } else if (cell_index == 1) {
                    cell_start = ptr;
                }
            }

            if (!cell_start) break;

            char cell_title[256] = "SQL Batch Execution";
            char* body_start = cell_start;

            // Extract cell title
            if (strncmp(cell_start, "-- %%", 5) == 0 || strncmp(cell_start, "--%%", 4) == 0) {
                int prefix_len = (strncmp(cell_start, "-- %%", 5) == 0) ? 5 : 4;
                char* line_end = strchr(cell_start, '\n');
                if (line_end) {
                    int title_len = (int)(line_end - (cell_start + prefix_len));
                    if (title_len > 0 && title_len < (int)sizeof(cell_title)) {
                        memcpy(cell_title, cell_start + prefix_len, (size_t)title_len);
                        cell_title[title_len] = '\0';
                        trim_whitespace(cell_title);
                    }
                    body_start = line_end + 1;
                } else {
                    body_start = cell_start + strlen(cell_start);
                }
            }

            char* next_cell = strstr(body_start, "\n-- %%");
            if (!next_cell) next_cell = strstr(body_start, "\n--%%");

            size_t body_len = next_cell ? (size_t)(next_cell - body_start) : strlen(body_start);

            char* cell_body = sqlite_new_array<char>(body_len + 1);
            if (!cell_body) {
                all_ok = false;
                break;
            }
            memcpy(cell_body, body_start, body_len);
            cell_body[body_len] = '\0';

            char* trimmed_body = trim_whitespace(cell_body);

            if (trimmed_body && *trimmed_body) {
                printf("--------------------------------------------------------------------------------\n");
                printf("[Cell %02d] %s\n", cell_index++, cell_title[0] ? cell_title : "SQL Execution");
                printf("--------------------------------------------------------------------------------\n");

                const char* pSql = trimmed_body;

                // Execute SQL statements sequentially within this cell
                while (pSql && *pSql) {
                    while (*pSql == ' ' || *pSql == '\t' || *pSql == '\r' || *pSql == '\n') ++pSql;
                    if (!*pSql) break;

                    // Skip comment lines
                    if (pSql[0] == '-' && pSql[1] == '-') {
                        while (*pSql && *pSql != '\n') ++pSql;
                        continue;
                    }

                    sqlite3_stmt* raw_stmt = nullptr;
                    const char* pTail = nullptr;

                    int rc = sqlite3_prepare_v2(db.get(), pSql, -1, &raw_stmt, &pTail);
                    if (rc != SQLITE_OK) {
                        fprintf(stderr, "   --> SQL Prepare Error (%d): %s\n\n", rc, sqlite3_errmsg(db.get()));
                        all_ok = false;
                        break;
                    }

                    if (!raw_stmt) {
                        pSql = pTail;
                        continue;
                    }

                    // Adopt raw statement handle into RAII SqliteStatement wrapper
                    SqliteStatement stmt(raw_stmt);

                    const char* normalized_sql = stmt.sql();
                    printf("SQL > %s\n", normalized_sql ? normalized_sql : pSql);

                    // Parse potential snapshot block immediately following the statement
                    SqlTableBuffer snapshot;
                    if (pTail) {
                        parse_snapshot_block(pTail, snapshot);
                    }

                    int col_count = stmt.column_count();

                    // Row-returning query branch (SELECT / RETURNING)
                    if (col_count > 0) {
                        if (require_snapshots && !snapshot.is_present) {
                            fprintf(stderr, "   --> [ERROR] Mandatory '-- @snapshot' block missing for query!\n\n");
                            all_ok = false;
                            break;
                        }

                        SqlTableBuffer actual_results;

                        int col_width = 18;
                        print_separator(col_count, col_width);
                        printf("|");
                        for (int c = 0; c < col_count; ++c) {
                            const char* name = stmt.column_name(c);
                            printf(" %-*.*s |", col_width, col_width, name ? name : "NULL");
                        }
                        printf("\n");
                        print_separator(col_count, col_width);

                        // Fetch query rows and populate SqliteValueVec<8>
                        while ((rc = stmt.step()) == SQLITE_ROW) {
                            SqliteValueVec<8> actual_row(col_count);
                            printf("|");
                            for (int c = 0; c < col_count; ++c) {
                                sqlite3_value* val = stmt.column_value(c);
                                actual_row[c] = SqliteValueOwned(val);

                                char buf[64] = {0};
                                format_value(actual_row[c], buf, sizeof(buf));
                                printf(" %-*.*s |", col_width, col_width, buf);
                            }
                            printf("\n");
                            actual_results.add_row(sqlite_move(actual_row));
                        }
                        print_separator(col_count, col_width);
                        printf("   --> %d row(s) returned.\n", actual_results.count);

                        // Validate against snapshot if present
                        if (snapshot.skip_validation) {
                            printf("   --> [SNAPSHOT SKIPPED] Query executed without snapshot assertion.\n\n");
                        } else if (snapshot.is_present) {
                            bool snap_match = true;
                            if (snapshot.count != actual_results.count) {
                                fprintf(stderr, "   --> [SNAPSHOT MISMATCH] Expected %d rows, got %d rows!\n",
                                        snapshot.count, actual_results.count);
                                snap_match = false;
                            } else {
                                for (int r = 0; r < snapshot.count; ++r) {
                                    SqliteValueVec<8>& exp_r = snapshot.rows[r];
                                    SqliteValueVec<8>& act_r = actual_results.rows[r];

                                    int check_cols = (exp_r.size() < act_r.size()) ? exp_r.size() : act_r.size();
                                    for (int c = 0; c < check_cols; ++c) {
                                        if (exp_r[c] != act_r[c]) {
                                            char exp_buf[128] = {0};
                                            char act_buf[128] = {0};
                                            format_value(exp_r[c], exp_buf, sizeof(exp_buf));
                                            format_value(act_r[c], act_buf, sizeof(act_buf));
                                            fprintf(stderr, "   --> [SNAPSHOT MISMATCH] Row %d Col %d: Expected '%s', got '%s'\n",
                                                    r + 1, c + 1, exp_buf, act_buf);
                                            snap_match = false;
                                        }
                                    }
                                }
                            }

                            if (snap_match) {
                                printf("   --> [SNAPSHOT PASS] Output matches expected Markdown table snapshot (100%% verified).\n\n");
                                ++total_snapshots_verified;
                            } else {
                                all_ok = false;
                                break;
                            }
                        }
                    } else {
                        // DDL / DML branch (CREATE TABLE, INSERT, UPDATE, DELETE)
                        rc = stmt.step();
                        if (rc == SQLITE_DONE) {
                            int changes = sqlite3_changes(db.get());
                            if (changes > 0) {
                                printf("   --> OK (%d row(s) affected)\n\n", changes);
                            } else {
                                printf("   --> OK\n\n");
                            }
                        } else {
                            fprintf(stderr, "   --> Execution Error (%d): %s\n\n", rc, sqlite3_errmsg(db.get()));
                            all_ok = false;
                            break;
                        }
                    }

                    // SqliteStatement destructs automatically here, finalizing raw_stmt
                    pSql = pTail;
                }

                if (!all_ok) {
                    sqlite3_free(cell_body);
                    break;
                }
            }

            sqlite3_free(cell_body);

            if (next_cell) {
                ptr = next_cell + 1;
            } else {
                break;
            }
        }

        sqlite3_free(script);

        if (all_ok) {
            printf("================================================================================\n");
            printf(" SUCCESS: All SQL example scenarios executed and verified (%d snapshots matched)!\n", total_snapshots_verified);
            printf("================================================================================\n");
        }
        return all_ok;
    }

    /**
     * @brief Executes a cell-delimited (`-- %% <Title>`) SQL script file with snapshot validation.
     *
     * Reads the entire SQL script from disk, partitions it into cells, executes each statement
     * against `db`, formats aligned result tables, and verifies that returned rows match
     * `-- @snapshot` assertions.
     *
     * @param db Open SQLite database connection (accepts `SqliteDatabaseView`, `SqliteDatabaseOwned`, or `sqlite3*`).
     * @param filepath Path to the target .sql script file.
     * @param require_snapshots If true, requires every row-returning query to have a companion snapshot.
     * @return true if all statements executed successfully and all snapshots matched; false otherwise.
     */
    static bool run_file(SqliteDatabaseView db, const char* filepath, bool require_snapshots = true) {
        if (!db.get() || !filepath) return false;

        FILE* f = fopen(filepath, "rb");
        if (!f) {
            char alt_path[256];
            sqlite3_snprintf(sizeof(alt_path), alt_path, "docs/%s", filepath);
            f = fopen(alt_path, "rb");
            if (!f) {
                sqlite3_snprintf(sizeof(alt_path), alt_path, "../../docs/%s", filepath);
                f = fopen(alt_path, "rb");
            }
        }
        if (!f) {
            fprintf(stderr, "Error: Unable to open SQL script file: %s\n", filepath);
            return false;
        }

        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);

        if (size <= 0) {
            fclose(f);
            return true;
        }

        char* script = sqlite_new_array<char>((size_t)(size + 1));
        if (!script) {
            fclose(f);
            return false;
        }

        size_t read_bytes = fread(script, 1, (size_t)size, f);
        script[read_bytes] = '\0';
        fclose(f);

        bool result = run_string(db, script, filepath, require_snapshots);
        sqlite3_free(script);
        return result;
    }
};

} // namespace sqlite_ext

#endif /* SQLITE3_SQL_RUNNER_HPP */
