#include <nds.h>

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "gui.h"
#include "logoSmall.h"

#define LOGO_TRANSPARENT_INDEX 0x5c

static unsigned short *drawing_buffer;
static unsigned short *bottom_framebuffer;
static unsigned short video_colors[256];

static const unsigned char font[40][7] = {
    {0, 0, 0, 0, 0, 0, 0},
    {14, 17, 17, 31, 17, 17, 17},
    {30, 17, 17, 30, 17, 17, 30},
    {14, 17, 16, 16, 16, 17, 14},
    {30, 17, 17, 17, 17, 17, 30},
    {31, 16, 16, 30, 16, 16, 31},
    {31, 16, 16, 30, 16, 16, 16},
    {14, 17, 16, 23, 17, 17, 15},
    {17, 17, 17, 31, 17, 17, 17},
    {14, 4, 4, 4, 4, 4, 14},
    {7, 2, 2, 2, 18, 18, 12},
    {17, 18, 20, 24, 20, 18, 17},
    {16, 16, 16, 16, 16, 16, 31},
    {17, 27, 21, 21, 17, 17, 17},
    {17, 25, 21, 19, 17, 17, 17},
    {14, 17, 17, 17, 17, 17, 14},
    {30, 17, 17, 30, 16, 16, 16},
    {14, 17, 17, 17, 21, 18, 13},
    {30, 17, 17, 30, 20, 18, 17},
    {15, 16, 16, 14, 1, 1, 30},
    {31, 4, 4, 4, 4, 4, 4},
    {17, 17, 17, 17, 17, 17, 14},
    {17, 17, 17, 17, 17, 10, 4},
    {17, 17, 17, 21, 21, 21, 10},
    {17, 17, 10, 4, 10, 17, 17},
    {17, 17, 10, 4, 4, 4, 4},
    {31, 1, 2, 4, 8, 16, 31},
    {14, 17, 19, 21, 25, 17, 14},
    {4, 12, 4, 4, 4, 4, 14},
    {14, 17, 1, 2, 4, 8, 31},
    {30, 1, 1, 14, 1, 1, 30},
    {2, 6, 10, 18, 31, 2, 2},
    {31, 16, 16, 30, 1, 1, 30},
    {14, 16, 16, 30, 17, 17, 14},
    {31, 1, 2, 4, 8, 8, 8},
    {14, 17, 17, 14, 17, 17, 14},
    {14, 17, 17, 15, 1, 1, 14},
    {0, 4, 4, 0, 4, 4, 0},
    {0, 0, 0, 31, 0, 0, 0},
    {0, 0, 0, 0, 0, 4, 8}
};

static int glyph_index(char character) {
    character = (char)toupper((unsigned char)character);
    if (character == ' ') {
        return 0;
    }
    if (character >= 'A' && character <= 'Z') {
        return 1 + character - 'A';
    }
    if (character >= '0' && character <= '9') {
        return 27 + character - '0';
    }
    if (character == ':') {
        return 37;
    }
    if (character == '-') {
        return 38;
    }
    return 39;
}

static void fill_rect(unsigned int x, unsigned int y, unsigned int width,
                      unsigned int height, unsigned short color) {
    if (drawing_buffer == bottom_framebuffer) {
        color |= BIT(15);
    }
    for (unsigned int row = y; row < y + height; row++) {
        unsigned int offset = row * SCREEN_WIDTH + x;
        for (unsigned int column = 0; column < width; column++) {
            drawing_buffer[offset + column] = color;
        }
    }
}

static void outline_rect(unsigned int x, unsigned int y, unsigned int width,
                         unsigned int height, unsigned short color) {
    fill_rect(x, y, width, 1, color);
    fill_rect(x, y + height - 1, width, 1, color);
    fill_rect(x, y, 1, height, color);
    fill_rect(x + width - 1, y, 1, height, color);
}

static void draw_text(unsigned int x, unsigned int y, const char *text,
                      unsigned short color, unsigned int max_characters) {
    if (drawing_buffer == bottom_framebuffer) {
        color |= BIT(15);
    }
    unsigned int characters = 0;
    while (*text != '\0' && characters < max_characters && x + 5 < SCREEN_WIDTH) {
        const unsigned char *rows = font[glyph_index(*text++)];
        for (unsigned int row = 0; row < 7; row++) {
            for (unsigned int column = 0; column < 5; column++) {
                if (rows[row] & (1u << (4 - column))) {
                    drawing_buffer[(y + row) * SCREEN_WIDTH + x + column] = color;
                }
            }
        }
        x += 6;
        characters++;
    }
}

static void draw_text_scaled(unsigned int x, unsigned int y, const char *text,
                             unsigned short color, unsigned int scale,
                             unsigned int max_characters) {
    unsigned int characters = 0;
    while (*text != '\0' && characters < max_characters &&
           x + 5 * scale < SCREEN_WIDTH) {
        const unsigned char *rows = font[glyph_index(*text++)];
        for (unsigned int row = 0; row < 7; row++) {
            for (unsigned int column = 0; column < 5; column++) {
                if (rows[row] & (1u << (4 - column))) {
                    fill_rect(x + column * scale, y + row * scale,
                              scale, scale, color);
                }
            }
        }
        x += 6 * scale;
        characters++;
    }
}

static void draw_logo_small(unsigned int x, unsigned int y,
                            unsigned int output_width, unsigned int output_height) {
    const unsigned char *bitmap = (const unsigned char *)logoSmallBitmap;
    unsigned int x_step = (80u << 16) / output_width;
    unsigned int y_step = (34u << 16) / output_height;
    for (unsigned int row = 0; row < output_height; row++) {
        unsigned int source_y = (row * y_step) >> 16;
        for (unsigned int column = 0; column < output_width; column++) {
            unsigned int source_x = (column * x_step) >> 16;
            unsigned char palette_index = bitmap[source_y * 80 + source_x];
            if (palette_index != LOGO_TRANSPARENT_INDEX) {
                unsigned short color = logoSmallPal[palette_index] & 0x7fff;
                if (drawing_buffer == bottom_framebuffer) {
                    color |= BIT(15);
                }
                drawing_buffer[(y + row) * SCREEN_WIDTH + x + column] = color;
            }
        }
    }
}

static void draw_wifi_signal_indicator(unsigned int x, unsigned int y,
                                       unsigned int strength) {
    unsigned short frame_color = strength == 0 ? RGB15(31, 0, 0) :
                                strength == 1 ? RGB15(31, 31, 0) : RGB15(0, 31, 0);
    unsigned short white = RGB15(31, 31, 31);
    unsigned short gray = RGB15(17, 17, 17);

    fill_rect(x, y, 16, 16, RGB15(0, 0, 0));
    fill_rect(x + 1, y + 1, 14, 1, frame_color);
    fill_rect(x + 1, y + 14, 14, 1, frame_color);
    fill_rect(x + 1, y + 1, 1, 14, frame_color);
    fill_rect(x + 14, y + 1, 1, 14, frame_color);
    fill_rect(x + 2, y + 5, 3, 3, gray);
    fill_rect(x + 3, y + 6, 1, 1, RGB15(0, 0, 0));
    fill_rect(x + 3, y + 8, 1, 5, gray);

    static const unsigned int bar_x[] = {5, 8, 11};
    static const unsigned int bar_height[] = {2, 5, 8};
    for (unsigned int bar = 0; bar < 3; bar++) {
        unsigned short color = bar + 1 <= strength ? white : gray;
        fill_rect(x + bar_x[bar], y + 13 - bar_height[bar], 2,
                  bar_height[bar], color);
    }
}

static void draw_top_screen(unsigned int wifi_strength) {
    unsigned short ink = RGB15(4, 5, 6);
    unsigned short muted = RGB15(12, 12, 12);
    drawing_buffer = VRAM_A;
    fill_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, RGB15(31, 31, 31));
    draw_logo_small(8, 4, 80, 34);
    draw_wifi_signal_indicator(226, 7, wifi_strength);

    draw_text_scaled(76, 68, "SEARCH", ink, 3, 10);
    draw_text(62, 105, "FIND VIDEOS ON YOUTUBE", muted, 24);
}

static void draw_search_button(void) {
    fill_rect(202, 39, 42, 21, RGB15(26, 3, 3));
    outline_rect(202, 39, 42, 21, RGB15(21, 2, 2));
    draw_text(211, 46, "GO", RGB15(31, 31, 31), 4);
}

static void draw_keyboard_key(unsigned int x, unsigned int y, unsigned int width,
                              char character) {
    fill_rect(x, y, width, 20, RGB15(31, 31, 31));
    outline_rect(x, y, width, 20, RGB15(17, 18, 18));
    char label[2] = {character, '\0'};
    unsigned int label_x = x + (width - 6) / 2;
    draw_text(label_x, y + 6, label, RGB15(4, 6, 7), 1);
}

static void draw_keyboard(void) {
    static const char *rows[] = {"1234567890", "qwertyuiop", "asdfghjkl", "zxcvbnm-"};
    static const unsigned int counts[] = {10, 10, 9, 8};
    static const unsigned int starts[] = {7, 7, 14, 13};
    static const unsigned int widths[] = {22, 22, 24, 27};
    static const unsigned int gaps[] = {2, 2, 2, 2};

    fill_rect(0, 66, SCREEN_WIDTH, 126, RGB15(24, 24, 23));
    for (unsigned int row = 0; row < 4; row++) {
        unsigned int y = 77 + row * 23;
        for (unsigned int column = 0; column < counts[row]; column++) {
            draw_keyboard_key(starts[row] + column * (widths[row] + gaps[row]),
                              y, widths[row], rows[row][column]);
        }
    }

    fill_rect(13, 170, 108, 21, RGB15(31, 31, 31));
    outline_rect(13, 170, 108, 21, RGB15(17, 18, 18));
    draw_text(53, 177, "SPACE", RGB15(4, 6, 7), 8);
    fill_rect(125, 170, 56, 21, RGB15(31, 31, 31));
    outline_rect(125, 170, 56, 21, RGB15(17, 18, 18));
    draw_text(137, 177, "DEL", RGB15(4, 6, 7), 5);
    fill_rect(185, 170, 58, 21, RGB15(25, 3, 3));
    outline_rect(185, 170, 58, 21, RGB15(17, 18, 18));
    draw_text(197, 177, "DONE", RGB15(31, 31, 31), 5);
}

static void draw_video_row(unsigned int y, unsigned int result_number,
                           const char *title, int selected) {
    unsigned short ink = RGB15(5, 6, 7);
    unsigned short separator = RGB15(23, 23, 22);
    unsigned short row_color = selected ? RGB15(31, 29, 26) : RGB15(31, 31, 31);
    static const unsigned short thumbnails[] = {
        RGB15(6, 13, 19), RGB15(16, 8, 7), RGB15(10, 14, 7), RGB15(14, 9, 17)
    };

    fill_rect(0, y, SCREEN_WIDTH, 16, row_color);
    fill_rect(0, y + 15, SCREEN_WIDTH, 1, separator);
    if (selected) {
        fill_rect(0, y, 3, 16, RGB15(27, 3, 3));
    }
    fill_rect(7, y + 2, 23, 12, thumbnails[result_number % 4]);
    fill_rect(15, y + 5, 7, 6, RGB15(25, 4, 4));
    fill_rect(17, y + 6, 1, 4, RGB15(31, 31, 31));
    fill_rect(18, y + 7, 2, 2, RGB15(31, 31, 31));

    char number[4];
    number[0] = (char)('1' + result_number % 9);
    number[1] = '.';
    number[2] = ' ';
    number[3] = '\0';
    draw_text(34, y + 4, number, RGB15(14, 14, 14), 3);
    draw_text(52, y + 4, title, ink, 32);
}

static void draw_bottom_screen(const char *query, char next_character,
                               const char *status_text,
                               const char video_titles[GUI_RESULT_SLOTS][GUI_TITLE_CAPACITY],
                               unsigned int video_count, unsigned int selected_video,
                               unsigned int wifi_strength, int keyboard_visible) {
    unsigned short paper = RGB15(31, 31, 31);
    unsigned short ink = RGB15(5, 6, 7);
    unsigned short muted = RGB15(14, 15, 15);

    drawing_buffer = bottom_framebuffer;
    fill_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, RGB15(28, 28, 27));
    fill_rect(0, 0, SCREEN_WIDTH, 29, paper);
    draw_wifi_signal_indicator(229, 6, wifi_strength);

    fill_rect(7, 34, 242, 28, paper);
    outline_rect(7, 34, 242, 28, RGB15(18, 19, 19));
    draw_text(13, 44, "SEARCH", muted, 8);
    char visible_query[30];
    unsigned int query_length = (unsigned int)strlen(query);
    unsigned int query_offset = query_length > 23 ? query_length - 23 : 0;
    unsigned int copy_length = query_length - query_offset;
    if (copy_length > sizeof(visible_query) - 2) {
        copy_length = sizeof(visible_query) - 2;
    }
    memcpy(visible_query, query + query_offset, copy_length);
    visible_query[copy_length++] = next_character;
    visible_query[copy_length] = '\0';
    draw_text(58, 44, visible_query, ink, 23);
    draw_search_button();

    if (keyboard_visible) {
        draw_keyboard();
    } else {
        fill_rect(0, 66, SCREEN_WIDTH, 16, RGB15(23, 23, 22));
        draw_text(8, 71, "VIDEO RESULTS", paper, 16);
        char count_text[2] = {(char)('0' + video_count), '\0'};
        draw_text(218, 71, count_text, paper, 1);

        unsigned int first_result = selected_video >= 2 ? selected_video - 2 : 0;
        for (unsigned int slot = 0; slot < 4; slot++) {
            unsigned int result = first_result + slot;
            unsigned int row_y = 83 + slot * 16;
            if (result < video_count) {
                draw_video_row(row_y, result, video_titles[result], result == selected_video);
            } else {
                fill_rect(0, row_y, SCREEN_WIDTH, 16, paper);
                fill_rect(0, row_y + 15, SCREEN_WIDTH, 1, RGB15(23, 23, 22));
            }
        }

        fill_rect(0, 149, SCREEN_WIDTH, 14, RGB15(24, 24, 23));
        draw_text(7, 152, status_text, paper, 39);
        const char *button_labels[] = {"CHAR", "ADD", "DEL", "WATCH"};
        for (unsigned int button = 0; button < 4; button++) {
            unsigned int x = 4 + button * 63;
            unsigned short color = button == 3 ? RGB15(25, 3, 3) : RGB15(23, 23, 22);
            fill_rect(x, 166, 59, 26, color);
            outline_rect(x, 166, 59, 26, RGB15(16, 16, 16));
            unsigned int label_x = x + (59 - (unsigned int)strlen(button_labels[button]) * 6) / 2;
            draw_text(label_x, 175, button_labels[button], paper, 8);
        }
    }
}

void gui_init(void) {
    videoSetMode(MODE_FB0);
    videoSetModeSub(MODE_5_2D);
    vramSetBankA(VRAM_A_LCD);
    vramSetBankC(VRAM_C_SUB_BG);
    bottom_framebuffer = BG_GFX_SUB;
    int bottom_background = bgInitSub(2, BgType_Bmp16, BgSize_B16_256x256, 0, 0);
    bgShow(bottom_background);
    for (unsigned int pixel = 0; pixel < 256; pixel++) {
        unsigned int red3 = pixel >> 5;
        unsigned int green3 = (pixel >> 2) & 7;
        unsigned int blue2 = pixel & 3;
        unsigned int red5 = (red3 * 31 + 3) / 7;
        unsigned int green5 = (green3 * 31 + 3) / 7;
        unsigned int blue5 = (blue2 * 31 + 1) / 3;
        video_colors[pixel] = (unsigned short)RGB15(red5, green5, blue5);
    }
}

void gui_draw_video_frame(const unsigned char *frame) {
    unsigned short *framebuffer = VRAM_A;
    for (unsigned int y = 0; y < 96; y++) {
        unsigned short *first_row = framebuffer + (y * 2) * SCREEN_WIDTH;
        for (unsigned int x = 0; x < 128; x++) {
            unsigned short color = video_colors[frame[y * 128 + x]];
            first_row[x * 2] = color;
            first_row[x * 2 + 1] = color;
        }
        memcpy(first_row + SCREEN_WIDTH, first_row, SCREEN_WIDTH * sizeof(*first_row));
    }
}

void gui_draw_player(const char *title, unsigned int frame_index, int paused,
                     unsigned int duration_seconds, unsigned int volume,
                     unsigned int wifi_strength) {
    drawing_buffer = bottom_framebuffer;
    unsigned short paper = RGB15(31, 31, 31);
    unsigned short ink = RGB15(4, 5, 6);
    fill_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, RGB15(29, 29, 28));
    fill_rect(0, 0, SCREEN_WIDTH, 29, paper);
    draw_logo_small(9, 2, 60, 25);
    draw_wifi_signal_indicator(229, 6, wifi_strength);

    draw_text(10, 38, title, ink, 34);
    fill_rect(10, 54, 25, 25, RGB15(20, 17, 13));
    fill_rect(12, 56, 21, 21, RGB15(22, 8, 6));
    fill_rect(16, 61, 13, 2, RGB15(31, 25, 18));
    fill_rect(16, 66, 8, 2, RGB15(31, 25, 18));
    draw_text(42, 58, "NDSTUBE", ink, 12);
    draw_text(42, 69, "LAN VIDEO", RGB15(12, 12, 11), 12);

    unsigned int max_seconds = duration_seconds == 0 ? 120 : duration_seconds;
    if (max_seconds > 120) {
        max_seconds = 120;
    }
    unsigned int seconds = frame_index / 6;
    if (seconds > max_seconds) {
        seconds = max_seconds;
    }
    char elapsed[12];
    char duration[12];
    snprintf(elapsed, sizeof(elapsed), "%02u:%02u", seconds / 60, seconds % 60);
    draw_text(10, 91, elapsed, ink, 8);
    snprintf(duration, sizeof(duration), "%02u:%02u", max_seconds / 60, max_seconds % 60);
    draw_text(211, 91, duration, ink, 6);
    fill_rect(10, 102, 236, 13, RGB15(0, 0, 0));
    fill_rect(12, 104, 232, 9, RGB15(31, 31, 31));
    unsigned int progress = frame_index * 232 / (max_seconds * 6);
    if (progress > 232) {
        progress = 232;
    }
    fill_rect(12, 104, progress, 9,
              RGB15(25, 3, 3));
    draw_text(10, 122, "VOLUME", RGB15(8, 8, 8), 10);
    fill_rect(55, 124, 145, 7, RGB15(20, 20, 19));
    fill_rect(55, 124, volume * 145 / 127, 7, RGB15(8, 18, 11));

    const char *labels[] = {"VOL-", paused ? "PLAY" : "PAUSE", "STOP", "VOL+"};
    for (unsigned int button = 0; button < 4; button++) {
        unsigned int x = 4 + button * 63;
        unsigned short color = button == 2 ? RGB15(25, 3, 3) : RGB15(23, 23, 22);
        fill_rect(x, 145, 59, 40, color);
        outline_rect(x, 145, 59, 40, RGB15(15, 15, 14));
        unsigned int label_x = x + (59 - (unsigned int)strlen(labels[button]) * 6) / 2;
        draw_text(label_x, 161, labels[button], RGB15(31, 31, 31), 8);
    }
}

GuiPlayerTouchAction gui_player_touch_action(unsigned int x, unsigned int y) {
    if (y < 145 || y >= 190) {
        return GUI_PLAYER_TOUCH_NONE;
    }
    switch (x / 64) {
    case 0:
        return GUI_PLAYER_TOUCH_VOLUME_DOWN;
    case 1:
        return GUI_PLAYER_TOUCH_TOGGLE;
    case 2:
        return GUI_PLAYER_TOUCH_STOP;
    case 3:
        return GUI_PLAYER_TOUCH_VOLUME_UP;
    default:
        return GUI_PLAYER_TOUCH_NONE;
    }
}

void gui_draw(const char *query, char next_character, const char *status_text,
              const char video_titles[GUI_RESULT_SLOTS][GUI_TITLE_CAPACITY],
              unsigned int video_count, unsigned int selected_video,
          unsigned int wifi_strength, int keyboard_visible) {
    (void)next_character;
    (void)status_text;
    (void)video_titles;
    (void)video_count;
    (void)selected_video;
    draw_top_screen(wifi_strength);
    draw_bottom_screen(query, next_character, status_text, video_titles,
                 video_count, selected_video, wifi_strength, keyboard_visible);
}

GuiTouchAction gui_touch_action(unsigned int x, unsigned int y,
                               unsigned int selected_video, unsigned int video_count,
                               int keyboard_visible, unsigned int *result_index,
                               char *key_character) {
    if (y >= 34 && y < 62) {
        return x >= 202 ? GUI_TOUCH_SEARCH : GUI_TOUCH_KEYBOARD;
    }
    if (keyboard_visible && y >= 77 && y < 169) {
        static const char *rows[] = {"1234567890", "qwertyuiop", "asdfghjkl", "zxcvbnm-"};
        static const unsigned int counts[] = {10, 10, 9, 8};
        static const unsigned int starts[] = {7, 7, 14, 13};
        static const unsigned int widths[] = {22, 22, 24, 27};
        unsigned int row = (y - 77) / 23;
        unsigned int row_y = 77 + row * 23;
        if (row < 4 && y < row_y + 20 && x >= starts[row]) {
            unsigned int stride = widths[row] + 2;
            unsigned int column = (x - starts[row]) / stride;
            unsigned int key_x = starts[row] + column * stride;
            if (column < counts[row] && x < key_x + widths[row]) {
                *key_character = rows[row][column];
                return GUI_TOUCH_KEY;
            }
        }
    }
    if (keyboard_visible && y >= 170) {
        if (x >= 13 && x < 121) {
            return GUI_TOUCH_SPACE;
        }
        if (x >= 125 && x < 181) {
            return GUI_TOUCH_BACKSPACE;
        }
        if (x >= 185 && x < 243) {
            return GUI_TOUCH_DONE;
        }
    }
    if (!keyboard_visible && y >= 83 && y < 147) {
        unsigned int first_result = selected_video >= 2 ? selected_video - 2 : 0;
        unsigned int index = first_result + (y - 83) / 16;
        if (index < video_count) {
            *result_index = index;
            return GUI_TOUCH_RESULT;
        }
    }
    if (!keyboard_visible && y >= 166) {
        switch (x / 64) {
        case 0:
            return GUI_TOUCH_CHARACTER;
        case 1:
            return GUI_TOUCH_ADD;
        case 2:
            return GUI_TOUCH_DELETE;
        case 3:
            return GUI_TOUCH_WATCH;
        default:
            break;
        }
    }
    return GUI_TOUCH_NONE;
}
