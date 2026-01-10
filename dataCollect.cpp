// #include <Arduino.h>
// #include <driver/i2s.h>

// // Safe Pins for ESP32-S3
// #define I2S_WS 9
// #define I2S_SCK 8
// #define I2S_SD 7
// #define I2S_PORT I2S_NUM_0
// #define BUFFER_SIZE 512

// bool isStreaming = false;

// void setup() {
//   // Use High Speed Serial
//   Serial.begin(921600);

//   i2s_config_t i2s_config = {
//     .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
//     .sample_rate = 16000,
//     .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
//     .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
//     .communication_format = I2S_COMM_FORMAT_I2S,
//     .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
//     .dma_buf_count = 8,
//     .dma_buf_len = BUFFER_SIZE,
//     .use_apll = false
//   };

//   i2s_pin_config_t pin_config = {
//     .bck_io_num = I2S_SCK,
//     .ws_io_num = I2S_WS,
//     .data_out_num = -1,
//     .data_in_num = I2S_SD
//   };

//   i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
//   i2s_set_pin(I2S_PORT, &pin_config);
// }

// void loop() {
//   // Command Logic
//   if (Serial.available()) {
//     char cmd = Serial.read();
//     if (cmd == 'S') isStreaming = true;
//     if (cmd == 'E') isStreaming = false;
//   }

//   if (isStreaming) {
//     int32_t raw_samples[128];
//     size_t bytes_read = 0;

//     // Read a block of 32-bit samples
//     i2s_read(I2S_PORT, &raw_samples, sizeof(raw_samples), &bytes_read, portMAX_DELAY);

//     int samples_count = bytes_read / 4;
//     int16_t out_buffer[128];

//     for (int i = 0; i < samples_count; i++) {
//       // Convert 32-bit I2S to 16-bit PCM
//       // Shift 14 is standard for INMP441 to get good volume without clipping
//       out_buffer[i] = (int16_t)(raw_samples[i] >> 14);
//     }

//     // Send RAW BYTES to Serial (No println, just binary)
//     Serial.write((uint8_t*)out_buffer, samples_count * 2);
//   }
// }