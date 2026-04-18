/**
 * screenshot_ocr_selection.h
 *
 * OCR text selection - pure functions for testing.
 * These functions are extracted from screenshot_float.c and can be tested
 * without window/GDI/clipboard dependencies.
 */

#ifndef SCREENSHOT_OCR_SELECTION_H
#define SCREENSHOT_OCR_SELECTION_H

#include <windows.h>
#include <stdbool.h>
#include "screenshot_ocr.h"

/**
 * Image layout structure for coordinate mapping.
 * Represents how an image is scaled and positioned within a window.
 */
typedef struct {
    float scale;        /* scaling factor */
    POINT offset;       /* offset from window origin */
    SIZE drawSize;      /* size of drawn image */
} OcrImageLayout;

/**
 * Selection state structure.
 */
typedef struct {
    int anchorWordIndex;    /* anchor word index for selection */
    int activeWordIndex;   /* active word index for selection */
} OcrSelectionState;

/**
 * Calculate image layout for fitting image in window.
 *
 * @param imageWidth Original image width
 * @param imageHeight Original image height
 * @param clientWidth Window client area width
 * @param clientHeight Window client area height
 * @param outLayout Output layout structure
 *
 * @return true if successful, false otherwise
 */
bool OcrCalculateImageLayout(
    int imageWidth,
    int imageHeight,
    int clientWidth,
    int clientHeight,
    OcrImageLayout* outLayout);

/**
 * Convert point from image coordinates to window coordinates.
 */
POINT OcrImageToWindowPoint(const OcrImageLayout* layout, POINT imagePoint);

/**
 * Convert point from window coordinates to image coordinates.
 */
POINT OcrWindowToImagePoint(const OcrImageLayout* layout, POINT windowPoint);

/**
 * Convert rectangle from image coordinates to window coordinates.
 */
RECT OcrImageToWindowRect(const OcrImageLayout* layout, RECT imageRect);

/**
 * Hit test: find word index at image point.
 *
 * @param results OCR results containing words
 * @param imagePoint Point in image coordinates
 *
 * @return Word index at point, or -1 if no word found
 */
int OcrHitTestWordAtImagePoint(const OCRResults* results, POINT imagePoint);

/**
 * Hit test: find word index at window point.
 *
 * @param results OCR results containing words
 * @param layout Image layout
 * @param windowPoint Point in window coordinates
 *
 * @return Word index at point, or -1 if no word found
 */
int OcrHitTestWordAtWindowPoint(
    const OCRResults* results,
    const OcrImageLayout* layout,
    POINT windowPoint);

/**
 * Get selected word range from selection state.
 *
 * @param selection Selection state
 * @param wordCount Total number of words
 * @param outStart Output start index
 * @param outEnd Output end index
 *
 * @return true if selection is valid, false otherwise
 */
bool OcrGetSelectedWordRange(
    const OcrSelectionState* selection,
    int wordCount,
    int* outStart,
    int* outEnd);

/**
 * Build UTF-8 text from selected word range.
 *
 * @param results OCR results containing words
 * @param selection Selection state
 *
 * @return Newly allocated UTF-8 text (caller must free), or NULL on error
 */
char* OcrBuildSelectedTextUtf8(
    const OCRResults* results,
    const OcrSelectionState* selection);

/**
 * Measure UTF-16 length from UTF-8 text.
 *
 * @param utf8Text UTF-8 encoded text
 *
 * @return UTF-16 length including null terminator, or 0 on error
 */
int OcrMeasureUtf16LengthFromUtf8(const char* utf8Text);

/**
 * Test if a selection is valid (anchor and active are in range).
 */
bool OcrSelectionIsValid(const OcrSelectionState* selection, int wordCount);

/**
 * Create a selection state with given anchor and active indices.
 */
OcrSelectionState OcrMakeSelection(int anchorWordIndex, int activeWordIndex);

#endif /* SCREENSHOT_OCR_SELECTION_H */
