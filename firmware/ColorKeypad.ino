// ColorKeypad.ino

#if defined (SPARK)
// Nothing to include if Spark
#else
#include <Wire.h>
#include <avr/pgmspace.h>
#endif

#include "Grove_OLED_128x64.h"
#include "Keypad_I2C.h"

// variables for timing the display loop
unsigned long int displayAccumulator = 0;
const unsigned int displayDelay = 250;
bool displayOn = true;

// variables for controlling the sleep timer
unsigned long int sleepAccumulator = 0;
const unsigned int sleepDelay = 10000;
bool timerOn = true;
bool awake = true;

// variables for timing the keypad reader loop
const unsigned int readerActiveDelay = 50;
const unsigned int readerSleepDelay = 150;
unsigned long int readerAccumulator = 0;
unsigned int readerDelay = readerActiveDelay;

const unsigned long int saveDelay = 10000;
unsigned long int saveAccumulator = 0;

// Used for the FSMs that determine what mode the program is in.
enum Mode{SetRed, SetGreen, SetBlue, Brightness, Publish};
Mode mode = SetRed;

// the variables for controlling the RGB LED
uint8_t red = 0;
uint8_t green = 0;
uint8_t blue = 0;
uint8_t brightness = 255;

// Multipurpose variable, for the most part used to determine when the entire
// display  needs to refresh or just select parts.
bool newMode = true;

// The following are the variables for the Keypad constructor and should be pretty
// straight-forward.
const byte ROWS = 4; //four rows
const byte COLS = 3; //three columns
char* I2CTYPE = "Adafruit_MCP23017";

char keys[ROWS][COLS] = {
  {'1','2','3'},
  {'4','5','6'},
  {'7','8','9'},
  {'*','0','#'}
};
byte rowPins[ROWS] = {14, 13, 12, 11}; //connect to the row pinouts of the keypad
byte colPins[COLS] = {10, 9, 8}; //connect to the column pinouts of the keypad

Keypad keypad = Keypad( makeKeymap(keys), rowPins, colPins, ROWS, COLS, I2CTYPE );
String val = "0";

// The processor has a habit of stalling if you don't use it for extended periods of time
// so these variables are part of a bit of code I use to keep the processor working.
int c = 0;
int r = 2;

// The following couple variables are used for saving the state of the program every time
// the display refreshes.
struct Save {
    int32_t val;
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    uint8_t brightness;
    Mode mode;
};

int saveAddress = 1;
int startAddress = 0;

Save newSave;

bool saved = false;

void setup()
{
    Wire.begin();
    // set up the LCD's number of columns and rows:
    SeeedOled.init();

    SeeedOled.clearDisplay();           //clear the screen and set start position to top left corner
    SeeedOled.setBrightness(255);
    SeeedOled.setNormalDisplay();       //Set display to Normal mode
    SeeedOled.setPageMode();            //Set addressing mode to Page Mode
    SeeedOled.setTextXY(0,0);           //Set the cursor to 0th Page, 0th Column

    RGB.control(true);
    // RGB.brightness(127);
    Particle.connect();

    byte start;
    EEPROM.get(startAddress, start);
    if (start != 0xFF) {
        Save retrieve;
        EEPROM.get(saveAddress, retrieve);
        String valStr(retrieve.val);
        val = valStr;
        red = retrieve.red;
        green = retrieve.green;
        blue = retrieve.blue;
        brightness = retrieve.brightness;
        mode = retrieve.mode;
    } else {
        start = 0x00;
        EEPROM.put(startAddress, start);
    }
    sleepAccumulator = millis();
}

void loop() {
    char newKey;

    // The Keypad reader loop
    if (millis() - readerAccumulator > readerDelay) {
        readerAccumulator = millis();
        bool button1Pressed = false;
        bool button2Pressed = false;
        newKey = keypad.getKey();
        if (newKey != NULL) {
            // every time a key is pressed, the sleep timer will reset
            timerOn = true;
            sleepAccumulator = millis();

            // determines the key that was pressed and what to do with it
            if (awake) {
                if (newKey == '#') {
                    button1Pressed = true;
                }
                else if (newKey != '*') {
                    if (val == "0") {
                        val = "";
                    }
                    val += newKey;
                }
                else if (newKey == '*') {
                    int len = val.length();
                    if (len > 1) {
                        val.remove(len - 1);
                    }
                    else if (len == 1) {
                        val = "0";
                    }
                    button2Pressed = true;
                }
            }
            // If the program is "asleep" (i.e. the sleep timer timed out and the program
            // went into standby mode), a key press will wake the program up.
            else {
                awake = true;
                newMode = true;
            }
        }
        // If the # key is pressed, this next function will be run to change the mode and perform
        // whatever function goes along with the mode change.
        if (button1Pressed) {
            // Converts the String val to an integer that will be set as the corresponding value.
            int valInt = val.toInt();
            bool goodVal = true;
            if (valInt > 255 || valInt == -1) {
                goodVal = false;
            }
            val = "0";
            switch(mode) {
                case SetRed:
                {
                    if (goodVal) {
                        red = valInt;
                        mode = SetGreen;
                        newMode = true;
                    }
                    break;
                }
                case SetGreen:
                {
                    if (goodVal) {
                        green = valInt;
                        mode = SetBlue;
                        newMode = true;
                    }
                    break;
                }
                case SetBlue:
                {
                    if (goodVal) {
                        blue = valInt;
                        mode = Publish;
                        newMode = true;
                    }
                    break;
                }
                case Brightness:
                {
                    if (goodVal) {
                        brightness = valInt;
                        mode = Publish;
                        newMode = true;
                    }
                    break;
                }
                // If the mode is Publish and the # key is pressed, the values are
                // reset and the program starts over.
                case Publish:
                {
                    mode = SetRed;
                    red = 0;
                    green = 0;
                    blue = 0;
                    brightness = 255;
                    newMode = true;
                    break;
                }
                default:
                {
                    break;
                }
            }
        }
        // If the mode is Publish and the * key is pressed, the mode is changed to
        // Brightness. This is the only way to set the brightness.
        if (button2Pressed) {
            if (mode == Publish) {
                newMode = true;
                mode = Brightness;
                val = "0";
            }
        }
    }
    // the sleep timer
    if (timerOn) {
        if (millis() - sleepAccumulator > sleepDelay) {
            timerOn = false;
            awake = false;
            newMode = true;
            // System.sleep(D6,CHANGE);
        }
    }
    // the display loop
    if (millis() - displayAccumulator > displayDelay) {
        displayAccumulator = millis();
        if (awake) {
            // readerDelay = readerActiveDelay;
            switch(mode) {
                case SetRed:
                {
                    if (newMode) {
                        print("Set Red:", 0);
                        newMode = false;
                    }
                    print(val, 1);
                    break;
                }
                case SetGreen:
                {
                    if (newMode) {
                        print("Set Green:", 0);
                        newMode = false;
                    }
                    print(val, 1);
                    break;
                }
                case SetBlue:
                {
                    if (newMode) {
                        print("Set Blue:", 0);
                        newMode = false;
                    }
                    print(val, 1);
                    break;
                }
                case Brightness:
                {
                    if (newMode) {
                        print("Set Brightness:", 0);
                        newMode = false;
                    }
                    print(val, 1);
                }
                case Publish:
                {
                    // If the program has just changed to Publish, the new RGB+Brightness value is
                    // published to the Particle Cloud.
                    if (newMode) {
                        String str = "";
                        str += red;
                        str += ",";
                        str += green;
                        str += ",";
                        str += blue;
                        str += ";";
                        str += brightness;
                        Particle.publish("newColor1997319", str);
                        print("New Color:", 0);
                        print(str, 1);
                        newMode = false;
                    }
                    break;
                }
                default:
                {
                    break;
                }
            }
            // Update the RGB with the most recent colors.
            RGB.color(red, green, blue);
            RGB.brightness(brightness);
        }
        // If the program isn't awake, the display is cleared to save battery. Before, I'd had
        // the reader loop go into standby to save power, but it affected the responsiveness of
        // the program so I commented it out.
        else {
            if (newMode) {
                SeeedOled.clearDisplay();
                displayOn = false;
                // readerDelay = readerSleepDelay;
            }
        }

        // This is the part of the code that keeps the processor from stalling. What it does is write a space (which
        // doesn't actually print anything) to one space each display cycle. The cursor position is incremented each
        // cycle to ensure an even wear on the display (I'm not even sure if writing an empty space wears the pixels
        // but I figured better safe then sorry).
        SeeedOled.setTextXY(r,c);
        SeeedOled.putString(" ");
        c++;
        if (c > 15) {
            c = 0;
            r++;
            if (r > 7) r = 2;
        }

        // Each display cycle, if a key was pressed, I update the saved state in EEPROM. I only update it if a key
        // is pressed because no changes will have been made if no keys were pressed.
        if (newKey != NULL) {
            newSave = {val.toInt(), red, green, blue, brightness, mode};
            saved = false;
        }
    }
    if (millis() - saveAccumulator > saveDelay) {
        saveAccumulator = millis();
        if (!saved) {
            if (EEPROM.length() > sizeof(newSave)-saveAddress) {
                EEPROM.put(saveAddress, newSave);
            }
            saved = true;
        }
    }
}

// This is my alternative to clearing the display each time I update it. What it does is take the String I want
// printed and, depending on its length, add that many empty spaces to the end of the line. This turns out to be
// MUCH faster than clearing the screen, or even than clearing the row, before writing to it.
void print(String str, int row) {
    SeeedOled.setTextXY(row,0);
    SeeedOled.putString(str);
    int len = str.length();
    String extra = "";
    for (int i = 0; i < 16-len; i++) {
        extra += " ";
    }
    SeeedOled.putString(extra);
}
