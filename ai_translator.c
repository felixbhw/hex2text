#include "ai_translator.h"
#include "common.h"
#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <curl/curl.h>
#include <json-c/json.h>

// Struct for curl response data
struct MemoryStruct {
    char *memory;
    size_t size;
};

// Callback function for curl to write received data
static size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    struct MemoryStruct *mem = (struct MemoryStruct *)userp;

    // Use g_realloc for consistency with the rest of the codebase
    char *ptr = g_realloc(mem->memory, mem->size + realsize + 1);
    if (!ptr) {
        fprintf(stderr, "Not enough memory (g_realloc returned NULL)\n");
        return 0;
    }

    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;

    return realsize;
}

// Global variables for settings dialog
static GtkWidget *ai_settings_dialog = NULL;
static GtkWidget *api_key_entry = NULL;
static GtkWidget *model_name_entry = NULL;
static GtkWidget *translate_to_entry = NULL;
static GtkWidget *translate_from_entry = NULL;
static GtkDropDown *provider_combo = NULL;
static GtkWidget *custom_context_text_view = NULL;
static GtkTextBuffer *custom_context_buffer = NULL;

// Current settings
static AIProvider current_provider = OPENAI;
static char *openai_api_key = NULL;
static char *gemini_api_key = NULL;
static char *custom_context = NULL;
static char *openai_model = NULL;
static char *gemini_model = NULL;
static char *translate_to = NULL;
static char *translate_from = NULL;

// Config file paths (stored in user's home directory)
static char *get_config_dir() {
    const char *home_dir = g_get_home_dir();
    return g_build_filename(home_dir, ".hex2text", NULL);
}

static char *get_openai_key_path() {
    char *config_dir = get_config_dir();
    char *path = g_build_filename(config_dir, "openai_key", NULL);
    g_free(config_dir);
    return path;
}

static char *get_gemini_key_path() {
    char *config_dir = get_config_dir();
    char *path = g_build_filename(config_dir, "gemini_key", NULL);
    g_free(config_dir);
    return path;
}

static char *get_context_path() {
    char *config_dir = get_config_dir();
    char *path = g_build_filename(config_dir, "custom_context", NULL);
    g_free(config_dir);
    return path;
}

static char *get_openai_model_path() {
    char *config_dir = get_config_dir();
    char *path = g_build_filename(config_dir, "openai_model", NULL);
    g_free(config_dir);
    return path;
}

static char *get_gemini_model_path() {
    char *config_dir = get_config_dir();
    char *path = g_build_filename(config_dir, "gemini_model", NULL);
    g_free(config_dir);
    return path;
}

static char *get_translate_to_path() {
    char *config_dir = get_config_dir();
    char *path = g_build_filename(config_dir, "translate_to", NULL);
    g_free(config_dir);
    return path;
}

static char *get_translate_from_path() {
    char *config_dir = get_config_dir();
    char *path = g_build_filename(config_dir, "translate_from", NULL);
    g_free(config_dir);
    return path;
}

// Ensure config directory exists
static void ensure_config_dir() {
    char *config_dir = get_config_dir();
    if (!g_file_test(config_dir, G_FILE_TEST_IS_DIR)) {
        g_mkdir_with_parents(config_dir, 0700);  // Secure permissions
    }
    g_free(config_dir);
}

// Function to save API keys securely
void save_api_key(AIProvider provider, const char *api_key) {
    ensure_config_dir();

    char *key_path = NULL;
    if (provider == OPENAI) {
        key_path = get_openai_key_path();
    } else {
        key_path = get_gemini_key_path();
    }

    FILE *file = fopen(key_path, "w");
    if (file) {
        fprintf(file, "%s", api_key);
        fclose(file);
        // Set secure permissions
        chmod(key_path, 0600);
    }

    g_free(key_path);
}

// Function to load API keys securely
char* load_api_key(AIProvider provider) {
    char *key_path = NULL;
    if (provider == OPENAI) {
        key_path = get_openai_key_path();
    } else {
        key_path = get_gemini_key_path();
    }

    char *api_key = NULL;
    FILE *file = fopen(key_path, "r");
    if (file) {
        // Get file size
        fseek(file, 0, SEEK_END);
        long size = ftell(file);
        fseek(file, 0, SEEK_SET);

        // Read the key
        api_key = g_malloc(size + 1);
        fread(api_key, 1, size, file);
        api_key[size] = '\0';

        fclose(file);
    }

    g_free(key_path);
    return api_key;
}

// Function to save custom context
void save_custom_context(const char *context) {
    ensure_config_dir();

    char *context_path = get_context_path();
    FILE *file = fopen(context_path, "w");
    if (file) {
        fprintf(file, "%s", context);
        fclose(file);
    }

    g_free(context_path);
}

// Function to load custom context
char* load_custom_context(void) {
    char *context_path = get_context_path();
    char *context = NULL;

    FILE *file = fopen(context_path, "r");
    if (file) {
        // Get file size
        fseek(file, 0, SEEK_END);
        long size = ftell(file);
        fseek(file, 0, SEEK_SET);

        // Read the context
        context = g_malloc(size + 1);
        fread(context, 1, size, file);
        context[size] = '\0';

        fclose(file);
    }

    g_free(context_path);
    return context;
}

// Function to save model name
void save_model_name(AIProvider provider, const char *model_name) {
    ensure_config_dir();

    char *model_path = NULL;
    if (provider == OPENAI) {
        model_path = get_openai_model_path();
    } else {
        model_path = get_gemini_model_path();
    }

    FILE *file = fopen(model_path, "w");
    if (file) {
        fprintf(file, "%s", model_name);
        fclose(file);
    }

    g_free(model_path);
}

// Function to load model name
char* load_model_name(AIProvider provider) {
    char *model_path = NULL;
    if (provider == OPENAI) {
        model_path = get_openai_model_path();
    } else {
        model_path = get_gemini_model_path();
    }

    char *model_name = NULL;
    FILE *file = fopen(model_path, "r");
    if (file) {
        // Get file size
        fseek(file, 0, SEEK_END);
        long size = ftell(file);
        fseek(file, 0, SEEK_SET);

        // Read the model name
        model_name = g_malloc(size + 1);
        fread(model_name, 1, size, file);
        model_name[size] = '\0';

        fclose(file);
    }

    g_free(model_path);
    return model_name;
}

// Function to save translation language
void save_translate_to(const char *language) {
    ensure_config_dir();

    char *path = get_translate_to_path();
    FILE *file = fopen(path, "w");
    if (file) {
        fprintf(file, "%s", language);
        fclose(file);
    }

    g_free(path);
}

// Function to load translation language
char* load_translate_to(void) {
    char *path = get_translate_to_path();
    char *language = NULL;

    FILE *file = fopen(path, "r");
    if (file) {
        // Get file size
        fseek(file, 0, SEEK_END);
        long size = ftell(file);
        fseek(file, 0, SEEK_SET);

        // Read the language
        language = g_malloc(size + 1);
        fread(language, 1, size, file);
        language[size] = '\0';

        fclose(file);
    }

    g_free(path);
    return language;
}

// Function to save source language
void save_translate_from(const char *language) {
    ensure_config_dir();

    char *path = get_translate_from_path();
    FILE *file = fopen(path, "w");
    if (file) {
        fprintf(file, "%s", language);
        fclose(file);
    }

    g_free(path);
}

// Function to load source language
char* load_translate_from(void) {
    char *path = get_translate_from_path();
    char *language = NULL;

    FILE *file = fopen(path, "r");
    if (file) {
        // Get file size
        fseek(file, 0, SEEK_END);
        long size = ftell(file);
        fseek(file, 0, SEEK_SET);

        // Read the language
        language = g_malloc(size + 1);
        fread(language, 1, size, file);
        language[size] = '\0';

        fclose(file);
    }

    g_free(path);
    return language;
}

// Function to check if an API key is valid
bool check_api_key(AIProvider provider, const char *api_key) {
    if (api_key == NULL || strlen(api_key) < 10) {
        return false;
    }

    CURL *curl;
    CURLcode res;
    struct MemoryStruct chunk;
    bool is_valid = false;

    chunk.memory = g_malloc(1);  // will be grown as needed by g_realloc
    chunk.size = 0;    // no data at this point

    curl_global_init(CURL_GLOBAL_ALL);
    curl = curl_easy_init();

    if (curl) {
        struct curl_slist *headers = NULL;

        if (provider == OPENAI) {
            // OpenAI API endpoint for a simple models list request
            curl_easy_setopt(curl, CURLOPT_URL, "https://api.openai.com/v1/models");

            // Set headers
            char auth_header[256];
            snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", api_key);
            headers = curl_slist_append(headers, auth_header);
            headers = curl_slist_append(headers, "Content-Type: application/json");
        } else {
            // Gemini API endpoint for a simple models list request
            char url[256];
            snprintf(url, sizeof(url), "https://generativelanguage.googleapis.com/v1/models?key=%s", api_key);
            curl_easy_setopt(curl, CURLOPT_URL, url);

            // Set headers
            headers = curl_slist_append(headers, "Content-Type: application/json");
        }

        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L); // 5 second timeout

        // Perform the request
        res = curl_easy_perform(curl);

        // Check for errors
        if (res == CURLE_OK) {
            long response_code;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);

            // 200 OK means the API key is valid
            if (response_code == 200) {
                is_valid = true;
            }
        }

        // Clean up
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    }

    curl_global_cleanup();
    g_free(chunk.memory);

    return is_valid;
}

// Function to create the prompt for AI translation
static char* create_translation_prompt(const char *text, const char *source_format, const char *target_format) {
    // Load custom context if available
    if (custom_context == NULL) {
        custom_context = load_custom_context();
    }

    // Load translation languages if not already loaded
    if (translate_to == NULL) {
        translate_to = load_translate_to();
        if (translate_to == NULL || strlen(translate_to) == 0) {
            translate_to = g_strdup("English"); // Default target language
        }
    }

    if (translate_from == NULL) {
        translate_from = load_translate_from();
    }

    // Create the prompt
    GString *prompt = g_string_new(NULL);

    g_string_append(prompt, "You are a specialized language and format translator. ");
    g_string_append(prompt, "Translate the following content. ");
    g_string_append(prompt, "Provide the translation and a character-by-character breakdown where relevant. ");
    g_string_append(prompt, "Preserve any control code structures or formatting (things like <|, etc). ");
    g_string_append(prompt, "Be concise and focus only on the translation task. ");

    // Add custom context if available
    if (custom_context != NULL && strlen(custom_context) > 0) {
        g_string_append(prompt, "\n\nContext for translation: ");
        g_string_append(prompt, custom_context);
    }

    g_string_append_printf(prompt, "\n\nSource format: %s", source_format);
    g_string_append_printf(prompt, "\nTarget format: %s", target_format);

    // Add translation languages
    g_string_append_printf(prompt, "\nTranslate to: %s", translate_to);

    if (translate_from != NULL && strlen(translate_from) > 0) {
        g_string_append_printf(prompt, "\nTranslate from: %s", translate_from);
    } else {
        g_string_append(prompt, "\nTranslate from: Auto-Detect (please specify if multiple languages are detected)");
    }

    g_string_append(prompt, "\n\nContent to translate:\n");
    g_string_append(prompt, text);

    char *result = g_string_free(prompt, FALSE);
    return result;
}

// Function to send text to OpenAI for translation
static char* send_to_openai(const char *prompt) {
    if (openai_api_key == NULL) {
        openai_api_key = load_api_key(OPENAI);
        if (openai_api_key == NULL || strlen(openai_api_key) < 10) {
            return g_strdup("Error: No valid OpenAI API key found. Please set it in AI Settings.");
        }
    }

    CURL *curl;
    CURLcode res;
    struct MemoryStruct chunk;
    char *translation = NULL;

    chunk.memory = g_malloc(1);  // will be grown as needed by g_realloc
    chunk.size = 0;    // no data at this point

    curl_global_init(CURL_GLOBAL_ALL);
    curl = curl_easy_init();

    if (curl) {
        // Create JSON payload
        json_object *json_payload = json_object_new_object();
        json_object *messages_array = json_object_new_array();

        // Add system message
        json_object *system_message = json_object_new_object();
        json_object_object_add(system_message, "role", json_object_new_string("system"));
        json_object_object_add(system_message, "content", json_object_new_string("You are a specialized format translator. Provide only the translation and a brief byte-by-byte breakdown. Preserve any control code structures or formatting (things like <|, etc). Be concise and focus only on the translation task."));
        json_object_array_add(messages_array, system_message);

        // Add user message with prompt
        json_object *user_message = json_object_new_object();
        json_object_object_add(user_message, "role", json_object_new_string("user"));
        json_object_object_add(user_message, "content", json_object_new_string(prompt));
        json_object_array_add(messages_array, user_message);

        // Add messages to payload
        json_object_object_add(json_payload, "messages", messages_array);

        // Use custom model if available, otherwise use default
        if (openai_model != NULL && strlen(openai_model) > 0) {
            json_object_object_add(json_payload, "model", json_object_new_string(openai_model));
        } else {
            json_object_object_add(json_payload, "model", json_object_new_string("gpt-3.5-turbo"));
        }

        json_object_object_add(json_payload, "temperature", json_object_new_double(0.3));
        json_object_object_add(json_payload, "max_tokens", json_object_new_int(2048));

        // Convert to string
        const char *json_str = json_object_to_json_string(json_payload);

        // Set up headers
        struct curl_slist *headers = NULL;
        char auth_header[256];
        snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", openai_api_key);
        headers = curl_slist_append(headers, auth_header);
        headers = curl_slist_append(headers, "Content-Type: application/json");

        // Set up request
        curl_easy_setopt(curl, CURLOPT_URL, "https://api.openai.com/v1/chat/completions");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_str);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);

        // Perform the request
        res = curl_easy_perform(curl);

        // Check for errors
        if (res == CURLE_OK) {
            // Parse the JSON response
            json_object *json_response = json_tokener_parse(chunk.memory);
            if (json_response != NULL) {
                json_object *choices;
                if (json_object_object_get_ex(json_response, "choices", &choices)) {
                    json_object *first_choice = json_object_array_get_idx(choices, 0);
                    if (first_choice != NULL) {
                        json_object *message;
                        if (json_object_object_get_ex(first_choice, "message", &message)) {
                            json_object *content;
                            if (json_object_object_get_ex(message, "content", &content)) {
                                translation = g_strdup(json_object_get_string(content));
                            }
                        }
                    }
                }

                // Check for error message
                if (translation == NULL) {
                    json_object *error;
                    if (json_object_object_get_ex(json_response, "error", &error)) {
                        json_object *message;
                        if (json_object_object_get_ex(error, "message", &message)) {
                            translation = g_strdup_printf("Error: %s", json_object_get_string(message));
                        }
                    }
                }

                json_object_put(json_response);
            }
        } else {
            translation = g_strdup_printf("Error: %s", curl_easy_strerror(res));
        }

        // Clean up
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        json_object_put(json_payload);
    }

    curl_global_cleanup();
    g_free(chunk.memory);

    if (translation == NULL) {
        translation = g_strdup("Error: Failed to get translation from OpenAI.");
    }

    return translation;
}

// Function to send text to Gemini for translation
static char* send_to_gemini(const char *prompt) {
    if (gemini_api_key == NULL) {
        gemini_api_key = load_api_key(GEMINI);
        if (gemini_api_key == NULL || strlen(gemini_api_key) < 10) {
            return g_strdup("Error: No valid Gemini API key found. Please set it in AI Settings.");
        }
    }

    CURL *curl;
    CURLcode res;
    struct MemoryStruct chunk;
    char *translation = NULL;

    chunk.memory = g_malloc(1);  // will be grown as needed by g_realloc
    chunk.size = 0;    // no data at this point

    curl_global_init(CURL_GLOBAL_ALL);
    curl = curl_easy_init();

    if (curl) {
        // Create JSON payload
        json_object *json_payload = json_object_new_object();
        json_object *contents_array = json_object_new_array();

        // Add system instructions
        json_object *system_content = json_object_new_object();
        json_object *system_parts_array = json_object_new_array();
        json_object *system_text_part = json_object_new_object();
        json_object_object_add(system_text_part, "text", json_object_new_string("You are a specialized format translator. Provide only the translation and a brief byte-by-byte breakdown. Preserve any control code structures or formatting (things like <|, etc). Be concise and focus only on the translation task."));
        json_object_array_add(system_parts_array, system_text_part);
        json_object_object_add(system_content, "parts", system_parts_array);
        json_object_object_add(system_content, "role", json_object_new_string("system"));
        json_object_array_add(contents_array, system_content);

        // Add user content part
        json_object *user_content = json_object_new_object();
        json_object *user_parts_array = json_object_new_array();
        json_object *user_text_part = json_object_new_object();
        json_object_object_add(user_text_part, "text", json_object_new_string(prompt));
        json_object_array_add(user_parts_array, user_text_part);
        json_object_object_add(user_content, "parts", user_parts_array);
        json_object_object_add(user_content, "role", json_object_new_string("user"));
        json_object_array_add(contents_array, user_content);

        // Add contents to payload
        json_object_object_add(json_payload, "contents", contents_array);

        // Add generation config
        json_object *gen_config = json_object_new_object();
        json_object_object_add(gen_config, "temperature", json_object_new_double(0.3));
        json_object_object_add(gen_config, "maxOutputTokens", json_object_new_int(2048));
        json_object_object_add(json_payload, "generationConfig", gen_config);

        // Convert to string
        const char *json_str = json_object_to_json_string(json_payload);

        // Set up URL with API key and model
        char url[512];
        const char *model_name = "gemini-2.0-flash";

        // Use custom model if available
        if (gemini_model != NULL && strlen(gemini_model) > 0) {
            model_name = gemini_model;
        }

        snprintf(url, sizeof(url), "https://generativelanguage.googleapis.com/v1beta/models/%s:generateContent?key=%s",
                model_name, gemini_api_key);

        // Set up headers
        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, "Content-Type: application/json");

        // Set up request
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_str);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);

        // Perform the request
        res = curl_easy_perform(curl);

        // Check for errors
        if (res == CURLE_OK) {
            // Parse the JSON response
            json_object *json_response = json_tokener_parse(chunk.memory);
            if (json_response != NULL) {
                json_object *candidates;
                if (json_object_object_get_ex(json_response, "candidates", &candidates)) {
                    json_object *first_candidate = json_object_array_get_idx(candidates, 0);
                    if (first_candidate != NULL) {
                        json_object *content;
                        if (json_object_object_get_ex(first_candidate, "content", &content)) {
                            json_object *parts;
                            if (json_object_object_get_ex(content, "parts", &parts)) {
                                json_object *first_part = json_object_array_get_idx(parts, 0);
                                if (first_part != NULL) {
                                    json_object *text;
                                    if (json_object_object_get_ex(first_part, "text", &text)) {
                                        translation = g_strdup(json_object_get_string(text));
                                    }
                                }
                            }
                        }
                    }
                }

                // Check for error message
                if (translation == NULL) {
                    json_object *error;
                    if (json_object_object_get_ex(json_response, "error", &error)) {
                        json_object *message;
                        if (json_object_object_get_ex(error, "message", &message)) {
                            translation = g_strdup_printf("Error: %s", json_object_get_string(message));
                        }
                    }
                }

                json_object_put(json_response);
            }
        } else {
            translation = g_strdup_printf("Error: %s", curl_easy_strerror(res));
        }

        // Clean up
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        json_object_put(json_payload);
    }

    curl_global_cleanup();
    g_free(chunk.memory);

    if (translation == NULL) {
        translation = g_strdup("Error: Failed to get translation from Gemini.");
    }

    return translation;
}

// Function to send text to AI for translation
void send_to_ai_translation(GtkWidget *parent_window, GtkTextBuffer *ai_buffer, const char *text, const char *source_format, const char *target_format) {
    fprintf(stderr, "DEBUG: send_to_ai_translation() called\n");

    if (parent_window == NULL || ai_buffer == NULL) {
        fprintf(stderr, "ERROR: parent_window or ai_buffer is NULL in send_to_ai_translation()\n");
        return;
    }

    if (text == NULL || source_format == NULL || target_format == NULL) {
        fprintf(stderr, "ERROR: One or more required parameters is NULL in send_to_ai_translation()\n");
        return;
    }

    fprintf(stderr, "DEBUG: ai_buffer=%p\n", ai_buffer);

    // Load model names if not already loaded
    if (openai_model == NULL) {
        fprintf(stderr, "DEBUG: Loading OpenAI model name\n");
        openai_model = load_model_name(OPENAI);
        if (openai_model == NULL || strlen(openai_model) == 0) {
            fprintf(stderr, "DEBUG: Using default OpenAI model\n");
            openai_model = g_strdup("gpt-3.5-turbo"); // Default model
        } else {
            fprintf(stderr, "DEBUG: Loaded OpenAI model: %s\n", openai_model);
        }
    }

    if (gemini_model == NULL) {
        fprintf(stderr, "DEBUG: Loading Gemini model name\n");
        gemini_model = load_model_name(GEMINI);
        if (gemini_model == NULL || strlen(gemini_model) == 0) {
            fprintf(stderr, "DEBUG: Using default Gemini model\n");
            gemini_model = g_strdup("gemini-2.0-flash"); // Default model
        } else {
            fprintf(stderr, "DEBUG: Loaded Gemini model: %s\n", gemini_model);
        }
    }

    // Create the prompt
    fprintf(stderr, "DEBUG: Creating translation prompt\n");
    char *prompt = create_translation_prompt(text, source_format, target_format);
    fprintf(stderr, "DEBUG: Prompt created, length: %zu bytes\n", strlen(prompt));

    // Set the AI translation view to "Loading..."
    fprintf(stderr, "DEBUG: Setting 'Loading...' message\n");
    gtk_text_buffer_set_text(ai_buffer, "Loading translation...", -1);

    // Process GTK events to update the UI
    fprintf(stderr, "DEBUG: Processing GTK events\n");
    while (g_main_context_pending(NULL))
        g_main_context_iteration(NULL, FALSE);

    // Send to the appropriate AI provider
    char *translation = NULL;
    fprintf(stderr, "DEBUG: current_provider=%d (0=OpenAI, 1=Gemini)\n", current_provider);
    if (current_provider == OPENAI) {
        fprintf(stderr, "DEBUG: Sending to OpenAI\n");
        translation = send_to_openai(prompt);
    } else {
        fprintf(stderr, "DEBUG: Sending to Gemini\n");
        translation = send_to_gemini(prompt);
    }

    fprintf(stderr, "DEBUG: Got translation response: %p\n", translation);

    // Update the AI translation view
    if (translation != NULL) {
        fprintf(stderr, "DEBUG: Updating AI translation view with result\n");
        gtk_text_buffer_set_text(ai_buffer, translation, -1);
    } else {
        fprintf(stderr, "DEBUG: Cannot update view: translation is NULL\n");
    }

    // Free memory
    g_free(prompt);
    g_free(translation);
    fprintf(stderr, "DEBUG: send_to_ai_translation() completed\n");
}



// Callback for the "Test API Key" button
static void on_test_api_key_clicked(GtkButton *button, gpointer user_data) {
    // Get the API key from the entry
    const char *api_key = gtk_editable_get_text(GTK_EDITABLE(api_key_entry));

    // Get the selected provider
    guint provider_index = gtk_drop_down_get_selected(provider_combo);
    AIProvider provider = (AIProvider)provider_index;

    // Test the API key
    bool is_valid = check_api_key(provider, api_key);

    // Show a message dialog with the result
    GtkAlertDialog *alert;
    if (is_valid) {
        alert = gtk_alert_dialog_new("API key is valid!");
    } else {
        alert = gtk_alert_dialog_new("API key is invalid or could not be verified.");
    }

    gtk_alert_dialog_set_modal(alert, TRUE);
    gtk_alert_dialog_show(alert, GTK_WINDOW(ai_settings_dialog));
    g_object_unref(alert);
}

// Callback for the "Save" button in AI settings
static void on_ai_settings_save_clicked(GtkButton *button, gpointer user_data) {
    // Get the API key from the entry
    const char *api_key = gtk_editable_get_text(GTK_EDITABLE(api_key_entry));

    // Get the model name from the entry
    const char *model_name = gtk_editable_get_text(GTK_EDITABLE(model_name_entry));

    // Get the selected provider
    guint provider_index = gtk_drop_down_get_selected(provider_combo);
    current_provider = (AIProvider)provider_index;

    // Save the API key
    save_api_key(current_provider, api_key);

    // Save the model name
    save_model_name(current_provider, model_name);

    // Get and save translation languages
    const char *translate_to_text = gtk_editable_get_text(GTK_EDITABLE(translate_to_entry));
    const char *translate_from_text = gtk_editable_get_text(GTK_EDITABLE(translate_from_entry));

    save_translate_to(translate_to_text);
    save_translate_from(translate_from_text);

    // Save the custom context
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(custom_context_buffer, &start, &end);
    char *context = gtk_text_buffer_get_text(custom_context_buffer, &start, &end, FALSE);
    save_custom_context(context);
    g_free(context);

    // Update the global variables
    if (current_provider == OPENAI) {
        g_free(openai_api_key);
        openai_api_key = g_strdup(api_key);
        g_free(openai_model);
        openai_model = g_strdup(model_name);
    } else {
        g_free(gemini_api_key);
        gemini_api_key = g_strdup(api_key);
        g_free(gemini_model);
        gemini_model = g_strdup(model_name);
    }

    // Update translation languages
    g_free(translate_to);
    translate_to = g_strdup(translate_to_text);

    g_free(translate_from);
    translate_from = g_strdup(translate_from_text);

    g_free(custom_context);
    custom_context = load_custom_context();

    // Close the dialog
    gtk_window_destroy(GTK_WINDOW(ai_settings_dialog));
    ai_settings_dialog = NULL;
}

// Callback for provider dropdown change
static void on_provider_changed(GtkDropDown *dropdown, GParamSpec *pspec, gpointer user_data) {
    // Get the selected provider
    guint provider_index = gtk_drop_down_get_selected(dropdown);
    AIProvider provider = (AIProvider)provider_index;

    // Load the API key for the selected provider
    char *api_key = load_api_key(provider);
    if (api_key != NULL) {
        gtk_editable_set_text(GTK_EDITABLE(api_key_entry), api_key);
        g_free(api_key);
    } else {
        gtk_editable_set_text(GTK_EDITABLE(api_key_entry), "");
    }

    // Load the model name for the selected provider
    char *model_name = load_model_name(provider);
    if (model_name != NULL) {
        gtk_editable_set_text(GTK_EDITABLE(model_name_entry), model_name);
        g_free(model_name);
    } else {
        // Set default model name based on provider
        if (provider == OPENAI) {
            gtk_editable_set_text(GTK_EDITABLE(model_name_entry), "gpt-3.5-turbo");
        } else {
            gtk_editable_set_text(GTK_EDITABLE(model_name_entry), "gemini-2.0-flash");
        }
    }
}

// Function to show the AI settings dialog
void show_ai_settings_dialog(GtkWidget *parent_window) {
    // Create the dialog
    ai_settings_dialog = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(ai_settings_dialog), "AI Settings");
    gtk_window_set_modal(GTK_WINDOW(ai_settings_dialog), TRUE);
    gtk_window_set_transient_for(GTK_WINDOW(ai_settings_dialog), GTK_WINDOW(parent_window));
    gtk_window_set_destroy_with_parent(GTK_WINDOW(ai_settings_dialog), TRUE);

    // Set default size
    gtk_window_set_default_size(GTK_WINDOW(ai_settings_dialog), 500, 400);

    // Create a vertical box for content
    GtkWidget *content_area = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_start(content_area, 10);
    gtk_widget_set_margin_end(content_area, 10);
    gtk_widget_set_margin_top(content_area, 10);
    gtk_widget_set_margin_bottom(content_area, 10);
    gtk_window_set_child(GTK_WINDOW(ai_settings_dialog), content_area);

    // Create the provider selection
    GtkWidget *provider_label = gtk_label_new("AI Provider:");
    gtk_widget_set_halign(provider_label, GTK_ALIGN_START);

    // Create string list for dropdown
    const char * const provider_strings[] = {"OpenAI", "Gemini", NULL};
    GtkStringList *providers = gtk_string_list_new(provider_strings);

    // Create dropdown
    GtkWidget *provider_dropdown = gtk_drop_down_new(G_LIST_MODEL(providers), NULL);
    provider_combo = GTK_DROP_DOWN(provider_dropdown);
    gtk_drop_down_set_selected(provider_combo, current_provider);

    // Create the API key entry
    GtkWidget *api_key_label = gtk_label_new("API Key:");
    gtk_widget_set_halign(api_key_label, GTK_ALIGN_START);

    api_key_entry = gtk_password_entry_new();
    gtk_password_entry_set_show_peek_icon(GTK_PASSWORD_ENTRY(api_key_entry), TRUE);

    // Load the API key for the current provider
    char *api_key = load_api_key(current_provider);
    if (api_key != NULL) {
        gtk_editable_set_text(GTK_EDITABLE(api_key_entry), api_key);
        g_free(api_key);
    }

    // Create the test button
    GtkWidget *test_button = gtk_button_new_with_label("Test API Key");

    // Create the model name entry
    GtkWidget *model_label = gtk_label_new("Model Name:");
    gtk_widget_set_halign(model_label, GTK_ALIGN_START);

    model_name_entry = gtk_entry_new();

    // Load the model name for the current provider
    char *model_name = load_model_name(current_provider);
    if (model_name != NULL && strlen(model_name) > 0) {
        gtk_editable_set_text(GTK_EDITABLE(model_name_entry), model_name);
        g_free(model_name);
    } else {
        // Set default model name based on provider
        if (current_provider == OPENAI) {
            gtk_editable_set_text(GTK_EDITABLE(model_name_entry), "gpt-3.5-turbo");
        } else {
            gtk_editable_set_text(GTK_EDITABLE(model_name_entry), "gemini-2.0-flash");
        }
    }

    // Add a tooltip to the model name entry
    gtk_widget_set_tooltip_text(model_name_entry,
        "Enter the model name to use for API calls (e.g., gpt-3.5-turbo, gpt-4 for OpenAI or gemini-2.0-flash, gemini-pro for Gemini)");

    // Create the custom context section
    GtkWidget *context_label = gtk_label_new("Custom Context for Translation:");
    gtk_widget_set_halign(context_label, GTK_ALIGN_START);

    // Create a help icon with tooltip
    GtkWidget *help_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_append(GTK_BOX(help_box), context_label);

    GtkWidget *help_icon = gtk_image_new_from_icon_name("help-about-symbolic");
    gtk_widget_set_tooltip_text(help_icon, "This is helpful if working on translating something for a specific context (game, program) or franchise.");
    gtk_box_append(GTK_BOX(help_box), help_icon);

    // Create the custom context text view
    GtkWidget *context_scroll = gtk_scrolled_window_new();
    GtkWidget *context_text_view = gtk_text_view_new();
    custom_context_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(context_text_view));
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(context_text_view), GTK_WRAP_WORD_CHAR);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(context_scroll), context_text_view);
    gtk_widget_set_vexpand(context_scroll, TRUE);

    // Load the custom context
    if (custom_context == NULL) {
        custom_context = load_custom_context();
    }
    if (custom_context != NULL) {
        gtk_text_buffer_set_text(custom_context_buffer, custom_context, -1);
    }

    // Add widgets to the content area
    gtk_box_append(GTK_BOX(content_area), provider_label);
    gtk_box_append(GTK_BOX(content_area), provider_dropdown);

    // Add model name field
    gtk_box_append(GTK_BOX(content_area), model_label);
    gtk_box_append(GTK_BOX(content_area), model_name_entry);
    gtk_widget_set_margin_bottom(model_name_entry, 5);

    // Add translation language fields
    GtkWidget *translate_to_label = gtk_label_new("Translate To:");
    gtk_widget_set_halign(translate_to_label, GTK_ALIGN_START);
    translate_to_entry = gtk_entry_new();

    // Load the target language
    if (translate_to == NULL) {
        translate_to = load_translate_to();
    }

    if (translate_to != NULL && strlen(translate_to) > 0) {
        gtk_editable_set_text(GTK_EDITABLE(translate_to_entry), translate_to);
    } else {
        gtk_editable_set_text(GTK_EDITABLE(translate_to_entry), "English");
    }

    GtkWidget *translate_from_label = gtk_label_new("Translate From (leave blank for auto-detect):");
    gtk_widget_set_halign(translate_from_label, GTK_ALIGN_START);
    translate_from_entry = gtk_entry_new();

    // Load the source language
    if (translate_from == NULL) {
        translate_from = load_translate_from();
    }

    if (translate_from != NULL && strlen(translate_from) > 0) {
        gtk_editable_set_text(GTK_EDITABLE(translate_from_entry), translate_from);
    }

    // Add tooltips
    gtk_widget_set_tooltip_text(translate_to_entry,
        "The language to translate to (e.g., English, Spanish, Japanese)");
    gtk_widget_set_tooltip_text(translate_from_entry,
        "The language to translate from. Leave blank for auto-detection.");

    // Add to layout
    gtk_box_append(GTK_BOX(content_area), translate_to_label);
    gtk_box_append(GTK_BOX(content_area), translate_to_entry);
    gtk_widget_set_margin_bottom(translate_to_entry, 5);

    gtk_box_append(GTK_BOX(content_area), translate_from_label);
    gtk_box_append(GTK_BOX(content_area), translate_from_entry);
    gtk_widget_set_margin_bottom(translate_from_entry, 10);

    // Add API key field
    gtk_box_append(GTK_BOX(content_area), api_key_label);

    // Create a box for the API key entry and test button
    GtkWidget *api_key_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_append(GTK_BOX(api_key_box), api_key_entry);
    gtk_widget_set_hexpand(api_key_entry, TRUE);
    gtk_box_append(GTK_BOX(api_key_box), test_button);

    gtk_box_append(GTK_BOX(content_area), api_key_box);
    gtk_widget_set_margin_bottom(api_key_box, 10);

    gtk_box_append(GTK_BOX(content_area), help_box);
    gtk_box_append(GTK_BOX(content_area), context_scroll);

    // Create buttons
    GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_widget_set_halign(button_box, GTK_ALIGN_END);
    gtk_widget_set_margin_top(button_box, 10);

    GtkWidget *cancel_button = gtk_button_new_with_label("Cancel");
    GtkWidget *save_button = gtk_button_new_with_label("Save");

    gtk_box_append(GTK_BOX(button_box), cancel_button);
    gtk_box_append(GTK_BOX(button_box), save_button);
    gtk_box_append(GTK_BOX(content_area), button_box);

    // Connect signals
    g_signal_connect(provider_combo, "notify::selected", G_CALLBACK(on_provider_changed), NULL);
    g_signal_connect(test_button, "clicked", G_CALLBACK(on_test_api_key_clicked), NULL);
    g_signal_connect(save_button, "clicked", G_CALLBACK(on_ai_settings_save_clicked), NULL);
    g_signal_connect(cancel_button, "clicked", G_CALLBACK(gtk_window_destroy), ai_settings_dialog);

    // Show the dialog
    gtk_window_present(GTK_WINDOW(ai_settings_dialog));
}

// Function to create the AI translator UI components
GtkWidget* create_ai_translator_ui(GtkWidget *parent_window) {
    fprintf(stderr, "DEBUG: create_ai_translator_ui() called\n");
    fprintf(stderr, "DEBUG: parent_window set to %p\n", parent_window);

    // Create the main box
    GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);

    // Create the send button
    GtkWidget *send_to_ai_button = gtk_button_new_with_label("Send to AI");
    gtk_widget_set_margin_top(send_to_ai_button, 5);
    gtk_widget_set_margin_bottom(send_to_ai_button, 5);
    gtk_widget_set_halign(send_to_ai_button, GTK_ALIGN_CENTER);
    fprintf(stderr, "DEBUG: send_to_ai_button created: %p\n", send_to_ai_button);

    // Create the AI translation view
    GtkWidget *ai_scroll = gtk_scrolled_window_new();
    GtkWidget *ai_translation_view = gtk_text_view_new();
    GtkTextBuffer *ai_translation_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(ai_translation_view));
    fprintf(stderr, "DEBUG: ai_translation_view=%p, ai_translation_buffer=%p\n",
            ai_translation_view, ai_translation_buffer);

    // Set monospace font (JetBrains Nerd Font Mono)
    PangoFontDescription *font_desc = pango_font_description_from_string("JetBrains Nerd Font Mono 12");
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(ai_translation_view), TRUE);

    // Apply custom font using CSS
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_string(provider,
        "textview { font-family: 'JetBrains Nerd Font Mono'; font-size: 12pt; }");
    gtk_widget_add_css_class(ai_translation_view, "custom-font");
    gtk_style_context_add_provider_for_display(gdk_display_get_default(),
        GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);

    pango_font_description_free(font_desc);

    // Set other properties
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(ai_translation_view), GTK_WRAP_WORD_CHAR);
    gtk_widget_set_vexpand(ai_translation_view, TRUE);
    gtk_widget_set_hexpand(ai_translation_view, TRUE);
    gtk_text_view_set_editable(GTK_TEXT_VIEW(ai_translation_view), FALSE);

    // Add the text view to the scroll window
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(ai_scroll), ai_translation_view);

    // Add widgets to the main box
    gtk_box_append(GTK_BOX(main_box), send_to_ai_button);
    gtk_box_append(GTK_BOX(main_box), ai_scroll);

    // Store references as object data
    g_object_set_data(G_OBJECT(main_box), "ai_translation_buffer", ai_translation_buffer);
    g_object_set_data(G_OBJECT(main_box), "send_to_ai_button", send_to_ai_button);

    // Note: We don't connect the signal here - it will be connected in main.c
    // This avoids circular dependencies

    // Load API keys
    fprintf(stderr, "DEBUG: Loading API keys and context\n");
    if (openai_api_key == NULL) openai_api_key = load_api_key(OPENAI);
    if (gemini_api_key == NULL) gemini_api_key = load_api_key(GEMINI);
    if (custom_context == NULL) custom_context = load_custom_context();

    fprintf(stderr, "DEBUG: create_ai_translator_ui() completed, returning main_box=%p\n", main_box);
    return main_box;
}
