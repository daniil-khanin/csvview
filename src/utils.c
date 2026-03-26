#include "utils.h"
#include <time.h>

// Parse a double, accepting both dot and comma as decimal separator.
double parse_double(const char *s, char **endptr)
{
    if (!s) { if (endptr) *endptr = (char *)s; return 0.0; }

    // Copy and replace comma→dot
    char buf[64];
    strncpy(buf, s, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    for (char *p = buf; *p; p++) {
        if (*p == ',') *p = '.';
    }

    char *ep;
    double result = strtod(buf, &ep);
    // Map endptr back into the original string
    if (endptr) *endptr = (char *)s + (ep - buf);
    return result;
}

/**
 * @brief Converts a column number (0-based) to an Excel-style letter designation
 *
 * Converts an integer column index (starting from 0) to a string of the form:
 *   0  → "A"
 *   1  → "B"
 *   25 → "Z"
 *   26 → "AA"
 *   702 → "AAA"  etc.
 *
 * @param col       Column number (0 = A, 1 = B, ..., 25 = Z, 26 = AA, ...)
 *                  If col < 0 — the function simply clears the buffer and returns
 * @param buf       Pointer to the buffer where the result will be written (null-terminated string)
 *                  The buffer must be large enough (recommended ≥ 16 bytes)
 *
 * @note
 *   - The function writes the result as an uppercase string
 *   - The maximum supported column number depends on the buffer size
 *     (with buf[16] — up to "XFD" ≈ column 16383, as in Excel 2007+)
 *   - If a negative number is passed — the buffer is cleared (empty string)
 *
 * @example
 *   char buf[16];
 *   col_letter(0, buf);   // buf → "A"
 *   col_letter(25, buf);  // buf → "Z"
 *   col_letter(26, buf);  // buf → "AA"
 *   col_letter(701, buf); // buf → "ZZ"
 *   col_letter(702, buf); // buf → "AAA"
 *
 * @warning The buffer must be initialised and at least 8–16 bytes in size
 *          depending on how large the column numbers you expect are.
 */
void col_letter(int col, char *buf)
{
    buf[0] = '\0';
    if (col < 0) return;

    char temp[16];
    int i = 0;

    // Convert the number to base-26, but with the Excel quirk:
    // there is no "zero" digit, so we do col / 26 - 1
    do {
        temp[i++] = 'A' + (col % 26);
        col = col / 26 - 1;
    } while (col >= 0);

    // Reverse the result (since we collected characters from the least significant digit)
    int j = 0;
    while (i > 0) {
        buf[j++] = temp[--i];
    }
    buf[j] = '\0';
}

/**
 * @brief Converts an Excel-style column letter designation to its numeric index (0-based)
 *
 * Converts a string such as "A", "B", "Z", "AA", "AB", "ZZ", "AAA", etc. to a column number,
 * where:
 *   "A"   → 0
 *   "B"   → 1
 *   "Z"   → 25
 *   "AA"  → 26
 *   "AB"  → 27
 *   "ZZ"  → 701
 *   "AAA" → 702
 *
 * @param label     Pointer to a string with the column designation (e.g. "AB", "ZZ")
 *                  Only uppercase letters A–Z are accepted
 *                  Empty string or NULL → returns -1
 *
 * @return
 *   ≥ 0    — successful conversion, column number (0-based)
 *   -1     — error: invalid format, empty string, or contains disallowed characters
 *
 * @note
 *   - The function strictly checks that the string consists ONLY of uppercase letters A–Z
 *   - The algorithm matches the column numbering used by Microsoft Excel / Google Sheets
 *   - The maximum allowed value depends on the string length and the size of int
 *     (with a 32-bit int typically up to ~7 letters ≈ column 8 billion)
 *
 * @example
 *   col_to_num("A")    // → 0
 *   col_to_num("Z")    // → 25
 *   col_to_num("AA")   // → 26
 *   col_to_num("AZ")   // → 51
 *   col_to_num("ZZ")   // → 701
 *   col_to_num("AAA")  // → 702
 *   col_to_num("abc")  // → -1 (lowercase letters)
 *   col_to_num("A1")   // → -1 (digits not allowed)
 *   col_to_num("")     // → -1
 *   col_to_num(NULL)   // → -1
 *
 * @see col_letter() — inverse function (number → letters)
 */
int col_to_num(const char *label)
{
    // Guard against NULL or empty string
    if (!label || !*label) {
        return -1;
    }

    int num = 0;

    // Iterate over each character of the string
    while (*label) {
        // Check that the character is strictly an uppercase letter A–Z
        if (*label < 'A' || *label > 'Z') {
            return -1;
        }

        // Convert to base-26
        // ('A' = 1, 'B' = 2, ..., 'Z' = 26)
        num = num * 26 + (*label - 'A' + 1);
        label++;
    }

    // Excel numbering starts at 1 inside the base-26 system,
    // so subtract 1 to get a 0-based index
    return num - 1;
}

/**
 * @brief Finds a column number (0-based) by its text name (header)
 *
 * Searches among all known column names (from the CSV file header)
 * and returns the index of the first column whose name matches the given string.
 * The comparison is case-sensitive.
 *
 * @param name      Pointer to a string with the column name (e.g. "Date", "Revenue", "User_ID")
 *                  If name == NULL or the string is empty — the function returns -1
 *
 * @return
 *   ≥ 0    — column index (0-based) whose name matches
 *   -1     — no column with that name was found (or name == NULL / empty string)
 *
 * @note
 *   - The function only works if column headers have been successfully read
 *     (i.e. the column_names[] array is populated and col_count > 0)
 *   - The comparison is strict: strcmp() → distinguishes "revenue" from "Revenue"
 *   - If the table has multiple columns with the same name — the first match is returned
 *   - Used in filters, :cf, :cal, :fq commands and other places where a column must be
 *     referenced by its human-readable name rather than a letter (A/B/C)
 *
 * @example
 *   // Assume headers: "ID", "Name", "Price", "Date"
 *   col_name_to_num("Price")   // → 2
 *   col_name_to_num("DATE")    // → -1 (case mismatch)
 *   col_name_to_num("id")      // → -1 (case mismatch)
 *   col_name_to_num("Unknown") // → -1
 *   col_name_to_num("")        // → -1
 *   col_name_to_num(NULL)      // → -1
 *
 * @see
 *   - col_to_num()     — convert letter designation "AA" → number
 *   - get_column_value() — get a cell value by column name
 */
int col_name_to_num(const char *name)
{
    // Fast guard against invalid input
    if (!name || !*name) {
        return -1;
    }

    // Linear search through the column name array
    for (int c = 0; c < col_count; c++) {
        // Check that the column name exists and matches
        if (column_names[c] && strcmp(column_names[c], name) == 0) {
            return c;  // Found — return index (0-based)
        }
    }

    // No match found
    return -1;
}

/**
 * @brief Saves the current state of the table (all rows) to the specified CSV file
 *
 * Creates a temporary file, writes all rows from the `rows` index into it,
 * then atomically replaces the original file with the new one using rename().
 *
 * If a row has a cache (`line_cache`), that is used.
 * If there is no cache — the row is read from the original file at the stored `offset`.
 *
 * @param filename      Full path to the target CSV file (where we save)
 * @param orig_f        Open FILE* of the source file (needed to read rows that do not
 *                      yet have a line_cache). Must be opened in "r" mode.
 * @param rows          Array of row indices (RowIndex), containing either a cached row
 *                      or an offset into the source file
 * @param row_count     Number of rows in the rows array to save
 *                      (usually equals the global variable row_count)
 *
 * @return
 *    0    — success: file was successfully rewritten
 *   -1    — error:
 *           • could not create/open the temporary file
 *           • read error from orig_f (fseek/fgets)
 *           • could not rename the temporary file to the target (rename)
 *
 * @note
 *   - The function is **atomic** at the filesystem level thanks to rename()
 *     (either the old file remains, or the new one appears immediately)
 *   - All rows are written with a trailing '\n'
 *   - If a row was empty or could not be read → an empty string + '\n' is written
 *   - The original file is **not closed** inside the function — that is the caller's responsibility
 *   - Used when:
 *     • deleting rows (:dr)
 *     • renaming a column (:cr)
 *     • modifying column content (:cf)
 *     • adding a column (:cal / :car)
 *
 * @example
 *    // Example: save all rows after deleting one
 *    if (save_file("data.csv", original_file, rows, new_row_count) == 0) {
 *        printf("File saved successfully\n");
 *    } else {
 *        fprintf(stderr, "Save failed\n");
 *    }
 *
 * @warning
 *   - If orig_f is corrupt or offsets in rows are invalid — some rows may be lost
 *   - Does not check whether the directory exists or whether write permission is granted
 *   - On rename() failure the temporary file (.tmp) remains on disk
 *     (it is recommended to delete it in the calling code on error)
 *
 * @see
 *   - load_saved_filters(), save_filter() — similar logic for working with config
 *   - apply_filter(), build_sorted_index() — often call save_file after changes
 */
int save_file(const char *filename, FILE *orig_f, RowIndex *rows, int row_count)
{
    char temp_name[1024];
    snprintf(temp_name, sizeof(temp_name), "%s.tmp", filename);

    FILE *out = fopen(temp_name, "w");
    if (!out) {
        return -1;
    }

    for (int r = 0; r < row_count; r++)
    {
        /* buf is declared here — valid for the whole loop, line will not become a dangling pointer */
        char buf[MAX_LINE_LEN];
        const char *line = rows[r].line_cache ? rows[r].line_cache : "";

        // If there is no cache — read the row from the original file
        if (!rows[r].line_cache)
        {
            if (fseek(orig_f, rows[r].offset, SEEK_SET) == 0)
            {
                if (fgets(buf, sizeof(buf), orig_f))
                {
                    // Strip \r\n (fprintf below will add \n itself)
                    buf[strcspn(buf, "\r\n")] = '\0';
                    line = buf;
                }
            }
        }

        // Write the row + newline
        fprintf(out, "%s\n", line);
    }

    fclose(out);

    // Atomically replace the original file
    if (rename(temp_name, filename) == 0) {
        return 0;
    }

    // If rename failed — the temporary file remains
    return -1;
}

/**
 * @brief Case-insensitive substring search (portable equivalent of strcasestr)
 *
 * Searches for the first occurrence of substring `needle` in string `haystack`,
 * ignoring character case.
 * The comparison uses `strncasecmp` (POSIX), so it works on most UNIX-like systems
 * and is compatible with most compilers.
 *
 * @param haystack  Pointer to the string to search in (may be NULL)
 * @param needle    Pointer to the substring to search for
 *                  If needle == NULL or needle is an empty string → haystack is returned
 *
 * @return
 *   - Pointer to the first occurrence of the substring in haystack (case-insensitive)
 *   - NULL if the substring is not found
 *   - haystack itself if needle is an empty string or NULL
 *
 * @note
 *   - The function does **not** modify the input strings
 *   - The returned pointer points inside haystack (no free needed)
 *   - Comparison is character-by-character using strncasecmp → depends on the current locale
 *   - Complexity: O(n·m) in the worst case (where n = strlen(haystack), m = strlen(needle))
 *   - This is a custom implementation because strcasestr is not available everywhere
 *     (it exists in glibc, but not in some BSDs or Windows MSVC by default)
 *
 * @example
 *   const char *text = "Hello World, hello again!";
 *   char *pos;
 *
 *   pos = strcasestr_custom(text, "hello");     // → pointer to "hello" (second occurrence)
 *   pos = strcasestr_custom(text, "WORLD");     // → pointer to "World"
 *   pos = strcasestr_custom(text, "");          // → text (start of string)
 *   pos = strcasestr_custom(text, "python");    // → NULL
 *   pos = strcasestr_custom(NULL, "test");      // → NULL (haystack is NULL)
 *   pos = strcasestr_custom("AbC", NULL);       // → "AbC" (needle is empty)
 *
 * @see
 *   - strcasestr()   — POSIX extension (not available everywhere)
 *   - strstr()       — case-sensitive equivalent
 *   - strncasecmp()  — used internally for comparison
 */
char *strcasestr_custom(const char *haystack, const char *needle)
{
    // Special case: empty or NULL needle → return start of haystack
    if (!needle || !*needle) {
        return (char *)haystack;
    }

    size_t nlen = strlen(needle);
    const char *p = haystack;

    // Search character by character, comparing a substring of length nlen
    while (*p) {
        // Case-insensitive comparison of the first nlen characters
        if (strncasecmp(p, needle, nlen) == 0) {
            return (char *)p;  // Found — return pointer to the start of the match
        }
        p++;
    }

    // No occurrence found
    return NULL;
}

/**
 * @brief Removes leading and trailing whitespace characters from a string (in-place)
 *
 * Modifies the given string by stripping all whitespace characters (space, tab, newline, etc.)
 * from both ends. The result is written back into the same memory; a pointer to the start
 * of the trimmed string is returned (it may be shifted to the right).
 *
 * @param str    Pointer to the string to trim (null-terminated)
 *               If str == NULL or the string is empty — returned unchanged
 *
 * @return
 *   - Pointer to the start of the trimmed string (often the same as str, but may be shifted)
 *   - str itself if the input was NULL or an empty string
 *
 * @note
 *   - The function works **in-place** — it modifies the content of the original buffer
 *   - Uses isspace() → removes all characters considered whitespace in the current locale
 *     (space ' ', tab '\t', newline '\n', carriage return '\r', vertical tab, etc.)
 *   - If the string becomes empty after trimming — a pointer to '\0' is returned
 *   - Safe for empty strings and strings consisting entirely of whitespace
 *   - Does NOT allocate new memory — do NOT free() the returned pointer
 *
 * @example
 *   char s1[] = "   Hello World   ";
 *   trim(s1);               // s1 → "Hello World\0"   (returns &s1[3])
 *
 *   char s2[] = "   \t\n  ";
 *   trim(s2);               // s2 → "\0"   (empty string)
 *
 *   char s3[] = "NoSpacesHere";
 *   trim(s3);               // s3 remains "NoSpacesHere"
 *
 *   char *s4 = NULL;
 *   trim(s4);               // returns NULL
 *
 *   char s5[] = "";
 *   trim(s5);               // returns s5 (pointer to '\0')
 *
 * @warning
 *   - The string must be in writable memory (not a string literal "const char*")
 *     Otherwise → segmentation fault on write attempt
 *     Example of bad usage:
 *       char *bad = "   test   ";   // string literal, usually in read-only memory
 *       trim(bad);                  // → UB (undefined behavior)
 *   - For very long strings — strlen() is called once, but it is still O(n)
 *
 * @see
 *   - Equivalent trim implementations in other languages (Python .strip(), Java .trim())
 *   - ltrim(), rtrim() — if only leading or only trailing removal is needed
 */
char *trim(char *str)
{
    // If NULL or already an empty string — return as-is
    if (!str || !*str) {
        return str;
    }

    // Strip spaces (and other isspace characters) from the left
    while (isspace((unsigned char)*str)) {
        str++;
    }

    // If after stripping from the left the string is empty — return pointer to '\0'
    if (*str == '\0') {
        return str;
    }

    // Find the end of the string
    char *end = str + strlen(str) - 1;

    // Strip spaces from the right, moving left
    while (end > str && isspace((unsigned char)*end)) {
        end--;
    }

    // Place the null terminator immediately after the last non-whitespace character
    *(end + 1) = '\0';

    // Return a pointer to the start of the trimmed string
    return str;
}

/**
 * @brief Parses a filter string into a structured expression (FilterExpr)
 *
 * Parses a user filter query of the form:
 *   [ ! ] column1 >= 100 AND column2 = "Active" OR column3 != "Blocked"
 *
 * Supports:
 *   - Negation of the entire expression via ! at the beginning
 *   - Operators: =, !=, >, >=, <, <=
 *   - Logical connectives: AND, OR (case-insensitive)
 *   - Quoted values (quotes are stripped automatically)
 *   - Any reasonable amount of whitespace
 *
 * @param query     Pointer to the filter query string (e.g. "Price > 500 AND Status = Active")
 *                  May be NULL or empty — returns -1 in that case
 * @param expr      Pointer to the FilterExpr structure where the parsed result will be stored
 *                  Fields expr->conditions and expr->logic_ops are allocated dynamically inside
 *                  (they must be freed via free_filter_expr() after use!)
 *
 * @return
 *    0    — success: expression successfully parsed, expr is populated
 *   -1    — error:
 *           • query == NULL or empty string
 *           • memory allocation failed (strdup/malloc)
 *           • no valid condition was found
 *           • syntax error (missing operator, invalid column name, etc.)
 *
 * @note
 *   - Allocates memory for column names and values (strdup) — must free via free_filter_expr(expr)
 *   - Supports up to 64 conditions (limit of temp_conds[64])
 *   - Values are automatically detected as numeric (strtod) when possible
 *   - Quotes (" or ') around values are stripped automatically
 *   - AND/OR logical operators must be separated by spaces (or end of string)
 *   - Negation (!) applies to the entire expression, not to individual conditions
 *
 * @example
 *   FilterExpr expr = {0};
 *   int res = parse_filter_expression("Age >= 18 AND City = \"Moscow\" OR Status != Blocked", &expr);
 *   // res == 0 → expr contains 3 conditions, 2 operators (AND, OR)
 *
 *   parse_filter_expression("! Price < 100", &expr);
 *   // → expr->negated == 1, one condition Price < 100
 *
 *   parse_filter_expression("Status = Active", &expr);
 *   // → one condition, negated == 0
 *
 *   parse_filter_expression("", &expr);           // → -1
 *   parse_filter_expression("Age >", &expr);      // → -1 (no value)
 *   parse_filter_expression("x = y AND", &expr);  // → -1 (incomplete expression)
 *
 * @warning
 *   - If the function returned 0 — you MUST call free_filter_expr(expr) after use,
 *     otherwise there will be a memory leak!
 *   - Does not support nested parentheses, operator precedence, or complex expressions —
 *     only a linear chain of AND/OR
 *   - Column names must not contain ><=! characters (spaces are stripped by trim)
 *
 * @see
 *   - free_filter_expr()    — mandatory cleanup after use
 *   - row_matches_filter()  — applies the parsed expression to a row
 *   - apply_filter()        — main consumer of this function's result
 */
int parse_filter_expression(const char *query, FilterExpr *expr)
{
    // Guard against empty or invalid input
    if (!query || !*query) {
        return -1;
    }

    // Make a working copy of the string so we can modify it
    char *input = strdup(query);
    if (!input) {
        return -1;
    }

    // Strip leading and trailing whitespace
    char *p = trim(input);

    // Bracket support: replace ( and ) with spaces before parsing.
    // With left-to-right evaluation, (A OR B) AND C correctly reduces
    // to A OR B AND C → same result.
    for (char *pp = p; *pp; pp++) {
        if (*pp == '(' || *pp == ')') *pp = ' ';
    }
    p = trim(p);

    // Check for negation of the entire expression
    expr->negated = 0;
    if (*p == '!') {
        expr->negated = 1;
        p = trim(p + 1);
    }

    Condition temp_conds[64];           // temporary condition array (max 64)
    LogicOperator temp_ops[63];         // operators between them (one fewer)
    int cond_count = 0;

    // Main parsing loop
    while (*p && cond_count < 64)
    {
        // Skip extra whitespace
        while (isspace(*p)) p++;
        if (!*p) break;

        // Check whether this is a logical operator (AND / OR)
        if (strncasecmp(p, "AND", 3) == 0 && (isspace(p[3]) || !p[3])) {
            if (cond_count > 0) temp_ops[cond_count-1] = LOGIC_AND;
            p += 3;
            continue;
        }
        if (strncasecmp(p, "OR", 2) == 0 && (isspace(p[2]) || !p[2])) {
            if (cond_count > 0) temp_ops[cond_count-1] = LOGIC_OR;
            p += 2;
            continue;
        }

        // Parse condition: column_name [spaces] operator [spaces] value
        char col_name[128] = {0};
        char *col_start = p;
        while (*p && !strchr("><=! \t", *p)) p++;
        int col_len = p - col_start;
        if (col_len >= (int)sizeof(col_name)) col_len = (int)sizeof(col_name)-1;
        strncpy(col_name, col_start, col_len);
        trim(col_name);

        if (!*col_name) break;  // empty column name — end of parsing

        // Skip whitespace before the operator
        while (isspace(*p)) p++;

        // Determine the comparison operator (check two-character ones first!)
        CompareOp op = -1;
        int op_len = 0;
        if      (p[0] == '>' && p[1] == '=') { op = OP_GE; op_len = 2; }
        else if (p[0] == '<' && p[1] == '=') { op = OP_LE; op_len = 2; }
        else if (p[0] == '!' && p[1] == '=') { op = OP_NE; op_len = 2; }
        else if (p[0] == '=' && p[1] == '=') { op = OP_EQ; op_len = 2; } // rare case
        else if (p[0] == '>')                { op = OP_GT; op_len = 1; }
        else if (p[0] == '<')                { op = OP_LT; op_len = 1; }
        else if (p[0] == '=')                { op = OP_EQ; op_len = 1; }

        if ((int)op == -1) {
            break;  // no operator — syntax error
        }

        p += op_len;

        // Skip whitespace before the value
        while (isspace(*p)) p++;

        // Read the value until the next AND/OR or end of string
        char val_buf[256] = {0};
        char *val_start = p;
        while (*p) {
            if (strncasecmp(p, " AND ", 5) == 0 || strncasecmp(p, " OR ", 4) == 0) break;
            p++;
        }
        int val_len = p - val_start;
        if (val_len >= (int)sizeof(val_buf)) val_len = (int)sizeof(val_buf)-1;
        strncpy(val_buf, val_start, val_len);
        trim(val_buf);

        // Strip quotes if present
        if ((val_buf[0] == '"' || val_buf[0] == '\'') &&
            val_buf[strlen(val_buf)-1] == val_buf[0])
        {
            memmove(val_buf, val_buf + 1, strlen(val_buf) - 2);
            val_buf[strlen(val_buf) - 2] = '\0';
        }

        // Populate the temporary condition structure
        temp_conds[cond_count].column       = strdup(col_name);
        temp_conds[cond_count].op           = op;
        temp_conds[cond_count].value        = strdup(val_buf);
        temp_conds[cond_count].value_is_num = 0;
        temp_conds[cond_count].value_num    = 0.0;

        // Try to parse the value as a number
        char *endptr;
        double num = parse_double(val_buf, &endptr);
        if (endptr != val_buf && *endptr == '\0') {
            temp_conds[cond_count].value_is_num = 1;
            temp_conds[cond_count].value_num    = num;
        }

        cond_count++;

        // Skip whitespace after the value (before the next AND/OR)
        while (isspace(*p)) p++;
    }

    // If no condition was successfully parsed — error
    if (cond_count == 0) {
        free(input);
        return -1;
    }

    // Copy the result into the output structure
    expr->cond_count = cond_count;
    expr->conditions = malloc(cond_count * sizeof(Condition));
    if (!expr->conditions) {
        free(input);
        return -1;
    }
    memcpy(expr->conditions, temp_conds, cond_count * sizeof(Condition));

    // Logical operators (if there is more than one condition)
    expr->logic_ops = NULL;
    if (cond_count > 1) {
        expr->logic_ops = malloc((cond_count - 1) * sizeof(LogicOperator));
        if (!expr->logic_ops) {
            free(expr->conditions);
            free(input);
            return -1;
        }
        memcpy(expr->logic_ops, temp_ops, (cond_count - 1) * sizeof(LogicOperator));
    }

    free(input);
    return 0;
}

/**
 * @brief Extracts a cell value from a CSV row by column name
 *
 * Parses a single CSV row and returns the value of the field that corresponds to the
 * specified column name. Correctly handles escaped quotes (RFC 4180).
 * Returns a newly allocated string (malloc) that **must** be freed with free().
 *
 * @param line          Pointer to the CSV row (one record, null-terminated)
 *                      May contain escaped quotes and commas inside fields
 * @param col_name      Name of the column whose value should be found
 *                      Comparison is case-insensitive (strcasecmp)
 *                      If col_name is empty or NULL → an empty string is returned
 * @param use_headers   Header usage flag:
 *                        1 — column names are taken from the column_names[] array
 *                        0 — names are generated as A, B, C, ..., AA, AB, etc.
 *
 * @return
 *   - New string (malloc) containing the cell value (without quotes)
 *   - Empty string "" (also via strdup) if:
 *     • column not found
 *     • row is empty / invalid
 *     • memory allocation failed
 *     • NULL or empty arguments were passed
 *
 * @note
 *   - The function **always** returns newly allocated memory → result must be free()d!
 *   - The parser handles quote escaping inside fields ("He said ""hello""")
 *   - Column name comparison is case-insensitive (strcasecmp) — convenient for users
 *   - If use_headers=1 but column_names[col_idx] == NULL — falls back to letter name
 *   - Maximum number of columns is limited by MAX_COLS
 *
 * @example
 *   // Assume headers: "ID", "Name", "Price"
 *   char *line = "101,\"Anna Smith\",29.99";
 *
 *   char *val1 = get_column_value(line, "Price", 1);     // → "29.99" (must free)
 *   char *val2 = get_column_value(line, "name", 1);      // → "Anna Smith" (case insensitive)
 *   char *val3 = get_column_value(line, "C", 0);         // → "29.99" (column C = third)
 *   char *val4 = get_column_value(line, "Unknown", 1);   // → ""
 *
 *   free(val1); free(val2); free(val3); free(val4);      // mandatory!
 *
 * @warning
 *   - The returned pointer **always** must be freed with free(),
 *     even if an empty string ("") was returned
 *   - Does not modify the source string line
 *   - For very long strings (> MAX_LINE_LEN elsewhere in the code) behaviour may be incorrect,
 *     but internally only strdup(line) is used, so it is bounded by available memory
 *
 * @see
 *   - col_name_to_num()     — get column index by name
 *   - evaluate_condition()  — uses this function when checking filters
 *   - row_matches_filter()  — main consumer
 */
char *get_column_value(const char *line, const char *col_name, int use_headers)
{
    if (!line || !*line || !col_name || !*col_name)
        return strdup("");

    int count;
    char **fields = parse_csv_line(line, &count);
    if (!fields)
        return strdup("");

    char *result = NULL;
    char letter[16];

    for (int i = 0; i < count; i++)
    {
        const char *current_name;
        if (use_headers && i < MAX_COLS && column_names[i])
        {
            current_name = column_names[i];
        }
        else
        {
            col_letter(i, letter);
            current_name = letter;
        }

        if (strcasecmp(current_name, col_name) == 0)
        {
            result = strdup(fields[i]);
            break;
        }
    }

    free_csv_fields(fields, count);
    return result ? result : strdup("");
}

/**
 * @brief Checks whether a cell value satisfies a given comparison condition
 *
 * Compares a cell value (`cell`) against a condition from the `Condition` structure.
 * Supports two comparison modes:
 *   - numeric (if the condition value was successfully parsed as a double)
 *   - string (lexicographic, using strcmp)
 *
 * @param cell      Pointer to the string — cell value from CSV (may be NULL)
 *                  If cell == NULL, it is treated as an empty string ""
 * @param cond      Pointer to the condition structure (Condition) containing:
 *                    • op          — comparison operator (OP_EQ, OP_NE, OP_GT, etc.)
 *                    • value       — string value to compare against
 *                    • value_is_num — flag indicating the value is numeric
 *                    • value_num   — numeric representation (if value_is_num == 1)
 *
 * @return
 *    1    — condition is satisfied (cell matches the condition)
 *    0    — condition is NOT satisfied OR an error occurred:
 *           • unknown operator
 *           • cell could not be parsed as a number (in numeric comparison mode)
 *           • NULL was passed for cond (not checked explicitly)
 *
 * @note
 *   - In **numeric** comparison mode:
 *     • If the cell is not a valid number (e.g. "abc", "12.3.4", "")
 *       → the condition is considered false (returns 0)
 *     • Uses strtod → respects locale (decimal separator . or ,)
 *   - In **string** comparison mode:
 *     • All operators are supported, but >, <, >=, <= only make sense for
 *       lexicographically ordered data (ISO dates, codes, etc.)
 *     • Comparison is case-sensitive (strcmp)
 *   - The function does **not** allocate memory and does **not** modify the input data
 *   - A very important internal function — used in row_matches_filter()
 *
 * @example
 *   Condition cond = { .op = OP_GT, .value = "100", .value_is_num = 1, .value_num = 100.0 };
 *
 *   evaluate_condition("150", &cond)    → 1   (150 > 100)
 *   evaluate_condition("99", &cond)     → 0
 *   evaluate_condition("abc", &cond)    → 0   (not a number)
 *   evaluate_condition("", &cond)       → 0
 *
 *   Condition cond2 = { .op = OP_EQ, .value = "Active", .value_is_num = 0 };
 *   evaluate_condition("active", &cond2) → 0   (case matters)
 *   evaluate_condition("Active", &cond2) → 1
 *
 * @warning
 *   - In string comparison with >/< operators the result may be surprising
 *     (e.g. "10" < "2" → true, because '1' < '2')
 *   - No support for partial matching, LIKE, regular expressions, etc.
 *     (only exact comparison)
 *
 * @see
 *   - row_matches_filter()   — main usage site
 *   - parse_filter_expression() — where the Condition structure comes from
 *   - apply_filter()         — the full filtering process
 */
int evaluate_condition(const char *cell, const Condition *cond)
{
    // Guard: if cell is NULL — treat as empty string
    if (!cell) {
        cell = "";
    }

    int col_num = col_name_to_num(cond->column);
    if (col_num < 0) {
        // column not found → treat condition as not satisfied
        return 0;
    }

    if (cond->value_is_num)
    {
        // Numeric comparison
        char *endptr;
        double cell_num = parse_double(cell, &endptr);

        // If the string could not be fully parsed as a number
        // (characters remain after the number, or the string did not start with a number)
        if (*endptr != '\0' || endptr == cell) {
            return 0;
        }

        switch (cond->op)
        {
            case OP_EQ: return cell_num == cond->value_num;
            case OP_NE: return cell_num != cond->value_num;
            case OP_GT: return cell_num >  cond->value_num;
            case OP_GE: return cell_num >= cond->value_num;
            case OP_LT: return cell_num <  cond->value_num;
            case OP_LE: return cell_num <= cond->value_num;
            default:    return 0;  // unknown operator
        }
    }
    else if (col_types[col_num] == COL_DATE)
    {
        // Strip any extra whitespace that the parser may have added
        char val_trimmed[256];
        strncpy(val_trimmed, cond->value, sizeof(val_trimmed)-1);
        val_trimmed[sizeof(val_trimmed)-1] = '\0';
        trim(val_trimmed);   // custom trim function

        size_t val_len  = strlen(val_trimmed);
        size_t cell_len = strlen(cell);

        /* ── Quarter format: "2025-Q1" .. "2025-Q4" ── */
        if (val_len == 7 && val_trimmed[5] == 'Q' &&
            val_trimmed[6] >= '1' && val_trimmed[6] <= '4' &&
            cell_len >= 10 && cell[4] == '-')
        {
            int q = val_trimmed[6] - '0';
            int q_start_m = (q - 1) * 3 + 1;   /* Q1→1, Q2→4, Q3→7, Q4→10 */
            int q_next_m  = q * 3 + 1;          /* first month of next quarter */
            int val_year  = 0;
            for (int i = 0; i < 4; i++) val_year = val_year * 10 + (val_trimmed[i] - '0');
            int next_year = val_year + (q_next_m > 12 ? 1 : 0);
            if (q_next_m > 12) q_next_m = 1;

            /* "YYYY-MM" boundary strings for lexicographic compare */
            char q_start[8], q_next[8];
            snprintf(q_start, sizeof(q_start), "%04d-%02d", val_year, q_start_m);
            snprintf(q_next,  sizeof(q_next),  "%04d-%02d", next_year, q_next_m);

            if (cond->op == OP_EQ)
            {
                /* cell must be >= start of quarter AND < start of next quarter */
                return strncmp(cell, q_start, 7) >= 0 && strncmp(cell, q_next, 7) < 0;
            }
            if (cond->op == OP_NE)
            {
                return !(strncmp(cell, q_start, 7) >= 0 && strncmp(cell, q_next, 7) < 0);
            }
            /* >= Q: date must be >= first day of quarter start month */
            if (cond->op == OP_GE) return strncmp(cell, q_start, 7) >= 0;
            /* >  Q: date must be >= first month of next quarter */
            if (cond->op == OP_GT) return strncmp(cell, q_next,  7) >= 0;
            /* <= Q: date must be < first month of next quarter */
            if (cond->op == OP_LE) return strncmp(cell, q_next,  7) <  0;
            /* <  Q: date must be < first month of this quarter */
            if (cond->op == OP_LT) return strncmp(cell, q_start, 7) <  0;
            return 0;
        }

        if (cond->op == OP_EQ)
        {
            // Most common case: matching a month "2026-01"
            if (val_len == 7 && cell_len >= 10 &&
                strncmp(cell, val_trimmed, 7) == 0 &&
                cell[7] == '-')
            {
                return 1;   // yes, this is January 2026 (or any day in that month)
            }

            // If a full date was entered — ordinary equality
            return strcmp(cell, val_trimmed) == 0;
        }

        /* Month format "YYYY-MM": use 7-char prefix compare so that
           "2025-08-15" <= "2025-08" works correctly (strcmp would give
           wrong result because '-' > '\0' in the 8th position). */
        if (val_len == 7 && cell_len >= 10 && cell[7] == '-')
        {
            int cmp = strncmp(cell, val_trimmed, 7);
            switch (cond->op)
            {
                case OP_NE: return cmp != 0;
                case OP_GT: return cmp >  0;
                case OP_GE: return cmp >= 0;
                case OP_LT: return cmp <  0;
                case OP_LE: return cmp <= 0;
                default:    return 0;
            }
        }

        // All other operators (>= > <= < !=) — lexicographic string comparison
        int cmp = strcmp(cell, val_trimmed);

        switch (cond->op)
        {
            case OP_NE: return cmp != 0;
            case OP_GT: return cmp >  0;
            case OP_GE: return cmp >= 0;
            case OP_LT: return cmp <  0;
            case OP_LE: return cmp <= 0;
            default:    return 0;
        }
    }    
    else
    {
        // String comparison
        switch (cond->op)
        {
            case OP_EQ: return strcmp(cell, cond->value) == 0;
            case OP_NE: return strcmp(cell, cond->value) != 0;

            // Lexicographic comparison (can be useful for ISO dates, codes, etc.)
            case OP_GT: return strcmp(cell, cond->value) > 0;
            case OP_GE: return strcmp(cell, cond->value) >= 0;
            case OP_LT: return strcmp(cell, cond->value) < 0;
            case OP_LE: return strcmp(cell, cond->value) <= 0;

            default:    return 0;  // unknown operator
        }
    }

    // We only reach here with an unknown operator (just in case)
    return 0;
}

/**
 * @brief Checks whether a single CSV row matches a given filter
 *
 * Applies a fully parsed filter expression (FilterExpr) to one CSV row.
 * Returns 1 if the row passes all filter conditions (respecting AND/OR and negation !).
 *
 * @param line      Pointer to the CSV row (one record, null-terminated)
 *                  Must be a valid CSV string (with commas, possible quotes)
 * @param expr      Pointer to the parsed filter expression (FilterExpr)
 *                  If expr == NULL or it has 0 conditions → returns 1 (all rows pass)
 *
 * @return
 *    1    — row matches the filter (all conditions satisfied)
 *    0    — row does NOT match the filter (at least one condition is false)
 *
 * @note
 *   - If the filter is empty (cond_count == 0) or expr == NULL → returns 1
 *     (important for the "no filter — show everything" logic)
 *   - Conditions are applied left-to-right respecting AND/OR precedence
 *     (without parentheses — simply sequential evaluation)
 *   - For logical AND, if the intermediate result is already false — further conditions
 *     are NOT checked (early exit — optimisation)
 *   - Negation (!) applies to the **entire** expression as a whole
 *   - get_column_value() is called for each condition → allocates memory,
 *     so each call is paired with free(val)
 *
 * @example
 *   // Assume expr: "Age >= 18 AND City = \"Moscow\""
 *   row_matches_filter("101,Anna,25,Moscow", &expr)   → 1
 *   row_matches_filter("102,Bob,17,Moscow", &expr)    → 0  (age < 18)
 *   row_matches_filter("103,John,30,Kyiv", &expr)     → 0  (city is not Moscow)
 *
 *   // expr with negation: "! Status = Blocked"
 *   row_matches_filter("...,Active", &expr)   → 1
 *   row_matches_filter("...,Blocked", &expr)  → 0
 *
 *   // Empty filter
 *   FilterExpr empty = {0};
 *   row_matches_filter(any_line, &empty) → 1 (always passes)
 *
 * @warning
 *   - The function **allocates and frees** memory for each cell value
 *     (get_column_value → strdup inside, free here)
 *   - If the CSV row is malformed (unclosed quotes, etc.) — behaviour of
 *     get_column_value may be unpredictable, but will usually return "" or a partial string
 *   - No protection against a very large number of conditions (though the parser limits to 64)
 *
 * @see
 *   - parse_filter_expression() — create a FilterExpr structure from a string
 *   - evaluate_condition()      — check a single condition
 *   - get_column_value()        — get a cell value by column name
 *   - apply_filter()            — apply the filter to all rows in the file
 */
int row_matches_filter(const char *line, const FilterExpr *expr)
{
    // Empty filter or NULL pointer → all rows pass
    if (!expr || expr->cond_count == 0) {
        return 1;
    }

    // Evaluate the first condition
    char *val = get_column_value(line, expr->conditions[0].column, use_headers);
    int result = evaluate_condition(val, &expr->conditions[0]);
    free(val);

    // Process the remaining conditions (if any)
    for (int i = 1; i < expr->cond_count; i++)
    {
        val = get_column_value(line, expr->conditions[i].column, use_headers);
        int next = evaluate_condition(val, &expr->conditions[i]);
        free(val);

        // Apply the logical operator between the previous result and the current one
        if (expr->logic_ops[i - 1] == LOGIC_AND)
        {
            result = result && next;
        }
        else  // LOGIC_OR
        {
            result = result || next;
        }

        // Optimisation: if AND and result is already false — no point checking further
        if (!result && expr->logic_ops[i - 1] == LOGIC_AND) {
            break;
        }
    }

    // Apply negation of the entire expression (! at the beginning)
    return expr->negated ? !result : result;
}

/**
 * @brief Frees all dynamically allocated memory inside a FilterExpr structure
 *
 * Completely clears the contents of the FilterExpr structure, freeing:
 *   - each column name (column) and value (value) in the conditions array
 *   - the conditions array itself
 *   - the logical operators array (logic_ops), if it was allocated
 *
 * After the call the structure becomes "empty" and safe for reuse or destruction.
 *
 * @param expr      Pointer to the FilterExpr structure whose memory should be freed
 *                  If expr == NULL — the function simply returns (safe)
 *
 * @return          No return value (void)
 *
 * @note
 *   - This function **must** be called after every successful parse_filter_expression(),
 *     otherwise there will be a memory leak!
 *   - Safe to call multiple times on the same structure
 *     (repeated free(NULL) calls are valid in C)
 *   - After the call all pointers inside expr become NULL, cond_count = 0
 *     (thanks to memset)
 *   - Does not touch the expr structure itself (no free(expr)), only its internal fields
 *   - Used in apply_filter() after processing the filter,
 *     and wherever a FilterExpr is created temporarily
 *
 * @example
 *   FilterExpr expr = {0};
 *   if (parse_filter_expression("Age > 18 AND City = Moscow", &expr) == 0) {
 *       // ... use expr ...
 *       free_filter_expr(&expr);          // ← mandatory!
 *   }
 *
 *   // Calling again is safe
 *   free_filter_expr(&expr);              // nothing will happen
 *
 * @warning
 *   - Do NOT call free() on expr->conditions or expr->logic_ops manually —
 *     that is already done inside the function
 *   - If you allocated memory for expr yourself (malloc), then free(expr) must be done
 *     separately after free_filter_expr(&expr)
 *   - After the call, expr fields MUST NOT be used without re-populating the structure
 *     (all pointers are zeroed out)
 *
 * @see
 *   - parse_filter_expression() — the function that allocates memory in expr
 *   - apply_filter()            — main consumer (frees after use)
 *   - row_matches_filter()      — uses expr but does not allocate memory
 */
void free_filter_expr(FilterExpr *expr)
{
    // If NULL was passed — do nothing (safe)
    if (!expr) {
        return;
    }

    // Free each dynamically allocated field in the conditions
    for (int i = 0; i < expr->cond_count; i++)
    {
        // column and value were allocated via strdup in parse_filter_expression
        free(expr->conditions[i].column);
        free(expr->conditions[i].value);

        // Zero out for safety (not strictly necessary at this point)
        expr->conditions[i].column = NULL;
        expr->conditions[i].value  = NULL;
    }

    // Free the conditions array itself
    free(expr->conditions);
    expr->conditions = NULL;

    // Free the logical operators array (may be NULL)
    free(expr->logic_ops);
    expr->logic_ops = NULL;

    // Zero out the entire structure for safety and predictability
    // (cond_count = 0, negated = 0, all pointers NULL)
    memset(expr, 0, sizeof(FilterExpr));
}

/**
 * @brief Comparison function for two doubles for use with qsort (callback)
 *
 * Simple comparison of two floating-point numbers.
 * Returns a negative value if da < db,
 * positive if da > db, and zero if equal.
 *
 * @param a  Pointer to the first double value
 * @param b  Pointer to the second double value
 * @return
 *   -1   — da < db
 *    0   — da == db
 *    1   — da > db
 *
 * @note
 *   - Very simple and fast implementation
 *   - Used for sorting arrays of floating-point numbers
 *   - Does not handle NaN and infinities (qsort behaviour with them is undefined)
 *
 * @example
 *   double values[] = {3.14, 1.0, 2.718, 0.0};
 *   qsort(values, 4, sizeof(double), compare_double);
 *   // After sorting: 0.0, 1.0, 2.718, 3.14
 *
 * @warning
 *   - Assumes the input pointers point to double (not float!)
 *   - Behaviour may be unpredictable in the presence of NaN
 *   - For stable sorting, use stable sort algorithms
 *
 * @see qsort() from <stdlib.h>
 */
int compare_double(const void *a, const void *b)
{
    // Extract double values from the pointers
    double da = *(const double *)a;
    double db = *(const double *)b;

    // Simple comparison
    if (da < db) {
        return -1;
    }
    if (da > db) {
        return 1;
    }

    // Equal (including +0 and -0 in IEEE 754)
    return 0;
}

/**
 * Formats an integer with spaces as thousands separators.
 * Example: 4351235 → "4 351 235"
 *          1000000  → "1 000 000"
 *          123      → "123"
 */
void format_number_with_spaces(long long num, char *buf, size_t bufsize)
{
    if (bufsize < 2) {
        buf[0] = '\0';
        return;
    }

    char temp[32];              // sufficient for long long
    int len = snprintf(temp, sizeof(temp), "%lld", num);
    if (len < 0 || len >= (int)sizeof(temp)) {
        strncpy(buf, "?", bufsize);
        buf[bufsize-1] = '\0';
        return;
    }

    int out_pos = 0;
    int digits = 0;

    // Iterate from end to start
    for (int i = len - 1; i >= 0; i--) {
        if (digits > 0 && digits % 3 == 0 && out_pos < (int)bufsize - 1) {
            buf[out_pos++] = ' ';
        }
        if (out_pos < (int)bufsize - 1) {
            buf[out_pos++] = temp[i];
        }
        digits++;
    }

    buf[out_pos] = '\0';

    // Reverse the string back
    int left = 0, right = out_pos - 1;
    while (left < right) {
        char t = buf[left];
        buf[left] = buf[right];
        buf[right] = t;
        left++;
        right--;
    }
}

/**
 * Truncates a string to max_width characters, appending … at the end
 * if the string was longer. Returns a newly allocated string.
 * If the string is NULL or empty → returns an empty string "".
 */
char *truncate_for_display(const char *str, int max_width)
{
    if (!str || !*str) {
        return strdup("");
    }

    if (max_width <= 0) {
        return strdup("");
    }

    size_t len = strlen(str);
    if (len <= (size_t)max_width) {
        return strdup(str);
    }

    // Reserve space for … (3 characters)
    int keep = max_width;
    if (keep < 1) keep = 1;

    char *result = malloc(max_width + 1);
    if (!result) return strdup(str);  // fallback on allocation error

    strncpy(result, str, keep);
    //strcpy(result + keep, "...");

    result[max_width] = '\0';
    return result;
}

char *clean_column_name(const char *raw)
{
    if (!raw || !*raw) return strdup("");

    // copy and strip trailing whitespace + \r\n + non-breaking spaces
    char *s = strdup(raw);
    if (!s) return strdup(raw);

    // strip the end
    size_t len = strlen(s);
    while (len > 0) {
        unsigned char c = (unsigned char)s[len-1];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == 0xA0) {
            s[--len] = '\0';
        } else {
            break;
        }
    }

    // strip the start (just in case)
    char *start = s;
    while (*start == ' ' || *start == '\t' || (unsigned char)*start == 0xA0) {
        start++;
    }

    if (start != s) {
        memmove(s, start, strlen(start) + 1);
    }

    return s;
}

/**
 * Parses a single CSV line according to RFC 4180 rules (simple implementation).
 * Returns an array of allocated fields (each field and the array itself must be freed).
 *
 * @param line      Input string (from file or cache)
 * @param out_count [out] Number of fields parsed
 * @return          Array of char* (NULL-terminated), each element is a malloc string
 *                  Returns NULL on error, out_count = 0
 */
char **parse_csv_line(const char *line, int *out_count)
{
    if (!line || !out_count) {
        if (out_count) *out_count = 0;
        return NULL;
    }

    *out_count = 0;

    // Estimate the approximate number of fields (for initial allocation)
    int max_fields = 64; // initial size, realloc later if needed
    char **fields = malloc(max_fields * sizeof(char *));
    if (!fields) return NULL;

    const char *p = line;
    int field_count = 0;
    char field_buf[MAX_LINE_LEN];
    int buf_pos;
    int in_quotes;

    while (*p)
    {
        buf_pos = 0;
        in_quotes = 0;
        field_buf[0] = '\0';

        // Skip leading spaces (but not the delimiter itself — important for TSV)
        while ((*p == ' ' || *p == '\t') && *p != csv_delimiter) p++;

        while (*p)
        {
            if (*p == '"')
            {
                if (in_quotes)
                {
                    // Closing quote
                    if (*(p + 1) == '"')
                    {
                        // Escaped "" → write a single "
                        if (buf_pos < MAX_LINE_LEN - 1)
                            field_buf[buf_pos++] = '"';
                        p += 2;
                        continue;
                    }
                    else
                    {
                        // End of quoted section
                        in_quotes = 0;
                        p++;
                        continue;
                    }
                }
                else
                {
                    // Start of quoted section
                    in_quotes = 1;
                    p++;
                    continue;
                }
            }

            if (*p == csv_delimiter && !in_quotes)
            {
                p++; // advance past the delimiter
                break;
            }

            // Regular character
            if (buf_pos < MAX_LINE_LEN - 1)
            {
                field_buf[buf_pos++] = *p;
            }
            p++;
        }

        field_buf[buf_pos] = '\0';

        // Add the field to the array
        if (field_count >= max_fields)
        {
            max_fields *= 2;
            char **new_fields = realloc(fields, max_fields * sizeof(char *));
            if (!new_fields)
            {
                // Error — free everything
                for (int k = 0; k < field_count; k++) free(fields[k]);
                free(fields);
                *out_count = 0;
                return NULL;
            }
            fields = new_fields;
        }

        fields[field_count] = strdup(field_buf);
        if (!fields[field_count])
        {
            // Memory error — clean up
            for (int k = 0; k < field_count; k++) free(fields[k]);
            free(fields);
            *out_count = 0;
            return NULL;
        }

        field_count++;

        // Skip whitespace after the delimiter (but not the delimiter itself)
        while ((*p == ' ' || *p == '\t') && *p != csv_delimiter) p++;
    }

    // Last field (if the string did not end with a comma)
    if (buf_pos > 0 || field_count > 0)
    {
        // If we already added it — fine
        // If not — add an empty field if there was a trailing comma
        if (*(p - 1) == csv_delimiter && buf_pos == 0)
        {
            fields[field_count] = strdup("");
            field_count++;
        }
    }

    *out_count = field_count;
    return fields;
}

/**
 * Frees the array of fields returned by parse_csv_line().
 */
void free_csv_fields(char **fields, int count)
{
    if (!fields) return;
    for (int i = 0; i < count; i++) free(fields[i]);
    free(fields);
}

/**
 * Builds a CSV/TSV/PSV line from an array of fields (RFC 4180).
 * Fields containing the delimiter, a quote, or a newline are wrapped in "...".
 * NULL fields are treated as an empty string.
 * Returns a malloc string WITHOUT a trailing \n. Must free().
 */
char *build_csv_line(char **fields, int count, char delimiter)
{
    if (!fields || count <= 0) return strdup("");

    // Calculate the maximum size (worst case: every character is a quote)
    size_t total = (size_t)count + 2; // delimiters + \0
    for (int i = 0; i < count; i++) {
        if (fields[i]) total += strlen(fields[i]) * 2 + 2;
    }

    char *result = malloc(total);
    if (!result) return NULL;

    char *p = result;
    for (int i = 0; i < count; i++) {
        if (i > 0) *p++ = delimiter;

        const char *f = fields[i] ? fields[i] : "";

        // Are quotes needed?
        int needs_quotes = 0;
        for (const char *s = f; *s; s++) {
            if (*s == delimiter || *s == '"' || *s == '\n' || *s == '\r') {
                needs_quotes = 1;
                break;
            }
        }

        if (needs_quotes) {
            *p++ = '"';
            for (const char *s = f; *s; s++) {
                if (*s == '"') *p++ = '"'; // escaping: "" → ""
                *p++ = *s;
            }
            *p++ = '"';
        } else {
            size_t len = strlen(f);
            memcpy(p, f, len);
            p += len;
        }
    }
    *p = '\0';
    return result;
}