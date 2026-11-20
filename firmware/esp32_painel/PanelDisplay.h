#pragma once
#include <Adafruit_ST7789.h>
#include <initializer_list>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "PanelAssets.h"

namespace painel_lcd {
// Campo Grande e Cuiaba, UTC-4. Os eventos continuam registrados em UTC.
constexpr int32_t UTC_OFFSET_SECONDS = -4 * 60 * 60;
constexpr time_t MIN_VALID_EPOCH = 1600000000;
constexpr uint16_t WHITE = 0xFFFF;
constexpr uint16_t LIME = 0xAFE7;
constexpr uint16_t MUTED = 0xD77A;

struct ScreenState {
    time_t clock;
    time_t lastBell;
    const char* gateState;
    bool ringing;
    bool historyError;
    bool soundError;
};

class PanelDisplay {
public:
    explicit PanelDisplay(Adafruit_ST7789& tft) : tft_(tft) {}

    void begin() {
        paint({0, 0, WIDTH, HEIGHT});
    }

    void update(uint32_t now, const ScreenState& state) {
        if (drawn_ && uint32_t(now - updatedAt_) < 100) return;
        updatedAt_ = now;
        const int64_t minute = state.clock > MIN_VALID_EPOCH ? int64_t(state.clock) / 60 : -1;
        if (!drawn_ || minute != minute_) {
            minute_ = minute;
            struct tm local = {};
            const bool valid = localTime(state.clock, local);
            char time[6] = "--:--";
            if (valid) strftime(time, sizeof(time), "%H:%M", &local);
            paint(TIME, {centered(timeFont, time, 115, 103)});

            const int32_t day = valid ? local.tm_year * 366 + local.tm_yday : -1;
            if (!drawn_ || day != day_) {
                day_ = day;
                char date[40] = "Aguardando hora";
                if (valid) {
                    const char* const weekdays[] = {"Domingo", "Segunda-feira", "Terça-feira", "Quarta-feira",
                                                   "Quinta-feira", "Sexta-feira", "Sábado"};
                    char numeric[16] = {};
                    strftime(numeric, sizeof(numeric), "%d/%m/%Y", &local);
                    snprintf(date, sizeof(date), "%s, %s", weekdays[local.tm_wday], numeric);
                }
                paint(DATE, {centered(smallFont, date, 89, 122)});
            }
        }

        if (!drawn_ || state.lastBell != lastBell_ || state.ringing != ringing_) {
            lastBell_ = state.lastBell;
            ringing_ = state.ringing;
            struct tm local = {};
            char time[6] = "--:--";
            if (localTime(lastBell_, local)) strftime(time, sizeof(time), "%H:%M", &local);
            paint(LAST, {centered(bellFont, time, 270, 112, ringing_ ? LIME : WHITE)});
        }

        const Mode mode = gateMode(state.gateState);
        if (!drawn_ || mode != mode_) {
            mode_ = mode;
            const Status& display = states[static_cast<uint8_t>(mode)];
            const Mask lock{display.icon, 34, 42, 121, 150, display.color};
            paint(STATUS, {centered(statusFont, "PORTÃO", 229, 169, display.color),
                           centered(statusFont, display.text, 229, 193, display.color)}, &lock);
        }

        const char* footer = state.ringing ? "Campainha tocando" :
                             state.soundError ? "Erro no buzzer - veja o Serial" :
                             state.historyError ? "Histórico local indisponível" :
                             mode == Mode::Offline ? "Sem comunicação com o servo" :
                             "Estado estimado pelo servo";
        if (!drawn_ || strcmp(footer_, footer) != 0) {
            snprintf(footer_, sizeof(footer_), "%s", footer);
            paint(FOOTER, {centered(smallFont, footer, 160, 217)});
        }
        drawn_ = true;
    }

private:
    enum class Mode : uint8_t { Closed, Open, Opening, Closing, Offline };
    struct Status { const char* text; const uint8_t* icon; uint16_t color; };
    static constexpr Status states[] = {
        {"FECHADO", closedLock, WHITE}, {"ABERTO", openLock, WHITE},
        {"ABRINDO", openLock, WHITE}, {"FECHANDO", closedLock, WHITE},
        {"SEM SINAL", lostLink, MUTED},
    };
    struct Text {
        const Font& font;
        const char* value;
        int16_t x, baseline;
        uint16_t color;
    };
    struct Mask {
        const uint8_t* data;
        uint8_t width, height;
        int16_t x, y;
        uint16_t color;
    };

    static bool localTime(time_t epoch, struct tm& local) {
        if (epoch <= MIN_VALID_EPOCH) return false;
        const time_t offsetTime = epoch + UTC_OFFSET_SECONDS;
        return gmtime_r(&offsetTime, &local) != nullptr;
    }

    static Mode gateMode(const char* value) {
        if (!value) return Mode::Offline;
        if (!strcmp(value, "closed")) return Mode::Closed;
        if (!strcmp(value, "open")) return Mode::Open;
        if (!strcmp(value, "opening")) return Mode::Opening;
        if (!strcmp(value, "closing")) return Mode::Closing;
        return Mode::Offline;
    }

    static uint16_t nextChar(const char*& text) {
        const uint8_t first = static_cast<uint8_t>(*text++);
        if ((first == 0xC2 || first == 0xC3) && (static_cast<uint8_t>(*text) & 0xC0) == 0x80) {
            return ((first & 0x1F) << 6) | (static_cast<uint8_t>(*text++) & 0x3F);
        }
        return first;
    }

    static const Glyph* glyphFor(const Font& font, uint16_t code) {
        for (uint8_t i = 0; i < font.count; ++i) {
            if (font.glyphs[i].code == code) return &font.glyphs[i];
        }
        return nullptr;
    }

    static Text centered(const Font& font, const char* text, int16_t x, int16_t baseline, uint16_t color = WHITE) {
        int16_t width = 0;
        for (const char* next = text; *next; ) {
            const Glyph* glyph = glyphFor(font, nextChar(next));
            if (glyph) width += glyph->advance;
        }
        return {font, text, static_cast<int16_t>(x - width / 2), baseline, color};
    }

    static uint16_t blend(uint16_t from, uint16_t to, uint8_t alpha) {
        const uint8_t inverse = 15 - alpha;
        const uint16_t red = ((from >> 11) * inverse + (to >> 11) * alpha + 7) / 15;
        const uint16_t green = (((from >> 5) & 63) * inverse + ((to >> 5) & 63) * alpha + 7) / 15;
        const uint16_t blue = ((from & 31) * inverse + (to & 31) * alpha + 7) / 15;
        return (red << 11) | (green << 5) | blue;
    }

    void overlayRow(const Rect& rect, int16_t y, const Mask& mask) {
        const int16_t row = y - mask.y;
        if (row < 0 || row >= mask.height) return;
        for (int16_t x = 0; x < mask.width; ++x) {
            const int16_t pixel = mask.x + x - rect.x;
            if (pixel < 0 || pixel >= rect.w) continue;
            const size_t index = size_t(row) * mask.width + x;
            const uint8_t byte = pgm_read_byte(mask.data + index / 2);
            const uint8_t alpha = index % 2 ? byte & 0x0F : byte >> 4;
            if (alpha == 15) line_[pixel] = mask.color;
            else if (alpha) line_[pixel] = blend(line_[pixel], mask.color, alpha);
        }
    }

    void textRow(const Rect& rect, int16_t y, const Text& label) {
        int16_t x = label.x;
        for (const char* next = label.value; *next; ) {
            const Glyph* glyph = glyphFor(label.font, nextChar(next));
            if (!glyph) continue;
            const Mask mask{label.font.mask + glyph->offset, glyph->width, glyph->height,
                            static_cast<int16_t>(x + glyph->left),
                            static_cast<int16_t>(label.baseline + glyph->top), label.color};
            overlayRow(rect, y, mask);
            x += glyph->advance;
        }
    }

    void paint(const Rect& rect, std::initializer_list<Text> labels = {}, const Mask* icon = nullptr) {
        tft_.startWrite();
        tft_.setAddrWindow(rect.x, rect.y, rect.w, rect.h);
        for (int16_t y = rect.y; y < rect.y + rect.h; ++y) {
            const uint8_t* source = backdrop + size_t(y) * WIDTH + rect.x;
            for (int16_t x = 0; x < rect.w; ++x) {
                line_[x] = pgm_read_word(palette + pgm_read_byte(source + x));
            }
            if (icon) overlayRow(rect, y, *icon);
            for (const Text& label : labels) textRow(rect, y, label);
            tft_.writePixels(line_, rect.w, true, false);
        }
        tft_.endWrite();
    }

    Adafruit_ST7789& tft_;
    alignas(4) uint16_t line_[WIDTH];
    uint32_t updatedAt_ = 0;
    int64_t minute_ = -1;
    int32_t day_ = -1;
    time_t lastBell_ = 0;
    Mode mode_ = Mode::Offline;
    bool drawn_ = false, ringing_ = false;
    char footer_[48] = {};
};
}
