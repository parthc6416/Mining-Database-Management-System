# ⛏️ Mining Database Management System

A menu-driven **Mining Database Management System (Mining DBMS)** developed using C, Oracle Database XE, and the ODBC API. The application allows users to manage mining operations, maintain records, execute database queries, and generate reports through an interactive interface.

## 📌 Project Overview

Mining operations involve managing multiple types of information, including mine sites, minerals, employees, equipment, production records, and safety inspections.

This project provides a centralized database management application that simplifies these operations using a C-based interface connected to an Oracle relational database through ODBC.

The application demonstrates the practical implementation of **DDL, DML, and DQL commands** as part of a database management system project.

## 🎯 Objectives

- Develop a database management application using C.
- Connect a C application to Oracle Database XE using the ODBC API.
- Implement database operations using SQL.
- Perform insert, update, delete, search, and retrieval operations.
- Generate reports using SQL joins, aggregate functions, grouping, and sorting.
- Demonstrate database schema management through DDL commands.

## 🛠️ Technology Stack

| Technology | Purpose |
|---|---|
| C | Application logic and menu-driven interface |
| Oracle Database XE | Relational database management |
| SQL | Data definition, manipulation, and querying |
| ODBC API | Connectivity between C and Oracle |
| GCC | Compiling the C application |
| Windows ODBC Data Source Administrator | Configuring the database connection |

## ✨ Features

### 1. Mining Data Management

The application provides CRUD operations (Create, Read, Update, Delete) for the following entities:

- **Mine Sites:** Manage mine names, locations, mineral types, depths, and operational status.
- **Minerals:** Maintain mineral details, categories, and market prices.
- **Miners / Employees:** Store employee information, job roles, and mine assignments.
- **Equipment:** Manage equipment details, status, and purchase costs.
- **Production Records:** Record production dates and quantities of minerals extracted.
- **Safety Inspections:** Maintain inspection dates, inspector details, results, and notes.

### 2. DDL — Data Definition Language

Demonstrates database schema operations, including:

- `CREATE TABLE` — Create a mining notes table.
- `ALTER TABLE` — Modify the table structure.
- `DROP TABLE` — Remove the demonstration table.
- Schema inspection using Oracle's `USER_TAB_COLUMNS` view.

### 3. DML — Data Manipulation Language

Supports operations such as:

- `INSERT` — Add new records.
- `UPDATE` — Modify existing records.
- `DELETE` — Remove selected records.

User inputs are collected through the application interface and used to perform database operations.

### 4. DQL — Data Query Language

The application supports data retrieval and reporting using `SELECT` queries.

Example reports include:

- Total mineral production grouped by mine and mineral.
- Mine sites sorted by depth.
- Equipment count for each mine.
- Safety inspection results.
- Searching and displaying existing records.

The reporting queries demonstrate SQL concepts such as `JOIN`, `GROUP BY`, `SUM`, `COUNT`, and `ORDER BY`.

## 🗂️ Database Design

The project uses the following primary tables:

| Table | Description |
|---|---|
| `MINE_SITE` | Stores mine site information |
| `MINERAL` | Stores mineral details |
| `MINER` | Stores miner and employee information |
| `EQUIPMENT` | Stores equipment records |
| `PRODUCTION_RECORD` | Stores mineral production data |
| `SAFETY_INSPECTION` | Stores safety inspection records |
| `MINING_NOTE` | Optional table used to demonstrate DDL operations |

Primary keys uniquely identify records, while foreign keys connect related records. For example, production records reference mine sites and minerals.

The database schema is defined in `mining.sql`.

## 📁 Project Structure

```text
MiningDBMS/
├── main.c
├── mining.sql
├── build.bat
├── README.md
└── .gitignore
```

- `main.c` — Main C application, ODBC connectivity, menus, CRUD operations, and reports.
- `mining.sql` — Oracle database schema creation script.
- `build.bat` — Windows build script.
- `README.md` — Project documentation.
- `.gitignore` — Excludes generated files and local configuration.

## ⚙️ Installation and Setup

### Prerequisites

Before running the application, ensure that you have:

1. Oracle Database XE installed and running.
2. An Oracle ODBC driver compatible with your system architecture.
3. GCC installed and accessible from the terminal.
4. ODBC development headers available to the compiler.
5. Windows ODBC Data Source Administrator configured for Oracle.

### Step 1: Clone the Repository

```bash
git clone https://github.com/YOUR_USERNAME/Mining-Database-Management-System.git
cd Mining-Database-Management-System
```

Replace `YOUR_USERNAME` with your GitHub username.

### Step 2: Configure the Oracle Database

Open SQL*Plus and connect using your Oracle account.

Check the current user:

```sql
SHOW USER;
```

Check the existing tables:

```sql
SELECT table_name
FROM