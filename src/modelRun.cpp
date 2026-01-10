// #include <Arduino.h>
// #include <driver/i2s.h>
// #include <Adafruit_NeoPixel.h>
// #include <freertos/FreeRTOS.h>
// #include <freertos/task.h>
// #include <freertos/semphr.h>

// // ---------------------------------------------------------------- //
// // REPLACE WITH YOUR LIBRARY NAME
// #include <ESP_VOICE_inferencing.h>
// // ---------------------------------------------------------------- //

// // --- Pin Definitions (Grove Board S3) ---
// #define I2S_WS 9    // Yellow wire (D6-D5 port)
// #define I2S_SCK 8   // White wire  (D6-D5 port)
// #define I2S_SD 7    // Yellow wire (D4-D3 port)
// #define NEOPIXEL_PIN 45
// #define BUTTON_PIN 0 // Boot Button

// // --- Audio Settings ---
// #define SAMPLE_RATE 16000
// #define RECORD_SECONDS 3
// #define TOTAL_SAMPLES (SAMPLE_RATE * RECORD_SECONDS)
// #define CONFIDENCE_THRESHOLD 0.90 // 90%

// // --- Globals ---
// Adafruit_NeoPixel pixels(1, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);
// int16_t *recordingBuffer; // Large buffer for 3s audio
// SemaphoreHandle_t btnSemaphore; // RTOS Signal
// int bestWindowStartIndex = 0; // Index of the loudest second

// // --- LED Colors ---
// uint32_t COL_RED, COL_ORANGE, COL_GREEN, COL_BLUE, COL_WHITE, COL_BLACK;

// // --- Function Prototypes ---
// void setupI2S();
// void captureSamples();
// void findBestWindow();
// int raw_audio_get_data(size_t offset, size_t length, float *out_ptr);
// void setLedColor(uint32_t color);
// void buttonTask(void *pvParameters);

// void setup() {
//     Serial.begin(115200);
//     // Init LED
//     pixels.begin();
//     pixels.setBrightness(50);
//     COL_RED = pixels.Color(255, 0, 0);
//     COL_ORANGE = pixels.Color(255, 165, 0);
//     COL_GREEN = pixels.Color(0, 255, 0);
//     COL_BLUE = pixels.Color(0, 0, 255);
//     COL_WHITE = pixels.Color(255, 255, 255);
//     COL_BLACK = pixels.Color(0, 0, 0);
//     setLedColor(COL_BLACK);

//     // Allocate Memory for 3 seconds of audio (~96KB)
//     recordingBuffer = (int16_t *)heap_caps_malloc(TOTAL_SAMPLES * sizeof(int16_t), MALLOC_CAP_8BIT);
//     if (recordingBuffer == NULL) {
//         Serial.println("ERR: Failed to allocate audio buffer!");
//         setLedColor(COL_RED); // Error indication
//         while (1);
//     }

//     setupI2S();

//     // Create RTOS Sync Object (Semaphore)
//     btnSemaphore = xSemaphoreCreateBinary();

//     // Start Button Task on Core 0
//     xTaskCreatePinnedToCore(buttonTask, "BtnTask", 2048, NULL, 1, NULL, 0);

//     Serial.println("System Ready. Press BOOT button (GPIO 0) to record.");
// }

// void loop() {
//     // This loop waits here until the Button Task gives the signal
//     if (xSemaphoreTake(btnSemaphore, portMAX_DELAY) == pdTRUE) {

//         // --- 1. RECORDING ---
//         Serial.println(">>> Recording 3 Seconds...");
//         setLedColor(COL_RED);
//         captureSamples(); // Blocks for 3 seconds

//         // --- 2. PROCESSING ---
//         Serial.println(">>> Finding loudest 1s window...");
//         setLedColor(COL_ORANGE);
//         findBestWindow();

//         Serial.println(">>> Running AI Inference...");

//         signal_t signal;
//         signal.total_length = EI_CLASSIFIER_RAW_SAMPLE_COUNT; // 1 second length
//         signal.get_data = &raw_audio_get_data;

//         ei_impulse_result_t result = { 0 };
//         EI_IMPULSE_ERROR r = run_classifier(&signal, &result, false);

//         if (r != EI_IMPULSE_OK) {
//             Serial.printf("ERR: Classifier failed (%d)\n", r);
//             setLedColor(COL_WHITE);
//             return;
//         }

//         // --- 3. RESULT LOGIC ---
//         float max_score = 0.0;
//         int max_index = -1;

//         // Find best label
//         for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
//             if (result.classification[ix].value > max_score) {
//                 max_score = result.classification[ix].value;
//                 max_index = ix;
//             }
//         }

//         const char* best_label = result.classification[max_index].label;
//         Serial.printf("Detected: %s (%.1f%%)\n", best_label, max_score * 100);

//         // LED Logic
//         if (max_score > CONFIDENCE_THRESHOLD) {
//             if (strcmp(best_label, "on") == 0) {
//                 setLedColor(COL_GREEN);
//             }
//             else if (strcmp(best_label, "off") == 0) {
//                 setLedColor(COL_BLUE);
//             }
//             else {
//                 setLedColor(COL_WHITE); // Noise or Unknown
//             }
//         } else {
//             Serial.println("Low Confidence -> Noise");
//             setLedColor(COL_WHITE);
//         }

//         // Loop finishes, goes back to waiting for semaphore
//     }
// }

// // ---------------------------------------------------------------- //
// // RTOS Task: Button Handler
// // ---------------------------------------------------------------- //
// void buttonTask(void *pvParameters) {
//     pinMode(BUTTON_PIN, INPUT_PULLUP);

//     while (true) {
//         // Check if button is pressed (LOW)
//         if (digitalRead(BUTTON_PIN) == LOW) {
//             // Simple Debounce: Wait 50ms and check again
//             vTaskDelay(pdMS_TO_TICKS(50));

//             if (digitalRead(BUTTON_PIN) == LOW) {
//                 // Signal the main loop to start
//                 xSemaphoreGive(btnSemaphore);

//                 // Wait until button is released to prevent double triggers
//                 while (digitalRead(BUTTON_PIN) == LOW) {
//                     vTaskDelay(pdMS_TO_TICKS(10));
//                 }
//             }
//         }
//         vTaskDelay(pdMS_TO_TICKS(10)); // Check every 10ms
//     }
// }

// // ---------------------------------------------------------------- //
// // I2S Setup
// // ---------------------------------------------------------------- //
// void setupI2S() {
//     i2s_config_t i2s_config = {
//         .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
//         .sample_rate = SAMPLE_RATE,
//         .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
//         .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
//         .communication_format = I2S_COMM_FORMAT_I2S,
//         .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
//         .dma_buf_count = 8,
//         .dma_buf_len = 512,
//         .use_apll = false
//     };

//     i2s_pin_config_t pin_config = {
//         .bck_io_num = I2S_SCK,
//         .ws_io_num = I2S_WS,
//         .data_out_num = -1,
//         .data_in_num = I2S_SD
//     };

//     i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
//     i2s_set_pin(I2S_NUM_0, &pin_config);
//     i2s_zero_dma_buffer(I2S_NUM_0);
// }

// // ---------------------------------------------------------------- //
// // Audio Capture (3 Seconds)
// // ---------------------------------------------------------------- //
// void captureSamples() {
//     size_t bytes_read = 0;
//     int samples_read = 0;
//     int32_t raw_block[128]; // Temp buffer

//     i2s_zero_dma_buffer(I2S_NUM_0);
//     // Flush start
//     i2s_read(I2S_NUM_0, &raw_block, sizeof(raw_block), &bytes_read, portMAX_DELAY);

//     while (samples_read < TOTAL_SAMPLES) {
//         size_t samples_to_read = 64;
//         if (samples_read + samples_to_read > TOTAL_SAMPLES) {
//             samples_to_read = TOTAL_SAMPLES - samples_read;
//         }

//         i2s_read(I2S_NUM_0, &raw_block, samples_to_read * 4, &bytes_read, portMAX_DELAY);
//         int actual_samples = bytes_read / 4;

//         for (int i = 0; i < actual_samples; i++) {
//             // 32-bit (24-bit data) -> 16-bit PCM
//             recordingBuffer[samples_read + i] = (int16_t)(raw_block[i] >> 14);
//         }
//         samples_read += actual_samples;
//     }
// }

// // ---------------------------------------------------------------- //
// // Find Loudest Window
// // ---------------------------------------------------------------- //
// void findBestWindow() {
//     int window_size = EI_CLASSIFIER_RAW_SAMPLE_COUNT; // 1 second (16000)
//     int step_size = 1600; // Check every 0.1s

//     float max_avg_amp = 0;
//     bestWindowStartIndex = 0;

//     for (int i = 0; i <= (TOTAL_SAMPLES - window_size); i += step_size) {
//         float current_sum = 0;
//         // Optimization: Check every 10th sample for speed
//         for (int j = 0; j < window_size; j+=10) {
//             current_sum += abs(recordingBuffer[i + j]);
//         }

//         if (current_sum > max_avg_amp) {
//             max_avg_amp = current_sum;
//             bestWindowStartIndex = i;
//         }
//     }
// }

// // ---------------------------------------------------------------- //
// // Edge Impulse Callback
// // ---------------------------------------------------------------- //
// int raw_audio_get_data(size_t offset, size_t length, float *out_ptr) {
//     size_t global_offset = bestWindowStartIndex + offset;

//     if (global_offset + length > TOTAL_SAMPLES) return -1;

//     numpy::int16_to_float(&recordingBuffer[global_offset], out_ptr, length);
//     return 0;
// }

// void setLedColor(uint32_t color) {
//     pixels.setPixelColor(0, color);
//     pixels.show();
// }