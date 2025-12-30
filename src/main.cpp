#include <Arduino.h>
#include <driver/i2s.h>
#include <Adafruit_NeoPixel.h>

// ---------------------------------------------------------------- //
// REPLACE WITH YOUR LIBRARY
#include <quocanmeomeo-project-1_inferencing.h> 
// ---------------------------------------------------------------- //

// --- Pin Definitions ---
#define I2S_WS 4
#define I2S_SCK 5
#define I2S_SD 6
#define NEOPIXEL_PIN 45
#define NEOPIXEL_COUNT 1

// --- Audio Settings ---
#define I2S_PORT I2S_NUM_0
#define SAMPLE_RATE 16000

// --- Constants for Logic ---
#define TRIGGER_CONFIDENCE 0.90   // > 90% to start verification
#define VERIFY_SAMPLES 10         // Inspect next 10 samples
#define VERIFY_MIN_COUNT 4        // "More than 5" means > 5 (so 6, 7, etc.)
#define VERIFY_MAX_COUNT 6       // Maximum matches allowed to avoid false positives
#define VERIFY_AVG_CONF 0.90      // Average confidence > 85%
#define LOCK_TIME_MS 3000         // Hang LED for 5 seconds

// --- LED Colors ---
uint32_t COL_GREEN, COL_BLUE, COL_WHITE, COL_BLACK;

// --- Globals ---
Adafruit_NeoPixel pixels(NEOPIXEL_COUNT, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

// Ring Buffer settings
#define N_SAMPLES_TO_ALLOC (EI_CLASSIFIER_RAW_SAMPLE_COUNT * 2) 
int16_t *inference_buffer; 
volatile uint32_t record_idx = 0;
bool debug_nn = false; 

// --- State Machine Enums ---
enum DetectionState {
    STATE_IDLE,       // Listening for initial > 90% trigger
    STATE_VERIFYING,  // Counting the next 10 samples
    STATE_LOCKED      // Success! Holding LED for 5 seconds
};

DetectionState currentState = STATE_IDLE;

// --- Verification Variables ---
char targetLabel[32];         // "on" or "off"
int verifyCounter = 0;        // Counts up to 10
int matchCounter = 0;         // How many times we saw the target
float matchConfidenceSum = 0; // To calculate average
unsigned long lockStartTime = 0; // Timer for the 5s hold

// --- Function Prototypes ---
void setupI2S();
void capture_samples_task(void* arg);
int microphone_audio_signal_get_data(size_t offset, size_t length, float *out_ptr);
void setLedColor(uint32_t color);

void setup() {
    Serial.begin(115200);
    while (!Serial);
    Serial.println("System Initialized");
    Serial.println("Logic: >90% Trigger -> Check next 10 -> if >4 matches & avg >90% -> Lock 3s");

    // 1. Setup LED
    pixels.begin();
    pixels.setBrightness(50);
    COL_GREEN = pixels.Color(0, 255, 0);
    COL_BLUE = pixels.Color(0, 0, 255);
    COL_WHITE = pixels.Color(255, 255, 255); // Noise/Idle
    COL_BLACK = pixels.Color(0, 0, 0);
    
    setLedColor(COL_WHITE); // Start in Noise/Idle state

    // 2. Allocate Ring Buffer
    inference_buffer = (int16_t *)heap_caps_malloc(N_SAMPLES_TO_ALLOC * sizeof(int16_t), MALLOC_CAP_8BIT);
    if (!inference_buffer) {
        Serial.println("ERR: Failed to allocate inference buffer!");
        while (1);
    }

    // 3. Initialize Classifier
    run_classifier_init();

    // 4. Setup I2S
    setupI2S();

    // 5. Create the Audio Capture Task
    xTaskCreatePinnedToCore(capture_samples_task, "CaptureTask", 4096, NULL, 1, NULL, 0);
}

void loop() {
    signal_t signal;
    signal.total_length = EI_CLASSIFIER_RAW_SAMPLE_COUNT;
    signal.get_data = &microphone_audio_signal_get_data;

    ei_impulse_result_t result = { 0 };

    // Continuous inference handles the window sliding automatically
    EI_IMPULSE_ERROR r = run_classifier_continuous(&signal, &result, debug_nn);
    
    if (r != EI_IMPULSE_OK) {
        ei_printf("ERR: Classifier failed (%d)\n", r);
        return;
    }

    // --- Get Best Prediction ---
    float max_score = 0.0;
    int max_index = -1;

    for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
        if (result.classification[ix].value > max_score) {
            max_score = result.classification[ix].value;
            max_index = ix;
        }
    }
    const char* current_label = result.classification[max_index].label;

    // Print status (helps debugging)
    ei_printf("[%s] Detect: %s (%.2f) | State: %d\n", 
              (currentState == STATE_LOCKED ? "LOCKED" : (currentState == STATE_VERIFYING ? "VERIFY" : "IDLE")), 
              current_label, max_score, currentState);


    // ---------------------------------------------------------------- //
    // STATE MACHINE LOGIC
    // ---------------------------------------------------------------- //
    
    switch (currentState) {
        
        // --- CASE 1: IDLE (Waiting for trigger) ---
        case STATE_IDLE:
            // Ensure LED is White (Noise) while waiting
            setLedColor(COL_WHITE);

            // Check trigger condition: > 90% AND ("on" OR "off")
            if (max_score > TRIGGER_CONFIDENCE) {
                if (strcmp(current_label, "on") == 0 || strcmp(current_label, "off") == 0) {
                    
                    // Start Verification
                    strcpy(targetLabel, current_label); // Remember what we are verifying
                    currentState = STATE_VERIFYING;
                    
                    // Reset counters
                    verifyCounter = 0;
                    matchCounter = 0;
                    matchConfidenceSum = 0;
                    
                    Serial.print(">>> Triggered! Verifying: ");
                    Serial.println(targetLabel);
                }
            }
            break;

        // --- CASE 2: VERIFYING (Checking next 10 samples) ---
        case STATE_VERIFYING:
            verifyCounter++;

            // Check if current sample matches target
            if (strcmp(current_label, targetLabel) == 0) {
                matchCounter++;
                matchConfidenceSum += max_score;
            }

            // Check if we have finished inspecting 10 samples
            if (verifyCounter >= VERIFY_SAMPLES) {
                
                float avgConf = 0;
                if (matchCounter > 0) avgConf = matchConfidenceSum / matchCounter;

                Serial.printf(">>> Verify Result: Matches: %d/%d, Avg Conf: %.2f\n", matchCounter, VERIFY_SAMPLES, avgConf);

                // Validation Logic: Matches > 5 AND Avg Conf > 0.85
                if (matchCounter >= VERIFY_MIN_COUNT && avgConf > VERIFY_AVG_CONF && matchCounter <= VERIFY_MAX_COUNT) {
                    
                    // SUCCESS! Lock the state.
                    currentState = STATE_LOCKED;
                    lockStartTime = millis();
                    
                    if (strcmp(targetLabel, "on") == 0) {
                        setLedColor(COL_GREEN);
                    } else {
                        setLedColor(COL_BLUE);
                    }
                    Serial.println(">>> CONFIRMED! Locking LED for 5s.");

                } else {
                    // FAILED. Back to IDLE.
                    Serial.println(">>> Verification Failed. Back to Noise.");
                    currentState = STATE_IDLE;
                    setLedColor(COL_WHITE);
                }
            }
            break;

        // --- CASE 3: LOCKED (Hang for 5 seconds) ---
        case STATE_LOCKED:
            // We continue running the classifier loop to keep the buffer clean,
            // but we IGNORE the result and DO NOT change the LED.
            
            if (millis() - lockStartTime > LOCK_TIME_MS) {
                Serial.println(">>> Lock Timer Expired. Resetting.");
                currentState = STATE_IDLE;
                setLedColor(COL_WHITE);
            }
            break;
    }
}

// ---------------------------------------------------------------- //
// I2S Setup 
// ---------------------------------------------------------------- //
void setupI2S() {
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 8,
        .dma_buf_len = 512,
        .use_apll = false
    };

    i2s_pin_config_t pin_config = {
        .bck_io_num = I2S_SCK,
        .ws_io_num = I2S_WS,
        .data_out_num = -1,
        .data_in_num = I2S_SD
    };

    i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
    i2s_set_pin(I2S_PORT, &pin_config);
    i2s_zero_dma_buffer(I2S_PORT);
}

// ---------------------------------------------------------------- //
// Capture Task
// ---------------------------------------------------------------- //
void capture_samples_task(void* arg) {
    size_t bytes_read;
    int32_t raw_buffer[128]; 
    
    while (true) {
        i2s_read(I2S_PORT, &raw_buffer, sizeof(raw_buffer), &bytes_read, portMAX_DELAY);
        int samples_read = bytes_read / 4; 

        for (int i = 0; i < samples_read; i++) {
            int16_t sample = (int16_t)(raw_buffer[i] >> 14);
            inference_buffer[record_idx] = sample;
            record_idx++;
            if (record_idx >= N_SAMPLES_TO_ALLOC) {
                record_idx = 0;
            }
        }
    }
}

// ---------------------------------------------------------------- //
// Audio Callback
// ---------------------------------------------------------------- //
int microphone_audio_signal_get_data(size_t offset, size_t length, float *out_ptr) {
    int start_index = record_idx - EI_CLASSIFIER_RAW_SAMPLE_COUNT + offset;
    
    if (start_index < 0) {
        start_index += N_SAMPLES_TO_ALLOC;
    }

    for (size_t i = 0; i < length; i++) {
        int index = (start_index + i);
        if (index >= N_SAMPLES_TO_ALLOC) {
            index -= N_SAMPLES_TO_ALLOC;
        }
        out_ptr[i] = (float)inference_buffer[index];
    }
    return 0;
}

void setLedColor(uint32_t color) {
    pixels.setPixelColor(0, color);
    pixels.show();
}