#include "formula.h"
#include "csvview_defs.h"   /* rows, f, row_count, use_headers, col_name_to_num, col_to_num */
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>

/* ── Limits ───────────────────────────────────────────────────────────────── */
#define MAX_NODES   512
#define FMLA_MAX_AGG      64
#define MAX_ARGS      8
#define FBUF        1024   /* field extraction buffer */

/* ═══════════════════════════════════════════════════════════════════════════
   TOKENISER
   ═══════════════════════════════════════════════════════════════════════════ */

typedef enum {
    T_NUM, T_IDENT,
    T_PLUS, T_MINUS, T_STAR, T_SLASH,
    T_EQ, T_NEQ, T_LT, T_LE, T_GT, T_GE,
    T_LPAREN, T_RPAREN, T_COMMA,
    T_END, T_ERR
} TokType;

typedef struct { TokType type; double num; char str[128]; } Token;

typedef struct {
    const char *src;
    int         pos;
    Token       cur;
    int         ready;   /* 1 = cur is valid (peek'd) */
} Lex;

static Token lex_read(Lex *lx) {
    Token t = {T_END, 0, {0}};
    const char *s = lx->src;
    int i = lx->pos;

    while (s[i] && isspace((unsigned char)s[i])) i++;
    if (!s[i]) { lx->pos = i; return t; }

    /* number */
    if (isdigit((unsigned char)s[i]) ||
        (s[i] == '.' && isdigit((unsigned char)s[i+1]))) {
        char *end; t.num = strtod(s+i, &end);
        t.type = T_NUM; lx->pos = (int)(end-s); return t;
    }

    /* identifier or backtick-quoted name */
    if (isalpha((unsigned char)s[i]) || s[i] == '_') {
        int j = 0;
        while ((isalnum((unsigned char)s[i]) || s[i] == '_') && j < 127)
            t.str[j++] = s[i++];
        t.str[j] = '\0'; t.type = T_IDENT; lx->pos = i; return t;
    }
    if (s[i] == '`') {
        i++; int j = 0;
        while (s[i] && s[i] != '`' && j < 127) t.str[j++] = s[i++];
        t.str[j] = '\0'; if (s[i] == '`') i++;
        t.type = T_IDENT; lx->pos = i; return t;
    }

    /* operators */
    lx->pos = i+1;
    switch (s[i]) {
        case '+': t.type=T_PLUS;   return t;
        case '*': t.type=T_STAR;   return t;
        case '/': t.type=T_SLASH;  return t;
        case '(': t.type=T_LPAREN; return t;
        case ')': t.type=T_RPAREN; return t;
        case ',': t.type=T_COMMA;  return t;
        case '-': t.type=T_MINUS;  return t;
        case '=': if(s[i+1]=='='){lx->pos=i+2;} t.type=T_EQ;  return t;
        case '!': if(s[i+1]=='='){lx->pos=i+2; t.type=T_NEQ; return t;} break;
        case '<': if(s[i+1]=='='){lx->pos=i+2; t.type=T_LE;  return t;}
                  t.type=T_LT; return t;
        case '>': if(s[i+1]=='='){lx->pos=i+2; t.type=T_GE;  return t;}
                  t.type=T_GT; return t;
    }
    snprintf(t.str, sizeof(t.str), "unexpected char '%c'", s[i]);
    t.type = T_ERR; return t;
}

static Token lex_peek(Lex *lx) {
    if (!lx->ready) { lx->cur = lex_read(lx); lx->ready = 1; }
    return lx->cur;
}
static Token lex_consume(Lex *lx) {
    Token t = lex_peek(lx); lx->ready = 0; return t;
}

/* ═══════════════════════════════════════════════════════════════════════════
   AST NODES
   ═══════════════════════════════════════════════════════════════════════════ */

typedef enum {
    N_NUM, N_COL, N_BINOP, N_NEG, N_FUNC, N_AGG
} NodeKind;

typedef enum {
    F_OP_ADD, F_OP_SUB, F_OP_MUL, F_OP_DIV,
    F_OP_EQ, F_OP_NEQ, F_OP_LT, F_OP_LE, F_OP_GT, F_OP_GE
} OpKind;

typedef enum {
    FN_ROUND, FN_ABS, FN_FLOOR, FN_CEIL, FN_MOD, FN_POW, FN_IF, FN_EMPTY
} FnKind;

typedef enum {
    AG_SUM, AG_AVG, AG_MIN, AG_MAX, AG_COUNT,
    AG_MEDIAN, AG_PERCENTILE, AG_STDDEV, AG_VAR, AG_RANK, AG_PCT,
    AG_SUM_ALL, AG_AVG_ALL, AG_MIN_ALL, AG_MAX_ALL, AG_COUNT_ALL,
    AG_MEDIAN_ALL, AG_PERCENTILE_ALL, AG_STDDEV_ALL, AG_VAR_ALL,
    AG_RANK_ALL, AG_PCT_ALL
} AggKind;

typedef struct {
    NodeKind kind;
    double   num;              /* N_NUM */
    int      col_idx;          /* N_COL, N_AGG */
    char     col_name[64];     /* N_COL, N_AGG */
    OpKind   op;               /* N_BINOP */
    int      left, right;      /* N_BINOP */
    int      child;            /* N_NEG */
    FnKind   fn;               /* N_FUNC */
    int      args[MAX_ARGS];
    int      nargs;
    AggKind  agg;              /* N_AGG */
    double   agg_param;        /* percentile */
    int      agg_id;           /* index into Formula.agg[] */
} Node;

/* ═══════════════════════════════════════════════════════════════════════════
   AGGREGATE CACHE
   ═══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    AggKind  kind;
    int      col_idx;
    double   param;
    int      is_all;    /* 0 = filtered view, 1 = whole file */
    double   scalar;    /* for sum/avg/min/max/count/median/pct/stddev/var */
    double  *per_row;   /* [real_row_idx] for rank; [real_row_idx] pct  */
    int      computed;
} AggCache;

/* ═══════════════════════════════════════════════════════════════════════════
   FORMULA STRUCT
   ═══════════════════════════════════════════════════════════════════════════ */

struct Formula {
    Node     nodes[MAX_NODES];
    int      n_nodes;
    int      root;
    char     error[256];
    AggCache agg[MAX_AGG];
    int      n_agg;
};

/* ═══════════════════════════════════════════════════════════════════════════
   PARSER
   ═══════════════════════════════════════════════════════════════════════════ */

static int alloc_node(Formula *f) {
    if (f->n_nodes >= MAX_NODES) {
        snprintf(f->error, sizeof(f->error), "Expression too complex (> %d nodes)", MAX_NODES);
        return -1;
    }
    memset(&f->nodes[f->n_nodes], 0, sizeof(Node));
    return f->n_nodes++;
}

static int find_or_add_agg(Formula *f, AggKind k, int col, double p) {
    for (int i = 0; i < f->n_agg; i++) {
        AggCache *a = &f->agg[i];
        if (a->kind == k && a->col_idx == col && a->param == p) return i;
    }
    if (f->n_agg >= FMLA_MAX_AGG) {
        snprintf(f->error, sizeof(f->error), "Too many aggregate functions (> %d)", FMLA_MAX_AGG);
        return -1;
    }
    AggCache *a = &f->agg[f->n_agg];
    memset(a, 0, sizeof(*a));
    a->kind   = k;
    a->col_idx= col;
    a->param  = p;
    a->is_all = (k >= AG_SUM_ALL);
    return f->n_agg++;
}

static int str_to_agg(const char *nm, AggKind *out) {
    static const struct { const char *nm; AggKind ag; } T[] = {
        {"col_sum",AG_SUM},{"col_avg",AG_AVG},{"col_min",AG_MIN},{"col_max",AG_MAX},
        {"col_count",AG_COUNT},{"col_median",AG_MEDIAN},{"col_percentile",AG_PERCENTILE},
        {"col_stddev",AG_STDDEV},{"col_var",AG_VAR},{"col_rank",AG_RANK},{"col_pct",AG_PCT},
        {"col_sum_all",AG_SUM_ALL},{"col_avg_all",AG_AVG_ALL},{"col_min_all",AG_MIN_ALL},
        {"col_max_all",AG_MAX_ALL},{"col_count_all",AG_COUNT_ALL},
        {"col_median_all",AG_MEDIAN_ALL},{"col_percentile_all",AG_PERCENTILE_ALL},
        {"col_stddev_all",AG_STDDEV_ALL},{"col_var_all",AG_VAR_ALL},
        {"col_rank_all",AG_RANK_ALL},{"col_pct_all",AG_PCT_ALL},{NULL,0}
    };
    for (int i = 0; T[i].nm; i++) if (strcmp(nm, T[i].nm)==0){*out=T[i].ag;return 1;}
    return 0;
}

static int str_to_fn(const char *nm, FnKind *out) {
    if (!strcmp(nm,"round")){*out=FN_ROUND;return 1;}
    if (!strcmp(nm,"abs"))  {*out=FN_ABS;  return 1;}
    if (!strcmp(nm,"floor")){*out=FN_FLOOR;return 1;}
    if (!strcmp(nm,"ceil")) {*out=FN_CEIL; return 1;}
    if (!strcmp(nm,"mod"))  {*out=FN_MOD;  return 1;}
    if (!strcmp(nm,"pow"))  {*out=FN_POW;  return 1;}
    if (!strcmp(nm,"if"))   {*out=FN_IF;   return 1;}
    if (!strcmp(nm,"empty")){*out=FN_EMPTY;return 1;}
    return 0;
}

/* Forward declarations */
static int parse_expr   (Formula *f, Lex *lx);
static int parse_cmp    (Formula *f, Lex *lx);
static int parse_add    (Formula *f, Lex *lx);
static int parse_mul    (Formula *f, Lex *lx);
static int parse_unary  (Formula *f, Lex *lx);
static int parse_primary(Formula *f, Lex *lx);

static int parse_expr(Formula *f, Lex *lx) { return parse_cmp(f, lx); }

static int parse_cmp(Formula *f, Lex *lx) {
    int left = parse_add(f, lx); if (left < 0) return -1;
    Token t = lex_peek(lx);
    OpKind op;
    if      (t.type==T_EQ)  op=F_OP_EQ;
    else if (t.type==T_NEQ) op=F_OP_NEQ;
    else if (t.type==T_LT)  op=F_OP_LT;
    else if (t.type==T_LE)  op=F_OP_LE;
    else if (t.type==T_GT)  op=F_OP_GT;
    else if (t.type==T_GE)  op=F_OP_GE;
    else return left;
    lex_consume(lx);
    int right = parse_add(f, lx); if (right < 0) return -1;
    int n = alloc_node(f); if (n < 0) return -1;
    f->nodes[n].kind=N_BINOP; f->nodes[n].op=op;
    f->nodes[n].left=left; f->nodes[n].right=right;
    return n;
}

static int parse_add(Formula *f, Lex *lx) {
    int left = parse_mul(f, lx); if (left < 0) return -1;
    for (;;) {
        Token t = lex_peek(lx);
        if (t.type!=T_PLUS && t.type!=T_MINUS) break;
        lex_consume(lx);
        int right = parse_mul(f, lx); if (right < 0) return -1;
        int n = alloc_node(f); if (n < 0) return -1;
        f->nodes[n].kind=N_BINOP; f->nodes[n].op=(t.type==T_PLUS)?F_OP_ADD:F_OP_SUB;
        f->nodes[n].left=left; f->nodes[n].right=right; left=n;
    }
    return left;
}

static int parse_mul(Formula *f, Lex *lx) {
    int left = parse_unary(f, lx); if (left < 0) return -1;
    for (;;) {
        Token t = lex_peek(lx);
        if (t.type!=T_STAR && t.type!=T_SLASH) break;
        lex_consume(lx);
        int right = parse_unary(f, lx); if (right < 0) return -1;
        int n = alloc_node(f); if (n < 0) return -1;
        f->nodes[n].kind=N_BINOP; f->nodes[n].op=(t.type==T_STAR)?F_OP_MUL:F_OP_DIV;
        f->nodes[n].left=left; f->nodes[n].right=right; left=n;
    }
    return left;
}

static int parse_unary(Formula *f, Lex *lx) {
    if (lex_peek(lx).type == T_MINUS) {
        lex_consume(lx);
        int child = parse_unary(f, lx); if (child < 0) return -1;
        int n = alloc_node(f); if (n < 0) return -1;
        f->nodes[n].kind=N_NEG; f->nodes[n].child=child; return n;
    }
    return parse_primary(f, lx);
}

static int parse_primary(Formula *f, Lex *lx) {
    Token t = lex_peek(lx);

    if (t.type == T_NUM) {
        lex_consume(lx);
        int n = alloc_node(f); if (n < 0) return -1;
        f->nodes[n].kind=N_NUM; f->nodes[n].num=t.num; return n;
    }

    if (t.type == T_LPAREN) {
        lex_consume(lx);
        int inner = parse_expr(f, lx); if (inner < 0) return -1;
        if (lex_consume(lx).type != T_RPAREN) {
            snprintf(f->error, sizeof(f->error), "Expected ')'"); return -1;
        }
        return inner;
    }

    if (t.type == T_IDENT) {
        lex_consume(lx);
        char name[128]; strncpy(name, t.str, 127); name[127]='\0';

        /* function call? */
        if (lex_peek(lx).type == T_LPAREN) {
            lex_consume(lx); /* eat '(' */

            /* aggregate? */
            AggKind ak;
            if (str_to_agg(name, &ak)) {
                Token ctok = lex_consume(lx);
                if (ctok.type != T_IDENT) {
                    snprintf(f->error,sizeof(f->error),"%s(): column name expected",name);
                    return -1;
                }
                int cidx = use_headers ? col_name_to_num(ctok.str) : col_to_num(ctok.str);
                if (cidx < 0) {
                    snprintf(f->error,sizeof(f->error),"Column not found: '%s'",ctok.str);
                    return -1;
                }
                double param = 0.0;
                if (ak==AG_PERCENTILE || ak==AG_PERCENTILE_ALL) {
                    if (lex_consume(lx).type!=T_COMMA){
                        snprintf(f->error,sizeof(f->error),
                            "col_percentile(): 2nd arg (p) expected"); return -1;
                    }
                    Token pv = lex_consume(lx);
                    if (pv.type!=T_NUM){
                        snprintf(f->error,sizeof(f->error),
                            "col_percentile(): p must be a number"); return -1;
                    }
                    param = pv.num;
                }
                if (lex_consume(lx).type!=T_RPAREN){
                    snprintf(f->error,sizeof(f->error),"%s(): expected ')'",name);
                    return -1;
                }
                int aid = find_or_add_agg(f, ak, cidx, param);
                if (aid < 0) return -1;
                int n = alloc_node(f); if (n < 0) return -1;
                f->nodes[n].kind=N_AGG; f->nodes[n].agg=ak;
                f->nodes[n].col_idx=cidx; strncpy(f->nodes[n].col_name,ctok.str,63);
                f->nodes[n].agg_param=param; f->nodes[n].agg_id=aid;
                return n;
            }

            /* built-in function */
            FnKind fk;
            if (!str_to_fn(name, &fk)) {
                snprintf(f->error,sizeof(f->error),"Unknown function: '%s'",name);
                return -1;
            }
            int n = alloc_node(f); if (n < 0) return -1;
            f->nodes[n].kind=N_FUNC; f->nodes[n].fn=fk; f->nodes[n].nargs=0;
            if (lex_peek(lx).type != T_RPAREN) {
                for (;;) {
                    if (f->nodes[n].nargs >= MAX_ARGS){
                        snprintf(f->error,sizeof(f->error),"Too many args to '%s'",name);
                        return -1;
                    }
                    int a = parse_expr(f, lx); if (a<0) return -1;
                    f->nodes[n].args[f->nodes[n].nargs++]=a;
                    if (lex_peek(lx).type==T_COMMA){lex_consume(lx);continue;}
                    break;
                }
            }
            if (lex_consume(lx).type!=T_RPAREN){
                snprintf(f->error,sizeof(f->error),"Expected ')' after '%s' args",name);
                return -1;
            }
            return n;
        }

        /* column reference */
        int cidx = use_headers ? col_name_to_num(name) : col_to_num(name);
        if (cidx < 0) {
            snprintf(f->error,sizeof(f->error),"Column not found: '%s'",name);
            return -1;
        }
        int n = alloc_node(f); if (n < 0) return -1;
        f->nodes[n].kind=N_COL; f->nodes[n].col_idx=cidx;
        strncpy(f->nodes[n].col_name,name,63); return n;
    }

    if (t.type==T_END)
        snprintf(f->error,sizeof(f->error),"Unexpected end of expression");
    else
        snprintf(f->error,sizeof(f->error),"Unexpected token: '%s'",t.str);
    return -1;
}

/* ═══════════════════════════════════════════════════════════════════════════
   PUBLIC: compile
   ═══════════════════════════════════════════════════════════════════════════ */

Formula *formula_compile(const char *expr) {
    Formula *f = calloc(1, sizeof(Formula));
    if (!f) return NULL;
    Lex lx;
    memset(&lx, 0, sizeof(lx));
    lx.src = expr;
    int root = parse_expr(f, &lx);
    if (root < 0 || f->error[0]) {
        if (!f->error[0]) snprintf(f->error,sizeof(f->error),"Parse error");
        return f;
    }
    if (lex_peek(&lx).type != T_END)
        snprintf(f->error,sizeof(f->error),"Unexpected token: '%s'",lex_peek(&lx).str);
    f->root = root;
    return f;
}

const char *formula_error(const Formula *f) {
    return (f && f->error[0]) ? f->error : NULL;
}

/* ═══════════════════════════════════════════════════════════════════════════
   AGGREGATE PRE-COMPUTATION
   ═══════════════════════════════════════════════════════════════════════════ */

/* Extract CSV field at col_idx (0-based), unquoted, into buf. */
static void field_at(const char *line, int col_idx, char *buf, int bsz) {
    int col=0; const char *p=line;
    while (col<col_idx && *p) {
        if (*p=='"') { p++; while(*p){if(*p=='"'){p++;if(*p!='"')break;p++;}else p++;}}
        else { while(*p && *p!=',') p++; }
        if (*p==',') p++; col++;
    }
    if (!*p){buf[0]='\0';return;}
    int i=0;
    if (*p=='"'){
        p++; while(*p && i<bsz-1){
            if(*p=='"'){p++;if(*p=='"'){buf[i++]='"';p++;}else break;}
            else buf[i++]=*p++;
        }
    } else { while(*p&&*p!=','&&*p!='\n'&&*p!='\r'&&i<bsz-1) buf[i++]=*p++; }
    buf[i]='\0';
}

/* Lazy-load row and return line. */
static const char *row_line(int ri) {
    if (!rows[ri].line_cache) {
        fseek(f, rows[ri].offset, SEEK_SET);
        char buf[MAX_LINE_LEN];
        if (fgets(buf, sizeof(buf), f)) {
            buf[strcspn(buf,"\r\n")]='\0';
            rows[ri].line_cache = strdup(buf);
        } else {
            rows[ri].line_cache = strdup("");
        }
    }
    return rows[ri].line_cache;
}

/* Comparators for qsort */
static int cmp_dbl_asc(const void *a, const void *b) {
    double da=*(double*)a, db=*(double*)b; return (da>db)-(da<db);
}

typedef struct { double v; int ri; } VI;  /* value + row index */
static int cmp_vi_desc(const void *a, const void *b) {
    double da=((VI*)a)->v, db=((VI*)b)->v; return (da<db)-(da>db);
}

/* Compute one aggregate entry given an array of real row indices. */
static void compute_one(AggCache *a, int *rrows, int n, FormulaProgressFn cb) {
    char fbuf[FBUF];
    double sum=0, mn=INFINITY, mx=-INFINITY;
    long   cnt=0;

    /* --- first pass: collect values ---------------------------------- */
    int need_vals = (a->kind==AG_MEDIAN || a->kind==AG_MEDIAN_ALL ||
                     a->kind==AG_PERCENTILE || a->kind==AG_PERCENTILE_ALL ||
                     a->kind==AG_STDDEV || a->kind==AG_STDDEV_ALL ||
                     a->kind==AG_VAR   || a->kind==AG_VAR_ALL);
    int need_rank = (a->kind==AG_RANK || a->kind==AG_RANK_ALL);
    int need_pct  = (a->kind==AG_PCT  || a->kind==AG_PCT_ALL);

    double *vals = NULL;
    VI     *vi   = NULL;

    if (need_vals)
        vals = malloc((size_t)n * sizeof(double));
    if (need_rank)
        vi = malloc((size_t)n * sizeof(VI));

    char pmsg[128];
    for (int i=0; i<n; i++) {
        const char *ln = row_line(rrows[i]);
        field_at(ln, a->col_idx, fbuf, FBUF);
        if (!fbuf[0]) continue;
        char *ep; double v = strtod(fbuf, &ep);
        if (ep==fbuf) continue;
        sum += v;
        if (v<mn) mn=v;
        if (v>mx) mx=v;
        if (need_vals) vals[cnt] = v;
        if (need_rank) vi[cnt] = (VI){v, i};
        cnt++;
        if (cb && i%500000==0) {
            snprintf(pmsg,sizeof(pmsg),"Aggregating col %d: %d%%", a->col_idx+1, (int)(100.0*i/n));
            cb(pmsg);
        }
    }

    /* --- compute scalar ------------------------------------------- */
    a->scalar = 0.0;
    switch (a->kind) {
        case AG_SUM: case AG_SUM_ALL: a->scalar=sum; break;
        case AG_AVG: case AG_AVG_ALL: a->scalar=cnt>0?sum/cnt:0.0; break;
        case AG_MIN: case AG_MIN_ALL: a->scalar=cnt>0?mn:0.0; break;
        case AG_MAX: case AG_MAX_ALL: a->scalar=cnt>0?mx:0.0; break;
        case AG_COUNT: case AG_COUNT_ALL: a->scalar=(double)cnt; break;
        case AG_PCT: case AG_PCT_ALL: a->scalar=sum; break; /* store sum, use per-row */

        case AG_STDDEV: case AG_STDDEV_ALL:
            if (cnt>1 && vals) {
                double mean=sum/cnt, ss=0;
                for (long i=0;i<cnt;i++) ss+=(vals[i]-mean)*(vals[i]-mean);
                a->scalar=sqrt(ss/cnt);
            } break;
        case AG_VAR: case AG_VAR_ALL:
            if (cnt>1 && vals) {
                double mean=sum/cnt, ss=0;
                for (long i=0;i<cnt;i++) ss+=(vals[i]-mean)*(vals[i]-mean);
                a->scalar=ss/cnt;
            } break;

        case AG_MEDIAN: case AG_MEDIAN_ALL:
            if (cnt>0 && vals) {
                qsort(vals, cnt, sizeof(double), cmp_dbl_asc);
                a->scalar=(cnt%2==1)?vals[cnt/2]:(vals[cnt/2-1]+vals[cnt/2])/2.0;
            } break;

        case AG_PERCENTILE: case AG_PERCENTILE_ALL:
            if (cnt>0 && vals) {
                qsort(vals, cnt, sizeof(double), cmp_dbl_asc);
                double idx_d=(a->param/100.0)*(cnt-1);
                int lo=(int)idx_d; double frac=idx_d-lo;
                a->scalar=(lo+1<(int)cnt)?vals[lo]*(1-frac)+vals[lo+1]*frac:vals[lo];
            } break;

        default: break;
    }

    /* --- per-row: rank -------------------------------------------- */
    if (need_rank && vi && cnt>0) {
        a->per_row = calloc(row_count, sizeof(double));
        if (a->per_row) {
            qsort(vi, cnt, sizeof(VI), cmp_vi_desc);
            /* dense ranking with tie handling */
            int ri2=0;
            while (ri2<(int)cnt) {
                int rj=ri2;
                while (rj<(int)cnt && vi[rj].v==vi[ri2].v) rj++;
                double rank=ri2+1;
                for (int k=ri2;k<rj;k++) a->per_row[rrows[vi[k].ri]]=rank;
                ri2=rj;
            }
        }
    }

    /* --- per-row: pct --------------------------------------------- */
    if (need_pct) {
        a->per_row = calloc(row_count, sizeof(double));
        if (a->per_row && sum!=0.0) {
            for (int i=0;i<n;i++) {
                const char *ln = row_line(rrows[i]);
                field_at(ln, a->col_idx, fbuf, FBUF);
                if (!fbuf[0]) continue;
                char *ep; double v=strtod(fbuf,&ep);
                if (ep!=fbuf) a->per_row[rrows[i]]=v/sum*100.0;
            }
        }
    }

    free(vals); free(vi);
    a->computed=1;
}

/* ═══════════════════════════════════════════════════════════════════════════
   PUBLIC: precompute
   ═══════════════════════════════════════════════════════════════════════════ */

int formula_precompute(Formula *f, int *disp_rows, int disp_count,
                       FormulaProgressFn cb) {
    if (!f || f->error[0]) return 1;

    /* Build all-rows array once if any _all aggregates exist */
    int  start_row = use_headers ? 1 : 0;
    int  all_n     = row_count - start_row;
    int *all_rows  = NULL;

    for (int i=0; i<f->n_agg; i++) if (f->agg[i].is_all) {
        all_rows = malloc((size_t)all_n * sizeof(int));
        if (!all_rows) {
            snprintf(f->error,sizeof(f->error),"Out of memory");
            return 1;
        }
        for (int j=0;j<all_n;j++) all_rows[j]=start_row+j;
        break;
    }

    char pmsg[128];
    for (int i=0; i<f->n_agg; i++) {
        AggCache *a = &f->agg[i];
        if (cb) {
            snprintf(pmsg,sizeof(pmsg),"Pre-computing %d/%d...", i+1, f->n_agg);
            cb(pmsg);
        }
        if (a->is_all)
            compute_one(a, all_rows, all_n, cb);
        else
            compute_one(a, disp_rows, disp_count, cb);
    }

    free(all_rows);
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════════════
   EVALUATOR
   ═══════════════════════════════════════════════════════════════════════════ */

/* Returns 0=ok, 1=undefined (div/0, empty, etc.) */
static int eval_node(const Formula *f, int ni,
                     int real_row, const char *line,
                     double *out) {
    const Node *nd = &f->nodes[ni];
    double L, R;

    switch (nd->kind) {
        case N_NUM: *out=nd->num; return 0;

        case N_COL: {
            char buf[FBUF];
            field_at(line, nd->col_idx, buf, FBUF);
            if (!buf[0]) return 1;
            char *ep; *out=strtod(buf,&ep);
            return (ep==buf)?1:0;
        }

        case N_NEG:
            if (eval_node(f,nd->child,real_row,line,&L)) return 1;
            *out=-L; return 0;

        case N_BINOP:
            if (eval_node(f,nd->left, real_row,line,&L)) return 1;
            if (eval_node(f,nd->right,real_row,line,&R)) return 1;
            switch (nd->op) {
                case F_OP_ADD: *out=L+R; return 0;
                case F_OP_SUB: *out=L-R; return 0;
                case F_OP_MUL: *out=L*R; return 0;
                case F_OP_DIV: if(R==0.0) return 1; *out=L/R; return 0;
                case F_OP_EQ:  *out=(L==R)?1.0:0.0; return 0;
                case F_OP_NEQ: *out=(L!=R)?1.0:0.0; return 0;
                case F_OP_LT:  *out=(L< R)?1.0:0.0; return 0;
                case F_OP_LE:  *out=(L<=R)?1.0:0.0; return 0;
                case F_OP_GT:  *out=(L> R)?1.0:0.0; return 0;
                case F_OP_GE:  *out=(L>=R)?1.0:0.0; return 0;
            }
            return 1;

        case N_FUNC: {
            double a0=0,a1=0,a2=0;
            int r0=0,r1=0;
            switch (nd->fn) {
                case FN_EMPTY: {
                    if (nd->nargs<1) return 1;
                    /* empty(col): returns 1 if the col value is empty/non-numeric */
                    const Node *an = &f->nodes[nd->args[0]];
                    if (an->kind==N_COL) {
                        char buf[FBUF];
                        field_at(line,an->col_idx,buf,FBUF);
                        *out = (!buf[0])?1.0:0.0; return 0;
                    }
                    r0=eval_node(f,nd->args[0],real_row,line,&a0);
                    *out=r0?1.0:0.0; return 0;
                }
                case FN_IF:
                    if (nd->nargs<3) return 1;
                    if (eval_node(f,nd->args[0],real_row,line,&a0)) return 1;
                    if (a0!=0.0) {
                        if (eval_node(f,nd->args[1],real_row,line,&a1)) return 1;
                        *out=a1;
                    } else {
                        if (eval_node(f,nd->args[2],real_row,line,&a2)) return 1;
                        *out=a2;
                    }
                    return 0;
                case FN_ROUND:
                    r0=eval_node(f,nd->args[0],real_row,line,&a0); if(r0) return 1;
                    if (nd->nargs>=2) {
                        r1=eval_node(f,nd->args[1],real_row,line,&a1); if(r1) return 1;
                        double factor=pow(10.0,a1);
                        *out=round(a0*factor)/factor;
                    } else { *out=round(a0); }
                    return 0;
                case FN_ABS:
                    if (eval_node(f,nd->args[0],real_row,line,&a0)) return 1;
                    *out=fabs(a0); return 0;
                case FN_FLOOR:
                    if (eval_node(f,nd->args[0],real_row,line,&a0)) return 1;
                    *out=floor(a0); return 0;
                case FN_CEIL:
                    if (eval_node(f,nd->args[0],real_row,line,&a0)) return 1;
                    *out=ceil(a0); return 0;
                case FN_MOD:
                    if (nd->nargs<2) return 1;
                    if (eval_node(f,nd->args[0],real_row,line,&a0)) return 1;
                    if (eval_node(f,nd->args[1],real_row,line,&a1)) return 1;
                    if (a1==0.0) return 1;
                    *out=fmod(a0,a1); return 0;
                case FN_POW:
                    if (nd->nargs<2) return 1;
                    if (eval_node(f,nd->args[0],real_row,line,&a0)) return 1;
                    if (eval_node(f,nd->args[1],real_row,line,&a1)) return 1;
                    *out=pow(a0,a1); return 0;
            }
            return 1;
        }

        case N_AGG: {
            const AggCache *ac = &f->agg[nd->agg_id];
            if (!ac->computed) return 1;
            /* per-row aggregates */
            if (ac->per_row) {
                *out = (real_row>=0 && real_row<row_count) ? ac->per_row[real_row] : 0.0;
                return 0;
            }
            /* scalar */
            *out = ac->scalar; return 0;
        }
    }
    return 1;
}

/* ═══════════════════════════════════════════════════════════════════════════
   PUBLIC: eval_row / free
   ═══════════════════════════════════════════════════════════════════════════ */

int formula_eval_row(const Formula *f, int real_row, int disp_idx,
                     const char *line, double *out) {
    (void)disp_idx; /* kept for API symmetry; rank uses real_row */
    if (!f || f->error[0]) return 1;
    return eval_node(f, f->root, real_row, line, out);
}

void formula_free(Formula *f) {
    if (!f) return;
    for (int i=0; i<f->n_agg; i++) free(f->agg[i].per_row);
    free(f);
}
