#include <TFT_eSPI.h>
#include <SPI.h>
#include <SD.h>
#include <FS.h>

TFT_eSPI tft = TFT_eSPI();

enum ScreenState { SPLASH, MAIN_MENU, SETTINGS_MENU, APP_FILEMAN, APP_BLUETOOTH, APP_WIFIPACK, APP_WIFI, APP_JAMMER, APP_TEXTEDITOR, FILE_DIALOG };
ScreenState currentScreen = SPLASH;
ScreenState previousScreen = MAIN_MENU;

unsigned long splashStartTime;

String appList[] = {"fileman", "bluetooth", "wifipack", "wifi", "jammer", "text editor"};
int appCount = sizeof(appList) / sizeof(appList[0]);
int appIndex = 0;

String settingsList[] = {"WiFi", "General", "Controls"};
int settingsCount = sizeof(settingsList) / sizeof(settingsList[0]);
int settingsIndex = 0;

// File manager variables
String currentPath = "/";
String fileList[50];
bool isDirectory[50];
int fileCount = 0;
int fileIndex = 0;
String clipboardPath = "";
int clipboardAction = 0; // 0=none, 1=copy, 2=cut

// File dialog variables
String dialogOptions[] = {"Copy", "Cut", "Paste", "Delete", "Close"};
int dialogCount = 5;
int dialogIndex = 0;
String selectedFilePath = "";

#define BTN_UP     35   // GPIO35 - input only, good for buttons
#define BTN_DOWN   34   // GPIO34 - input only, good for buttons  
#define BTN_LEFT   39   // GPIO39 - input only, good for buttons
#define BTN_RIGHT  36   // GPIO36 - input only, good for buttons
#define BTN_SELECT 0    // GPIO0 - BOOT button, safe to use
#define SD_CS      5    // SD card chip select pin

uint16_t splashBitmap[160 * 80];

// Icon bitmaps - keep empty for now, you can paste your icons here later
uint16_t fileIconBitmap[10 * 10]; // 10x10 file icon
uint16_t settingsIconBitmap[10 * 10]; // 10x10 settings cog icon
uint16_t wifiConnectedBitmap[12 * 8]; // 12x8 wifi connected icon
uint16_t wifiDisconnectedBitmap[12 * 8]; // 12x8 wifi disconnected icon

bool isWifiConnected = false; // You can update this based on actual wifi status

// Button state tracking for proper debouncing
struct ButtonState {
  bool lastState;
  bool currentState;
  unsigned long lastChange;
  bool pressed;
};

ButtonState buttons[5]; // For 5 buttons: UP, DOWN, LEFT, RIGHT, SELECT
const int buttonPins[] = {BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT, BTN_SELECT};
const int DEBOUNCE_DELAY = 50;

void generateHeartSplash() {
  for (int y = 0; y < 80; y++) {
    for (int x = 0; x < 160; x++) {
      bool inHeart = (x > 60 && x < 80 && y > 25 && y < 45) || 
                     (x > 80 && x < 100 && y > 25 && y < 45) || 
                     ((x - 80)*(x - 80) + (y - 50)*(y - 50) < 400);
      splashBitmap[y * 160 + x] = inHeart ? TFT_RED : TFT_BLACK;
    }
  }
}

void updateButtons() {
  unsigned long currentTime = millis();
  
  for (int i = 0; i < 5; i++) {
    bool reading = digitalRead(buttonPins[i]);
    
    // Reset pressed flag
    buttons[i].pressed = false;
    
    // Check if button state changed
    if (reading != buttons[i].lastState) {
      buttons[i].lastChange = currentTime;
    }
    
    // If enough time has passed since last change
    if ((currentTime - buttons[i].lastChange) > DEBOUNCE_DELAY) {
      // If the button state has actually changed
      if (reading != buttons[i].currentState) {
        buttons[i].currentState = reading;
        
        // Button was just pressed (HIGH to LOW transition for pull-up buttons)
        if (buttons[i].currentState == LOW && buttons[i].lastState == HIGH) {
          buttons[i].pressed = true;
        }
      }
    }
    
    buttons[i].lastState = reading;
  }
}

bool isButtonPressed(int buttonIndex) {
  return buttons[buttonIndex].pressed;
}

char getInput() {
  if (Serial.available()) return Serial.read();
  return '\0';
}

void scanDirectory(String path) {
  fileCount = 0;
  File root = SD.open(path);
  if (!root) {
    return;
  }
  
  File file = root.openNextFile();
  while (file && fileCount < 50) {
    String fileName = file.name();
    if (!fileName.startsWith(".")) { // Skip hidden files
      fileList[fileCount] = fileName;
      isDirectory[fileCount] = file.isDirectory();
      fileCount++;
    }
    file.close();
    file = root.openNextFile();
  }
  root.close();
  
  if (fileIndex >= fileCount) fileIndex = max(0, fileCount - 1);
}

void drawPathBar() {
  tft.fillRect(0, 0, 160, 12, TFT_DARKGREY);
  tft.setCursor(2, 2);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(1);
  String displayPath = currentPath;
  if (displayPath.length() > 20) {
    displayPath = "..." + displayPath.substring(displayPath.length() - 17);
  }
  tft.println(displayPath);
}

void drawFileManager() {
  tft.fillScreen(TFT_BLACK);
  drawPathBar();
  
  int visible = 5;
  int start = constrain(fileIndex - (visible/2), 0, max(0, fileCount - visible));
  
  for (int i = 0; i < min(visible, fileCount); i++) {
    int idx = start + i;
    if (idx >= fileCount) break;
    
    int y = 15 + i * 12;
    if (idx == fileIndex) {
      tft.fillRect(0, y - 1, 120, 12, TFT_DARKGREY);
    }
    tft.setCursor(2, y);
    tft.setTextColor(TFT_WHITE, idx == fileIndex ? TFT_DARKGREY : TFT_BLACK);
    tft.setTextSize(1);
    
    String displayName = (isDirectory[idx] ? "[" : "") + fileList[idx] + (isDirectory[idx] ? "]" : "");
    if (displayName.length() > 18) {
      displayName = displayName.substring(0, 15) + "...";
    }
    tft.println(displayName);
  }
  
  // Scrollbar
  if (fileCount > visible) {
    int barHeight = max(5, (68 * visible) / fileCount);
    int barTop = 12 + map(fileIndex, 0, fileCount - 1, 0, 68 - barHeight);
    tft.fillRect(120, 12, 3, 68, TFT_DARKGREY);
    tft.fillRect(120, barTop, 3, barHeight, TFT_WHITE);
  }
  
  drawRightUI(APP_FILEMAN);
}

void drawFileDialog() {
  // Semi-transparent background
  tft.fillRect(20, 20, 120, 40, TFT_BLACK);
  tft.drawRect(20, 20, 120, 40, TFT_WHITE);
  
  tft.setCursor(25, 25);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(1);
  tft.println("File Actions:");
  
  for (int i = 0; i < dialogCount; i++) {
    int y = 35 + i * 10;
    if (i == dialogIndex) {
      tft.fillRect(25, y - 1, 110, 9, TFT_DARKGREY);
    }
    tft.setCursor(27, y);
    tft.setTextColor(TFT_WHITE, i == dialogIndex ? TFT_DARKGREY : TFT_BLACK);
    tft.println(dialogOptions[i]);
  }
}

void executeFileAction() {
  String fullPath = currentPath + "/" + fileList[fileIndex];
  
  switch (dialogIndex) {
    case 0: // Copy
      clipboardPath = fullPath;
      clipboardAction = 1;
      break;
      
    case 1: // Cut
      clipboardPath = fullPath;
      clipboardAction = 2;
      break;
      
    case 2: // Paste
      if (clipboardAction > 0) {
        String fileName = clipboardPath.substring(clipboardPath.lastIndexOf('/') + 1);
        String newPath = currentPath + "/" + fileName;
        
        if (clipboardAction == 1) { // Copy
          // Implement copy logic here
        } else if (clipboardAction == 2) { // Cut
          SD.rename(clipboardPath.c_str(), newPath.c_str());
          clipboardAction = 0;
        }
        scanDirectory(currentPath);
      }
      break;
      
    case 3: // Delete
      if (SD.remove(fullPath.c_str()) || SD.rmdir(fullPath.c_str())) {
        scanDirectory(currentPath);
      }
      break;
      
    case 4: // Close
      break;
  }
  
  currentScreen = APP_FILEMAN;
  drawFileManager();
}

void drawRightUI(ScreenState state) {
  tft.fillRect(150, 0, 10, 80, TFT_BLACK);
  
  // WiFi status icon at top
  if (isWifiConnected) {
    // Draw wifi connected icon (empty bitmap for now)
    tft.pushImage(152, 5, 12, 8, wifiConnectedBitmap);
  } else {
    // Draw wifi disconnected icon (empty bitmap for now)
    tft.pushImage(152, 5, 12, 8, wifiDisconnectedBitmap);
  }
  
  // App/Settings icon in center
  int centerY = 35;
  if (state == MAIN_MENU) {
    // Draw file icon (empty bitmap for now)
    tft.pushImage(155, centerY, 10, 10, fileIconBitmap);
  } else if (state == SETTINGS_MENU) {
    // Draw settings cog icon (empty bitmap for now)
    tft.pushImage(155, centerY, 10, 10, settingsIconBitmap);
  }
}

void drawMainMenu() {
  tft.fillScreen(TFT_BLACK);
  int visible = 4;
  int start = constrain(appIndex - (visible/2), 0, max(0, appCount - visible));

  for (int i = 0; i < min(visible, appCount); i++) {
    int idx = start + i;
    if (idx >= appCount) break;
    
    int y = 5 + i * 18;
    if (idx == appIndex) {
      tft.fillRect(0, y - 2, 120, 18, TFT_DARKGREY);
    }
    tft.setCursor(5, y);
    tft.setTextColor(TFT_WHITE, idx == appIndex ? TFT_DARKGREY : TFT_BLACK);
    tft.setTextSize(1);
    tft.println(appList[idx]);
  }

  // Scrollbar - moved to touch the selection area
  if (appCount > visible) {
    int barHeight = max(10, (80 * visible) / appCount);
    int barTop = map(appIndex, 0, appCount - 1, 0, 80 - barHeight);
    tft.fillRect(120, 0, 3, 80, TFT_DARKGREY); // Moved from 145 to 120
    tft.fillRect(120, barTop, 3, barHeight, TFT_WHITE);
  }

  drawRightUI(MAIN_MENU);
}

void drawSettingsMenu() {
  tft.fillScreen(TFT_BLACK);
  for (int i = 0; i < settingsCount; i++) {
    int y = 10 + i * 20;
    if (i == settingsIndex) {
      tft.fillRect(0, y - 2, 120, 18, TFT_DARKGREY); // Changed from TFT_NAVY to TFT_DARKGREY
    }
    tft.setCursor(5, y);
    tft.setTextColor(TFT_WHITE, i == settingsIndex ? TFT_DARKGREY : TFT_BLACK);
    tft.setTextSize(1);
    tft.println(settingsList[i]);
  }

  // Scrollbar for settings menu - same style as main menu
  if (settingsCount > 0) {
    int barHeight = max(10, (80 * 3) / settingsCount); // Assuming 3 visible items
    int barTop = map(settingsIndex, 0, settingsCount - 1, 0, 80 - barHeight);
    tft.fillRect(120, 0, 3, 80, TFT_DARKGREY); // Same position as main menu
    tft.fillRect(120, barTop, 3, barHeight, TFT_WHITE);
  }

  drawRightUI(SETTINGS_MENU);
}

// Individual app functions
void runFileManager() {
  if (!SD.begin(SD_CS)) {
    tft.fillScreen(TFT_BLACK);
    tft.setCursor(10, 30);
    tft.setTextColor(TFT_RED);
    tft.println("SD Card not found!");
    delay(2000);
    currentScreen = MAIN_MENU;
    drawMainMenu();
    return;
  }
  
  currentPath = "/";
  fileIndex = 0;
  scanDirectory(currentPath);
  drawFileManager();
}

void runBluetooth() {
  tft.fillScreen(TFT_BLACK);
  tft.setCursor(10, 30);
  tft.setTextColor(TFT_BLUE);
  tft.setTextSize(1);
  tft.println("Bluetooth Manager");
  tft.setCursor(10, 45);
  tft.println("Press LEFT to go back");
}

void runWifiPack() {
  tft.fillScreen(TFT_BLACK);
  tft.setCursor(10, 30);
  tft.setTextColor(TFT_GREEN);
  tft.setTextSize(1);
  tft.println("WiFi Pack Tools");
  tft.setCursor(10, 45);
  tft.println("Press LEFT to go back");
}

void runWifi() {
  tft.fillScreen(TFT_BLACK);
  tft.setCursor(10, 30);
  tft.setTextColor(TFT_CYAN);
  tft.setTextSize(1);
  tft.println("WiFi Manager");
  tft.setCursor(10, 45);
  tft.println("Press LEFT to go back");
}

void runJammer() {
  tft.fillScreen(TFT_BLACK);
  tft.setCursor(10, 30);
  tft.setTextColor(TFT_RED);
  tft.setTextSize(1);
  tft.println("Signal Jammer");
  tft.setCursor(10, 45);
  tft.println("Press LEFT to go back");
}

void runTextEditor() {
  tft.fillScreen(TFT_BLACK);
  tft.setCursor(10, 30);
  tft.setTextColor(TFT_YELLOW);
  tft.setTextSize(1);
  tft.println("Text Editor");
  tft.setCursor(10, 45);
  tft.println("Press LEFT to go back");
}

void launchApp(int appIdx) {
  previousScreen = MAIN_MENU;
  
  switch (appIdx) {
    case 0: // fileman
      currentScreen = APP_FILEMAN;
      runFileManager();
      break;
    case 1: // bluetooth
      currentScreen = APP_BLUETOOTH;
      runBluetooth();
      break;
    case 2: // wifipack
      currentScreen = APP_WIFIPACK;
      runWifiPack();
      break;
    case 3: // wifi
      currentScreen = APP_WIFI;
      runWifi();
      break;
    case 4: // jammer
      currentScreen = APP_JAMMER;
      runJammer();
      break;
    case 5: // text editor
      currentScreen = APP_TEXTEDITOR;
      runTextEditor();
      break;
  }
}

void setup() {
  Serial.begin(115200);
  tft.init();
  tft.setRotation(3);  // Fixed rotation
  
  // Initialize button pins and states
  for (int i = 0; i < 5; i++) {
    pinMode(buttonPins[i], INPUT_PULLUP);  // Using internal pull-up resistors
    buttons[i].lastState = digitalRead(buttonPins[i]);
    buttons[i].currentState = buttons[i].lastState;
    buttons[i].lastChange = 0;
    buttons[i].pressed = false;
  }
  
  generateHeartSplash();
  tft.pushImage(0, 0, 160, 80, splashBitmap);
  splashStartTime = millis();
  
  // Only essential startup message
  Serial.println("System Ready");
  Serial.println("Controls: WASD keys OR 5-button navigation");
  Serial.println("Buttons: UP(35) DOWN(34) LEFT(39) RIGHT(36) SELECT(0)");
  Serial.println("Connect buttons between pins and GND, R key for file actions");
}

void loop() {
  updateButtons(); // Update button states every loop
  char c = getInput();

  switch (currentScreen) {
    case SPLASH:
      if (millis() - splashStartTime > 2000) {
        currentScreen = MAIN_MENU;
        drawMainMenu();
      }
      break;

    case MAIN_MENU:
      if (isButtonPressed(1) || c == 's') { // BTN_DOWN or 's' key
        if (appIndex < appCount - 1) {
          appIndex++;
          drawMainMenu();
        }
      }
      if (isButtonPressed(0) || c == 'w') { // BTN_UP or 'w' key
        if (appIndex > 0) {
          appIndex--;
          drawMainMenu();
        }
      }
      if (isButtonPressed(4) || c == 'e') { // BTN_SELECT or 'e' key
        launchApp(appIndex);
      }
      if (isButtonPressed(3) || c == 'd') { // BTN_RIGHT or 'd' key
        currentScreen = SETTINGS_MENU;
        settingsIndex = 0; // Reset settings index
        drawSettingsMenu();
      }
      break;

    case SETTINGS_MENU:
      if (isButtonPressed(1) || c == 's') { // BTN_DOWN or 's' key
        if (settingsIndex < settingsCount - 1) {
          settingsIndex++;
          drawSettingsMenu();
        }
      }
      if (isButtonPressed(0) || c == 'w') { // BTN_UP or 'w' key
        if (settingsIndex > 0) {
          settingsIndex--;
          drawSettingsMenu();
        }
      }
      if (isButtonPressed(2) || c == 'a') { // BTN_LEFT or 'a' key
        currentScreen = MAIN_MENU;
        drawMainMenu();
      }
      if (isButtonPressed(4) || c == 'e') { // BTN_SELECT or 'e' key
        tft.fillScreen(TFT_BLACK);
        tft.setCursor(10, 30);
        tft.setTextSize(1);
        tft.setTextColor(TFT_CYAN);
        tft.println("Opening " + settingsList[settingsIndex]);
        delay(1500);
        drawSettingsMenu();
      }
      break;

    case APP_FILEMAN:
      if (isButtonPressed(1) || c == 's') { // BTN_DOWN or 's' key
        if (fileIndex < fileCount - 1) {
          fileIndex++;
          drawFileManager();
        }
      }
      if (isButtonPressed(0) || c == 'w') { // BTN_UP or 'w' key
        if (fileIndex > 0) {
          fileIndex--;
          drawFileManager();
        }
      }
      if (isButtonPressed(4) || c == 'e') { // BTN_SELECT or 'e' key
        if (fileCount > 0) {
          if (isDirectory[fileIndex]) {
            String newPath = currentPath;
            if (!currentPath.endsWith("/")) newPath += "/";
            newPath += fileList[fileIndex];
            currentPath = newPath;
            fileIndex = 0;
            scanDirectory(currentPath);
            drawFileManager();
          }
        }
      }
      if (isButtonPressed(2) || c == 'a') { // BTN_LEFT or 'a' key - Go back or up directory
        if (currentPath != "/") {
          int lastSlash = currentPath.lastIndexOf('/');
          if (lastSlash > 0) {
            currentPath = currentPath.substring(0, lastSlash);
          } else {
            currentPath = "/";
          }
          fileIndex = 0;
          scanDirectory(currentPath);
          drawFileManager();
        } else {
          currentScreen = MAIN_MENU;
          drawMainMenu();
        }
      }
      if (isButtonPressed(3) || c == 'r') { // BTN_RIGHT or 'r' key - File actions
        if (fileCount > 0) {
          selectedFilePath = currentPath + "/" + fileList[fileIndex];
          currentScreen = FILE_DIALOG;
          dialogIndex = 0;
          drawFileDialog();
        }
      }
      break;

    case FILE_DIALOG:
      if (isButtonPressed(1) || c == 's') { // BTN_DOWN or 's' key
        if (dialogIndex < dialogCount - 1) {
          dialogIndex++;
          drawFileDialog();
        }
      }
      if (isButtonPressed(0) || c == 'w') { // BTN_UP or 'w' key
        if (dialogIndex > 0) {
          dialogIndex--;
          drawFileDialog();
        }
      }
      if (isButtonPressed(4) || c == 'e') { // BTN_SELECT or 'e' key
        executeFileAction();
      }
      if (isButtonPressed(2) || c == 'a') { // BTN_LEFT or 'a' key - Cancel
        currentScreen = APP_FILEMAN;
        drawFileManager();
      }
      break;

    case APP_BLUETOOTH:
    case APP_WIFIPACK:
    case APP_WIFI:
    case APP_JAMMER:
    case APP_TEXTEDITOR:
      if (isButtonPressed(2) || c == 'a') { // BTN_LEFT or 'a' key - Go back
        currentScreen = MAIN_MENU;
        drawMainMenu();
      }
      break;
  }
  
  // Small delay to prevent overwhelming the system
  delay(10);
}
