#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
// Evita conflitti tra funzioni Win32 e raylib
#define CloseWindow WinCloseWindow
#define ShowCursor WinShowCursor
#define Rectangle WinRectangle
#include <windows.h>
#include <commdlg.h>
#undef CloseWindow
#undef ShowCursor
#undef Rectangle
#undef DrawText
#undef DrawTextEx
#undef LoadImage
#undef PlaySound
#endif

#include "raylib.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

// Stati colore: 0 = Neutro, 1 = Rosso, 2 = Arancione, 3 = Giallo, 4 = Verde
struct SubTask {
    std::string text;
    int status = 0;
};

struct Task {
    std::string text;
    int status = 0;
    std::vector<SubTask> subtasks;
    std::vector<std::string> tags;
};

enum class EditMode { None, MainTask, SubTask, Search, Title };

struct DragState {
    bool pending = false;
    bool active = false;
    int srcTask = -1;
    int srcSub = -1;
    Vector2 startPos = { 0, 0 };
    Vector2 offset = { 0, 0 };
};

struct SubLayout {
    int subIdx = -1;
    Rectangle box{};
    Rectangle pill{};
    Rectangle delBtn{};
    std::vector<std::string> lines;
};

struct TagLayout {
    int tagIdx = -1;
    Rectangle pill{};
    Rectangle delBtn{};
};

struct ColLayout {
    int taskIdx = -1;
    Rectangle progressBar{};
    Rectangle mainBox{};
    Rectangle mainPill{};
    Rectangle mainDelBtn{};
    std::vector<std::string> mainLines;
    std::vector<SubLayout> subs;
    Rectangle addSubBtn{};
    std::vector<TagLayout> tags;
    float bottomY = 0.0f;
};

const std::string AUTO_SAVE_JSON = "tasks2_project.json";
const std::string LEGACY_SAVE_TXT = "tasks2_board.txt";

// ============================================================================
// UTILITY STRINGHE & GEOMETRIA
// ============================================================================

float Dist(Vector2 a, Vector2 b) {
    float dx = a.x - b.x;
    float dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

std::string ToLowerStr(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string Trim(const std::string& s) {
    size_t start = 0;
    while (start < s.size() && (s[start] == ' ' || s[start] == '\t')) start++;
    size_t end = s.size();
    while (end > start && (s[end - 1] == ' ' || s[end - 1] == '\t')) end--;
    return s.substr(start, end - start);
}

std::string SanitizeFileName(const std::string& name) {
    std::string out;
    const std::string invalid = "\\/:*?\"<>|";
    for (char c : name) {
        if (static_cast<unsigned char>(c) >= 32 && invalid.find(c) == std::string::npos) {
            out.push_back(c);
        }
    }
    out = Trim(out);
    return out.empty() ? "progetto_tasks2" : out;
}

std::string ExtractFileName(const std::string& path) {
    size_t pos = path.find_last_of("\\/");
    return (pos == std::string::npos) ? path : path.substr(pos + 1);
}

// ============================================================================
// FINESTRE DI DIALOGO NATIVE WINDOWS (Importa / Esporta JSON)
// Carica comdlg32.dll a runtime: non servono flag extra al compilatore!
// ============================================================================

std::string OpenJsonFileDialog(void* ownerWindow) {
#ifdef _WIN32
    HMODULE mod = LoadLibraryA("comdlg32.dll");
    if (!mod) return "";
    using FnOpenA = BOOL (WINAPI *)(LPOPENFILENAMEA);
    auto fnOpen = reinterpret_cast<FnOpenA>(GetProcAddress(mod, "GetOpenFileNameA"));
    if (!fnOpen) {
        FreeLibrary(mod);
        return "";
    }

    char fileBuf[MAX_PATH] = { 0 };
    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = static_cast<HWND>(ownerWindow);
    ofn.lpstrFilter = "File JSON (*.json)\0*.json\0Tutti i file (*.*)\0*.*\0";
    ofn.lpstrFile   = fileBuf;
    ofn.nMaxFile    = MAX_PATH;
    ofn.lpstrTitle  = "Importa progetto Tasks 2 (.json)";
    ofn.Flags       = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    ofn.lpstrDefExt = "json";

    std::string result;
    if (fnOpen(&ofn)) {
        result = fileBuf;
    }
    FreeLibrary(mod);
    return result;
#else
    (void)ownerWindow;
    return "progetto_tasks2.json";
#endif
}

std::string SaveJsonFileDialog(void* ownerWindow, const std::string& defaultBaseName) {
#ifdef _WIN32
    HMODULE mod = LoadLibraryA("comdlg32.dll");
    if (!mod) return "";
    using FnSaveA = BOOL (WINAPI *)(LPOPENFILENAMEA);
    auto fnSave = reinterpret_cast<FnSaveA>(GetProcAddress(mod, "GetSaveFileNameA"));
    if (!fnSave) {
        FreeLibrary(mod);
        return "";
    }

    char fileBuf[MAX_PATH] = { 0 };
    std::string suggested = SanitizeFileName(defaultBaseName) + ".json";
    std::snprintf(fileBuf, MAX_PATH, "%s", suggested.c_str());

    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = static_cast<HWND>(ownerWindow);
    ofn.lpstrFilter = "File JSON (*.json)\0*.json\0Tutti i file (*.*)\0*.*\0";
    ofn.lpstrFile   = fileBuf;
    ofn.nMaxFile    = MAX_PATH;
    ofn.lpstrTitle  = "Esporta progetto Tasks 2 (.json)";
    ofn.Flags       = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
    ofn.lpstrDefExt = "json";

    std::string result;
    if (fnSave(&ofn)) {
        result = fileBuf;
    }
    FreeLibrary(mod);
    return result;
#else
    (void)ownerWindow;
    return SanitizeFileName(defaultBaseName) + ".json";
#endif
}

// ============================================================================
// SERIALIZZATORE E PARSER JSON INTEGRATO (zero dipendenze esterne)
// ============================================================================

std::string JsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned int>(c));
                    out += buf;
                } else {
                    out.push_back(static_cast<char>(c));
                }
                break;
        }
    }
    return out;
}

struct JsonValue {
    enum class Type { Null, Bool, Number, String, Array, Object };
    Type type = Type::Null;
    bool boolVal = false;
    double numVal = 0.0;
    std::string strVal;
    std::vector<JsonValue> arrVal;
    std::vector<std::pair<std::string, JsonValue>> objVal;

    const JsonValue* Get(const std::string& key) const {
        if (type != Type::Object) return nullptr;
        for (const auto& kv : objVal) {
            if (kv.first == key) return &kv.second;
        }
        return nullptr;
    }
};

class JsonParser {
    const std::string& src;
    size_t pos = 0;

    void SkipWs() {
        while (pos < src.size() && (src[pos] == ' ' || src[pos] == '\t' || src[pos] == '\n' || src[pos] == '\r')) {
            pos++;
        }
    }

    bool ParseString(std::string& out) {
        if (pos >= src.size() || src[pos] != '"') return false;
        pos++; // salta '"'
        out.clear();
        while (pos < src.size()) {
            char c = src[pos++];
            if (c == '"') return true;
            if (c == '\\') {
                if (pos >= src.size()) return false;
                char e = src[pos++];
                switch (e) {
                    case '"':  out.push_back('"');  break;
                    case '\\': out.push_back('\\'); break;
                    case '/':  out.push_back('/');  break;
                    case 'b':  out.push_back('\b'); break;
                    case 'f':  out.push_back('\f'); break;
                    case 'n':  out.push_back('\n'); break;
                    case 'r':  out.push_back('\r'); break;
                    case 't':  out.push_back('\t'); break;
                    case 'u': {
                        if (pos + 4 > src.size()) return false;
                        unsigned int code = 0;
                        for (int k = 0; k < 4; ++k) {
                            char h = src[pos++];
                            code <<= 4;
                            if (h >= '0' && h <= '9') code |= (h - '0');
                            else if (h >= 'a' && h <= 'f') code |= (h - 'a' + 10);
                            else if (h >= 'A' && h <= 'F') code |= (h - 'A' + 10);
                            else return false;
                        }
                        if (code >= 32 && code <= 126) out.push_back(static_cast<char>(code));
                        else if (code == '\n') out.push_back('\n');
                        else out.push_back('?');
                        break;
                    }
                    default: return false;
                }
            } else {
                out.push_back(c);
            }
        }
        return false;
    }

    bool ParseNumber(JsonValue& val) {
        size_t start = pos;
        if (pos < src.size() && src[pos] == '-') pos++;
        if (pos >= src.size() || !std::isdigit(static_cast<unsigned char>(src[pos]))) return false;
        while (pos < src.size() && std::isdigit(static_cast<unsigned char>(src[pos]))) pos++;
        if (pos < src.size() && src[pos] == '.') {
            pos++;
            if (pos >= src.size() || !std::isdigit(static_cast<unsigned char>(src[pos]))) return false;
            while (pos < src.size() && std::isdigit(static_cast<unsigned char>(src[pos]))) pos++;
        }
        if (pos < src.size() && (src[pos] == 'e' || src[pos] == 'E')) {
            pos++;
            if (pos < src.size() && (src[pos] == '+' || src[pos] == '-')) pos++;
            if (pos >= src.size() || !std::isdigit(static_cast<unsigned char>(src[pos]))) return false;
            while (pos < src.size() && std::isdigit(static_cast<unsigned char>(src[pos]))) pos++;
        }
        try {
            val.type = JsonValue::Type::Number;
            val.numVal = std::stod(src.substr(start, pos - start));
            return true;
        } catch (...) {
            return false;
        }
    }

    bool ParseArray(JsonValue& val) {
        pos++; // salta '['
        val.type = JsonValue::Type::Array;
        SkipWs();
        if (pos < src.size() && src[pos] == ']') {
            pos++;
            return true;
        }
        while (pos < src.size()) {
            JsonValue elem;
            if (!ParseValue(elem)) return false;
            val.arrVal.push_back(std::move(elem));
            SkipWs();
            if (pos < src.size() && src[pos] == ',') {
                pos++;
                SkipWs();
            } else if (pos < src.size() && src[pos] == ']') {
                pos++;
                return true;
            } else {
                return false;
            }
        }
        return false;
    }

    bool ParseObject(JsonValue& val) {
        pos++; // salta '{'
        val.type = JsonValue::Type::Object;
        SkipWs();
        if (pos < src.size() && src[pos] == '}') {
            pos++;
            return true;
        }
        while (pos < src.size()) {
            SkipWs();
            std::string key;
            if (!ParseString(key)) return false;
            SkipWs();
            if (pos >= src.size() || src[pos] != ':') return false;
            pos++;
            JsonValue child;
            if (!ParseValue(child)) return false;
            val.objVal.emplace_back(std::move(key), std::move(child));
            SkipWs();
            if (pos < src.size() && src[pos] == ',') {
                pos++;
                SkipWs();
            } else if (pos < src.size() && src[pos] == '}') {
                pos++;
                return true;
            } else {
                return false;
            }
        }
        return false;
    }

public:
    explicit JsonParser(const std::string& input) : src(input) {
        // Salta eventuale BOM UTF-8
        if (src.size() >= 3 &&
            static_cast<unsigned char>(src[0]) == 0xEF &&
            static_cast<unsigned char>(src[1]) == 0xBB &&
            static_cast<unsigned char>(src[2]) == 0xBF) {
            pos = 3;
        }
    }

    bool ParseValue(JsonValue& val) {
        SkipWs();
        if (pos >= src.size()) return false;
        char c = src[pos];
        if (c == '{') return ParseObject(val);
        if (c == '[') return ParseArray(val);
        if (c == '"') {
            val.type = JsonValue::Type::String;
            return ParseString(val.strVal);
        }
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) {
            return ParseNumber(val);
        }
        if (src.compare(pos, 4, "true") == 0) {
            pos += 4;
            val.type = JsonValue::Type::Bool;
            val.boolVal = true;
            return true;
        }
        if (src.compare(pos, 5, "false") == 0) {
            pos += 5;
            val.type = JsonValue::Type::Bool;
            val.boolVal = false;
            return true;
        }
        if (src.compare(pos, 4, "null") == 0) {
            pos += 4;
            val.type = JsonValue::Type::Null;
            return true;
        }
        return false;
    }
};

bool SaveBoardJson(const std::string& path, const std::string& title, const std::vector<Task>& tasks) {
    std::ofstream out(path);
    if (!out.is_open()) return false;

    out << "{\n";
    out << "  \"title\": \"" << JsonEscape(title) << "\",\n";
    out << "  \"version\": \"0.2\",\n";
    out << "  \"tasks\": [\n";

    for (size_t i = 0; i < tasks.size(); ++i) {
        const Task& t = tasks[i];
        out << "    {\n";
        out << "      \"text\": \"" << JsonEscape(t.text) << "\",\n";
        out << "      \"status\": " << t.status << ",\n";

        out << "      \"tags\": [";
        for (size_t k = 0; k < t.tags.size(); ++k) {
            if (k > 0) out << ", ";
            out << "\"" << JsonEscape(t.tags[k]) << "\"";
        }
        out << "],\n";

        out << "      \"subtasks\": [\n";
        for (size_t s = 0; s < t.subtasks.size(); ++s) {
            const SubTask& st = t.subtasks[s];
            out << "        { \"text\": \"" << JsonEscape(st.text) << "\", \"status\": " << st.status << " }";
            if (s + 1 < t.subtasks.size()) out << ",";
            out << "\n";
        }
        out << "      ]\n";
        out << "    }";
        if (i + 1 < tasks.size()) out << ",";
        out << "\n";
    }

    out << "  ]\n";
    out << "}\n";
    return out.good();
}

bool LoadBoardJson(const std::string& path, std::string& title, std::vector<Task>& tasks) {
    std::ifstream in(path);
    if (!in.is_open()) return false;

    std::stringstream ss;
    ss << in.rdbuf();
    std::string content = ss.str();

    JsonParser parser(content);
    JsonValue root;
    if (!parser.ParseValue(root) || root.type != JsonValue::Type::Object) {
        return false;
    }

    const JsonValue* tasksNode = root.Get("tasks");
    if (!tasksNode || tasksNode->type != JsonValue::Type::Array) {
        return false;
    }

    std::string loadedTitle = "New Task";
    if (const JsonValue* titleNode = root.Get("title"); titleNode && titleNode->type == JsonValue::Type::String) {
        loadedTitle = titleNode->strVal;
    }

    std::vector<Task> loadedTasks;
    for (const auto& tVal : tasksNode->arrVal) {
        if (tVal.type != JsonValue::Type::Object) continue;
        Task t;
        if (const JsonValue* txt = tVal.Get("text"); txt && txt->type == JsonValue::Type::String) {
            t.text = txt->strVal;
        }
        if (const JsonValue* st = tVal.Get("status"); st && st->type == JsonValue::Type::Number) {
            t.status = std::clamp(static_cast<int>(std::round(st->numVal)), 0, 4);
        }
        if (const JsonValue* tags = tVal.Get("tags"); tags && tags->type == JsonValue::Type::Array) {
            for (const auto& tg : tags->arrVal) {
                if (tg.type == JsonValue::Type::String && !tg.strVal.empty()) {
                    t.tags.push_back(tg.strVal);
                }
            }
        }
        if (const JsonValue* subs = tVal.Get("subtasks"); subs && subs->type == JsonValue::Type::Array) {
            for (const auto& sVal : subs->arrVal) {
                if (sVal.type != JsonValue::Type::Object) continue;
                SubTask stItem;
                if (const JsonValue* sTxt = sVal.Get("text"); sTxt && sTxt->type == JsonValue::Type::String) {
                    stItem.text = sTxt->strVal;
                }
                if (const JsonValue* sSt = sVal.Get("status"); sSt && sSt->type == JsonValue::Type::Number) {
                    stItem.status = std::clamp(static_cast<int>(std::round(sSt->numVal)), 0, 4);
                }
                t.subtasks.push_back(stItem);
            }
        }
        loadedTasks.push_back(std::move(t));
    }

    title = std::move(loadedTitle);
    tasks = std::move(loadedTasks);
    return true;
}

// Migrazione automatica dal vecchio file .txt se presente
std::string UnescapeLegacy(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            char n = s[++i];
            if (n == 'n') out.push_back('\n');
            else if (n == 'p') out.push_back('|');
            else out.push_back(n);
        } else {
            out.push_back(s[i]);
        }
    }
    return out;
}

bool LoadLegacyTxt(const std::string& path, std::string& title, std::vector<Task>& tasks) {
    std::ifstream in(path);
    if (!in.is_open()) return false;
    tasks.clear();
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("TITLE|", 0) == 0) {
            title = UnescapeLegacy(line.substr(6));
        } else if (line.rfind("T|", 0) == 0) {
            size_t p = line.find('|', 2);
            if (p == std::string::npos) continue;
            Task t;
            try { t.status = std::clamp(std::stoi(line.substr(2, p - 2)), 0, 4); } catch (...) { t.status = 0; }
            t.text = UnescapeLegacy(line.substr(p + 1));
            tasks.push_back(t);
        } else if (line.rfind("H|", 0) == 0 && !tasks.empty()) {
            std::string tag = UnescapeLegacy(line.substr(2));
            if (!tag.empty()) tasks.back().tags.push_back(tag);
        } else if (line.rfind("S|", 0) == 0 && !tasks.empty()) {
            size_t p = line.find('|', 2);
            if (p == std::string::npos) continue;
            SubTask s;
            try { s.status = std::clamp(std::stoi(line.substr(2, p - 2)), 0, 4); } catch (...) { s.status = 0; }
            s.text = UnescapeLegacy(line.substr(p + 1));
            tasks.back().subtasks.push_back(s);
        }
    }
    return !tasks.empty();
}

// ============================================================================
// COLORI, LAYOUT E TESTO MULTILINEA
// ============================================================================

Color GetStatusColor(int status) {
    switch (status) {
        case 1:  return { 230, 102, 102, 255 }; // Rosso
        case 2:  return { 230, 138, 62, 255 };  // Arancione
        case 3:  return { 235, 224, 108, 255 }; // Giallo
        case 4:  return { 88, 224, 132, 255 };  // Verde
        default: return { 222, 227, 236, 255 }; // Neutro
    }
}

Color GetStatusHoverColor(int status) {
    Color c = GetStatusColor(status);
    return {
        static_cast<unsigned char>(std::min(255, c.r + 14)),
        static_cast<unsigned char>(std::min(255, c.g + 14)),
        static_cast<unsigned char>(std::min(255, c.b + 14)),
        255
    };
}

Color GetColumnBarColor(const Task& t) {
    float r = 0, g = 0, b = 0;
    int coloredCount = 0;

    auto accum = [&](int st) {
        if (st > 0) {
            Color c = GetStatusColor(st);
            r += c.r; g += c.g; b += c.b;
            coloredCount++;
        }
    };

    accum(t.status);
    for (const auto& s : t.subtasks) accum(s.status);

    if (coloredCount == 0) return { 130, 140, 160, 255 };
    return {
        static_cast<unsigned char>(r / coloredCount),
        static_cast<unsigned char>(g / coloredCount),
        static_cast<unsigned char>(b / coloredCount),
        255
    };
}

void DrawRoundedBoxWithBorder(Rectangle rec, float roundness, int segments, Color fill, Color border, float thick = 2.0f) {
    Rectangle outer = {
        rec.x - thick, rec.y - thick,
        rec.width + thick * 2.0f, rec.height + thick * 2.0f
    };
    DrawRectangleRounded(outer, roundness, segments, border);
    DrawRectangleRounded(rec, roundness, segments, fill);
}

std::vector<std::string> WrapText(const std::string& text, int fontSize, float maxWidth) {
    std::vector<std::string> lines;
    if (text.empty()) {
        lines.push_back("");
        return lines;
    }

    size_t pos = 0;
    while (pos <= text.size()) {
        size_t nl = text.find('\n', pos);
        std::string para = (nl == std::string::npos) ? text.substr(pos) : text.substr(pos, nl - pos);

        if (para.empty()) {
            lines.push_back("");
        } else {
            std::string current;
            size_t i = 0;
            while (i < para.size()) {
                size_t nextSpace = para.find(' ', i);
                std::string token = (nextSpace == std::string::npos)
                    ? para.substr(i)
                    : para.substr(i, nextSpace - i + 1);

                std::string candidate = current + token;
                if (MeasureText(candidate.c_str(), fontSize) <= static_cast<int>(maxWidth)) {
                    current = candidate;
                } else {
                    if (!current.empty()) {
                        lines.push_back(current);
                        current.clear();
                    }
                    if (MeasureText(token.c_str(), fontSize) > static_cast<int>(maxWidth)) {
                        for (char ch : token) {
                            std::string cTest = current + ch;
                            if (!current.empty() && MeasureText(cTest.c_str(), fontSize) > static_cast<int>(maxWidth)) {
                                lines.push_back(current);
                                current.clear();
                            }
                            current.push_back(ch);
                        }
                    } else {
                        current = token;
                    }
                }
                if (nextSpace == std::string::npos) break;
                i = nextSpace + 1;
            }
            if (!current.empty() || lines.empty()) lines.push_back(current);
        }

        if (nl == std::string::npos) break;
        pos = nl + 1;
    }

    if (lines.empty()) lines.push_back("");
    return lines;
}

bool ExtractHashtags(Task& t, bool finalize) {
    bool extracted = false;
    std::string& txt = t.text;
    std::string result;
    size_t i = 0;

    while (i < txt.size()) {
        bool validStart = (txt[i] == '#') && (i == 0 || txt[i - 1] == ' ' || txt[i - 1] == '\n');
        if (validStart) {
            size_t j = i + 1;
            while (j < txt.size() && (std::isalnum(static_cast<unsigned char>(txt[j])) || txt[j] == '_')) j++;
            bool terminated = (j < txt.size() && (txt[j] == ' ' || txt[j] == '\n')) || (finalize && j == txt.size());
            if (j > i + 1 && terminated) {
                std::string tag = txt.substr(i + 1, j - (i + 1));
                std::string tagLow = ToLowerStr(tag);
                bool exists = false;
                for (const auto& existing : t.tags) {
                    if (ToLowerStr(existing) == tagLow) { exists = true; break; }
                }
                if (!exists) t.tags.push_back(tag);
                extracted = true;
                if (j < txt.size() && txt[j] == ' ') j++;
                i = j;
                continue;
            }
        }
        result.push_back(txt[i]);
        i++;
    }

    if (extracted) {
        txt = finalize ? Trim(result) : result;
    }
    return extracted;
}

// ============================================================================
// MAIN
// ============================================================================

int main() {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT);
    InitWindow(1180, 680, "Tasks 2 (C++ raylib)");
    SetWindowMinSize(720, 460);
    SetExitKey(KEY_NULL);
    SetTargetFPS(60);

    const Color COL_BG          = { 53, 62, 82, 255 };
    const Color COL_TOPBAR      = { 30, 35, 47, 255 };
    const Color COL_BOTTOMBAR   = { 36, 42, 56, 255 };
    const Color COL_INPUT_BG    = { 44, 51, 68, 255 };
    const Color COL_OUTLINE     = { 22, 26, 35, 255 };
    const Color COL_FOCUS       = { 96, 165, 250, 255 };
    const Color COL_TEXT_DARK   = { 26, 30, 40, 255 };
    const Color COL_PLACEHOLDER = { 105, 114, 132, 255 };
    const Color COL_TEXT_LIGHT  = { 232, 237, 245, 255 };
    const Color COL_MUTED       = { 150, 160, 180, 255 };
    const Color COL_WATERMARK   = { 145, 158, 185, 120 };
    const Color COL_TAG_BG      = { 40, 47, 63, 255 };
    const Color COL_DANGER      = { 220, 72, 72, 255 };
    const Color COL_SUCCESS     = { 54, 166, 98, 255 };

    std::string boardTitle = "New Task";
    std::vector<Task> tasks;

    if (!LoadBoardJson(AUTO_SAVE_JSON, boardTitle, tasks)) {
        if (LoadLegacyTxt(LEGACY_SAVE_TXT, boardTitle, tasks)) {
            SaveBoardJson(AUTO_SAVE_JSON, boardTitle, tasks);
        }
    }

    if (tasks.empty()) {
        Task demo;
        demo.text = "Main Task di esempio";
        demo.status = 1;
        demo.tags.push_back("demo");
        demo.subtasks.push_back({ "Clicca la pillola destra (o tasto destro) per cambiare colore", 2 });
        demo.subtasks.push_back({ "Usa Esporta JSON / Importa JSON nella barra in alto", 3 });
        demo.subtasks.push_back({ "Trascina una subtask su un'altra colonna!", 4 });
        tasks.push_back(demo);
        SaveBoardJson(AUTO_SAVE_JSON, boardTitle, tasks);
    }

    EditMode editMode = EditMode::None;
    int editTaskIdx = -1;
    int editSubIdx = -1;
    std::string searchText;

    float scrollX = 0.0f;
    float scrollY = 0.0f;
    bool panning = false;
    Vector2 panStartMouse = { 0, 0 };
    float panStartScrollX = 0.0f;
    float panStartScrollY = 0.0f;
    bool draggingScrollbar = false;
    float scrollbarDragOffset = 0.0f;

    DragState drag;

    float blinkTimer = 0.0f;
    float backspaceTimer = 0.0f;
    bool backspaceHeld = false;

    std::string toastText;
    Color toastColor = COL_SUCCESS;
    float toastTimer = 0.0f;

    auto showToast = [&](const std::string& msg, bool isError = false) {
        toastText = msg;
        toastColor = isError ? COL_DANGER : COL_SUCCESS;
        toastTimer = 2.8f;
    };

    auto autoSave = [&]() {
        SaveBoardJson(AUTO_SAVE_JSON, boardTitle, tasks);
    };

    auto finishCurrentEdit = [&]() {
        if (editMode == EditMode::MainTask && editTaskIdx >= 0 && editTaskIdx < static_cast<int>(tasks.size())) {
            ExtractHashtags(tasks[editTaskIdx], true);
            autoSave();
        } else if (editMode == EditMode::SubTask || editMode == EditMode::Title) {
            autoSave();
        }
        editMode = EditMode::None;
        editTaskIdx = -1;
        editSubIdx = -1;
    };

    auto importFromPath = [&](const std::string& path) {
        if (path.empty()) return;
        std::string newTitle;
        std::vector<Task> newTasks;
        if (LoadBoardJson(path, newTitle, newTasks)) {
            finishCurrentEdit();
            boardTitle = std::move(newTitle);
            tasks = std::move(newTasks);
            searchText.clear();
            scrollX = 0.0f;
            scrollY = 0.0f;
            autoSave();
            showToast("Importato: " + ExtractFileName(path), false);
        } else {
            showToast("Errore: file JSON non valido!", true);
        }
    };

    while (!WindowShouldClose()) {
        float dt = std::min(GetFrameTime(), 0.1f);
        blinkTimer += dt;
        if (toastTimer > 0.0f) toastTimer -= dt;

        // Supporto drag & drop diretto di un file .json sulla finestra
        if (IsFileDropped()) {
            FilePathList dropped = LoadDroppedFiles();
            if (dropped.count > 0 && dropped.paths[0] != nullptr) {
                importFromPath(dropped.paths[0]);
            }
            UnloadDroppedFiles(dropped);
        }

        int winW = GetScreenWidth();
        int winH = GetScreenHeight();
        Vector2 mouse = GetMousePosition();
        bool leftPressed  = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
        bool leftDown     = IsMouseButtonDown(MOUSE_BUTTON_LEFT);
        bool leftReleased = IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
        bool rightPressed = IsMouseButtonPressed(MOUSE_BUTTON_RIGHT);

        const float topBarH = 44.0f;
        const float bottomBarH = 30.0f;
        Rectangle boardArea = { 0.0f, topBarH, static_cast<float>(winW), winH - topBarH - bottomBarH };

        // --- LAYOUT TOP BAR (dinamico) ---
        Rectangle exportBtn   = { static_cast<float>(winW) - 122.0f, 7.0f, 110.0f, 30.0f };
        Rectangle importBtn   = { exportBtn.x - 116.0f,              7.0f, 110.0f, 30.0f };
        Rectangle newBoardBtn = { importBtn.x - 74.0f,               7.0f,  68.0f, 30.0f };

        Rectangle titleRect   = { 12.0f, 7.0f, 152.0f, 30.0f };
        float searchMaxW      = std::max(100.0f, newBoardBtn.x - 246.0f);
        float searchW         = std::clamp(searchMaxW, 100.0f, 220.0f);
        Rectangle searchRect  = { 206.0f, 7.0f, searchW, 30.0f };
        Rectangle clearSearchBtn = { searchRect.x + searchRect.width + 6.0f, 9.0f, 26.0f, 26.0f };

        // --- INPUT TASTIERA ---
        if (editMode != EditMode::None) {
            std::string* targetStr = nullptr;
            bool allowMultiline = false;
            size_t maxLen = 4096;

            if (editMode == EditMode::MainTask && editTaskIdx >= 0 && editTaskIdx < static_cast<int>(tasks.size())) {
                targetStr = &tasks[editTaskIdx].text;
                allowMultiline = true;
            } else if (editMode == EditMode::SubTask && editTaskIdx >= 0 && editTaskIdx < static_cast<int>(tasks.size()) &&
                       editSubIdx >= 0 && editSubIdx < static_cast<int>(tasks[editTaskIdx].subtasks.size())) {
                targetStr = &tasks[editTaskIdx].subtasks[editSubIdx].text;
                allowMultiline = true;
            } else if (editMode == EditMode::Search) {
                targetStr = &searchText;
                maxLen = 40;
            } else if (editMode == EditMode::Title) {
                targetStr = &boardTitle;
                maxLen = 28;
            }

            if (targetStr != nullptr) {
                bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
                bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);

                if (ctrl && IsKeyPressed(KEY_V)) {
                    const char* clip = GetClipboardText();
                    if (clip != nullptr) {
                        for (size_t i = 0; clip[i] != '\0' && targetStr->size() < maxLen; ++i) {
                            unsigned char ch = static_cast<unsigned char>(clip[i]);
                            if ((ch >= 32 && ch <= 126) || (allowMultiline && ch == '\n')) {
                                targetStr->push_back(static_cast<char>(ch));
                            }
                        }
                        blinkTimer = 0.0f;
                    }
                }

                int key = GetCharPressed();
                while (key > 0) {
                    if (key >= 32 && key <= 126 && targetStr->size() < maxLen) {
                        targetStr->push_back(static_cast<char>(key));
                        blinkTimer = 0.0f;
                        if (key == ' ' && editMode == EditMode::MainTask) {
                            ExtractHashtags(tasks[editTaskIdx], false);
                        }
                    }
                    key = GetCharPressed();
                }

                if (IsKeyPressed(KEY_BACKSPACE) && !targetStr->empty()) {
                    targetStr->pop_back();
                    backspaceHeld = true;
                    backspaceTimer = 0.38f;
                    blinkTimer = 0.0f;
                } else if (IsKeyDown(KEY_BACKSPACE) && backspaceHeld) {
                    backspaceTimer -= dt;
                    if (backspaceTimer <= 0.0f && !targetStr->empty()) {
                        targetStr->pop_back();
                        backspaceTimer = 0.04f;
                        blinkTimer = 0.0f;
                    }
                } else {
                    backspaceHeld = false;
                }

                if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) {
                    if (allowMultiline && shift) {
                        targetStr->push_back('\n');
                        blinkTimer = 0.0f;
                    } else {
                        finishCurrentEdit();
                    }
                }

                if (IsKeyPressed(KEY_ESCAPE)) {
                    finishCurrentEdit();
                }
            }
        }

        // --- FILTRAGGIO PER HASHTAG / TESTO ---
        std::vector<int> visibleCols;
        std::string query = ToLowerStr(Trim(searchText));
        if (!query.empty() && query[0] == '#') query = query.substr(1);

        for (int i = 0; i < static_cast<int>(tasks.size()); ++i) {
            if (query.empty()) {
                visibleCols.push_back(i);
                continue;
            }
            bool match = false;
            for (const auto& tag : tasks[i].tags) {
                if (ToLowerStr(tag).find(query) != std::string::npos) { match = true; break; }
            }
            if (!match && ToLowerStr(tasks[i].text).find(query) != std::string::npos) match = true;
            if (!match) {
                for (const auto& s : tasks[i].subtasks) {
                    if (ToLowerStr(s.text).find(query) != std::string::npos) { match = true; break; }
                }
            }
            if (match) visibleCols.push_back(i);
        }

        // --- CALCOLO LAYOUT COLONNE ---
        const float colW = 248.0f;
        const float pillW = 20.0f;
        const float pillGap = 5.0f;
        const float boxW = colW - pillW - pillGap;
        const float subIndent = 14.0f;
        const float subBoxW = boxW - subIndent;
        const float colGap = 28.0f;
        const float padLeft = 24.0f;
        const float padTop = 22.0f;
        const int fontSize = 16;
        const float lineH = 20.0f;
        const float addCardSize = 84.0f;

        float totalContentW = padLeft + visibleCols.size() * (colW + colGap) + addCardSize + 60.0f;
        float maxScrollX = std::max(0.0f, totalContentW - boardArea.width);

        // Pan con tasto centrale
        if (IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE) && CheckCollisionPointRec(mouse, boardArea)) {
            panning = true;
            panStartMouse = mouse;
            panStartScrollX = scrollX;
            panStartScrollY = scrollY;
        }
        if (panning) {
            if (IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)) {
                scrollX = panStartScrollX - (mouse.x - panStartMouse.x);
                scrollY = panStartScrollY - (mouse.y - panStartMouse.y);
            } else {
                panning = false;
            }
        }

        // Scroll con rotellina
        if (CheckCollisionPointRec(mouse, boardArea) && !panning) {
            float wheel = GetMouseWheelMove();
            if (wheel != 0.0f) {
                bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
                if (shift) scrollY -= wheel * 42.0f;
                else if (maxScrollX > 0.0f) scrollX -= wheel * 56.0f;
                else scrollY -= wheel * 42.0f;
            }
        }

        // Scrollbar orizzontale
        Rectangle hTrack = { 16.0f, static_cast<float>(winH) - bottomBarH - 12.0f, static_cast<float>(winW) - 32.0f, 8.0f };
        float thumbW = (maxScrollX > 0.0f)
            ? std::max(46.0f, (boardArea.width / totalContentW) * hTrack.width)
            : hTrack.width;
        float thumbX = (maxScrollX > 0.0f)
            ? hTrack.x + (scrollX / maxScrollX) * (hTrack.width - thumbW)
            : hTrack.x;
        Rectangle hThumb = { thumbX, hTrack.y - 2.0f, thumbW, hTrack.height + 4.0f };

        if (maxScrollX > 0.0f) {
            if (leftPressed && CheckCollisionPointRec(mouse, hThumb)) {
                draggingScrollbar = true;
                scrollbarDragOffset = mouse.x - hThumb.x;
            }
            if (draggingScrollbar) {
                if (leftDown) {
                    float newThumbX = std::clamp(mouse.x - scrollbarDragOffset, hTrack.x, hTrack.x + hTrack.width - thumbW);
                    scrollX = ((newThumbX - hTrack.x) / std::max(1.0f, hTrack.width - thumbW)) * maxScrollX;
                } else {
                    draggingScrollbar = false;
                }
            }
        } else {
            draggingScrollbar = false;
        }
        scrollX = std::clamp(scrollX, 0.0f, maxScrollX);

        // Costruzione layout colonne
        std::vector<ColLayout> cols;
        cols.reserve(visibleCols.size());
        float maxContentH = 200.0f;

        for (size_t c = 0; c < visibleCols.size(); ++c) {
            int idx = visibleCols[c];
            const Task& t = tasks[idx];
            ColLayout cl;
            cl.taskIdx = idx;

            float colX = padLeft + c * (colW + colGap) - scrollX;
            float curY = topBarH + padTop - scrollY;

            cl.progressBar = { colX, curY, boxW, 7.0f };
            curY += 13.0f;

            cl.mainLines = WrapText(t.text, fontSize, boxW - 32.0f);
            float mainH = std::max(36.0f, cl.mainLines.size() * lineH + 14.0f);
            cl.mainBox = { colX, curY, boxW, mainH };
            cl.mainPill = { colX + boxW + pillGap, curY, pillW, mainH };
            cl.mainDelBtn = { colX + boxW - 22.0f, curY + 5.0f, 18.0f, 18.0f };
            curY += mainH + 5.0f;

            for (int s = 0; s < static_cast<int>(t.subtasks.size()); ++s) {
                SubLayout sl;
                sl.subIdx = s;
                sl.lines = WrapText(t.subtasks[s].text, fontSize, subBoxW - 30.0f);
                float subH = std::max(32.0f, sl.lines.size() * lineH + 12.0f);
                sl.box = { colX + subIndent, curY, subBoxW, subH };
                sl.pill = { colX + boxW + pillGap, curY, pillW, subH };
                sl.delBtn = { sl.box.x + sl.box.width - 20.0f, curY + 5.0f, 16.0f, 16.0f };
                cl.subs.push_back(sl);
                curY += subH + 5.0f;
            }

            cl.addSubBtn = { colX + subIndent, curY + 2.0f, subBoxW, 28.0f };
            curY += 36.0f;

            float tagX = colX;
            float tagY = curY + 2.0f;
            for (int tg = 0; tg < static_cast<int>(t.tags.size()); ++tg) {
                std::string label = "#" + t.tags[tg];
                int tw = MeasureText(label.c_str(), 13);
                float pillWidth = static_cast<float>(tw + 26);
                if (tagX > colX && tagX + pillWidth > colX + colW) {
                    tagX = colX;
                    tagY += 24.0f;
                }
                TagLayout tl;
                tl.tagIdx = tg;
                tl.pill = { tagX, tagY, pillWidth, 20.0f };
                tl.delBtn = { tagX + 3.0f, tagY + 2.0f, 16.0f, 16.0f };
                cl.tags.push_back(tl);
                tagX += pillWidth + 6.0f;
            }
            if (!t.tags.empty()) curY = tagY + 26.0f;

            cl.bottomY = curY;
            maxContentH = std::max(maxContentH, (curY + scrollY) - topBarH + 40.0f);
            cols.push_back(cl);
        }

        float maxScrollY = std::max(0.0f, maxContentH - boardArea.height);
        scrollY = std::clamp(scrollY, 0.0f, maxScrollY);

        float addColX = padLeft + visibleCols.size() * (colW + colGap) - scrollX + 10.0f;
        float addColY = topBarH + padTop + 13.0f - scrollY;
        Rectangle bigAddBtn = { addColX, addColY, addCardSize, addCardSize };

        // --- DRAG & DROP SUBTASK ---
        if (drag.pending) {
            if (leftDown) {
                if (Dist(mouse, drag.startPos) > 8.0f) {
                    drag.pending = false;
                    drag.active = true;
                    finishCurrentEdit();
                }
            } else if (leftReleased) {
                finishCurrentEdit();
                editMode = EditMode::SubTask;
                editTaskIdx = drag.srcTask;
                editSubIdx = drag.srcSub;
                blinkTimer = 0.0f;
                drag.pending = false;
            }
        } else if (drag.active && leftReleased) {
            if (drag.srcTask >= 0 && drag.srcTask < static_cast<int>(tasks.size()) &&
                drag.srcSub >= 0 && drag.srcSub < static_cast<int>(tasks[drag.srcTask].subtasks.size())) {
                int dstCol = -1;
                for (size_t c = 0; c < cols.size(); ++c) {
                    float colLeft = cols[c].mainBox.x - colGap * 0.5f;
                    float colRight = cols[c].mainBox.x + colW + colGap * 0.5f;
                    if (mouse.x >= colLeft && mouse.x <= colRight) {
                        dstCol = static_cast<int>(c);
                        break;
                    }
                }
                if (dstCol >= 0) {
                    int dstTaskIdx = cols[dstCol].taskIdx;
                    int insertIdx = static_cast<int>(cols[dstCol].subs.size());
                    for (size_t s = 0; s < cols[dstCol].subs.size(); ++s) {
                        float midY = cols[dstCol].subs[s].box.y + cols[dstCol].subs[s].box.height * 0.5f;
                        if (mouse.y < midY) {
                            insertIdx = static_cast<int>(s);
                            break;
                        }
                    }

                    SubTask moved = tasks[drag.srcTask].subtasks[drag.srcSub];
                    tasks[drag.srcTask].subtasks.erase(tasks[drag.srcTask].subtasks.begin() + drag.srcSub);
                    if (dstTaskIdx == drag.srcTask && insertIdx > drag.srcSub) insertIdx--;

                    auto& dstSubs = tasks[dstTaskIdx].subtasks;
                    insertIdx = std::clamp(insertIdx, 0, static_cast<int>(dstSubs.size()));
                    dstSubs.insert(dstSubs.begin() + insertIdx, moved);
                    autoSave();
                }
            }
            drag.active = false;
            drag.srcTask = -1;
            drag.srcSub = -1;
        }

        // --- GESTIONE CLICK SINISTRO ---
        if (leftPressed && !draggingScrollbar) {
            bool handled = false;

            if (mouse.y < topBarH) {
                if (CheckCollisionPointRec(mouse, titleRect)) {
                    finishCurrentEdit();
                    editMode = EditMode::Title;
                    blinkTimer = 0.0f;
                    handled = true;
                } else if (CheckCollisionPointRec(mouse, searchRect)) {
                    finishCurrentEdit();
                    editMode = EditMode::Search;
                    blinkTimer = 0.0f;
                    handled = true;
                } else if (!searchText.empty() && CheckCollisionPointRec(mouse, clearSearchBtn)) {
                    searchText.clear();
                    scrollX = 0.0f;
                    handled = true;
                } else if (CheckCollisionPointRec(mouse, exportBtn)) {
                    finishCurrentEdit();
                    std::string outPath = SaveJsonFileDialog(GetWindowHandle(), boardTitle);
                    if (!outPath.empty()) {
                        if (SaveBoardJson(outPath, boardTitle, tasks)) {
                            showToast("Esportato: " + ExtractFileName(outPath), false);
                        } else {
                            showToast("Errore durante il salvataggio JSON!", true);
                        }
                    }
                    handled = true;
                } else if (CheckCollisionPointRec(mouse, importBtn)) {
                    finishCurrentEdit();
                    std::string inPath = OpenJsonFileDialog(GetWindowHandle());
                    if (!inPath.empty()) {
                        importFromPath(inPath);
                    }
                    handled = true;
                } else if (CheckCollisionPointRec(mouse, newBoardBtn)) {
                    finishCurrentEdit();
                    tasks.clear();
                    boardTitle = "New Task";
                    searchText.clear();
                    scrollX = 0.0f;
                    scrollY = 0.0f;
                    autoSave();
                    showToast("Progetto svuotato", false);
                    handled = true;
                } else {
                    finishCurrentEdit();
                    handled = true;
                }
            }

            if (!handled && CheckCollisionPointRec(mouse, boardArea)) {
                if (CheckCollisionPointRec(mouse, bigAddBtn)) {
                    finishCurrentEdit();
                    searchText.clear();
                    Task nt;
                    nt.text = "";
                    nt.status = 0;
                    tasks.push_back(nt);
                    editMode = EditMode::MainTask;
                    editTaskIdx = static_cast<int>(tasks.size()) - 1;
                    blinkTimer = 0.0f;
                    autoSave();
                    float newTotalW = padLeft + tasks.size() * (colW + colGap) + addCardSize + 60.0f;
                    scrollX = std::max(0.0f, newTotalW - boardArea.width);
                    handled = true;
                }

                if (!handled) {
                    int delTaskIdx = -1;
                    int delSubParent = -1, delSubIdx = -1;
                    int delTagParent = -1, delTagIdx = -1;

                    for (const auto& cl : cols) {
                        int idx = cl.taskIdx;

                        if (CheckCollisionPointRec(mouse, cl.mainDelBtn)) {
                            delTaskIdx = idx; handled = true; break;
                        }
                        if (CheckCollisionPointRec(mouse, cl.mainPill)) {
                            tasks[idx].status = (tasks[idx].status + 1) % 5;
                            autoSave();
                            handled = true; break;
                        }
                        if (CheckCollisionPointRec(mouse, cl.mainBox)) {
                            finishCurrentEdit();
                            editMode = EditMode::MainTask;
                            editTaskIdx = idx;
                            blinkTimer = 0.0f;
                            handled = true; break;
                        }

                        for (const auto& sl : cl.subs) {
                            if (CheckCollisionPointRec(mouse, sl.delBtn)) {
                                delSubParent = idx; delSubIdx = sl.subIdx;
                                handled = true; break;
                            }
                            if (CheckCollisionPointRec(mouse, sl.pill)) {
                                tasks[idx].subtasks[sl.subIdx].status = (tasks[idx].subtasks[sl.subIdx].status + 1) % 5;
                                autoSave();
                                handled = true; break;
                            }
                            if (CheckCollisionPointRec(mouse, sl.box)) {
                                drag.pending = true;
                                drag.active = false;
                                drag.srcTask = idx;
                                drag.srcSub = sl.subIdx;
                                drag.startPos = mouse;
                                drag.offset = { mouse.x - sl.box.x, mouse.y - sl.box.y };
                                handled = true; break;
                            }
                        }
                        if (handled) break;

                        if (CheckCollisionPointRec(mouse, cl.addSubBtn)) {
                            finishCurrentEdit();
                            tasks[idx].subtasks.push_back({ "", 0 });
                            editMode = EditMode::SubTask;
                            editTaskIdx = idx;
                            editSubIdx = static_cast<int>(tasks[idx].subtasks.size()) - 1;
                            blinkTimer = 0.0f;
                            autoSave();
                            handled = true; break;
                        }

                        for (const auto& tl : cl.tags) {
                            if (CheckCollisionPointRec(mouse, tl.delBtn)) {
                                delTagParent = idx; delTagIdx = tl.tagIdx;
                                handled = true; break;
                            }
                            if (CheckCollisionPointRec(mouse, tl.pill)) {
                                finishCurrentEdit();
                                searchText = tasks[idx].tags[tl.tagIdx];
                                scrollX = 0.0f;
                                handled = true; break;
                            }
                        }
                        if (handled) break;
                    }

                    if (delTaskIdx >= 0) {
                        finishCurrentEdit();
                        tasks.erase(tasks.begin() + delTaskIdx);
                        autoSave();
                    } else if (delSubParent >= 0 && delSubIdx >= 0) {
                        finishCurrentEdit();
                        auto& subs = tasks[delSubParent].subtasks;
                        subs.erase(subs.begin() + delSubIdx);
                        autoSave();
                    } else if (delTagParent >= 0 && delTagIdx >= 0) {
                        auto& tgs = tasks[delTagParent].tags;
                        tgs.erase(tgs.begin() + delTagIdx);
                        autoSave();
                    }
                }
            }

            if (!handled) finishCurrentEdit();
        }

        // --- CLICK DESTRO -> CAMBIA COLORE BOX ---
        if (rightPressed && CheckCollisionPointRec(mouse, boardArea)) {
            for (const auto& cl : cols) {
                int idx = cl.taskIdx;
                if (CheckCollisionPointRec(mouse, cl.mainBox) || CheckCollisionPointRec(mouse, cl.mainPill)) {
                    tasks[idx].status = (tasks[idx].status + 1) % 5;
                    autoSave();
                    break;
                }
                bool changedSub = false;
                for (const auto& sl : cl.subs) {
                    if (CheckCollisionPointRec(mouse, sl.box) || CheckCollisionPointRec(mouse, sl.pill)) {
                        tasks[idx].subtasks[sl.subIdx].status = (tasks[idx].subtasks[sl.subIdx].status + 1) % 5;
                        autoSave();
                        changedSub = true;
                        break;
                    }
                }
                if (changedSub) break;
            }
        }

        // --- RENDERING ---
        BeginDrawing();
        ClearBackground(COL_BG);

        // Titolo watermark
        const char* wm = boardTitle.empty() ? "New Task" : boardTitle.c_str();
        DrawText(wm, static_cast<int>(winW * 0.20f), static_cast<int>(winH * 0.54f), 38, COL_WATERMARK);

        BeginScissorMode(0, static_cast<int>(topBarH), winW, static_cast<int>(boardArea.height));

        for (const auto& cl : cols) {
            int idx = cl.taskIdx;
            const Task& t = tasks[idx];

            DrawRectangleRounded(cl.progressBar, 0.8f, 6, GetColumnBarColor(t));

            if (drag.active && mouse.x >= cl.mainBox.x - colGap * 0.5f && mouse.x <= cl.mainBox.x + colW + colGap * 0.5f) {
                Rectangle colHighlight = { cl.mainBox.x - 6.0f, cl.progressBar.y - 6.0f, colW + 12.0f, (cl.bottomY - cl.progressBar.y) + 10.0f };
                DrawRectangleRounded(colHighlight, 0.04f, 6, { 96, 165, 250, 32 });
            }

            // MainTask box
            bool mainEditing = (editMode == EditMode::MainTask && editTaskIdx == idx);
            bool mainHover = CheckCollisionPointRec(mouse, cl.mainBox);
            Color mainFill = mainHover ? GetStatusHoverColor(t.status) : GetStatusColor(t.status);
            DrawRoundedBoxWithBorder(cl.mainBox, 0.22f, 8, mainFill, mainEditing ? COL_FOCUS : COL_OUTLINE, mainEditing ? 2.5f : 2.0f);

            if (t.text.empty() && !mainEditing) {
                DrawText("Enter task...", static_cast<int>(cl.mainBox.x + 10), static_cast<int>(cl.mainBox.y + 10), fontSize, COL_PLACEHOLDER);
            } else {
                for (size_t li = 0; li < cl.mainLines.size(); ++li) {
                    DrawText(cl.mainLines[li].c_str(),
                             static_cast<int>(cl.mainBox.x + 10),
                             static_cast<int>(cl.mainBox.y + 8 + li * lineH),
                             fontSize, COL_TEXT_DARK);
                }
                if (mainEditing && (static_cast<int>(blinkTimer * 2.0f) % 2 == 0)) {
                    size_t lastLi = cl.mainLines.size() - 1;
                    int caretX = static_cast<int>(cl.mainBox.x + 11) + MeasureText(cl.mainLines[lastLi].c_str(), fontSize);
                    int caretY = static_cast<int>(cl.mainBox.y + 7 + lastLi * lineH);
                    DrawRectangle(caretX, caretY, 2, 18, COL_TEXT_DARK);
                }
            }

            if (mainHover || CheckCollisionPointRec(mouse, cl.mainDelBtn)) {
                bool delHov = CheckCollisionPointRec(mouse, cl.mainDelBtn);
                DrawRectangleRounded(cl.mainDelBtn, 0.35f, 6, delHov ? COL_DANGER : Color{ 0, 0, 0, 70 });
                DrawText("x", static_cast<int>(cl.mainDelBtn.x + 5), static_cast<int>(cl.mainDelBtn.y + 1), 14, WHITE);
            }

            bool mainPillHov = CheckCollisionPointRec(mouse, cl.mainPill);
            Color mpFill = mainPillHov ? WHITE : GetStatusColor(t.status);
            DrawRoundedBoxWithBorder(cl.mainPill, 0.9f, 8, mpFill, mainPillHov ? COL_FOCUS : COL_OUTLINE, 2.0f);
            DrawCircle(static_cast<int>(cl.mainPill.x + cl.mainPill.width * 0.5f),
                       static_cast<int>(cl.mainPill.y + cl.mainPill.height * 0.5f),
                       3.0f, COL_OUTLINE);

            // SubTasks
            for (const auto& sl : cl.subs) {
                bool isBeingDragged = (drag.active && drag.srcTask == idx && drag.srcSub == sl.subIdx);
                const SubTask& st = t.subtasks[sl.subIdx];

                bool subEditing = (editMode == EditMode::SubTask && editTaskIdx == idx && editSubIdx == sl.subIdx);
                bool subHover = !drag.active && CheckCollisionPointRec(mouse, sl.box);

                Color subFill = subHover ? GetStatusHoverColor(st.status) : GetStatusColor(st.status);
                if (isBeingDragged) subFill.a = 95;
                DrawRoundedBoxWithBorder(sl.box, 0.24f, 8, subFill, subEditing ? COL_FOCUS : COL_OUTLINE, subEditing ? 2.5f : 2.0f);

                if (st.text.empty() && !subEditing) {
                    DrawText("Enter subTask...", static_cast<int>(sl.box.x + 10), static_cast<int>(sl.box.y + 8), fontSize, COL_PLACEHOLDER);
                } else {
                    Color txtCol = isBeingDragged ? Color{ 26, 30, 40, 110 } : COL_TEXT_DARK;
                    for (size_t li = 0; li < sl.lines.size(); ++li) {
                        DrawText(sl.lines[li].c_str(),
                                 static_cast<int>(sl.box.x + 10),
                                 static_cast<int>(sl.box.y + 7 + li * lineH),
                                 fontSize, txtCol);
                    }
                    if (subEditing && (static_cast<int>(blinkTimer * 2.0f) % 2 == 0)) {
                        size_t lastLi = sl.lines.size() - 1;
                        int caretX = static_cast<int>(sl.box.x + 11) + MeasureText(sl.lines[lastLi].c_str(), fontSize);
                        int caretY = static_cast<int>(sl.box.y + 6 + lastLi * lineH);
                        DrawRectangle(caretX, caretY, 2, 18, COL_TEXT_DARK);
                    }
                }

                if (subHover || CheckCollisionPointRec(mouse, sl.delBtn)) {
                    bool sDelHov = CheckCollisionPointRec(mouse, sl.delBtn);
                    DrawRectangleRounded(sl.delBtn, 0.35f, 6, sDelHov ? COL_DANGER : Color{ 0, 0, 0, 70 });
                    DrawText("x", static_cast<int>(sl.delBtn.x + 4), static_cast<int>(sl.delBtn.y), 13, WHITE);
                }

                bool spHov = !drag.active && CheckCollisionPointRec(mouse, sl.pill);
                Color spFill = spHov ? WHITE : GetStatusColor(st.status);
                if (isBeingDragged) spFill.a = 95;
                DrawRoundedBoxWithBorder(sl.pill, 0.9f, 8, spFill, spHov ? COL_FOCUS : COL_OUTLINE, 2.0f);
                DrawCircle(static_cast<int>(sl.pill.x + sl.pill.width * 0.5f),
                           static_cast<int>(sl.pill.y + sl.pill.height * 0.5f),
                           2.8f, COL_OUTLINE);
            }

            // Pulsante "+ Subtask"
            bool addSubHov = CheckCollisionPointRec(mouse, cl.addSubBtn);
            DrawRoundedBoxWithBorder(
                cl.addSubBtn, 0.35f, 6,
                addSubHov ? Color{ 74, 86, 114, 255 } : Color{ 44, 52, 70, 255 },
                addSubHov ? COL_FOCUS : Color{ 82, 94, 120, 255 },
                1.2f
            );
            const char* subLbl = "+ Subtask";
            DrawText(subLbl,
                     static_cast<int>(cl.addSubBtn.x + (cl.addSubBtn.width - MeasureText(subLbl, 14)) / 2),
                     static_cast<int>(cl.addSubBtn.y + 7),
                     14, addSubHov ? WHITE : COL_MUTED);

            // Hashtag pills
            for (const auto& tl : cl.tags) {
                bool tagHov = CheckCollisionPointRec(mouse, tl.pill);
                DrawRectangleRounded(tl.pill, 0.6f, 6, tagHov ? Color{ 66, 77, 102, 255 } : COL_TAG_BG);

                bool xHov = CheckCollisionPointRec(mouse, tl.delBtn);
                DrawCircle(static_cast<int>(tl.delBtn.x + 8), static_cast<int>(tl.delBtn.y + 8), 6.0f, xHov ? COL_DANGER : Color{ 80, 90, 112, 255 });
                DrawText("x", static_cast<int>(tl.delBtn.x + 5), static_cast<int>(tl.delBtn.y + 2), 11, WHITE);

                std::string label = "#" + t.tags[tl.tagIdx];
                DrawText(label.c_str(), static_cast<int>(tl.pill.x + 21), static_cast<int>(tl.pill.y + 4), 13, COL_MUTED);
            }
        }

        // Pulsante grande "+" MainTask
        bool bigAddHov = CheckCollisionPointRec(mouse, bigAddBtn);
        DrawRoundedBoxWithBorder(
            bigAddBtn, 0.14f, 8,
            bigAddHov ? Color{ 72, 84, 112, 255 } : Color{ 53, 62, 82, 255 },
            bigAddHov ? WHITE : Color{ 195, 205, 222, 255 },
            2.2f
        );
        float cx = bigAddBtn.x + bigAddBtn.width * 0.5f;
        float cy = bigAddBtn.y + bigAddBtn.height * 0.5f;
        DrawRectangleRounded({ cx - 18.0f, cy - 3.5f, 36.0f, 7.0f }, 0.4f, 4, bigAddHov ? WHITE : Color{ 215, 223, 236, 255 });
        DrawRectangleRounded({ cx - 3.5f, cy - 18.0f, 7.0f, 36.0f }, 0.4f, 4, bigAddHov ? WHITE : Color{ 215, 223, 236, 255 });

        // Ghost Drag & Drop
        if (drag.active && drag.srcTask >= 0 && drag.srcTask < static_cast<int>(tasks.size()) &&
            drag.srcSub >= 0 && drag.srcSub < static_cast<int>(tasks[drag.srcTask].subtasks.size())) {
            const SubTask& st = tasks[drag.srcTask].subtasks[drag.srcSub];
            auto lines = WrapText(st.text, fontSize, subBoxW - 30.0f);
            float ghH = std::max(32.0f, lines.size() * lineH + 12.0f);
            Rectangle ghost = { mouse.x - drag.offset.x, mouse.y - drag.offset.y, subBoxW, ghH };

            Color gFill = GetStatusColor(st.status);
            gFill.a = 230;
            DrawRoundedBoxWithBorder(ghost, 0.24f, 8, gFill, COL_FOCUS, 2.5f);
            if (st.text.empty()) {
                DrawText("Enter subTask...", static_cast<int>(ghost.x + 10), static_cast<int>(ghost.y + 8), fontSize, COL_PLACEHOLDER);
            } else {
                for (size_t li = 0; li < lines.size(); ++li) {
                    DrawText(lines[li].c_str(), static_cast<int>(ghost.x + 10), static_cast<int>(ghost.y + 7 + li * lineH), fontSize, COL_TEXT_DARK);
                }
            }
        }

        EndScissorMode();

        // --- TOP BAR ---
        DrawRectangle(0, 0, winW, static_cast<int>(topBarH), COL_TOPBAR);
        DrawLine(0, static_cast<int>(topBarH), winW, static_cast<int>(topBarH), COL_OUTLINE);

        // Titolo progetto
        bool titleEditing = (editMode == EditMode::Title);
        DrawRoundedBoxWithBorder(titleRect, 0.25f, 6, titleEditing ? COL_INPUT_BG : COL_TOPBAR, titleEditing ? COL_FOCUS : Color{ 50, 58, 76, 255 }, 1.2f);
        BeginScissorMode(static_cast<int>(titleRect.x + 4), static_cast<int>(titleRect.y), static_cast<int>(titleRect.width - 8), static_cast<int>(titleRect.height));
        DrawText(boardTitle.empty() ? "New Task" : boardTitle.c_str(), static_cast<int>(titleRect.x + 8), static_cast<int>(titleRect.y + 7), 16, COL_TEXT_LIGHT);
        if (titleEditing && (static_cast<int>(blinkTimer * 2.0f) % 2 == 0)) {
            int tw = MeasureText(boardTitle.c_str(), 16);
            DrawRectangle(static_cast<int>(titleRect.x + 9 + tw), static_cast<int>(titleRect.y + 6), 2, 18, COL_FOCUS);
        }
        EndScissorMode();

        // Contatore MainTask
        DrawText(TextFormat("[%d]", static_cast<int>(tasks.size())), 172, 14, 15, COL_MUTED);

        // Barra di ricerca
        bool searchEditing = (editMode == EditMode::Search);
        DrawRoundedBoxWithBorder(searchRect, 0.25f, 6, COL_INPUT_BG, searchEditing ? COL_FOCUS : Color{ 60, 70, 92, 255 }, 1.4f);
        BeginScissorMode(static_cast<int>(searchRect.x + 4), static_cast<int>(searchRect.y), static_cast<int>(searchRect.width - 8), static_cast<int>(searchRect.height));
        if (searchText.empty() && !searchEditing) {
            DrawText("Search (#tag)...", static_cast<int>(searchRect.x + 8), static_cast<int>(searchRect.y + 7), 15, COL_MUTED);
        } else {
            DrawText(searchText.c_str(), static_cast<int>(searchRect.x + 8), static_cast<int>(searchRect.y + 7), 15, COL_TEXT_LIGHT);
        }
        if (searchEditing && (static_cast<int>(blinkTimer * 2.0f) % 2 == 0)) {
            int sw = MeasureText(searchText.c_str(), 15);
            DrawRectangle(static_cast<int>(searchRect.x + 9 + sw), static_cast<int>(searchRect.y + 6), 2, 18, COL_FOCUS);
        }
        EndScissorMode();

        if (!searchText.empty()) {
            bool clrHov = CheckCollisionPointRec(mouse, clearSearchBtn);
            DrawRectangleRounded(clearSearchBtn, 0.3f, 6, clrHov ? COL_DANGER : COL_INPUT_BG);
            DrawText("X", static_cast<int>(clearSearchBtn.x + 8), static_cast<int>(clearSearchBtn.y + 6), 14, WHITE);
        }

        // Pulsante Svuota
        bool newHov = CheckCollisionPointRec(mouse, newBoardBtn);
        DrawRoundedBoxWithBorder(newBoardBtn, 0.25f, 6, newHov ? Color{ 88, 42, 46, 255 } : COL_INPUT_BG, newHov ? COL_DANGER : Color{ 62, 72, 94, 255 }, 1.2f);
        const char* svuotaLbl = "Svuota";
        DrawText(svuotaLbl, static_cast<int>(newBoardBtn.x + (newBoardBtn.width - MeasureText(svuotaLbl, 14)) / 2), static_cast<int>(newBoardBtn.y + 8), 14, newHov ? WHITE : COL_MUTED);

        // Pulsante Importa JSON
        bool impHov = CheckCollisionPointRec(mouse, importBtn);
        DrawRoundedBoxWithBorder(importBtn, 0.25f, 6, impHov ? Color{ 62, 96, 178, 255 } : COL_INPUT_BG, impHov ? COL_FOCUS : Color{ 70, 84, 114, 255 }, 1.3f);
        const char* impLbl = "Importa JSON";
        DrawText(impLbl, static_cast<int>(importBtn.x + (importBtn.width - MeasureText(impLbl, 14)) / 2), static_cast<int>(importBtn.y + 8), 14, WHITE);

        // Pulsante Esporta JSON
        bool expHov = CheckCollisionPointRec(mouse, exportBtn);
        DrawRoundedBoxWithBorder(exportBtn, 0.25f, 6, expHov ? Color{ 50, 148, 88, 255 } : Color{ 38, 110, 68, 255 }, expHov ? Color{ 98, 224, 142, 255 } : Color{ 62, 160, 98, 255 }, 1.3f);
        const char* expLbl = "Esporta JSON";
        DrawText(expLbl, static_cast<int>(exportBtn.x + (exportBtn.width - MeasureText(expLbl, 14)) / 2), static_cast<int>(exportBtn.y + 8), 14, WHITE);

        // --- NOTIFICA TOAST ---
        if (toastTimer > 0.0f && !toastText.empty()) {
            int tw = MeasureText(toastText.c_str(), 15);
            Rectangle toastRec = { static_cast<float>(winW - tw - 44), topBarH + 12.0f, static_cast<float>(tw + 28), 34.0f };
            unsigned char alpha = static_cast<unsigned char>(std::clamp(toastTimer / 0.5f, 0.0f, 1.0f) * 240.0f);
            Color bg = toastColor; bg.a = alpha;
            Color bd = WHITE; bd.a = static_cast<unsigned char>(alpha * 0.6f);
            Color tx = WHITE; tx.a = alpha;
            DrawRoundedBoxWithBorder(toastRec, 0.3f, 6, bg, bd, 1.2f);
            DrawText(toastText.c_str(), static_cast<int>(toastRec.x + 14), static_cast<int>(toastRec.y + 9), 15, tx);
        }

        // --- SCROLLBAR ORIZZONTALE ---
        if (maxScrollX > 0.0f) {
            DrawRectangleRounded(hTrack, 0.8f, 6, { 34, 40, 54, 255 });
            DrawRectangleRounded(hThumb, 0.8f, 6, draggingScrollbar ? COL_FOCUS : Color{ 108, 120, 148, 255 });
        }

        // --- BOTTOM BAR ---
        DrawRectangle(0, winH - static_cast<int>(bottomBarH), winW, static_cast<int>(bottomBarH), COL_BOTTOMBAR);
        DrawLine(0, winH - static_cast<int>(bottomBarH), winW, winH - static_cast<int>(bottomBarH), COL_OUTLINE);
        DrawText("Click sx: scrivi  |  Pillola/Click dx: cambia colore  |  Trascina subtask: sposta  |  Trascina file .json qui per importare",
                 16, winH - 22, 13, COL_MUTED);

        EndDrawing();
    }

    autoSave();
    CloseWindow();
    return 0;
}