#ifndef AI_TRANSLATOR_H
#define AI_TRANSLATOR_H

#include <gtk/gtk.h>
#include <stdbool.h>
#include "common.h"

// Function to create the AI translator UI components
// Returns a GtkWidget containing the AI translator UI
// The returned widget will have data attached to it that can be retrieved with g_object_get_data
GtkWidget* create_ai_translator_ui(GtkWidget *parent_window);

// Function to show the AI settings dialog
void show_ai_settings_dialog(GtkWidget *parent_window);

// Function to send text to AI for translation
void send_to_ai_translation(GtkWidget *parent_window, GtkTextBuffer *ai_buffer,
                           const char *text, const char *source_format, const char *target_format);

// Function to check if an API key is valid
bool check_api_key(AIProvider provider, const char *api_key);

// Function to save API keys securely
void save_api_key(AIProvider provider, const char *api_key);

// Function to load API keys securely
char* load_api_key(AIProvider provider);

// Function to save custom context
void save_custom_context(const char *context);

// Function to load custom context
char* load_custom_context(void);

// Function to save model name
void save_model_name(AIProvider provider, const char *model_name);

// Function to load model name
char* load_model_name(AIProvider provider);

// Function to save translation language
void save_translate_to(const char *language);

// Function to load translation language
char* load_translate_to(void);

// Function to save source language
void save_translate_from(const char *language);

// Function to load source language
char* load_translate_from(void);

#endif /* AI_TRANSLATOR_H */
