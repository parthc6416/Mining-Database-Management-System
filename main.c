#include <windows.h>
#include <sql.h>
#include <sqlext.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAX_FIELDS 6
#define INPUT_LEN 256

static SQLHENV env = SQL_NULL_HENV;
static SQLHDBC dbc = SQL_NULL_HDBC;

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
     {"Record ID", "Mine ID", "Mineral ID", "Production date (YYYY-MM-DD)", "Tonnes produced"},
     "NNNDN", "MINE_ID", 5},
    {"Safety Inspections", "SAFETY_INSPECTION",
     {"INSPECTION_ID", "MINE_ID", "INSPECTION_DATE", "INSPECTOR_NAME", "RESULT", "NOTES"},
     {"Inspection ID", "Mine ID", "Inspection date (YYYY-MM-DD)", "Inspector name", "Result", "Notes"},
     "NNDTTT", "INSPECTOR_NAME", 6}
};
#define ENTITY_COUNT ((int)(sizeof(entities) / sizeof(entities[0])))

static int ok(SQLRETURN r) {
    return r == SQL_SUCCESS || r == SQL_SUCCESS_WITH_INFO;
}

static void showError(SQLSMALLINT type, SQLHANDLE handle) {
    SQLCHAR state[6] = "";
    SQLCHAR message[1024] = "";
    SQLINTEGER native = 0;
    SQLSMALLINT length = 0, rec = 1;
    while (handle != SQL_NULL_HANDLE &&
           SQLGetDiagRec(type, handle, rec, state, &native, message,
                         (SQLSMALLINT)sizeof(message), &length) != SQL_NO_DATA) {
        if (state[0] == '\0') break;
        printf("ODBC error [%s, %ld]: %s\n", state, (long)native, message);
        rec++;
    }
}

static void readLine(const char *prompt, char *out, size_t size) {
    printf("%s: ", prompt);
    if (!fgets(out, (int)size, stdin)) {
        out[0] = '\0';
        return;
    }
    size_t n = strlen(out);
    if (n && out[n - 1] == '\n') out[n - 1] = '\0';
    else {
        int ch;
        while ((ch = getchar()) != '\n' && ch != EOF) {}
    }
    n = strlen(out);
    if (n && out[n - 1] == '\r') out[n - 1] = '\0';
}

static int readChoice(const char *prompt) {
    char s[40], *end;
    long value;
    readLine(prompt, s, sizeof(s));
    value = strtol(s, &end, 10);
    while (*end && isspace((unsigned char)*end)) end++;
    if (s == end || *end != '\0') return -1;
    return (int)value;
}

static void waitEnter(void) {
    char line[8];
    printf("\nPress Enter to continue...");
    fgets(line, sizeof(line), stdin);
}

static int connectDB(void) {
    char dsn[128], user[128], pass[128];
    SQLRETURN r;

    if (!ok(SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &env))) {
        printf("Could not allocate ODBC environment.\n");
        return 0;
    }
    if (!ok(SQLSetEnvAttr(env, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3, 0))) {
        showError(SQL_HANDLE_ENV, env);
        return 0;
    }
    if (!ok(SQLAllocHandle(SQL_HANDLE_DBC, env, &dbc))) {
        showError(SQL_HANDLE_ENV, env);
        return 0;
    }

    printf("===== MINING DATABASE MANAGEMENT SYSTEM =====\n");
    readLine("ODBC DSN (for example MDBMS_ORACLE)", dsn, sizeof(dsn));
    readLine("Oracle username", user, sizeof(user));
    readLine("Oracle password", pass, sizeof(pass));

    r = SQLConnect(dbc, (SQLCHAR *)dsn, SQL_NTS,
                   (SQLCHAR *)user, SQL_NTS, (SQLCHAR *)pass, SQL_NTS);
    memset(pass, 0, sizeof(pass));

    if (!ok(r)) {
        showError(SQL_HANDLE_DBC, dbc);
        return 0;
    }

    /* Keep transaction behavior explicit; DML is committed by this app after success. */
    SQLSetConnectAttr(dbc, SQL_ATTR_AUTOCOMMIT, (SQLPOINTER)SQL_AUTOCOMMIT_ON, 0);
    printf("\nConnected to Oracle through ODBC.\n");
    return 1;
}

static void disconnectDB(void) {
    if (dbc != SQL_NULL_HDBC) {
        SQLDisconnect(dbc);
        SQLFreeHandle(SQL_HANDLE_DBC, dbc);
        dbc = SQL_NULL_HDBC;
    }
    if (env != SQL_NULL_HENV) {
        SQLFreeHandle(SQL_HANDLE_ENV, env);
        env = SQL_NULL_HENV;
    }
}

/* SQL values are bound parameters; identifiers come only from fixed definitions above. */
static int executePrepared(const char *sql, char values[][INPUT_LEN], int count, SQLLEN *affected) {
    SQLHSTMT st = SQL_NULL_HSTMT;
    SQLLEN indicators[MAX_FIELDS];
    SQLRETURN r;
    if (!ok(SQLAllocHandle(SQL_HANDLE_STMT, dbc, &st))) {
        showError(SQL_HANDLE_DBC, dbc);
        return 0;
    }
    r = SQLPrepare(st, (SQLCHAR *)sql, SQL_NTS);
    if (!ok(r)) {
        showError(SQL_HANDLE_STMT, st);
        SQLFreeHandle(SQL_HANDLE_STMT, st);
        return 0;
    }
    for (int i = 0; i < count; i++) {
        indicators[i] = SQL_NTS;
        r = SQLBindParameter(st, (SQLUSMALLINT)(i + 1), SQL_PARAM_INPUT,
                             SQL_C_CHAR, SQL_VARCHAR, INPUT_LEN - 1, 0,
                             values[i], INPUT_LEN, &indicators[i]);
        if (!ok(r)) {
            showError(SQL_HANDLE_STMT, st);
            SQLFreeHandle(SQL_HANDLE_STMT, st);
            return 0;
        }
    }
    r = SQLExecute(st);
    if (!ok(r)) {
        showError(SQL_HANDLE_STMT, st);
        SQLFreeHandle(SQL_HANDLE_STMT, st);
        return 0;
    }
    if (affected) SQLRowCount(st, affected);
    SQLFreeHandle(SQL_HANDLE_STMT, st);
    return 1;
}

static void executeDDL(const char *sql) {
    SQLHSTMT st = SQL_NULL_HSTMT;
    if (!ok(SQLAllocHandle(SQL_HANDLE_STMT, dbc, &st))) {
        showError(SQL_HANDLE_DBC, dbc);
        return;
    }
    SQLRETURN r = SQLExecDirect(st, (SQLCHAR *)sql, SQL_NTS);
    if (ok(r)) printf("DDL statement executed successfully.\n");
    else showError(SQL_HANDLE_STMT, st);
    SQLFreeHandle(SQL_HANDLE_STMT, st);
}

static void showQuery(const char *sql) {
    SQLHSTMT st = SQL_NULL_HSTMT;
    SQLRETURN r;
    SQLSMALLINT ncols = 0;
    if (!ok(SQLAllocHandle(SQL_HANDLE_STMT, dbc, &st))) {
        showError(SQL_HANDLE_DBC, dbc);
        return;
    }
    r = SQLExecDirect(st, (SQLCHAR *)sql, SQL_NTS);
    if (!ok(r)) {
        showError(SQL_HANDLE_STMT, st);
        SQLFreeHandle(SQL_HANDLE_STMT, st);
        return;
    }
    SQLNumResultCols(st, &ncols);
    for (SQLSMALLINT c = 1; c <= ncols; c++) {
        SQLCHAR name[128] = "";
        SQLSMALLINT len = 0, type = 0, scale = 0, nullable = 0;
        SQLULEN size = 0;
        SQLDescribeCol(st, c, name, sizeof(name), &len, &type, &size, &scale, &nullable);
        printf("%-22.22s", name);
    }
    printf("\n");
    for (SQLSMALLINT c = 0; c < ncols; c++) printf("----------------------");
    printf("\n");

    int rows = 0;
    while ((r = SQLFetch(st)) != SQL_NO_DATA) {
        if (!ok(r)) {
            showError(SQL_HANDLE_STMT, st);
            break;
        }
        for (SQLSMALLINT c = 1; c <= ncols; c++) {
            SQLCHAR value[512] = "";
            SQLLEN indicator = 0;
            r = SQLGetData(st, c, SQL_C_CHAR, value, sizeof(value), &indicator);
            if (indicator == SQL_NULL_DATA) printf("%-22s", "NULL");
            else if (ok(r)) printf("%-22.22s", (char *)value);
            else printf("%-22s", "[read error]");
        }
        printf("\n");
        rows++;
    }
    printf("Rows fetched: %d\n", rows);
    SQLFreeHandle(SQL_HANDLE_STMT, st);
}

static void listRecords(Entity *e) {
    char sql[1200] = "SELECT ";
    for (int i = 0; i < e->n; i++) {
        if (i) strcat(sql, ", ");
        if (strcmp(e->cols[i], "PRODUCTION_DATE") == 0 ||
            strcmp(e->cols[i], "INSPECTION_DATE") == 0)
            strcat(sql, "TO_CHAR("), strcat(sql, e->cols[i]), strcat(sql, ", 'YYYY-MM-DD') AS "), strcat(sql, e->cols[i]);
        else strcat(sql, e->cols[i]);
    }
    strcat(sql, " FROM ");
    strcat(sql, e->table);
    strcat(sql, " ORDER BY ");
    strcat(sql, e->cols[0]);
    showQuery(sql);
}

static void insertRecord(Entity *e) {
    char values[MAX_FIELDS][INPUT_LEN] = {{0}};
    char sql[1600] = "";
    SQLLEN affected = 0;
    strcat(sql, "INSERT INTO ");
    strcat(sql, e->table);
    strcat(sql, " (");
    for (int i = 0; i < e->n; i++) {
        if (i) strcat(sql, ", ");
        strcat(sql, e->cols[i]);
        readLine(e->labels[i], values[i], sizeof(values[i]));
        if (values[i][0] == '\0') {
            printf("Empty values are not accepted in this basic version.\n");
            return;
        }
    }
    strcat(sql, ") VALUES (");
    for (int i = 0; i < e->n; i++) {
        if (i) strcat(sql, ", ");
        if (e->types[i] == 'N') strcat(sql, "TO_NUMBER(?)");
        else if (e->types[i] == 'D') strcat(sql, "TO_DATE(?, 'YYYY-MM-DD')");
        else strcat(sql, "?");
    }
    strcat(sql, ")");
    if (executePrepared(sql, values, e->n, &affected))
        printf("Insert successful. Rows affected: %ld\n", (long)affected);
}

static void updateRecord(Entity *e) {
    char id[INPUT_LEN], values[MAX_FIELDS][INPUT_LEN] = {{0}};
    char sql[1600] = "";
    SQLLEN affected = 0;
    readLine(e->labels[0], id, sizeof(id));
    if (!id[0]) { printf("ID is required.\n"); return; }

    strcat(sql, "UPDATE ");
    strcat(sql, e->table);
    strcat(sql, " SET ");
    for (int i = 1; i < e->n; i++) {
        if (i > 1) strcat(sql, ", ");
        strcat(sql, e->cols[i]);
        strcat(sql, " = ");
        if (e->types[i] == 'N') strcat(sql, "TO_NUMBER(?)");
        else if (e->types[i] == 'D') strcat(sql, "TO_DATE(?, 'YYYY-MM-DD')");
        else strcat(sql, "?");
        readLine(e->labels[i], values[i - 1], sizeof(values[i - 1]));
        if (!values[i - 1][0]) { printf("Empty values are not accepted.\n"); return; }
    }
    strcat(sql, " WHERE ");
    strcat(sql, e->cols[0]);
    strcat(sql, " = TO_NUMBER(?)");
    strcpy(values[e->n - 1], id);
    if (executePrepared(sql, values, e->n, &affected))
        printf("Update executed. Rows affected: %ld\n", (long)affected);
}

static void deleteRecord(Entity *e) {
    char value[MAX_FIELDS][INPUT_LEN] = {{0}};
    char sql[512];
    SQLLEN affected = 0;
    readLine("ID to delete", value[0], sizeof(value[0]));
    if (value[0][0] == '\0') { printf("ID is required.\n"); return; }
    printf("WARNING: this permanently deletes the matching record.\n");
    char confirm[16];
    readLine("Type YES to confirm", confirm, sizeof(confirm));
    if (strcmp(confirm, "YES") != 0) {
        printf("Delete cancelled.\n");
        return;
    }
    snprintf(sql, sizeof(sql), "DELETE FROM %s WHERE %s = TO_NUMBER(?)", e->table, e->cols[0]);
    if (executePrepared(sql, value, 1, &affected))
        printf("Delete executed. Rows affected: %ld\n", (long)affected);
}

static void searchRecords(Entity *e) {
    char value[INPUT_LEN], sql[512];
    SQLHSTMT st = SQL_NULL_HSTMT;
    SQLRETURN r;
    SQLLEN indicator = SQL_NTS;
    SQLSMALLINT ncols = 0;
    readLine("Search text (partial match)", value, sizeof(value));
    snprintf(sql, sizeof(sql),
             "SELECT * FROM %s WHERE UPPER(TO_CHAR(%s)) LIKE '%%' || UPPER(?) || '%%'",
             e->table, e->search_col);
    if (!ok(SQLAllocHandle(SQL_HANDLE_STMT, dbc, &st))) {
        showError(SQL_HANDLE_DBC, dbc);
        return;
    }
    r = SQLPrepare(st, (SQLCHAR *)sql, SQL_NTS);
    if (ok(r))
        r = SQLBindParameter(st, 1, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_VARCHAR,
                             INPUT_LEN - 1, 0, value, sizeof(value), &indicator);
    if (ok(r)) r = SQLExecute(st);
    if (!ok(r)) {
        showError(SQL_HANDLE_STMT, st);
        SQLFreeHandle(SQL_HANDLE_STMT, st);
        return;
    }
    SQLNumResultCols(st, &ncols);
    for (SQLSMALLINT c = 1; c <= ncols; c++) {
        SQLCHAR name[128] = "";
        SQLSMALLINT len = 0, type = 0, scale = 0, nullable = 0;
        SQLULEN size = 0;
        SQLDescribeCol(st, c, name, sizeof(name), &len, &type, &size, &scale, &nullable);
        printf("%-22.22s", name);
    }
    printf("\n");
    int rows = 0;
    while ((r = SQLFetch(st)) != SQL_NO_DATA) {
        if (!ok(r)) { showError(SQL_HANDLE_STMT, st); break; }
        for (SQLSMALLINT c = 1; c <= ncols; c++) {
            SQLCHAR v[512] = "";
            SQLLEN ind = 0;
            SQLRETURN gr = SQLGetData(st, c, SQL_C_CHAR, v, sizeof(v), &ind);
            if (ind == SQL_NULL_DATA) printf("%-22s", "NULL");
            else if (ok(gr)) printf("%-22.22s", (char *)v);
            else printf("%-22s", "[read error]");
        }
        printf("\n");
        rows++;
    }
    printf("Rows found: %d\n", rows);
    SQLFreeHandle(SQL_HANDLE_STMT, st);
}

static void entityMenu(Entity *e) {
    for (;;) {
        printf("\n--- %s ---\n", e->title);
        printf("1. Insert record\n2. View all records\n3. Search records\n4. Update record\n5. Delete record\n0. Back\n");
        int c = readChoice("Choice");
        switch (c) {
            case 1: insertRecord(e); waitEnter(); break;
            case 2: listRecords(e); waitEnter(); break;
            case 3: searchRecords(e); waitEnter(); break;
            case 4: updateRecord(e); waitEnter(); break;
            case 5: deleteRecord(e); waitEnter(); break;
            case 0: return;
            default: printf("Invalid choice.\n");
        }
    }
}

static void reportsMenu(void) {
    for (;;) {
        printf("\n--- Mining Reports / DQL ---\n");
        printf("1. Production by mine and mineral (JOIN, GROUP BY, SUM)\n");
        printf("2. Mine sites ordered by depth\n");
        printf("3. Equipment count by mine\n");
        printf("4. Safety inspection results\n");
        printf("5. View all mine sites\n");
        printf("0. Back\n");
        switch (readChoice("Choice")) {
            case 1:
                showQuery("SELECT ms.MINE_NAME, m.MINERAL_NAME, NVL(SUM(pr.TONNES_PRODUCED),0) TOTAL_TONNES "
                          "FROM MINE_SITE ms JOIN PRODUCTION_RECORD pr ON ms.MINE_ID=pr.MINE_ID "
                          "JOIN MINERAL m ON m.MINERAL_ID=pr.MINERAL_ID "
                          "GROUP BY ms.MINE_NAME, m.MINERAL_NAME ORDER BY TOTAL_TONNES DESC");
                waitEnter(); break;
            case 2:
                showQuery("SELECT MINE_ID, MINE_NAME, LOCATION, DEPTH_METERS FROM MINE_SITE ORDER BY DEPTH_METERS DESC");
                waitEnter(); break;
            case 3:
                showQuery("SELECT ms.MINE_NAME, COUNT(e.EQUIPMENT_ID) EQUIPMENT_COUNT "
                          "FROM MINE_SITE ms LEFT JOIN EQUIPMENT e ON ms.MINE_ID=e.MINE_ID "
                          "GROUP BY ms.MINE_NAME ORDER BY ms.MINE_NAME");
                waitEnter(); break;
            case 4:
                showQuery("SELECT ms.MINE_NAME, si.INSPECTION_DATE, si.INSPECTOR_NAME, si.RESULT "
                          "FROM MINE_SITE ms JOIN SAFETY_INSPECTION si ON ms.MINE_ID=si.MINE_ID "
                          "ORDER BY si.INSPECTION_DATE DESC");
                waitEnter(); break;
            case 5: listRecords(&entities[0]); waitEnter(); break;
            case 0: return;
            default: printf("Invalid choice.\n");
        }
    }
}

static void ddlMenu(void) {
    for (;;) {
        printf("\n--- DDL / Schema Management ---\n");
        printf("1. Create MINING_NOTE table\n");
        printf("2. Add NOTES column to MINING_NOTE\n");
        printf("3. Inspect mining table columns\n");
        printf("4. Drop MINING_NOTE table (DANGEROUS)\n");
        printf("0. Back\n");
        switch (readChoice("Choice")) {
            case 1:
                executeDDL("CREATE TABLE MINING_NOTE (NOTE_ID NUMBER PRIMARY KEY, MINE_ID NUMBER REFERENCES MINE_SITE(MINE_ID), NOTE_DATE DATE, NOTE_TEXT VARCHAR2(500))");
                waitEnter(); break;
            case 2:
                executeDDL("ALTER TABLE MINING_NOTE ADD (CATEGORY VARCHAR2(50))");
                waitEnter(); break;
            case 3:
                showQuery("SELECT TABLE_NAME, COLUMN_NAME, DATA_TYPE FROM USER_TAB_COLUMNS "
                          "WHERE TABLE_NAME IN ('MINE_SITE','MINERAL','MINER','EQUIPMENT','PRODUCTION_RECORD','SAFETY_INSPECTION','MINING_NOTE') "
                          "ORDER BY TABLE_NAME, COLUMN_ID");
                waitEnter(); break;
            case 4: {
                char confirm[16];
                readLine("Type DROP to confirm", confirm, sizeof(confirm));
                if (strcmp(confirm, "DROP") == 0) executeDDL("DROP TABLE MINING_NOTE CASCADE CONSTRAINTS");
                else printf("Drop cancelled.\n");
                waitEnter(); break;
            }
            case 0: return;
            default: printf("Invalid choice.\n");
        }
    }
}

int main(void) {
    if (!connectDB()) {
        printf("\nConnection failed. Verify DSN, credentials, Oracle service, and network.\n");
        disconnectDB();
        return 1;
    }
    for (;;) {
        printf("\n========== MINING DATABASE MANAGEMENT SYSTEM ==========\n");
        for (int i = 0; i < ENTITY_COUNT; i++) printf("%d. %s\n", i + 1, entities[i].title);
        printf("%d. Reports / DQL queries\n", ENTITY_COUNT + 1);
        printf("%d. DDL operations\n", ENTITY_COUNT + 2);
        printf("0. Exit\n");
        int c = readChoice("Choice");
        if (c == 0) break;
        if (c >= 1 && c <= ENTITY_COUNT) entityMenu(&entities[c - 1]);
        else if (c == ENTITY_COUNT + 1) reportsMenu();
        else if (c == ENTITY_COUNT + 2) ddlMenu();
        else printf("Invalid choice. Please try again.\n");
    }
    disconnectDB();
    printf("Disconnected. Goodbye.\n");
    return 0;
}
