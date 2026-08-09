#include <Arduino.h>
#include <Adafruit_TinyUSB.h>
#include <AlfredoCRSF.h>

// Standard Gamepad HID Descriptor
uint8_t const desc_hid_report[] = {
  TUD_HID_REPORT_DESC_GAMEPAD()
};

Adafruit_USBD_HID usb_hid;

typedef struct ATTR_PACKED {
  int8_t  x;         // CH1: Roll
  int8_t  y;         // CH2: Pitch
  int8_t  z;         // CH3: Throttle
  int8_t  rz;        // CH4: Yaw
  int8_t  rx;        // CH5: Slider S1
  int8_t  ry;        // CH6: Slider S2
  uint8_t hat;       // Hat switch (0 = centered)
  uint32_t buttons;  // Bitfield for Buttons 1-12 (CH7-CH14)
} hid_custom_gamepad_report_t;

hid_custom_gamepad_report_t gp;
AlfredoCRSF crsf;

void setup() {
  pinMode(LED_GREEN, OUTPUT);
  digitalWrite(LED_GREEN, HIGH); // Off initially

  // Ultra-fast 1ms USB HID polling rate
  usb_hid.setPollInterval(1);
  usb_hid.setReportDescriptor(desc_hid_report, sizeof(desc_hid_report));
  usb_hid.begin();

  // Hardware UART on D5 (RX) / D6 (TX)
  Serial1.setPins(D5, D6);
  Serial1.begin(420000);
  NRF_UARTE0->BAUDRATE = 0x06B50000; // Exact 420k timing override

  crsf.begin(Serial1);
  memset(&gp, 0, sizeof(gp));
}

void loop() {
  crsf.update();

  if (crsf.isLinkUp()) {
    digitalWrite(LED_GREEN, LOW); // Green LED ON solid when linked

    // --- 1. STICKS (CH1 - CH4) ---
    gp.x  = (int8_t)map(constrain(crsf.getChannel(1), 988, 2012), 988, 2012, -127, 127);
    gp.y  = (int8_t)map(constrain(crsf.getChannel(2), 988, 2012), 988, 2012, -127, 127);
    gp.z  = (int8_t)map(constrain(crsf.getChannel(3), 988, 2012), 988, 2012, -127, 127);
    gp.rz = (int8_t)map(constrain(crsf.getChannel(4), 988, 2012), 988, 2012, -127, 127);

    // --- 2. SLIDERS / DIALS (CH5 & CH6) ---
    gp.rx = (int8_t)map(constrain(crsf.getChannel(5), 988, 2012), 988, 2012, -127, 127);
    gp.ry = (int8_t)map(constrain(crsf.getChannel(6), 988, 2012), 988, 2012, -127, 127);

    uint32_t btnState = 0;

    // --- 3. TOGGLES & MOMENTARIES (CH7 - CH10 -> Buttons 1 to 4) ---
    // SA, SD, SG, SH: High (>1500us) turns on single button
    if (crsf.getChannel(7)  > 1500) btnState |= (1UL << 0); // Button 1 (SA)
    if (crsf.getChannel(8)  > 1500) btnState |= (1UL << 1); // Button 2 (SD)
    if (crsf.getChannel(9)  > 1500) btnState |= (1UL << 2); // Button 3 (SG)
    if (crsf.getChannel(10) > 1500) btnState |= (1UL << 3); // Button 4 (SH)

    // --- 4. CUMULATIVE 3-POS SWITCHES (CH11 - CH14 -> Buttons 5 to 12) ---
    // SE, SB, SC, SF:
    // Pushed Away (<1300us)   -> Off (No buttons)
    // Middle      (1300-1700) -> Button A ON
    // Pulled Near (>1700us)   -> Button A ON + Button B ON
    uint8_t btnIndex = 4; // Start at Button 5 bit offset

    for (uint8_t ch = 11; ch <= 14; ch++) {
      int val = crsf.getChannel(ch);

      if (val >= 1300 && val <= 1700) {
        // Mid position: Turn on first button
        btnState |= (1UL << btnIndex);
      } 
      else if (val > 1700) {
        // Fully pulled position: Keep first button ON AND turn on second button
        btnState |= (1UL << btnIndex);
        btnState |= (1UL << (btnIndex + 1));
      }
      // If val < 1300 (pushed away), neither bit is set (Off)

      btnIndex += 2; // Allocate 2 button bits per 3-pos switch
    }

    gp.buttons = btnState;

    if (usb_hid.ready()) {
      usb_hid.sendReport(0, &gp, sizeof(gp));
    }
  } else {
    digitalWrite(LED_GREEN, HIGH); // Off if link drops
  }
}