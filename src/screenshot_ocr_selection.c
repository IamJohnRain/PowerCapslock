/**
 * screenshot_ocr_selection.c
 *
 * OCR text selection - pure functions implementation.
 */

#include "screenshot_ocr_selection.h"
#include <stdlib.h>
#include <string.h>

bool OcrCalculateImageLayout(
    int imageWidth,
    int imageHeight,
    int clientWidth,
    int clientHeight,
    OcrImageLayout* outLayout) {

    if (outLayout == NULL || imageWidth <= 0 || imageHeight <= 0 ||
        clientWidth <= 0 || clientHeight <= 0) {
        return false;
    }

    /* Calculate scale to fit image in client area */
    float scaleX = (float)clientWidth / imageWidth;
    float scaleY = (float)clientHeight / imageHeight;

    /* Use the smaller scale to ensure image fits entirely */
    float scale = (scaleX < scaleY) ? scaleX : scaleY;

    /* Ensure scale is positive */
    if (scale <= 0) {
        scale = 1.0f;
    }

    /* Calculate drawn size */
    int drawWidth = (int)(imageWidth * scale);
    int drawHeight = (int)(imageHeight * scale);

    /* Calculate centered offset */
    int offsetX = (clientWidth - drawWidth) / 2;
    int offsetY = (clientHeight - drawHeight) / 2;

    outLayout->scale = scale;
    outLayout->offset.x = offsetX;
    outLayout->offset.y = offsetY;
    outLayout->drawSize.cx = drawWidth;
    outLayout->drawSize.cy = drawHeight;

    return true;
}

POINT OcrImageToWindowPoint(const OcrImageLayout* layout, POINT imagePoint) {
    POINT wp;
    if (layout == NULL) {
        wp.x = imagePoint.x;
        wp.y = imagePoint.y;
        return wp;
    }
    wp.x = (int)(imagePoint.x * layout->scale + layout->offset.x);
    wp.y = (int)(imagePoint.y * layout->scale + layout->offset.y);
    return wp;
}

POINT OcrWindowToImagePoint(const OcrImageLayout* layout, POINT windowPoint) {
    POINT ip;
    if (layout == NULL || layout->scale <= 0) {
        ip.x = windowPoint.x;
        ip.y = windowPoint.y;
        return ip;
    }
    ip.x = (int)((windowPoint.x - layout->offset.x) / layout->scale);
    ip.y = (int)((windowPoint.y - layout->offset.y) / layout->scale);
    return ip;
}

RECT OcrImageToWindowRect(const OcrImageLayout* layout, RECT imageRect) {
    RECT winRect;
    if (layout == NULL) {
        return imageRect;
    }

    winRect.left = (int)(imageRect.left * layout->scale + layout->offset.x);
    winRect.top = (int)(imageRect.top * layout->scale + layout->offset.y);
    winRect.right = (int)(imageRect.right * layout->scale + layout->offset.x);
    winRect.bottom = (int)(imageRect.bottom * layout->scale + layout->offset.y);

    return winRect;
}

int OcrHitTestWordAtImagePoint(const OCRResults* results, POINT imagePoint) {
    if (results == NULL || results->words == NULL || results->wordCount <= 0) {
        return -1;
    }

    int i;
    for (i = 0; i < results->wordCount; i++) {
        RECT bbox = results->words[i].boundingBox;

        /* Check if point is within bounding box (inclusive) */
        if (imagePoint.x >= bbox.left && imagePoint.x <= bbox.right &&
            imagePoint.y >= bbox.top && imagePoint.y <= bbox.bottom) {
            return i;
        }
    }

    return -1;
}

int OcrHitTestWordAtWindowPoint(
    const OCRResults* results,
    const OcrImageLayout* layout,
    POINT windowPoint) {

    if (results == NULL || layout == NULL) {
        return -1;
    }

    /* Convert window point to image point */
    POINT imagePoint = OcrWindowToImagePoint(layout, windowPoint);

    /* Check if point is within image bounds */
    if (imagePoint.x < 0 || imagePoint.y < 0 ||
        imagePoint.x > layout->drawSize.cx ||
        imagePoint.y > layout->drawSize.cy) {
        return -1;
    }

    return OcrHitTestWordAtImagePoint(results, imagePoint);
}

bool OcrGetSelectedWordRange(
    const OcrSelectionState* selection,
    int wordCount,
    int* outStart,
    int* outEnd) {

    if (selection == NULL || outStart == NULL || outEnd == NULL) {
        return false;
    }

    if (selection->anchorWordIndex < 0 || selection->anchorWordIndex >= wordCount ||
        selection->activeWordIndex < 0 || selection->activeWordIndex >= wordCount) {
        return false;
    }

    *outStart = (selection->anchorWordIndex < selection->activeWordIndex) ?
                selection->anchorWordIndex : selection->activeWordIndex;
    *outEnd = (selection->anchorWordIndex < selection->activeWordIndex) ?
              selection->activeWordIndex : selection->anchorWordIndex;

    return true;
}

char* OcrBuildSelectedTextUtf8(
    const OCRResults* results,
    const OcrSelectionState* selection) {

    if (results == NULL || results->words == NULL || selection == NULL) {
        return NULL;
    }

    int start, end;
    if (!OcrGetSelectedWordRange(selection, results->wordCount, &start, &end)) {
        return NULL;
    }

    /* Calculate required buffer size */
    size_t totalLen = 0;
    int i;
    for (i = start; i <= end; i++) {
        if (i >= 0 && i < results->wordCount) {
            totalLen += strlen(results->words[i].text);
            if (results->words[i].isLineBreak) {
                totalLen += 2; /* \r\n */
            } else if (i < end) {
                totalLen += 1; /* space */
            }
        }
    }

    char* result = (char*)malloc(totalLen + 1);
    if (result == NULL) {
        return NULL;
    }

    /* Build the text */
    char* p = result;
    for (i = start; i <= end; i++) {
        if (i >= 0 && i < results->wordCount) {
            OCRWord* word = &results->words[i];
            strcpy(p, word->text);
            p += strlen(word->text);

            if (word->isLineBreak) {
                *p++ = '\r';
                *p++ = '\n';
            } else if (i < end) {
                *p++ = ' ';
            }
        }
    }
    *p = '\0';

    return result;
}

int OcrMeasureUtf16LengthFromUtf8(const char* utf8Text) {
    if (utf8Text == NULL) {
        return 0;
    }

    /* Calculate required size for wide character conversion */
    int wideLen = MultiByteToWideChar(CP_UTF8, 0, utf8Text, -1, NULL, 0);
    return wideLen;
}

bool OcrSelectionIsValid(const OcrSelectionState* selection, int wordCount) {
    if (selection == NULL) {
        return false;
    }

    return (selection->anchorWordIndex >= 0 &&
            selection->anchorWordIndex < wordCount &&
            selection->activeWordIndex >= 0 &&
            selection->activeWordIndex < wordCount);
}

OcrSelectionState OcrMakeSelection(int anchorWordIndex, int activeWordIndex) {
    OcrSelectionState state;
    state.anchorWordIndex = anchorWordIndex;
    state.activeWordIndex = activeWordIndex;
    return state;
}
