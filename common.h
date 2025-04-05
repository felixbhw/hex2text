#ifndef COMMON_H
#define COMMON_H

#include <gtk/gtk.h>
#include <stdbool.h>

// Encoding types - moved from main.c
typedef enum {
    HEX,
    ASCII,
    UTF8,
    UTF16LE,
    UTF16BE,
    UTF32LE,
    UTF32BE,
    ISO8859_1,
    ISO8859_15,
    SHIFT_JIS,
    EUC_JP,
    KOI8_R
} EncodingType;

// AI Provider types - moved from ai_translator.h
typedef enum {
    OPENAI,
    GEMINI
} AIProvider;

// Window-specific data structure - moved from main.c
struct WindowData {
    GtkWidget *top_text_view;
    GtkWidget *bottom_text_view;
    GtkDropDown *top_encoding_dropdown;
    GtkDropDown *bottom_encoding_dropdown;
    GtkTextBuffer *top_buffer;
    GtkTextBuffer *bottom_buffer;
    GtkWidget *top_counter_label;
    GtkWidget *bottom_counter_label;
    GtkWidget *ai_translator_box;
    GtkTextBuffer *ai_translation_buffer;
    GtkWidget *send_to_ai_button;
    bool is_updating; // Flag to prevent recursive updates
};

// Define the WindowData type
typedef struct WindowData WindowData;

// Format name conversion utility
const char* encoding_type_to_string(EncodingType type);

#endif /* COMMON_H */
