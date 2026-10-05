#include <nds.h>
#include <dswifi9.h>
#include "gui.h"
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#include <stdio.h>
#include <string.h>

#define SERVER_IP "172.20.50.68"
#define SERVER_PORT 8080
#define RESPONSE_CAPACITY 4096
#define MAX_RESULTS GUI_RESULT_SLOTS
#define VIDEO_WIDTH 128
#define VIDEO_HEIGHT 96
#define VIDEO_FPS 6
#define VIDEO_FRAME_SIZE (VIDEO_WIDTH * VIDEO_HEIGHT)
#define AUDIO_RATE 8000
#define AUDIO_BLOCK_SIZE ((AUDIO_RATE + VIDEO_FPS - 1) / VIDEO_FPS)
#define SOCKET_BUFFER_SIZE 8192
#define VIDEO_RECV_BUFFER_SIZE (32 * 1024)

static const char alphabet[] = " abcdefghijklmnopqrstuvwxyz0123456789-";
static char query[64];
static unsigned int query_length;
static unsigned int alphabet_index = 1;
static char video_ids[MAX_RESULTS][12];
static char video_titles[MAX_RESULTS][GUI_TITLE_CAPACITY];
static unsigned int video_count;
static unsigned int selected_video;
static unsigned char video_frame[VIDEO_FRAME_SIZE];
static signed char audio_block[AUDIO_BLOCK_SIZE];
static char status_text[80] = "Set the relay IP in source, then search.";
static int wifi_initialized;
static unsigned int wifi_strength;
static unsigned int wifi_refresh_frames;

typedef struct {
    int socket_fd;
    unsigned char buffer[SOCKET_BUFFER_SIZE];
    unsigned int offset;
    unsigned int length;
} SocketReader;

static int send_all(int socket_fd, const char *data, unsigned int length) {
    unsigned int sent = 0;
    while (sent < length) {
        int count = send(socket_fd, data + sent, length - sent, 0);
        if (count <= 0) {
            return 0;
        }
        sent += (unsigned int)count;
    }
    return 1;
}

static int connect_relay(const char *resource) {
    if (!wifi_initialized) {
        snprintf(status_text, sizeof(status_text), "Connecting to configured Wi-Fi...");
        if (!Wifi_InitDefault(WFC_CONNECT)) {
            snprintf(status_text, sizeof(status_text), "Wi-Fi setup failed; check console settings.");
            return -1;
        }
        wifi_initialized = 1;
    }

    int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0) {
        snprintf(status_text, sizeof(status_text), "Could not open network socket.");
        return -1;
    }

    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(SERVER_PORT);
    address.sin_addr.s_addr = inet_addr(SERVER_IP);
    snprintf(status_text, sizeof(status_text), "Contacting relay at %s...", SERVER_IP);
    if (connect(socket_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        snprintf(status_text, sizeof(status_text), "Relay connection failed at %s.", SERVER_IP);
        closesocket(socket_fd);
        return -1;
    }

    char request[400];
    snprintf(request, sizeof(request),
             "GET %s HTTP/1.0\r\nHost: %s\r\nConnection: close\r\n\r\n",
             resource, SERVER_IP);
    if (!send_all(socket_fd, request, (unsigned int)strlen(request))) {
        snprintf(status_text, sizeof(status_text), "Could not send relay request.");
        closesocket(socket_fd);
        return -1;
    }
    return socket_fd;
}

static unsigned int read_wifi_strength(void) {
    if (!wifi_initialized || Wifi_AssocStatus() != ASSOCSTATUS_ASSOCIATED) {
        return 0;
    }
    if (isDSiMode()) {
        int dbm = (int)(s8)wlmgrGetRssi();
        if (dbm <= -90) return 0;
        if (dbm >= -52) return 3;
        if (dbm >= -65) return 2;
        if (dbm >= -78) return 1;
        return 0;
    }
    return wlmgrGetSignalStrength();
}

static int refresh_wifi_strength(void) {
    wifi_refresh_frames++;
    if (wifi_refresh_frames < 60) {
        return 0;
    }
    wifi_refresh_frames = 0;
    unsigned int strength = read_wifi_strength();
    if (strength == wifi_strength) {
        return 0;
    }
    wifi_strength = strength;
    return 1;
}

static int reader_read_exact(SocketReader *reader, unsigned char *output, unsigned int wanted) {
    unsigned int copied = 0;
    while (copied < wanted) {
        if (reader->offset == reader->length) {
            int received = recv(reader->socket_fd, reader->buffer, sizeof(reader->buffer), 0);
            if (received <= 0) {
                return 0;
            }
            reader->offset = 0;
            reader->length = (unsigned int)received;
        }

        unsigned int available = reader->length - reader->offset;
        unsigned int amount = wanted - copied;
        if (amount > available) {
            amount = available;
        }
        memcpy(output + copied, reader->buffer + reader->offset, amount);
        reader->offset += amount;
        copied += amount;
    }
    return 1;
}

static int reader_read_headers(SocketReader *reader, char *headers, unsigned int capacity) {
    unsigned int length = 0;
    while (length + 1 < capacity) {
        if (!reader_read_exact(reader, (unsigned char *)&headers[length], 1)) {
            return 0;
        }
        length++;
        if (length >= 4 && memcmp(headers + length - 4, "\r\n\r\n", 4) == 0) {
            headers[length] = '\0';
            return 1;
        }
    }
    return 0;
}

static void encode_query(char *encoded, unsigned int capacity) {
    static const char hex[] = "0123456789ABCDEF";
    unsigned int output = 0;
    for (unsigned int index = 0; index < query_length && output + 4 < capacity; index++) {
        unsigned char character = (unsigned char)query[index];
        if ((character >= 'a' && character <= 'z') ||
            (character >= 'A' && character <= 'Z') ||
            (character >= '0' && character <= '9') || character == '-') {
            encoded[output++] = (char)character;
        } else if (character == ' ') {
            encoded[output++] = '+';
        } else {
            encoded[output++] = '%';
            encoded[output++] = hex[character >> 4];
            encoded[output++] = hex[character & 15];
        }
    }
    encoded[output] = '\0';
}

static void copy_result_rows(char *response) {
    char *body = strstr(response, "\r\n\r\n");
    if (body == NULL || strstr(response, " 200 ") == NULL) {
        snprintf(status_text, sizeof(status_text), "Relay error: %.60s", response);
        video_count = 0;
        return;
    }

    body += 4;
    video_count = 0;
    selected_video = 0;
    while (*body != '\0' && video_count < MAX_RESULTS) {
        char *line_end = strchr(body, '\n');
        if (line_end != NULL) {
            *line_end = '\0';
        }
        char *id_end = strchr(body, '\t');
        if (id_end != NULL && (unsigned int)(id_end - body) == 11) {
            *id_end = '\0';
            char *duration_end = strchr(id_end + 1, '\t');
            if (duration_end != NULL) {
                size_t title_length;
                strncpy(video_ids[video_count], body, sizeof(video_ids[video_count]) - 1);
                video_ids[video_count][sizeof(video_ids[video_count]) - 1] = '\0';
                title_length = strlen(duration_end + 1);
                if (title_length >= sizeof(video_titles[video_count])) {
                    title_length = sizeof(video_titles[video_count]) - 1;
                }
                memcpy(video_titles[video_count], duration_end + 1, title_length);
                video_titles[video_count][title_length] = '\0';
                video_count++;
            }
        }
        if (line_end == NULL) {
            break;
        }
        body = line_end + 1;
    }
    snprintf(status_text, sizeof(status_text), video_count ? "Search complete. Select a title and press X." : "No results found.");
}

static void perform_search(void) {
    char encoded[256];
    char resource[300];
    char response[RESPONSE_CAPACITY];
    unsigned int response_length = 0;

    if (query_length == 0) {
        snprintf(status_text, sizeof(status_text), "Enter a search term first.");
        return;
    }

    encode_query(encoded, sizeof(encoded));
    snprintf(resource, sizeof(resource), "/search?q=%s", encoded);
    int socket_fd = connect_relay(resource);
    if (socket_fd < 0) {
        return;
    }

    while (response_length + 1 < sizeof(response)) {
        int count = recv(socket_fd, response + response_length,
                         sizeof(response) - response_length - 1, 0);
        if (count <= 0) {
            break;
        }
        response_length += (unsigned int)count;
    }
    response[response_length] = '\0';
    closesocket(socket_fd);
    copy_result_rows(response);
}

static int update_player_controls(int pressed, int *playing, int *paused,
                                  int audio_channel, unsigned int *volume) {
    int changed = 0;
    GuiPlayerTouchAction touch_action = GUI_PLAYER_TOUCH_NONE;
    if (pressed & KEY_TOUCH) {
        touchPosition touch;
        touchRead(&touch);
        touch_action = gui_player_touch_action(touch.px, touch.py);
    }

    if ((pressed & KEY_B) || touch_action == GUI_PLAYER_TOUCH_STOP) {
        *playing = 0;
        return 1;
    }
    if ((pressed & KEY_X) || touch_action == GUI_PLAYER_TOUCH_TOGGLE) {
        *paused = !*paused;
        if (*paused) {
            soundPause(audio_channel);
        } else {
            soundResume(audio_channel);
        }
        changed = 1;
    }
    if ((pressed & KEY_LEFT) || touch_action == GUI_PLAYER_TOUCH_VOLUME_DOWN) {
        *volume = *volume > 16 ? *volume - 16 : 0;
        soundSetVolume(audio_channel, (u8)*volume);
        changed = 1;
    }
    if ((pressed & KEY_RIGHT) || touch_action == GUI_PLAYER_TOUCH_VOLUME_UP) {
        *volume = *volume < 111 ? *volume + 16 : 127;
        soundSetVolume(audio_channel, (u8)*volume);
        changed = 1;
    }
    return changed;
}

static int reader_read_nonblocking(SocketReader *reader, unsigned char *output,
                                   unsigned int wanted, unsigned int *copied) {
    while (*copied < wanted) {
        if (reader->offset == reader->length) {
            int received = recv(reader->socket_fd, reader->buffer,
                                sizeof(reader->buffer), 0);
            if (received == 0) {
                return -1;
            }
            if (received < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    return 0;
                }
                return -1;
            }
            reader->offset = 0;
            reader->length = (unsigned int)received;
        }

        unsigned int available = reader->length - reader->offset;
        unsigned int amount = wanted - *copied;
        if (amount > available) {
            amount = available;
        }
        memcpy(output + *copied, reader->buffer + reader->offset, amount);
        reader->offset += amount;
        *copied += amount;
    }
    return 1;
}

static int read_video_payload(SocketReader *reader, unsigned char *output,
                              unsigned int wanted, unsigned int *copied,
                              const char *title, unsigned int frame_index,
                              int *playing, int *paused, int audio_channel,
                              unsigned int *volume) {
    while (*playing && *copied < wanted && pmMainLoop()) {
        scanKeys();
        if (update_player_controls(keysDown(), playing, paused,
                                   audio_channel, volume)) {
            gui_draw_player(title, frame_index, *paused, *volume, wifi_strength);
        }
        if (!*playing) {
            return 0;
        }
        if (*paused) {
            swiWaitForVBlank();
            if (refresh_wifi_strength()) {
                gui_draw_player(title, frame_index, *paused, *volume, wifi_strength);
            }
            continue;
        }

        int result = reader_read_nonblocking(reader, output, wanted, copied);
        if (result < 0) {
            return 0;
        }
        if (result == 0) {
            swiWaitForVBlank();
            if (refresh_wifi_strength()) {
                gui_draw_player(title, frame_index, *paused, *volume, wifi_strength);
            }
        }
    }
    return *copied == wanted;
}

static void play_selected_video(void) {
    if (selected_video >= video_count) {
        snprintf(status_text, sizeof(status_text), "Choose a result first.");
        return;
    }

    char resource[64];
    char headers[512];
    snprintf(resource, sizeof(resource), "/video?id=%s", video_ids[selected_video]);
    int socket_fd = connect_relay(resource);
    if (socket_fd < 0) {
        return;
    }
    int receive_buffer_size = VIDEO_RECV_BUFFER_SIZE;
    setsockopt(socket_fd, SOL_SOCKET, SO_RCVBUF, &receive_buffer_size,
               (socklen_t)sizeof(receive_buffer_size));

    SocketReader reader = {socket_fd, {0}, 0, 0};
    if (!reader_read_headers(&reader, headers, sizeof(headers)) || strstr(headers, " 200 ") == NULL) {
        snprintf(status_text, sizeof(status_text), "Video unavailable from relay.");
        closesocket(socket_fd);
        return;
    }
    int nonblocking = 1;
    if (ioctl(socket_fd, FIONBIO, &nonblocking) < 0) {
        snprintf(status_text, sizeof(status_text), "Could not enable responsive video reads.");
        closesocket(socket_fd);
        return;
    }

    soundEnable();
    videoSetMode(MODE_FB0);
    vramSetBankA(VRAM_A_LCD);
    unsigned int frame_ticks = 60 / VIDEO_FPS;
    unsigned int frame_index = 0;
    int audio_channel = -1;
    int playing = 1;
    int paused = 0;
    unsigned int volume = 96;
    wifi_strength = read_wifi_strength();
    gui_draw_player(video_titles[selected_video], frame_index, paused, volume, wifi_strength);
    while (playing && pmMainLoop()) {
        scanKeys();
        if (update_player_controls(keysDown(), &playing, &paused, audio_channel, &volume)) {
            gui_draw_player(video_titles[selected_video], frame_index, paused, volume, wifi_strength);
        }
        if (!playing) {
            break;
        }
        unsigned int frame_bytes = 0;
        if (!read_video_payload(&reader, video_frame, sizeof(video_frame), &frame_bytes,
                                video_titles[selected_video], frame_index, &playing,
                                &paused, audio_channel, &volume)) {
            break;
        }
        unsigned int sample_start = frame_index * AUDIO_RATE / VIDEO_FPS;
        unsigned int sample_end = (frame_index + 1) * AUDIO_RATE / VIDEO_FPS;
        unsigned int sample_count = sample_end - sample_start;
        unsigned int audio_bytes = 0;
        if (!read_video_payload(&reader, (unsigned char *)audio_block, sample_count,
                                &audio_bytes, video_titles[selected_video], frame_index,
                                &playing, &paused, audio_channel, &volume)) {
            break;
        }
        if (audio_channel >= 0) {
            soundKill(audio_channel);
        }
        audio_channel = soundPlaySample(audio_block, SoundFormat_8Bit, sample_count,
                                        AUDIO_RATE, (u8)volume, 64, false, 0);
        gui_draw_video_frame(video_frame);

        for (unsigned int tick = 0; tick < frame_ticks; tick++) {
            swiWaitForVBlank();
            if (refresh_wifi_strength()) {
                gui_draw_player(video_titles[selected_video], frame_index, paused,
                                volume, wifi_strength);
            }
            if (!pmMainLoop()) {
                playing = 0;
                break;
            }
            scanKeys();
            if (update_player_controls(keysDown(), &playing, &paused,
                                       audio_channel, &volume)) {
                gui_draw_player(video_titles[selected_video], frame_index, paused,
                                volume, wifi_strength);
            }
            if (!playing) {
                break;
            }
            while (paused && playing && pmMainLoop()) {
                swiWaitForVBlank();
                scanKeys();
                if (update_player_controls(keysDown(), &playing, &paused,
                                           audio_channel, &volume)) {
                    gui_draw_player(video_titles[selected_video], frame_index, paused,
                                    volume, wifi_strength);
                }
            }
            if (!playing) {
                playing = 0;
                break;
            }
        }
        frame_index++;
        if (frame_index % VIDEO_FPS == 0) {
            gui_draw_player(video_titles[selected_video], frame_index, paused,
                            volume, wifi_strength);
        }
    }
    soundKill(audio_channel);
    soundDisable();
    closesocket(socket_fd);
    snprintf(status_text, sizeof(status_text), "Playback stopped. Select a title and press X.");
}

int main(void) {
    gui_init();
    if (isDSiMode()) {
        setCpuClock(true);
        snprintf(status_text, sizeof(status_text), "DSi mode: 134 MHz ARM9 and expanded RAM.");
    } else {
        snprintf(status_text, sizeof(status_text), "DS mode: standard ARM9 clock and memory.");
    }
    int gui_dirty = 1;
    int keyboard_visible = 0;
    wifi_strength = read_wifi_strength();
    while (pmMainLoop()) {
        scanKeys();
        int pressed = keysDown();

        if (pressed & KEY_UP) {
            alphabet_index = (alphabet_index + sizeof(alphabet) - 2) % (sizeof(alphabet) - 1);
            gui_dirty = 1;
        }
        if (pressed & KEY_DOWN) {
            alphabet_index = (alphabet_index + 1) % (sizeof(alphabet) - 1);
            gui_dirty = 1;
        }
        if ((pressed & KEY_A) && query_length + 1 < sizeof(query)) {
            query[query_length++] = alphabet[alphabet_index];
            query[query_length] = '\0';
            gui_dirty = 1;
        }
        if ((pressed & KEY_B) && query_length > 0) {
            query[--query_length] = '\0';
            gui_dirty = 1;
        }
        if ((pressed & KEY_LEFT) && video_count > 0 && selected_video > 0) {
            selected_video--;
            gui_dirty = 1;
        }
        if ((pressed & KEY_RIGHT) && video_count > 0 && selected_video + 1 < video_count) {
            selected_video++;
            gui_dirty = 1;
        }
        if (pressed & KEY_START) {
            perform_search();
            gui_dirty = 1;
        }
        if (pressed & KEY_X) {
            play_selected_video();
            gui_dirty = 1;
        }
        if (pressed & KEY_TOUCH) {
            touchPosition touch;
            touchRead(&touch);
            unsigned int touched_result = selected_video;
            char touched_character = '\0';
            GuiTouchAction action = gui_touch_action(touch.px, touch.py,
                                                     selected_video, video_count,
                                                     keyboard_visible, &touched_result,
                                                     &touched_character);
            switch (action) {
            case GUI_TOUCH_SEARCH:
                perform_search();
                keyboard_visible = 0;
                gui_dirty = 1;
                break;
            case GUI_TOUCH_KEYBOARD:
                keyboard_visible = !keyboard_visible;
                gui_dirty = 1;
                break;
            case GUI_TOUCH_KEY:
            case GUI_TOUCH_SPACE:
                if (query_length + 1 < sizeof(query)) {
                    query[query_length++] = action == GUI_TOUCH_SPACE ? ' ' : touched_character;
                    query[query_length] = '\0';
                    gui_dirty = 1;
                }
                break;
            case GUI_TOUCH_BACKSPACE:
                if (query_length > 0) {
                    query[--query_length] = '\0';
                    gui_dirty = 1;
                }
                break;
            case GUI_TOUCH_DONE:
                keyboard_visible = 0;
                gui_dirty = 1;
                break;
            case GUI_TOUCH_CHARACTER:
                alphabet_index = (alphabet_index + 1) % (sizeof(alphabet) - 1);
                gui_dirty = 1;
                break;
            case GUI_TOUCH_ADD:
                if (query_length + 1 < sizeof(query)) {
                    query[query_length++] = alphabet[alphabet_index];
                    query[query_length] = '\0';
                    gui_dirty = 1;
                }
                break;
            case GUI_TOUCH_DELETE:
                if (query_length > 0) {
                    query[--query_length] = '\0';
                    gui_dirty = 1;
                }
                break;
            case GUI_TOUCH_WATCH:
                play_selected_video();
                gui_dirty = 1;
                break;
            case GUI_TOUCH_RESULT:
                selected_video = touched_result;
                gui_dirty = 1;
                break;
            case GUI_TOUCH_NONE:
                break;
            }
        }

        if (gui_dirty) {
            gui_draw(query, alphabet[alphabet_index], status_text, video_titles,
                     video_count, selected_video, wifi_strength, keyboard_visible);
            gui_dirty = 0;
        }
        swiWaitForVBlank();
        if (refresh_wifi_strength()) {
            gui_dirty = 1;
        }
    }
    return 0;
}