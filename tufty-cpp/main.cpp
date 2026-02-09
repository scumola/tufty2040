/**
 * Tufty 2040 Badge - C++ Version
 *
 * Features:
 * - PNG slideshow from LittleFS flash filesystem
 * - Name badge display
 * - Game of Life with differential rendering
 *
 * Buttons:
 * - A: Skip to next image
 * - B: Show name badge for 60 seconds
 * - C: Enter/exit Game of Life mode
 */

#include "pico/stdlib.h"
#include <stdio.h>
#include <cstring>
#include <string>
#include <algorithm>
#include "pico/time.h"
#include "pico/platform.h"
#include "hardware/gpio.h"
#include "hardware/regs/addressmap.h"

#include "common/pimoroni_common.hpp"
#include "drivers/st7789/st7789.hpp"
#include "libraries/pico_graphics/pico_graphics.hpp"
#include "tufty2040.hpp"
#include "PNGdec.h"

// LittleFS filesystem
extern "C" {
#include "pico_hal.h"
}

using namespace pimoroni;

// Hardware setup
Tufty2040 tufty;

ST7789 st7789(
    Tufty2040::WIDTH,
    Tufty2040::HEIGHT,
    ROTATE_180,
    ParallelPins{
        Tufty2040::LCD_CS,
        Tufty2040::LCD_DC,
        Tufty2040::LCD_WR,
        Tufty2040::LCD_RD,
        Tufty2040::LCD_D0,
        Tufty2040::BACKLIGHT
    }
);

// Use RGB565 for better color quality (16-bit color)
PicoGraphics_PenRGB565 graphics(st7789.width, st7789.height, nullptr);

// PNG decoder
PNG png;

// Button pins
#define BUTTON_A    Tufty2040::A     // GPIO 7
#define BUTTON_B    Tufty2040::B     // GPIO 8
#define BUTTON_C    Tufty2040::C     // GPIO 9
#define BUTTON_UP   Tufty2040::UP    // GPIO 22
#define BUTTON_DOWN Tufty2040::DOWN  // GPIO 6

// Game of Life constants - 106x80 grid with 3x3 pixel cells
constexpr int LIFE_X = 106;
constexpr int LIFE_Y = 80;
constexpr int LIFE_SIZE = 3;
constexpr int LIFE_FRAMES = 500;
constexpr int INITIAL_DOTS = 2000;

// Double-buffered grids for Game of Life
uint8_t lifegrid[2][LIFE_X * LIFE_Y];
uint8_t change_mask[LIFE_X * LIFE_Y];

// Colors
Pen WHITE, BLACK, RED, GREEN, BLUE, YELLOW, CYAN, MAGENTA;

// Filesystem state
bool fs_mounted = false;
int image_count = 0;

// Image list - stores filenames found in pics/
#define MAX_IMAGES 200
char image_list[MAX_IMAGES][32];  // Store up to 200 filenames, 32 chars each

// Random seed
uint32_t rand_seed = 12345;

uint32_t fast_rand() {
    rand_seed = rand_seed * 1103515245 + 12345;
    return (rand_seed >> 16) & 0x7FFF;
}

// Initialize button GPIOs
void init_buttons() {
    gpio_init(BUTTON_A);
    gpio_set_dir(BUTTON_A, GPIO_IN);
    gpio_pull_down(BUTTON_A);

    gpio_init(BUTTON_B);
    gpio_set_dir(BUTTON_B, GPIO_IN);
    gpio_pull_down(BUTTON_B);

    gpio_init(BUTTON_C);
    gpio_set_dir(BUTTON_C, GPIO_IN);
    gpio_pull_down(BUTTON_C);

    gpio_init(BUTTON_UP);
    gpio_set_dir(BUTTON_UP, GPIO_IN);
    gpio_pull_down(BUTTON_UP);

    gpio_init(BUTTON_DOWN);
    gpio_set_dir(BUTTON_DOWN, GPIO_IN);
    gpio_pull_down(BUTTON_DOWN);
}

bool button_pressed(uint gpio) {
    return gpio_get(gpio);
}

using pimoroni::millis;

// ============================================================================
// PNG Decoder callbacks for LittleFS
// ============================================================================

struct PNGFileHandle {
    int file;
    int32_t size;
};

void* png_open_callback(const char* filename, int32_t* size) {
    int file = pico_open(filename, LFS_O_RDONLY);
    if (file < 0) {
        printf("PNG: Failed to open %s\n", filename);
        return nullptr;
    }

    PNGFileHandle* handle = new PNGFileHandle;
    handle->file = file;
    handle->size = pico_size(file);
    *size = handle->size;

    printf("PNG: Opened %s, size=%ld\n", filename, handle->size);
    return handle;
}

void png_close_callback(void* pHandle) {
    PNGFileHandle* handle = (PNGFileHandle*)pHandle;
    pico_close(handle->file);
    delete handle;
}

int32_t png_read_callback(PNGFILE* pFile, uint8_t* pBuf, int32_t iLen) {
    PNGFileHandle* handle = (PNGFileHandle*)pFile->fHandle;
    return pico_read(handle->file, pBuf, iLen);
}

int32_t png_seek_callback(PNGFILE* pFile, int32_t iPosition) {
    PNGFileHandle* handle = (PNGFileHandle*)pFile->fHandle;
    return pico_lseek(handle->file, iPosition, LFS_SEEK_SET) >= 0 ? 1 : 0;
}

// PNG draw callback - renders directly to the framebuffer
void png_draw_callback(PNGDRAW* pDraw) {
    uint16_t lineBuffer[320];

    // Convert the PNG line to RGB565 - use BIG_ENDIAN for ST7789
    png.getLineAsRGB565(pDraw, lineBuffer, PNG_RGB565_BIG_ENDIAN, 0xffffffff);

    // Get pointer to graphics framebuffer
    uint16_t* fb = (uint16_t*)graphics.frame_buffer;

    // Copy line to framebuffer
    int y = pDraw->y;
    if (y < 240) {
        memcpy(&fb[y * 320], lineBuffer, 320 * 2);
    }
}

// ============================================================================
// PNG Loading
// ============================================================================

bool load_png(const char* filename) {
    if (!fs_mounted) {
        printf("Filesystem not mounted\n");
        return false;
    }

    int result = png.open(filename, png_open_callback, png_close_callback,
                          png_read_callback, png_seek_callback, png_draw_callback);

    if (result != PNG_SUCCESS) {
        printf("PNG: Failed to open %s, error=%d\n", filename, result);
        return false;
    }

    printf("PNG: %dx%d, bpp=%d\n", png.getWidth(), png.getHeight(), png.getBpp());

    // Decode the image
    result = png.decode(nullptr, 0);
    png.close();

    if (result != PNG_SUCCESS) {
        printf("PNG: Decode failed, error=%d\n", result);
        return false;
    }

    return true;
}

// Scan pics/ directory for PNG files (excluding tufty-name.png)
int scan_images() {
    int count = 0;
    struct lfs_info info;

    int dir = pico_dir_open("pics");
    if (dir < 0) {
        printf("Failed to open pics/ directory\n");
        return 0;
    }

    while (pico_dir_read(dir, &info) > 0 && count < MAX_IMAGES) {
        // Skip . and ..
        if (info.name[0] == '.') continue;

        // Skip directories
        if (info.type == LFS_TYPE_DIR) continue;

        // Check if it's a PNG file (ends with .png)
        int len = strlen(info.name);
        if (len < 5) continue;
        if (strcasecmp(&info.name[len-4], ".png") != 0) continue;

        // Skip tufty-name.png (name badge)
        if (strcasecmp(info.name, "tufty-name.png") == 0) continue;

        // Add to image list
        strncpy(image_list[count], info.name, 31);
        image_list[count][31] = '\0';
        printf("  Found: %s (%ld bytes)\n", info.name, info.size);
        count++;
    }

    pico_dir_close(dir);
    return count;
}

// ============================================================================
// Game of Life
// ============================================================================

void calculate_generation(int fnow, int fnext) {
    uint8_t* grid_now = lifegrid[fnow];
    uint8_t* grid_next = lifegrid[fnext];

    for (int x = 1; x < LIFE_X - 1; x++) {
        int idx = x * LIFE_Y;
        for (int y = 1; y < LIFE_Y - 1; y++) {
            int idx_curr = idx + y;
            int idx_up = idx_curr - LIFE_Y;
            int idx_down = idx_curr + LIFE_Y;

            int neighbors = 0;
            if (grid_now[idx_up - 1] == 1) neighbors++;
            if (grid_now[idx_up] == 1) neighbors++;
            if (grid_now[idx_up + 1] == 1) neighbors++;
            if (grid_now[idx_curr - 1] == 1) neighbors++;
            if (grid_now[idx_curr + 1] == 1) neighbors++;
            if (grid_now[idx_down - 1] == 1) neighbors++;
            if (grid_now[idx_down] == 1) neighbors++;
            if (grid_now[idx_down + 1] == 1) neighbors++;

            if (grid_now[idx_curr] == 1) {
                grid_next[idx_curr] = (neighbors >= 2 && neighbors <= 3) ? 1 : 2;
            } else {
                grid_next[idx_curr] = (neighbors == 3) ? 1 : 0;
            }
        }
    }
}

void mark_changes(int fnow, int fnext) {
    uint8_t* grid_now = lifegrid[fnow];
    uint8_t* grid_next = lifegrid[fnext];

    for (int i = 0; i < LIFE_X * LIFE_Y; i++) {
        change_mask[i] = (grid_now[i] != grid_next[i]) ? grid_next[i] : 255;
    }
}

void draw_changes() {
    for (int x = 0; x < LIFE_X; x++) {
        for (int y = 0; y < LIFE_Y; y++) {
            uint8_t val = change_mask[x * LIFE_Y + y];
            if (val != 255) {
                if (val == 1) graphics.set_pen(WHITE);
                else if (val == 2) graphics.set_pen(RED);
                else graphics.set_pen(BLACK);
                graphics.rectangle(Rect(x * LIFE_SIZE, y * LIFE_SIZE, LIFE_SIZE, LIFE_SIZE));
            }
        }
    }
}

void init_life_grid() {
    memset(lifegrid[0], 0, LIFE_X * LIFE_Y);
    memset(lifegrid[1], 0, LIFE_X * LIFE_Y);
    rand_seed = millis();

    for (int i = 0; i < INITIAL_DOTS; i++) {
        int x = 1 + (fast_rand() % (LIFE_X - 2));
        int y = 1 + (fast_rand() % (LIFE_Y - 2));
        lifegrid[0][x * LIFE_Y + y] = 1;
    }
}

void draw_full_life_grid(int fnow) {
    graphics.set_pen(BLACK);
    graphics.clear();

    uint8_t* grid = lifegrid[fnow];
    for (int x = 0; x < LIFE_X; x++) {
        for (int y = 0; y < LIFE_Y; y++) {
            int idx = x * LIFE_Y + y;
            if (grid[idx] == 1) {
                graphics.set_pen(WHITE);
                graphics.rectangle(Rect(x * LIFE_SIZE, y * LIFE_SIZE, LIFE_SIZE, LIFE_SIZE));
            } else if (grid[idx] == 2) {
                graphics.set_pen(RED);
                graphics.rectangle(Rect(x * LIFE_SIZE, y * LIFE_SIZE, LIFE_SIZE, LIFE_SIZE));
            }
        }
    }
}

void run_game_of_life() {
    init_life_grid();
    int frames = 0, fnow = 0, fnext = 1;

    draw_full_life_grid(fnow);
    st7789.update(&graphics);

    uint32_t total_calc = 0, total_draw = 0, total_update = 0;
    uint32_t frame_start = millis();

    while (frames < LIFE_FRAMES) {
        uint32_t t0 = millis();
        calculate_generation(fnow, fnext);
        uint32_t t1 = millis();
        total_calc += (t1 - t0);

        mark_changes(fnow, fnext);
        draw_changes();
        uint32_t t2 = millis();
        total_draw += (t2 - t1);

        st7789.update(&graphics);
        uint32_t t3 = millis();
        total_update += (t3 - t2);

        fnow = fnext;
        fnext = 1 - fnext;
        frames++;

        if (frames % 50 == 0) {
            uint32_t elapsed = millis() - frame_start;
            float fps = 50.0f * 1000.0f / (float)elapsed;
            printf("Frame %d: calc=%lums draw=%lums update=%lums FPS=%.1f\n",
                   frames, total_calc, total_draw, total_update, fps);
            total_calc = total_draw = total_update = 0;
            frame_start = millis();
        }

        if (button_pressed(BUTTON_C)) {
            sleep_ms(200);
            break;
        }
    }
}

// ============================================================================
// Fallback Pattern Drawing (when no filesystem/images)
// ============================================================================

void draw_pattern(int pattern_num) {
    int style = pattern_num % 6;

    switch (style) {
        case 0:  // Gradient
            for (int y = 0; y < 240; y++) {
                uint8_t r = (y * 255) / 240;
                uint8_t g = ((pattern_num * 37) + y) % 255;
                uint8_t b = 255 - r;
                graphics.set_pen(graphics.create_pen(r, g, b));
                graphics.rectangle(Rect(0, y, 320, 1));
            }
            break;

        case 1:  // Circles
            graphics.set_pen(graphics.create_pen(20, 20, 60));
            graphics.clear();
            for (int i = 0; i < 8; i++) {
                int x = 40 + (i % 4) * 80;
                int y = 60 + (i / 4) * 120;
                graphics.set_pen(graphics.create_pen((i * 30 + pattern_num * 20) % 255,
                                                     (100 + i * 20) % 255,
                                                     (200 - i * 15) % 255));
                graphics.circle(Point(x, y), 30 + (i * 5));
            }
            break;

        case 2:  // Grid
            for (int x = 0; x < 320; x += 20) {
                for (int y = 0; y < 240; y += 20) {
                    graphics.set_pen(graphics.create_pen((x * y / 100 + pattern_num * 10) % 255,
                                                         (x + pattern_num * 5) % 255,
                                                         (y + pattern_num * 7) % 255));
                    graphics.rectangle(Rect(x + 2, y + 2, 16, 16));
                }
            }
            break;

        case 3:  // Stripes
            for (int x = 0; x < 320; x += 8) {
                graphics.set_pen(graphics.create_pen(((x / 8) * 17 + pattern_num * 30) % 255,
                                                     (128 + pattern_num * 5) % 255,
                                                     (200 - (x / 8) * 5) % 255));
                graphics.rectangle(Rect(x, 0, 8, 240));
            }
            break;

        case 4:  // Radial
            for (int ring = 120; ring > 0; ring -= 8) {
                graphics.set_pen(graphics.create_pen((ring * 2 + pattern_num * 20) % 255,
                                                     (255 - ring * 2 + pattern_num * 10) % 255,
                                                     (128 + pattern_num * 15) % 255));
                graphics.circle(Point(160, 120), ring);
            }
            break;

        case 5:  // Checkerboard
            for (int x = 0; x < 320; x += 32) {
                for (int y = 0; y < 240; y += 32) {
                    bool white = ((x / 32) + (y / 32)) % 2 == 0;
                    if (white) {
                        graphics.set_pen(graphics.create_pen((200 + pattern_num * 3) % 255,
                                                             (180 + pattern_num * 7) % 255,
                                                             (160 + pattern_num * 11) % 255));
                    } else {
                        graphics.set_pen(graphics.create_pen((50 + pattern_num * 5) % 255,
                                                             (30 + pattern_num * 9) % 255,
                                                             (80 + pattern_num * 13) % 255));
                    }
                    graphics.rectangle(Rect(x, y, 32, 32));
                }
            }
            break;
    }

    graphics.set_pen(WHITE);
    char buf[32];
    snprintf(buf, sizeof(buf), "Pattern %d", pattern_num);
    graphics.text(buf, Point(10, 220), 320);
}

// ============================================================================
// Name Badge
// ============================================================================

void draw_name_badge() {
    // Try to load name badge PNG first
    if (fs_mounted && load_png("pics/tufty-name.png")) {
        return;  // Successfully loaded
    }

    // Fallback to drawn badge
    graphics.set_pen(graphics.create_pen(20, 40, 100));
    graphics.clear();

    graphics.set_pen(WHITE);
    graphics.rectangle(Rect(0, 0, 320, 60));

    graphics.set_pen(graphics.create_pen(20, 40, 100));
    graphics.text("HELLO", Point(100, 5), 320, 2.0f);
    graphics.text("my name is", Point(90, 35), 320, 1.0f);

    graphics.set_pen(WHITE);
    graphics.rectangle(Rect(10, 70, 300, 120));

    graphics.set_pen(BLACK);
    graphics.text("Steve", Point(80, 100), 320, 4.0f);

    graphics.set_pen(graphics.create_pen(200, 50, 50));
    graphics.rectangle(Rect(0, 195, 320, 45));

    graphics.set_pen(WHITE);
    graphics.text("Tufty 2040 Badge", Point(70, 210), 320, 1.5f);
}

// ============================================================================
// Main
// ============================================================================

int main() {
    stdio_init_all();
    init_buttons();

    // Initialize display early and clear to black
    st7789.set_backlight(200);

    // Create pens early
    WHITE = graphics.create_pen(255, 255, 255);
    BLACK = graphics.create_pen(0, 0, 0);
    RED = graphics.create_pen(255, 0, 0);
    GREEN = graphics.create_pen(0, 255, 0);
    BLUE = graphics.create_pen(0, 0, 255);
    YELLOW = graphics.create_pen(255, 255, 0);
    CYAN = graphics.create_pen(0, 255, 255);
    MAGENTA = graphics.create_pen(255, 0, 255);

    // Clear screen immediately to show we're alive
    graphics.set_pen(BLACK);
    graphics.clear();
    graphics.set_pen(WHITE);

    // Show flash size on screen for debugging
    char debug_msg[64];
    snprintf(debug_msg, sizeof(debug_msg), "Flash: %d MB", PICO_FLASH_SIZE_BYTES / 1024 / 1024);
    graphics.text(debug_msg, Point(80, 100), 320, 2.0f);
    graphics.text("Booting...", Point(100, 130), 320, 2.0f);
    st7789.update(&graphics);

    // Wait for USB serial to enumerate
    sleep_ms(2000);

    printf("\n\nTufty 2040 Badge - C++ Version\n");
    printf("Buttons: A=next, B=name badge, C=Game of Life\n");
    printf("Flash size: %d MB\n", PICO_FLASH_SIZE_BYTES / 1024 / 1024);

    // Mount filesystem
    printf("Mounting filesystem...\n");
    int mount_result = pico_mount(false);
    printf("Mount result: %d\n", mount_result);

    // If mount failed, try formatting and remounting
    if (mount_result != LFS_ERR_OK) {
        printf("Mount failed, formatting filesystem...\n");
        mount_result = pico_mount(true);  // true = format first
        printf("Format + mount result: %d\n", mount_result);
    }

    // Show mount result on screen
    graphics.set_pen(BLACK);
    graphics.clear();
    graphics.set_pen(WHITE);
    snprintf(debug_msg, sizeof(debug_msg), "Flash: %d MB", PICO_FLASH_SIZE_BYTES / 1024 / 1024);
    graphics.text(debug_msg, Point(80, 80), 320, 2.0f);
    snprintf(debug_msg, sizeof(debug_msg), "Mount: %d", mount_result);
    graphics.text(debug_msg, Point(80, 110), 320, 2.0f);
    st7789.update(&graphics);
    sleep_ms(3000);  // Show for 3 seconds

    if (mount_result == LFS_ERR_OK) {
        fs_mounted = true;
        printf("Filesystem mounted successfully\n");

        // Show filesystem stats
        struct pico_fsstat_t stat;
        if (pico_fsstat(&stat) == LFS_ERR_OK) {
            printf("FS: %d blocks, %d bytes/block, %d used\n",
                   (int)stat.block_count, (int)stat.block_size, (int)stat.blocks_used);
        }

        // Scan for available images
        printf("Scanning for images...\n");
        image_count = scan_images();
        printf("Found %d images in pics/\n", image_count);
    } else {
        printf("Filesystem mount failed - using patterns\n");
        fs_mounted = false;
    }

    rand_seed = millis();
    int image_index = 0;

    // Debug: pointer to filesystem area
    const uint8_t* fs_flash = (const uint8_t*)(XIP_NOCACHE_NOALLOC_BASE + PICO_FLASH_SIZE_BYTES - (2 * 1024 * 1024));

    // Main loop
    while (true) {
        printf("--- Flash: %dMB, FS: %s, Images: %d, Showing: %d ---\n",
               PICO_FLASH_SIZE_BYTES / 1024 / 1024,
               fs_mounted ? "mounted" : "not mounted",
               image_count, image_index);
        printf("FS@0x%08X: %02X %02X %02X %02X %02X %02X %02X %02X\n",
               (unsigned int)fs_flash,
               fs_flash[0], fs_flash[1], fs_flash[2], fs_flash[3],
               fs_flash[4], fs_flash[5], fs_flash[6], fs_flash[7]);

        tufty.led(128);

        bool loaded = false;
        if (fs_mounted && image_count > 0) {
            char filename[64];
            snprintf(filename, sizeof(filename), "pics/%s", image_list[image_index]);
            printf("Loading: %s\n", filename);
            loaded = load_png(filename);
        }

        if (!loaded) {
            draw_pattern(image_index);
        }

        st7789.update(&graphics);
        tufty.led(0);

        uint32_t start_time = millis();
        const uint32_t display_time = 15000;

        while (millis() - start_time < display_time) {
            sleep_ms(100);

            if (button_pressed(BUTTON_A)) {
                sleep_ms(200);
                break;
            }

            if (button_pressed(BUTTON_B)) {
                tufty.led(128);
                draw_name_badge();
                st7789.update(&graphics);
                tufty.led(0);

                uint32_t badge_start = millis();
                while (millis() - badge_start < 60000) {
                    sleep_ms(100);
                    if (button_pressed(BUTTON_A) || button_pressed(BUTTON_B) || button_pressed(BUTTON_C)) {
                        sleep_ms(200);
                        break;
                    }
                }
                break;
            }

            if (button_pressed(BUTTON_C)) {
                sleep_ms(200);
                run_game_of_life();
                break;
            }
        }

        // Pick random next image
        if (image_count > 1) {
            int new_index;
            do {
                new_index = fast_rand() % image_count;
            } while (new_index == image_index);  // Avoid showing same image twice
            image_index = new_index;
        } else if (image_count == 0) {
            // No images found, pick random pattern
            image_index = fast_rand() % 72;
        }
    }

    return 0;
}
