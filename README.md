# Hex2Text

A format converter and translator utility for Linux, useful for game text extraction and fan translation projects.

## Features

### Format Conversion
- Convert between multiple encodings:
  - Hex, ASCII, UTF-8, UTF-16LE, UTF-16BE, UTF-32LE, UTF-32BE
  - ISO-8859-1, ISO-8859-15, Shift-JIS, EUC-JP, KOI8-R
- Bidirectional conversion (top-to-bottom and bottom-to-top)
- Real-time character and byte counting
- Format swapping

### AI Translation
- Translate decoded text to other languages using OpenAI or Google Gemini
- Useful for game text extraction and fan translation projects
- Customizable translation context for game-specific terminology
- API key storage in `~/.hex2text/` directory

## Platform Support
- Linux (primary)
- May work on Windows with appropriate GTK4 setup, but not officially supported

## Requirements
- GTK 4
- libcurl
- json-c

## Building
```bash
mkdir -p build && cd build
cmake ..
make
```

## Usage
1. Enter text in either the top or bottom field
2. Select source and target formats from the dropdown menus
3. View the converted output in real-time
4. For translating game text or other content:
   - Click "Tools" → "AI Translator"
   - Configure API keys in "Tools" → "AI Settings"
   - Click "Send to AI" to translate the decoded text
