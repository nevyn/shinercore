// Inspiration for beat detection:
// * https://github.com/blaz-r/ESP32-music-beat-sync/blob/main/src/ESP32-music-beat-sync.cpp
// * https://github.com/garrickhogrebe/ESP32FastLEDSketches same with threads
//
// References for mic handling:
// * https://docs.m5stack.com/en/atom/atomecho
// * Using i2c on the Echo: https://github.com/m5stack/M5-ProductExampleCodes/blob/master/Core/Atom/AtomEcho/Arduino/Repeater/Repeater.ino
// * ... and cleaned up: https://github.com/nevyn/NevynsArduino/blob/master/m5audiotest/m5audiotest.ino
// * Using Unified: https://github.com/m5stack/M5Unified/blob/master/examples/Basic/Microphone/Microphone.ino


/// Opens microphone if available, and detects the beat of any playing music using FFT.
class BeatDetector
{
public:
  BeatDetector() 
    : _isOnBeat(false), data_offset(0)
  {}

  void setup()
  {
    uses_echo = OpenEchoMic();
    logger.print("Beat detector using echo mic? ");
    logger.println(uses_echo);
  }

  void update()
  {
    if(uses_echo)
    {
      ReadEcho();
    }
    static const int third_of_a_second_in_samples = 1000; // figure out
    if(data_offset > third_of_a_second_in_samples)
    {
      AnalyzeAudio();
      data_offset = 0;
    }
  }

  bool isOnBeat()
  {
    return _isOnBeat;
  }
private:
  uint8_t micdata[1024 * 80];
  int data_offset;
  bool uses_echo;

  bool _isOnBeat;

  // The mic on an M5Atom Echo is bolted onto basically an M5StickC.
  // Not sure if this code will also work on an M5StickC; if not, we can use M5.Microphone there instead.
  bool OpenEchoMic()
  {
    #define CONFIG_I2S_BCK_PIN 19
    #define CONFIG_I2S_LRCK_PIN 33
    #define CONFIG_I2S_DATA_PIN 22
    #define CONFIG_I2S_DATA_IN_PIN 23
    #define SPEAKER_I2S_NUMBER I2S_NUM_0

    esp_err_t err = ESP_OK;

    i2s_driver_uninstall(SPEAKER_I2S_NUMBER);
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER),
        .sample_rate = 16000,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT, // is fixed at 12bit, stereo, MSB
        .channel_format = I2S_CHANNEL_FMT_ALL_RIGHT,
        .communication_format = I2S_COMM_FORMAT_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 6,
        .dma_buf_len = 60,
    };
    i2s_config.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_PDM);

    err += i2s_driver_install(SPEAKER_I2S_NUMBER, &i2s_config, 0, NULL);
    i2s_pin_config_t tx_pin_config;

    tx_pin_config.bck_io_num = CONFIG_I2S_BCK_PIN;
    tx_pin_config.ws_io_num = CONFIG_I2S_LRCK_PIN;
    tx_pin_config.data_out_num = CONFIG_I2S_DATA_PIN;
    tx_pin_config.data_in_num = CONFIG_I2S_DATA_IN_PIN;

    err += i2s_set_pin(SPEAKER_I2S_NUMBER, &tx_pin_config);
    err += i2s_set_clk(SPEAKER_I2S_NUMBER, 16000, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_MONO);

    return err == ESP_OK;
  }

  bool ReadEcho()
  {
    size_t bytes_read;
    i2s_read(SPEAKER_I2S_NUMBER, (char *)(micdata + data_offset), 1024, &bytes_read, (100 / portTICK_RATE_MS));
    data_offset += 1024;

    // ?? do we read until bytes_read is less than 1024, or how do we know how much we got?
  }

  void AnalyzeAudio()
  {
    // ...
  }
};

