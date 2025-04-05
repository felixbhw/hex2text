#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include "common.h"
#include "ai_translator.h"

// Global flag for debugging
bool debug_mode = false;

// Forward declarations
static void update_conversion(WindowData *data);
static void update_reverse_conversion(WindowData *data);
static void convert_between_formats(const char *input, EncodingType from_type,
                                  char **output, size_t *output_len, EncodingType to_type);
static void toggle_ai_translator(GSimpleAction *action, GVariant *parameter, gpointer user_data);
static void show_ai_settings(GSimpleAction *action, GVariant *parameter, gpointer user_data);
static void update_counter_labels(WindowData *data);
static void open_new_window(GSimpleAction *action, GVariant *parameter, gpointer user_data);
static void on_window_destroy(GtkWidget *window, gpointer user_data);
static void on_send_to_ai_clicked(GtkButton *button, gpointer user_data);

// Convert a single hex character to its value
static int hex_char_to_int(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1; // Invalid hex character
}

// Convert a string of hex to binary data
static unsigned char *hex_to_binary(const char *hex_str, size_t *out_len) {
    size_t hex_len = strlen(hex_str);
    size_t bin_len = 0;
    unsigned char *bin_data;

    // Remove spaces and validate hex string
    char *clean_hex = g_malloc(hex_len + 1);
    size_t clean_index = 0;

    for (size_t i = 0; i < hex_len; i++) {
        if (isspace(hex_str[i])) continue;
        if (isxdigit(hex_str[i])) {
            clean_hex[clean_index++] = hex_str[i];
        } else {
            g_free(clean_hex);
            *out_len = 0;
            return NULL; // Invalid character
        }
    }

    clean_hex[clean_index] = '\0';
    hex_len = clean_index;

    // Ensure even number of hex digits
    if (hex_len % 2 != 0) {
        g_free(clean_hex);
        *out_len = 0;
        return NULL;
    }

    bin_len = hex_len / 2;
    bin_data = g_malloc(bin_len);

    for (size_t i = 0; i < bin_len; i++) {
        int high = hex_char_to_int(clean_hex[i * 2]);
        int low = hex_char_to_int(clean_hex[i * 2 + 1]);

        if (high == -1 || low == -1) {
            g_free(clean_hex);
            g_free(bin_data);
            *out_len = 0;
            return NULL;
        }

        bin_data[i] = (high << 4) | low;
    }

    g_free(clean_hex);
    *out_len = bin_len;
    return bin_data;
}

// Convert binary data to hex string
static char *binary_to_hex(const unsigned char *data, size_t len) {
    // Allocate memory for hex string: each byte becomes 2 hex chars + space + null terminator
    char *hex_str = g_malloc(len * 3 + 1);

    if (len == 0) {
        // Handle empty data case
        hex_str[0] = '\0';
        return hex_str;
    }

    // Convert each byte to hex
    for (size_t i = 0; i < len; i++) {
        sprintf(hex_str + i * 3, "%02X ", data[i]);
    }

    // Remove trailing space and ensure null termination
    hex_str[len * 3 - 1] = '\0';

    return hex_str;
}

// Convert binary data to text based on encoding
static char *binary_to_text(const unsigned char *data, size_t len, EncodingType encoding) {
    GError *error = NULL;
    char *text = NULL;

    switch (encoding) {
        case ASCII: {
            // For ASCII, just copy and ensure null termination
            text = g_malloc(len + 1);
            memcpy(text, data, len);
            text[len] = '\0';

            // Replace non-printable ASCII with replacement character
            // For ASCII, we'll use a simple '?' since we can't fit Unicode in a char
            for (size_t i = 0; i < len; i++) {
                if (text[i] < 32 || text[i] > 126) {
                    text[i] = '?'; // Question mark as replacement character for ASCII
                }
            }
            break;
        }

        case UTF8: {
            // For UTF-8, handle invalid sequences character by character
            GString *result = g_string_new(NULL);
            const char *end = (const char *)data + len;
            const char *p = (const char *)data;

            while (p < end) {
                gunichar ch;
                if (g_utf8_validate(p, 1, NULL)) {
                    ch = g_utf8_get_char_validated(p, end - p);
                    if (ch == (gunichar)-1 || ch == (gunichar)-2) {
                        // Invalid UTF-8 sequence
                        g_string_append(result, "⍰"); // Diamond with question mark as replacement character
                        p++; // Skip this byte
                    } else {
                        // Valid character
                        g_string_append_unichar(result, ch);
                        p = g_utf8_next_char(p);
                    }
                } else {
                    // Invalid UTF-8 byte
                    g_string_append(result, "⍰"); // Diamond with question mark as replacement character
                    p++; // Skip this byte
                }
            }

            text = g_string_free(result, FALSE);
            break;
        }

        case UTF16LE: {
            // Convert from UTF-16LE to UTF-8 with error handling
            GString *result = g_string_new(NULL);

            // Process 2 bytes at a time
            for (size_t i = 0; i < len; i += 2) {
                if (i + 1 >= len) {
                    // Incomplete surrogate pair
                    g_string_append(result, "⍰");
                    break;
                }

                // Get UTF-16 code unit
                guint16 code_unit = data[i] | (data[i+1] << 8);

                // Check if it's a high surrogate
                if (code_unit >= 0xD800 && code_unit <= 0xDBFF) {
                    // Need another code unit for the low surrogate
                    if (i + 3 >= len) {
                        // Incomplete surrogate pair
                        g_string_append(result, "⍰");
                        break;
                    }

                    guint16 low_surrogate = data[i+2] | (data[i+3] << 8);
                    if (low_surrogate >= 0xDC00 && low_surrogate <= 0xDFFF) {
                        // Valid surrogate pair
                        gunichar ch = 0x10000 + ((code_unit - 0xD800) << 10) + (low_surrogate - 0xDC00);
                        g_string_append_unichar(result, ch);
                        i += 2; // Skip the low surrogate
                    } else {
                        // Invalid surrogate pair
                        g_string_append(result, "⍰");
                    }
                } else if (code_unit >= 0xDC00 && code_unit <= 0xDFFF) {
                    // Unexpected low surrogate
                    g_string_append(result, "⍰");
                } else {
                    // Regular BMP character
                    g_string_append_unichar(result, code_unit);
                }
            }

            text = g_string_free(result, FALSE);
            break;
        }

        case UTF16BE: {
            // Convert from UTF-16BE to UTF-8 with error handling
            GString *result = g_string_new(NULL);

            // Process 2 bytes at a time
            for (size_t i = 0; i < len; i += 2) {
                if (i + 1 >= len) {
                    // Incomplete surrogate pair
                    g_string_append(result, "⍰");
                    break;
                }

                // Get UTF-16 code unit (big endian)
                guint16 code_unit = (data[i] << 8) | data[i+1];

                // Check if it's a high surrogate
                if (code_unit >= 0xD800 && code_unit <= 0xDBFF) {
                    // Need another code unit for the low surrogate
                    if (i + 3 >= len) {
                        // Incomplete surrogate pair
                        g_string_append(result, "⍰");
                        break;
                    }

                    guint16 low_surrogate = (data[i+2] << 8) | data[i+3];
                    if (low_surrogate >= 0xDC00 && low_surrogate <= 0xDFFF) {
                        // Valid surrogate pair
                        gunichar ch = 0x10000 + ((code_unit - 0xD800) << 10) + (low_surrogate - 0xDC00);
                        g_string_append_unichar(result, ch);
                        i += 2; // Skip the low surrogate
                    } else {
                        // Invalid surrogate pair
                        g_string_append(result, "⍰");
                    }
                } else if (code_unit >= 0xDC00 && code_unit <= 0xDFFF) {
                    // Unexpected low surrogate
                    g_string_append(result, "⍰");
                } else {
                    // Regular BMP character
                    g_string_append_unichar(result, code_unit);
                }
            }

            text = g_string_free(result, FALSE);
            break;
        }
    }

    return text;
}

// Convert text to binary based on encoding
static unsigned char *text_to_binary(const char *text, size_t *out_len, EncodingType encoding) {
    GError *error = NULL;
    unsigned char *bin_data = NULL;
    size_t text_len = strlen(text);

    switch (encoding) {
        case ASCII: {
            // For ASCII, just copy the bytes
            bin_data = g_malloc(text_len);
            memcpy(bin_data, text, text_len);
            *out_len = text_len;
            break;
        }

        case UTF8: {
            // For UTF-8, just copy if valid
            if (g_utf8_validate(text, -1, NULL)) {
                bin_data = g_malloc(text_len);
                memcpy(bin_data, text, text_len);
                *out_len = text_len;
            } else {
                *out_len = 0;
                return NULL;
            }
            break;
        }

        case UTF16LE: {
            // Convert from UTF-8 to UTF-16LE
            gsize bytes_written;
            char *converted = g_convert(text, -1, "UTF-16LE", "UTF-8", NULL, &bytes_written, &error);
            if (error) {
                g_error_free(error);
                *out_len = 0;
                return NULL;
            }

            bin_data = (unsigned char *)converted;
            *out_len = bytes_written;
            break;
        }

        case UTF16BE: {
            // Convert from UTF-8 to UTF-16BE
            gsize bytes_written;
            char *converted = g_convert(text, -1, "UTF-16BE", "UTF-8", NULL, &bytes_written, &error);
            if (error) {
                g_error_free(error);
                *out_len = 0;
                return NULL;
            }

            bin_data = (unsigned char *)converted;
            *out_len = bytes_written;
            break;
        }
    }

    return bin_data;
}

// Convert between any two formats
static void convert_between_formats(const char *input, EncodingType from_type,
                                  char **output, size_t *output_len, EncodingType to_type) {
    // First convert to binary data as an intermediate format
    size_t bin_len = 0;
    unsigned char *bin_data = NULL;

    // Special case: if input is empty, output is empty
    if (input == NULL || *input == '\0') {
        *output = g_strdup("");
        *output_len = 0;
        return;
    }

    // Step 1: Convert input to binary based on from_type
    if (from_type == HEX) {
        bin_data = hex_to_binary(input, &bin_len);
        if (!bin_data) {
            // Try to salvage as much as possible from invalid hex
            GString *valid_hex = g_string_new(NULL);
            size_t input_len = strlen(input);

            for (size_t i = 0; i < input_len; i += 2) {
                // Skip whitespace
                while (i < input_len && isspace(input[i])) i++;
                if (i >= input_len) break;

                // Need at least 2 characters for a hex byte
                if (i + 1 >= input_len) {
                    g_string_append(valid_hex, "⍰");
                    break;
                }

                // Check if we have a valid hex byte
                if (isxdigit(input[i]) && isxdigit(input[i+1])) {
                    char hex_byte[3] = {input[i], input[i+1], '\0'};
                    g_string_append(valid_hex, hex_byte);
                } else {
                    g_string_append(valid_hex, "⍰⍰"); // Placeholder for invalid hex
                }
            }

            *output = g_string_free(valid_hex, FALSE);
            *output_len = strlen(*output);
            return;
        }
    } else {
        bin_data = text_to_binary(input, &bin_len, from_type);
        if (!bin_data && strlen(input) > 0) {
            // If conversion failed but we had input, return a placeholder
            *output = g_strdup("[Conversion error - invalid input format]");
            *output_len = strlen(*output);
            return;
        } else if (!bin_data) {
            // Empty input
            *output = g_strdup("");
            *output_len = 0;
            return;
        }
    }

    // Step 2: Convert binary to output based on to_type
    if (to_type == HEX) {
        *output = binary_to_hex(bin_data, bin_len);
        *output_len = strlen(*output);
    } else {
        *output = binary_to_text(bin_data, bin_len, to_type);
        if (!*output) {
            *output = g_strdup("[Conversion error]");
            *output_len = strlen(*output);
        } else {
            *output_len = strlen(*output);
        }
    }

    g_free(bin_data);
}

// Update conversion between the two text views
static void update_conversion(WindowData *data) {
    if (data->is_updating) return;
    data->is_updating = true;

    // Get the source text (from top buffer)
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(data->top_buffer, &start, &end);
    char *source_text = gtk_text_buffer_get_text(data->top_buffer, &start, &end, FALSE);

    // Get encoding types
    EncodingType from_type = gtk_drop_down_get_selected(data->top_encoding_dropdown);
    EncodingType to_type = gtk_drop_down_get_selected(data->bottom_encoding_dropdown);

    // Convert between formats
    char *result = NULL;
    size_t result_len = 0;

    if (strlen(source_text) > 0) {
        convert_between_formats(source_text, from_type, &result, &result_len, to_type);
        gtk_text_buffer_set_text(data->bottom_buffer, result, -1);
        g_free(result);
    } else {
        gtk_text_buffer_set_text(data->bottom_buffer, "", -1);
    }

    g_free(source_text);
    data->is_updating = false;

    // Update character and byte counters
    update_counter_labels(data);
}

// Update conversion from bottom to top
static void update_reverse_conversion(WindowData *data) {
    if (data->is_updating) return;
    data->is_updating = true;

    // Get the source text (from bottom buffer)
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(data->bottom_buffer, &start, &end);
    char *source_text = gtk_text_buffer_get_text(data->bottom_buffer, &start, &end, FALSE);

    // Get encoding types
    EncodingType from_type = gtk_drop_down_get_selected(data->bottom_encoding_dropdown);
    EncodingType to_type = gtk_drop_down_get_selected(data->top_encoding_dropdown);

    // Convert between formats
    char *result = NULL;
    size_t result_len = 0;

    if (strlen(source_text) > 0) {
        convert_between_formats(source_text, from_type, &result, &result_len, to_type);
        gtk_text_buffer_set_text(data->top_buffer, result, -1);
        g_free(result);
    } else {
        gtk_text_buffer_set_text(data->top_buffer, "", -1);
    }

    g_free(source_text);
    data->is_updating = false;

    // Update character and byte counters
    update_counter_labels(data);
}

// Function to update character and byte counters
static void update_counter_labels(WindowData *data) {
    if (data->is_updating) return;

    // Get encoding types
    EncodingType top_encoding = gtk_drop_down_get_selected(data->top_encoding_dropdown);
    EncodingType bottom_encoding = gtk_drop_down_get_selected(data->bottom_encoding_dropdown);

    // Update top counter
    if (data->top_buffer != NULL && data->top_counter_label != NULL) {
        GtkTextIter start, end;
        gtk_text_buffer_get_bounds(data->top_buffer, &start, &end);
        char *text = gtk_text_buffer_get_text(data->top_buffer, &start, &end, FALSE);

        size_t bytes, chars;

        if (top_encoding == HEX) {
            // For hex input, show the decoded metrics
            size_t bin_len = 0;
            unsigned char *bin_data = hex_to_binary(text, &bin_len);

            if (bin_data) {
                // For hex input, we want to show the actual number of bytes in the binary data
                bytes = bin_len;

                // For character count, we need to interpret based on the encoding
                if (bottom_encoding == UTF16LE || bottom_encoding == UTF16BE) {
                    // For UTF-16, each character is typically 2 bytes
                    // Count the actual characters by looking at the data
                    chars = 0;
                    size_t i = 0;
                    while (i < bin_len) {
                        // Check if we have enough bytes for a character
                        if (i + 1 >= bin_len) {
                            // Incomplete character at the end
                            chars++;
                            break;
                        }

                        // Get the code unit
                        guint16 code_unit;
                        if (bottom_encoding == UTF16LE) {
                            code_unit = bin_data[i] | (bin_data[i+1] << 8);
                        } else { // UTF16BE
                            code_unit = (bin_data[i] << 8) | bin_data[i+1];
                        }

                        // Check if it's a high surrogate (part of a surrogate pair)
                        if (code_unit >= 0xD800 && code_unit <= 0xDBFF) {
                            // This is a surrogate pair, need 4 bytes for one character
                            if (i + 3 < bin_len) {
                                // We have enough bytes for the low surrogate
                                guint16 low_surrogate;
                                if (bottom_encoding == UTF16LE) {
                                    low_surrogate = bin_data[i+2] | (bin_data[i+3] << 8);
                                } else { // UTF16BE
                                    low_surrogate = (bin_data[i+2] << 8) | bin_data[i+3];
                                }

                                if (low_surrogate >= 0xDC00 && low_surrogate <= 0xDFFF) {
                                    // Valid surrogate pair
                                    chars++;
                                    i += 4; // Skip both surrogates (4 bytes)
                                    continue;
                                }
                            }
                            // Invalid or incomplete surrogate pair
                            chars++;
                            i += 2;
                        } else {
                            // Regular BMP character
                            chars++;
                            i += 2;
                        }
                    }
                } else if (bottom_encoding == UTF8) {
                    // For UTF-8, count Unicode code points
                    char *utf8_text = g_convert((const char *)bin_data, bin_len, "UTF-8", "UTF-8", NULL, NULL, NULL);
                    if (utf8_text) {
                        chars = g_utf8_strlen(utf8_text, -1);
                        g_free(utf8_text);
                    } else {
                        chars = bin_len; // Fallback
                    }
                } else {
                    // For ASCII and other encodings, assume 1:1 mapping
                    chars = bin_len;
                }

                g_free(bin_data);
            } else {
                // If conversion failed, show raw metrics
                bytes = strlen(text);
                chars = g_utf8_strlen(text, -1);
            }
        } else {
            // For non-hex input, show raw metrics
            bytes = strlen(text);
            chars = g_utf8_strlen(text, -1);
        }

        // Update the label
        char counter_text[100];
        snprintf(counter_text, sizeof(counter_text), "Characters: %zu | Bytes: %zu", chars, bytes);
        gtk_label_set_text(GTK_LABEL(data->top_counter_label), counter_text);

        g_free(text);
    }

    // Update bottom counter
    if (data->bottom_buffer != NULL && data->bottom_counter_label != NULL) {
        GtkTextIter start, end;
        gtk_text_buffer_get_bounds(data->bottom_buffer, &start, &end);
        char *text = gtk_text_buffer_get_text(data->bottom_buffer, &start, &end, FALSE);

        // For the bottom counter, always show the actual metrics
        size_t bytes = strlen(text);
        size_t chars = g_utf8_strlen(text, -1);

        // Update the label
        char counter_text[100];
        snprintf(counter_text, sizeof(counter_text), "Characters: %zu | Bytes: %zu", chars, bytes);
        gtk_label_set_text(GTK_LABEL(data->bottom_counter_label), counter_text);

        g_free(text);
    }
}

// Callback for text buffer changes
static void on_text_buffer_changed(GtkTextBuffer *buffer, gpointer user_data) {
    WindowData *data = (WindowData *)user_data;

    if (buffer == data->top_buffer) {
        update_conversion(data); // Top to bottom
    } else if (buffer == data->bottom_buffer) {
        update_reverse_conversion(data); // Bottom to top
    }
}

// Callback for encoding dropdown changes
static void on_encoding_changed(GtkDropDown *dropdown, GParamSpec *pspec, gpointer user_data) {
    WindowData *data = (WindowData *)user_data;

    // When encoding changes, update the conversion
    if (dropdown == data->top_encoding_dropdown || dropdown == data->bottom_encoding_dropdown) {
        update_conversion(data);
    }
}

// Callback for swap button
static void on_swap_clicked(GtkButton *button, gpointer user_data) {
    WindowData *data = (WindowData *)user_data;
    GtkTextIter start, end;

    // Get top content
    gtk_text_buffer_get_bounds(data->top_buffer, &start, &end);
    char *top_content = gtk_text_buffer_get_text(data->top_buffer, &start, &end, FALSE);

    // Get bottom content
    gtk_text_buffer_get_bounds(data->bottom_buffer, &start, &end);
    char *bottom_content = gtk_text_buffer_get_text(data->bottom_buffer, &start, &end, FALSE);

    // Swap encoding selections
    guint top_encoding = gtk_drop_down_get_selected(data->top_encoding_dropdown);
    guint bottom_encoding = gtk_drop_down_get_selected(data->bottom_encoding_dropdown);

    data->is_updating = true; // Prevent recursive updates

    // Swap contents
    gtk_text_buffer_set_text(data->top_buffer, bottom_content, -1);
    gtk_text_buffer_set_text(data->bottom_buffer, top_content, -1);

    // Swap encodings
    gtk_drop_down_set_selected(data->top_encoding_dropdown, bottom_encoding);
    gtk_drop_down_set_selected(data->bottom_encoding_dropdown, top_encoding);

    g_free(top_content);
    g_free(bottom_content);

    data->is_updating = false;

    // Update conversion
    update_conversion(data);
}

// Create a text view with monospace font
static GtkWidget *create_text_view(GtkTextBuffer **buffer) {
    GtkWidget *text_view = gtk_text_view_new();
    *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view));

    // Set monospace font (JetBrains Nerd Font Mono)
    PangoFontDescription *font_desc = pango_font_description_from_string("JetBrains Nerd Font Mono 12");
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(text_view), TRUE);

    // Apply custom font using CSS
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_string(provider,
        "textview { font-family: 'JetBrains Nerd Font Mono'; font-size: 12pt; }");
    gtk_widget_add_css_class(text_view, "custom-font");
    gtk_style_context_add_provider_for_display(gdk_display_get_default(),
        GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);

    pango_font_description_free(font_desc);

    // Set other properties
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(text_view), GTK_WRAP_WORD_CHAR);
    gtk_widget_set_vexpand(text_view, TRUE);
    gtk_widget_set_hexpand(text_view, TRUE);

    return text_view;
}

// Create a dropdown for encoding selection
static GtkWidget *create_encoding_dropdown(const char *label_text, EncodingType default_encoding) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    GtkWidget *label = gtk_label_new(label_text);

    // Create string list for dropdown
    const char * const encoding_strings[] = {
        "Hex", "ASCII", "UTF-8", "UTF-16LE", "UTF-16BE",
        "UTF-32LE", "UTF-32BE", "ISO-8859-1", "ISO-8859-15",
        "Shift-JIS", "EUC-JP", "KOI8-R", NULL
    };
    GtkStringList *encodings = gtk_string_list_new(encoding_strings);

    // Create dropdown
    GtkWidget *dropdown = gtk_drop_down_new(G_LIST_MODEL(encodings), NULL);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(dropdown), default_encoding);

    // Pack widgets
    gtk_box_append(GTK_BOX(box), label);
    gtk_box_append(GTK_BOX(box), dropdown);

    return box;
}

// Application activate callback
static void activate(GtkApplication *app, gpointer user_data) {
    // If user_data is NULL, create a new window, otherwise use the provided window
    GtkWidget *window;
    if (user_data == NULL) {
        window = gtk_application_window_new(app);
    } else {
        window = GTK_WIDGET(user_data);
    }

    // Create window-specific data
    WindowData *data = g_malloc(sizeof(WindowData));
    memset(data, 0, sizeof(WindowData));
    data->is_updating = false;

    // Store the data in the window
    g_object_set_data(G_OBJECT(window), "window_data", data);

    // Connect destroy signal to free the data
    g_signal_connect(window, "destroy", G_CALLBACK(on_window_destroy), NULL);
    gtk_window_set_title(GTK_WINDOW(window), "Format Converter");
    gtk_window_set_default_size(GTK_WINDOW(window), 1000, 600);

    // Create menu bar
    GtkWidget *header_bar = gtk_header_bar_new();
    gtk_window_set_titlebar(GTK_WINDOW(window), header_bar);

    // Create Tools menu
    GtkWidget *tools_menu_button = gtk_menu_button_new();
    gtk_menu_button_set_label(GTK_MENU_BUTTON(tools_menu_button), "Tools");

    // Create menu model
    GMenu *tools_menu = g_menu_new();
    g_menu_append(tools_menu, "New Window", "app.new_window");
    g_menu_append(tools_menu, "AI Translator", "app.ai_translator");
    g_menu_append(tools_menu, "AI Settings", "app.ai_settings");

    // Set menu model to button
    gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(tools_menu_button), G_MENU_MODEL(tools_menu));
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header_bar), tools_menu_button);

    // Create actions
    GSimpleAction *new_window_action = g_simple_action_new("new_window", NULL);
    GSimpleAction *ai_translator_action = g_simple_action_new("ai_translator", NULL);
    GSimpleAction *ai_settings_action = g_simple_action_new("ai_settings", NULL);

    // Action handlers
    g_signal_connect(new_window_action, "activate", G_CALLBACK(open_new_window), app);
    g_signal_connect(ai_translator_action, "activate", G_CALLBACK(toggle_ai_translator), window);
    g_signal_connect(ai_settings_action, "activate", G_CALLBACK(show_ai_settings), window);

    // Add actions to application
    g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(new_window_action));
    g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(ai_translator_action));
    g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(ai_settings_action));

    // Variables for AI translator
    GtkWidget *ai_translator_box = NULL;
    gboolean ai_translator_visible = FALSE;

    // Main vertical box
    GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_start(main_box, 10);
    gtk_widget_set_margin_end(main_box, 10);
    gtk_widget_set_margin_top(main_box, 10);
    gtk_widget_set_margin_bottom(main_box, 10);

    // Top section
    GtkWidget *top_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    GtkWidget *top_label_box = create_encoding_dropdown("Format:", HEX);
    data->top_encoding_dropdown = GTK_DROP_DOWN(gtk_widget_get_last_child(top_label_box));

    // Create top text view with scrolling
    GtkWidget *top_scroll = gtk_scrolled_window_new();
    data->top_text_view = create_text_view(&data->top_buffer);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(top_scroll), data->top_text_view);

    // Create character/byte counter for top field
    data->top_counter_label = gtk_label_new("Characters: 0 | Bytes: 0");
    gtk_widget_set_halign(data->top_counter_label, GTK_ALIGN_END);
    gtk_widget_set_margin_top(data->top_counter_label, 2);
    gtk_widget_set_margin_bottom(data->top_counter_label, 5);

    // Pack top widgets
    gtk_box_append(GTK_BOX(top_box), top_label_box);
    gtk_box_append(GTK_BOX(top_box), top_scroll);
    gtk_box_append(GTK_BOX(top_box), data->top_counter_label);

    // Swap button
    GtkWidget *swap_button = gtk_button_new_with_label("⇅ Swap");
    gtk_widget_set_halign(swap_button, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(swap_button, 5);
    gtk_widget_set_margin_bottom(swap_button, 5);
    g_signal_connect(swap_button, "clicked", G_CALLBACK(on_swap_clicked), data);

    // Bottom section - will be a horizontal box to hold both the text view and AI translator
    GtkWidget *bottom_container = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);

    // Left side - normal text view
    GtkWidget *bottom_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    GtkWidget *bottom_label_box = create_encoding_dropdown("Format:", UTF8);
    data->bottom_encoding_dropdown = GTK_DROP_DOWN(gtk_widget_get_last_child(bottom_label_box));

    // Create bottom text view with scrolling
    GtkWidget *bottom_scroll = gtk_scrolled_window_new();
    data->bottom_text_view = create_text_view(&data->bottom_buffer);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(bottom_scroll), data->bottom_text_view);

    // Create character/byte counter for bottom field
    data->bottom_counter_label = gtk_label_new("Characters: 0 | Bytes: 0");
    gtk_widget_set_halign(data->bottom_counter_label, GTK_ALIGN_END);
    gtk_widget_set_margin_top(data->bottom_counter_label, 2);
    gtk_widget_set_margin_bottom(data->bottom_counter_label, 5);

    // Pack bottom widgets
    gtk_box_append(GTK_BOX(bottom_box), bottom_label_box);
    gtk_box_append(GTK_BOX(bottom_box), bottom_scroll);
    gtk_box_append(GTK_BOX(bottom_box), data->bottom_counter_label);

    // Add the bottom box to the container
    gtk_box_append(GTK_BOX(bottom_container), bottom_box);
    gtk_widget_set_hexpand(bottom_box, TRUE);

    // Right side - AI translator (initially hidden)
    data->ai_translator_box = create_ai_translator_ui(window);
    if (data->ai_translator_box != NULL) {
        // Get the AI translation buffer and send button from the AI translator box
        data->ai_translation_buffer = g_object_get_data(G_OBJECT(data->ai_translator_box), "ai_translation_buffer");
        data->send_to_ai_button = g_object_get_data(G_OBJECT(data->ai_translator_box), "send_to_ai_button");

        // Connect the send button signal
        if (data->send_to_ai_button != NULL) {
            g_signal_connect(data->send_to_ai_button, "clicked", G_CALLBACK(on_send_to_ai_clicked), window);
        }

        gtk_widget_set_visible(data->ai_translator_box, FALSE);
        gtk_box_append(GTK_BOX(bottom_container), data->ai_translator_box);
        gtk_widget_set_hexpand(data->ai_translator_box, TRUE);
    } else {
        fprintf(stderr, "ERROR: Failed to create AI translator UI\n");
    }

    // Pack everything into main box
    gtk_box_append(GTK_BOX(main_box), top_box);
    gtk_box_append(GTK_BOX(main_box), swap_button);
    gtk_box_append(GTK_BOX(main_box), bottom_container);

    // Set window content
    gtk_window_set_child(GTK_WINDOW(window), main_box);

    // Connect signals
    g_signal_connect(data->top_buffer, "changed", G_CALLBACK(on_text_buffer_changed), data);
    g_signal_connect(data->bottom_buffer, "changed", G_CALLBACK(on_text_buffer_changed), data);
    g_signal_connect(data->top_encoding_dropdown, "notify::selected", G_CALLBACK(on_encoding_changed), data);
    g_signal_connect(data->bottom_encoding_dropdown, "notify::selected", G_CALLBACK(on_encoding_changed), data);

    // Store references for use in action handlers
    g_object_set_data(G_OBJECT(window), "window_data", data);
    g_object_set_data(G_OBJECT(window), "top_buffer", data->top_buffer);
    g_object_set_data(G_OBJECT(window), "bottom_buffer", data->bottom_buffer);
    g_object_set_data(G_OBJECT(window), "top_encoding_dropdown", data->top_encoding_dropdown);
    g_object_set_data(G_OBJECT(window), "bottom_encoding_dropdown", data->bottom_encoding_dropdown);

    // Initial update of the counters
    update_counter_labels(data);

    // Show window
    gtk_window_present(GTK_WINDOW(window));
}

// Open a new window
static void open_new_window(GSimpleAction *action, GVariant *parameter, gpointer user_data) {
    GtkApplication *app = GTK_APPLICATION(user_data);

    // Create a new window
    GtkWidget *window = gtk_application_window_new(app);

    // Call the activate function to set up the window
    activate(app, window);

    // Show the window
    gtk_window_present(GTK_WINDOW(window));
}

// Toggle AI translator visibility
static void toggle_ai_translator(GSimpleAction *action, GVariant *parameter, gpointer user_data) {
    fprintf(stderr, "DEBUG: toggle_ai_translator() called\n");

    if (user_data == NULL) {
        fprintf(stderr, "ERROR: user_data is NULL in toggle_ai_translator()\n");
        return;
    }

    GtkWidget *window = GTK_WIDGET(user_data);
    fprintf(stderr, "DEBUG: window=%p\n", window);

    WindowData *data = g_object_get_data(G_OBJECT(window), "window_data");
    if (data == NULL) {
        fprintf(stderr, "ERROR: window_data is NULL!\n");
        return;
    }

    if (data->ai_translator_box != NULL) {
        gboolean visible = gtk_widget_get_visible(data->ai_translator_box);
        fprintf(stderr, "DEBUG: Current visibility: %d, setting to: %d\n", visible, !visible);
        gtk_widget_set_visible(data->ai_translator_box, !visible);
    } else {
        fprintf(stderr, "ERROR: ai_translator_box is NULL!\n");
    }

    fprintf(stderr, "DEBUG: toggle_ai_translator() completed\n");
}

// Show AI settings dialog
static void show_ai_settings(GSimpleAction *action, GVariant *parameter, gpointer user_data) {
    GtkWidget *window = GTK_WIDGET(user_data);
    show_ai_settings_dialog(window);
}

// Callback for the "Send to AI" button
static void on_send_to_ai_clicked(GtkButton *button, gpointer user_data) {
    GtkWidget *window = GTK_WIDGET(user_data);
    if (window == NULL) {
        fprintf(stderr, "ERROR: window is NULL in on_send_to_ai_clicked\n");
        return;
    }

    WindowData *data = g_object_get_data(G_OBJECT(window), "window_data");
    if (data == NULL) {
        fprintf(stderr, "ERROR: window_data is NULL in on_send_to_ai_clicked\n");
        return;
    }

    // Get the text from the bottom text view (the converted text)
    if (data->bottom_buffer == NULL || data->top_encoding_dropdown == NULL ||
        data->bottom_encoding_dropdown == NULL || data->ai_translation_buffer == NULL) {
        fprintf(stderr, "ERROR: One or more required objects is NULL in on_send_to_ai_clicked\n");
        return;
    }

    // Get the text from the bottom buffer
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(data->bottom_buffer, &start, &end);
    char *text = gtk_text_buffer_get_text(data->bottom_buffer, &start, &end, FALSE);

    // Get the encoding types
    guint source_format_index = gtk_drop_down_get_selected(data->top_encoding_dropdown);
    guint target_format_index = gtk_drop_down_get_selected(data->bottom_encoding_dropdown);

    // Convert index to format name using our utility function
    const char *source_format = encoding_type_to_string((EncodingType)source_format_index);
    const char *target_format = encoding_type_to_string((EncodingType)target_format_index);

    // Check if we got valid format names
    if (strcmp(source_format, "Unknown") == 0 || strcmp(target_format, "Unknown") == 0) {
        fprintf(stderr, "ERROR: Format index out of bounds\n");
        g_free(text);
        return;
    }

    // Send to AI for translation
    send_to_ai_translation(window, data->ai_translation_buffer, text, source_format, target_format);

    g_free(text);
}

// Window destroy callback
static void on_window_destroy(GtkWidget *window, gpointer user_data) {
    WindowData *data = g_object_get_data(G_OBJECT(window), "window_data");
    if (data != NULL) {
        g_free(data);
    }
}

int main(int argc, char *argv[]) {
    GtkApplication *app;
    int status;

    app = gtk_application_new("com.example.hex2text", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);

    return status;
}