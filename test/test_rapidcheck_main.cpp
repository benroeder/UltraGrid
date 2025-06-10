/**
 * @file test_rapidcheck_main.cpp
 * @brief Main entry point for all RapidCheck property-based tests
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <rapidcheck.h>
#include <iostream>

// Declare test functions from other files
extern void test_alpha_blend_properties();
extern void test_overlay_properties();

int main() {
    std::cout << "Running UltraGrid RapidCheck property-based tests..." << std::endl;
    
    std::cout << "\n=== Alpha Blend Tests ===" << std::endl;
    test_alpha_blend_properties();
    
    std::cout << "\n=== Overlay Tests ===" << std::endl;
    test_overlay_properties();
    
    std::cout << "\nAll RapidCheck tests completed successfully!" << std::endl;
    return 0;
}