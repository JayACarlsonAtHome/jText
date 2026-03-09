// JText -- main.cpp

#include "jText.h"

int main() {
    // Create a sample file in memory
    JTextFile jfile;
    jfile.date = "2026-03-06";
    jfile.purpose = "Jay's workshop inventory";

    // Field List section
    JTextSection fieldList;
    fieldList.name = "Field List";
    fieldList.entries = {
            {'#', "Item ID", "unique short code"},
            {'#', "Tool Name", ""},
            {'#', "Quantity", "current count"}
    };
    jfile.sections.push_back(fieldList);

    // Data Section
    JTextSection dataSec;
    dataSec.name = "Data Section";
    dataSec.entries = {
            {'#', "T001", "primary claw"},
            {'$', "Phillips #2 set", "includes #1,#2,#3 bits"},
            {'#', "3", ""}
    };
    jfile.sections.push_back(dataSec);

    // Custom section
    JTextSection notes;
    notes.name = "Quick Notes";
    notes.entries = {
            {'#', "Keep eye on charger", "showing wear"},
            {'"', "Organize pegboard", "do this weekend"}
    };
    jfile.sections.push_back(notes);

    // Write to file
    if (jfile.write("workshop.jtext")) {
        std::cout << "File written successfully.\n";
    }

    // Read full file
    JTextFile readFile;
    if (readFile.read_full("workshop.jtext")) {
        std::cout << "Read full: Purpose = " << readFile.purpose << "\n";
        // Access sections, e.g., readFile.sections[0].entries[0].value
    }

    // Read only a section
    auto optSec = readFile.read_section("workshop.jtext", "Quick Notes");
    if (optSec) {
        std::cout << "Read section: First entry value = " << optSec->entries[0].value << "\n";
    }

    return 0;
}
