#pragma once
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

// Tiny test reporter: failures throw, so make stops and never writes a pass report.
struct Tests {
    std::vector<std::string> passed;
    void check(bool ok, const std::string& name) {
        if (!ok) throw std::runtime_error("FAILED: " + name);
        passed.push_back(name);
    }
    void near(double actual, double expected, double tolerance, const std::string& name) {
        check(std::isfinite(actual) && std::abs(actual - expected) <= tolerance, name);
    }
    template<class F> void rejects(F fn, const std::string& name) {
        bool rejected = false;
        try { fn(); } catch (const std::invalid_argument&) { rejected = true; }
        check(rejected, name);
    }
    void report(const std::string& project, const std::string& detail = "") const {
        std::cout << "{\n  \"project\": \"" << project << "\",\n"
                  << "  \"status\": \"In progress\",\n  \"verification\": \"passed\",\n"
                  << "  \"tests_passed\": " << passed.size() << ",\n  \"checks\": [\n";
        for (size_t i=0; i<passed.size(); ++i)
            std::cout << "    \"" << passed[i] << "\"" << (i+1<passed.size()?",":"") << "\n";
        std::cout << "  ]" << detail << "\n}\n";
    }
};
