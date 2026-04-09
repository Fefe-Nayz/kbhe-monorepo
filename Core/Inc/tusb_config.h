/*
 * tusb_config.h
 * Configuration TinyUSB pour clavier HID USB High Speed 8kHz
 * STM32F723VET6 avec PHY HS intégré
 */

#ifndef TUSB_CONFIG_H_
#define TUSB_CONFIG_H_

#ifdef __cplusplus
extern "C" {
#endif

//--------------------------------------------------------------------+
// Board Specific Configuration
//--------------------------------------------------------------------+

// MCU STM32F7
#define CFG_TUSB_MCU OPT_MCU_STM32F7

// RHPort configuration
// STM32F723: Port 0 = USB FS, Port 1 = USB HS avec PHY intégré
// On utilise Port 1 (USB HS) en mode Device High Speed
#define CFG_TUSB_RHPORT1_MODE (OPT_MODE_DEVICE | OPT_MODE_HIGH_SPEED)

//--------------------------------------------------------------------+
// COMMON CONFIGURATION
//--------------------------------------------------------------------+

// No RTOS
#define CFG_TUSB_OS OPT_OS_NONE

// Debug level (0: no debug, 1: error, 2: warning, 3: info)
#ifndef CFG_TUSB_DEBUG
#define CFG_TUSB_DEBUG 1
#endif

// USB DMA sur STM32F7 - alignement 4 bytes requis
#define CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_ALIGN __attribute__((aligned(4)))

//--------------------------------------------------------------------+
// DEVICE CONFIGURATION
//--------------------------------------------------------------------+

// Enable Device stack
#define CFG_TUD_ENABLED 1

// Endpoint 0 max packet size (64 pour High Speed)
#define CFG_TUD_ENDPOINT0_SIZE 64

//--------------------------------------------------------------------+
// CLASS CONFIGURATION - HID uniquement
//--------------------------------------------------------------------+
#define CFG_TUD_HID 5 // Keyboard + Raw HID + Gamepad + NKRO Keyboard + Consumer
#define CFG_TUD_CDC 0
#define CFG_TUD_MSC 0
#define CFG_TUD_MIDI 0
#define CFG_TUD_VENDOR 0

// HID buffer size: doit contenir ID (optionnel) + données du rapport
// Keyboard report = 8 bytes (modifier + reserved + 6 keycodes)
#define CFG_TUD_HID_EP_BUFSIZE 64

//--------------------------------------------------------------------+
// DWC2 Specific Configuration (Synopsys DWC2 IP)
//--------------------------------------------------------------------+

// Utiliser le mode Slave (pas DMA) - plus simple et fiable
#define CFG_TUD_DWC2_SLAVE_ENABLE 1
#define CFG_TUD_DWC2_DMA_ENABLE 0

#ifdef __cplusplus
}
#endif

#endif /* TUSB_CONFIG_H_ */
