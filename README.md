# ⛏️ Mining Database Management System

A database management application developed using **C, Oracle Database XE, SQL, and the ODBC API** to manage mining operations, maintain records, and generate reports through an interactive interface.

## 📌 Project Overview

Mining operations require efficient management of mine sites, minerals, employees, equipment, production records, and safety inspections.

The Mining Database Management System (Mining DBMS) provides a centralized solution for organizing this information in an Oracle relational database. The application connects to Oracle using the Open Database Connectivity (ODBC) API and allows users to perform database operations through a menu-driven C application.

This project demonstrates the practical implementation of database concepts, including Data Definition Language (DDL), Data Manipulation Language (DML), and Data Query Language (DQL).

## 🎯 Objectives

- Develop a database management application using C.
- Establish connectivity between C and Oracle Database XE using ODBC.
- Implement DDL, DML, and DQL operations.
- Perform insert, update, delete, search, and retrieval operations using user input.
- Generate reports using SQL joins, aggregate functions, grouping, and sorting.
- Demonstrate relational database design using primary and foreign keys.

## 🛠️ Technology Stack

| Technology | Purpose |
|---|---|
| C | Application logic and database operations |
| Oracle Database XE | Relational database management |
| SQL | Database definition, manipulation, and querying |
| ODBC API | Connectivity between C and Oracle |
| GCC | Compilation of C programs |
| Windows ODBC Administrator | Configuration of the Oracle data source |
| HTML, CSS, JavaScript | Web interface, if included in the project |

## ✨ Features

### 1. Mining Operations Management

The system manages the following entities:

- **Mine Sites:** Mine names, locations, mineral types, depths, and operational status.
- **Minerals:** Mineral names, categories, and market prices.
- **Miners / Employees:** Employee details, job roles, contact information, and mine assignments.
- **Equipment:** Equipment names, operational status, purchase costs, and associated mine sites.
- **Production Records:** Production dates, quantities, mines, and minerals.
- **Safety Inspections:** Inspection dates, inspector names, results, and notes.

### 2. CRUD Operations

The application supports the four fundamental database operations:

- **Create:** Insert new records into the database.
- **Read:** Display and retrieve existing records.
- **Update:** Modify existing records.
- **Delete:** Remove selected records with confirmation.

User inputs are collected interactively, allowing records to be managed without manually writing every SQL statement.

### 3. DDL — Data Definition Language

The system demonstrates schema management through commands such as:

- `CREATE TABLE` — Create a table.
- `ALTER TABLE` — Modify an existing table.
- `DROP TABLE` — Remove a table.
- Schema inspection using Oracle's `USER_TAB_COLUMNS` data dictionary view.

### 4. DML — Data Manipulation Language

The application implements:

- `INSERT` — Add mining records.
- `UPDATE` — Modify existing information.
- `DELETE` — Delete selected records.

### 5. DQL — Data Query Language

The application uses `SELECT` queries to retrieve information and generate reports.

Example reports include:

- Mineral production grouped by mine and mineral.
- Mine sites sorted by depth.
- Equipment counts for individual mine sites.
- Safety inspection records and results.
- Record searches using partial text matching.

SQL concepts demonstrated include `JOIN`, `GROUP BY`, `SUM`, `COUNT`, and `ORDER BY`.

## 🗄️ Database Design

The database schema is organized around the following tables:

| Table | Description |
|---|---|
| `MINE_SITE` | Stores information about mine sites |
| `MINERAL` | Stores mineral details |
| `MINER` | Stores miner and employee information |
| `EQUIPMENT` | Stores equipment details |
| `PRODUCTION_RECORD` | Stores mineral production data |
| `SAFETY_INSPECTION` | Stores safety inspection information |
| `MINING_NOTE` | Demonstrates additional DDL operations, when created |

Primary keys uniquely identify records, while foreign keys establish relationships between related tables. For example, production records reference the associated mine site and mineral.

The database schema is provided in `mining.sql`.

## 📁 Project Structure

```text
MiningDBMS/
├── main.c
├── server.c
├── mining.sql
├── build.bat
├── build_server.bat
├── run_web.bat
├── README.md
├── .gitignore
└── www/
    ├── index.html
    ├── style.css
    └── app.js
```

**Note:** The structure above includes the optional web application files. Keep only the entries that actually exist in your final repository.

| File / Directory | Purpose |
|---|---|
| `main.c` | Main menu-driven C application and database operations |
| `server.c` | C server for the web interface, if included |
| `mining.sql` | Oracle database schema |
| `build.bat` | Builds the command-line application |
| `build_server.bat` | Builds the server, if included |
| `run_web.bat` | Launches the web application, if included |
| `www/` | Web interface assets, if included |
| `.gitignore` | Excludes generated files and local configuration |
| `README.md` | Project documentation |

## ⚙️ Installation and Setup

### Prerequisites

Before running the project, ensure that you have:

1. Oracle Database XE installed and running.
2. A compatible Oracle ODBC driver.
3. GCC installed and accessible from the terminal.
4. ODBC development headers and libraries available to the compiler.
5. A configured Oracle ODBC data source.

### Step 1: Clone the Repository

```bash
git clone https://github.com/parthc6416/Mining-Database-Management-System.git
cd Mining-Database-Management-System
```

### Step 2: Configure the Oracle Database

Open SQL*Plus and connect to your Oracle database.

Check the current database user:

```sql
SHOW USER;
```

Check the existing tables:

```sql
SELECT table_name
FROM user_tables
ORDER BY table_name;
```

If the required mining tables do not exist, and your Oracle account has the necessary privileges, execute the `mining.sql` script.

**Important:** Review the existing schema before executing the script. Do not run it blindly against existing tables with the same names.

### Step 3: Configure the ODBC Data Source

On Windows:

1. Open the appropriate ODBC Data Source Administrator.
2. Select the **System DSN** tab.
3. Configure the Oracle ODBC driver.
4. Set the Data Source Name to `MDBMS_ORACLE`.
5. Configure the correct Oracle service name, such as `xe` if that is the service configured on your system.
6. Test the connection using your Oracle credentials.
7. Save the configuration after a successful connection test.

The DSN name used by the application must match the configured data source.

### Step 4: Build the Application

For the command-line application, open a terminal in the project directory and run:

```bat
build.bat
```

Alternatively, compile directly using GCC:

```bat
gcc -Wall -Wextra main.c -o miningdbms.exe -lodbc32
```

If the compiler cannot find `sql.h` or `sqlext.h`, configure compatible ODBC development headers.

Ensure that the compiler, ODBC libraries, and Oracle driver use compatible architectures.

### Step 5: Run the Application

```bat
miningdbms.exe
```

Enter the configured DSN, Oracle username, and Oracle password when prompted.

If your repository includes the web interface, follow the instructions in the relevant build scripts to compile and launch the server.

## 🔐 Security Considerations

- Database credentials should be entered at runtime rather than hardcoded.
- Parameterized SQL statements should be used for user-provided values.
- Database permissions depend on the Oracle account used to connect.
- A dedicated application schema is preferable to using the Oracle `SYSTEM` account for routine operations.
- Never commit passwords, access tokens, or private connection details to GitHub.
- Keep vendor database drivers and local installation files outside the repository unless redistribution is explicitly permitted.

## 🚀 Future Enhancements

Potential improvements include:

- Automatic ID generation using Oracle sequences.
- More advanced search and filtering.
- Exportable production and safety reports.
- Role-based access control.
- Transaction management and audit logging.
- Improved input validation and error handling.
- Enhanced production analytics and a graphical dashboard.

## 🎓 Academic Purpose

This project demonstrates the integration of C programming, relational database design, SQL commands, and ODBC connectivity in a practical mining management scenario.

It provides hands-on experience with database operations and illustrates how a C application can interact with an Oracle database to manage structured records and generate useful reports.

## 📄 License

No license is specified yet. Add an appropriate license if you intend to permit others to reuse or distribute this project.

---

**Developed using C, Oracle Database XE, SQL, and ODBC.**
