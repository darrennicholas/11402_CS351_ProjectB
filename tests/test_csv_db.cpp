#include <gtest/gtest.h>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <sstream>
#include <chrono>
#include <cctype>

// -------------------------------------------------------------------
// Core logic (same as in the final csv_db.cpp, but without main())
// -------------------------------------------------------------------

// ----- CSV Parser (robust) -----
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
        if (!inQuotes) {
            size_t start = field.find_first_not_of(" \t");
            size_t end = field.find_last_not_of(" \t");
            field = (start == std::string::npos) ? "" : field.substr(start, end - start + 1);
        }
        row.push_back(field);
        field.clear();
    };

    while (std::getline(file, line)) {
        for (size_t i = 0; i < line.size(); ++i) {
            char ch = line[i];
            if (inQuotes) {
                if (ch == '"') {
                    // Check if next character is also a quote (escaped quote)
                    if (i + 1 < line.size() && line[i + 1] == '"') {
                        field += '"';  // Add single quote to field
                        ++i;           // Skip the second quote
                    } else {
                        inQuotes = false;  // End of quoted field
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
        if (!inQuotes) {
            addField();
            if (!row.empty()) {
                table.push_back(row);
                row.clear();
            }
        } else {
            field += '\n';
        }
    }
    if (inQuotes) {
        addField();
        row.push_back(field);
        table.push_back(row);
    }
    return table;
}

// ----- Table & Index -----
using Row = std::vector<std::string>;
using Table = std::vector<Row>;

class IndexManager {
public:
    void buildIndex(const Table& table, size_t colIdx) {
        std::unordered_map<std::string, std::vector<size_t>> idx;
        for (size_t rowId = 0; rowId < table.size(); ++rowId) {
            if (colIdx >= table[rowId].size()) continue;
            const std::string& val = table[rowId][colIdx];
            idx[val].push_back(rowId);
        }
        indexes[colIdx] = std::move(idx);
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

// ----- Query AST -----
enum class Op { EQ, NE, GT, LT };

struct Condition {
    std::string column;
    Op op;
    std::string value;
};

struct Query {
    std::vector<std::string> selectedColumns;  // empty = SELECT *
    std::string tableName;
    std::vector<Condition> conditions;
};

// Helpers
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
        while (iss >> col) q.selectedColumns.push_back(col);
    }

    size_t wherePos = s.find(" WHERE ");
    if (wherePos == std::string::npos) {
        q.tableName = trim(s.substr(fromPos + 6));
        return q;
    }

    q.tableName = trim(s.substr(fromPos + 6, wherePos - (fromPos + 6)));
    std::string condPart = trim(s.substr(wherePos + 7));
    size_t pos = 0;
    std::string remaining = condPart;
    while ((pos = remaining.find(" AND ")) != std::string::npos) {
        q.conditions.push_back(parseCondition(trim(remaining.substr(0, pos))));
        remaining = remaining.substr(pos + 5);
    }
    if (!remaining.empty())
        q.conditions.push_back(parseCondition(trim(remaining)));

    return q;
}

// ----- Query Execution -----
bool evaluateCondition(const Row& row, const Condition& cond,
                       const std::unordered_map<std::string, size_t>& colMap) {
    auto it = colMap.find(cond.column);
    if (it == colMap.end()) throw std::runtime_error("Unknown column: " + cond.column);
    size_t colIdx = it->second;
    if (colIdx >= row.size()) return false;
    const std::string& cell = row[colIdx];

    if (cond.op == Op::EQ) return cell == cond.value;
    if (cond.op == Op::NE) return cell != cond.value;
    
    // Numeric comparison for GT/LT operators
    if (cond.op == Op::GT) {
        try {
            double cellVal = std::stod(cell);
            double condVal = std::stod(cond.value);
            return cellVal > condVal;
        } catch (...) {
            return cell > cond.value;
        }
    }
    
    if (cond.op == Op::LT) {
        try {
            double cellVal = std::stod(cell);
            double condVal = std::stod(cond.value);
            return cellVal < condVal;
        } catch (...) {
            return cell < cond.value;
        }
    }
    
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
    std::unordered_map<std::string, size_t> colNameToIdx;
    for (size_t i = 0; i < headers.size(); ++i)
        colNameToIdx[headers[i]] = i;

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

    std::vector<size_t> rowIds;
    bool usedIndex = false;
    for (size_t i = 0; i < q.conditions.size(); ++i) {
        const auto& cond = q.conditions[i];
        if (cond.op == Op::EQ) {
            auto it = colNameToIdx.find(cond.column);
            if (it != colNameToIdx.end() && idxMgr.hasIndex(it->second)) {
                rowIds = idxMgr.lookup(it->second, cond.value);
                usedIndex = true;
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

    if (!usedIndex) {
        rowIds = scanTable(dataTable, q.conditions, colNameToIdx);
    }

    Table result;
    for (size_t rid : rowIds) {
        Row outRow;
        for (size_t colIdx : outColIndices) {
            outRow.push_back((colIdx < dataTable[rid].size()) ? dataTable[rid][colIdx] : "");
        }
        result.push_back(outRow);
    }
    return result;
}

// -------------------------------------------------------------------
// Test Fixture (using in‑memory table to avoid file I/O)
// -------------------------------------------------------------------

class CsvDbTest : public ::testing::Test {
protected:
    std::vector<std::string> headers;
    Table dataTable;
    IndexManager idxMgr;

    void SetUp() override {
        // Data from orders.csv (10 rows, as defined earlier)
        headers = {"order_id", "customer_name", "product", "total", "status"};
        dataTable = {
            {"2001", "Sofia Rodriguez", "Bluetooth Speaker", "79.99", "Shipped"},
            {"2002", "Oliver Smith", "Laptop Stand", "29.50", "Processing"},
            {"2003", "Ava Johnson", "Noise Cancelling Headphones", "199.00", "Delivered"},
            {"2004", "Ethan Williams", "Gaming Mouse", "49.95", "Cancelled"},
            {"2005", "Isabella Jones", "Mechanical Keyboard", "129.99", "Delivered"},
            {"2006", "Mason Garcia", "USB Microphone", "89.00", "Shipped"},
            {"2007", "Lucas Martinez", "LED Monitor Strip", "15.75", "Returned"},
            {"2008", "Mia Davis", "Wireless Charger", "25.00", "Processing"},
            {"2009", "Amelia Brown", "Desk Lamp with USB", "34.99", "Delivered"},
            {"2010", "James Wilson", "External SSD 1TB", "119.00", "Shipped"}
        };
        // Build indexes on "status" and "customer_name"
        std::unordered_map<std::string, size_t> colIdx;
        for (size_t i = 0; i < headers.size(); ++i)
            colIdx[headers[i]] = i;
        idxMgr.buildIndex(dataTable, colIdx["status"]);
        idxMgr.buildIndex(dataTable, colIdx["customer_name"]);
    }
};

// -------------------------------------------------------------------
// Test Cases
// -------------------------------------------------------------------

// 1. SELECT * without WHERE
TEST_F(CsvDbTest, SelectAllReturnsAllRowsAndColumns) {
    Query q = parseQuery("SELECT * FROM orders");
    Table result = executeQuery(q, dataTable, headers, idxMgr);
    EXPECT_EQ(result.size(), 10);
    EXPECT_EQ(result[0].size(), 5);
    EXPECT_EQ(result[0][0], "2001");
    EXPECT_EQ(result[0][1], "Sofia Rodriguez");
}

// 2. Column projection without WHERE
TEST_F(CsvDbTest, SelectColumnsOnly) {
    Query q = parseQuery("SELECT customer_name, total FROM orders");
    Table result = executeQuery(q, dataTable, headers, idxMgr);
    EXPECT_EQ(result.size(), 10);
    EXPECT_EQ(result[0].size(), 2);
    EXPECT_EQ(result[0][0], "Sofia Rodriguez");
    EXPECT_EQ(result[0][1], "79.99");
}

// 3. WHERE with equality (uses index on status)
TEST_F(CsvDbTest, WhereEqualsUsesIndex) {
    Query q = parseQuery("SELECT * FROM orders WHERE status = \"Shipped\"");
    Table result = executeQuery(q, dataTable, headers, idxMgr);
    EXPECT_EQ(result.size(), 3);   // rows 2001, 2006, 2010
    for (const auto& row : result) {
        EXPECT_EQ(row[4], "Shipped");
    }
}

// 4. WHERE with inequality != (no index used)
TEST_F(CsvDbTest, WhereNotEquals) {
    Query q = parseQuery("SELECT order_id, status FROM orders WHERE status != \"Delivered\"");
    Table result = executeQuery(q, dataTable, headers, idxMgr);
    // Delivered appears 3 times (2003,2005,2009) -> expect 7 rows
    EXPECT_EQ(result.size(), 7);
    for (const auto& row : result) {
        EXPECT_NE(row[1], "Delivered");
    }
}

// 5. WHERE with > (numeric comparison)
TEST_F(CsvDbTest, WhereGreaterThan) {
    Query q = parseQuery("SELECT order_id, total FROM orders WHERE total > \"100.00\"");
    Table result = executeQuery(q, dataTable, headers, idxMgr);
    // totals > 100: 199.00, 129.99, 119.00 (rows 2003,2005,2010)
    EXPECT_EQ(result.size(), 3);
    std::vector<std::string> expected_ids = {"2003", "2005", "2010"};
    for (size_t i = 0; i < result.size(); ++i) {
        EXPECT_TRUE(std::find(expected_ids.begin(), expected_ids.end(), result[i][0]) != expected_ids.end());
    }
}

// 6. WHERE with < (numeric comparison)
TEST_F(CsvDbTest, WhereLessThan) {
    Query q = parseQuery("SELECT order_id, total FROM orders WHERE total < \"30.00\"");
    Table result = executeQuery(q, dataTable, headers, idxMgr);
    // totals < 30.00: 29.50 (2002), 15.75 (2007), 25.00 (2008)
    EXPECT_EQ(result.size(), 3);
    std::vector<std::string> expected_ids = {"2002", "2007", "2008"};
    for (const auto& row : result) {
        EXPECT_TRUE(std::find(expected_ids.begin(), expected_ids.end(), row[0]) != expected_ids.end());
    }
}

// 7. AND condition: uses index on first equality, then filters
TEST_F(CsvDbTest, AndConditionUsesIndex) {
    Query q = parseQuery("SELECT customer_name, status FROM orders WHERE status = \"Shipped\" AND total > \"100.00\"");
    Table result = executeQuery(q, dataTable, headers, idxMgr);
    // Shipped rows: 2001(79.99),2006(89.00),2010(119.00) -> only 2010 qualifies
    EXPECT_EQ(result.size(), 1);
    EXPECT_EQ(result[0][0], "James Wilson");
    EXPECT_EQ(result[0][1], "Shipped");
}

// 8. AND without index on first condition (fallback to scan)
TEST_F(CsvDbTest, AndWithoutIndexFallback) {
    Query q = parseQuery("SELECT order_id FROM orders WHERE total > \"100.00\" AND status = \"Delivered\"");
    Table result = executeQuery(q, dataTable, headers, idxMgr);
    // total>100: 2003(199.00,Delivered),2005(129.99,Delivered),2010(119.00,Shipped)
    // status=Delivered: 2003 and 2005
    EXPECT_EQ(result.size(), 2);
    EXPECT_EQ(result[0][0], "2003");
    EXPECT_EQ(result[1][0], "2005");
}

// 9. Invalid column name in SELECT
TEST_F(CsvDbTest, InvalidSelectColumnThrows) {
    Query q = parseQuery("SELECT invalid_column FROM orders");
    EXPECT_THROW(executeQuery(q, dataTable, headers, idxMgr), std::runtime_error);
}

// 10. Invalid column name in WHERE
TEST_F(CsvDbTest, InvalidWhereColumnThrows) {
    Query q = parseQuery("SELECT * FROM orders WHERE bad_column = \"Shipped\"");
    // The exception will be thrown inside evaluateCondition when building colMap
    EXPECT_THROW(executeQuery(q, dataTable, headers, idxMgr), std::runtime_error);
}

// 11. Empty query (parse error)
TEST_F(CsvDbTest, EmptyQueryThrows) {
    EXPECT_THROW(parseQuery(""), std::runtime_error);
}

// 12. Missing WHERE clause is fine (full table)
TEST_F(CsvDbTest, NoWhereClause) {
    Query q = parseQuery("SELECT product FROM orders");
    Table result = executeQuery(q, dataTable, headers, idxMgr);
    EXPECT_EQ(result.size(), 10);
    EXPECT_EQ(result[0].size(), 1);
}

// 13. Test that index lookup returns correct row IDs directly
TEST_F(CsvDbTest, IndexLookupDirect) {
    std::unordered_map<std::string, size_t> colIdx;
    for (size_t i = 0; i < headers.size(); ++i) colIdx[headers[i]] = i;
    auto rows = idxMgr.lookup(colIdx["status"], "Shipped");
    EXPECT_EQ(rows.size(), 3);
    // The row indices (0‑based) should be 0,5,9 (corresponding to 2001,2006,2010)
    std::vector<size_t> expected = {0,5,9};
    EXPECT_EQ(rows, expected);
}

// 14. CSV parser test: read a real file with quoted fields
TEST(CsvParserTest, HandlesQuotedFields) {
    // Create a temporary CSV with quotes
    std::ofstream tmp("test_quoted.csv");
    tmp << "id,name,city\n";
    tmp << "1,\"New York, NY\",USA\n";
    tmp << "2,\"Los Angeles, CA\",USA\n";
    tmp.close();

    auto table = parseCSV("test_quoted.csv");
    EXPECT_EQ(table.size(), 3);          // header + 2 rows
    EXPECT_EQ(table[1][1], "New York, NY");
    EXPECT_EQ(table[2][1], "Los Angeles, CA");
    std::remove("test_quoted.csv");
}

// 15. CSV parser test: escaped quotes inside quoted field
TEST(CsvParserTest, HandlesEscapedQuotes) {
    std::ofstream tmp("test_escape.csv");
    tmp << "id,message\n";
    tmp << "1,\"He said \"\"Hello\"\" to me\"\n";
    tmp.close();

    auto table = parseCSV("test_escape.csv");
    EXPECT_EQ(table.size(), 2);
    EXPECT_EQ(table[1][1], "He said \"Hello\" to me");
    std::remove("test_escape.csv");
}

// -------------------------------------------------------------------
// Main
// -------------------------------------------------------------------
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
