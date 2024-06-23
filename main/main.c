#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "sdkconfig.h"
#include "nvs_flash.h"

//custom libs
#include "app_config.h"
#include "web_server.h"
#include "utils.h"
#include "mesh_network.h"


// Pino do LED
#define LED_PIN 2
// Pino do Botao para trocar o modo da aplicacao
#define BUTTON_WIFI_MODE_PIN 5
#define HOLD_TIME_MS 5000

static const char *TAG = "MAIN_APP";
bool press_hold_timeout = false;

// Tarefa para piscar o LED
static void blink_led_task(void *arg) {
    bool led_state = false;
    while (true) {
        gpio_set_level(LED_PIN, led_state);

        if (press_hold_timeout){
			led_state = !led_state;
            // Pisca o LED 5 vezes por segundo indicando ao usuario que ja pode soltar o botao
            vTaskDelay(pdMS_TO_TICKS(200));
        } else if(nvs_get_app_mode() == APP_MODE_WIFI_AP_WEB_SERVER) {
			led_state = !led_state;
            // Pisca o LED a cada 1 segundo se estiver no modo AP
            vTaskDelay(pdMS_TO_TICKS(1000));
        } else if (nvs_get_app_mode() == APP_MODE_WIFI_MESH_NETWORK) {
            // Mantem o LED ligado se estiver no modo STA
            led_state = true;
            vTaskDelay(pdMS_TO_TICKS(1000));
        } else {
			led_state = !led_state;
            // Pisca o LED 10 vezes por segundo indicando que esta desconectado o wifi
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}

// Tarefa para verificar botoes
static void check_buttons_task(void *arg) {
    static uint32_t press_start_time = 0;

    while (true) {		

        //Verifica botao para trocar o modo do wifi entre ponto de acesso (AP) e estação (STA)
        if (gpio_get_level(BUTTON_WIFI_MODE_PIN) == 0) { //se pressionado
            if(press_hold_timeout == false) {
                if (press_start_time == 0) {
                    press_start_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
                } else if ((xTaskGetTickCount() * portTICK_PERIOD_MS - press_start_time) >= HOLD_TIME_MS) {
                    //Ja segurou o botao por tempo suficiente,
                    //a task blink_led_task vai fazer o led piscar 10x por segundo, indicando que ja podemos soltar o botao.
                    press_hold_timeout = true;
                }
            }
        } else {
            press_start_time = 0;

            if(press_hold_timeout) {
                //Solicitado troca do modo WiFi
                press_hold_timeout = false;
                if(nvs_get_app_mode() == APP_MODE_WIFI_AP_WEB_SERVER)
                    nvs_set_app_mode(APP_MODE_WIFI_MESH_NETWORK);
                else 
                    nvs_set_app_mode(APP_MODE_WIFI_AP_WEB_SERVER);
                
            }
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void init_IOs() {
    // Configura o pino do LED como saída
    gpio_set_direction(LED_PIN, GPIO_MODE_OUTPUT);

    // Configura o pino do botão como entrada
    gpio_set_direction(BUTTON_WIFI_MODE_PIN, GPIO_MODE_INPUT);
    gpio_set_pull_mode(BUTTON_WIFI_MODE_PIN, GPIO_PULLUP_ONLY);
    gpio_set_intr_type(BUTTON_WIFI_MODE_PIN, GPIO_INTR_ANYEDGE);
}

// Função principal do aplicativo
void app_main(void) {
	//nvs_flash_erase();
    
    ESP_LOGI(TAG, "App Version: 5");
	
	print_chip_info();

    init_IOs();

    // Inicializa o armazenamento não volátil (NVS)
    nvs_flash_init();

    APP_MODE_t app_mode = nvs_get_app_mode();
    switch(app_mode)
    {
        case APP_MODE_WIFI_AP_WEB_SERVER:
        {
            // Inicializa WiFi como Ponto de Acesso e sobe Webserver
            start_webserver();
        }
        break;
        case APP_MODE_WIFI_MESH_NETWORK:
        {
            // Inicializa aplicacao com rede mesh
            start_mesh();
        }
        break;
    }

    // Cria a tarefa para piscar o LED
    xTaskCreate(blink_led_task, "blink_led_task", 2048, NULL, 5, NULL);
    // Cria a tarefa para checar botoes
    xTaskCreate(check_buttons_task, "check_buttons_task", 4096, NULL, 5, NULL);
}