/* MINING DATABASE MANAGEMENT SYSTEM - WEB SERVER (C + WinSock + ODBC + Oracle)
 *
 * Serves the browser GUI in www/ and a small JSON API over HTTP on 127.0.0.1.
 * The database layer mirrors main.c: fixed entity definitions, bound parameters
 * for every value, and identifiers that only ever come from the tables below.
 *
 * Build:  build_server.bat      (gcc server.c -o miningweb.exe -lodbc32 -lws2_32 -ladvapi32)
 * Run:    miningweb.exe         then open http://127.0.0.1:8090
 *         miningweb.exe 9000    to listen on a different port
 */

#define _WIN32_WINNT 0x0601
#define WIN32_LEAN_AND_MEAN

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <wincrypt.h>
#include <sql.h>
#include <sqlext.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>

#define MAX_FIELDS   6
#define VAL_LEN      512
#define MAX_PARAMS   32
#define TOKEN_LEN    33
/* Oracle XE's own HTTP listener usually owns 8080, so default higher. */
#define DEFAULT_PORT 8090

/* ------------------------------------------------------------------ *
 * Entity / report / DDL definitions (same schema as the CLI version)
 * ------------------------------------------------------------------ */

typedef struct {
    const char *title;
    const char *table;
    const char *cols[MAX_FIELDS];
    const char *labels[MAX_FIELDS];
    const char *types;   /* N = number, T = text, D = YYYY-MM-DD date */
    const char *search_col;
    int n;
} Entity;

static Entity entities[] = {
    {"Mine Sites", "MINE_SITE",
     {"MINE_ID", "MINE_NAME", "LOCATION", "MINERAL_TYPE", "DEPTH_METERS", "STATUS"},
     {"Mine ID", "Mine name", "Location", "Mineral type", "Depth in meters", "Status"},
     "NTTTNT", "MINE_NAME", 6},
    {"Minerals", "MINERAL",
     {"MINERAL_ID", "MINERAL_NAME", "CATEGORY", "MARKET_PRICE"},
     {"Mineral ID", "Mineral name", "Category", "Market price"},
     "NTTN", "MINERAL_NAME", 4},
    {"Miners / Employees", "MINER",
     {"MINER_ID", "MINE_ID", "MINER_NAME", "JOB_ROLE", "PHONE"},
     {"Miner ID", "Mine ID", "Miner name", "Job role", "Phone"},
     "NNTTT", "MINER_NAME", 5},
    {"Equipment", "EQUIPMENT",
     {"EQUIPMENT_ID", "MINE_ID", "EQUIPMENT_NAME", "STATUS", "PURCHASE_COST"},
     {"Equipment ID", "Mine ID", "Equipment name", "Status", "Purchase cost"},
     "NNTTN", "EQUIPMENT_NAME", 5},
    {"Production Records", "PRODUCTION_RECORD",
     {"RECORD_ID", "MINE_ID", "MINERAL_ID", "PRODUCTION_DATE", "TONNES_PRODUCED"},
     {"Record ID", "Mine ID", "Mineral ID", "Production date", "Tonnes produced"},
     "NNNDN", "MINE_ID", 5},
    {"Safety Inspections", "SAFETY_INSPECTION",
     {"INSPECTION_ID", "MINE_ID", "INSPECTION_DATE", "INSPECTOR_NAME", "RESULT", "NOTES"},
     {"Inspection ID", "Mine ID", "Inspection date", "Inspector name", "Result", "Notes"},
     "NNDTTT", "INSPECTOR_NAME", 6}
};
#define ENTITY_COUNT ((int)(sizeof(entities) / sizeof(entities[0])))

typedef struct {
    const char *title;
    const char *description;
    const char *sql;
} Report;

static Report reports[] = {
    {"Production by mine and mineral",
     "JOIN + GROUP BY + SUM across mine sites, production records and minerals.",
     "SELECT ms.MINE_NAME, m.MINERAL_NAME, NVL(SUM(pr.TONNES_PRODUCED),0) TOTAL_TONNES "
     "FROM MINE_SITE ms JOIN PRODUCTION_RECORD pr ON ms.MINE_ID=pr.MINE_ID "
     "JOIN MINERAL m ON m.MINERAL_ID=pr.MINERAL_ID "
     "GROUP BY ms.MINE_NAME, m.MINERAL_NAME ORDER BY TOTAL_TONNES DESC"},
    {"Mine sites ordered by depth",
     "ORDER BY on the mine site table, deepest first.",
     "SELECT MINE_ID, MINE_NAME, LOCATION, DEPTH_METERS FROM MINE_SITE ORDER BY DEPTH_METERS DESC"},
    {"Equipment count by mine",
     "LEFT JOIN + COUNT so mines with no equipment still appear.",
     "SELECT ms.MINE_NAME, COUNT(e.EQUIPMENT_ID) EQUIPMENT_COUNT "
     "FROM MINE_SITE ms LEFT JOIN EQUIPMENT e ON ms.MINE_ID=e.MINE_ID "
     "GROUP BY ms.MINE_NAME ORDER BY ms.MINE_NAME"},
    {"Safety inspection results",
     "JOIN between mine sites and safety inspections, most recent first.",
     "SELECT ms.MINE_NAME, TO_CHAR(si.INSPECTION_DATE,'YYYY-MM-DD') INSPECTION_DATE, "
     "si.INSPECTOR_NAME, si.RESULT "
     "FROM MINE_SITE ms JOIN SAFETY_INSPECTION si ON ms.MINE_ID=si.MINE_ID "
     "ORDER BY si.INSPECTION_DATE DESC"},
    {"Miners per mine with job roles",
     "Workforce grouped by mine site.",
     "SELECT ms.MINE_NAME, mn.JOB_ROLE, COUNT(mn.MINER_ID) MINERS "
     "FROM MINE_SITE ms JOIN MINER mn ON ms.MINE_ID=mn.MINE_ID "
     "GROUP BY ms.MINE_NAME, mn.JOB_ROLE ORDER BY ms.MINE_NAME, mn.JOB_ROLE"},
    {"Estimated production value",
     "SUM of tonnes multiplied by the mineral market price.",
     "SELECT m.MINERAL_NAME, NVL(SUM(pr.TONNES_PRODUCED),0) TOTAL_TONNES, "
     "NVL(SUM(pr.TONNES_PRODUCED * m.MARKET_PRICE),0) ESTIMATED_VALUE "
     "FROM MINERAL m JOIN PRODUCTION_RECORD pr ON m.MINERAL_ID=pr.MINERAL_ID "
     "GROUP BY m.MINERAL_NAME ORDER BY ESTIMATED_VALUE DESC"}
};
#define REPORT_COUNT ((int)(sizeof(reports) / sizeof(reports[0])))

typedef struct {
    const char *title;
    const char *description;
    const char *sql;
    int is_query;     /* 1 = returns rows, 0 = DDL statement */
    int destructive;  /* 1 = requires confirm=DROP */
} DdlOp;

static DdlOp ddlops[] = {
    {"Create MINING_NOTE table",
     "CREATE TABLE with a primary key and a foreign key to MINE_SITE.",
     "CREATE TABLE MINING_NOTE (NOTE_ID NUMBER PRIMARY KEY, "
     "MINE_ID NUMBER REFERENCES MINE_SITE(MINE_ID), NOTE_DATE DATE, "
     "NOTE_TEXT VARCHAR2(500))", 0, 0},
    {"Add CATEGORY column to MINING_NOTE",
     "ALTER TABLE ... ADD on the table created above.",
     "ALTER TABLE MINING_NOTE ADD (CATEGORY VARCHAR2(50))", 0, 0},
    {"Inspect mining table columns",
     "Reads USER_TAB_COLUMNS to show the schema of every project table.",
     "SELECT TABLE_NAME, COLUMN_NAME, DATA_TYPE, DATA_LENGTH, NULLABLE "
     "FROM USER_TAB_COLUMNS WHERE TABLE_NAME IN "
     "('MINE_SITE','MINERAL','MINER','EQUIPMENT','PRODUCTION_RECORD',"
     "'SAFETY_INSPECTION','MINING_NOTE') ORDER BY TABLE_NAME, COLUMN_ID", 1, 0},
    {"Drop MINING_NOTE table",
     "DROP TABLE ... CASCADE CONSTRAINTS. Permanently removes the table.",
     "DROP TABLE MINING_NOTE CASCADE CONSTRAINTS", 0, 1}
};
#define DDL_COUNT ((int)(sizeof(ddlops) / sizeof(ddlops[0])))

/* ------------------------------------------------------------------ *
 * Growable string buffer
 * ------------------------------------------------------------------ */

typedef struct {
    char  *p;
    size_t len;
    size_t cap;
} Buf;

static void buf_init(Buf *b) {
    b->cap = 1024;
    b->len = 0;
    b->p = (char *)malloc(b->cap);
    if (b->p) b->p[0] = '\0';
}

static void buf_free(Buf *b) {
    free(b->p);
    b->p = NULL;
    b->len = b->cap = 0;
}

static int buf_reserve(Buf *b, size_t extra) {
    if (!b->p) return 0;
    if (b->len + extra + 1 <= b->cap) return 1;
    size_t want = b->cap;
    while (want < b->len + extra + 1) want *= 2;
    char *np = (char *)realloc(b->p, want);
    if (!np) return 0;
    b->p = np;
    b->cap = want;
    return 1;
}

static void buf_add_n(Buf *b, const char *s, size_t n) {
    if (!buf_reserve(b, n)) return;
    memcpy(b->p + b->len, s, n);
    b->len += n;
    b->p[b->len] = '\0';
}

static void buf_add(Buf *b, const char *s) {
    if (s) buf_add_n(b, s, strlen(s));
}

static void buf_addf(Buf *b, const char *fmt, ...) {
    char tmp[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if ((size_t)n < sizeof(tmp)) {
        buf_add_n(b, tmp, (size_t)n);
        return;
    }
    char *big = (char *)malloc((size_t)n + 1);
    if (!big) return;
    va_start(ap, fmt);
    vsnprintf(big, (size_t)n + 1, fmt, ap);
    va_end(ap);
    buf_add_n(b, big, (size_t)n);
    free(big);
}

/* Appends s as a quoted, escaped JSON string. */
static void buf_json_str(Buf *b, const char *s) {
    buf_add_n(b, "\"", 1);
    if (!s) s = "";
    for (const unsigned char *q = (const unsigned char *)s; *q; q++) {
        switch (*q) {
            case '"':  buf_add(b, "\\\""); break;
            case '\\': buf_add(b, "\\\\"); break;
            case '\n': buf_add(b, "\\n");  break;
            case '\r': buf_add(b, "\\r");  break;
            case '\t': buf_add(b, "\\t");  break;
            default:
                if (*q < 0x20) buf_addf(b, "\\u%04x", *q);
                else buf_add_n(b, (const char *)q, 1);
        }
    }
    buf_add_n(b, "\"", 1);
}

/* ------------------------------------------------------------------ *
 * ODBC state
 * ------------------------------------------------------------------ */

static SQLHENV env = SQL_NULL_HENV;
static SQLHDBC dbc = SQL_NULL_HDBC;
static int connected = 0;
static char session_token[TOKEN_LEN] = "";
static char session_user[128] = "";
static char session_dsn[256] = "";
static CRITICAL_SECTION db_lock;

static int ok(SQLRETURN r) {
    return r == SQL_SUCCESS || r == SQL_SUCCESS_WITH_INFO;
}

/* Collects the ODBC diagnostic records into a single readable message. */
static void collect_error(SQLSMALLINT type, SQLHANDLE handle, Buf *err) {
    SQLCHAR state[7], message[1024];
    SQLINTEGER native = 0;
    SQLSMALLINT length = 0, rec = 1;

    if (handle == SQL_NULL_HANDLE) {
        buf_add(err, "No ODBC handle available.");
        return;
    }
    while (rec < 8) {
        state[0] = '\0';
        message[0] = '\0';
        SQLRETURN r = SQLGetDiagRec(type, handle, rec, state, &native, message,
                                    (SQLSMALLINT)sizeof(message), &length);
        if (r == SQL_NO_DATA || !ok(r) || state[0] == '\0') break;
        if (err->len) buf_add(err, " | ");
        buf_addf(err, "[%s %ld] %s", (char *)state, (long)native, (char *)message);
        rec++;
    }
    if (err->len == 0) buf_add(err, "The ODBC driver reported a failure without details.");
}

static void db_disconnect(void) {
    if (dbc != SQL_NULL_HDBC) {
        SQLDisconnect(dbc);
        SQLFreeHandle(SQL_HANDLE_DBC, dbc);
        dbc = SQL_NULL_HDBC;
    }
    if (env != SQL_NULL_HENV) {
        SQLFreeHandle(SQL_HANDLE_ENV, env);
        env = SQL_NULL_HENV;
    }
    connected = 0;
    session_token[0] = '\0';
    session_user[0] = '\0';
    session_dsn[0] = '\0';
}

/* Connects either by DSN name or by a full ODBC connection string. */
static int db_connect(const char *dsn, const char *user, const char *pass,
                      const char *connstr, Buf *err) {
    SQLRETURN r;

    db_disconnect();

    if (!ok(SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &env))) {
        buf_add(err, "Could not allocate the ODBC environment handle.");
        return 0;
    }
    if (!ok(SQLSetEnvAttr(env, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3, 0))) {
        collect_error(SQL_HANDLE_ENV, env, err);
        db_disconnect();
        return 0;
    }
    if (!ok(SQLAllocHandle(SQL_HANDLE_DBC, env, &dbc))) {
        collect_error(SQL_HANDLE_ENV, env, err);
        db_disconnect();
        return 0;
    }

    if (connstr && connstr[0]) {
        SQLCHAR out[1024];
        SQLSMALLINT outlen = 0;
        r = SQLDriverConnect(dbc, NULL, (SQLCHAR *)connstr, SQL_NTS,
                             out, (SQLSMALLINT)sizeof(out), &outlen,
                             SQL_DRIVER_NOPROMPT);
    } else {
        r = SQLConnect(dbc, (SQLCHAR *)dsn, SQL_NTS,
                       (SQLCHAR *)user, SQL_NTS, (SQLCHAR *)pass, SQL_NTS);
    }

    if (!ok(r)) {
        collect_error(SQL_HANDLE_DBC, dbc, err);
        db_disconnect();
        return 0;
    }

    /* Keep transaction behavior explicit; each successful DML is committed. */
    SQLSetConnectAttr(dbc, SQL_ATTR_AUTOCOMMIT, (SQLPOINTER)SQL_AUTOCOMMIT_ON, 0);
    connected = 1;
    return 1;
}

/* ------------------------------------------------------------------ *
 * Result sets -> JSON
 * ------------------------------------------------------------------ */

static void append_columns(SQLHSTMT st, SQLSMALLINT ncols, Buf *out) {
    buf_add(out, "\"columns\":[");
    for (SQLSMALLINT c = 1; c <= ncols; c++) {
        SQLCHAR name[128] = "";
        SQLSMALLINT len = 0, type = 0, scale = 0, nullable = 0;
        SQLULEN size = 0;
        SQLDescribeCol(st, c, name, (SQLSMALLINT)sizeof(name), &len, &type,
                       &size, &scale, &nullable);
        if (c > 1) buf_add(out, ",");
        buf_json_str(out, (char *)name);
    }
    buf_add(out, "]");
}

/* Streams every row of an executed statement into out as {"columns":..,"rows":..}. */
static int append_rows(SQLHSTMT st, Buf *out, Buf *err) {
    SQLSMALLINT ncols = 0;
    SQLRETURN r;
    int rows = 0;

    SQLNumResultCols(st, &ncols);
    append_columns(st, ncols, out);
    buf_add(out, ",\"rows\":[");

    while ((r = SQLFetch(st)) != SQL_NO_DATA) {
        if (!ok(r)) {
            collect_error(SQL_HANDLE_STMT, st, err);
            buf_add(out, "]");
            return -1;
        }
        if (rows) buf_add(out, ",");
        buf_add(out, "[");
        for (SQLSMALLINT c = 1; c <= ncols; c++) {
            SQLCHAR value[2048] = "";
            SQLLEN indicator = 0;
            SQLRETURN gr = SQLGetData(st, c, SQL_C_CHAR, value,
                                      (SQLLEN)sizeof(value), &indicator);
            if (c > 1) buf_add(out, ",");
            if (indicator == SQL_NULL_DATA) buf_add(out, "null");
            else if (ok(gr)) buf_json_str(out, (char *)value);
            else buf_json_str(out, "[read error]");
        }
        buf_add(out, "]");
        rows++;
    }
    buf_add(out, "]");
    return rows;
}

/* Runs a fixed SELECT and writes the full JSON envelope into out. */
static int query_json(const char *sql, Buf *out, Buf *err) {
    SQLHSTMT st = SQL_NULL_HSTMT;

    if (!ok(SQLAllocHandle(SQL_HANDLE_STMT, dbc, &st))) {
        collect_error(SQL_HANDLE_DBC, dbc, err);
        return 0;
    }
    if (!ok(SQLExecDirect(st, (SQLCHAR *)sql, SQL_NTS))) {
        collect_error(SQL_HANDLE_STMT, st, err);
        SQLFreeHandle(SQL_HANDLE_STMT, st);
        return 0;
    }

    buf_add(out, "{\"ok\":true,");
    int rows = append_rows(st, out, err);
    SQLFreeHandle(SQL_HANDLE_STMT, st);
    if (rows < 0) {
        out->len = 0;
        if (out->p) out->p[0] = '\0';
        return 0;
    }
    buf_addf(out, ",\"rowCount\":%d}", rows);
    return 1;
}

/* Values are always bound parameters; empty text becomes SQL NULL. */
static int bind_values(SQLHSTMT st, char values[][VAL_LEN], SQLLEN *indicators,
                       int count, Buf *err) {
    for (int i = 0; i < count; i++) {
        indicators[i] = values[i][0] ? SQL_NTS : SQL_NULL_DATA;
        SQLRETURN r = SQLBindParameter(st, (SQLUSMALLINT)(i + 1), SQL_PARAM_INPUT,
                                       SQL_C_CHAR, SQL_VARCHAR, VAL_LEN - 1, 0,
                                       values[i], VAL_LEN, &indicators[i]);
        if (!ok(r)) {
            collect_error(SQL_HANDLE_STMT, st, err);
            return 0;
        }
    }
    return 1;
}

static int exec_prepared(const char *sql, char values[][VAL_LEN], int count,
                         SQLLEN *affected, Buf *err) {
    SQLHSTMT st = SQL_NULL_HSTMT;
    SQLLEN indicators[MAX_FIELDS + 1];

    if (!ok(SQLAllocHandle(SQL_HANDLE_STMT, dbc, &st))) {
        collect_error(SQL_HANDLE_DBC, dbc, err);
        return 0;
    }
    if (!ok(SQLPrepare(st, (SQLCHAR *)sql, SQL_NTS))) {
        collect_error(SQL_HANDLE_STMT, st, err);
        SQLFreeHandle(SQL_HANDLE_STMT, st);
        return 0;
    }
    if (!bind_values(st, values, indicators, count, err)) {
        SQLFreeHandle(SQL_HANDLE_STMT, st);
        return 0;
    }
    if (!ok(SQLExecute(st))) {
        collect_error(SQL_HANDLE_STMT, st, err);
        SQLFreeHandle(SQL_HANDLE_STMT, st);
        return 0;
    }
    if (affected) SQLRowCount(st, affected);
    SQLFreeHandle(SQL_HANDLE_STMT, st);
    return 1;
}

static int exec_ddl(const char *sql, Buf *err) {
    SQLHSTMT st = SQL_NULL_HSTMT;

    if (!ok(SQLAllocHandle(SQL_HANDLE_STMT, dbc, &st))) {
        collect_error(SQL_HANDLE_DBC, dbc, err);
        return 0;
    }
    if (!ok(SQLExecDirect(st, (SQLCHAR *)sql, SQL_NTS))) {
        collect_error(SQL_HANDLE_STMT, st, err);
        SQLFreeHandle(SQL_HANDLE_STMT, st);
        return 0;
    }
    SQLFreeHandle(SQL_HANDLE_STMT, st);
    return 1;
}

/* ------------------------------------------------------------------ *
 * SQL builders (identifiers come only from the entity table above)
 * ------------------------------------------------------------------ */

static int is_date_col(const Entity *e, int i) {
    return e->types[i] == 'D';
}

static void build_select_list(const Entity *e, Buf *sql) {
    for (int i = 0; i < e->n; i++) {
        if (i) buf_add(sql, ", ");
        if (is_date_col(e, i))
            buf_addf(sql, "TO_CHAR(%s, 'YYYY-MM-DD') AS %s", e->cols[i], e->cols[i]);
        else
            buf_add(sql, e->cols[i]);
    }
}

static void build_list_sql(const Entity *e, Buf *sql) {
    buf_add(sql, "SELECT ");
    build_select_list(e, sql);
    buf_addf(sql, " FROM %s ORDER BY %s", e->table, e->cols[0]);
}

static void build_param(const Entity *e, int i, Buf *sql) {
    if (e->types[i] == 'N')      buf_add(sql, "TO_NUMBER(?)");
    else if (e->types[i] == 'D') buf_add(sql, "TO_DATE(?, 'YYYY-MM-DD')");
    else                         buf_add(sql, "?");
}

static void build_insert_sql(const Entity *e, Buf *sql) {
    buf_addf(sql, "INSERT INTO %s (", e->table);
    for (int i = 0; i < e->n; i++) {
        if (i) buf_add(sql, ", ");
        buf_add(sql, e->cols[i]);
    }
    buf_add(sql, ") VALUES (");
    for (int i = 0; i < e->n; i++) {
        if (i) buf_add(sql, ", ");
        build_param(e, i, sql);
    }
    buf_add(sql, ")");
}

static void build_update_sql(const Entity *e, Buf *sql) {
    buf_addf(sql, "UPDATE %s SET ", e->table);
    for (int i = 1; i < e->n; i++) {
        if (i > 1) buf_add(sql, ", ");
        buf_addf(sql, "%s = ", e->cols[i]);
        build_param(e, i, sql);
    }
    buf_addf(sql, " WHERE %s = TO_NUMBER(?)", e->cols[0]);
}

/* ------------------------------------------------------------------ *
 * HTTP request parsing
 * ------------------------------------------------------------------ */

typedef struct {
    char key[64];
    char val[VAL_LEN];
} Param;

typedef struct {
    Param items[MAX_PARAMS];
    int n;
} Params;

typedef struct {
    char   method[8];
    char   path[512];
    char   cookie[256];
    Params params;
} Request;

static int hexval(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void url_decode(const char *src, size_t srclen, char *dst, size_t dstsize) {
    size_t o = 0;
    for (size_t i = 0; i < srclen && o + 1 < dstsize; i++) {
        if (src[i] == '+') {
            dst[o++] = ' ';
        } else if (src[i] == '%' && i + 2 < srclen) {
            int hi = hexval((unsigned char)src[i + 1]);
            int lo = hexval((unsigned char)src[i + 2]);
            if (hi >= 0 && lo >= 0) {
                dst[o++] = (char)((hi << 4) | lo);
                i += 2;
            } else {
                dst[o++] = src[i];
            }
        } else {
            dst[o++] = src[i];
        }
    }
    dst[o] = '\0';
}

/* Parses key=value&key=value into p, URL-decoding both sides. */
static void parse_params(const char *s, Params *p) {
    while (s && *s && p->n < MAX_PARAMS) {
        const char *amp = strchr(s, '&');
        const char *end = amp ? amp : s + strlen(s);
        const char *eq = memchr(s, '=', (size_t)(end - s));
        Param *it = &p->items[p->n];

        if (eq) {
            url_decode(s, (size_t)(eq - s), it->key, sizeof(it->key));
            url_decode(eq + 1, (size_t)(end - eq - 1), it->val, sizeof(it->val));
        } else {
            url_decode(s, (size_t)(end - s), it->key, sizeof(it->key));
            it->val[0] = '\0';
        }
        if (it->key[0]) p->n++;
        if (!amp) break;
        s = amp + 1;
    }
}

static const char *param_get(const Params *p, const char *key) {
    for (int i = 0; i < p->n; i++)
        if (strcmp(p->items[i].key, key) == 0) return p->items[i].val;
    return NULL;
}

static const char *param_or(const Params *p, const char *key, const char *fallback) {
    const char *v = param_get(p, key);
    return v ? v : fallback;
}

static int param_int(const Params *p, const char *key, int fallback) {
    const char *v = param_get(p, key);
    char *end;
    long n;
    if (!v || !*v) return fallback;
    n = strtol(v, &end, 10);
    if (*end) return fallback;
    return (int)n;
}

/* ------------------------------------------------------------------ *
 * HTTP responses
 * ------------------------------------------------------------------ */

static int send_all(SOCKET s, const char *data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        int n = send(s, data + sent, (int)(len - sent), 0);
        if (n <= 0) return 0;
        sent += (size_t)n;
    }
    return 1;
}

static void send_response(SOCKET s, int code, const char *status, const char *ctype,
                          const char *extra, const char *body, size_t len) {
    Buf head;
    buf_init(&head);
    buf_addf(&head, "HTTP/1.1 %d %s\r\n", code, status);
    buf_addf(&head, "Content-Type: %s\r\n", ctype);
    buf_addf(&head, "Content-Length: %lu\r\n", (unsigned long)len);
    buf_add(&head, "Cache-Control: no-store\r\n");
    buf_add(&head, "X-Content-Type-Options: nosniff\r\n");
    buf_add(&head, "Connection: close\r\n");
    if (extra && *extra) buf_add(&head, extra);
    buf_add(&head, "\r\n");
    send_all(s, head.p, head.len);
    if (len) send_all(s, body, len);
    buf_free(&head);
}

static void send_json(SOCKET s, int code, const char *status, const char *json,
                      const char *extra) {
    send_response(s, code, status, "application/json; charset=utf-8", extra,
                  json, strlen(json));
}

static void send_json_buf(SOCKET s, Buf *b) {
    send_json(s, 200, "OK", b->p ? b->p : "{}", NULL);
}

static void send_error(SOCKET s, int code, const char *status, const char *message) {
    Buf b;
    buf_init(&b);
    buf_add(&b, "{\"ok\":false,\"error\":");
    buf_json_str(&b, message);
    buf_add(&b, "}");
    send_json(s, code, status, b.p ? b.p : "{\"ok\":false}", NULL);
    buf_free(&b);
}

static void send_error_buf(SOCKET s, int code, const char *status, Buf *err) {
    send_error(s, code, status, err->len ? err->p : "Unknown database error.");
}

/* ------------------------------------------------------------------ *
 * Session token
 * ------------------------------------------------------------------ */

static void make_token(char *out) {
    static const char hex[] = "0123456789abcdef";
    unsigned char raw[16];
    HCRYPTPROV prov = 0;
    int got = 0;

    if (CryptAcquireContext(&prov, NULL, NULL, PROV_RSA_FULL,
                            CRYPT_VERIFYCONTEXT | CRYPT_SILENT)) {
        got = CryptGenRandom(prov, sizeof(raw), raw) ? 1 : 0;
        CryptReleaseContext(prov, 0);
    }
    if (!got) {
        /* Fallback: mix clocks and the process id. */
        LARGE_INTEGER pc;
        QueryPerformanceCounter(&pc);
        unsigned long long mix = (unsigned long long)pc.QuadPart
                               ^ ((unsigned long long)GetTickCount64() << 17)
                               ^ ((unsigned long long)GetCurrentProcessId() << 33);
        for (size_t i = 0; i < sizeof(raw); i++) {
            mix = mix * 6364136223846793005ULL + 1442695040888963407ULL;
            raw[i] = (unsigned char)(mix >> 33);
        }
    }
    for (size_t i = 0; i < sizeof(raw); i++) {
        out[i * 2]     = hex[raw[i] >> 4];
        out[i * 2 + 1] = hex[raw[i] & 0x0f];
    }
    out[sizeof(raw) * 2] = '\0';
}

/* Constant-time-ish comparison of the cookie token against the session token. */
static int token_valid(const char *cookie) {
    char found[TOKEN_LEN] = "";
    const char *p = cookie;

    if (!connected || !session_token[0]) return 0;
    while (p && *p) {
        while (*p == ' ' || *p == ';') p++;
        if (strncmp(p, "mdbms_token=", 12) == 0) {
            p += 12;
            size_t i = 0;
            while (p[i] && p[i] != ';' && i + 1 < sizeof(found)) {
                found[i] = p[i];
                i++;
            }
            found[i] = '\0';
            break;
        }
        p = strchr(p, ';');
    }
    if (!found[0]) return 0;
    if (strlen(found) != strlen(session_token)) return 0;
    unsigned char diff = 0;
    for (size_t i = 0; session_token[i]; i++)
        diff |= (unsigned char)(found[i] ^ session_token[i]);
    return diff == 0;
}

/* ------------------------------------------------------------------ *
 * Static files
 * ------------------------------------------------------------------ */

static char www_root[MAX_PATH] = "";

static void resolve_www_root(void) {
    char exe[MAX_PATH];
    DWORD n = GetModuleFileNameA(NULL, exe, sizeof(exe));
    if (n == 0 || n >= sizeof(exe)) {
        strcpy(www_root, "www");
        return;
    }
    char *slash = strrchr(exe, '\\');
    if (slash) *slash = '\0';

    size_t dir = strlen(exe);
    if (dir + 5 > sizeof(www_root)) {   /* "\www" plus the terminator */
        strcpy(www_root, "www");
        return;
    }
    memcpy(www_root, exe, dir);
    memcpy(www_root + dir, "\\www", 5);
}

static const char *mime_for(const char *path) {
    const char *dot = strrchr(path, '.');
    if (!dot) return "application/octet-stream";
    if (_stricmp(dot, ".html") == 0) return "text/html; charset=utf-8";
    if (_stricmp(dot, ".css") == 0)  return "text/css; charset=utf-8";
    if (_stricmp(dot, ".js") == 0)   return "application/javascript; charset=utf-8";
    if (_stricmp(dot, ".json") == 0) return "application/json; charset=utf-8";
    if (_stricmp(dot, ".svg") == 0)  return "image/svg+xml";
    if (_stricmp(dot, ".png") == 0)  return "image/png";
    if (_stricmp(dot, ".ico") == 0)  return "image/x-icon";
    return "application/octet-stream";
}

static void serve_static(SOCKET s, const char *path) {
    char rel[512];
    char full[MAX_PATH];

    if (strcmp(path, "/") == 0) {
        strcpy(rel, "index.html");
    } else {
        size_t n = strlen(path + 1);
        if (n == 0 || n >= sizeof(rel)) {
            send_error(s, 404, "Not Found", "File not found in the www folder.");
            return;
        }
        memcpy(rel, path + 1, n + 1);
    }

    /* Reject anything that could escape www/. */
    if (strstr(rel, "..") || strchr(rel, ':') || rel[0] == '\\' || rel[0] == '/') {
        send_error(s, 403, "Forbidden", "Path not allowed.");
        return;
    }
    for (char *q = rel; *q; q++) if (*q == '/') *q = '\\';

    size_t rootlen = strlen(www_root);
    size_t rellen = strlen(rel);
    if (rootlen + 1 + rellen + 1 > sizeof(full)) {
        send_error(s, 404, "Not Found", "File path is too long.");
        return;
    }
    memcpy(full, www_root, rootlen);
    full[rootlen] = '\\';
    memcpy(full + rootlen + 1, rel, rellen + 1);

    FILE *f = fopen(full, "rb");
    if (!f) {
        send_error(s, 404, "Not Found", "File not found in the www folder.");
        return;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0) size = 0;

    char *body = (char *)malloc((size_t)size + 1);
    if (!body) {
        fclose(f);
        send_error(s, 500, "Internal Server Error", "Out of memory reading the file.");
        return;
    }
    size_t got = fread(body, 1, (size_t)size, f);
    fclose(f);
    send_response(s, 200, "OK", mime_for(rel), NULL, body, got);
    free(body);
}

/* ------------------------------------------------------------------ *
 * API handlers
 * ------------------------------------------------------------------ */

static void api_meta(SOCKET s) {
    Buf b;
    buf_init(&b);
    buf_add(&b, "{\"ok\":true,\"entities\":[");
    for (int i = 0; i < ENTITY_COUNT; i++) {
        Entity *e = &entities[i];
        if (i) buf_add(&b, ",");
        buf_add(&b, "{\"index\":");
        buf_addf(&b, "%d,\"title\":", i);
        buf_json_str(&b, e->title);
        buf_add(&b, ",\"table\":");
        buf_json_str(&b, e->table);
        buf_add(&b, ",\"searchColumn\":");
        buf_json_str(&b, e->search_col);
        buf_add(&b, ",\"fields\":[");
        for (int j = 0; j < e->n; j++) {
            if (j) buf_add(&b, ",");
            buf_add(&b, "{\"column\":");
            buf_json_str(&b, e->cols[j]);
            buf_add(&b, ",\"label\":");
            buf_json_str(&b, e->labels[j]);
            buf_addf(&b, ",\"type\":\"%c\"}", e->types[j]);
        }
        buf_add(&b, "]}");
    }
    buf_add(&b, "],\"reports\":[");
    for (int i = 0; i < REPORT_COUNT; i++) {
        if (i) buf_add(&b, ",");
        buf_addf(&b, "{\"index\":%d,\"title\":", i);
        buf_json_str(&b, reports[i].title);
        buf_add(&b, ",\"description\":");
        buf_json_str(&b, reports[i].description);
        buf_add(&b, "}");
    }
    buf_add(&b, "],\"ddl\":[");
    for (int i = 0; i < DDL_COUNT; i++) {
        if (i) buf_add(&b, ",");
        buf_addf(&b, "{\"index\":%d,\"title\":", i);
        buf_json_str(&b, ddlops[i].title);
        buf_add(&b, ",\"description\":");
        buf_json_str(&b, ddlops[i].description);
        buf_add(&b, ",\"sql\":");
        buf_json_str(&b, ddlops[i].sql);
        buf_addf(&b, ",\"isQuery\":%s,\"destructive\":%s}",
                 ddlops[i].is_query ? "true" : "false",
                 ddlops[i].destructive ? "true" : "false");
    }
    buf_add(&b, "]}");
    send_json_buf(s, &b);
    buf_free(&b);
}

static void api_status(SOCKET s, Request *req) {
    Buf b;
    buf_init(&b);
    int auth = token_valid(req->cookie);
    buf_addf(&b, "{\"ok\":true,\"connected\":%s,\"user\":", auth ? "true" : "false");
    buf_json_str(&b, auth ? session_user : "");
    buf_add(&b, ",\"dsn\":");
    buf_json_str(&b, auth ? session_dsn : "");
    buf_add(&b, "}");
    send_json_buf(s, &b);
    buf_free(&b);
}

/* Lists the ODBC DSNs and drivers installed on this machine. */
static void api_datasources(SOCKET s) {
    SQLHENV e2 = SQL_NULL_HENV;
    Buf b;

    buf_init(&b);
    buf_add(&b, "{\"ok\":true,\"dsns\":[");

    if (ok(SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &e2)) &&
        ok(SQLSetEnvAttr(e2, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3, 0))) {
        SQLCHAR dsn[256], desc[256];
        SQLSMALLINT dlen = 0, dslen = 0;
        SQLUSMALLINT dir = SQL_FETCH_FIRST;
        int first = 1;
        while (ok(SQLDataSources(e2, dir, dsn, (SQLSMALLINT)sizeof(dsn), &dlen,
                                 desc, (SQLSMALLINT)sizeof(desc), &dslen))) {
            if (!first) buf_add(&b, ",");
            buf_add(&b, "{\"name\":");
            buf_json_str(&b, (char *)dsn);
            buf_add(&b, ",\"driver\":");
            buf_json_str(&b, (char *)desc);
            buf_add(&b, "}");
            first = 0;
            dir = SQL_FETCH_NEXT;
        }

        buf_add(&b, "],\"drivers\":[");
        SQLCHAR drv[512], attrs[512];
        SQLSMALLINT alen = 0, atlen = 0;
        dir = SQL_FETCH_FIRST;
        first = 1;
        while (ok(SQLDrivers(e2, dir, drv, (SQLSMALLINT)sizeof(drv), &alen,
                             attrs, (SQLSMALLINT)sizeof(attrs), &atlen))) {
            if (!first) buf_add(&b, ",");
            buf_json_str(&b, (char *)drv);
            first = 0;
            dir = SQL_FETCH_NEXT;
        }
        buf_add(&b, "]}");
        SQLFreeHandle(SQL_HANDLE_ENV, e2);
    } else {
        buf_add(&b, "],\"drivers\":[]}");
        if (e2 != SQL_NULL_HENV) SQLFreeHandle(SQL_HANDLE_ENV, e2);
    }

    send_json_buf(s, &b);
    buf_free(&b);
}

static void api_connect(SOCKET s, Request *req) {
    const char *dsn     = param_or(&req->params, "dsn", "");
    const char *user    = param_or(&req->params, "user", "");
    const char *pass    = param_or(&req->params, "password", "");
    const char *connstr = param_or(&req->params, "connstr", "");
    Buf err, out;
    char extra[160];

    if (!connstr[0] && !dsn[0]) {
        send_error(s, 400, "Bad Request",
                   "Enter an ODBC DSN name, or switch to a full connection string.");
        return;
    }

    buf_init(&err);
    EnterCriticalSection(&db_lock);
    int good = db_connect(dsn, user, pass, connstr, &err);
    if (good) {
        make_token(session_token);
        snprintf(session_user, sizeof(session_user), "%s", user);
        snprintf(session_dsn, sizeof(session_dsn), "%s", connstr[0] ? "(connection string)" : dsn);
    }
    LeaveCriticalSection(&db_lock);

    if (!good) {
        send_error(s, 502, "Bad Gateway", err.len ? err.p : "Connection failed.");
        buf_free(&err);
        return;
    }
    buf_free(&err);

    /* HttpOnly keeps the token out of page scripts; the server is localhost-only. */
    snprintf(extra, sizeof(extra),
             "Set-Cookie: mdbms_token=%s; Path=/; HttpOnly; SameSite=Strict\r\n",
             session_token);

    buf_init(&out);
    buf_add(&out, "{\"ok\":true,\"user\":");
    buf_json_str(&out, session_user);
    buf_add(&out, ",\"dsn\":");
    buf_json_str(&out, session_dsn);
    buf_add(&out, "}");
    send_json(s, 200, "OK", out.p, extra);
    buf_free(&out);
}

static void api_disconnect(SOCKET s) {
    EnterCriticalSection(&db_lock);
    db_disconnect();
    LeaveCriticalSection(&db_lock);
    send_json(s, 200, "OK", "{\"ok\":true}",
              "Set-Cookie: mdbms_token=; Path=/; HttpOnly; SameSite=Strict; Max-Age=0\r\n");
}

static void api_list(SOCKET s, Request *req) {
    int idx = param_int(&req->params, "entity", -1);
    Buf sql, out, err;

    if (idx < 0 || idx >= ENTITY_COUNT) {
        send_error(s, 400, "Bad Request", "Unknown entity.");
        return;
    }
    buf_init(&sql);
    buf_init(&out);
    buf_init(&err);
    build_list_sql(&entities[idx], &sql);

    EnterCriticalSection(&db_lock);
    int good = query_json(sql.p, &out, &err);
    LeaveCriticalSection(&db_lock);

    if (good) send_json_buf(s, &out);
    else send_error_buf(s, 502, "Bad Gateway", &err);

    buf_free(&sql);
    buf_free(&out);
    buf_free(&err);
}

static void api_search(SOCKET s, Request *req) {
    int idx = param_int(&req->params, "entity", -1);
    const char *q = param_or(&req->params, "q", "");
    Buf sql, out, err;
    char values[1][VAL_LEN];
    SQLLEN indicators[1];
    SQLHSTMT st = SQL_NULL_HSTMT;

    if (idx < 0 || idx >= ENTITY_COUNT) {
        send_error(s, 400, "Bad Request", "Unknown entity.");
        return;
    }
    Entity *e = &entities[idx];

    snprintf(values[0], VAL_LEN, "%s", q);
    buf_init(&sql);
    buf_init(&out);
    buf_init(&err);

    buf_add(&sql, "SELECT ");
    build_select_list(e, &sql);
    buf_addf(&sql, " FROM %s WHERE UPPER(TO_CHAR(%s)) LIKE '%%' || UPPER(?) || '%%' ORDER BY %s",
             e->table, e->search_col, e->cols[0]);

    EnterCriticalSection(&db_lock);
    int good = 0;
    if (!ok(SQLAllocHandle(SQL_HANDLE_STMT, dbc, &st))) {
        collect_error(SQL_HANDLE_DBC, dbc, &err);
    } else if (!ok(SQLPrepare(st, (SQLCHAR *)sql.p, SQL_NTS))) {
        collect_error(SQL_HANDLE_STMT, st, &err);
    } else {
        /* An empty search term must still bind as text so LIKE '%%' matches all. */
        indicators[0] = SQL_NTS;
        if (!ok(SQLBindParameter(st, 1, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_VARCHAR,
                                 VAL_LEN - 1, 0, values[0], VAL_LEN, &indicators[0])))
            collect_error(SQL_HANDLE_STMT, st, &err);
        else if (!ok(SQLExecute(st)))
            collect_error(SQL_HANDLE_STMT, st, &err);
        else {
            buf_add(&out, "{\"ok\":true,");
            int rows = append_rows(st, &out, &err);
            if (rows >= 0) {
                buf_addf(&out, ",\"rowCount\":%d}", rows);
                good = 1;
            }
        }
    }
    if (st != SQL_NULL_HSTMT) SQLFreeHandle(SQL_HANDLE_STMT, st);
    LeaveCriticalSection(&db_lock);

    if (good) send_json_buf(s, &out);
    else send_error_buf(s, 502, "Bad Gateway", &err);

    buf_free(&sql);
    buf_free(&out);
    buf_free(&err);
}

static void api_insert(SOCKET s, Request *req) {
    int idx = param_int(&req->params, "entity", -1);
    char values[MAX_FIELDS][VAL_LEN];
    Buf sql, err, out;
    SQLLEN affected = 0;

    if (idx < 0 || idx >= ENTITY_COUNT) {
        send_error(s, 400, "Bad Request", "Unknown entity.");
        return;
    }
    Entity *e = &entities[idx];

    memset(values, 0, sizeof(values));
    for (int i = 0; i < e->n; i++) {
        char key[16];
        snprintf(key, sizeof(key), "v%d", i);
        snprintf(values[i], VAL_LEN, "%s", param_or(&req->params, key, ""));
    }
    if (!values[0][0]) {
        send_error(s, 400, "Bad Request", "The primary key value is required.");
        return;
    }

    buf_init(&sql);
    buf_init(&err);
    build_insert_sql(e, &sql);

    EnterCriticalSection(&db_lock);
    int good = exec_prepared(sql.p, values, e->n, &affected, &err);
    LeaveCriticalSection(&db_lock);

    if (good) {
        buf_init(&out);
        buf_addf(&out, "{\"ok\":true,\"rowsAffected\":%ld,\"message\":\"Record inserted.\"}",
                 (long)affected);
        send_json_buf(s, &out);
        buf_free(&out);
    } else {
        send_error_buf(s, 502, "Bad Gateway", &err);
    }
    buf_free(&sql);
    buf_free(&err);
}

static void api_update(SOCKET s, Request *req) {
    int idx = param_int(&req->params, "entity", -1);
    char values[MAX_FIELDS][VAL_LEN];
    Buf sql, err, out;
    SQLLEN affected = 0;

    if (idx < 0 || idx >= ENTITY_COUNT) {
        send_error(s, 400, "Bad Request", "Unknown entity.");
        return;
    }
    Entity *e = &entities[idx];

    memset(values, 0, sizeof(values));
    /* Parameters 1..n-1 are the updated columns; the last one is the key. */
    for (int i = 1; i < e->n; i++) {
        char key[16];
        snprintf(key, sizeof(key), "v%d", i);
        snprintf(values[i - 1], VAL_LEN, "%s", param_or(&req->params, key, ""));
    }
    snprintf(values[e->n - 1], VAL_LEN, "%s", param_or(&req->params, "id", ""));
    if (!values[e->n - 1][0]) {
        send_error(s, 400, "Bad Request", "The record id is required.");
        return;
    }

    buf_init(&sql);
    buf_init(&err);
    build_update_sql(e, &sql);

    EnterCriticalSection(&db_lock);
    int good = exec_prepared(sql.p, values, e->n, &affected, &err);
    LeaveCriticalSection(&db_lock);

    if (good) {
        buf_init(&out);
        buf_addf(&out, "{\"ok\":true,\"rowsAffected\":%ld,\"message\":%s}", (long)affected,
                 affected > 0 ? "\"Record updated.\""
                              : "\"No record matched that id. Nothing was changed.\"");
        send_json_buf(s, &out);
        buf_free(&out);
    } else {
        send_error_buf(s, 502, "Bad Gateway", &err);
    }
    buf_free(&sql);
    buf_free(&err);
}

static void api_delete(SOCKET s, Request *req) {
    int idx = param_int(&req->params, "entity", -1);
    const char *id = param_or(&req->params, "id", "");
    const char *confirm = param_or(&req->params, "confirm", "");
    char values[MAX_FIELDS][VAL_LEN];
    char sql[512];
    Buf err, out;
    SQLLEN affected = 0;

    if (idx < 0 || idx >= ENTITY_COUNT) {
        send_error(s, 400, "Bad Request", "Unknown entity.");
        return;
    }
    if (!id[0]) {
        send_error(s, 400, "Bad Request", "The record id is required.");
        return;
    }
    if (strcmp(confirm, "YES") != 0) {
        send_error(s, 400, "Bad Request",
                   "Delete was not confirmed. This permanently removes the record.");
        return;
    }
    Entity *e = &entities[idx];

    memset(values, 0, sizeof(values));
    snprintf(values[0], VAL_LEN, "%s", id);
    snprintf(sql, sizeof(sql), "DELETE FROM %s WHERE %s = TO_NUMBER(?)",
             e->table, e->cols[0]);

    buf_init(&err);
    EnterCriticalSection(&db_lock);
    int good = exec_prepared(sql, values, 1, &affected, &err);
    LeaveCriticalSection(&db_lock);

    if (good) {
        buf_init(&out);
        buf_addf(&out, "{\"ok\":true,\"rowsAffected\":%ld,\"message\":%s}", (long)affected,
                 affected > 0 ? "\"Record deleted.\""
                              : "\"No record matched that id. Nothing was deleted.\"");
        send_json_buf(s, &out);
        buf_free(&out);
    } else {
        send_error_buf(s, 502, "Bad Gateway", &err);
    }
    buf_free(&err);
}

static void api_report(SOCKET s, Request *req) {
    int idx = param_int(&req->params, "report", -1);
    Buf out, err;

    if (idx < 0 || idx >= REPORT_COUNT) {
        send_error(s, 400, "Bad Request", "Unknown report.");
        return;
    }
    buf_init(&out);
    buf_init(&err);

    EnterCriticalSection(&db_lock);
    int good = query_json(reports[idx].sql, &out, &err);
    LeaveCriticalSection(&db_lock);

    if (good) send_json_buf(s, &out);
    else send_error_buf(s, 502, "Bad Gateway", &err);

    buf_free(&out);
    buf_free(&err);
}

static void api_ddl(SOCKET s, Request *req) {
    int idx = param_int(&req->params, "op", -1);
    const char *confirm = param_or(&req->params, "confirm", "");
    Buf out, err;

    if (idx < 0 || idx >= DDL_COUNT) {
        send_error(s, 400, "Bad Request", "Unknown DDL operation.");
        return;
    }
    DdlOp *op = &ddlops[idx];
    if (op->destructive && strcmp(confirm, "DROP") != 0) {
        send_error(s, 400, "Bad Request",
                   "Type DROP to confirm. This permanently removes the table.");
        return;
    }

    buf_init(&out);
    buf_init(&err);

    EnterCriticalSection(&db_lock);
    int good;
    if (op->is_query) {
        good = query_json(op->sql, &out, &err);
    } else {
        good = exec_ddl(op->sql, &err);
        if (good) buf_add(&out, "{\"ok\":true,\"message\":\"DDL statement executed successfully.\"}");
    }
    LeaveCriticalSection(&db_lock);

    if (good) send_json_buf(s, &out);
    else send_error_buf(s, 502, "Bad Gateway", &err);

    buf_free(&out);
    buf_free(&err);
}

static void api_dashboard(SOCKET s) {
    Buf out, err;
    buf_init(&out);
    buf_init(&err);

    EnterCriticalSection(&db_lock);
    int good = query_json(
        "SELECT (SELECT COUNT(*) FROM MINE_SITE) MINE_SITES, "
        "(SELECT COUNT(*) FROM MINE_SITE WHERE UPPER(STATUS)='ACTIVE') ACTIVE_MINES, "
        "(SELECT COUNT(*) FROM MINERAL) MINERALS, "
        "(SELECT COUNT(*) FROM MINER) MINERS, "
        "(SELECT COUNT(*) FROM EQUIPMENT) EQUIPMENT, "
        "(SELECT COUNT(*) FROM PRODUCTION_RECORD) PRODUCTION_RECORDS, "
        "(SELECT COUNT(*) FROM SAFETY_INSPECTION) SAFETY_INSPECTIONS, "
        "(SELECT NVL(SUM(TONNES_PRODUCED),0) FROM PRODUCTION_RECORD) TOTAL_TONNES, "
        "(SELECT NVL(SUM(PURCHASE_COST),0) FROM EQUIPMENT) EQUIPMENT_VALUE "
        "FROM DUAL", &out, &err);
    LeaveCriticalSection(&db_lock);

    if (good) send_json_buf(s, &out);
    else send_error_buf(s, 502, "Bad Gateway", &err);

    buf_free(&out);
    buf_free(&err);
}

/* ------------------------------------------------------------------ *
 * Routing
 * ------------------------------------------------------------------ */

static void route(SOCKET s, Request *req) {
    const char *path = req->path;
    int is_post = strcmp(req->method, "POST") == 0;

    if (strncmp(path, "/api/", 5) != 0) {
        if (strcmp(req->method, "GET") != 0) {
            send_error(s, 405, "Method Not Allowed", "Use GET for page resources.");
            return;
        }
        serve_static(s, path);
        return;
    }

    /* Open endpoints: needed before a session exists. */
    if (strcmp(path, "/api/status") == 0)      { api_status(s, req); return; }
    if (strcmp(path, "/api/meta") == 0)        { api_meta(s); return; }
    if (strcmp(path, "/api/datasources") == 0) { api_datasources(s); return; }
    if (strcmp(path, "/api/connect") == 0) {
        if (!is_post) { send_error(s, 405, "Method Not Allowed", "Use POST to connect."); return; }
        api_connect(s, req);
        return;
    }

    /* Everything else needs a live session. */
    if (!token_valid(req->cookie)) {
        send_error(s, 401, "Unauthorized", "Not connected. Sign in to the database first.");
        return;
    }

    if (strcmp(path, "/api/disconnect") == 0) {
        if (!is_post) { send_error(s, 405, "Method Not Allowed", "Use POST to disconnect."); return; }
        api_disconnect(s);
        return;
    }
    if (strcmp(path, "/api/dashboard") == 0) { api_dashboard(s); return; }
    if (strcmp(path, "/api/list") == 0)      { api_list(s, req); return; }
    if (strcmp(path, "/api/search") == 0)    { api_search(s, req); return; }
    if (strcmp(path, "/api/report") == 0)    { api_report(s, req); return; }

    /* State-changing endpoints are POST only. */
    if (!is_post) {
        if (strcmp(path, "/api/insert") == 0 || strcmp(path, "/api/update") == 0 ||
            strcmp(path, "/api/delete") == 0 || strcmp(path, "/api/ddl") == 0) {
            send_error(s, 405, "Method Not Allowed", "This operation requires POST.");
            return;
        }
    } else {
        if (strcmp(path, "/api/insert") == 0) { api_insert(s, req); return; }
        if (strcmp(path, "/api/update") == 0) { api_update(s, req); return; }
        if (strcmp(path, "/api/delete") == 0) { api_delete(s, req); return; }
        if (strcmp(path, "/api/ddl") == 0)    { api_ddl(s, req); return; }
    }

    send_error(s, 404, "Not Found", "Unknown API endpoint.");
}

/* ------------------------------------------------------------------ *
 * Connection handling
 * ------------------------------------------------------------------ */

#define REQ_MAX (64 * 1024)

/* Reads headers, then the body, and fills req. Returns 0 on a bad request. */
static int read_request(SOCKET s, Request *req, Buf *raw) {
    size_t header_end = 0;
    long content_length = 0;

    memset(req, 0, sizeof(*req));

    for (;;) {
        if (raw->len) {
            char *brk = strstr(raw->p, "\r\n\r\n");
            if (brk) { header_end = (size_t)(brk - raw->p) + 4; break; }
        }
        if (raw->len > REQ_MAX) return 0;
        char chunk[4096];
        int n = recv(s, chunk, sizeof(chunk), 0);
        if (n <= 0) return 0;
        buf_add_n(raw, chunk, (size_t)n);
    }

    /* Request line. */
    char line[1024];
    const char *eol = strstr(raw->p, "\r\n");
    size_t llen = eol ? (size_t)(eol - raw->p) : 0;
    if (llen == 0 || llen >= sizeof(line)) return 0;
    memcpy(line, raw->p, llen);
    line[llen] = '\0';

    char *sp1 = strchr(line, ' ');
    if (!sp1) return 0;
    *sp1 = '\0';
    char *target = sp1 + 1;
    char *sp2 = strchr(target, ' ');
    if (sp2) *sp2 = '\0';

    size_t mlen = strlen(line);
    if (mlen == 0 || mlen >= sizeof(req->method)) return 0;
    memcpy(req->method, line, mlen + 1);

    char *qmark = strchr(target, '?');
    if (qmark) {
        *qmark = '\0';
        parse_params(qmark + 1, &req->params);
    }
    char decoded[512];
    url_decode(target, strlen(target), decoded, sizeof(decoded));
    snprintf(req->path, sizeof(req->path), "%s", decoded);

    /* Headers we care about. */
    const char *h = eol + 2;
    const char *hend = raw->p + header_end;
    while (h < hend) {
        const char *nl = strstr(h, "\r\n");
        if (!nl || nl == h) break;
        size_t hl = (size_t)(nl - h);
        if (hl >= 15 && _strnicmp(h, "Content-Length:", 15) == 0) {
            content_length = strtol(h + 15, NULL, 10);
        } else if (hl >= 7 && _strnicmp(h, "Cookie:", 7) == 0) {
            const char *v = h + 7;
            while (*v == ' ') v++;
            size_t vl = (size_t)(nl - v);
            if (vl >= sizeof(req->cookie)) vl = sizeof(req->cookie) - 1;
            memcpy(req->cookie, v, vl);
            req->cookie[vl] = '\0';
        }
        h = nl + 2;
    }

    if (content_length < 0 || content_length > REQ_MAX) return 0;

    /* Body. */
    while (raw->len < header_end + (size_t)content_length) {
        char chunk[4096];
        int n = recv(s, chunk, sizeof(chunk), 0);
        if (n <= 0) break;
        buf_add_n(raw, chunk, (size_t)n);
    }
    if (content_length > 0 && raw->len >= header_end + (size_t)content_length) {
        char *body = (char *)malloc((size_t)content_length + 1);
        if (body) {
            memcpy(body, raw->p + header_end, (size_t)content_length);
            body[content_length] = '\0';
            parse_params(body, &req->params);
            free(body);
        }
    }
    return 1;
}

static DWORD WINAPI handle_client(LPVOID arg) {
    SOCKET s = (SOCKET)(ULONG_PTR)arg;
    Request req;
    Buf raw;

    buf_init(&raw);
    if (read_request(s, &req, &raw)) {
        printf("  %-6s %s\n", req.method, req.path);
        fflush(stdout);
        route(s, &req);
    } else {
        send_error(s, 400, "Bad Request", "The request could not be parsed.");
    }
    buf_free(&raw);

    shutdown(s, SD_SEND);
    closesocket(s);
    return 0;
}

/* ------------------------------------------------------------------ *
 * main
 * ------------------------------------------------------------------ */

int main(int argc, char **argv) {
    WSADATA wsa;
    SOCKET listener;
    struct sockaddr_in addr;
    int port = DEFAULT_PORT;
    int reuse = 1;

    if (argc > 1) {
        int p = atoi(argv[1]);
        if (p > 0 && p < 65536) port = p;
        else {
            printf("Invalid port '%s'. Using %d.\n", argv[1], port);
        }
    }

    resolve_www_root();
    InitializeCriticalSection(&db_lock);

    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        printf("WSAStartup failed: %d\n", WSAGetLastError());
        return 1;
    }
    listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == INVALID_SOCKET) {
        printf("Could not create the listening socket: %d\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof(reuse));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((unsigned short)port);
    /* Loopback only: this is a classroom application, not a public service. */
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (bind(listener, (struct sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR) {
        printf("Could not bind to port %d: %d\n", port, WSAGetLastError());
        printf("Another program may already be using it. Try: miningweb.exe 8091\n");
        closesocket(listener);
        WSACleanup();
        return 1;
    }
    if (listen(listener, SOMAXCONN) == SOCKET_ERROR) {
        printf("listen() failed: %d\n", WSAGetLastError());
        closesocket(listener);
        WSACleanup();
        return 1;
    }

    printf("===== MINING DATABASE MANAGEMENT SYSTEM (WEB) =====\n");
    printf("Serving files from : %s\n", www_root);
    printf("Open in a browser  : http://127.0.0.1:%d\n", port);
    printf("Stop the server    : press Ctrl+C in this window\n\n");

    for (;;) {
        SOCKET client = accept(listener, NULL, NULL);
        if (client == INVALID_SOCKET) continue;

        DWORD timeout = 15000;
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof(timeout));
        setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, (const char *)&timeout, sizeof(timeout));

        HANDLE t = CreateThread(NULL, 0, handle_client, (LPVOID)(ULONG_PTR)client, 0, NULL);
        if (t) CloseHandle(t);
        else handle_client((LPVOID)(ULONG_PTR)client);
    }

    /* Not reached: the loop above runs until the process is stopped. */
}
