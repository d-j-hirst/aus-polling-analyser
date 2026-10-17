#include "../SeatVolatility.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

int main(int argc, char** argv) {
    std::string const metadata = "format,1\nelection,2030fed\ngeneric_sd,3\nfallback_sd,3.8\nkurtosis,4\n";
    std::istringstream input(metadata + "seat,Harbour,1.1,3.2\nseat,Old Harbour,0.9,4\n");
    auto table = SeatVolatility::read(input, "2030fed");
    assert(table.lookup("Harbour", "Old Harbour", "").sd == 3.2);
    assert(table.lookup("New Harbour", "Old Harbour", "Harbour").sd == 4);
    assert(table.lookup("New Harbour", "", "Harbour").elasticity == 1.1);
    assert(!table.lookup("Unknown", "", "").matched);
    assert(table.lookup("Unknown", "", "").sd == 3.8);
    for (auto const& bad : {metadata + "seat,X,1,1\n", metadata + "seat,X,1,nan\n",
        metadata + "seat,X,1,3\nseat,X,1,4\n", metadata + "fallback_sd,4\n",
        std::string("format,1\nelection,2030fed\n")}) {
        bool rejected = false;
        try { std::istringstream stream(bad); SeatVolatility::read(stream, "2030fed"); }
        catch (std::exception const&) { rejected = true; }
        assert(rejected);
    }
    bool wrongTerm = false;
    try { std::istringstream stream(metadata); SeatVolatility::read(stream, "2022fed"); }
    catch (std::exception const&) { wrongTerm = true; }
    assert(wrongTerm);
    // When local generated data exists, also consume it with the production
    // parser. The fictional cases above remain portable in a fresh checkout.
    auto root = argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::path(".");
    auto directory = root / "analysis/Seat Statistics/individual-seat-factors";
    int generatedFiles = 0;
    if (std::filesystem::exists(directory)) {
        for (auto const& entry : std::filesystem::directory_iterator(directory)) {
            if (entry.path().extension() != ".csv") continue;
            std::ifstream file(entry.path());
            auto generated = SeatVolatility::read(file, entry.path().stem().string());
            assert(!generated.seats.empty());
            ++generatedFiles;
        }
    }
    std::cout << "Election-specific volatility, fallback, aliases and validation passed; "
        << generatedFiles << " generated files checked.\n";
}
