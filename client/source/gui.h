#ifndef NDSTUBE_GUI_H
#define NDSTUBE_GUI_H

#define GUI_RESULT_SLOTS 8
#define GUI_TITLE_CAPACITY 101

typedef enum {
    GUI_TOUCH_NONE,
    GUI_TOUCH_SEARCH,
    GUI_TOUCH_KEYBOARD,
    GUI_TOUCH_KEY,
    GUI_TOUCH_SPACE,
    GUI_TOUCH_BACKSPACE,
    GUI_TOUCH_DONE,
    GUI_TOUCH_CHARACTER,
    GUI_TOUCH_ADD,
    GUI_TOUCH_DELETE,
    GUI_TOUCH_WATCH,
    GUI_TOUCH_RESULT
} GuiTouchAction;

typedef enum {
    GUI_PLAYER_TOUCH_NONE,
    GUI_PLAYER_TOUCH_TOGGLE,
    GUI_PLAYER_TOUCH_STOP,
    GUI_PLAYER_TOUCH_VOLUME_DOWN,
    GUI_PLAYER_TOUCH_VOLUME_UP
} GuiPlayerTouchAction;

void gui_init(void);
void gui_draw(const char *query, char next_character, const char *status_text,
              const char video_titles[GUI_RESULT_SLOTS][GUI_TITLE_CAPACITY],
              unsigned int video_count, unsigned int selected_video,
              unsigned int wifi_strength, int keyboard_visible);
GuiTouchAction gui_touch_action(unsigned int x, unsigned int y,
                               unsigned int selected_video, unsigned int video_count,
                               int keyboard_visible, unsigned int *result_index,
                               char *key_character);
void gui_draw_video_frame(const unsigned char *frame);
void gui_draw_player(const char *title, unsigned int frame_index, int paused,
                     unsigned int duration_seconds, unsigned int volume,
                     unsigned int wifi_strength);
GuiPlayerTouchAction gui_player_touch_action(unsigned int x, unsigned int y);

#endif