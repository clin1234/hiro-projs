# BLE Telegraph

Firmware for the Raspberry Pi Pico 2 W / MechSoul board. It supports both BLE-to-LED Morse output and one-button Morse input.

## Pin assignment

| Device | Signal | GPIO |
| --- | --- | ---: |
| RGB LED | Red | 7 |
| RGB LED | Green | 6 |
| RGB LED | Blue | 8 |
| Buttons | Button A (Morse key) | 1 |
| Buttons | Button B (reserved) | 0 |
| Ultrasonic sensor | Trig | 9 |
| Ultrasonic sensor | Echo | 22 |
| MPU-6050 | SCL | 17 |
| MPU-6050 | SDA | 16 |
| MPU-6050 | AD0 | 15 |
| MPU-6050 | INT | 14 |
| Servo | LFL — Left Front Leg | 18 |
| Servo | LFS — Left Front Shoulder | 19 |
| Servo | LBS — Left Back Shoulder | 20 |
| Servo | LBL — Left Back Leg | 21 |
| Servo | RFL — Right Front Leg | 13 |
| Servo | RFS — Right Front Shoulder | 12 |
| Servo | RBS — Right Back Shoulder | 11 |
| Servo | RBL — Right Back Leg | 10 |

Button A and Button B are configured as active-low inputs with `INPUT_PULLUP`. The sensor, IMU, and servo GPIOs are documented/reserved but are not initialized or used by this telegraph sketch.

## Button A Morse input

Button A is a single-button Morse key. A press is classified when the button is released:

- Press shorter than **2 dits**: dit (`.`).
- Press of **2 dits or longer**: dah (`-`).
- Release-to-next-press gap of **1 dit**: another symbol in the same character.
- After a gap of **3 dits**, finalize and transmit the current character, then clear the Morse symbol buffer.
- If the next press begins after a gap of **7 dits or longer**, insert one word-space character (but do not add a trailing space when the message simply ends).
- No new press for **15 dits**: finish the message and reset its state; completed characters have already been transmitted individually.

The default `DIT_TIME_MS` is 100 ms. Change this one constant to suit the operator; keep it fixed during a message. The same timing unit is used for the RGB LED Morse output. A 25 ms debounce is applied to Button A. Button A continues to be sampled while the RGB LED is playing a BLE message.

The decoder uses the same `MORSE_TABLE` as the LED transmitter. Character-to-Morse and Morse-to-character conversion therefore cannot drift apart through separately maintained lookup lists. Unsupported sequences are returned as `[`.

## BLE behavior

The sketch exposes the Nordic UART Service through `BLEServiceUART`.

- Text received over BLE, terminated by CR or LF, is flashed with the RGB LED: red for a dit, blue for a dah, and green when playback completes.
- Button A keeps its current character's dits/dahs in the temporary `morseBuffer`. After each accepted symbol, the firmware sends a Record-Separator-prefixed `BUFFER:` snapshot so the web page can mirror that buffer in its single-line field. These snapshots are UI synchronization metadata, not decoded text.
- When a 3-dit letter gap is reached, the firmware translates the buffered pattern, clears `morseBuffer`, and sends only the translated character as an ordinary one-character Nordic UART text record. A new press after a 7-dit gap sends a translated space; a message that simply ends does not gain a trailing space. No accumulated message is retransmitted at the 15-dit timeout.
- The collapsed **Received dits and dahs** panel in `index.html` mirrors the firmware's temporary buffer. Its indicator flashes red for a dit, blue for a dah, and green during pauses. When a translated character arrives, the web page appends it to the received-text terminal and clears the matching buffer field.

