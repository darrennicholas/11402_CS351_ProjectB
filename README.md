# Project B: CSV Mini Database & Query Engine

## Overview
This project implements a lightweight CSV-based mini database and query engine.  
It is designed as an educational project to demonstrate how structured data can be parsed, indexed, and queried without relying on a full database system.

The system loads CSV files into memory, optionally builds indexes, and executes simple queries using a minimal query grammar.

---

## Learning Objectives
This project teaches the following core concepts:

- CSV parsing and data validation  
- Handling quoted fields and delimiters  
- In-memory data representation  
- Indexing for faster data retrieval  
- Designing a simple query language  
- Understanding performance and memory trade-offs  

---

## CSV Parsing
The system reads and parses CSV files into an internal table structure.

Key parsing considerations include:
- Comma-separated values
- Quoted fields containing commas
- Escaped quotes within quoted fields
- Header row processing
- Handling malformed or inconsistent rows

### CSV Parsing Approaches
Two implementation approaches are acceptable:

**Custom CSV Parser**  
Students may implement a simple but robust CSV parser that correctly handles quoted fields and edge cases. This approach is recommended for learning purposes.

**Lightweight CSV Library (Optional)**  
A small external library may be used via a package manager such as vcpkg or Conan. This allows students to focus more on indexing and query execution logic.

---

## Data Storage Model
Parsed CSV data is stored entirely in memory using:
- A list of column headers
- A collection of rows
- String-based field values

This structure allows fast access and simple iteration over rows.

---

## Indexing
To improve query performance, the project supports basic indexing.

Indexing concepts covered include:
- Column-based indexing
- Hash-based lookup
- Mapping values to row locations
- Memory overhead versus speed improvement

Indexes are optional and can be enabled selectively per column.

---

## Query Grammar
The project supports a minimal SQL-like query language.

Query features include:
- Selecting all columns or specific columns
- Filtering rows using a WHERE condition
- Equality-based comparisons
- Support for string and numeric values

The grammar is intentionally simple to focus on parsing and execution rather than full SQL compliance.

---

## Query Execution
The query engine performs the following steps:
- Parse the input query
- Determine whether an index can be used
- Execute either an indexed lookup or full table scan
- Return matching rows

This design highlights real-world trade-offs in database execution strategies.

---

## Performance Trade-Offs
This project emphasizes understanding performance considerations, including:
- Full table scans versus indexed queries
- Memory usage of indexes
- Cost of preprocessing versus query-time speed
- Simplicity versus extensibility

Students are encouraged to test performance on small and large datasets.

---

## Project Structure
The project is organized into logical components such as:
- CSV parsing
- Table management
- Index management
- Query parsing
- Query execution

This separation of concerns mirrors real database system design.

---

## Possible Extensions
Optional extensions for advanced exploration include:
- Multiple conditions in WHERE clauses
- Sorting results
- Limiting result counts
- Case-insensitive queries
- Persisting indexes to disk

---

## Educational Value
This project provides hands-on experience with:
- File parsing
- Data-oriented design
- Basic database internals
- Query planning and optimization

It is suitable for systems programming, data structures, and software engineering courses.

---

## License
This project is released under the MIT License.
