#include <Arduino.h>
#include <BLE.h>
#include <ctype.h>

// -----------------------------------------------------------------------------
// MechSoul / Raspberry Pi Pico 2 W pinout
// -----------------------------------------------------------------------------

// RGB LED
constexpr uint8_t RED_PIN   = 7;
constexpr uint8_t GREEN_PIN = 6;
constexpr uint8_t BLUE_PIN  = 8;

// Push buttons (wired active-low; INPUT_PULLUP is enabled in setup()).
constexpr uint8_t BUTTON_A_PIN = 1;
constexpr uint8_t BUTTON_B_PIN = 0;

// Ultrasonic distance sensor (reserved for future use).
constexpr uint8_t ULTRASONIC_TRIG_PIN = 9;
constexpr uint8_t ULTRASONIC_ECHO_PIN = 22;

// MPU-6050 (reserved for future use).
constexpr uint8_t MPU6050_SCL_PIN = 17;
constexpr uint8_t MPU6050_SDA_PIN = 16;
constexpr uint8_t MPU6050_AD0_PIN = 15;
constexpr uint8_t MPU6050_INT_PIN = 14;

// Servo outputs (reserved for future use).
constexpr uint8_t SERVO_LFL_PIN = 18;
constexpr uint8_t SERVO_LFS_PIN = 19;
constexpr uint8_t SERVO_LBS_PIN = 20;
constexpr uint8_t SERVO_LBL_PIN = 21;
constexpr uint8_t SERVO_RFL_PIN = 13;
constexpr uint8_t SERVO_RFS_PIN = 12;
constexpr uint8_t SERVO_RBS_PIN = 11;
constexpr uint8_t SERVO_RBL_PIN = 10;

// -----------------------------------------------------------------------------
// Morse timing
//
// Button A is a single-button Morse key:
//   - release before 2 dits: dit
//   - hold for at least 2 dits: dah
//   - release-to-next-press gap of 1 dit: next symbol in the same character
//   - gap of at least 3 dits: next character
//   - gap of at least 7 dits: next word
//   - no new press for 15 dits: finish and send the decoded message
//
// Tune DIT_TIME_MS to the operator. Keep it constant for a whole message.
// LED output uses the same unit so the input and output rhythms stay aligned.
// -----------------------------------------------------------------------------
constexpr uint16_t DIT_TIME_MS = 100;
constexpr uint16_t DASH_PRESS_THRESHOLD_MS = DIT_TIME_MS * 2;

constexpr uint32_t LETTER_GAP_MS = DIT_TIME_MS * 3UL;
constexpr uint32_t WORD_GAP_MS = DIT_TIME_MS * 7UL;
constexpr uint32_t MESSAGE_TIMEOUT_MS = DIT_TIME_MS * 15UL;

constexpr uint16_t BUTTON_DEBOUNCE_MS = 25;
constexpr uint16_t END_FLASH_TIME_MS = 100;
constexpr size_t MAX_MESSAGE_LENGTH = 6000;
constexpr size_t MAX_MORSE_SYMBOLS = 8;

// -----------------------------------------------------------------------------
// One source of truth for both character -> Morse and Morse -> character.
// -----------------------------------------------------------------------------
struct MorseEntry {
  char character;
  const char *code;
};

constexpr MorseEntry MORSE_TABLE[] = {
  {'A', ".-"},
  {'B', "-..."},
  {'C', "-.-."},
  {'D', "-.."},
  {'E', "."},
  {'F', "..-."},
  {'G', "--."},
  {'H', "...."},
  {'I', ".."},
  {'J', ".---"},
  {'K', "-.-"},
  {'L', ".-.."},
  {'M', "--"},
  {'N', "-."},
  {'O', "---"},
  {'P', ".--."},
  {'Q', "--.-"},
  {'R', ".-."},
  {'S', "..."},
  {'T', "-"},
  {'U', "..-"},
  {'V', "...-"},
  {'W', ".--"},
  {'X', "-..-"},
  {'Y', "-.--"},
  {'Z', "--.."},

  {'0', "-----"},
  {'1', ".----"},
  {'2', "..---"},
  {'3', "...--"},
  {'4', "....-"},
  {'5', "....."},
  {'6', "-...."},
  {'7', "--..."},
  {'8', "---.."},
  {'9', "----."},

  {'.', ".-.-.-"},
  {',', "--..--"},
  {'?', "..--.."},
  {'\'', ".----."},
  {'!', "-.-.--"},
  {'/', "-..-."},
  {'(', "-.--."},
  {')', "-.--.-"},
  {'&', ".-..."},
  {':', "---..."},
  {';', "-.-.-."},
  {'=', "-...-"},
  {'+', ".-.-."},
  {'-', "-....-"},
  {'_', "..--.-"},
  {'"', ".-..-."},
  {'$', "...-..-"},
  {'@', ".--.-."}
};

constexpr size_t MORSE_TABLE_SIZE =
    sizeof(MORSE_TABLE) / sizeof(MORSE_TABLE[0]);

const char *morseCode(char character)
{
  character = static_cast<char>(
      toupper(static_cast<unsigned char>(character)));

  for (size_t i = 0; i < MORSE_TABLE_SIZE; ++i) {
    const MorseEntry &entry = MORSE_TABLE[i];
    if (entry.character == character) {
      return entry.code;
    }
  }

  return nullptr;
}

char characterFromMorse(const String &code)
{
  for (size_t i = 0; i < MORSE_TABLE_SIZE; ++i) {
    const MorseEntry &entry = MORSE_TABLE[i];
    if (code == entry.code) {
      return entry.character;
    }
  }

  return '[';
}

// -----------------------------------------------------------------------------
// BLE Nordic UART service
// -----------------------------------------------------------------------------
BLEServiceUART mechSoulControl;

String receivedMessage;
String morseBuffer;
bool hasDecodedCharacterInMessage = false;

// Buffer snapshots synchronize the in-progress UI field. Only completed
// characters are sent as ordinary BLE text records.
void reportMorseBuffer();
void reportMorseCharacter(char character);

// -----------------------------------------------------------------------------
// Button A state
// -----------------------------------------------------------------------------
bool buttonARawPressed = false;
bool buttonAStablePressed = false;
uint32_t buttonALastRawChangeMs = 0;
uint32_t buttonAPressStartMs = 0;
uint32_t lastButtonReleaseMs = 0;
bool hasLastButtonRelease = false;

// Forward declarations used while the RGB LED transmitter is active.
void updateButtonA(uint32_t now);
void processMorseTimeout(uint32_t now);

// Keep Button A responsive during LED playback instead of blocking in delay().
void delayWithButtonPolling(uint32_t durationMs)
{
  const uint32_t startedAt = millis();

  while (static_cast<uint32_t>(millis() - startedAt) < durationMs) {
    const uint32_t now = millis();
    updateButtonA(now);
    processMorseTimeout(now);
    delay(1);
  }
}

// -----------------------------------------------------------------------------
// RGB LED control
// -----------------------------------------------------------------------------
void allOff()
{
  digitalWrite(RED_PIN, LOW);
  digitalWrite(GREEN_PIN, LOW);
  digitalWrite(BLUE_PIN, LOW);
}

void redOn()
{
  allOff();
  digitalWrite(RED_PIN, HIGH);
}

void blueOn()
{
  allOff();
  digitalWrite(BLUE_PIN, HIGH);
}

void greenOn()
{
  allOff();
  digitalWrite(GREEN_PIN, HIGH);
}

// -----------------------------------------------------------------------------
// BLE text -> RGB Morse output
// -----------------------------------------------------------------------------
void transmitSymbol(char symbol)
{
  switch (symbol) {
    case '.':
      redOn();
      delayWithButtonPolling(DIT_TIME_MS);
      allOff();
      break;

    case '-':
      blueOn();
      delayWithButtonPolling(DIT_TIME_MS * 3UL);
      allOff();
      break;
  }

  delayWithButtonPolling(DIT_TIME_MS);
}

void transmitCharacter(char character)
{
  if (character == ' ') {
    return;
  }

  const char *code = morseCode(character);
  if (code == nullptr) {
    return;
  }

  for (const char *symbol = code; *symbol != '\0'; ++symbol) {
    transmitSymbol(*symbol);
  }
}

void transmitMessage(const String &message)
{
  Serial.print("Transmitting BLE message: ");
  Serial.println(message);

  for (size_t i = 0; i < message.length(); ++i) {
    const char character = message[i];

    if (character == ' ') {
      // The previous character already emitted a 3-dit letter gap.
      delayWithButtonPolling(WORD_GAP_MS - LETTER_GAP_MS);
      continue;
    }

    transmitCharacter(character);

    // transmitSymbol() ended with a 1-dit gap. Add 2 more dits to
    // make the total inter-character gap equal to 3 dits.
    delayWithButtonPolling(LETTER_GAP_MS - DIT_TIME_MS);
  }

  greenOn();
  delayWithButtonPolling(END_FLASH_TIME_MS);
  allOff();

  mechSoulControl.println("Transmission complete.");
  mechSoulControl.flush();
  Serial.println("Transmission complete.");
}

// -----------------------------------------------------------------------------
// Button A Morse input -> decoded ASCII text over BLE
// -----------------------------------------------------------------------------
void finishMorseCharacter()
{
  if (morseBuffer.length() == 0) {
    return;
  }

  const String completedCode = morseBuffer;
  const char character = characterFromMorse(completedCode);

  // Clear the temporary symbol buffer as soon as the character is decoded.
  morseBuffer = "";
  reportMorseCharacter(character);
  hasDecodedCharacterInMessage = true;

  Serial.print("Morse ");
  Serial.print(completedCode);
  Serial.print(" -> ");
  Serial.println(character);
}

void appendWordSpace()
{
  if (!hasDecodedCharacterInMessage) {
    return;
  }

  // A word separator is also a decoded character in the BLE text stream.
  reportMorseCharacter(' ');
}

void resetMorseInput()
{
  morseBuffer = "";
  hasDecodedCharacterInMessage = false;
  hasLastButtonRelease = false;
  lastButtonReleaseMs = 0;
}

void finishMorseMessage()
{
  finishMorseCharacter();
  Serial.println("Button A Morse message complete.");
  resetMorseInput();
}

void onButtonAPressed(uint32_t now)
{
  if (hasLastButtonRelease) {
    const uint32_t gap = now - lastButtonReleaseMs;

    if (gap >= MESSAGE_TIMEOUT_MS) {
      // A new press after the timeout starts a new message.
      finishMorseMessage();
    } else if (gap >= WORD_GAP_MS) {
      finishMorseCharacter();
      appendWordSpace();
    } else if (gap >= LETTER_GAP_MS) {
      finishMorseCharacter();
    }
    // Shorter gaps remain within the current character.
  }

  buttonAPressStartMs = now;
}

// Send a snapshot of the temporary input buffer for the browser's live UI.
// This is a UI synchronization record, not decoded text for the terminal.
void reportMorseBuffer()
{
  const String event = String("\x1E" "BUFFER:") + morseBuffer;
  mechSoulControl.println(event.c_str());
  mechSoulControl.flush();
}

// Send only the translated character as an ordinary Nordic UART text record.
void reportMorseCharacter(char character)
{
  mechSoulControl.println(character);
  mechSoulControl.flush();
}

void onButtonAReleased(uint32_t now)
{
  const uint32_t pressDuration = now - buttonAPressStartMs;
  const char symbol =
      pressDuration < DASH_PRESS_THRESHOLD_MS ? '.' : '-';

  if (morseBuffer.length() < MAX_MORSE_SYMBOLS) {
    morseBuffer += symbol;
    reportMorseBuffer();
  }

  lastButtonReleaseMs = now;
  hasLastButtonRelease = true;

  Serial.print(symbol == '.' ? "Dit: " : "Dah: ");
  Serial.println(morseBuffer);
}

void updateButtonA(uint32_t now)
{
  // The board buttons are assumed to connect the GPIO to GND when pressed.
  const bool rawPressed = digitalRead(BUTTON_A_PIN) == LOW;

  if (rawPressed != buttonARawPressed) {
    buttonARawPressed = rawPressed;
    buttonALastRawChangeMs = now;
  }

  if (static_cast<uint32_t>(now - buttonALastRawChangeMs) <
      BUTTON_DEBOUNCE_MS) {
    return;
  }

  if (buttonAStablePressed == buttonARawPressed) {
    return;
  }

  buttonAStablePressed = buttonARawPressed;

  // Use the debounced edge timestamp, not the end of the debounce period,
  // so 1-, 3-, 7-, and 15-dit gaps are measured from the physical edge.
  if (buttonAStablePressed) {
    onButtonAPressed(buttonALastRawChangeMs);
  } else {
    onButtonAReleased(buttonALastRawChangeMs);
  }
}

void processMorseTimeout(uint32_t now)
{
  // A held button is still an active transmission, not message silence.
  if (buttonARawPressed || buttonAStablePressed || !hasLastButtonRelease) {
    return;
  }

  const uint32_t gap = now - lastButtonReleaseMs;

  if (gap >= MESSAGE_TIMEOUT_MS) {
    finishMorseMessage();
    return;
  }

  // A character is known after the standard 3-dit letter gap.
  if (gap >= LETTER_GAP_MS) {
    finishMorseCharacter();
  }


}

// -----------------------------------------------------------------------------
// Finish a line-delimited BLE message and flash it on the RGB LED.
// -----------------------------------------------------------------------------
void handleReceivedBleMessage()
{
  if (receivedMessage.length() == 0) {
    return;
  }

  Serial.print("BLE message received: ");
  Serial.println(receivedMessage);

  transmitMessage(receivedMessage);
  receivedMessage = "";
}

// -----------------------------------------------------------------------------
// Setup
// -----------------------------------------------------------------------------
void setup()
{
  Serial.begin(115200);
  delay(1000);

  pinMode(RED_PIN, OUTPUT);
  pinMode(GREEN_PIN, OUTPUT);
  pinMode(BLUE_PIN, OUTPUT);
  allOff();

  pinMode(BUTTON_A_PIN, INPUT_PULLUP);
  pinMode(BUTTON_B_PIN, INPUT_PULLUP);

  BLE.begin("MechSoul Telegraph");
  BLE.server()->addService(&mechSoulControl);
  mechSoulControl.setAutoflush(50);
  BLE.startAdvertising(true);

  Serial.print("BLE MAC: ");
  Serial.println(BLE.address().toString());

  Serial.println("MechSoul Telegraph ready.");
  Serial.println("BLE text followed by Enter flashes as RGB Morse.");
  Serial.println("Button A: short press = dit; hold >= 2 dits = dah.");
  Serial.println("Use 1-dit symbol gaps, 3-dit letter gaps, and 7-dit word gaps.");
  Serial.println("Button A message is sent after 15 dits of inactivity.");
}

// -----------------------------------------------------------------------------
// Main loop
// -----------------------------------------------------------------------------
void loop()
{
  const uint32_t now = millis();

  updateButtonA(now);
  processMorseTimeout(now);

  while (mechSoulControl.available()) {
    const char character = static_cast<char>(mechSoulControl.read());

    // BLE text commands are still delimited by CR/LF.
    if (character == '\r' || character == '\n') {
      handleReceivedBleMessage();
      continue;
    }

    // Only printable ASCII is sent to the RGB Morse transmitter.
    if (character >= 0x20 && character <= 0x7E &&
        receivedMessage.length() < MAX_MESSAGE_LENGTH) {
      receivedMessage += character;
    }
  }
}
