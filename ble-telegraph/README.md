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
- Gap of **3 dits or longer**: finish the current character.
- Gap of **7 dits or longer**: finish the current character and insert a word space.
- No new press for **15 dits**: finish the message and send the decoded text over BLE.

The default `DIT_TIME_MS` is 100 ms. Change this one constant to suit the operator; keep it fixed during a message. The same timing unit is used for the RGB LED Morse output. A 25 ms debounce is applied to Button A.

The decoder uses the same `MORSE_TABLE` as the LED transmitter. Character-to-Morse and Morse-to-character conversion therefore cannot drift apart through separately maintained lookup lists. Unsupported sequences are returned as `?`.

## BLE behavior

The sketch exposes the Nordic UART Service through `BLEServiceUART`.

- Text received over BLE, terminated by CR or LF, is flashed with the RGB LED: red for a dit, blue for a dah, and green when playback completes.
- Morse entered with Button A is decoded and sent back over the Nordic UART TX characteristic after 15 dits of inactivity.
- The web terminal in `index.html` can send text to the Pico and display decoded Button A messages.

