#include "local_3d_test.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bno085.h"
#include "lcd_board_config.h"
#include "lcd_gpio_writer.h"

#define FRAME_PERIOD_MS 100U
#define FRAME_BYTES ((size_t)LCD_H_RES * LCD_V_RES)
#define NEAR_PLANE 0.20f
#define FOCAL_LENGTH 430.0f

static const char *TAG = "local_3d";

typedef struct {
    float x;
    float y;
    float z;
} vec3_t;

typedef struct {
    float w;
    float x;
    float y;
    float z;
} quat_t;

typedef struct {
    int x;
    int y;
    float z;
} projected_t;

static uint8_t rgb332(uint8_t red, uint8_t green, uint8_t blue)
{
    return (uint8_t)((red & 0xe0U) | ((green >> 3) & 0x1cU) | (blue >> 6));
}

static quat_t quat_conjugate(quat_t q)
{
    return (quat_t){q.w, -q.x, -q.y, -q.z};
}

static quat_t quat_multiply(quat_t a, quat_t b)
{
    return (quat_t){
        .w = a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
        .x = a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        .y = a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        .z = a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
    };
}

static quat_t quat_normalize(quat_t q)
{
    const float norm = sqrtf(q.w * q.w + q.x * q.x +
                             q.y * q.y + q.z * q.z);
    if (norm < 0.001f) {
        return (quat_t){1.0f, 0.0f, 0.0f, 0.0f};
    }
    const float inverse = 1.0f / norm;
    q.w *= inverse;
    q.x *= inverse;
    q.y *= inverse;
    q.z *= inverse;
    return q;
}

static vec3_t quat_rotate(quat_t q, vec3_t v)
{
    const vec3_t t = {
        2.0f * (q.y * v.z - q.z * v.y),
        2.0f * (q.z * v.x - q.x * v.z),
        2.0f * (q.x * v.y - q.y * v.x),
    };
    return (vec3_t){
        v.x + q.w * t.x + (q.y * t.z - q.z * t.y),
        v.y + q.w * t.y + (q.z * t.x - q.x * t.z),
        v.z + q.w * t.z + (q.x * t.y - q.y * t.x),
    };
}

static void put_pixel(uint8_t *frame, int x, int y, uint8_t color,
                      unsigned thickness)
{
    const int radius = (int)thickness / 2;
    for (int dy = -radius; dy <= radius; ++dy) {
        const int py = y + dy;
        if (py < 0 || py >= LCD_V_RES) {
            continue;
        }
        for (int dx = -radius; dx <= radius; ++dx) {
            const int px = x + dx;
            if (px >= 0 && px < LCD_H_RES) {
                frame[(size_t)py * LCD_H_RES + (size_t)px] = color;
            }
        }
    }
}

static void draw_line(uint8_t *frame, int x0, int y0, int x1, int y1,
                      uint8_t color, unsigned thickness)
{
    /* Keep a near-plane projection from producing an excessively long loop. */
    if (x0 < -1600) x0 = -1600;
    if (x0 > 2400) x0 = 2400;
    if (x1 < -1600) x1 = -1600;
    if (x1 > 2400) x1 = 2400;
    if (y0 < -960) y0 = -960;
    if (y0 > 1440) y0 = 1440;
    if (y1 < -960) y1 = -960;
    if (y1 > 1440) y1 = 1440;

    int dx = abs(x1 - x0);
    int sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0);
    int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;

    for (;;) {
        put_pixel(frame, x0, y0, color, thickness);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        const int doubled = 2 * error;
        if (doubled >= dy) {
            error += dy;
            x0 += sx;
        }
        if (doubled <= dx) {
            error += dx;
            y0 += sy;
        }
    }
}

static vec3_t world_to_camera(vec3_t world, quat_t inverse_view)
{
    /* The virtual camera is 1.6 units above the floor at the origin. */
    world.y -= 1.6f;
    return quat_rotate(inverse_view, world);
}

static bool project(vec3_t camera, projected_t *output)
{
    if (camera.z <= NEAR_PLANE) {
        return false;
    }
    output->x = (int)lroundf((float)LCD_H_RES / 2.0f +
                            FOCAL_LENGTH * camera.x / camera.z);
    output->y = (int)lroundf((float)LCD_V_RES / 2.0f -
                            FOCAL_LENGTH * camera.y / camera.z);
    output->z = camera.z;
    return true;
}

static void draw_world_line(uint8_t *frame, quat_t inverse_view,
                            vec3_t a, vec3_t b, uint8_t color,
                            unsigned thickness)
{
    vec3_t camera_a = world_to_camera(a, inverse_view);
    vec3_t camera_b = world_to_camera(b, inverse_view);
    if (camera_a.z <= NEAR_PLANE && camera_b.z <= NEAR_PLANE) {
        return;
    }

    if (camera_a.z <= NEAR_PLANE || camera_b.z <= NEAR_PLANE) {
        vec3_t *behind = camera_a.z <= NEAR_PLANE ? &camera_a : &camera_b;
        const vec3_t ahead = camera_a.z <= NEAR_PLANE ? camera_b : camera_a;
        const float ratio = (NEAR_PLANE - behind->z) /
                            (ahead.z - behind->z);
        behind->x += (ahead.x - behind->x) * ratio;
        behind->y += (ahead.y - behind->y) * ratio;
        behind->z = NEAR_PLANE;
    }

    projected_t projected_a;
    projected_t projected_b;
    if (project(camera_a, &projected_a) && project(camera_b, &projected_b)) {
        draw_line(frame, projected_a.x, projected_a.y,
                  projected_b.x, projected_b.y, color, thickness);
    }
}

static void draw_cube(uint8_t *frame, quat_t inverse_view, vec3_t center,
                      float size, float height, uint8_t color)
{
    const float half = size / 2.0f;
    vec3_t vertex[8] = {
        {center.x - half, 0.0f, center.z - half},
        {center.x + half, 0.0f, center.z - half},
        {center.x + half, 0.0f, center.z + half},
        {center.x - half, 0.0f, center.z + half},
        {center.x - half, height, center.z - half},
        {center.x + half, height, center.z - half},
        {center.x + half, height, center.z + half},
        {center.x - half, height, center.z + half},
    };
    static const uint8_t edge[12][2] = {
        {0, 1}, {1, 2}, {2, 3}, {3, 0},
        {4, 5}, {5, 6}, {6, 7}, {7, 4},
        {0, 4}, {1, 5}, {2, 6}, {3, 7},
    };
    for (size_t index = 0; index < 12U; ++index) {
        draw_world_line(frame, inverse_view,
                        vertex[edge[index][0]], vertex[edge[index][1]],
                        color, 3U);
    }
}

static void render_scene(uint8_t *frame, quat_t relative)
{
    const uint8_t background = rgb332(10, 18, 38);
    const uint8_t grid_minor = rgb332(40, 75, 90);
    const uint8_t grid_major = rgb332(95, 135, 145);
    memset(frame, background, FRAME_BYTES);

    const quat_t inverse_view = quat_conjugate(relative);
    for (int coordinate = -12; coordinate <= 12; ++coordinate) {
        const uint8_t color = coordinate == 0 ? grid_major : grid_minor;
        draw_world_line(frame, inverse_view,
                        (vec3_t){(float)coordinate, 0.0f, -12.0f},
                        (vec3_t){(float)coordinate, 0.0f, 12.0f}, color, 1U);
        draw_world_line(frame, inverse_view,
                        (vec3_t){-12.0f, 0.0f, (float)coordinate},
                        (vec3_t){12.0f, 0.0f, (float)coordinate}, color, 1U);
    }

    static const struct {
        vec3_t center;
        uint8_t red;
        uint8_t green;
        uint8_t blue;
    } cubes[] = {
        {{ 0.0f, 0.0f,  6.0f}, 245,  65,  65},
        {{ 5.0f, 0.0f,  5.0f}, 245, 155,  45},
        {{ 7.0f, 0.0f,  0.0f}, 235, 225,  55},
        {{ 5.0f, 0.0f, -5.0f},  75, 220,  90},
        {{ 0.0f, 0.0f, -7.0f},  55, 205, 225},
        {{-5.0f, 0.0f, -5.0f},  70, 110, 245},
        {{-7.0f, 0.0f,  0.0f}, 160,  85, 240},
        {{-5.0f, 0.0f,  5.0f}, 235,  75, 180},
    };
    for (size_t index = 0; index < sizeof(cubes) / sizeof(cubes[0]); ++index) {
        draw_cube(frame, inverse_view, cubes[index].center,
                  1.5f, 1.8f + (float)(index % 3U) * 0.7f,
                  rgb332(cubes[index].red, cubes[index].green,
                         cubes[index].blue));
    }

    const uint8_t crosshair = rgb332(255, 255, 255);
    const int center_x = LCD_H_RES / 2;
    const int center_y = LCD_V_RES / 2;
    draw_line(frame, center_x - 14, center_y, center_x + 14, center_y,
              crosshair, 1U);
    draw_line(frame, center_x, center_y - 14, center_x, center_y + 14,
              crosshair, 1U);
}

void local_3d_test_run(void)
{
    uint8_t *frame = heap_caps_aligned_alloc(
        16, FRAME_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    ESP_ERROR_CHECK(frame != NULL ? ESP_OK : ESP_ERR_NO_MEM);

    bool centered = false;
    quat_t center = {1.0f, 0.0f, 0.0f, 0.0f};
    quat_t relative = center;
    uint32_t frame_count = 0;
    uint32_t slow_frames = 0;
    int64_t stats_start_us = esp_timer_get_time();
    TickType_t last_wake = xTaskGetTickCount();
    const TickType_t frame_period = pdMS_TO_TICKS(FRAME_PERIOD_MS);

    ESP_LOGI(TAG, "offline 3D room ready; hold LCD forward for recenter");
    for (;;) {
        const int64_t frame_start_us = esp_timer_get_time();
        bno085_quaternion_t sample;
        if (bno085_get_latest_quaternion(&sample)) {
            const quat_t current = quat_normalize(
                (quat_t){sample.w, sample.x, sample.y, sample.z});
            if (!centered) {
                center = current;
                centered = true;
                ESP_LOGI(TAG, "3D view recentered at BNO seq=%lu",
                         (unsigned long)sample.sequence);
            }
            relative = quat_normalize(
                quat_multiply(quat_conjugate(center), current));
        }

        render_scene(frame, relative);
        const esp_err_t result = lcd_gpio_writer_draw_rgb332(frame, FRAME_BYTES);
        if (result != ESP_OK) {
            ESP_LOGE(TAG, "3D frame failed: %s", esp_err_to_name(result));
            if (result == ESP_ERR_TIMEOUT) {
                vTaskDelay(pdMS_TO_TICKS(100));
                esp_restart();
            }
        }

        const int64_t render_us = esp_timer_get_time() - frame_start_us;
        if (render_us > (int64_t)FRAME_PERIOD_MS * 1000) {
            ++slow_frames;
        }
        xTaskDelayUntil(&last_wake, frame_period);
        ++frame_count;
        if (frame_count == 10U) {
            const int64_t elapsed_us = esp_timer_get_time() - stats_start_us;
            ESP_LOGI(TAG,
                     "3D fps=%.2f slow=%lu/10 qrel=[%.3f %.3f %.3f %.3f]",
                     10000000.0 / (double)elapsed_us,
                     (unsigned long)slow_frames, relative.w, relative.x,
                     relative.y, relative.z);
            frame_count = 0;
            slow_frames = 0;
            stats_start_us = esp_timer_get_time();
        }
    }
}
