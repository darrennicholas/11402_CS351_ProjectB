/**
 * CSV Mini Database & Query Engine
 * ---------------------------------
 * Features:
 * - Robust CSV parser (quotes, escaped quotes)
 * - In‑memory table storage
 * - Hash index on any column (fast equality lookups)
 * - Query language: SELECT columns FROM file WHERE conditions
 *   Operators: =, !=, >, <   |   Combine with AND
 * - Performance measurement (index build / query time)
 * - Interactive REPL
 *
 * Usage: Compile with C++17, run, then type queries like:
 *   SELECT * FROM orders WHERE status = "Shipped"
 *   SELECT customer_name, total FROM orders WHERE total > 100
 *   SELECT order_id, product FROM orders WHERE status = "Delivered" AND total < 50
 */

#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <unordered_map>
#include <algorithm>
#include <chrono>
#include <cctype>
#include <stdexcept>

// -------------------------------------------------------------------
// 1. Robust CSV Parser
// -------------------------------------------------------------------

std::vector<std::vector<std::string>> parseCSV(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open())
        throw std::runtime_error("Cannot open file: " + filename);

    std::vector<std::vector<std::string>> table;
    std::string line;
    std::string field;
    bool inQuotes = false;
    std::vector<std::string> row;

    auto addField = [&]() {
        // Trim whitespace outside quotes
        if (!inQuotes) {
            size_t start = field.find_first_not_of(" \t");
            size_t end = field.find_last_not_of(" \t");
            field = (start == std::string::npos) ? "" : field.substr(start, end - start + 1);
        }
        row.push_back(field);
        field.clear();
    };

    while (std::getline(file, line)) {
        for (char ch : line) {
            if (inQuotes) {
                if (ch == '"') {
                    // Check for escaped quote (double quote)
                    if (file.peek() == '"') {
                        field += ch;
                        file.get();          // consume second quote
                        field += '"';
                    } else {
                        inQuotes = false;
                    }
                } else {
                    field += ch;
                }
            } else {
                if (ch == ',') {
                    addField();
                } else if (ch == '"') {
                    inQuotes = true;
                } else {
                    field += ch;
                }
            }
        }
        // End of line
        if (!inQuotes) {
            addField();
            if (!row.empty()) {
                table.push_back(row);
                row.clear();
            }
        } else {
            field += '\n';   // multiline field
        }
    }
    if (inQuotes) {
        addField();
        row.push_back(field);
        table.push_back(row);
    }
    return table;
}

// -------------------------------------------------------------------
// 2. Table & Index Structures
// -------------------------------------------------------------------

using Row = std::vector<std::string>;
using Table = std::vector<Row>;

class IndexManager {
public:
    void buildIndex(const Table& table, size_t colIdx) {
        auto start = std::chrono::steady_clock::now();
        std::unordered_map<std::string, std::vector<size_t>> idx;
        for (size_t rowId = 0; rowId < table.size(); ++rowId) {
            if (colIdx >= table[rowId].size()) continue;
            const std::string& val = table[rowId][colIdx];
            idx[val].push_back(rowId);
        }
        indexes[colIdx] = std::move(idx);
        auto end = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(end - start).count();
        std::cout << "[Index] Built on column " << colIdx << " in " << elapsed << " sec\n";
    }

    std::vector<size_t> lookup(size_t colIdx, const std::string& value) const {
        auto it = indexes.find(colIdx);
        if (it == indexes.end()) return {};
        auto valIt = it->second.find(value);
        if (valIt == it->second.end()) return {};
        return valIt->second;
    }

    bool hasIndex(size_t colIdx) const {
        return indexes.find(colIdx) != indexes.end();
    }

private:
    std::unordered_map<size_t, std::unordered_map<std::string, std::vector<size_t>>> indexes;
};

// -------------------------------------------------------------------
// 3. Query Language: Parser & AST
// -------------------------------------------------------------------

enum class Op { EQ, NE, GT, LT };

struct Condition {
    std::string column;
    Op op;
    std::string value;
};

struct Query {
    std::vector<std::string> selectedColumns;  // empty = SELECT *
    std::string tableName;                     // actually the CSV filename
    std::vector<Condition> conditions;         // AND semantics
};

// Helper functions
static inline std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    size_t end = s.find_last_not_of(" \t\r\n");
    return (start == std::string::npos) ? "" : s.substr(start, end - start + 1);
}

static inline std::string toUpper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::toupper);
    return s;
}

Condition parseCondition(const std::string& condStr) {
    std::string opStr;
    std::string left, right;
    size_t pos;

    if ((pos = condStr.find("!=")) != std::string::npos) {
        opStr = "!=";
        left = trim(condStr.substr(0, pos));
        right = trim(condStr.substr(pos + 2));
    } else if ((pos = condStr.find("=")) != std::string::npos) {
        opStr = "=";
        left = trim(condStr.substr(0, pos));
        right = trim(condStr.substr(pos + 1));
    } else if ((pos = condStr.find(">")) != std::string::npos) {
        opStr = ">";
        left = trim(condStr.substr(0, pos));
        right = trim(condStr.substr(pos + 1));
    } else if ((pos = condStr.find("<")) != std::string::npos) {
        opStr = "<";
        left = trim(condStr.substr(0, pos));
        right = trim(condStr.substr(pos + 1));
    } else {
        throw std::runtime_error("Invalid condition: " + condStr);
    }

    Condition c;
    c.column = left;
    // Remove surrounding quotes from value
    if (right.size() >= 2 && right.front() == '"' && right.back() == '"')
        right = right.substr(1, right.size() - 2);
    c.value = right;

    if (opStr == "=") c.op = Op::EQ;
    else if (opStr == "!=") c.op = Op::NE;
    else if (opStr == ">") c.op = Op::GT;
    else if (opStr == "<") c.op = Op::LT;
    else throw std::runtime_error("Unknown operator: " + opStr);

    return c;
}

Query parseQuery(const std::string& queryStr) {
    Query q;
    std::string s = queryStr;

    // 1. SELECT ... FROM
    size_t selStart = s.find("SELECT ");
    if (selStart == std::string::npos) throw std::runtime_error("Missing SELECT");
    selStart += 7;
    size_t fromPos = s.find(" FROM ");
    if (fromPos == std::string::npos) throw std::runtime_error("Missing FROM");
    std::string colPart = trim(s.substr(selStart, fromPos - selStart));

    if (colPart == "*") {
        q.selectedColumns.clear();
    } else {
        std::replace(colPart.begin(), colPart.end(), ',', ' ');
        std::istringstream iss(colPart);
        std::string col;
        while (iss >> col) {
            q.selectedColumns.push_back(col);
        }
    }

    // 2. Table name and optional WHERE
    size_t wherePos = s.find(" WHERE ");
    if (wherePos == std::string::npos) {
        size_t fileStart = fromPos + 6;
        q.tableName = trim(s.substr(fileStart));
        return q;
    }

    size_t fileStart = fromPos + 6;
    q.tableName = trim(s.substr(fileStart, wherePos - fileStart));

    // 3. Conditions after WHERE
    std::string condPart = trim(s.substr(wherePos + 7));
    // Split by " AND "
    size_t pos = 0;
    std::string remaining = condPart;
    while ((pos = remaining.find(" AND ")) != std::string::npos) {
        std::string cond = remaining.substr(0, pos);
        q.conditions.push_back(parseCondition(trim(cond)));
        remaining = remaining.substr(pos + 5);
    }
    if (!remaining.empty()) {
        q.conditions.push_back(parseCondition(trim(remaining)));
    }

    return q;
}

// -------------------------------------------------------------------
// 4. Query Execution Engine
// -------------------------------------------------------------------

bool evaluateCondition(const Row& row, const Condition& cond,
                       const std::unordered_map<std::string, size_t>& colMap) {
    auto it = colMap.find(cond.column);
    if (it == colMap.end()) throw std::runtime_error("Unknown column: " + cond.column);
    size_t colIdx = it->second;
    if (colIdx >= row.size()) return false;
    const std::string& cell = row[colIdx];

    if (cond.op == Op::EQ) return cell == cond.value;
    if (cond.op == Op::NE) return cell != cond.value;
    if (cond.op == Op::GT) return cell > cond.value;
    if (cond.op == Op::LT) return cell < cond.value;
    return false;
}

std::vector<size_t> scanTable(const Table& table,
                              const std::vector<Condition>& conditions,
                              const std::unordered_map<std::string, size_t>& colMap) {
    std::vector<size_t> result;
    for (size_t rid = 0; rid < table.size(); ++rid) {
        bool match = true;
        for (const auto& cond : conditions) {
            if (!evaluateCondition(table[rid], cond, colMap)) {
                match = false;
                break;
            }
        }
        if (match) result.push_back(rid);
    }
    return result;
}

Table executeQuery(const Query& q, const Table& dataTable,
                   const std::vector<std::string>& headers,
                   const IndexManager& idxMgr) {
    // Map column name to index
    std::unordered_map<std::string, size_t> colNameToIdx;
    for (size_t i = 0; i < headers.size(); ++i)
        colNameToIdx[headers[i]] = i;

    // Determine output columns
    std::vector<size_t> outColIndices;
    std::vector<std::string> outColNames;
    if (q.selectedColumns.empty()) {
        for (size_t i = 0; i < headers.size(); ++i) {
            outColIndices.push_back(i);
            outColNames.push_back(headers[i]);
        }
    } else {
        for (const auto& col : q.selectedColumns) {
            auto it = colNameToIdx.find(col);
            if (it == colNameToIdx.end())
                throw std::runtime_error("Selected column not found: " + col);
            outColIndices.push_back(it->second);
            outColNames.push_back(col);
        }
    }

    // Choose execution strategy: use index if possible
    std::vector<size_t> rowIds;
    bool usedIndex = false;
    for (size_t i = 0; i < q.conditions.size(); ++i) {
        const auto& cond = q.conditions[i];
        if (cond.op == Op::EQ) {
            auto it = colNameToIdx.find(cond.column);
            if (it != colNameToIdx.end() && idxMgr.hasIndex(it->second)) {
                rowIds = idxMgr.lookup(it->second, cond.value);
                usedIndex = true;
                // Filter with all conditions
                std::vector<size_t> filtered;
                for (size_t rid : rowIds) {
                    bool ok = true;
                    for (size_t j = 0; j < q.conditions.size(); ++j) {
                        if (j == i) continue;
                        if (!evaluateCondition(dataTable[rid], q.conditions[j], colNameToIdx)) {
                            ok = false;
                            break;
                        }
                    }
                    if (ok) filtered.push_back(rid);
                }
                rowIds = std::move(filtered);
                break;
            }
        }
    }

    auto startExec = std::chrono::steady_clock::now();
    if (!usedIndex) {
        rowIds = scanTable(dataTable, q.conditions, colNameToIdx);
        auto end = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(end - startExec).count();
        std::cout << "[Exec] Full table scan took " << elapsed << " sec\n";
    } else {
        auto end = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(end - startExec).count();
        std::cout << "[Exec] Used index, scanned " << rowIds.size() << " rows in " << elapsed << " sec\n";
    }

    // Build result table
    Table result;
    for (size_t rid : rowIds) {
        Row outRow;
        for (size_t colIdx : outColIndices) {
            if (colIdx < dataTable[rid].size())
                outRow.push_back(dataTable[rid][colIdx]);
            else
                outRow.push_back("");
        }
        result.push_back(outRow);
    }
    return result;
}

void printResult(const Table& result, const std::vector<std::string>& headers) {
    if (result.empty()) {
        std::cout << "(empty result)\n";
        return;
    }
    // Calculate column widths
    std::vector<int> widths(headers.size());
    for (size_t i = 0; i < headers.size(); ++i)
        widths[i] = (int)headers[i].size();
    for (const auto& row : result) {
        for (size_t i = 0; i < row.size(); ++i) {
            widths[i] = std::max(widths[i], (int)row[i].size());
        }
    }

    // Print header
    std::string separator = "+";
    for (int w : widths) separator += std::string(w + 2, '-') + "+";
    std::cout << separator << "\n";
    for (size_t i = 0; i < headers.size(); ++i) {
        std::cout << "| " << headers[i]
                  << std::string(widths[i] - headers[i].size(), ' ') << " ";
    }
    std::cout << "|\n" << separator << "\n";

    // Print rows
    for (const auto& row : result) {
        for (size_t i = 0; i < row.size(); ++i) {
            std::cout << "| " << row[i]
                      << std::string(widths[i] - row[i].size(), ' ') << " ";
        }
        std::cout << "|\n";
    }
    std::cout << separator << "\n";
    std::cout << result.size() << " row(s) returned.\n";
}

// -------------------------------------------------------------------
// 5. Main: Create sample CSV and REPL
// -------------------------------------------------------------------

void createSampleOrdersCSV(const std::string& filename) {
    std::ofstream file(filename);
    if (!file) return;
    file << "order_id,customer_name,product,total,status\n";
    file << "2001,Sofia Rodriguez,\"Bluetooth Speaker\",79.99,Shipped\n";
    file << "2002,Oliver Smith,\"Laptop Stand\",29.50,Processing\n";
    file << "2003,Ava Johnson,\"Noise Cancelling Headphones\",199.00,Delivered\n";
    file << "2004,Ethan Williams,\"Gaming Mouse\",49.95,Cancelled\n";
    file << "2005,Isabella Jones,\"Mechanical Keyboard\",129.99,Delivered\n";
    file << "2006,Mason Garcia,\"USB Microphone\",89.00,Shipped\n";
    file << "2007,Lucas Martinez,\"LED Monitor Strip\",15.75,Returned\n";
    file << "2008,Mia Davis,\"Wireless Charger\",25.00,Processing\n";
    file << "2009,Amelia Brown,\"Desk Lamp with USB\",34.99,Delivered\n";
    file << "2010,James Wilson,\"External SSD 1TB\",119.00,Shipped\n";
    file.close();
    std::cout << "Created sample file: " << filename << "\n";
}

int main() {
    const std::string csvFile = "orders.csv";
    createSampleOrdersCSV(csvFile);

    // Load CSV
    Table rawTable = parseCSV(csvFile);
    if (rawTable.size() < 2) {
        std::cerr << "Error: CSV has no data rows.\n";
        return 1;
    }
    std::vector<std::string> headers = rawTable[0];
    Table dataTable(rawTable.begin() + 1, rawTable.end());
    std::cout << "Loaded " << dataTable.size() << " rows from " << csvFile << "\n";

    // Build indexes on columns that are often used in WHERE clauses
    IndexManager idxMgr;
    // Map column names to indices
    std::unordered_map<std::string, size_t> colIdx;
    for (size_t i = 0; i < headers.size(); ++i)
        colIdx[headers[i]] = i;

    idxMgr.buildIndex(dataTable, colIdx["status"]);
    idxMgr.buildIndex(dataTable, colIdx["customer_name"]);
    // You can also build on order_id, product, etc.

    // REPL
    std::cout << "\n=== CSV Query Engine (type 'exit' to quit) ===\n";
    std::cout << "Example queries:\n";
    std::cout << "  SELECT * FROM orders WHERE status = \"Shipped\"\n";
    std::cout << "  SELECT customer_name, total FROM orders WHERE total > 100\n";
    std::cout << "  SELECT order_id, product FROM orders WHERE status = \"Delivered\" AND total < 50\n\n";

    std::string line;
    while (true) {
        std::cout << ">> ";
        if (!std::getline(std::cin, line)) break;
        line = trim(line);
        if (line.empty()) continue;
        if (toUpper(line) == "EXIT") break;

        try {
            Query q = parseQuery(line);
            // The query table name must match the CSV filename (without .csv extension)
            if (q.tableName != "orders" && q.tableName != "orders.csv") {
                std::cout << "Only 'orders' table is available.\n";
                continue;
            }
            Table result = executeQuery(q, dataTable, headers, idxMgr);
            printResult(result, q.selectedColumns.empty() ? headers : q.selectedColumns);
        } catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << "\n";
        }
        std::cout << "\n";
    }

    return 0;
}