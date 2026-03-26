#include "concat_files.h"
#include <ctype.h>   // for isspace, in case trim is needed
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void print_progress(int current, int total, const char *current_file)
{
    if (total <= 0) return;

    int bar_width = 20;
    int pos = (current * bar_width) / total;

    printf("\r[");
    for (int i = 0; i < bar_width; i++) {
        if (i < pos) printf("=");
        else if (i == pos) printf(">");
        else printf(" ");
    }
    printf("] %3d%% (%d/%d) ... %s", 
           (current * 100) / total, current, total, 
           current_file ? current_file : "");

    fflush(stdout);
}

static void print_progress_done(const char *output_name)
{
    printf("\r[====================] 100%% done\n");
    printf("Successfully created: %s\n", output_name);
    fflush(stdout);
}

// Returns 1 if the string needs to be quoted according to CSV rules
static int needs_quoting(const char *str)
{
    if (!str || !*str) return 0;
    while (*str) {
        if (*str == ',' || *str == '"' || *str == '\n') {
            return 1;
        }
        str++;
    }
    return 0;
}

// Helper: get the base filename without path and without extension
static char *get_basename_noext(const char *path)
{
    const char *filename = strrchr(path, '/');
    if (!filename) filename = strrchr(path, '\\');
    if (!filename) filename = path;
    else filename++;

    char *dot = strrchr(filename, '.');
    size_t len = dot ? (size_t)(dot - filename) : strlen(filename);

    char *result = malloc(len + 1);
    if (result) {
        strncpy(result, filename, len);
        result[len] = '\0';
    }
    return result;
}

// Helper: count the number of columns in a line (by commas)
static int count_columns(const char *line)
{
    if (!line || !*line) return 0;

    int count = 1;
    while (*line) {
        if (*line == ',') count++;
        line++;
    }
    return count;
}

int concat_and_save_files(
    char **files,
    int file_count,
    const char *source_col_name,
    const char *user_output,
    char **result_filename
)
{
    if (file_count < 1 || !source_col_name || !result_filename) {
        fprintf(stderr, "Invalid parameters\n");
        return 1;
    }

    *result_filename = NULL;

    // Generate the output filename
    char *output_name = NULL;
    if (user_output && *user_output) {
        output_name = strdup(user_output);
    } else {
        time_t now = time(NULL);
        struct tm *tm = localtime(&now);
        char buf[64];
        strftime(buf, sizeof(buf), "merged_%Y%m%d_%H%M%S.csv", tm);
        output_name = strdup(buf);
    }

    if (!output_name) {
        fprintf(stderr, "Failed to allocate output filename\n");
        return 1;
    }

    printf("Merging %d files into %s\n", file_count, output_name);

    // Open the first file
    FILE *first = fopen(files[0], "r");
    if (!first) {
        fprintf(stderr, "Cannot open first file: %s\n", files[0]);
        free(output_name);
        return 1;
    }

    char header[8192] = {0};
    if (!fgets(header, sizeof(header), first)) {
        fprintf(stderr, "First file is empty: %s\n", files[0]);
        fclose(first);
        free(output_name);
        return 1;
    }
    header[strcspn(header, "\n")] = '\0';

    int expected_cols = count_columns(header);
    if (expected_cols < 1) {
        fprintf(stderr, "Invalid header in first file\n");
        fclose(first);
        free(output_name);
        return 1;
    }

    // Create the output file
    FILE *out = fopen(output_name, "w");
    if (!out) {
        fprintf(stderr, "Cannot create output file: %s\n", output_name);
        fclose(first);
        free(output_name);
        return 1;
    }

    // Write the new header: source_col_name + original header
    if (needs_quoting(source_col_name)) {
        // Escape: double any internal quotes
        fprintf(out, "\"");
        for (const char *p = source_col_name; *p; p++) {
            if (*p == '"') {
                fputs("\"\"", out);
            } else {
                fputc(*p, out);
            }
        }
        fprintf(out, "\",%s\n", header);
    } else {
        // Simple name — no quoting needed
        fprintf(out, "%s,%s\n", source_col_name, header);
    }

    // Process the first file (data after the header)
    char line[8192];
    while (fgets(line, sizeof(line), first)) {
        line[strcspn(line, "\n")] = '\0';
        char *basename = get_basename_noext(files[0]);
        fprintf(out, "\"%s\",%s\n", basename ? basename : "unknown", line);
        free(basename);
    }
    fclose(first);

    // Show progress after the first file
    print_progress(1, file_count, files[0]);

    // Process the remaining files
    for (int i = 1; i < file_count; i++) {
        FILE *fp = fopen(files[i], "r");
        if (!fp) {
            fprintf(stderr, "\nCannot open file: %s\n", files[i]);
            fclose(out);
            remove(output_name);
            free(output_name);
            return 1;
        }

        // Skip the first line (header) — mandatory!
        char dummy[8192];
        if (!fgets(dummy, sizeof(dummy), fp)) {
            fprintf(stderr, "Warning: %s is empty or has no data after header\n", files[i]);
            fclose(fp);
            continue;
        }

        // Now read the first data line and check the column count
        if (!fgets(line, sizeof(line), fp)) {
            // file contained only a header — just skip it
            fclose(fp);
            continue;
        }

        int this_cols = count_columns(line);
        if (this_cols != expected_cols) {
            fprintf(stderr, "\nError: %s has %d columns (first data row), expected %d\n",
                    files[i], this_cols, expected_cols);
            fclose(fp);
            fclose(out);
            remove(output_name);
            free(output_name);
            return 2;
        }

        // Write this line and all subsequent lines
        do {
            line[strcspn(line, "\n")] = '\0';
            char *basename = get_basename_noext(files[i]);
            fprintf(out, "\"%s\",%s\n", basename ? basename : "unknown", line);
            free(basename);
        } while (fgets(line, sizeof(line), fp));

        fclose(fp);

        print_progress(i + 1, file_count, files[i]);
    }    

    fclose(out);

    // Final message
    print_progress_done(output_name);

    *result_filename = output_name;
    return 0;
}