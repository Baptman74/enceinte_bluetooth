#include <stdio.h>
#include <stdlib.h>

#include "pico/stdlib.h"

#include "hardware/adc.h"
#include "hardware/dma.h"
#include "hardware/pwm.h"
#include "hardware/irq.h"

#define CAPTURE_CHANNEL 0
#define TAILLE_CAPTURE 5

#define PWM_LF 1
#define PWM_HF 2
#define CLKDIV 1

uint16_t buffer_entree[TAILLE_CAPTURE];
uint16_t hf_buffer[TAILLE_CAPTURE];
uint16_t lf_buffer[TAILLE_CAPTURE];

uint slice_num_lf;
uint slice_num_hf;

int capture;

int valchanged = 0;


//coefficients de filtres passe haut 2kHz et passe bas 2kHz
float hf_a0 = 0.9666074108551588;
float hf_a1 = -1.966296873007214;
float hf_a2 = 0.983148436503607;
float hf_b1 = 1.965986335159269;
float hf_b2 = -0.9666074108551588;

float lf_a0 = 0.000155268923972433;
float lf_a1 = 0.000310537847944866;
float lf_a2 = 0.000155268923972433;
float lf_b1 = 1.965986335159269;
float lf_b2 = -0.9666074108551588;


void proceed_transfer(){ //fonction déclenchée si le fifo a une valeur
  valchanged = 1;
}

int main(){
  stdio_init_all();
  adc_gpio_init(26+CAPTURE_CHANNEL);
  adc_init();
  adc_select_input(CAPTURE_CHANNEL);

  //ADC setup
  adc_fifo_setup(
    true, //ecris tout dans le FIFO
    true, //allume le dreq du DMA
    1, //IRQ quand 1 sample dans le FIFO
    false, //pas de bit d'erreur
    true //shift samples de 8bit dans le FIFO
  );

  adc_set_clkdiv(480); //48'000'000/480 = 100'000
  adc_irq_set_enabled(true);
  irq_set_exclusive_handler(ADC0_IRQ_FIFO,proceed_transfer);

  //PWM setup
  slice_num_hf = pwm_gpio_to_slice_num(PWM_HF);//recupère une slice
  gpio_set_function(PWM_HF, GPIO_FUNC_PWM);//defini la slice en PWM
  gpio_set_drive_strength(PWM_HF, GPIO_DRIVE_STRENGTH_12MA);//drive a puissance max
  gpio_set_slew_rate(PWM_HF, GPIO_SLEW_RATE_FAST);//slew rate (=temps de montée/descente) le plus rapide (/!\ peut ajouter de l'overshoot)
  pwm_set_clkdiv(slice_num_hf, CLKDIV);//diviseur de fréquence
  pwm_set_wrap(slice_num_hf, 0x3ff);//la rampe retourne a 0 a partir de 0x3ff (= 1023) -> résolution de 1024
  pwm_set_chan_level(slice_num_hf, PWM_CHAN_A, 0); //met le PWM a 0 pour le moment
  pwm_set_mask_enabled((1u << slice_num_hf)); //autorise plusieurs masques de PWM

  slice_num_lf = pwm_gpio_to_slice_num(PWM_LF);//recupère une slice
  gpio_set_function(PWM_LF, GPIO_FUNC_PWM);//defini la slice en PWM
  gpio_set_drive_strength(PWM_LF, GPIO_DRIVE_STRENGTH_12MA);//drive a puissance max
  gpio_set_slew_rate(PWM_LF, GPIO_SLEW_RATE_FAST);//slew rate (=temps de montée/descente) le plus rapide (/!\ peut ajouter de l'overshoot)
  pwm_set_clkdiv(slice_num_lf, CLKDIV);//diviseur de fréquence
  pwm_set_wrap(slice_num_lf, 0x3ff); //la rampe retourne a 0 a partir de 0x3ff (= 1023) -> résolution de 1024
  pwm_set_chan_level(slice_num_lf, PWM_CHAN_A, 0);//met le PWM a 0 pour le moment
  pwm_set_mask_enabled((1u << slice_num_lf));//autorise plusieurs masques de PWM

  //DMA setup
  int dma_capture = dma_claim_unused_channel(true); //récupère un canal DMA inutilisé
  dma_channel_config capture_conf = dma_channel_get_default_config(dma_capture); //récupère la config par défaut

  int dma_pwm_lf = dma_claim_unused_channel(true);//récupère un canal DMA inutilisé
  dma_channel_config pwm_lf_conf = dma_channel_get_default_config(dma_pwm_lf);//récupère la config par défaut

  int dma_pwm_hf = dma_claim_unused_channel(true);//récupère un canal DMA inutilisé
  dma_channel_config pwm_hf_conf = dma_channel_get_default_config(dma_pwm_hf);//récupère la config par défaut

  channel_config_set_transfer_data_size(&capture_conf, DMA_SIZE_16); //transfert deux octets par deux octets
  channel_config_set_read_increment(&capture_conf, false); //n'incrémente pas la case mémoire de lecture
  channel_config_set_write_increment(&capture_conf, false);//n'incrémente pas la case mémoire d'ecriture
  channel_config_set_chain_to(&capture_conf, dma_pwm_hf); //renvoie au pwm hf une fois le ping terminé
  channel_config_set_irq_quiet(&capture_conf, true); //interruption en mode silencieuse
  channel_config_set_dreq(&capture_conf, true); //active les data requests
  dma_channel_configure(
    dma_capture,//channel en train d'être config
    &capture_conf,//configuration
    &capture, //emplacement d'écriture
    &adc_hw->fifo, //emplacement de lecture
    3,//nombre de transfert
    false//démare tout de suite
  );

  channel_config_set_transfer_data_size(&pwm_hf_conf, DMA_SIZE_16); //transfert deux octets par deux octets
  channel_config_set_read_increment(&pwm_hf_conf, false);//n'incrémente pas la case mémoire de lecture
  channel_config_set_write_increment(&pwm_hf_conf, false);//n'incrémente pas la case mémoire d'écriture
  channel_config_set_chain_to(&pwm_hf_conf, dma_pwm_lf); //renvoie au pwm lf une fois le hf terminé
  channel_config_set_irq_quiet(&pwm_hf_conf, true); //interruption en mode silencieuse
  channel_config_set_dreq(&pwm_hf_conf, true);//active les data requests
  dma_channel_configure(
    dma_pwm_hf, //channel en train d'être config
    &pwm_hf_conf, //configuration
    &pwm_hw->slice[slice_num_hf].cc, //emplacement d'écriture
    &hf_buffer[0], // emplacement de lecture
    1, //nombre de transfert
    false //démare tout de suite
  );

  channel_config_set_transfer_data_size(&pwm_lf_conf, DMA_SIZE_16);//transfert deux octets par deux octets
  channel_config_set_read_increment(&pwm_lf_conf, false);//n'incrémente pas la case mémoire de lecture
  channel_config_set_write_increment(&pwm_lf_conf, false);//n'incrémente pas la case mémoire d'écriture
  channel_config_set_chain_to(&pwm_lf_conf, dma_capture); //renvoie a capture une fois le lf terminé
  channel_config_set_irq_quiet(&pwm_lf_conf, true);//interruption en mode silencieuse
  channel_config_set_dreq(&pwm_lf_conf, true);//active les data requests
  dma_channel_configure(
    dma_pwm_lf, //channel en train d'être config
    &pwm_lf_conf,//configuration
    &pwm_hw->slice[slice_num_lf].cc, //emplacement d'écriture
    &lf_buffer[0],// emplacement de lecture
    1,//nombre de transfert
    false //démare tout de suite
  );

  //adc and dma start
  dma_start_channel_mask(1u<<dma_capture);
  irq_set_enabled(ADC0_IRQ_FIFO,true); //active l'interruption sur l'ADC (si il y a une valeur dans l'adc, l'interruption est levée)
  adc_run(true);


  while (true){
    if (valchanged == 1){

      //table shifting
      for (int i = TAILLE_CAPTURE - 2; i >= 0;i--){
        buffer_entree[i+1] = buffer_entree[i];
      }
      //copie val
      buffer_entree[0] = capture>>2;

      for(int i = 0; i<TAILLE_CAPTURE-1;i++){
        hf_buffer[i] = buffer_entree[i];
        lf_buffer[i] = buffer_entree[i];
      }

      //filtrage
      hf_buffer[0] =(int)((hf_buffer[0] * hf_a0 + hf_buffer[1] * hf_a1 + hf_buffer[2] * hf_a2)+(hf_buffer[1] * hf_b1 + hf_buffer[2] * hf_b2));
      lf_buffer[0] =(int)((lf_buffer[0] * lf_a0 + lf_buffer[1] * lf_a1 + lf_buffer[2] * lf_a2)+(lf_buffer[1] * lf_b1 + lf_buffer[2] * lf_b2));

      //fin d'interruption
      valchanged = 0;
    }

  }
}
