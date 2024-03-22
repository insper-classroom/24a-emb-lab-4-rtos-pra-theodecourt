#include <FreeRTOS.h>
#include <task.h>
#include <semphr.h>
#include <queue.h>

#include "ssd1306.h"
#include "gfx.h"

#include "pico/stdlib.h"
#include <stdio.h>

const uint BTN_1_OLED = 28;
const uint BTN_2_OLED = 26;
const uint BTN_3_OLED = 27;

const uint LED_1_OLED = 20;
const uint LED_2_OLED = 21;
const uint LED_3_OLED = 22;

int TRIG_PIN = 12;  // GPIO pin for the Trigger
int ECHO_PIN = 13;  // GPIO pin for the Echo

QueueHandle_t xQueueTime;
QueueHandle_t xQueueDistance;
SemaphoreHandle_t xSemaphoreTrigger;


void oled1_btn_led_init(void) {
    gpio_init(LED_1_OLED);
    gpio_set_dir(LED_1_OLED, GPIO_OUT);

    gpio_init(LED_2_OLED);
    gpio_set_dir(LED_2_OLED, GPIO_OUT);

    gpio_init(LED_3_OLED);
    gpio_set_dir(LED_3_OLED, GPIO_OUT);

    gpio_init(BTN_1_OLED);
    gpio_set_dir(BTN_1_OLED, GPIO_IN);
    gpio_pull_up(BTN_1_OLED);

    gpio_init(BTN_2_OLED);
    gpio_set_dir(BTN_2_OLED, GPIO_IN);
    gpio_pull_up(BTN_2_OLED);

    gpio_init(BTN_3_OLED);
    gpio_set_dir(BTN_3_OLED, GPIO_IN);
    gpio_pull_up(BTN_3_OLED);
}

void send_pulse() {
    gpio_put(TRIG_PIN, 1);
    sleep_us(10);
    gpio_put(TRIG_PIN, 0);
}

void pin_callback(uint gpio, uint32_t events) {
    static uint32_t start_time, end_time,duration;

    if (gpio == ECHO_PIN) {
        if (gpio_get(ECHO_PIN)) {
            // ECHO_PIN mudou para alto
            start_time = to_us_since_boot(get_absolute_time());
        } else {
            // ECHO_PIN mudou para baixo
            end_time = to_us_since_boot(get_absolute_time());
            duration = end_time - start_time;

            xQueueSendFromISR(xQueueTime, &duration, 0);
        }
    }
}

void trigger_task(void *p) {
    printf("Inicializando Driver\n");
    ssd1306_init();

    printf("Inicializando GLX\n");
    ssd1306_t disp;
    gfx_init(&disp, 128, 32);

    printf("Inicializando btn and LEDs\n");
    oled1_btn_led_init();

    gpio_init(TRIG_PIN);
    gpio_set_dir(TRIG_PIN, GPIO_OUT);
    gpio_put(TRIG_PIN, 0);

    while (1) {
        send_pulse();
        xSemaphoreGive(xSemaphoreTrigger);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void echo_task(void *p) {
    printf("Inicializando Driver\n");
    ssd1306_init();

    printf("Inicializando GLX\n");
    ssd1306_t disp;
    gfx_init(&disp, 128, 32);

    printf("Inicializando btn and LEDs\n");
    oled1_btn_led_init();

    gpio_init(ECHO_PIN);
    gpio_set_dir(ECHO_PIN, GPIO_IN);
    gpio_pull_up(ECHO_PIN);

    uint32_t duration;

    while (1) {
        if (xQueueReceive(xQueueTime, &duration,  pdMS_TO_TICKS(50))) {
            float distance = (float)duration / 58.0;
            xQueueSend(xQueueDistance, &distance, 0);
        }
    }
}

void oled_task(void *p) {
    printf("Inicializando Driver\n");
    ssd1306_init();

    printf("Inicializando GLX\n");
    ssd1306_t disp;
    gfx_init(&disp, 128, 32);

    printf("Inicializando btn and LEDs\n");
    oled1_btn_led_init();

    float distance;
    char distance_str[20];
    int bar_length;
    const char *error_msg = "Erro: Sem leitura";
    TickType_t last_reading_time = 0;
    TickType_t current_time;
    const TickType_t max_time_without_reading = pdMS_TO_TICKS(500); // 500 ms

    while (1) {
        if (xSemaphoreTake(xSemaphoreTrigger, pdMS_TO_TICKS(50)) == pdTRUE) {
            current_time = xTaskGetTickCount();
            if (xQueueReceive(xQueueDistance, &distance, pdMS_TO_TICKS(50))) {
                last_reading_time = current_time;
                if (distance > 200.0) {
                    gfx_clear_buffer(&disp);
                    // Exibe a mensagem de erro se a distância for maior que 200 cm
                    gfx_draw_string(&disp, 0, 0, 1, "Erro: Distancia > 200");
                    gfx_show(&disp);
                    vTaskDelay(pdMS_TO_TICKS(150)); 
                } else {
                    gfx_clear_buffer(&disp);
                    snprintf(distance_str, sizeof(distance_str), "%.2f", distance);
                    gfx_draw_string(&disp, 0, 0, 1, distance_str);
                    bar_length = (int)(distance * 128.0 / 200.0);
                    if (bar_length > 128) {
                        bar_length = 128;
                    }

                    // Desenha a barra
                    gfx_draw_line(&disp, 15, 27, bar_length + 15, 27);
                    gfx_show(&disp);
                    vTaskDelay(pdMS_TO_TICKS(50));
                    }
            } else if (current_time - last_reading_time > max_time_without_reading) {
                // Exibe a mensagem de erro se não houver leitura recente
                gfx_clear_buffer(&disp);
                gfx_draw_string(&disp, 0, 0, 1, error_msg);
                gfx_show(&disp);
            }
        } else {
            gfx_clear_buffer(&disp);
            gfx_draw_string(&disp, 0, 0, 1, error_msg);
            gfx_show(&disp);
            vTaskDelay(pdMS_TO_TICKS(150));
        }
    }
}

int main() {
    stdio_init_all();

    gpio_set_irq_enabled_with_callback(ECHO_PIN, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true, &pin_callback);

    xSemaphoreTrigger = xSemaphoreCreateBinary();
    xQueueTime = xQueueCreate(32, sizeof(uint32_t) );
    xQueueDistance = xQueueCreate(32, sizeof(float) );

    xTaskCreate(trigger_task, "Trigger task", 4095, NULL, 1, NULL);
    xTaskCreate(echo_task, "Echo task", 4095, NULL, 1, NULL);
    xTaskCreate(oled_task, "OLED task", 4095, NULL, 1, NULL);

    vTaskStartScheduler();

    while (true)
        ;
}