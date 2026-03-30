/**
 * json_parse.c
 *
 * Minimal JSON parser for NDJSON support.
 * Supports flat JSON objects with string/number/bool/null values.
 * Nested objects/arrays are captured as opaque strings.
 */

#include "json_parse.h"
#include "csvview_defs.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ── Internal primitives ─────────────────────────────────────────────────── */

static void skip_ws(const char **p)
{
    while (**p == ' ' || **p == '\t' || **p == '\r' || **p == '\n')
        (*p)++;
}

/* Parse a JSON string value starting at *p (which must point to the opening ").
   Advances *p past the closing ".  Writes unescaped bytes into out[0..out_size-1].
   Returns the number of bytes written (not counting the NUL terminator). */
static int parse_json_string(const char **p, char *out, int out_size)
{
    if (**p != '"') return 0;
    (*p)++;                      /* skip opening " */

    int len = 0;
    while (**p && **p != '"') {
        char c = **p;
        if (c == '\\') {
            (*p)++;
            switch (**p) {
                case '"':  c = '"';  break;
                case '\\': c = '\\'; break;
                case '/':  c = '/';  break;
                case 'n':  c = '\n'; break;
                case 'r':  c = '\r'; break;
                case 't':  c = '\t'; break;
                case 'b':  c = '\b'; break;
                case 'f':  c = '\f'; break;
                case 'u': {
                    /* \uXXXX — decode BMP codepoint into UTF-8 */
                    unsigned int cp = 0;
                    for (int i = 1; i <= 4; i++) {
                        if (!(*p)[i]) break;
                        char hc = (*p)[i];
                        cp <<= 4;
                        if (hc >= '0' && hc <= '9') cp |= (unsigned)(hc - '0');
                        else if (hc >= 'a' && hc <= 'f') cp |= (unsigned)(hc - 'a' + 10);
                        else if (hc >= 'A' && hc <= 'F') cp |= (unsigned)(hc - 'A' + 10);
                    }
                    (*p) += 4;   /* will be incremented once more below */
                    if (cp < 0x80) {
                        c = (char)cp;
                    } else if (cp < 0x800) {
                        if (len < out_size - 3) {
                            if (out) out[len++] = (char)(0xC0 | (cp >> 6));
                            c = (char)(0x80 | (cp & 0x3F));
                        } else { c = '?'; }
                    } else {
                        if (len < out_size - 4) {
                            if (out) out[len++] = (char)(0xE0 | (cp >> 12));
                            if (out) out[len++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                            c = (char)(0x80 | (cp & 0x3F));
                        } else { c = '?'; }
                    }
                    break;
                }
                default: c = **p; break;
            }
        }
        if (out && len < out_size - 1) out[len] = c;
        len++;
        (*p)++;
    }
    if (**p == '"') (*p)++;      /* skip closing " */
    if (out) out[(len < out_size) ? len : out_size - 1] = '\0';
    return len;
}

/* Skip any JSON value at *p, advancing past it. */
static void skip_json_value(const char **p)
{
    skip_ws(p);
    if (**p == '"') {
        char tmp[MAX_LINE_LEN];
        parse_json_string(p, tmp, sizeof(tmp));
    } else if (**p == '{' || **p == '[') {
        char open  = **p;
        char close = (open == '{') ? '}' : ']';
        (*p)++;
        int depth = 1;
        while (**p && depth > 0) {
            if (**p == '"') {
                char tmp[MAX_LINE_LEN];
                parse_json_string(p, tmp, sizeof(tmp));
            } else {
                if (**p == open)  depth++;
                else if (**p == close) depth--;
                (*p)++;
            }
        }
    } else {
        /* number / bool / null */
        while (**p && **p != ',' && **p != '}' && **p != ']' &&
               !isspace((unsigned char)**p))
            (*p)++;
    }
}

/* Parse any JSON value at *p as a string, writing it into out[0..out_size-1].
   Strings have their quotes/escapes removed; everything else is verbatim.
   Returns the number of bytes written. */
static int parse_json_value_as_str(const char **p, char *out, int out_size)
{
    skip_ws(p);
    if (**p == '"') {
        return parse_json_string(p, out, out_size);
    } else if (**p == '{' || **p == '[') {
        const char *start = *p;
        skip_json_value(p);
        int len = (int)(*p - start);
        if (len >= out_size) len = out_size - 1;
        if (out) { memcpy(out, start, (size_t)len); out[len] = '\0'; }
        return len;
    } else {
        const char *start = *p;
        while (**p && **p != ',' && **p != '}' && **p != ']' && **p != '\n')
            (*p)++;
        /* trim trailing whitespace */
        const char *end = *p;
        while (end > start && isspace((unsigned char)end[-1])) end--;
        int len = (int)(end - start);
        if (len >= out_size) len = out_size - 1;
        if (out) { memcpy(out, start, (size_t)len); out[len] = '\0'; }
        return len;
    }
}

/* Find value for `key` in JSON object string `obj`.
   Writes result into out[0..out_size-1].  Returns 1 if found, 0 otherwise. */
static int json_find_key(const char *obj, const char *key,
                          char *out, int out_size)
{
    if (!obj || !key) { if (out && out_size > 0) out[0] = '\0'; return 0; }
    const char *p = obj;
    skip_ws(&p);
    if (*p != '{') { if (out && out_size > 0) out[0] = '\0'; return 0; }
    p++;

    while (*p) {
        skip_ws(&p);
        if (*p == '}' || *p == '\0') break;
        if (*p == ',') { p++; skip_ws(&p); }
        if (*p != '"') break;

        char key_buf[512];
        parse_json_string(&p, key_buf, sizeof(key_buf));
        skip_ws(&p);
        if (*p != ':') break;
        p++;
        skip_ws(&p);

        if (strcmp(key_buf, key) == 0) {
            parse_json_value_as_str(&p, out, out_size);
            return 1;
        } else {
            skip_json_value(&p);
        }
        skip_ws(&p);
    }

    if (out && out_size > 0) out[0] = '\0';
    return 0;
}

/* ── Public API ──────────────────────────────────────────────────────────── */

char **ndjson_parse_row(const char *buf, int *out_count)
{
    *out_count = 0;
    int n = col_count > 0 ? col_count : 0;

    char **fields = malloc((size_t)(n > 0 ? n : 1) * sizeof(char *));
    if (!fields) return NULL;

    for (int i = 0; i < n; i++) {
        char val[MAX_LINE_LEN];
        val[0] = '\0';
        if (buf && *buf && column_names[i])
            json_find_key(buf, column_names[i], val, sizeof(val));
        fields[i] = strdup(val);
    }

    *out_count = n;
    return fields;
}

char **ndjson_parse_positional(const char *obj, int *out_count)
{
    *out_count = 0;
    if (!obj) return NULL;

    int cap = 64;
    char **fields = malloc((size_t)cap * sizeof(char *));
    if (!fields) return NULL;

    const char *p = obj;
    skip_ws(&p);
    if (*p != '{') { free(fields); return NULL; }
    p++;

    int count = 0;
    while (*p) {
        skip_ws(&p);
        if (*p == '}' || *p == '\0') break;
        if (*p == ',') { p++; skip_ws(&p); }
        if (*p != '"') break;

        char key_buf[512];
        parse_json_string(&p, key_buf, sizeof(key_buf));
        (void)key_buf;
        skip_ws(&p);
        if (*p != ':') break;
        p++;
        skip_ws(&p);

        char val_buf[MAX_LINE_LEN];
        parse_json_value_as_str(&p, val_buf, sizeof(val_buf));

        if (count >= cap) {
            cap *= 2;
            char **tmp = realloc(fields, (size_t)cap * sizeof(char *));
            if (!tmp) break;
            fields = tmp;
        }
        fields[count++] = strdup(val_buf);
        skip_ws(&p);
    }

    *out_count = count;
    return fields;
}

int ndjson_discover_columns(FILE *fp, char **col_names, int max_cols)
{
    char buf[MAX_LINE_LEN];
    int  count = 0;
    int  lines = 0;

    rewind(fp);
    while (lines < 200 && count < max_cols && fgets(buf, sizeof(buf), fp)) {
        lines++;
        buf[strcspn(buf, "\r\n")] = '\0';
        if (!buf[0]) continue;

        const char *p = buf;
        skip_ws(&p);
        if (*p != '{') continue;
        p++;

        while (*p && count < max_cols) {
            skip_ws(&p);
            if (*p == '}' || *p == '\0') break;
            if (*p == ',') { p++; skip_ws(&p); }
            if (*p != '"') break;

            char key_buf[512];
            parse_json_string(&p, key_buf, sizeof(key_buf));
            skip_ws(&p);
            if (*p == ':') { p++; skip_ws(&p); }
            skip_json_value(&p);
            skip_ws(&p);

            /* Only add if not already seen */
            int found = 0;
            for (int i = 0; i < count; i++) {
                if (strcmp(col_names[i], key_buf) == 0) { found = 1; break; }
            }
            if (!found)
                col_names[count++] = strdup(key_buf);
        }
    }

    rewind(fp);
    return count;
}

/* ── Serialization helpers ───────────────────────────────────────────────── */

/* Escape a raw string for use as a JSON string value.
   Returns a malloc'd string WITHOUT surrounding quotes. */
static char *json_escape(const char *s)
{
    if (!s) return strdup("");
    size_t len = strlen(s);
    char *out = malloc(len * 6 + 1); /* worst case: every char → \uXXXX */
    if (!out) return strdup("");
    size_t pos = 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        if      (c == '"')  { out[pos++] = '\\'; out[pos++] = '"';  }
        else if (c == '\\') { out[pos++] = '\\'; out[pos++] = '\\'; }
        else if (c == '\n') { out[pos++] = '\\'; out[pos++] = 'n';  }
        else if (c == '\r') { out[pos++] = '\\'; out[pos++] = 'r';  }
        else if (c == '\t') { out[pos++] = '\\'; out[pos++] = 't';  }
        else if (c < 0x20)  { pos += (size_t)sprintf(out + pos, "\\u%04x", c); }
        else                { out[pos++] = (char)c; }
    }
    out[pos] = '\0';
    return out;
}

/* Returns 1 if s is a syntactically valid JSON number. */
static int is_json_number(const char *s)
{
    if (!s || !*s) return 0;
    const char *p = s;
    if (*p == '-') p++;
    if (!isdigit((unsigned char)*p)) return 0;
    while (isdigit((unsigned char)*p)) p++;
    if (*p == '.') {
        p++;
        if (!isdigit((unsigned char)*p)) return 0;
        while (isdigit((unsigned char)*p)) p++;
    }
    if (*p == 'e' || *p == 'E') {
        p++;
        if (*p == '+' || *p == '-') p++;
        if (!isdigit((unsigned char)*p)) return 0;
        while (isdigit((unsigned char)*p)) p++;
    }
    return *p == '\0';
}

char *ndjson_build_row(char **fields, int count,
                       char **col_names, ColType *col_types)
{
    int buf_size = MAX_LINE_LEN * 2;
    char *out = malloc((size_t)buf_size);
    if (!out) return NULL;

    int pos = 0;
    out[pos++] = '{';

    int limit = (count < col_count) ? count : col_count;

    for (int i = 0; i < limit; i++) {
        if (i > 0) {
            if (pos + 2 >= buf_size) {
                buf_size *= 2;
                char *tmp = realloc(out, (size_t)buf_size);
                if (!tmp) { free(out); return NULL; }
                out = tmp;
            }
            out[pos++] = ',';
        }

        /* Write key */
        const char *raw_key = (col_names && col_names[i]) ? col_names[i] : "";
        char *key_esc = json_escape(raw_key);
        int   klen    = (int)strlen(key_esc);
        if (pos + klen + 6 >= buf_size) {
            buf_size = pos + klen + 64;
            char *tmp = realloc(out, (size_t)buf_size);
            if (!tmp) { free(out); free(key_esc); return NULL; }
            out = tmp;
        }
        out[pos++] = '"';
        memcpy(out + pos, key_esc, (size_t)klen);
        pos += klen;
        out[pos++] = '"';
        out[pos++] = ':';
        free(key_esc);

        /* Write value */
        const char *val = (fields && fields[i]) ? fields[i] : "";
        ColType ct = (col_types && i < col_count) ? col_types[i] : COL_STR;

        const char *as_is = NULL;
        if      (strcmp(val, "null")  == 0) as_is = "null";
        else if (strcmp(val, "true")  == 0) as_is = "true";
        else if (strcmp(val, "false") == 0) as_is = "false";

        if (as_is) {
            int vlen = (int)strlen(as_is);
            if (pos + vlen + 2 >= buf_size) {
                buf_size *= 2;
                char *tmp = realloc(out, (size_t)buf_size);
                if (!tmp) { free(out); return NULL; }
                out = tmp;
            }
            memcpy(out + pos, as_is, (size_t)vlen);
            pos += vlen;
        } else if (ct == COL_NUM && val[0] == '\0') {
            /* Empty numeric → null */
            if (pos + 6 >= buf_size) {
                buf_size *= 2;
                char *tmp = realloc(out, (size_t)buf_size);
                if (!tmp) { free(out); return NULL; }
                out = tmp;
            }
            memcpy(out + pos, "null", 4);
            pos += 4;
        } else if (ct == COL_NUM && is_json_number(val)) {
            /* Valid number → no quotes */
            int vlen = (int)strlen(val);
            if (pos + vlen + 2 >= buf_size) {
                buf_size *= 2;
                char *tmp = realloc(out, (size_t)buf_size);
                if (!tmp) { free(out); return NULL; }
                out = tmp;
            }
            memcpy(out + pos, val, (size_t)vlen);
            pos += vlen;
        } else {
            /* String: escape and quote */
            char *val_esc = json_escape(val);
            int   vlen    = (int)strlen(val_esc);
            if (pos + vlen + 4 >= buf_size) {
                buf_size = pos + vlen + 64;
                char *tmp = realloc(out, (size_t)buf_size);
                if (!tmp) { free(out); free(val_esc); return NULL; }
                out = tmp;
            }
            out[pos++] = '"';
            memcpy(out + pos, val_esc, (size_t)vlen);
            pos += vlen;
            out[pos++] = '"';
            free(val_esc);
        }
    }

    if (pos + 3 >= buf_size) {
        char *tmp = realloc(out, (size_t)(pos + 4));
        if (!tmp) { free(out); return NULL; }
        out = tmp;
    }
    out[pos++] = '}';
    out[pos]   = '\0';
    return out;
}
