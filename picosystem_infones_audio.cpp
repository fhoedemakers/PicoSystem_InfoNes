#include <stdio.h>
#include <string.h>

#include "FrensHelpers.h"
#include <pico.h>
#include "pico/stdlib.h"

#include "hardware/pwm.h"

//audio for picosystem buzzer, and optional rx(gpio 1) / ground speaker mod.
//only supports buzzer or speaker in this version.
namespace picosystem{

    enum EAudioMode{
    BuzzerOnly = 0,
    SpeakerOnly = 1,
    Both = 2,
    None = 3
    };



 EAudioMode audioMode = BuzzerOnly;
 bool soundOn= false;
 bool mute = false;
const int SAMPLERATE = 390;
BYTE audiobuffer [SAMPLERATE];
int audiomode=0;
//int playbackrate = 100; //in ms
int samplecount = 0;
int cursample = 0;
int freq =1200;
int perframesamples = 60;
//int sampledelay = 1;
//int perframesamples_speakermod = 8;
uint32_t sfreq =1200;
int volume = 80;
int pin = 1 ;// rx = 1 , pin tx = 0 //cmake uart disabled, usb enabled.
int AUDIO = 11;

uint32_t         _audio_pwm_wrap = 10000;
uint32_t         _audio_pwm_wrap2 = 10000;//speaker
bool speakeron = false;
bool buzzeron = false;
   #ifndef NO_OVERCLOCK
      float clock = 250000000.0f;
    #else
      float clock = 125000000.0f;
    #endif

void initAudio()
{
    if (audioMode== EAudioMode::SpeakerOnly|| audioMode == EAudioMode::Both)
    {
        int audio_pwm_slice_number = pwm_gpio_to_slice_num(pin);
        pwm_config audio_pwm_cfg = pwm_get_default_config();
        pwm_init(audio_pwm_slice_number, &audio_pwm_cfg, true);
        gpio_set_function(pin, GPIO_FUNC_PWM);
        pwm_set_gpio_level(pin, 0);
        speakeron = true;
    }
    else if (speakeron)
    {
        //if mode changes and speaker was used, turn it off.
         pwm_set_gpio_level(pin, 0);
         speakeron=false;
    }
    if (audioMode== EAudioMode::BuzzerOnly|| audioMode == EAudioMode::Both)
    {
        int audio_pwm_slice_number = pwm_gpio_to_slice_num(AUDIO);
    pwm_config audio_pwm_cfg = pwm_get_default_config();
    pwm_init(audio_pwm_slice_number, &audio_pwm_cfg, true);
    gpio_set_function(AUDIO, GPIO_FUNC_PWM);

    pwm_set_gpio_level(AUDIO, 0);
buzzeron = true;
    }
    else if (buzzeron)
    {
        buzzeron= false;
        pwm_set_gpio_level(AUDIO, 0);
    }


soundOn = true;

}
void updatesoundmode(int newmode)
{
    audioMode= (EAudioMode)newmode;
    initAudio();
}
void stopAudio()
{
     if (audioMode== EAudioMode::SpeakerOnly|| audioMode == EAudioMode::Both)
    {

        pwm_set_gpio_level(pin, 0);

    }
    if (audioMode== EAudioMode::BuzzerOnly|| audioMode == EAudioMode::Both)
    {
     pwm_set_gpio_level(AUDIO, 0);
    }
}

 void setbuffer(int samples, BYTE *wave1, BYTE *wave2, BYTE *wave3, BYTE *wave4, BYTE *wave5)
{
//raw buffer
 if (samples)
    {
        for (int i = 0 ; i < samples; i++)
        {
            unsigned int incoming = wave1[i]  + wave2[i]  + wave3[i]  + wave4[i]  + wave5[i]/5;//overflow protection?

            audiobuffer[i] = incoming;
            //avg_wave[i]= wave1[i]  + wave2[i]/2;
            //avg_wave[i]*= freq/255;
            //avg_wave[i] = avg_wave[i]*4 % 2000;
            //avg_wave[i]*=freq/255;
            //avg_wave[i]= std::min(20000,avg_wave[i]);
            //if ( avg_wave[i] < 10 && avg_wave[i] > 0)
              //  avg_wave[i]= std::max ( avg_wave[i],64);
        }
        samplecount = samples;
        //soundon=true;
        cursample =0;
    }
}




void playsamples_buzzer()
{
   if (!mute )
   {
    uint32_t v = volume;
     uint32_t f=  audiobuffer[cursample]*freq /255;

    #ifndef NO_OVERCLOCK
      float clock = 250000000.0f;
    #else
      float clock = 125000000.0f;
    #endif

    float pwm_divider = clock / _audio_pwm_wrap / f;
    pwm_set_clkdiv(pwm_gpio_to_slice_num(AUDIO), pwm_divider);
    pwm_set_wrap(pwm_gpio_to_slice_num(AUDIO), _audio_pwm_wrap);

    // work out usable range of volumes at this frequency. the piezo speaker
    // isn't driven in a way that can control volume easily however if we're
    // clever with the duty cycle we can ensure that the ceramic doesn't have
    // time to fully deflect - effectively reducing the volume.
    //
    // through experiment it seems that constraining the deflection period of
    // the piezo to between 0 and 1/10000th of a second gives reasonable control
    // over the volume. the relationship is non linear so we also apply a
    // correction curve which is tuned so that the result sounds reasonable.
    uint32_t max_count = (f * _audio_pwm_wrap) / 10000;

    // the change in volume isn't linear - we correct for this here
    float curve = 1.8f;
    uint32_t level = (pow((float)(v) / 100.0f, curve) * max_count);
    pwm_set_gpio_level(AUDIO, level);
   }

}
void playsamples_speaker()
{
if (!mute )
   {
    uint32_t v = volume;
     uint32_t f=  audiobuffer[cursample]*sfreq /255;

    #ifndef NO_OVERCLOCK
      float clock = 250000000.0f;
    #else
      float clock = 125000000.0f;
    #endif

    float pwm_divider = clock / _audio_pwm_wrap2 / f;
    pwm_set_clkdiv(pwm_gpio_to_slice_num(pin), pwm_divider);
    pwm_set_wrap(pwm_gpio_to_slice_num(pin), _audio_pwm_wrap2);

    // work out usable range of volumes at this frequency. the piezo speaker
    // isn't driven in a way that can control volume easily however if we're
    // clever with the duty cycle we can ensure that the ceramic doesn't have
    // time to fully deflect - effectively reducing the volume.
    //
    // through experiment it seems that constraining the deflection period of
    // the piezo to between 0 and 1/10000th of a second gives reasonable control
    // over the volume. the relationship is non linear so we also apply a
    // correction curve which is tuned so that the result sounds reasonable.
    uint32_t max_count = (f * _audio_pwm_wrap2) / 10000;

    // the change in volume isn't linear - we correct for this here
    float curve = 1.8f;
    uint32_t level = (pow((float)(v) / 100.0f, curve) * max_count);
    pwm_set_gpio_level(pin, level);
   }

  }
 void playaudio()
{
    if(soundOn)
    {
        switch(audioMode)
        {
        case EAudioMode::BuzzerOnly:
            playsamples_buzzer();
            break;

        case EAudioMode::SpeakerOnly:
            playsamples_speaker();
            break;

        case EAudioMode::Both:
            playsamples_buzzer();
            playsamples_speaker();
            break;
        }

             cursample++;


    }
    else
    {
        stopAudio();
    }

}

}
