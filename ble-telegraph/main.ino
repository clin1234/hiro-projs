#include <Arduino.h>
#include <BLE.h>

// -----------------------------------------------------------------------------
// MechSoul RGB LED pinout from the supplied manual
// -----------------------------------------------------------------------------
constexpr uint8_t RED_PIN   = 7;
constexpr uint8_t GREEN_PIN = 6;
constexpr uint8_t BLUE_PIN  = 8;

// -----------------------------------------------------------------------------
// Morse timing
//
// Standard Morse timing uses:
//   dot      = 1 unit
//   dash     = 3 units
//   symbol   = 1 unit between dots/dashes
//   letter   = 3 units between letters
//   word     = 7 units between words
//
// Here we use 200 ms as one unit.
// -----------------------------------------------------------------------------
constexpr uint16_t DOT_TIME = 100;
constexpr uint16_t DASH_TIME = DOT_TIME * 3;

constexpr uint16_t SYMBOL_GAP = DOT_TIME;
constexpr uint16_t LETTER_GAP = DOT_TIME * 3;
constexpr uint16_t WORD_GAP   = DOT_TIME * 7;

// Green message-complete indication.
constexpr uint16_t END_FLASH_TIME = 100;

// -----------------------------------------------------------------------------
// BLE Nordic UART service
// -----------------------------------------------------------------------------
BLEServiceUART mechSoulControl;

// Maximum message that can be accumulated before transmission.
// Increase if you want longer messages.
constexpr size_t MAX_MESSAGE_LENGTH = 6000;

String receivedMessage;

// -----------------------------------------------------------------------------
// Morse alphabet
// -----------------------------------------------------------------------------
const char *morseCode(char c)
{
  switch (c) {
    case 'A': return ".-";
    case 'B': return "-...";
    case 'C': return "-.-.";
    case 'D': return "-..";
    case 'E': return ".";
    case 'F': return "..-.";
    case 'G': return "--.";
    case 'H': return "....";
    case 'I': return "..";
    case 'J': return ".---";
    case 'K': return "-.-";
    case 'L': return ".-..";
    case 'M': return "--";
    case 'N': return "-.";
    case 'O': return "---";
    case 'P': return ".--.";
    case 'Q': return "--.-";
    case 'R': return ".-.";
    case 'S': return "...";
    case 'T': return "-";
    case 'U': return "..-";
    case 'V': return "...-";
    case 'W': return ".--";
    case 'X': return "-..-";
    case 'Y': return "-.--";
    case 'Z': return "--..";

    case '0': return "-----";
    case '1': return ".----";
    case '2': return "..---";
    case '3': return "...--";
    case '4': return "....-";
    case '5': return ".....";
    case '6': return "-....";
    case '7': return "--...";
    case '8': return "---..";
    case '9': return "----.";

    case '.': return ".-.-.-";
    case ',': return "--..--";
    case '?': return "..--..";
    case '\'': return ".----.";
    case '!': return "-.-.--";
    case '/': return "-..-.";
    case '(': return "-.--.";
    case ')': return "-.--.-";
    case '&': return ".-...";
    case ':': return "---...";
    case ';': return "-.-.-.";
    case '=': return "-...-";
    case '+': return ".-.-.";
    case '-': return "-....-";
    case '_': return "..--.-";
    case '"': return ".-..-.";
    case '$': return "...-..-";
    case '@': return ".--.-.";

    default:
      return nullptr;
  }
}

// -----------------------------------------------------------------------------
// RGB control
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
// Transmit one Morse symbol
// -----------------------------------------------------------------------------
void transmitSymbol(char symbol)
{
  switch (symbol) {
    case '.':
      redOn();
      delay(DOT_TIME);
      allOff();
      break;

    case '-':
      blueOn();
      delay(DASH_TIME);
      allOff();
      break;
  }

  delay(SYMBOL_GAP);
}

// -----------------------------------------------------------------------------
// Transmit one character as Morse
// -----------------------------------------------------------------------------
void transmitCharacter(char c)
{
  if (c == ' ') {
    // Additional word spacing is handled by transmitMessage().
    return;
  }

  c = toupper(static_cast<unsigned char>(c));

  const char *code = morseCode(c);

  if (code == nullptr) {
    // Unsupported character: ignore it.
    return;
  }

  for (const char *p = code; *p != '\0'; ++p) {
    transmitSymbol(*p);
  }
}

// -----------------------------------------------------------------------------
// Transmit the entire message
// -----------------------------------------------------------------------------
void transmitMessage(const String &message)
{
  Serial.print("Transmitting: ");
  Serial.println(message);

  for (size_t i = 0; i < message.length(); ++i) {
    char c = message[i];

    if (c == ' ') {
      // The previous character already got LETTER_GAP.
      // Add enough time to make the total word gap 7 units.
      delay(WORD_GAP - LETTER_GAP);
      continue;
    }

    transmitCharacter(c);

    // transmitCharacter() ends with SYMBOL_GAP, so compensate for
    // that and make the gap between letters 3 units total.
    delay(LETTER_GAP - SYMBOL_GAP);
  }

  // Entire message has now been transmitted.
  greenOn();
  delay(END_FLASH_TIME);
  allOff();

  mechSoulControl.println("Transmission complete.");
  Serial.println("Transmission complete.");
}

// -----------------------------------------------------------------------------
// Finish a Bluetooth message
// -----------------------------------------------------------------------------
void handleReceivedMessage()
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

  BLE.begin("MechSoul Telegraph");
  
  BLE.server()->addService(&mechSoulControl);

  // Automatically flush received/transmitted UART data periodically.
  mechSoulControl.setAutoflush(50);

  BLE.startAdvertising(true);

  Serial.print("BLE MAC: ");
  Serial.println(BLE.address().toString());

  Serial.println("MechSoul Telegraph ready.");
  Serial.println("Send a message over BLE followed by Enter.");
}

// -----------------------------------------------------------------------------
// Main loop
// -----------------------------------------------------------------------------
void loop()
{
  while (mechSoulControl.available()) {
    char c = static_cast<char>(mechSoulControl.read());

    // Treat CR/LF as end-of-message.
    if (c == '\r' || c == '\n') {
      handleReceivedMessage();
      continue;
    }

    // Store printable characters.
    if (isPrintable(static_cast<unsigned char>(c))) {
      if (receivedMessage.length() < MAX_MESSAGE_LENGTH) {
        receivedMessage += c;
      }
    }
  }
}
