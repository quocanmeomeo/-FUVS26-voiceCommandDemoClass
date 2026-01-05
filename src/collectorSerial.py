import serial
import wave
import time
import os
import keyboard

# --- Configuration ---

# ---------------------------------------------------------------- //
# // // REPLACE WITH YOUR ESP32's port
COMPORT = 'COM9'  # Update to your ESP32's port
#---------------------------------------------------------------- //

#---------------------------------------------------------------- //
RECORD_SECONDS = 60 # Change this for longer dataset 
#---------------------------------------------------------------- //

BAUD = 921600
SAMPLE_RATE = 16000

TOTAL_BYTES = SAMPLE_RATE * RECORD_SECONDS * 2  # 16-bit PCM

# Create base data directory
DATA_DIR = "dataset"
if not os.path.exists(DATA_DIR):
    os.makedirs(DATA_DIR)

def get_next_filename(label):
    """Checks the folder and returns the next available filename like 'on_05.wav'"""
    folder = os.path.join(DATA_DIR, label)
    if not os.path.exists(folder):
        os.makedirs(folder)
    
    existing_files = [f for f in os.listdir(folder) if f.endswith('.wav')]
    count = len(existing_files)
    return os.path.join(folder, f"{label}_{count:03d}.wav")

def record_sample(ser, label):
    filename = get_next_filename(label)
    print(f"\n[READY] Press SPACE to record {RECORD_SECONDS} for label: '{label}'")
    keyboard.wait('space')
    
    # Flush any old data in the buffer
    ser.reset_input_buffer()
    
    print(f"Recording {filename}...")
    ser.write(b'S') # Send START command to ESP32
    
    audio_data = bytearray()
    start_time = time.time()
    
    while len(audio_data) < TOTAL_BYTES:
        if ser.in_waiting > 0:
            chunk = ser.read(ser.in_waiting)
            audio_data.extend(chunk)
        
        # UI Progress
        percent = (len(audio_data) / TOTAL_BYTES) * 100
        print(f"\rProgress: {percent:.1f}%", end="")
        
        if time.time() - start_time > RECORD_SECONDS + 1: # Timeout fail-safe
            break

    ser.write(b'E') # Send STOP command to ESP32
    
    # Save to Wave
    with wave.open(filename, 'wb') as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(SAMPLE_RATE)
        wf.writeframes(audio_data[:TOTAL_BYTES])
    
    print(f"\n[DONE] Saved {filename}")
    time.sleep(0.5) # Debounce

def main():
    try:
        ser = serial.Serial(COMPORT, BAUD, timeout=1)
        ser.set_buffer_size(rx_size=1024*1024)
        time.sleep(2)

        print("--- Voice Command Data Collector ---")
        label = input("Enter label to record (e.g., 'on', 'off', 'background'): ").strip().lower()
        
        if not label:
            print("Label cannot be empty!")
            return

        print(f"Collecting data for: {label}")
        print("Press 'Ctrl+C' to stop or change label.")

        while True:
            record_sample(ser, label)

    except KeyboardInterrupt:
        print("\nExiting...")
    except Exception as e:
        print(f"Error: {e}")
    finally:
        if 'ser' in locals():
            ser.close()

if __name__ == "__main__":
    main()