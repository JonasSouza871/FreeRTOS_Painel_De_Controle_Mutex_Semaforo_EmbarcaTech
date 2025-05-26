#include <stdio.h>
#include "pico/stdlib.h"
#include "FreeRTOS.h" 
#include "task.h"
#include "semphr.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "lib/Display_Bibliotecas/ssd1306.h"
#include "lib/Matriz_Bibliotecas/matriz_led.h"

// ===== DEFINIÇÕES =====
#define BOTAO_A_GPIO 5      
#define BOTAO_B_GPIO 6      
#define JOYSTICK_GPIO 22    
#define MAX_USUARIOS 10     

// Configurações I2C e Display
#define I2C_PORT i2c1
#define I2C_SDA 14
#define I2C_SCL 15
#define DISPLAY_ADDRESS 0x3C
#define DISPLAY_WIDTH 128
#define DISPLAY_HEIGHT 64

// LEDs RGB
#define LED_VERDE_GPIO 11   
#define LED_AZUL_GPIO 12    
#define LED_VERMELHO_GPIO 13 

// ===== VARIÁVEIS GLOBAIS =====
volatile int usuarios_ativos = 0;
volatile int contador_resets = 0;
volatile bool mostrar_resetado = false;
volatile bool tela_atual_stats = true; 

SemaphoreHandle_t xMutexUsuarios;
SemaphoreHandle_t xMutexDisplay;
SemaphoreHandle_t xResetSem;
SemaphoreHandle_t xSemaphoreContagem;

// Display
ssd1306_t display;

// Variáveis para debounce da interrupção
volatile uint32_t last_interrupt_time = 0;
const uint32_t debounce_delay_ms = 400;

// ===== FUNÇÃO PARA OBTER COR DA MATRIZ LED BASEADA NO NÚMERO =====
uint32_t obter_cor_numero(int numero) {
    const uint32_t cores_numeros[] = {
        COR_AZUL,      
        COR_VERDE,     
        COR_LARANJA,   
        COR_VIOLETA,   
        COR_OURO,      
        COR_PRATA,     
        COR_MARROM,    
        COR_BRANCO,    
        COR_CINZA,     
        COR_AMARELO    
    };
    
    if (numero >= 0 && numero <= 9) {
        return cores_numeros[numero];
    }
    return COR_VERMELHO; 
}

// ===== FUNÇÃO PARA ATUALIZAR MATRIZ LED =====
void atualizar_matriz_led() {
    if (usuarios_ativos == MAX_USUARIOS) {
        matriz_draw_pattern(PAD_X, COR_VERMELHO);
    } else {
        uint32_t cor = obter_cor_numero(usuarios_ativos);
        matriz_draw_number(usuarios_ativos, cor);
    }
}

// ===== FUNÇÃO PARA OBTER COR DO LED EM TEXTO =====
const char* obter_cor_led() {
    if (usuarios_ativos == 0) {
        return "AZUL";
    } 
    else if (usuarios_ativos >= 1 && usuarios_ativos <= MAX_USUARIOS - 2) {
        return "VERDE";
    }
    else if (usuarios_ativos == MAX_USUARIOS - 1) {
        return "AMARELO";
    }
    else if (usuarios_ativos == MAX_USUARIOS) {
        return "VERMELHO";
    }
    return "ERRO";
}

// ===== FUNÇÃO PARA CONTROLAR LED RGB =====
void atualizar_led_rgb() {
    if (usuarios_ativos == 0) {
        gpio_put(LED_AZUL_GPIO, 1);
        gpio_put(LED_VERDE_GPIO, 0);
        gpio_put(LED_VERMELHO_GPIO, 0);
    } 
    else if (usuarios_ativos >= 1 && usuarios_ativos <= MAX_USUARIOS - 2) {
        gpio_put(LED_AZUL_GPIO, 0);
        gpio_put(LED_VERDE_GPIO, 1);
        gpio_put(LED_VERMELHO_GPIO, 0);
    }
    else if (usuarios_ativos == MAX_USUARIOS - 1) {
        gpio_put(LED_AZUL_GPIO, 0);
        gpio_put(LED_VERDE_GPIO, 1);    
        gpio_put(LED_VERMELHO_GPIO, 1); 
    }
    else if (usuarios_ativos == MAX_USUARIOS) {
        gpio_put(LED_AZUL_GPIO, 0);
        gpio_put(LED_VERDE_GPIO, 0);
        gpio_put(LED_VERMELHO_GPIO, 1);
    }
}

// ===== FUNÇÃO PARA ATUALIZAR DISPLAY COMPLETA (AGORA DECIDE QUAL TELA MOSTRAR) =====
void atualizar_display_principal() {
    if (xSemaphoreTake(xMutexDisplay, pdMS_TO_TICKS(100)) == pdTRUE) {
        ssd1306_fill(&display, false); 

        if (tela_atual_stats) {
            char linha1[32], linha2[32], linha3[32], linha4[32], linha5[32];
            
            sprintf(linha1, "Usuarios: %d/%d", usuarios_ativos, MAX_USUARIOS);
            
            if (usuarios_ativos == 0) {
                sprintf(linha2, "Estado: VAZIO");
            } else if (usuarios_ativos == MAX_USUARIOS) {
                sprintf(linha2, "Estado: LOTADO");
            } else if (usuarios_ativos == MAX_USUARIOS - 1) {
                sprintf(linha2, "Estado:ENCHENDO");
            } else {
                sprintf(linha2, "Estado: NORMAL");
            }
            
            sprintf(linha3, "LED: %s", obter_cor_led());
            sprintf(linha4, "Resets: %d", contador_resets);
            
            ssd1306_draw_string(&display, linha1, 2, 2, false);
            ssd1306_draw_string(&display, linha2, 2, 14, false);  
            ssd1306_draw_string(&display, linha3, 2, 26, false);
            ssd1306_draw_string(&display, linha4, 2, 38, false);
            
            if (mostrar_resetado) {
                sprintf(linha5, "** RESETADO! **");
                ssd1306_draw_string(&display, linha5, 15, 52, false);
            }
        } else {
            const int ICON_WIDTH = 12;
            const int ICON_HEIGHT = 12;
            const int SPACING = 8;
            const int ICONS_PER_ROW = 5;
            const int CONTENT_WIDTH_ROW = (ICONS_PER_ROW * ICON_WIDTH) + ((ICONS_PER_ROW > 1 ? ICONS_PER_ROW - 1 : 0) * SPACING);
            const int MARGIN_X = (DISPLAY_WIDTH - CONTENT_WIDTH_ROW) / 2;
            
            const int Y_ROW1 = (DISPLAY_HEIGHT / 4) - (ICON_HEIGHT / 2);
            const int Y_ROW2 = (DISPLAY_HEIGHT * 3 / 4) - (ICON_HEIGHT / 2);

            int usuarios_para_desenhar = usuarios_ativos;

            for (int i = 0; i < usuarios_para_desenhar && i < MAX_USUARIOS; ++i) {
                int x_pos, y_pos;
                if (i < ICONS_PER_ROW) { 
                    x_pos = MARGIN_X + (i * (ICON_WIDTH + SPACING));
                    y_pos = Y_ROW1;
                } else { 
                    x_pos = MARGIN_X + ((i - ICONS_PER_ROW) * (ICON_WIDTH + SPACING));
                    y_pos = Y_ROW2;
                }
                // Corrigido para usar ssd1306_rect com os parâmetros corretos
                // top, left, width, height, value, fill
                ssd1306_rect(&display, y_pos, x_pos, ICON_WIDTH, ICON_HEIGHT, true, true);
            }
        }
        
        ssd1306_send_data(&display);
        xSemaphoreGive(xMutexDisplay);
    }
    
    atualizar_led_rgb();
    atualizar_matriz_led();
}

// ===== ISR COM DEBOUNCE =====
void gpio_irq_handler(uint gpio, uint32_t events) {
    uint32_t current_time = to_ms_since_boot(get_absolute_time());
    
    if ((current_time - last_interrupt_time) > debounce_delay_ms) {
        last_interrupt_time = current_time;
        
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        xSemaphoreGiveFromISR(xResetSem, &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}

// ===== TASK BOTÃO A - ENTRADA =====
void vTaskEntrada(void *pvParameters) {
    bool botao_anterior = true;
    bool botao_atual;
    
    while (1) {
        botao_atual = gpio_get(BOTAO_A_GPIO);
        
        if (botao_anterior && !botao_atual) {
            vTaskDelay(pdMS_TO_TICKS(50));
            
            if (!gpio_get(BOTAO_A_GPIO)) {
                if (xSemaphoreTake(xSemaphoreContagem, 0) == pdTRUE) {
                    xSemaphoreTake(xMutexUsuarios, portMAX_DELAY);
                    usuarios_ativos++;
                    printf("[ENTRADA] Total: %d/%d\n", usuarios_ativos, MAX_USUARIOS);
                    atualizar_display_principal();
                    xSemaphoreGive(xMutexUsuarios);
                } else {
                    printf("[ENTRADA] LIMITE! %d/%d - BEEP\n", MAX_USUARIOS, MAX_USUARIOS);
                }
                
                while (!gpio_get(BOTAO_A_GPIO)) {
                    vTaskDelay(pdMS_TO_TICKS(10));
                }
            }
        }
        
        botao_anterior = botao_atual;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// ===== TASK BOTÃO B - SAÍDA =====
void vTaskSaida(void *pvParameters) {
    bool botao_anterior = true;
    bool botao_atual;
    
    while (1) {
        botao_atual = gpio_get(BOTAO_B_GPIO);
        
        if (botao_anterior && !botao_atual) {
            vTaskDelay(pdMS_TO_TICKS(50));
            
            if (!gpio_get(BOTAO_B_GPIO)) {
                xSemaphoreTake(xMutexUsuarios, portMAX_DELAY);
                
                if (usuarios_ativos > 0) {
                    usuarios_ativos--;
                    xSemaphoreGive(xSemaphoreContagem);
                    printf("[SAIDA] Total: %d/%d\n", usuarios_ativos, MAX_USUARIOS);
                    atualizar_display_principal();
                } else {
                    printf("[SAIDA] Nenhum usuario ativo\n");
                }
                
                xSemaphoreGive(xMutexUsuarios);
                
                while (!gpio_get(BOTAO_B_GPIO)) {
                    vTaskDelay(pdMS_TO_TICKS(10));
                }
            }
        }
        
        botao_anterior = botao_atual;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// ===== TASK RESET - JOYSTICK COM INTERRUPÇÃO =====
void vTaskReset(void *pvParameters) {
    while (1) {
        if (xSemaphoreTake(xResetSem, portMAX_DELAY) == pdTRUE) {
            xSemaphoreTake(xMutexUsuarios, portMAX_DELAY);
            
            int vagas_ocupadas = usuarios_ativos;
            for (int i = 0; i < vagas_ocupadas; i++) {
                xSemaphoreGive(xSemaphoreContagem);
            }
            
            usuarios_ativos = 0;
            contador_resets++;
            
            printf("[RESET] Sistema resetado! Total: %d/%d - BEEP DUPLO (Reset #%d)\n", 
                   usuarios_ativos, MAX_USUARIOS, contador_resets);
            
            mostrar_resetado = true;
            atualizar_display_principal();
            
            xSemaphoreGive(xMutexUsuarios);
            
            vTaskDelay(pdMS_TO_TICKS(2000));
            
            xSemaphoreTake(xMutexUsuarios, portMAX_DELAY);
            mostrar_resetado = false;
            atualizar_display_principal();
            xSemaphoreGive(xMutexUsuarios);
        }
    }
}

// ===== TASK PARA ALTERNAR TELAS =====
void vTaskAlternarTelas(void *pvParameters) {
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(2000)); 

        xSemaphoreTake(xMutexUsuarios, portMAX_DELAY);
        tela_atual_stats = !tela_atual_stats; 
        atualizar_display_principal(); 
        xSemaphoreGive(xMutexUsuarios);
    }
}

// ===== FUNÇÃO PRINCIPAL =====
int main() {
    stdio_init_all();
    
    i2c_init(I2C_PORT, 400 * 1000);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);
    
    ssd1306_init(&display, DISPLAY_WIDTH, DISPLAY_HEIGHT, false, DISPLAY_ADDRESS, I2C_PORT);
    ssd1306_config(&display);
    
    inicializar_matriz_led();
    printf("Matriz LED 5x5 inicializada!\n");
    
    gpio_init(LED_VERDE_GPIO);
    gpio_set_dir(LED_VERDE_GPIO, GPIO_OUT);
    gpio_put(LED_VERDE_GPIO, 0);
    
    gpio_init(LED_AZUL_GPIO);
    gpio_set_dir(LED_AZUL_GPIO, GPIO_OUT);
    gpio_put(LED_AZUL_GPIO, 0);
    
    gpio_init(LED_VERMELHO_GPIO);
    gpio_set_dir(LED_VERMELHO_GPIO, GPIO_OUT);
    gpio_put(LED_VERMELHO_GPIO, 0);
    
    gpio_init(BOTAO_A_GPIO);
    gpio_set_dir(BOTAO_A_GPIO, GPIO_IN);
    gpio_pull_up(BOTAO_A_GPIO);
    
    gpio_init(BOTAO_B_GPIO);
    gpio_set_dir(BOTAO_B_GPIO, GPIO_IN);
    gpio_pull_up(BOTAO_B_GPIO);
    
    gpio_init(JOYSTICK_GPIO);
    gpio_set_dir(JOYSTICK_GPIO, GPIO_IN);
    gpio_pull_up(JOYSTICK_GPIO);
    gpio_set_irq_enabled_with_callback(JOYSTICK_GPIO, GPIO_IRQ_EDGE_FALL, true, &gpio_irq_handler);
    
    xMutexUsuarios = xSemaphoreCreateMutex();
    xMutexDisplay = xSemaphoreCreateMutex();
    xResetSem = xSemaphoreCreateBinary();
    xSemaphoreContagem = xSemaphoreCreateCounting(MAX_USUARIOS, MAX_USUARIOS);
    
    atualizar_display_principal();
    
    xTaskCreate(vTaskEntrada, "TaskEntrada", 1024, NULL, 2, NULL);
    xTaskCreate(vTaskSaida, "TaskSaida", 1024, NULL, 2, NULL);
    xTaskCreate(vTaskReset, "TaskReset", 1024, NULL, 3, NULL);
    xTaskCreate(vTaskAlternarTelas, "TaskAlternarTelas", 1024, NULL, 1, NULL); 
    
    printf("Sistema completo iniciado!\n");
    
    vTaskStartScheduler();
    
    return 0;
}
