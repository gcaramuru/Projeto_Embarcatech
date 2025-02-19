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
// Biblioteca gerada pelo arquivo .pio durante compilação.
#include "ws2818b.pio.h"

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
/* Definições da matriz de led */
#define LED_COUNT 25
#define LED_PIN 7
struct pixel_t {// Definição de pixel GRB
  uint8_t G, R, B; // Três valores de 8-bits compõem um pixel.
};
typedef struct pixel_t pixel_t;
typedef pixel_t npLED_t; // Mudança de nome de "struct pixel_t" para "npLED_t" por clareza.
npLED_t leds[LED_COUNT];// Declaração do buffer de pixels que formam a matriz.
PIO np_pio;// Variável para uso da máquina PIO.
uint sm;// Variável para uso da máquina PIO.
/* Definição do buzzer */
#define BUZZER_PIN 21

int start = 5;
bool selecao = 0;
int posicao_joystick = 0;  // 0 mantém parado, 1 desce, 2 sobe
int conteudo_selecionado = 0;
uint16_t eixo_x;
uint16_t eixo_y;
bool level_led = 0;
bool interrupcao = 0;

// Preparar área de renderização para o display (ssd1306_width pixels por ssd1306_n_pages páginas)
uint8_t ssd[ssd1306_buffer_length];
struct render_area frame_area = {
  start_column : 0,
  end_column : ssd1306_width - 1,
  start_page : 0,
  end_page : ssd1306_n_pages - 1
};

void conf_botao_a();
void conf_joystick();
void conf_i2c();
void conf_display();
void texto_inicial_display();
void conf_led();
void conf_botao_b();

static absolute_time_t last_button_a_press; // Variável de tempo que auxilia no debouncing do botão A
/* Função de interrupção do botão A, para alterar a frequência do led */
static void button_A_irq_handler(){
    absolute_time_t current_time = get_absolute_time(); // Atualização do tempo na variável
    /* Condicional que verifica se o botão A foi pressionado, evitando erros por falsa detecção */
    if (absolute_time_diff_us(last_button_a_press, current_time) > 100000) {
        last_button_a_press = current_time; // Atualização do tempo na variável
        
        memset(ssd, 0, ssd1306_buffer_length);
        render_on_display(ssd, &frame_area);
        
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
        int y = 0;
        for (uint i = 0; i < count_of(text_bot); i++)
        {
          ssd1306_draw_string(ssd, 5, y, text_bot[i]);
          y += 8;
        }
        render_on_display(ssd, &frame_area);

        start = 1;
    }
}

void texto_inicial_display(){
  gpio_set_irq_enabled_with_callback(BUTTON_A_PIN, GPIO_IRQ_EDGE_FALL, true, &button_A_irq_handler);
  memset(ssd, 0, ssd1306_buffer_length);
  render_on_display(ssd, &frame_area);

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

    int y = 0;
    for (uint i = 0; i < count_of(texto_inicial); i++)
    {
        ssd1306_draw_string(ssd, 5, y, texto_inicial[i]);
        y += 8;
    }
    render_on_display(ssd, &frame_area);
}

void leitura_joystick_callback(){
  /* Ler entradas do joystick */
  adc_select_input(ADC_EIXO_X_JOYSTICK_CHANNEL);
  sleep_us(2);
  eixo_x = adc_read();

  adc_select_input(ADC_EIXO_Y_JOYSTICK_CHANNEL);
  sleep_us(2);
  eixo_y = adc_read();

  if(eixo_x >= 2300){
    posicao_joystick = 2;
  }
  else if(eixo_x <= 1700){
    posicao_joystick = 1;
  }
  else{
    posicao_joystick = 0;
  }
}

static absolute_time_t last_button_selecao_press; // Variável de tempo que auxilia no debouncing do botão B
/* Função de interrupção do botão B, para alterar a frequência do led */
void button_selecao_irq_handler(){
    absolute_time_t current_time = get_absolute_time(); // Atualização do tempo na variável
    /* Condicional que verifica se o botão B foi pressionado, evitando erros por falsa detecção */
    if (absolute_time_diff_us(last_button_selecao_press, current_time) > 100000) {
      last_button_selecao_press = current_time; // Atualização do tempo na variável
      selecao = 1;
    }
}

void selecionar_conteudo(){
  struct repeating_timer timer;
  int posicao_I = 8;
  
  while(selecao == 0){
    // add_repeating_timer_ms(100, leitura_joystick_callback, NULL, &timer);
    leitura_joystick_callback();
    gpio_set_irq_enabled_with_callback(BUTTON_JOYSTICK_PIN, GPIO_IRQ_EDGE_FALL, true, &button_selecao_irq_handler);

    // printf("Posicao_joystick: %d\n", posicao_joystick);

    if(posicao_joystick == 0){
      ssd1306_draw_string(ssd, 5, posicao_I, "I");
      render_on_display(ssd, &frame_area);
    }
    else if(posicao_joystick == 1){
      ssd1306_draw_string(ssd, 5, posicao_I, " ");
      render_on_display(ssd, &frame_area);
      posicao_I += 8;
      if(posicao_I > 56){
        posicao_I = 8;
      }
      ssd1306_draw_string(ssd, 5, posicao_I, "I");
      render_on_display(ssd, &frame_area);
    }
    else if(posicao_joystick == 2){
      ssd1306_draw_string(ssd, 5, posicao_I, " ");
      render_on_display(ssd, &frame_area);
      posicao_I -= 8;
      if(posicao_I < 8){
        posicao_I = 56;
      }
      ssd1306_draw_string(ssd, 5, posicao_I, "I");
      render_on_display(ssd, &frame_area);
    }
    sleep_ms(200);
  }
  gpio_set_irq_enabled_with_callback(BUTTON_JOYSTICK_PIN, GPIO_IRQ_EDGE_FALL, false, &button_selecao_irq_handler);
  conteudo_selecionado = (posicao_I / 8);
}

void conf_led(){
  gpio_init(LED_AZUL);
  gpio_set_dir(LED_AZUL, GPIO_OUT);
  gpio_put(LED_AZUL, 0); 

  gpio_init(LED_VERDE);
  gpio_set_dir(LED_VERDE, GPIO_OUT);
  gpio_put(LED_VERDE, 0);

  gpio_init(LED_VERMELHO);
  gpio_set_dir(LED_VERMELHO, GPIO_OUT);
  gpio_put(LED_VERMELHO, 0);
}

void conteudo_led(){
  memset(ssd, 0, ssd1306_buffer_length);
  render_on_display(ssd, &frame_area);

  char *texto_led[] = {
    "                ",
    "LED eh um       ",
    "diodo e ao      ",
    "aplicar tensao  ",
    "mandando 1      ",
    "com gpio_put    ",
    "acendemos o led ",
    "                ",
    };

    int y = 0;
    for (uint i = 0; i < count_of(texto_led); i++)
    {
        ssd1306_draw_string(ssd, 5, y, texto_led[i]);
        y += 8;
    }
    render_on_display(ssd, &frame_area);

    conf_led();
    gpio_put(LED_AZUL, 1);
    sleep_ms(6000);
    gpio_put(LED_AZUL,0);
}

/* Função de conversão do dado lido no canal adc para temperatura */
float temperatura() {
  adc_select_input(ADC_TEMPERATURE_CHANNEL);
  uint16_t temp = adc_read();
  const float conversion_factor = 3.3f / (1 << 12);
  float voltage = temp * conversion_factor;    
  float celsius = 27.0f - (voltage - 0.706f) / 0.001721f; 
  return celsius;
}

void conteudo_adc(){
  memset(ssd, 0, ssd1306_buffer_length);
  render_on_display(ssd, &frame_area);

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

  int y = 0;
  for (uint i = 0; i < count_of(texto_adc); i++)
  {
      ssd1306_draw_string(ssd, 5, y, texto_adc[i]);
      y += 8;
  }
  render_on_display(ssd, &frame_area);
  sleep_ms(10000);

  float temp = temperatura();
  char tempC[16];
  snprintf(tempC, sizeof(tempC), "    %.2f C", temp);

  memset(ssd, 0, ssd1306_buffer_length);
  render_on_display(ssd, &frame_area);

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

  y = 0;
  for (uint i = 0; i < count_of(texto__exemplo_adc); i++)
  {
      ssd1306_draw_string(ssd, 5, y, texto__exemplo_adc[i]);
      y += 8;
  }
  render_on_display(ssd, &frame_area);
  sleep_ms(6000);

}

static absolute_time_t last_button_b_press; // Variável de tempo que auxilia no debouncing do botão B
/* Função de interrupção do botão B, para alterar a frequência do led */
void button_B_irq_handler(){
    absolute_time_t current_time = get_absolute_time(); // Atualização do tempo na variável
    /* Condicional que verifica se o botão B foi pressionado, evitando erros por falsa detecção */
    if (absolute_time_diff_us(last_button_b_press, current_time) > 100000) {
      last_button_b_press = current_time; // Atualização do tempo na variável
      gpio_put(LED_VERDE, 0);  
      start = 3;
    }
}

void conteudo_botao(){
  memset(ssd, 0, ssd1306_buffer_length);
  render_on_display(ssd, &frame_area);

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

  int y = 0;
  for (uint i = 0; i < count_of(texto_botao); i++)
  {
      ssd1306_draw_string(ssd, 5, y, texto_botao[i]);
      y += 8;
  }
  render_on_display(ssd, &frame_area);
  sleep_ms(10000);

  memset(ssd, 0, ssd1306_buffer_length);
  render_on_display(ssd, &frame_area);

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

  y = 0;
  for (uint i = 0; i < count_of(texto_exemplo_botao); i++)
  {
      ssd1306_draw_string(ssd, 5, y, texto_exemplo_botao[i]);
      y += 8;
  }
  render_on_display(ssd, &frame_area);
  
  conf_led();
  gpio_put(LED_VERDE, 1);
  gpio_set_irq_enabled_with_callback(BUTTON_B_PIN, GPIO_IRQ_EDGE_FALL, true, &button_B_irq_handler);
  while (start != 3)
  {
    printf("start: %d\n",start);
    sleep_ms(300);
  }
  gpio_set_irq_enabled_with_callback(BUTTON_B_PIN, GPIO_IRQ_EDGE_FALL, false, &button_B_irq_handler);
}

// Notas musicais para a música tema de Star Wars
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
const uint note_duration[] = {
  500, 500, 500, 350, 150, 300, 500, 350,
  150, 300, 500, 500, 500, 500, 350, 150,
  300, 500, 500, 350, 150, 300, 500, 350,
  150, 300, 650, 500, 150, 300, 500, 350,
  150, 300, 500, 150, 300, 500, 350, 150,
  300, 650, 500, 350, 150, 300, 500, 350,
  150, 300, 500, 500, 500, 500, 350, 150,
  300, 500, 500, 350, 150, 300, 500, 350,
  150, 300, 500, 350, 150, 300, 500, 500,
  350, 150, 300, 500, 500, 350, 150, 300,
};
// Inicializa o PWM no pino do buzzer
void pwm_init_buzzer(uint pin) {
  gpio_set_function(pin, GPIO_FUNC_PWM);
  uint slice_num = pwm_gpio_to_slice_num(pin);
  pwm_config config = pwm_get_default_config();
  pwm_config_set_clkdiv(&config, 4.0f); // Ajusta divisor de clock
  pwm_init(slice_num, &config, true);
  pwm_set_gpio_level(pin, 0); // Desliga o PWM inicialmente
}

// Toca uma nota com a frequência e duração especificadas
void play_tone(uint pin, uint frequency, uint duration_ms) {
  uint slice_num = pwm_gpio_to_slice_num(pin);
  uint32_t clock_freq = clock_get_hz(clk_sys);
  uint32_t top = clock_freq / frequency - 1;

  pwm_set_wrap(slice_num, top);
  pwm_set_gpio_level(pin, top / 2); // 50% de duty cycle

  sleep_ms(duration_ms);

  pwm_set_gpio_level(pin, 0); // Desliga o som após a duração
  sleep_ms(50); // Pausa entre notas
}

// Função principal para tocar a música
void play_star_wars(uint pin) {
  for (int i = 0; i < sizeof(star_wars_notes) / sizeof(star_wars_notes[0]); i++) {
      if (star_wars_notes[i] == 0) {
          sleep_ms(note_duration[i]);
      } else {
          play_tone(pin, star_wars_notes[i], note_duration[i]);
      }
  }
}

void conteudo_buzzer(){
  memset(ssd, 0, ssd1306_buffer_length);
  render_on_display(ssd, &frame_area);

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

  int y = 0;
  for (uint i = 0; i < count_of(texto_buzzer); i++)
  {
      ssd1306_draw_string(ssd, 5, y, texto_buzzer[i]);
      y += 8;
  }
  render_on_display(ssd, &frame_area);

  pwm_init_buzzer(BUZZER_PIN);
  play_star_wars(BUZZER_PIN);

  sleep_ms(6000);
}

void conteudo_display(){
  memset(ssd, 0, ssd1306_buffer_length);
  render_on_display(ssd, &frame_area);

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

  int y = 0;
  for (uint i = 0; i < count_of(texto_display); i++)
  {
      ssd1306_draw_string(ssd, 5, y, texto_display[i]);
      y += 8;
  }
  render_on_display(ssd, &frame_area);
  sleep_ms(6000);
}

static absolute_time_t last_button_joystick_press; // Variável de tempo que auxilia no debouncing do botão do joystick
/* Função de interrupção do botão do joystick*/
void button_joystick_irq_handler(){
    absolute_time_t current_time = get_absolute_time(); // Atualização do tempo na variável
    /* Condicional que verifica se o botão do joystick foi pressionado, evitando erros por falsa detecção */
    if (absolute_time_diff_us(last_button_joystick_press, current_time) > 100000) {
      last_button_joystick_press = current_time; // Atualização do tempo na variável
      pwm_set_gpio_level(LED_VERDE, 0);
      pwm_set_gpio_level(LED_AZUL, 0);
      pwm_set_gpio_level(LED_VERMELHO, 0);
      start = 3;
    }
}

void setup_pwm(){
  uint slice1, slice2, slice3;
  gpio_set_function(LED_VERDE, GPIO_FUNC_PWM);
  slice1 = pwm_gpio_to_slice_num(LED_VERDE);
  pwm_set_clkdiv(slice1, DIVIDER_PWM);
  pwm_set_wrap(slice1, PERIOD);
  pwm_set_gpio_level(LED_VERDE, 0);
  pwm_set_enabled(slice1, true);

  
  gpio_set_function(LED_AZUL, GPIO_FUNC_PWM);
  slice2 = pwm_gpio_to_slice_num(LED_AZUL);
  pwm_set_clkdiv(slice2, DIVIDER_PWM);
  pwm_set_wrap(slice2, PERIOD);
  pwm_set_gpio_level(LED_AZUL, 0);
  pwm_set_enabled(slice2, true);

  gpio_set_function(LED_VERMELHO, GPIO_FUNC_PWM);
  slice3 = pwm_gpio_to_slice_num(LED_VERMELHO);
  pwm_set_clkdiv(slice3, DIVIDER_PWM);
  pwm_set_wrap(slice3, PERIOD);
  pwm_set_gpio_level(LED_VERMELHO, 0);
  pwm_set_enabled(slice3, true);
}

void conteudo_joystick(){
  memset(ssd, 0, ssd1306_buffer_length);
  render_on_display(ssd, &frame_area);

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

  int y = 0;
  for (uint i = 0; i < count_of(texto_joystick); i++)
  {
      ssd1306_draw_string(ssd, 5, y, texto_joystick[i]);
      y += 8;
  }
  render_on_display(ssd, &frame_area);
  sleep_ms(10000);
  memset(ssd, 0, ssd1306_buffer_length);
  render_on_display(ssd, &frame_area);

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

  y = 0;
  for (uint i = 0; i < count_of(texto_exemplo_joystick); i++)
  {
      ssd1306_draw_string(ssd, 5, y, texto_exemplo_joystick[i]);
      y += 8;
  }
  render_on_display(ssd, &frame_area);
  sleep_ms(500);

  setup_pwm();
  gpio_set_irq_enabled_with_callback(BUTTON_JOYSTICK_PIN, GPIO_IRQ_EDGE_FALL, true, &button_joystick_irq_handler);
  while(start != 3){
    leitura_joystick_callback();
    pwm_set_gpio_level(LED_VERDE, eixo_y);
    pwm_set_gpio_level(LED_AZUL, eixo_x);
    pwm_set_gpio_level(LED_VERMELHO, eixo_x);
    sleep_ms(300);
  }
  gpio_set_irq_enabled_with_callback(BUTTON_JOYSTICK_PIN, GPIO_IRQ_EDGE_FALL, false, &button_joystick_irq_handler);
}

/* Inicializa a máquina PIO para controle da matriz de LEDs */
void npInit(uint pin) {

  // Cria programa PIO.
  uint offset = pio_add_program(pio0, &ws2818b_program);
  np_pio = pio0;

  // Toma posse de uma máquina PIO.
  sm = pio_claim_unused_sm(np_pio, false);
  if (sm < 0) {
    np_pio = pio1;
    sm = pio_claim_unused_sm(np_pio, true); // Se nenhuma máquina estiver livre, panic!
  }

  // Inicia programa na máquina PIO obtida.
  ws2818b_program_init(np_pio, sm, offset, pin, 800000.f);

  // Limpa buffer de pixels.
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
  // Escreve cada dado de 8-bits dos pixels em sequência no buffer da máquina PIO.
  for (uint i = 0; i < LED_COUNT; ++i) {
    pio_sm_put_blocking(np_pio, sm, leds[i].G);
    pio_sm_put_blocking(np_pio, sm, leds[i].R);
    pio_sm_put_blocking(np_pio, sm, leds[i].B);
  }
  sleep_us(100); // Espera 100us, sinal de RESET do datasheet.
}

// Função para converter a posição do matriz para uma posição do vetor.
int getIndex(int x, int y) {
  // Se a linha for par (0, 2, 4), percorremos da esquerda para a direita.
  // Se a linha for ímpar (1, 3), percorremos da direita para a esquerda.
  if (y % 2 == 0) {
      return 24-(y * 5 + x); // Linha par (esquerda para direita).
  } else {
      return 24-(y * 5 + (4 - x)); // Linha ímpar (direita para esquerda).
  }
}

void conteudo_matriz_de_led(){
  memset(ssd, 0, ssd1306_buffer_length);
  render_on_display(ssd, &frame_area);

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

  int y = 0;
  for (uint i = 0; i < count_of(texto_matriz_de_led); i++)
  {
      ssd1306_draw_string(ssd, 5, y, texto_matriz_de_led[i]);
      y += 8;
  }
  render_on_display(ssd, &frame_area);
  // Inicializa matriz de LEDs NeoPixel.
  npInit(LED_PIN);
  npClear();
  // Matriz de desenho dos leds
  int matriz[5][5][3] = {
    {{255, 0, 0}, {255, 0, 0}, {255, 0, 0}, {255, 0, 0}, {255, 0, 0}},
    {{255, 0, 0}, {0, 255, 0}, {255, 20, 150}, {0, 255, 0}, {255, 0, 0}},
    {{255, 0, 0}, {255, 20, 150}, {255, 255, 255}, {255, 20, 150}, {0, 0, 255}},
    {{0, 0, 255}, {0, 255, 0}, {255, 20, 150}, {0, 255, 0}, {0, 0, 255}},
    {{0, 0, 255}, {0, 0, 255}, {0, 0, 255}, {0, 0, 255}, {0, 0, 255}}
  };
  // Desenhando Sprite contido na matriz
  for(int linha = 0; linha < 5; linha++){
    for(int coluna = 0; coluna < 5; coluna++){
      int posicao = getIndex(linha, coluna);
      npSetLED(posicao, matriz[coluna][linha][0], matriz[coluna][linha][1], matriz[coluna][linha][2]);
    }
  }

  npWrite();
  sleep_ms(6000);
  npClear();
  npWrite();
}

void conteudo_pwm(){
  memset(ssd, 0, ssd1306_buffer_length);
  render_on_display(ssd, &frame_area);

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

  int y = 0;
  for (uint i = 0; i < count_of(texto_pwm); i++)
  {
      ssd1306_draw_string(ssd, 5, y, texto_pwm[i]);
      y += 8;
  }
  render_on_display(ssd, &frame_area);

  setup_pwm();

  int pwm_level = 0;
  absolute_time_t initial_time = get_absolute_time();
  absolute_time_t current_time = get_absolute_time(); 
  while (absolute_time_diff_us(initial_time, current_time) < 10000000) {
    current_time = get_absolute_time();
    pwm_set_gpio_level(LED_VERDE, pwm_level);
    pwm_level += 38;
    sleep_ms(100);
  }
  pwm_set_gpio_level(LED_VERDE, 0);
}

bool callback_temporizador(){
    conf_led();
    gpio_put(LED_VERMELHO, level_led);
    level_led = !level_led;
}

void conteudo_temporizador(){
  memset(ssd, 0, ssd1306_buffer_length);
  render_on_display(ssd, &frame_area);

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

  int y = 0;
  for (uint i = 0; i < count_of(texto_temporizador); i++)
  {
      ssd1306_draw_string(ssd, 5, y, texto_temporizador[i]);
      y += 8;
  }
  render_on_display(ssd, &frame_area);
  sleep_ms(10000);

  memset(ssd, 0, ssd1306_buffer_length);
  render_on_display(ssd, &frame_area);

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

  y = 0;
  for (uint i = 0; i < count_of(texto_exemplo_temporizador); i++)
  {
      ssd1306_draw_string(ssd, 5, y, texto_exemplo_temporizador[i]);
      y += 8;
  }
  render_on_display(ssd, &frame_area);

  struct repeating_timer timer;
  add_repeating_timer_ms(200, callback_temporizador, NULL, &timer);
  absolute_time_t initial_time = get_absolute_time();
  absolute_time_t current_time = get_absolute_time(); 
  while (absolute_time_diff_us(initial_time, current_time) < 10000000) {
    current_time = get_absolute_time();
    sleep_ms(100);
  }
  gpio_put(LED_VERMELHO, 0);
  cancel_repeating_timer(&timer);
}

static absolute_time_t time_interrupcao; // Variável de tempo que auxilia no debouncing do botão do joystick
void interrupcao_irq_handler(){
    absolute_time_t current_time = get_absolute_time(); // Atualização do tempo na variável
    if (absolute_time_diff_us(time_interrupcao, current_time) > 100000) {
        time_interrupcao = current_time; // Atualização do tempo na variável
        interrupcao = 1;
    }
    time_interrupcao = get_absolute_time();
    current_time = get_absolute_time();
    while(absolute_time_diff_us(time_interrupcao, current_time) < 5000000){
        current_time = get_absolute_time();
        gpio_put(LED_VERMELHO, 1);
        pwm_set_gpio_level(BUZZER_PIN, 0);
    }
    gpio_put(LED_VERMELHO, 0);
}

void conteudo_interrupcao(){
  memset(ssd, 0, ssd1306_buffer_length);
  render_on_display(ssd, &frame_area);

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

  int y = 0;
  for (uint i = 0; i < count_of(texto_interrupcao); i++)
  {
      ssd1306_draw_string(ssd, 5, y, texto_interrupcao[i]);
      y += 8;
  }
  render_on_display(ssd, &frame_area);
  sleep_ms(10000);

  memset(ssd, 0, ssd1306_buffer_length);
  render_on_display(ssd, &frame_area);

  char *texto_exemplo_interrupcao[] = {
    "Por exemplo     ",
    "interromper o   ",
    "buzzer para     ",
    "acender o led   ",
    "usando o        ",
    "botao B         ",
    "                ",
    };

  y = 0;
  for (uint i = 0; i < count_of(texto_exemplo_interrupcao); i++)
  {
      ssd1306_draw_string(ssd, 5, y, texto_exemplo_interrupcao[i]);
      y += 8;
  }
  render_on_display(ssd, &frame_area);

  gpio_set_irq_enabled_with_callback(BUTTON_B_PIN, GPIO_IRQ_EDGE_FALL, true, &interrupcao_irq_handler);
  pwm_init_buzzer(BUZZER_PIN);
  while(interrupcao == 0){
    pwm_set_gpio_level(BUZZER_PIN, 200);
  }
  gpio_set_irq_enabled_with_callback(BUTTON_B_PIN, GPIO_IRQ_EDGE_FALL, false, &interrupcao_irq_handler);
}

void conteudo_pio(){
  memset(ssd, 0, ssd1306_buffer_length);
  render_on_display(ssd, &frame_area);

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

  int y = 0;
  for (uint i = 0; i < count_of(texto_pio); i++)
  {
      ssd1306_draw_string(ssd, 5, y, texto_pio[i]);
      y += 8;
  }
  render_on_display(ssd, &frame_area);
  sleep_ms(10000);
}

int main()
{
    stdio_init_all();   // Inicializa os tipos stdio padrão presentes ligados ao binário
    
    adc_init();
    conf_botao_a();
    conf_joystick();
    conf_i2c();
    conf_display();
    texto_inicial_display();
    conf_led();
    conf_botao_b();

    while(true) {

      if(start == 1){
        gpio_set_irq_enabled_with_callback(BUTTON_A_PIN, GPIO_IRQ_EDGE_FALL, false, &button_A_irq_handler);
        // sleep_ms(4000);
        memset(ssd, 0, ssd1306_buffer_length);
        render_on_display(ssd, &frame_area);
        sleep_ms(100);

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
        int y = 0;
        for (uint i = 0; i < count_of(conteudo); i++)
        {
          ssd1306_draw_string(ssd, 5, y, conteudo[i]);
          y += 8;
        }
        render_on_display(ssd, &frame_area);

        selecionar_conteudo();

        switch (conteudo_selecionado){
          case 1:
            conteudo_adc();
            printf("caso 1\n");
            selecao = 0;
            start = 0;
            break;
          case 2:
            conteudo_led();
            printf("caso 2\n");
            selecao = 0;
            start = 0;
            break;
          case 3:
            conteudo_botao();
            printf("caso 3\n");
            selecao = 0;
            start = 0;
            break;
          case 4:
            conteudo_buzzer();
            printf("caso 4\n");
            selecao = 0;
            start = 0;
            break;
          case 5:
            conteudo_display();
            printf("caso 5\n");
            selecao = 0;
            start = 0;
            break;
          case 6:
            //pagina 2
            printf("caso 6\n");
            selecao = 0;
            start = 4;
           break;
          case 7:
            printf("caso 7\n");
            selecao = 0;
            start = 5;
            break;
        default:
          break;
        }
      }
      else if(start == 0){
        gpio_set_irq_enabled_with_callback(BUTTON_B_PIN, GPIO_IRQ_EDGE_FALL, false, &button_B_irq_handler);
        start = 1;
      }
      else if(start == 4){
        memset(ssd, 0, ssd1306_buffer_length);
        render_on_display(ssd, &frame_area);
        sleep_ms(100);

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
        int y = 0;
        for (uint i = 0; i < count_of(novo_conteudo); i++)
        {
          ssd1306_draw_string(ssd, 5, y, novo_conteudo[i]);
          y += 8;
        }
        render_on_display(ssd, &frame_area);

        selecionar_conteudo();

        switch (conteudo_selecionado){
          case 1:
            conteudo_joystick();
            printf("caso 1\n");
            selecao = 0;
            break;
          case 2:
            conteudo_matriz_de_led();
            printf("caso 2\n");
            selecao = 0;
            break;
          case 3:
            conteudo_pwm();
            printf("caso 3\n");
            selecao = 0;
            break;
          case 4:
            conteudo_temporizador();
            printf("caso 4\n");
            selecao = 0;
            break;
          case 5:
            conteudo_interrupcao();
            printf("caso 5\n");
            selecao = 0;
            break;
          case 6:
            conteudo_pio();
            printf("caso 6\n");
            selecao = 0;
            break;
          case 7:
            printf("caso 7\n");
            selecao = 0;
            start = 5;
            break;
          default:
            break;
        }
        if(start != 5){
          start = 4;
        }
      }
      else if(start == 5){
        texto_inicial_display();
        while(start == 5){
          sleep_ms(500);
        }
        sleep_ms(4000);
      }
    }

    return 0;
}

void conf_botao_a(){
  /* Inicialização do pino do Botão A */
  gpio_init(BUTTON_A_PIN);
  gpio_set_dir(BUTTON_A_PIN, GPIO_IN);
  gpio_pull_up(BUTTON_A_PIN);
  gpio_set_irq_enabled_with_callback(BUTTON_A_PIN, GPIO_IRQ_EDGE_FALL, true, &button_A_irq_handler);
}

void conf_joystick(){
  /* Inicialização e configuração do canal ADC para o joystick*/
  adc_gpio_init(EIXO_X_JOYSTICK_PIN);
  adc_gpio_init(EIXO_Y_JOYSTICK_PIN);
  gpio_init(BUTTON_JOYSTICK_PIN);
  gpio_set_dir(BUTTON_JOYSTICK_PIN, GPIO_IN);
  gpio_pull_up(BUTTON_JOYSTICK_PIN);
}

void conf_temperatura(){
  /* Inicialização e configuração do canal ADC para a temperatura*/
    adc_set_temp_sensor_enabled(true);
}

void conf_i2c(){
  // Inicialização do i2c
  i2c_init(i2c1, ssd1306_i2c_clock * 1000);
  gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
  gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
  gpio_pull_up(I2C_SDA);
  gpio_pull_up(I2C_SCL);
}

void conf_display(){
  // Processo de inicialização completo do OLED SSD1306
  ssd1306_init();
  calculate_render_area_buffer_length(&frame_area);

  // zera o display inteiro
  memset(ssd, 0, ssd1306_buffer_length);
  render_on_display(ssd, &frame_area);

  restart:
}

void conf_botao_b(){
  /* Inicialização do pino do Botão B */
  gpio_init(BUTTON_B_PIN);
  gpio_set_dir(BUTTON_B_PIN, GPIO_IN);
  gpio_pull_up(BUTTON_B_PIN);
}