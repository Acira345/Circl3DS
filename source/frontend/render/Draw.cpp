#include "Draw.hpp"
#include <cmath>
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <unordered_map>

namespace {
    C2D_TextBuf s_staticTextBuf = nullptr;
    size_t s_staticGlyphCount = 0;
    std::unordered_map<std::string, C2D_Text> s_staticTextMap;
    std::unordered_map<std::string, C2D_Text> s_frameTextMap;
    C2D_TextBuf s_lastFrameBuf = nullptr;
}

void resetDrawFrameTextCache() {
    s_frameTextMap.clear();
}

void clearDrawStaticTextCache() {
    s_frameTextMap.clear();
    s_staticTextMap.clear();
    if (s_staticTextBuf) {
        C2D_TextBufClear(s_staticTextBuf);
    }
    s_staticGlyphCount = 0;
}

void shutdownDrawTextCache() {
    s_frameTextMap.clear();
    s_staticTextMap.clear();
    if (s_staticTextBuf) {
        C2D_TextBufDelete(s_staticTextBuf);
        s_staticTextBuf = nullptr;
    }
    s_staticGlyphCount = 0;
}

void drawCachedTextParse(C2D_Text* outText, C2D_TextBuf buf, const char* str) {
    if (!outText) return;
    const char* safeStr = str ? str : "";
    size_t len = std::strlen(safeStr);

    // 1. Caché estática entre fotogramas para etiquetas repetidas y números de combo cortos
    if (len > 0 && len <= 24) {
        auto it = s_staticTextMap.find(safeStr);
        if (it != s_staticTextMap.end()) {
            *outText = it->second;
            return;
        }
        if (!s_staticTextBuf) {
            s_staticTextBuf = C2D_TextBufNew(2048);
        }
        if (s_staticTextBuf && s_staticTextMap.size() < 160 && (s_staticGlyphCount + len + 4) < 1900) {
            C2D_Text t;
            C2D_TextParse(&t, s_staticTextBuf, safeStr);
            C2D_TextOptimize(&t);
            s_staticTextMap.emplace(safeStr, t);
            s_staticGlyphCount += len;
            *outText = t;
            return;
        }
    }

    // 2. Caché por fotograma (evita re-parsear el mismo HUD dos veces en modo 3D estereoscópico)
    if (buf) {
        if (s_lastFrameBuf != buf) {
            s_frameTextMap.clear();
            s_lastFrameBuf = buf;
        }
        if (len > 0 && len <= 96 && s_frameTextMap.size() < 96) {
            auto fit = s_frameTextMap.find(safeStr);
            if (fit != s_frameTextMap.end()) {
                *outText = fit->second;
                return;
            }
            C2D_Text t;
            C2D_TextParse(&t, buf, safeStr);
            C2D_TextOptimize(&t);
            s_frameTextMap.emplace(safeStr, t);
            *outText = t;
            return;
        }
    }

    C2D_TextParse(outText, buf, safeStr);
    C2D_TextOptimize(outText);
}

void drawRingZ(float x, float y, float z, float radius, float thickness, u32 color, int segments) {
    if (radius < 0.5f) return;
    int segs = std::max(12, std::min(segments, 36));
    const float step = (2.0f * static_cast<float>(M_PI)) / segs;
    const float cosS = std::cos(step);
    const float sinS = std::sin(step);
    float ux = 1.0f;
    float uy = 0.0f;
    float prevX = x + radius;
    float prevY = y;
    for (int i = 1; i < segs; ++i) {
        float nx = ux * cosS - uy * sinS;
        float ny = ux * sinS + uy * cosS;
        ux = nx;
        uy = ny;
        float curX = x + ux * radius;
        float curY = y + uy * radius;
        C2D_DrawLine(prevX, prevY, color, curX, curY, color, thickness, z);
        prevX = curX;
        prevY = curY;
    }
    C2D_DrawLine(prevX, prevY, color, x + radius, y, color, thickness, z);
}

void drawRing(float x, float y, float radius, float thickness, u32 color, int segments, float z) {
    drawRingZ(x, y, z, radius, thickness, color, segments);
}

void drawRectOutline(float x, float y, float w, float h, float thickness, u32 color, float z) {
    C2D_DrawLine(x, y, color, x + w, y, color, thickness, z);
    C2D_DrawLine(x + w, y, color, x + w, y + h, color, thickness, z);
    C2D_DrawLine(x + w, y + h, color, x, y + h, color, thickness, z);
    C2D_DrawLine(x, y + h, color, x, y, color, thickness, z);
}

void drawText(C2D_TextBuf buf, float x, float y, float scale, u32 color, const char* str, float z) {
    C2D_Text text;
    drawCachedTextParse(&text, buf, str);
    float rx = std::round(x);
    float ry = std::round(y);
    C2D_DrawText(&text, C2D_WithColor, rx, ry, z, scale, scale, color);
}

void drawCenteredText(C2D_TextBuf buf, float cx, float cy, float scale, u32 color, const char* str, float z) {
    C2D_Text text;
    drawCachedTextParse(&text, buf, str);
    float w = 0.0f, h = 0.0f;
    C2D_TextGetDimensions(&text, scale, scale, &w, &h);
    float rx = std::round(cx - (w * 0.5f));
    float ry = std::round(cy - (h * 0.5f));
    C2D_DrawText(&text, C2D_WithColor, rx, ry, z, scale, scale, color);
}

void drawTextWithShadow(C2D_TextBuf buf, float x, float y, float scale, u32 color, const char* str, float z, u32 shadowColor) {
    C2D_Text text;
    drawCachedTextParse(&text, buf, str);
    float rx = std::round(x);
    float ry = std::round(y);
    C2D_DrawText(&text, C2D_WithColor, rx + 1.0f, ry + 1.0f, z - 0.001f, scale, scale, shadowColor);
    C2D_DrawText(&text, C2D_WithColor, rx, ry, z, scale, scale, color);
}

void drawCenteredTextWithShadow(C2D_TextBuf buf, float cx, float cy, float scale, u32 color, const char* str, float z, u32 shadowColor) {
    C2D_Text text;
    drawCachedTextParse(&text, buf, str);
    float w = 0.0f, h = 0.0f;
    C2D_TextGetDimensions(&text, scale, scale, &w, &h);
    float rx = std::round(cx - (w * 0.5f));
    float ry = std::round(cy - (h * 0.5f));
    C2D_DrawText(&text, C2D_WithColor, rx + 1.0f, ry + 1.0f, z - 0.001f, scale, scale, shadowColor);
    C2D_DrawText(&text, C2D_WithColor, rx, ry, z, scale, scale, color);
}

void drawRightAlignedText(C2D_TextBuf buf, float rx, float y, float scale, u32 color, const char* str, float z) {
    C2D_Text text;
    drawCachedTextParse(&text, buf, str);
    C2D_DrawText(&text, C2D_WithColor | C2D_AlignRight, rx, y, z, scale, scale, color);
}

void drawKeyBadge(C2D_TextBuf buf, float x, float y, const char* key, const char* label, u32 keyClr, u32 textClr, float z) {
    float rx = std::round(x);
    float ry = std::round(y);
    int len = key ? static_cast<int>(std::strlen(key)) : 1;
    float kw = (len <= 1) ? 15.0f : std::round(8.0f + len * 6.0f);
    Ui::pill(rx, ry - 1.0f, kw, 15.0f, z - 0.005f, keyClr);
    drawCenteredText(buf, rx + kw * 0.5f, ry + 6.5f, (len > 3) ? 0.25f : 0.30f, Ui::rgba(0, 0, 0, 245), key, z);
    drawTextWithShadow(buf, rx + kw + 4.0f, ry, 0.34f, textClr, label, z);
}

void drawReverseArrow(float cx, float cy, float targetX, float targetY, float size, u32 color, float z) {
    float angle = std::atan2(targetY - cy, targetX - cx);
    float tipX = cx + std::cos(angle) * (size * 0.48f);
    float tipY = cy + std::sin(angle) * (size * 0.48f);
    float wingAngle = 2.42f; // ~138 grados
    float w1X = tipX + std::cos(angle + wingAngle) * (size * 0.80f);
    float w1Y = tipY + std::sin(angle + wingAngle) * (size * 0.80f);
    float w2X = tipX + std::cos(angle - wingAngle) * (size * 0.80f);
    float w2Y = tipY + std::sin(angle - wingAngle) * (size * 0.80f);
    float notchX = cx - std::cos(angle) * (size * 0.05f);
    float notchY = cy - std::sin(angle) * (size * 0.05f);

    // Chevron solido estilizado de flecha de retorno
    C2D_DrawTriangle(tipX, tipY, color, w1X, w1Y, color, notchX, notchY, color, z);
    C2D_DrawTriangle(tipX, tipY, color, w2X, w2Y, color, notchX, notchY, color, z);
    C2D_DrawLine(w1X, w1Y, color, tipX, tipY, color, 2.2f, z + 0.0001f);
    C2D_DrawLine(w2X, w2Y, color, tipX, tipY, color, 2.2f, z + 0.0001f);
}

std::string truncateString(const std::string& str, size_t maxLen) {
    if (str.size() <= maxLen) return str;
    return str.substr(0, maxLen - 3) + "...";
}

std::string formatScoreThousands(int score) {
    std::string s = std::to_string(score);
    int n = static_cast<int>(s.length()) - 3;
    while (n > 0) {
        s.insert(n, ",");
        n -= 3;
    }
    return s;
}

u32 getScoreGradeColor(const std::string& grade, u32 cursorClr) {
    if (grade == "SS" || grade == "SSH") return Ui::rgba(255, 102, 171);
    if (grade == "S" || grade == "SH") return cursorClr;
    if (grade == "A") return Ui::rgba(110, 235, 110);
    if (grade == "B") return Ui::rgba(100, 180, 255);
    if (grade == "C") return Ui::rgba(195, 120, 255);
    return Ui::rgba(255, 80, 80);
}

void drawStatBar(C2D_TextBuf buf, float x, float y, const char* label, float val, u32 fillClr, u32 bgClr, u32 txtClr) {
    (void)bgClr;
    char str[32];
    std::snprintf(str, sizeof(str), "%s: %.1f", label, val);
    drawText(buf, x, y, 0.40f, txtClr, str);

    float barX = x + 58.0f;
    float barY = y + 3.0f;
    float barW = 92.0f;
    float barH = 6.5f;
    float fillRatio = std::max(0.0f, std::min(1.0f, val / 10.0f));

    Ui::statBar(barX, barY, barW, barH, 0.2f, fillRatio, fillClr);
}
