/**
 * @file test_rapidcheck_main_minimal.cpp
 * @brief Minimal entry point for overlay RapidCheck tests
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <rapidcheck.h>
#include <iostream>

// Declare test functions from other files
extern void test_alpha_blend_properties();
extern void test_overlay_properties();
extern void test_overlay_caching_properties();
extern void test_r12l_edge_cases_properties();

int main() {
    std::cout << "Running UltraGrid RapidCheck property-based tests (extended)..." << std::endl;
    
    std::cout << "\n=== Alpha Blend Tests ===" << std::endl;
    test_alpha_blend_properties();
    
    std::cout << "\n=== Overlay Tests ===" << std::endl;
    test_overlay_properties();
    
    std::cout << "\n=== Overlay Caching Tests ===" << std::endl;
    test_overlay_caching_properties();
    
    std::cout << "\n=== R12L Edge Case Tests ===" << std::endl;
    test_r12l_edge_cases_properties();
    
    std::cout << "\nAll RapidCheck tests completed successfully!" << std::endl;
    return 0;
}