#include <Arduino.h>
#include "SD_MMC.h"
#include "driver/i2s.h"
#include "vorbisDecoder.h"



// Digital I/O used
#ifdef CONFIG_IDF_TARGET_ESP32S3
    #define SD_MMC_D0     11
    #define SD_MMC_CLK    13
    #define SD_MMC_CMD    14
    #define I2S_DOUT       9
    #define I2S_BCLK       3
    #define I2S_LRC        1
#endif

#ifdef CONFIG_IDF_TARGET_ESP32
    #define SD_MMC_D0      2
    #define SD_MMC_CLK    14
    #define SD_MMC_CMD    15
    #define I2S_DOUT      25
    #define I2S_BCLK      27
    #define I2S_LRC       26
#endif

uint8_t             m_i2s_num = I2S_NUM_0;          // I2S_NUM_0 or I2S_NUM_1
i2s_config_t        m_i2s_config;                   // stores values for I2S driver
i2s_pin_config_t    m_pin_config;
uint32_t            m_sampleRate=16000;
uint8_t             m_bitsPerSample = 16;           // bitsPerSample
uint8_t             m_vol=64;                       // volume
size_t              m_i2s_bytesWritten = 0;         // set in i2s_write() but not used
uint8_t             m_channels=2;
int16_t             m_outBuff[2048*2];              // Interleaved L/R
int16_t             m_validSamples = 0;
int16_t             m_curSample = 0;
bool                m_f_forceMono = false;
bool                m_f_isPlaying = false;

typedef enum { LEFTCHANNEL=0, RIGHTCHANNEL=1 } SampleIndex;

const uint8_t volumetable[22]={   0,  1,  2,  3,  4 , 6 , 8, 10, 12, 14, 17,
                                 20, 23, 27, 30 ,34, 38, 43 ,48, 52, 58, 64}; //22 elements

TaskHandle_t opus_task;
File file;

// prototypes
bool playSample(int16_t sample[2]);

//---------------------------------------------------------------------------------------------------------------------
//        I 2 S   S t u f f
//---------------------------------------------------------------------------------------------------------------------
void setupI2S(){
    m_i2s_num = I2S_NUM_0; // i2s port number
    m_i2s_config.mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    m_i2s_config.sample_rate          = 16000;
    m_i2s_config.bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT;
    m_i2s_config.channel_format       = I2S_CHANNEL_FMT_RIGHT_LEFT;
    m_i2s_config.communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_STAND_I2S);
    m_i2s_config.intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1; // high interrupt priority
    m_i2s_config.dma_buf_count        = 8;      // max buffers
    m_i2s_config.dma_buf_len          = 1024;   // max value
    m_i2s_config.tx_desc_auto_clear   = true;   // new in V1.0.1
    m_i2s_config.fixed_mclk           = I2S_PIN_NO_CHANGE;
    i2s_driver_install((i2s_port_t)m_i2s_num, &m_i2s_config, 0, NULL);
}
//---------------------------------------------------------------------------------------------------------------------
esp_err_t I2Sstart(uint8_t i2s_num) {
    // It is not necessary to call this function after i2s_driver_install() (it is started automatically),
    // however it is necessary to call it after i2s_stop()
    return i2s_start((i2s_port_t) i2s_num);
}
//---------------------------------------------------------------------------------------------------------------------
esp_err_t I2Sstop(uint8_t i2s_num) {
    return i2s_stop((i2s_port_t) i2s_num);
}
//---------------------------------------------------------------------------------------------------------------------
bool setPinout(uint8_t BCLK, uint8_t LRC, uint8_t DOUT, int8_t DIN) {

    m_pin_config.bck_io_num   = BCLK;
    m_pin_config.ws_io_num    = LRC; //  wclk
    m_pin_config.data_out_num = DOUT;
    m_pin_config.data_in_num  = DIN;
    const esp_err_t result = i2s_set_pin((i2s_port_t) m_i2s_num, &m_pin_config);
    return (result == ESP_OK);
}
//---------------------------------------------------------------------------------------------------------------------
bool setSampleRate(uint32_t sampRate) {
    i2s_set_sample_rates((i2s_port_t)m_i2s_num, sampRate);
    m_sampleRate = sampRate;
    return true;
}
//---------------------------------------------------------------------------------------------------------------------
bool setBitsPerSample(int bits) {
    if((bits != 16) && (bits != 8)) return false;
    m_bitsPerSample = bits;
    return true;
}
uint8_t getBitsPerSample(){
    return m_bitsPerSample;
}
//---------------------------------------------------------------------------------------------------------------------
bool setChannels(int ch) {
    if((ch < 1) || (ch > 2)) return false;
    m_channels = ch;
    return true;
}
uint8_t getChannels(){
    return m_channels;
}
//---------------------------------------------------------------------------------------------------------------------
int32_t Gain(int16_t s[2]) {
    int32_t v[2];
//    float step = (float)m_vol /64;
    uint8_t l = 0, r = 0;

    v[LEFTCHANNEL] = (s[LEFTCHANNEL]  * (m_vol - l)) >> 6;
    v[RIGHTCHANNEL]= (s[RIGHTCHANNEL] * (m_vol - r)) >> 6;

    return (v[RIGHTCHANNEL] << 16) | (v[LEFTCHANNEL] & 0xffff);
}
//---------------------------------------------------------------------------------------------------------------------
bool playChunk() {
    // If we've got data, try and pump it out..
    int16_t sample[2];
    if(getBitsPerSample() == 8) {
        if(m_channels == 1) {
            while(m_validSamples) {
                uint8_t x =  m_outBuff[m_curSample] & 0x00FF;
                uint8_t y = (m_outBuff[m_curSample] & 0xFF00) >> 8;
                sample[LEFTCHANNEL]  = x;
                sample[RIGHTCHANNEL] = x;
                while(1) {
                    if(playSample(sample)) break;
                } // Can't send?
                sample[LEFTCHANNEL]  = y;
                sample[RIGHTCHANNEL] = y;
                while(1) {
                    if(playSample(sample)) break;
                } // Can't send?
                m_validSamples--;
                m_curSample++;
            }
        }
        if(m_channels == 2) {
            while(m_validSamples) {
                uint8_t x =  m_outBuff[m_curSample] & 0x00FF;
                uint8_t y = (m_outBuff[m_curSample] & 0xFF00) >> 8;
                if(!m_f_forceMono) { // stereo mode
                    sample[LEFTCHANNEL]  = x;
                    sample[RIGHTCHANNEL] = y;
                }
                else { // force mono
                    uint8_t xy = (x + y) / 2;
                    sample[LEFTCHANNEL]  = xy;
                    sample[RIGHTCHANNEL] = xy;
                }

                while(1) {
                    if(playSample(sample)) break;
                } // Can't send?
                m_validSamples--;
                m_curSample++;
            }
        }
        m_curSample = 0;
        return true;
    }
    if(getBitsPerSample() == 16) {
        if(m_channels == 1) {
            while(m_validSamples) {
                sample[LEFTCHANNEL]  = m_outBuff[m_curSample];
                sample[RIGHTCHANNEL] = m_outBuff[m_curSample];
                if(!playSample(sample)) {
                    return false;
                } // Can't send
                m_validSamples--;
                m_curSample++;
            }
        }
        if(m_channels == 2) {
            while(m_validSamples) {
                if(!m_f_forceMono) { // stereo mode
                    sample[LEFTCHANNEL]  = m_outBuff[m_curSample * 2];
                    sample[RIGHTCHANNEL] = m_outBuff[m_curSample * 2 + 1];
                }
                else { // mono mode, #100
                    int16_t xy = (m_outBuff[m_curSample * 2] + m_outBuff[m_curSample * 2 + 1]) / 2;
                    sample[LEFTCHANNEL] = xy;
                    sample[RIGHTCHANNEL] = xy;
                }
                if(!playSample(sample)) {
                    return false;
                } // Can't send
                m_validSamples--;
                m_curSample++;
            }
        }
        m_curSample = 0;
        return true;
    }
    log_e("BitsPer Sample must be 8 or 16!");
    return false;
}
//---------------------------------------------------------------------------------------------------------------------
bool playSample(int16_t sample[2]) {

    if (getBitsPerSample() == 8) { // Upsample from unsigned 8 bits to signed 16 bits
        sample[LEFTCHANNEL]  = ((sample[LEFTCHANNEL]  & 0xff) -128) << 8;
        sample[RIGHTCHANNEL] = ((sample[RIGHTCHANNEL] & 0xff) -128) << 8;
    }

    sample[LEFTCHANNEL]  = sample[LEFTCHANNEL]  >> 1; // half Vin so we can boost up to 6dB in filters
    sample[RIGHTCHANNEL] = sample[RIGHTCHANNEL] >> 1;

    uint32_t s32 = Gain(sample); // vosample2lume;

    esp_err_t err = i2s_write((i2s_port_t) m_i2s_num, (const char*) &s32, sizeof(uint32_t), &m_i2s_bytesWritten, 1000);
    if(err != ESP_OK) {
        log_e("ESP32 Errorcode %i", err);
        return false;
    }
    if(m_i2s_bytesWritten < 4) {
        log_e("Can't stuff any more in I2S..."); // increase waitingtime or outputbuffer
        return false;
    }
    return true;
}
//---------------------------------------------------------------------------------------------------------------------
//   V O R B I S   S t u f f
//---------------------------------------------------------------------------------------------------------------------

OggVorbis_File vf;
int            eof = 0;

void setup() {
    setupI2S();
    setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT, -1);
    setBitsPerSample(16);
    setChannels(2);
    setSampleRate(44100);
    I2Sstart(m_i2s_num);
    Serial.begin(115200);
    delay(1000);
    pinMode(SD_MMC_D0, INPUT_PULLUP);
    #ifdef CONFIG_IDF_TARGET_ESP32S3
        SD_MMC.setPins(SD_MMC_CLK, SD_MMC_CMD, SD_MMC_D0);
    #endif
    if(!SD_MMC.begin("/sdcard", true)){
        log_i("SD card not found,");
        while(true){;}
    }


	const char inPath[]  = "/ogg/in1.ogg";
	const char outPath[] = "/ogg/out.wav";

	if(!SD_MMC.exists(inPath)){
		log_e("File does not exists");
		return;
	}

	File fIn = SD_MMC.open(inPath, "r");
	File fOut = SD_MMC.open(outPath, "w");

	if(ov_open(&fIn, &vf) < 0) {
		printf("Input does not appear to be an Ogg bitstream.\n");
		exit(1);
	}
    m_f_isPlaying = true;
	char       **ptr = ov_comment(&vf)->user_comments;
	vorbis_info *vi = ov_info(&vf, -1);
	while(*ptr) {
		fprintf(stderr, "%s\n", *ptr);
		++ptr;
	}
	fprintf(stderr, "\nBitstream is %d channel, %idHz\n", vi->channels, vi->rate);
	fprintf(stderr, "\nDecoded length: %i samples\n", (int32_t)ov_pcm_total(&vf, -1));
	fprintf(stderr, "Encoded by: %s\n\n", ov_comment(&vf)->vendor);

	uint8_t header[44];
	memset(header, 0, 44);
	memcpy(header, "RIFF", 4);
	memcpy(header + 4,  "\x00\x00\x00\x00", 4); // Chunk Size
	memcpy(header + 8,  "WAVE", 4);
    memcpy(header + 12, "fmt ", 4);
	memcpy(header + 16, "\x10\x00\x00\x00", 4); // Subchunk 1 Size
    memcpy(header + 20, "\x01\x00", 2); // // AudioFormat
    memcpy(header + 22, "\x02\x00", 2); // // NumChannels
    memcpy(header + 24, "\x44\xAC\x00\x00", 4); // Sample Rate
    memcpy(header + 28, "\x44\xAC\x00\x00", 4); // Byte Rate
    memcpy(header + 32, "\x02\x00", 2); // Block Align
    memcpy(header + 34, "\x10\x00", 2); // Bits Per Sample
	memcpy(header + 36, "data", 4);// Subchunk 2 ID
	memcpy(header + 40, "\x00\x00\x00\x00", 4); // Subchunk 2 Size
	fOut.write(header, 44);



	while(!eof) {
		int32_t ret = ov_read(&vf, m_outBuff, sizeof(m_outBuff));

		if(ret == 0) { /* EOF */
			eof = 1;
			break;
		}
		else if(ret < 0) {
			/* error in the stream.  Not a problem, just reporting it in case we (the app) cares.  In this case, we
			 * don't. */
		}
		else {
			/* we don't bother dealing with sample rate changes, etc, but you'll have to*/

			fOut.write((const uint8_t*)m_outBuff, ret);
			m_validSamples = ret/4;
			printf("validSamples %i\n", m_validSamples);
            playChunk();
		}
	}

	/* cleanup */
	//ov_clear(&vf);
	fIn.close();
	fOut.close();
	log_e("highWaterMark %u", uxTaskGetStackHighWaterMark(NULL));

	printf("ready!!!\n");  // prints !!!Hello World!!!
	return;

}

void loop() {
  // put your main code here, to run repeatedly:
}
