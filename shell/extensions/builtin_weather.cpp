// shell/extensions/builtin_weather.cpp
// Built-in shell extension: weather display widget.
// Reads from wttr.in API (via curl) or local sensor data.

#include "extension_api.h"

#include <imgui.h>

#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

namespace {

struct WeatherData {
    std::string location = "Unknown";
    std::string condition = "N/A";
    float temp_c = 0.0f;
    float temp_f = 0.0f;
    float feels_like_c = 0.0f;
    int humidity_pct = 0;
    float wind_kmh = 0.0f;
    std::string wind_dir;
    int visibility_km = 0;
    int uv_index = 0;
    std::string sunrise;
    std::string sunset;
    bool valid = false;
    std::string raw_text;
    std::string error;
};

struct WeatherState {
    SlExtensionContext* ctx = nullptr;

    WeatherData current;
    std::mutex data_mu;

    float refresh_interval_s = 600.0f; // 10 minutes
    float time_since_refresh = 9999.0f; // Force immediate fetch
    bool fetching = false;

    std::string location_query; // Empty = auto-detect
    bool use_fahrenheit = false;
};

WeatherState g_weather;

// Execute a command and capture stdout
std::string exec_command(const char* cmd) {
    std::array<char, 4096> buffer;
    std::string result;

    FILE* pipe = ::popen(cmd, "r");
    if (!pipe) return "";

    while (::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        result += buffer.data();
    }

    ::pclose(pipe);
    return result;
}

WeatherData parse_wttr_response(const std::string& raw) {
    WeatherData data;
    data.raw_text = raw;

    if (raw.empty() || raw.find("Unknown location") != std::string::npos) {
        data.error = "Could not fetch weather data";
        return data;
    }

    // Parse the wttr.in JSON format response
    // We use the ?format= parameter for structured output:
    // %l: location, %C: condition, %t: temp, %f: feels like, %h: humidity
    // %w: wind, %v: visibility, %u: UV, %S: sunrise, %s: sunset
    // The response is tab-separated values

    // Split by tab
    std::vector<std::string> fields;
    std::string current;
    for (char c : raw) {
        if (c == '\t') {
            fields.push_back(current);
            current.clear();
        } else if (c != '\n' && c != '\r') {
            current += c;
        }
    }
    if (!current.empty()) fields.push_back(current);

    if (fields.size() >= 10) {
        data.location = fields[0];
        data.condition = fields[1];

        // Temperature: might be "+20°C" or "+68°F"
        try {
            std::string temp_str = fields[2];
            // Remove + and degree signs
            std::string digits;
            bool neg = false;
            for (char c : temp_str) {
                if (c == '-') neg = true;
                if ((c >= '0' && c <= '9') || c == '.') digits += c;
            }
            if (!digits.empty()) {
                float t = std::stof(digits);
                if (neg) t = -t;
                data.temp_c = t;
                data.temp_f = t * 9.0f / 5.0f + 32.0f;
            }
        } catch (...) {}

        // Feels like
        try {
            std::string fl_str = fields[3];
            std::string digits;
            bool neg = false;
            for (char c : fl_str) {
                if (c == '-') neg = true;
                if ((c >= '0' && c <= '9') || c == '.') digits += c;
            }
            if (!digits.empty()) {
                float t = std::stof(digits);
                if (neg) t = -t;
                data.feels_like_c = t;
            }
        } catch (...) {}

        // Humidity
        try {
            std::string digits;
            for (char c : fields[4]) {
                if (c >= '0' && c <= '9') digits += c;
            }
            if (!digits.empty()) data.humidity_pct = std::stoi(digits);
        } catch (...) {}

        // Wind
        data.wind_dir = "";
        try {
            std::string wind_str = fields[5];
            // Format might be "↑15km/h" or "NW 15km/h"
            std::string speed_digits;
            for (char c : wind_str) {
                if (c >= '0' && c <= '9') speed_digits += c;
                else if (!speed_digits.empty()) break;
            }
            if (!speed_digits.empty()) data.wind_kmh = std::stof(speed_digits);

            // Extract direction characters at beginning
            for (size_t i = 0; i < wind_str.size(); ++i) {
                char c = wind_str[i];
                if (c >= '0' && c <= '9') break;
                if (c == 'N' || c == 'S' || c == 'E' || c == 'W') data.wind_dir += c;
            }
        } catch (...) {}

        // Visibility
        try {
            std::string digits;
            for (char c : fields[6]) {
                if (c >= '0' && c <= '9') digits += c;
            }
            if (!digits.empty()) data.visibility_km = std::stoi(digits);
        } catch (...) {}

        // UV Index
        try {
            std::string digits;
            for (char c : fields[7]) {
                if (c >= '0' && c <= '9') digits += c;
            }
            if (!digits.empty()) data.uv_index = std::stoi(digits);
        } catch (...) {}

        data.sunrise = fields.size() > 8 ? fields[8] : "";
        data.sunset = fields.size() > 9 ? fields[9] : "";

        data.valid = true;
    } else {
        // Fallback: just display the raw text
        data.raw_text = raw;
        data.error = "Unexpected response format";
    }

    return data;
}

void fetch_weather() {
    g_weather.fetching = true;

    std::string loc = g_weather.location_query;
    std::string url = "wttr.in/";
    if (!loc.empty()) {
        url += loc;
    }
    // Request tab-separated format for easy parsing
    url += "?format=%l\\t%C\\t%t\\t%f\\t%h\\t%w\\t%v\\t%u\\t%S\\t%s";

    std::string cmd = "curl -sf --max-time 10 '" + url + "' 2>/dev/null";
    std::string response = exec_command(cmd.c_str());

    WeatherData data = parse_wttr_response(response);

    {
        std::lock_guard lock(g_weather.data_mu);
        g_weather.current = data;
    }

    g_weather.fetching = false;
}

ImVec4 temp_color(float temp_c) {
    if (temp_c <= 0.0f) return ImVec4(0.4f, 0.6f, 1.0f, 1.0f);
    if (temp_c <= 15.0f) return ImVec4(0.5f, 0.8f, 1.0f, 1.0f);
    if (temp_c <= 25.0f) return ImVec4(0.0f, 0.9f, 0.6f, 1.0f);
    if (temp_c <= 35.0f) return ImVec4(1.0f, 0.8f, 0.2f, 1.0f);
    return ImVec4(1.0f, 0.3f, 0.2f, 1.0f);
}

} // anonymous namespace

extern "C" {

SlExtensionInfo sl_extension_info() {
    SlExtensionInfo info{};
    info.api_version = SL_EXTENSION_API_VERSION;
    std::strncpy(info.name, "weather", SL_EXT_NAME_MAX - 1);
    std::strncpy(info.version, "1.0.0", SL_EXT_VERSION_MAX - 1);
    std::strncpy(info.author, "StrayLight OS", SL_EXT_AUTHOR_MAX - 1);
    std::strncpy(info.description,
                 "Weather display from wttr.in or local sensor",
                 SL_EXT_DESCRIPTION_MAX - 1);
    return info;
}

int sl_extension_init(SlExtensionContext* ctx) {
    g_weather = WeatherState{};
    g_weather.ctx = ctx;
    g_weather.time_since_refresh = 9999.0f; // Force immediate fetch

    if (ctx && ctx->log_info) {
        ctx->log_info("Weather extension initialized");
    }
    return 0;
}

void sl_extension_render(float dt) {
    g_weather.time_since_refresh += dt;

    if (g_weather.time_since_refresh >= g_weather.refresh_interval_s && !g_weather.fetching) {
        g_weather.time_since_refresh = 0.0f;
        std::thread(fetch_weather).detach();
    }

    ImGui::SetNextWindowSize(ImVec2(260.0f, 220.0f), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Weather", nullptr, ImGuiWindowFlags_NoCollapse)) {
        std::lock_guard lock(g_weather.data_mu);
        const auto& w = g_weather.current;

        if (g_weather.fetching && !w.valid) {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Fetching weather data...");
        } else if (!w.valid && !w.error.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Error: %s", w.error.c_str());
            if (!w.raw_text.empty()) {
                ImGui::TextWrapped("%s", w.raw_text.c_str());
            }
        } else if (w.valid) {
            // Location
            ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "%s", w.location.c_str());

            // Large temperature display
            float display_temp = g_weather.use_fahrenheit ? w.temp_f : w.temp_c;
            const char* unit = g_weather.use_fahrenheit ? "F" : "C";
            ImVec4 tcolor = temp_color(w.temp_c);

            char temp_buf[32];
            std::snprintf(temp_buf, sizeof(temp_buf), "%.0f%s", display_temp, unit);
            ImGui::PushFont(nullptr); // Default font
            ImGui::TextColored(tcolor, "%s", temp_buf);
            ImGui::PopFont();

            // Condition
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%s", w.condition.c_str());

            ImGui::Separator();

            // Detail rows
            float feels_display = g_weather.use_fahrenheit ?
                (w.feels_like_c * 9.0f / 5.0f + 32.0f) : w.feels_like_c;
            ImGui::Text("Feels like: %.0f%s", feels_display, unit);
            ImGui::Text("Humidity:   %d%%", w.humidity_pct);

            if (w.wind_kmh > 0.0f) {
                ImGui::Text("Wind:       %.0f km/h %s", w.wind_kmh, w.wind_dir.c_str());
            }

            if (w.visibility_km > 0) {
                ImGui::Text("Visibility: %d km", w.visibility_km);
            }

            ImGui::Text("UV Index:   %d", w.uv_index);

            if (!w.sunrise.empty() && !w.sunset.empty()) {
                ImGui::Text("Sunrise:    %s", w.sunrise.c_str());
                ImGui::Text("Sunset:     %s", w.sunset.c_str());
            }
        } else {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "No data");
        }

        // Fetching indicator
        if (g_weather.fetching) {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "(updating...)");
        }

        // Settings
        if (ImGui::CollapsingHeader("Settings")) {
            ImGui::Checkbox("Fahrenheit", &g_weather.use_fahrenheit);
            ImGui::SliderFloat("Refresh (min)", &g_weather.refresh_interval_s,
                               60.0f, 3600.0f, "%.0f s");

            static char loc_buf[128]{};
            ImGui::InputText("Location", loc_buf, sizeof(loc_buf));
            if (ImGui::Button("Set Location")) {
                g_weather.location_query = loc_buf;
                g_weather.time_since_refresh = 9999.0f; // Force refresh
            }
        }
    }
    ImGui::End();
}

void sl_extension_shutdown() {
    if (g_weather.ctx && g_weather.ctx->log_info) {
        g_weather.ctx->log_info("Weather extension shut down");
    }
    g_weather = WeatherState{};
}

} // extern "C"
