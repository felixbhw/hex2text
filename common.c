#include "common.h"

// Convert encoding type to string representation
const char* encoding_type_to_string(EncodingType type) {
    static const char* encoding_names[] = {
        "Hex", "ASCII", "UTF-8", "UTF-16LE", "UTF-16BE",
        "UTF-32LE", "UTF-32BE", "ISO-8859-1", "ISO-8859-15",
        "Shift-JIS", "EUC-JP", "KOI8-R"
    };
    
    if (type >= 0 && type < sizeof(encoding_names)/sizeof(encoding_names[0])) {
        return encoding_names[type];
    }
    
    return "Unknown";
}
