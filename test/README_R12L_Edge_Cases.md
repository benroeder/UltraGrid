# R12L Edge Case Testing Documentation

## Overview

This document describes the comprehensive property-based testing implementation for R12L format edge cases, particularly focusing on incomplete pixel groups (when width is not divisible by 8).

## R12L Format Background

- **Format**: 12-bit packed RGB 4:4:4 little-endian (SMPTE 268M DPX v1, Annex C, Method C4)
- **Packing**: 8 pixels packed into 36 bytes
- **Challenge**: When image width is not divisible by 8, incomplete groups create edge cases

## Critical Issue Identified

The current `alpha_blend_r12l()` implementation in `src/utils/alpha_blend.c` (lines 416-419) **does not blend pixels in incomplete groups**:

```c
// Handle remaining pixels (less than 8)
// For now, we'll leave them unblended as this is complex
// In a production implementation, you'd handle the partial group
```

This means when `width % 8 != 0`, the remaining pixels are left unblended.

## Property-Based Tests Implemented

### 1. R12L Pixel Packing/Unpacking Validation
- **Test**: `R12L pixel packing and unpacking is reversible`
- **Purpose**: Validates the complex R12L packing format is correctly implemented
- **Coverage**: Tests all pixel positions within 8-pixel groups

### 2. Memory Allocation for Incomplete Groups
- **Test**: `R12L memory allocation handles incomplete groups correctly`
- **Purpose**: Ensures proper memory allocation for widths not divisible by 8
- **Validation**: Verifies `((width + 7) / 8) * 36` bytes are allocated

### 3. Current Implementation Behavior
- **Test**: `current alpha_blend_r12l preserves incomplete group pixels`
- **Purpose**: Documents the current limitation where incomplete groups are not blended
- **Validation**: Confirms unblended pixels remain unchanged

### 4. Enhanced Implementation Simulation
- **Test**: `enhanced R12L blending processes all pixels including incomplete groups`
- **Purpose**: Tests an improved implementation that handles incomplete groups
- **Method**: Uses temporary buffers to blend incomplete groups

### 5. Alpha=0 Edge Case
- **Test**: `alpha=0 preserves destination in incomplete R12L groups`
- **Purpose**: Validates that zero alpha correctly preserves destination
- **Coverage**: Tests incomplete groups specifically

### 6. Incomplete Group Size Variations
- **Test**: `R12L handles all incomplete group sizes (1-7 pixels)`
- **Purpose**: Tests all possible incomplete group sizes
- **Coverage**: 1-7 remaining pixels after complete 8-pixel groups

### 7. Component Value Boundaries
- **Test**: `R12L handles component value boundaries correctly`
- **Purpose**: Tests 12-bit boundary values (0 and 4095)
- **Validation**: Ensures no overflow/underflow in packing/unpacking

### 8. Alpha Scaling Validation
- **Test**: `R12L alpha scaling from 8-bit to 12-bit is correct`
- **Purpose**: Validates the alpha scaling formula: `(alpha << 4) | (alpha >> 4)`
- **Properties**: Tests monotonicity, boundary values, and range validation

### 9. Memory Bounds Checking
- **Test**: `R12L enhanced blending respects memory boundaries`
- **Purpose**: Ensures no buffer overruns with incomplete groups
- **Method**: Uses exact-size allocation and validates no segfaults

### 10. Pixel-Level Validation
- **Test**: `pixel-level validation for small R12L incomplete groups`
- **Purpose**: Validates actual blending mathematics for small incomplete groups
- **Coverage**: Tests specific alpha blending results with known values

## Enhanced Implementation Approach

The tests include an `alpha_blend_r12l_enhanced()` function that demonstrates how to properly handle incomplete groups:

1. **Process complete groups** using the existing function
2. **Handle remaining pixels** using temporary 36-byte buffers
3. **Blend full 8-pixel group** with padding for remaining pixels
4. **Copy back results** preserving memory boundaries

## Test Results

All 10 R12L edge case tests pass successfully, providing:

- ✅ **Validation** of current implementation behavior
- ✅ **Documentation** of incomplete group limitations  
- ✅ **Proof-of-concept** for enhanced implementation
- ✅ **Edge case coverage** for all incomplete group sizes
- ✅ **Memory safety validation** preventing buffer overruns
- ✅ **Mathematical correctness** of alpha scaling and blending

## Production Recommendations

1. **Implement enhanced incomplete group handling** in `alpha_blend_r12l()`
2. **Use temporary buffer approach** for incomplete groups
3. **Maintain existing performance** for complete 8-pixel groups
4. **Add regression tests** to prevent incomplete group issues

## Files Modified

- `test/test_r12l_edge_cases_rapidcheck.cpp` - New comprehensive test suite
- `test/test_rapidcheck_main_minimal.cpp` - Added R12L test integration
- `Makefile` - Added R12L test compilation rules

## Running the Tests

```bash
make rapidcheck-tests
```

The R12L edge case tests provide comprehensive coverage of the most challenging aspects of the R12L format implementation.