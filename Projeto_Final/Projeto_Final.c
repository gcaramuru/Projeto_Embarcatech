#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "pico/stdlib.h"
#include "pico/binary_info.h"
#include "inc/ssd1306.h"
#include "hardware/i2c.h"      
#include "hardware/adc.h"
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/clocks.h" 
#include "hardware/gpio.h"
#include "hardware/pwm.h"     
#include "hardware/timer.h" 

#include "ws2818b.pio.h"  // Biblioteca gerada pelo arquivo .pio durante compilação.

/* Definição dos pinos dos botões */
const uint BUTTON_A_PIN = 5;
const uint BUTTON_B_PIN = 6;
/* Definição dos pinos I2C */
const uint I2C_SDA = 14;
const uint I2C_SCL = 15;
/* Definição dos pinos do joystick */
const uint BUTTON_JOYSTICK_PIN = 22;
const uint EIXO_Y_JOYSTICK_PIN = 27;
const uint EIXO_X_JOYSTICK_PIN = 26;
/* Definição dos canais ADC utilizados*/
const int ADC_TEMPERATURE_CHANNEL = 4;
const int ADC_EIXO_Y_JOYSTICK_CHANNEL = 1;
const int ADC_EIXO_X_JOYSTICK_CHANNEL = 0;
/* Definição dos pinos do led */
const uint LED_VERDE = 11;
const uint LED_AZUL = 12;
const uint LED_VERMELHO = 13; 
/* Definições PWM (10kHz)*/
const uint16_t PERIOD = 4096;  
const float DIVIDER_PWM = 3.0;
/* Definições da matriz de led (baseado no exemplo disponibilizado no github da placa)*/
#define LED_COUNT 25
#define LED_PIN 7
struct pixel_t {                // Definição de pixel GRB
  uint8_t G, R, B;              // Três valores de 8-bits compõem um pixel.
};                              
typedef struct pixel_t pixel_t; 
typedef pixel_t npLED_t;        // Mudança de nome de "struct pixel_t" para "npLED_t" por clareza.
npLED_t leds[LED_COUNT];        // Declaração do buffer de pixels que formam a matriz.
PIO np_pio;                     // Variável para uso da máquina PIO.
uint sm;                        // Variável para uso da máquina PIO.
/* Definição do pino do buzzer */
#define BUZZER_PIN 21

/* Definições de váriaves globais utilizadas durante o código */
int start = 5;                // Variável que auxilia no controle de execução das telas principais do display e em alguns loops
bool selecao = 0;             // Variável que condiciona o loop do código enquanto o usuário não seleciona o conteúdo no display
int posicao_joystick = 0;     // Variável que auxilia na movimentação do indicador de seleção no display
int conteudo_selecionado = 0; // Variável que auxilia na indicação de qual conteúdo foi escolhido 
uint16_t eixo_x;              // Variável que armazena o valor lido do eixo x do potenciometro
uint16_t eixo_y;              // Variável que armazena o valor lido do eixo y do potenciometro
bool level_led = 0;           // Variável que auxilia na mudança de estado do led, durante o conteudo de temporização
bool interrupcao = 0;         // Variável que condicionao loop no conteudo de interrupção

/* Preparar área de renderização para o display */
uint8_t ssd[ssd1306_buffer_length];
struct render_area frame_area = {
  start_column : 0,
  end_column : ssd1306_width - 1,
  start_page : 0,
  end_page : ssd1306_n_pages - 1
};

/* Declaração das funções de configuração (Como elas estão definidas após o main, é preciso declarar) */
void conf_botao_a();
void conf_joystick();
void conf_i2c();
void conf_display();
void texto_inicial_display();
void conf_led();
void conf_botao_b();

/* Função que "desenha" a string que será renderizada no display */
void desenho_da_string(char *text[]){
  int y = 0; // Variável auxiliar do loop
  /* Loop que "desenha" todas posições do display */
  for (uint i = 0; i < 8; i++)
  {
    ssd1306_draw_string(ssd, 5, y, text[i]);    // Função que "desenha" o caracter na posição específica
    y += 8; // Atualização da variável
  }
}

/* Função de interrupção do botão A */
static void button_A_irq_handler(){
    static absolute_time_t last_button_a_press; // Variável de tempo que auxilia no debouncing do botão A
    absolute_time_t current_time = get_absolute_time(); // Atualização do tempo na variável
    /* Condicional que verifica se o botão A foi pressionado, evitando erros por falsa detecção */
    if (absolute_time_diff_us(last_button_a_press, current_time) > 100000) {
        last_button_a_press = current_time; // Atualização do tempo na variável
        
        memset(ssd, 0, ssd1306_buffer_length);  // Limpa o display
        render_on_display(ssd, &frame_area);    // Renderiza a informação no display
        /* Texto a ser renderizado no display */
        char *text_bot[] = {
        "                ",
        "Mova o joystick ",
        "para posicionar ",
        "o cursor e      ",
        "clique para     ",
        "selecionar o    ",
        "conteudo        ",
        "                ",
        };
        desenho_da_string(text_bot);            // "Desenha" string a ser renderizada
        render_on_display(ssd, &frame_area);    // Renderiza a informação no display

        start = 1;                              // Atualização da variável de controle de execução das telas principais do display e de alguns loops
    }
}

/* Função de inicialização da aplicação (Tela inicial + habilitação da interrupção do botão A)  */
void texto_inicial_display(){
  gpio_set_irq_enabled_with_callback(BUTTON_A_PIN, GPIO_IRQ_EDGE_FALL, true, &button_A_irq_handler);    // Habilita interrupção do botão A
  memset(ssd, 0, ssd1306_buffer_length);  // Limpa o display
  render_on_display(ssd, &frame_area);    // Renderiza a informação no display
  /* Texto a ser renderizado no display */
  char *texto_inicial[] = {
    "                ",
    "   Bem Vindo    ",
    "                ",
    "   Aperte o     ",
    "   Botao A      ",
    "  para iniciar  ",
    "                ",
    "                ",
    };
    desenho_da_string(texto_inicial);        // "Desenha" string a ser renderizada
    render_on_display(ssd, &frame_area);    // Renderiza a informação no display
}

/* Função de leitura dos potenciometros do joystick */
void leitura_joystick_callback(){
  adc_select_input(ADC_EIXO_X_JOYSTICK_CHANNEL);    // Seleciona o canal ADC do potenciometro do eixo x
  sleep_us(2);                                      // Delay
  eixo_x = adc_read();                              // Armazena a leitura do eixo x

  adc_select_input(ADC_EIXO_Y_JOYSTICK_CHANNEL);    // Seleciona o canal ADC do potenciometro do eixo y
  sleep_us(2);                                      // Delay
  eixo_y = adc_read();                              // Armazena a leitura do eixo y
  /* Condição que verifica se o indicador de seleção no display vai descer, subir ou ficar parado */
  if(eixo_x >= 2300){
    posicao_joystick = 2;   // Passa o valor que indica que vai ficar subir
  }
  else if(eixo_x <= 1700){
    posicao_joystick = 1;   // Passa o valor que indica que vai ficar descer
  }
  else{
    posicao_joystick = 0;   // Passa o valor que indica que vai ficar parado
  }
}


/* Função de interrupção do botão do joystick */
void button_selecao_irq_handler(){
    static absolute_time_t last_button_selecao_press; // Variável de tempo que auxilia no debouncing do botão de joystick
    absolute_time_t current_time = get_absolute_time(); // Atualização do tempo na variável
    /* Condicional que verifica se o botão do joystick foi pressionado, evitando erros por falsa detecção */
    if (absolute_time_diff_us(last_button_selecao_press, current_time) > 100000) {
      last_button_selecao_press = current_time; // Atualização do tempo na variável
      selecao = 1;  // Atualiza a variável que condiciona o loop do código enquanto o usuário não seleciona o conteúdo no display
    }
}

/* Função que indica o conteúdo selecionado, baseado na posição da tela */
void selecionar_conteudo(){
  struct repeating_timer timer; // Variável auxiliar de tempo
  int posicao_I = 8;            // Variável auxiliar que indica a posição 
  /* Loop de posicionamento do indicador de seleção no display */
  while(selecao == 0){
    leitura_joystick_callback();    // Realiza a leitura do joystick para saber se o indicador de seleção deve subir, descer ou ficar parado
    gpio_set_irq_enabled_with_callback(BUTTON_JOYSTICK_PIN, GPIO_IRQ_EDGE_FALL, true, &button_selecao_irq_handler); // Habilita a interrupção do botão do joystick, responsável pela seleção
    /* Condição que renderiza na tela o indicador de seleção, baseado na leitura do joystick */
    if(posicao_joystick == 0){
      ssd1306_draw_string(ssd, 5, posicao_I, "I");    // Função que "desenha" o caracter na posição específica
      render_on_display(ssd, &frame_area);            // Renderiza a informação no display
    }
    else if(posicao_joystick == 1){
      ssd1306_draw_string(ssd, 5, posicao_I, " ");    // Função que "desenha" o caracter na posição específica
      render_on_display(ssd, &frame_area);            // Renderiza a informação no display
      posicao_I += 8;                                 // Atualização da variável
      /* Condição que "reinicia" a posição do indicador de seleção, caso ultrapasse o limite da lista de conteudo */
      if(posicao_I > 56){
        posicao_I = 8;
      }
      ssd1306_draw_string(ssd, 5, posicao_I, "I");    // Função que "desenha" o caracter na posição específica
      render_on_display(ssd, &frame_area);            // Renderiza a informação no display
    }
    else if(posicao_joystick == 2){
      ssd1306_draw_string(ssd, 5, posicao_I, " ");    // Função que "desenha" o caracter na posição específica
      render_on_display(ssd, &frame_area);            // Renderiza a informação no display
      posicao_I -= 8;                                 // Atualização da variável
      /* Condição que "reinicia" a posição do indicador de seleção, caso ultrapasse o limite da lista de conteudo */
      if(posicao_I < 8){
        posicao_I = 56;
      }
      ssd1306_draw_string(ssd, 5, posicao_I, "I");    // Função que "desenha" o caracter na posição específica
      render_on_display(ssd, &frame_area);            // Renderiza a informação no display
    }
    sleep_ms(200);    // Delay (Esse delay controla a sensibilidade do indicador de posição)
  }
  gpio_set_irq_enabled_with_callback(BUTTON_JOYSTICK_PIN, GPIO_IRQ_EDGE_FALL, false, &button_selecao_irq_handler); // Desabilita a interrupção do botão do joystick
  conteudo_selecionado = (posicao_I / 8);   // Armazena a posição do conteúdo selecionado
}

/* Função de configuração básica do led */
void conf_led(){
  gpio_init(LED_AZUL);                  // Inicializa o pino do led azul
  gpio_set_dir(LED_AZUL, GPIO_OUT);     // Configura como saída
  gpio_put(LED_AZUL, 0);                // Seta a saída inicial como 0 (led apagado)

  gpio_init(LED_VERDE);                  // Inicializa o pino do led verde
  gpio_set_dir(LED_VERDE, GPIO_OUT);     // Configura como saída
  gpio_put(LED_VERDE, 0);                // Seta a saída inicial como 0 (led apagado)

  gpio_init(LED_VERMELHO);                  // Inicializa o pino do led vermelho
  gpio_set_dir(LED_VERMELHO, GPIO_OUT);     // Configura como saída
  gpio_put(LED_VERMELHO, 0);                // Seta a saída inicial como 0 (led apagado)
}

/* Função que executa o conteúdo "LED" */
void conteudo_led(){
  memset(ssd, 0, ssd1306_buffer_length);  // Limpa o display
  render_on_display(ssd, &frame_area);    // Renderiza a informação no display
  /* Texto a ser renderizado no display */
  char *texto_led[] = {
    "                ",
    "LED eh um       ",
    "dispositivo     ",
    "usado para      ",
    "indicacao       ",
    "visual          ",
    "                ",
    "                ",
    };
    desenho_da_string(texto_led);        // "Desenha" string a ser renderizada
    render_on_display(ssd, &frame_area); // Renderiza a informação no display

    conf_led();             // Configura o led
    gpio_put(LED_AZUL, 1);  // Acende o led azul
    sleep_ms(6000);         // Delay
    gpio_put(LED_AZUL,0);   // Apaga o led azul
}

/* Função de conversão do dado lido no canal adc para temperatura */
float temperatura() {
  adc_select_input(ADC_TEMPERATURE_CHANNEL);                    // Seleciona o canal ADC do sensor de temperatura
  uint16_t temp = adc_read();                                   // Armazena a leitura da temperatura
  /* Converte a leitura analógica em temperatura */
  const float conversion_factor = 3.3f / (1 << 12);
  float voltage = temp * conversion_factor;    
  float celsius = 27.0f - (voltage - 0.706f) / 0.001721f; 
  float fahrenheit = (1.8*celsius) + 32;
  return fahrenheit;
}

/* Função que executa o conteúdo "ADC" */
void conteudo_adc(){
  memset(ssd, 0, ssd1306_buffer_length);  // Limpa o display
  render_on_display(ssd, &frame_area);    // Renderiza a informação no display
  /* Texto a ser renderizado no display */
  char *texto_adc[] = {
    "ADC eh uma      ",
    "conversao       ",
    "de analogico    ",
    "para digital    ",
    "permitindo que  ",
    "o sistema       ",
    "entenda dados   ",
    "analogicos      ",
    };
  desenho_da_string(texto_adc);        // "Desenha" string a ser renderizada
  render_on_display(ssd, &frame_area); // Renderiza a informação no display
  sleep_ms(8000);                     // Delay

  float temp = temperatura();          // Armazena na variável a leitura da temperatura do sensor
  char tempC[16];                      // Variável que auxilia na conversão do valor de temperatura em string
  snprintf(tempC, sizeof(tempC), "    %.2f F", temp);   // Converte o valor da temperatura na string a ser renderizada

  memset(ssd, 0, ssd1306_buffer_length);  // Limpa o display
  render_on_display(ssd, &frame_area);    // Renderiza a informação no display
  /* Texto a ser renderizado no display */
  char *texto__exemplo_adc[] = {
    "                ",
    "Por exemplo     ",
    "ler o sensor    ",
    "de temperatura  ",
    "interna         ",
    "retornando      ",
    tempC,
    "                ",
    };
  desenho_da_string(texto__exemplo_adc);    // "Desenha" string a ser renderizada
  render_on_display(ssd, &frame_area);      // Renderiza a informação no display
  sleep_ms(8000);                           // Delay

}


/* Função de interrupção do botão B */
void button_B_irq_handler(){
    static absolute_time_t last_button_b_press; // Variável de tempo que auxilia no debouncing do botão B
    absolute_time_t current_time = get_absolute_time(); // Atualização do tempo na variável
    /* Condicional que verifica se o botão B foi pressionado, evitando erros por falsa detecção */
    if (absolute_time_diff_us(last_button_b_press, current_time) > 100000) {
      last_button_b_press = current_time; // Atualização do tempo na variável
      gpio_put(LED_VERDE, 0);             // Apaga o led verde
      start = 3;                          // Atualização da variável de controle de execução das telas principais do display e de alguns loops
    }
}

/* Função que executa o conteúdo do "BOTAO" */
void conteudo_botao(){
  memset(ssd, 0, ssd1306_buffer_length);       // Limpa o display
  render_on_display(ssd, &frame_area);         // Renderiza a informação no display
  /* Texto a ser renderizado no display */
  char *texto_botao[] = {
    "                ",
    "Botao eh        ",
    "uma entrada     ",
    "do sistema      ",
    "que permite     ",
    "interacao com   ",
    "o usuario       ",
    "                ",
    };
  desenho_da_string(texto_botao);             // "Desenha" string a ser renderizada
  render_on_display(ssd, &frame_area);        // Renderiza a informação no display
  sleep_ms(8000);                            // Delay

  memset(ssd, 0, ssd1306_buffer_length);      // Limpa o display
  render_on_display(ssd, &frame_area);        // Renderiza a informação no display
  /* Texto a ser renderizado no display */
  char *texto_exemplo_botao[] = {
    "                ",
    "Por exemplo     ",
    "ao apertar o    ",
    "Botao B         ",
    "podemos sair    ",
    "desta tela      ",
    "e apagar        ",
    "o led           ",
    };
  desenho_da_string(texto_exemplo_botao);     // "Desenha" string a ser renderizada
  render_on_display(ssd, &frame_area);        // Renderiza a informação no display
  
  conf_led();               // Configura o led
  gpio_put(LED_VERDE, 1);   // Acende o led verde
  gpio_set_irq_enabled_with_callback(BUTTON_B_PIN, GPIO_IRQ_EDGE_FALL, true, &button_B_irq_handler);    // Habilita a interrupção do botão B
  /* Loop que mantém a execução "parada" enquanto aguarda o clique no botão B */
  while (start != 3)
  {
    sleep_ms(300);
  }
  gpio_set_irq_enabled_with_callback(BUTTON_B_PIN, GPIO_IRQ_EDGE_FALL, false, &button_B_irq_handler);   // Desabilita a interrupção do botão B
}

/* Notas musicais para a música tema de Star Wars (Baseada no exemplo disponível no github) */ 
const uint star_wars_notes[] = {
  330, 330, 330, 262, 392, 523, 330, 262,
  392, 523, 330, 659, 659, 659, 698, 523,
  415, 349, 330, 262, 392, 523, 330, 262,
  392, 523, 330, 659, 659, 659, 698, 523,
  415, 349, 330, 523, 494, 440, 392, 330,
  659, 784, 659, 523, 494, 440, 392, 330,
  659, 659, 330, 784, 880, 698, 784, 659,
  523, 494, 440, 392, 659, 784, 659, 523,
  494, 440, 392, 330, 659, 523, 659, 262,
  330, 294, 247, 262, 220, 262, 330, 262,
  330, 294, 247, 262, 330, 392, 523, 440,
  349, 330, 659, 784, 659, 523, 494, 440,
  392, 659, 784, 659, 523, 494, 440, 392
};
/* Duração das notas musicais (Baseada no exemplo disponível no github) */ 
const uint note_duration[] = {
  250, 250, 250, 175, 75, 150, 250, 175,
  75, 150, 250, 250, 250, 250, 175, 75,
  150, 250, 250, 175, 75, 150, 250, 175,
  75, 150, 325, 250, 75, 150, 250, 175,
  75, 150, 250, 75, 150, 250, 175, 75,
  150, 325, 250, 175, 75, 150, 250, 175,
  75, 150, 250, 250, 250, 250, 175, 75,
  150, 250, 250, 175, 75, 150, 250, 175,
  75, 150, 250, 175, 75, 150, 250, 250,
  175, 75, 150, 250, 250, 175, 75, 150,
};

/* Inicializa o PWM no pino do buzzer */
void pwm_init_buzzer(uint pin) {
  gpio_set_function(pin, GPIO_FUNC_PWM);        // Configura o pino para função PWM
  /* Configuração PWM */
  uint slice_num = pwm_gpio_to_slice_num(pin);
  pwm_config config = pwm_get_default_config();
  pwm_config_set_clkdiv(&config, 4.0f);
  pwm_init(slice_num, &config, true);
  pwm_set_gpio_level(pin, 0);                   // Desliga o PWM inicialmente
}

/* Toca uma nota com a frequência e duração especificadas */ 
void play_tone(uint pin, uint frequency, uint duration_ms) {
  uint slice_num = pwm_gpio_to_slice_num(pin);  // Armazena o slice do PWM
  uint32_t clock_freq = clock_get_hz(clk_sys);  // Armazena a frequência do clock
  uint32_t top = clock_freq / frequency - 1;    // Calcula o valor do wrap

  pwm_set_wrap(slice_num, top);                 // Configura o wrap
  pwm_set_gpio_level(pin, top / 2);             // Configura o valor pwm para 50% de duty cycle

  sleep_ms(duration_ms);                        // Delay

  pwm_set_gpio_level(pin, 0);                   // Desliga o som após a duração
  sleep_ms(50);                                 // Delay
}

/* Função principal para tocar a música */ 
void play_star_wars(uint pin) {
  /* Loop responsável por chamar a função que toca as notas */
  for (int i = 0; i < sizeof(star_wars_notes) / sizeof(star_wars_notes[0]); i++) {
      if (star_wars_notes[i] == 0) {
          sleep_ms(note_duration[i]);
      } else {
          play_tone(pin, star_wars_notes[i], note_duration[i]);
      }
  }
}

/* Função que executa o conteudo "BUZZER" */
void conteudo_buzzer(){
  memset(ssd, 0, ssd1306_buffer_length);      // Limpa o display
  render_on_display(ssd, &frame_area);        // Renderiza a informação no display 
  /* Texto a ser renderizado no display */
  char *texto_buzzer[] = {
    "                ",
    "Buzzer eh uma   ",
    "saida de audio  ",
    "que permite     ",
    "interacao       ",
    "com o usuario   ",
    "                ",
    "                ",
    };
  desenho_da_string(texto_buzzer);            // "Desenha" string a ser renderizada
  render_on_display(ssd, &frame_area);        // Renderiza a informação no display

  pwm_init_buzzer(BUZZER_PIN);                // Inicializa o PWM no pino do buzzer
  play_star_wars(BUZZER_PIN);                 // Toca a múscia (baseado no exemplo do github)

  // sleep_ms(6000);                             // Delay
}

/* Função que executa o conteudo "DISPLAY" */
void conteudo_display(){
  memset(ssd, 0, ssd1306_buffer_length);      // Limpa o display
  render_on_display(ssd, &frame_area);        // Renderiza a informação no display 
  /* Texto a ser renderizado no display */
  char *texto_display[] = {
    "                ",
    "Display eh      ",
    "uma saida       ",
    "do sistema      ",
    "que transmite   ",
    "informacoes     ",
    "ao usuario      ",
    "                ",
    };
  desenho_da_string(texto_display);           // "Desenha" string a ser renderizada
  render_on_display(ssd, &frame_area);        // Renderiza a informação no display
  sleep_ms(8000);                             // Delay
}

/* Função de interrupção do botão do joystick*/
void button_joystick_irq_handler(){
    static absolute_time_t last_button_joystick_press; // Variável de tempo que auxilia no debouncing do botão do joystick
    absolute_time_t current_time = get_absolute_time(); // Atualização do tempo na variável
    /* Condicional que verifica se o botão do joystick foi pressionado, evitando erros por falsa detecção */
    if (absolute_time_diff_us(last_button_joystick_press, current_time) > 100000) {
      last_button_joystick_press = current_time; // Atualização do tempo na variável
      pwm_set_gpio_level(LED_VERDE, 0);     // Apaga o led verde na configuração PWM
      pwm_set_gpio_level(LED_AZUL, 0);      // Apaga o led azul na configuração PWM
      pwm_set_gpio_level(LED_VERMELHO, 0);  // Apaga o led vermelho na configuração PWM
      start = 3;                            // Atualização da variável de controle de execução das telas principais do display e de alguns loops
    }
}

/* Função de configuração do PWM do led */
void setup_pwm(){
  uint slice1, slice2, slice3;  // Variáveis que armazenam os slices dos leds
  /* Configuração do PWM do led verde */
  gpio_set_function(LED_VERDE, GPIO_FUNC_PWM);
  slice1 = pwm_gpio_to_slice_num(LED_VERDE);
  pwm_set_clkdiv(slice1, DIVIDER_PWM);
  pwm_set_wrap(slice1, PERIOD);
  pwm_set_gpio_level(LED_VERDE, 0);
  pwm_set_enabled(slice1, true);
  /* Configuração do PWM do led azul */
  gpio_set_function(LED_AZUL, GPIO_FUNC_PWM);
  slice2 = pwm_gpio_to_slice_num(LED_AZUL);
  pwm_set_clkdiv(slice2, DIVIDER_PWM);
  pwm_set_wrap(slice2, PERIOD);
  pwm_set_gpio_level(LED_AZUL, 0);
  pwm_set_enabled(slice2, true);
  /* Configuração do PWM do led vermelho */
  gpio_set_function(LED_VERMELHO, GPIO_FUNC_PWM);
  slice3 = pwm_gpio_to_slice_num(LED_VERMELHO);
  pwm_set_clkdiv(slice3, DIVIDER_PWM);
  pwm_set_wrap(slice3, PERIOD);
  pwm_set_gpio_level(LED_VERMELHO, 0);
  pwm_set_enabled(slice3, true);
}

/* Função que executa o conteúdo "JOYSTICK" */
void conteudo_joystick(){
  memset(ssd, 0, ssd1306_buffer_length);      // Limpa o display
  render_on_display(ssd, &frame_area);        // Renderiza a informação no display 
  /* Texto a ser renderizado no display */
  char *texto_joystick[] = {
    "                ",
    "Joystick usa 2  ",
    "potenciometros  ",
    "como entradas   ",
    "analogicas e    ",
    "um botao como   ",
    "entrada digital ",
    "                ",
    };
  desenho_da_string(texto_joystick);          // "Desenha" string a ser renderizada
  render_on_display(ssd, &frame_area);        // Renderiza a informação no display
  sleep_ms(8000);                            // Delay
  memset(ssd, 0, ssd1306_buffer_length);      // Limpa o display
  render_on_display(ssd, &frame_area);        // Renderiza a informação no display
  /* Texto a ser renderizado no display */
  char *texto_exemplo_joystick[] = {
    "Podemos         ",
    "controlar a cor ",
    "do led mexendo  ",
    "o joystick e    ",
    "sair da tela    ",
    "ao pressionar   ",
    "o botao do      ",
    "joystick        ",
    };
  desenho_da_string(texto_exemplo_joystick);  // "Desenha" string a ser renderizada
  render_on_display(ssd, &frame_area);        // Renderiza a informação no display
  sleep_ms(500);                              // Delay

  setup_pwm();                                // Chamada da função de coniguração do PWM dos leds
  gpio_set_irq_enabled_with_callback(BUTTON_JOYSTICK_PIN, GPIO_IRQ_EDGE_FALL, true, &button_joystick_irq_handler);  // Habilita a interrupção do botão do joystick
  /* Loop que permite o controle PWM dos led utilizando o joystick */
  while(start != 3){
    leitura_joystick_callback();                // Realiza a leitura dos eios do joystick
    pwm_set_gpio_level(LED_VERDE, eixo_y);      // Autaliza o valor pwm do led verde
    pwm_set_gpio_level(LED_AZUL, eixo_x);       // Autaliza o valor pwm do led azul
    pwm_set_gpio_level(LED_VERMELHO, eixo_x);   // Autaliza o valor pwm do led vermelho
    sleep_ms(300);                              // Delay
  }
  gpio_set_irq_enabled_with_callback(BUTTON_JOYSTICK_PIN, GPIO_IRQ_EDGE_FALL, false, &button_joystick_irq_handler);  // Desabilita a interrupção do botão do joystick
}

/* Inicializa a máquina PIO para controle da matriz de LEDs */
void npInit(uint pin) {
  /* Cria programa PIO */ 
  uint offset = pio_add_program(pio0, &ws2818b_program);
  np_pio = pio0;
  
  sm = pio_claim_unused_sm(np_pio, false);  // Toma posse de uma máquina PIO 
  if (sm < 0) {
    np_pio = pio1;
    sm = pio_claim_unused_sm(np_pio, true); // Se nenhuma máquina estiver livre, panic!
  }
  ws2818b_program_init(np_pio, sm, offset, pin, 800000.f);  // Inicia programa na máquina PIO obtida
  /* Limpa buffer de pixels */
  for (uint i = 0; i < LED_COUNT; ++i) {
    leds[i].R = 0;
    leds[i].G = 0;
    leds[i].B = 0;
  }
}
/* Atribui uma cor RGB a um LED */
void npSetLED(const uint index, const uint8_t r, const uint8_t g, const uint8_t b) {
  leds[index].R = r;
  leds[index].G = g;
  leds[index].B = b;
}
/* Limpa o buffer de pixels */
void npClear() {
  for (uint i = 0; i < LED_COUNT; ++i)
    npSetLED(i, 0, 0, 0);
}
/* Escreve os dados do buffer nos LEDs */
void npWrite() {
  /* Escreve cada dado de 8-bits dos pixels em sequência no buffer da máquina PIO */
  for (uint i = 0; i < LED_COUNT; ++i) {
    pio_sm_put_blocking(np_pio, sm, leds[i].G);
    pio_sm_put_blocking(np_pio, sm, leds[i].R);
    pio_sm_put_blocking(np_pio, sm, leds[i].B);
  }
  sleep_us(100); // Delay
}
/* Função para converter a posição do matriz para uma posição do vetor */
int getIndex(int x, int y) {
  // Se a linha for par, percorremos da esquerda para a direita, se for ímpar, percorremos da direita para a esquerda
  if (y % 2 == 0) {
      return 24-(y * 5 + x); // Linha par (esquerda para direita).
  } else {
      return 24-(y * 5 + (4 - x)); // Linha ímpar (direita para esquerda).
  }
}

/* Função que executa o conteúdo "MATRIZ DE LED" */
void conteudo_matriz_de_led(){
  memset(ssd, 0, ssd1306_buffer_length);      // Limpa o display
  render_on_display(ssd, &frame_area);        // Renderiza a informação no display
  /* Texto a ser renderizado no display */
  char *texto_matriz_de_led[] = {
    "A matriz de led ",
    "aumneta o poder ",
    "de informacao   ",
    "sem ter que     ",
    "utilizar uma    ",
    "saida ou pino   ",
    "para cada led   ",
    "individualmente  ",
    };
  desenho_da_string(texto_matriz_de_led);     // "Desenha" string a ser renderizada
  render_on_display(ssd, &frame_area);        // Renderiza a informação no display
  
  npClear();        // Limpa os leds da matriz  (apaga todos)
  /* Matriz de desenho dos leds */
  int matriz[5][5][3] = {
    {{120, 0, 0}, {120, 0, 0}, {120, 0, 0}, {120, 0, 0}, {120, 0, 0}},
    {{120, 0, 0}, {0, 120, 0}, {230, 0, 50}, {0, 120, 0}, {120, 0, 0}},
    {{120, 0, 0}, {230, 0, 50}, {120, 120, 120}, {230, 0, 50}, {0, 0, 120}},
    {{0, 0, 120}, {0, 120, 0}, {230, 0, 50}, {0, 120, 0}, {0, 0, 120}},
    {{0, 0, 120}, {0, 0, 120}, {0, 0, 120}, {0, 0, 120}, {0, 0, 120}}
  };
  /* Desenhando Sprite contido na matriz */ 
  for(int linha = 0; linha < 5; linha++){
    for(int coluna = 0; coluna < 5; coluna++){
      int posicao = getIndex(linha, coluna);
      npSetLED(posicao, matriz[coluna][linha][0], matriz[coluna][linha][1], matriz[coluna][linha][2]);
    }
  }
  npWrite();        // Passando o conteúdo para os leds da matriz
  sleep_ms(8000);   // Delay
  npClear();        // Limpa os leds da matriz  (apaga todos)
  npWrite();        // Passando o conteúdo para os leds da matriz
}

/* Função que executa o conteúdo "PWM" */
void conteudo_pwm(){
  memset(ssd, 0, ssd1306_buffer_length);      // Limpa o display
  render_on_display(ssd, &frame_area);        // Renderiza a informação no display
  /* Texto a ser renderizado no display */
  char *texto_pwm[] = {
    "PWM eh uma      ",
    "tecnica que     ",
    "se utiliza para ",
    "controlar a     ",
    "potencia de um  ",
    "dispositivo     ",
    "por exemplo     ",
    "brilho de leds  ",
    };
  desenho_da_string(texto_pwm);         // "Desenha" string a ser renderizada
  render_on_display(ssd, &frame_area);  // Renderiza a informação no display

  setup_pwm();                          // Chamada da função de coniguração do PWM dos leds

  int pwm_level = 0;                    // Variável que auxilia no controle da intensidade do led (valor PWM)
  absolute_time_t initial_time = get_absolute_time();   // Variável auxiliar de tempo
  absolute_time_t current_time = get_absolute_time();   // Variável auxiliar de tempo
  /* Loop de duração de 10 segundos, em que o led aumenta a instensidade do brilho */
  while (absolute_time_diff_us(initial_time, current_time) < 10000000) {
    current_time = get_absolute_time();         // Atualização do valor do tempo
    pwm_set_gpio_level(LED_VERDE, pwm_level);   // Autaliza o valor pwm do led verde   
    pwm_level += 38;                            // Atualiza o valor da variável auxiliar
    sleep_ms(100);                              // Delay
  }
  pwm_set_gpio_level(LED_VERDE, 0);             // Autaliza o valor pwm do led verde
}
/* Callback do temporizador */
bool callback_temporizador(){
    conf_led();                         // Configura o led
    gpio_put(LED_VERMELHO, level_led);  // Muda o estado do led
    level_led = !level_led;             // Muda altera o valor da variável que armazena o estado do led
}

/* Função que executa o conteúdo "TEMPORIZADOR" */
void conteudo_temporizador(){
  memset(ssd, 0, ssd1306_buffer_length);      // Limpa o display
  render_on_display(ssd, &frame_area);        // Renderiza a informação no display
  /* Texto a ser renderizado no display */
  char *texto_temporizador[] = {
    "Temporizador    ",
    "eh uma          ",
    "funcionalidade  ",
    "que permite     ",
    "contar o tempo  ",
    "para controlar  ",
    "eventos         ",
    "                ",
    };
  desenho_da_string(texto_temporizador);    // "Desenha" string a ser renderizada
  render_on_display(ssd, &frame_area);      // Renderiza a informação no display
  sleep_ms(8000);                          // Delay
  memset(ssd, 0, ssd1306_buffer_length);    // Limpa o display
  render_on_display(ssd, &frame_area);      // Renderiza a informação no display
  /* Texto a ser renderizado no display */
  char *texto_exemplo_temporizador[] = {
    "                ",
    "Por exemplo     ",
    "podemos         ",
    "utilizar um     ",
    "temporizador    ",
    "para fazer      ",
    "um led piscar   ",
    "                ",
    };
  desenho_da_string(texto_exemplo_temporizador);    // "Desenha" string a ser renderizada
  render_on_display(ssd, &frame_area);      // Renderiza a informação no display

  struct repeating_timer timer;                                     // Variável auxiliar de tempo
  add_repeating_timer_ms(200, callback_temporizador, NULL, &timer); // Inicializa o temporizador
  absolute_time_t initial_time = get_absolute_time();               // Variável auxiliar de tempo
  absolute_time_t current_time = get_absolute_time();               // Variável auxiliar de tempo
  /* Loop durante 10 segundos, apenas para manter a execução do programa "parada" para rodar apenas o temporizador */
  while (absolute_time_diff_us(initial_time, current_time) < 10000000) {
    current_time = get_absolute_time(); // Atualização do valor do tempo
    sleep_ms(100);                      // Delay
  } 
  gpio_put(LED_VERMELHO, 0);            // Apaga o led vermelho
  cancel_repeating_timer(&timer);       // Finaliza o temporizador
}

void interrupcao_irq_handler(){
    static absolute_time_t time_interrupcao;// Variável de tempo que auxilia no loop
    absolute_time_t current_time;           // Atualização do tempo na variável

    interrupcao = 1;                        // Atualiza a variável que condiciona o loop
    
    time_interrupcao = get_absolute_time(); // Atualização do valor do tempo
    current_time = get_absolute_time();     // Atualização do valor do tempo
    /* Loop de duração de 3 segundos */
    while(absolute_time_diff_us(time_interrupcao, current_time) < 3000000){
        current_time = get_absolute_time(); // Atualização do valor do tempo
        gpio_put(LED_VERMELHO, 1);          // Acende o led vermelho
        pwm_set_gpio_level(BUZZER_PIN, 0);  // Desliga o buzzer
    }
    gpio_put(LED_VERMELHO, 0);              // Apaga o led vermelho
}

/* Função que executa o conteúdo "INTERRUPCAO" */
void conteudo_interrupcao(){
  memset(ssd, 0, ssd1306_buffer_length);    // Limpa o display
  render_on_display(ssd, &frame_area);      // Renderiza a informação no display
  /* Texto a ser renderizado no display */
  char *texto_interrupcao[] = {
    "Interrupcao eh  ",
    "um mecanismo    ",
    "que interrompe  ",
    "o fluxo de      ",
    "execucao para   ",
    "poder realizar  ",
    "outra tarefa    ",
    "                ",
    };
  desenho_da_string(texto_interrupcao);     // "Desenha" string a ser renderizada
  render_on_display(ssd, &frame_area);      // Renderiza a informação no display
  sleep_ms(8000);                          // Delay
  memset(ssd, 0, ssd1306_buffer_length);    // Limpa o display
  render_on_display(ssd, &frame_area);      // Renderiza a informação no display
  /* Texto a ser renderizado no display */
  char *texto_exemplo_interrupcao[] = {
    "                ",
    "Por exemplo     ",
    "interromper o   ",
    "buzzer para     ",
    "acender o led   ",
    "usando o        ",
    "botao B         ",
    "                ",
    };
  desenho_da_string(texto_exemplo_interrupcao);     // "Desenha" string a ser renderizada
  render_on_display(ssd, &frame_area);      // Renderiza a informação no display

  gpio_set_irq_enabled_with_callback(BUTTON_B_PIN, GPIO_IRQ_EDGE_FALL, true, &interrupcao_irq_handler); // Habilita a interrupção do botão B
  pwm_init_buzzer(BUZZER_PIN);  // Configura o pino do buzzer para PWM
  /* Loop que toca o buzzer enquanto não há interrupção */
  while(interrupcao == 0){
    pwm_set_gpio_level(BUZZER_PIN, 200);    // Envia o valor PWM para o buzzer tocar
  }
  gpio_set_irq_enabled_with_callback(BUTTON_B_PIN, GPIO_IRQ_EDGE_FALL, false, &interrupcao_irq_handler); // Desabilita a interrupção do botão B
  interrupcao = 0;     // Atualiza a variável que condiciona o loop 
}

/* Função que executa o conteúdo "PIO" */
void conteudo_pio(){
  memset(ssd, 0, ssd1306_buffer_length);    // Limpa o display
  render_on_display(ssd, &frame_area);      // Renderiza a informação no display
  /* Texto a ser renderizado no display */
  char *texto_pio[] = {
    "PIO eh um       ",
    "recurso de      ",
    "hardware que    ",
    "permite criar   ",
    "maquinas de     ",
    "estado          ",
    "programaveis    ",
    "                ",
    };
  desenho_da_string(texto_pio);             // "Desenha" string a ser renderizada
  render_on_display(ssd, &frame_area);      // Renderiza a informação no display
  sleep_ms(8000);                          // Delay

  memset(ssd, 0, ssd1306_buffer_length);    // Limpa o display
  render_on_display(ssd, &frame_area);      // Renderiza a informação no display
  /* Texto a ser renderizado no display */
  char *texto_exemplo_pio[] = {
    "                ",
    "                ",
    "Esse recurso    ",
    "Eh utilizado    ",
    "no controle da  ",
    "matriz de led   ",
    "                ",
    "                ",
    };
  desenho_da_string(texto_exemplo_pio);             // "Desenha" string a ser renderizada
  render_on_display(ssd, &frame_area);      // Renderiza a informação no display
  /* Matriz de desenho dos leds */
  int matriz[5][5][3] = {
    {{80, 0, 0}, {80, 0, 0}, {80, 0, 0}, {80, 0, 0}, {80, 0, 0}},
    {{80, 0, 0}, {0, 0, 100}, {0, 0, 100}, {0, 0, 100}, {80, 0, 0}},
    {{80, 0, 0}, {0, 0, 100}, {0, 200, 0}, {0, 0, 100}, {80, 0, 0}},
    {{80, 0, 0}, {0, 0, 100}, {0, 0, 100}, {0, 0, 100}, {80, 0, 0}},
    {{80, 0, 0}, {80, 0, 0}, {80, 0, 0}, {80, 0, 0}, {80, 0, 0}}
  };
  /* Desenhando Sprite contido na matriz */ 
  for(int linha = 0; linha < 5; linha++){
    for(int coluna = 0; coluna < 5; coluna++){
      int posicao = getIndex(linha, coluna);
      npSetLED(posicao, matriz[coluna][linha][0], matriz[coluna][linha][1], matriz[coluna][linha][2]);
    }
  }
  npWrite();        // Passando o conteúdo para os leds da matriz
  sleep_ms(8000);   // Delay
  npClear();        // Limpa os leds da matriz  (apaga todos)
  npWrite();        // Passando o conteúdo para os leds da matriz
}

/* Função de execução principal do sistema */
int main()
{
    stdio_init_all();   // Inicializa os tipos stdio padrão presentes ligados ao binário
    /* Chama as funções de configuração inicial */
    adc_init();
    conf_botao_a();
    conf_joystick();
    conf_i2c();
    conf_display();
    conf_led();
    conf_botao_b();
    npInit(LED_PIN);  // Inicializa a máquina PIO para a matriz de LEDs NeoPixel
    texto_inicial_display();
    /* Loop infinito que roda a execução do projeto */
    while(true) {
      /* Condição que verifica o momento de execução do código */
      /*
            start == 1, executa a primeira tela de lista de conteúdo e a opção selecionada
            start == 4, executa a segunda tela de lista de conteúdo e a opção selecionada
            start == 5, executa a tela inicial enquanto não o botão A não é pressionado
      */
      if(start == 1){
        gpio_set_irq_enabled_with_callback(BUTTON_A_PIN, GPIO_IRQ_EDGE_FALL, false, &button_A_irq_handler); // Habilita a interrupção do botão A
        memset(ssd, 0, ssd1306_buffer_length);    // Limpa o display
        render_on_display(ssd, &frame_area);      // Renderiza a informação no display
        sleep_ms(100);                            // Delay
        /* Texto a ser renderizado no display */
        char *conteudo[] = {
        "                ",
        "      ADC       ",
        "      LED       ",
        "     Botao      ",
        "     Buzzer     ",
        "     Display    ",
        "     Pagina 2   ",
        "           sair ",
        };
        desenho_da_string(conteudo);              // "Desenha" string a ser renderizada
        render_on_display(ssd, &frame_area);      // Renderiza a informação no display

        selecionar_conteudo();                    // Chamada da função para saber o conteúdo selecionado
        /* Condicional que chama a função do conteúdo selecionado */
        switch (conteudo_selecionado){
          case 1:
            conteudo_adc(); // Chama a função do conteúdo "ADC"
            start = 1;      // Atualização da variável de controle de execução das telas principais do display e de alguns loops
            break;
          case 2:
            conteudo_led(); // Chama a função do conteúdo "LED"
            start = 1;      // Atualização da variável de controle de execução das telas principais do display e de alguns loops
            break;
          case 3:
            conteudo_botao(); // Chama a função do conteúdo "BOTAO"
            start = 1;        // Atualização da variável de controle de execução das telas principais do display e de alguns loops
            break;
          case 4:
            conteudo_buzzer(); // Chama a função do conteúdo "BUZZER"
            start = 1;         // Atualização da variável de controle de execução das telas principais do display e de alguns loops
            break;
          case 5:
            conteudo_display(); // Chama a função do conteúdo "DISPLAY"
            start = 1;          // Atualização da variável de controle de execução das telas principais do display e de alguns loops
            break;
          case 6:
            /* Caso que passa para página 2 */
            start = 4;    // Atualização da variável de controle de execução das telas principais do display e de alguns loops
           break;
          case 7:
            /* Caso que sai da aplicação (volta para tela inicial) */
            start = 5;    // Atualização da variável de controle de execução das telas principais do display e de alguns loops
            break;
          default:
            break;
        }
        selecao = 0;
      }
      else if(start == 4){
        memset(ssd, 0, ssd1306_buffer_length);    // Limpa o display
        render_on_display(ssd, &frame_area);      // Renderiza a informação no display
        sleep_ms(100);                            // Delay
        /* Texto a ser renderizado no display */
        char *novo_conteudo[] = {
        "                ",
        "    Joystick    ",
        "  Matriz de LED ",
        "      PWM       ",
        "  Temporizador  ",
        "  Interrupcao   ",
        "      PIO       ",
        "           sair ",
        };
        desenho_da_string(novo_conteudo);         // "Desenha" string a ser renderizada
        render_on_display(ssd, &frame_area);      // Renderiza a informação no display

        selecionar_conteudo();                    // Chamada da função para saber o conteúdo selecionado
        /* Condicional que chama a função do conteúdo selecionado */
        switch (conteudo_selecionado){
          case 1:
            conteudo_joystick();        // Chama a função do conteúdo "JOYSTICK"
            break;
          case 2:
            conteudo_matriz_de_led();   // Chama a função do conteúdo "MATRIZ DE LED"
            break;
          case 3:
            conteudo_pwm();             // Chama a função do conteúdo "PWM"
            break;
          case 4:
            conteudo_temporizador();    // Chama a função do conteúdo "TEMPORIZADOR"
            break;
          case 5:
            conteudo_interrupcao();     // Chama a função do conteúdo "INTERRUPCAO"
            break;
          case 6:
            conteudo_pio();             // Chama a função do conteúdo "PIO"
            break;
          case 7:
            /* Caso que sai da aplicação (volta para tela inicial) */
            start = 5;  // Atualização da variável de controle de execução das telas principais do display e de alguns loops
            break;
          default:
            break;
        }
        selecao = 0;    // Atualização da variável que condiciona o loop do código enquanto o usuário não seleciona o conteúdo no display
        /* Condição que verifica de o usuário quer sair da aplicação */
        if(start != 5){
          start = 4;    // Atualização da variável de controle de execução das telas principais do display e de alguns loops
        }
      }
      else if(start == 5){
        texto_inicial_display();    // Chama a função da tela inicial
        /* Loop que mantém a execução do programa "parada" para que aguarde a interrupção do botão A, para iniciar a aplicação*/
        while(start == 5){
          sleep_ms(500);            // Delay
        }
        sleep_ms(5000);             // Delay
      }
    }
    return 0;
}

/* Configuração do botão A */
void conf_botao_a(){
  gpio_init(BUTTON_A_PIN);
  gpio_set_dir(BUTTON_A_PIN, GPIO_IN);
  gpio_pull_up(BUTTON_A_PIN);
  gpio_set_irq_enabled_with_callback(BUTTON_A_PIN, GPIO_IRQ_EDGE_FALL, true, &button_A_irq_handler);
}
/* Configuração do joystick */
void conf_joystick(){
  adc_gpio_init(EIXO_X_JOYSTICK_PIN);
  adc_gpio_init(EIXO_Y_JOYSTICK_PIN);
  gpio_init(BUTTON_JOYSTICK_PIN);
  gpio_set_dir(BUTTON_JOYSTICK_PIN, GPIO_IN);
  gpio_pull_up(BUTTON_JOYSTICK_PIN);
}
/* Configuração do sensor interno de temperatura */
void conf_temperatura(){
    adc_set_temp_sensor_enabled(true);
}
/* Configuração dos pinos I2C */
void conf_i2c(){
  i2c_init(i2c1, ssd1306_i2c_clock * 1000);
  gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
  gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
  gpio_pull_up(I2C_SDA);
  gpio_pull_up(I2C_SCL);
}
/* Configuração do display */
void conf_display(){
  ssd1306_init();
  calculate_render_area_buffer_length(&frame_area);
  memset(ssd, 0, ssd1306_buffer_length);
  render_on_display(ssd, &frame_area);
  restart:
}
/* Configuração do botão B */
void conf_botao_b(){
  gpio_init(BUTTON_B_PIN);
  gpio_set_dir(BUTTON_B_PIN, GPIO_IN);
  gpio_pull_up(BUTTON_B_PIN);
}