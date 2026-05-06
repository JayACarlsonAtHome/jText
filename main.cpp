#include "jText.h"
#include <iostream>

int main() {
    JTextFile jfile;
    jfile.date = "2026-05-05";
    jfile.purpose = "Jay's workshop inventory";
    jfile.case_mode = CaseMode::Insensitive;
    jfile.filename = "Workshop_Inventory_Test_001.jtext";

    // New SQL metadata
    jfile.sql_dialect = "PostGreSQL";
    jfile.table_name = "Workshop_Inventory_Test_001";

    // Field List with types
    jfile.sections.push_back({
        "Field List",
        {
            {1, '#', 0, {"Category"},    "varchar(50)",  false, false},
            {2, '#', 0, {"Item"},        "varchar(100)", false, false},
            {3, '#', 0, {"Quantity"},    "int",          false, false}
        }
    });

    // Data Section
    JTextSection data;
    data.name = "Data Section";
    data.entries = {
        {1, '#', '^', {"Tools", "Power Tools", "Cordless Drill"}, "18V lithium-ion, needs charger", false, false},
        {2, '#', 0,   {"Angle Grinder"}, "4.5 inch corded", false, false},
        {3, '#', 0,   {"NULL"}, "", true, false},
        {4, '#', '^', {"Storage", "Pegboard", "Hooks"}, "Heavy duty", false, false}
    };
    jfile.sections.push_back(data);

    // Special raw block example
    JTextSection notes;
    notes.name = "Quick Notes";
    notes.entries.push_back({
        5, '#', 0, {"This line can contain # $ ^ | * \\ and anything else"},
        "Special block comment example", false, true
    });
    jfile.sections.push_back(notes);

    // Write using the member filename
    if (auto res = jfile.write(""); res) {   // empty string = use member filename
        std::cout << "✅ File written successfully: " << jfile.filename << "\n";
    } else {
        std::cout << "❌ Write failed: " << res.error() << "\n";
    }

    // Read back test
    JTextFile readFile;
    if (auto res = readFile.read_full("workshop.jtext"); res) {
        std::cout << "✅ Read successful!\n";
        std::cout << "Purpose: " << readFile.purpose << "\n";
        std::cout << "SQL Dialect: " << (readFile.sql_dialect.empty() ? "(none)" : readFile.sql_dialect) << "\n";
        std::cout << "Table Name:  " << (readFile.table_name.empty() ? "(none)" : readFile.table_name) << "\n";
    } else {
        std::cout << "❌ Read failed: " << res.error() << "\n";
    }

    // Run validation
    if (auto v = readFile.validate(); v) {
        // Validation already prints its own output
    } else {
        std::cout << "❌ Validation failed: " << v.error() << "\n";
    }
    return 0;
}