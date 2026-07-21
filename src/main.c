#include <stdint.h>
#include <stdbool.h>
#include "gpu.h"

// --- APP STATES ---
enum AppState {
    STATE_MENU,
    STATE_3D_MODEL,
    STATE_XMB_WAVES
};

enum AppState current_state = STATE_MENU;
int menu_selection = 0;       
int frame_counter = 0;        

// --- 256-POINT SINE LUT (Amplitude +/- 40) ---
const short SINE_LUT[256] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 
    20, 21, 22, 23, 24, 25, 26, 26, 27, 28, 29, 30, 30, 31, 32, 32, 33, 34, 
    34, 35, 35, 36, 36, 37, 37, 38, 38, 38, 39, 39, 39, 40, 40, 40, 40, 40, 
    40, 40, 40, 40, 40, 40, 40, 40, 40, 39, 39, 39, 38, 38, 38, 37, 37, 36, 
    36, 35, 35, 34, 34, 33, 32, 32, 31, 30, 30, 29, 28, 27, 26, 26, 25, 24, 
    23, 22, 21, 20, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 
    4, 3, 2, 1, 0, -1, -2, -3, -4, -5, -6, -7, -8, -9, -10, -11, -12, -13, 
    -14, -15, -16, -17, -18, -19, -20, -21, -22, -23, -24, -25, -26, -26, 
    -27, -28, -29, -30, -30, -31, -32, -32, -33, -34, -34, -35, -35, -36, 
    -36, -37, -37, -38, -38, -38, -39, -39, -39, -40, -40, -40, -40, -40, 
    -40, -40, -40, -40, -40, -40, -40, -40, -40, -39, -39, -39, -38, -38, 
    -38, -37, -37, -36, -36, -35, -35, -34, -34, -33, -32, -32, -31, -30, 
    -30, -29, -28, -27, -26, -26, -25, -24, -23, -22, -21, -20, -19, -18, 
    -17, -16, -15, -14, -13, -12, -11, -10, -9, -8, -7, -6, -5, -4, -3, -2, -1
};

// --- NATIVE SDK WAVE DRAWING (Using PolyF4 to ensure full SDK compatibility) ---
#define WAVE_SEGMENTS 16
#define SCREEN_WIDTH 320

void DrawXMBWave(int frame, int base_y, int phase_gap, int speed, uint8_t r, uint8_t g, uint8_t b) {
    int i;
    int seg_width = SCREEN_WIDTH / WAVE_SEGMENTS;
    
    for (i = 0; i < WAVE_SEGMENTS; i++) {
        int idx0 = ((i * phase_gap) + (frame * speed)) & 255;
        int idx1 = (((i + 1) * phase_gap) + (frame * speed)) & 255;
        
        int y_offset0 = SINE_LUT[idx0];
        int y_offset1 = SINE_LUT[idx1];

        PolyF4 p;
        p.code = 0x28; // POLY_F4 command code in ps1-bare-metal
        
        p.x0 = i * seg_width;         p.y0 = base_y + y_offset0 - 10;
        p.x1 = (i + 1) * seg_width;   p.y1 = base_y + y_offset1 - 10;
        p.x2 = i * seg_width;         p.y2 = base_y + y_offset0 + 40;
        p.x3 = (i + 1) * seg_width;   p.y3 = base_y + y_offset1 + 40;

        // Base wave tint
        p.r = r; p.g = g; p.b = b;

        draw_poly_f4(&p);
    }
}

int main(void) {
    // Initialize the spinning cube demo setup or base context
    uint16_t pad_old = 0;
    uint16_t pad_new = 0;

    while (1) {
        // Read controls if available from the example environment
        uint16_t pad = 0; 
        pad_new = pad & ~pad_old;
        pad_old = pad;

        if (current_state == STATE_MENU) {
            if (pad_new & 0x10) { menu_selection = 0; }
            if (pad_new & 0x40) { menu_selection = 1; }
            if (pad_new & 0x20) { 
                if (menu_selection == 0) current_state = STATE_3D_MODEL;
                else current_state = STATE_XMB_WAVES;
            }

        } else if (current_state == STATE_3D_MODEL) {
            // ==========================================
            // SPINNING CUBE MODEL RENDERING GOES HERE
            // ==========================================

            if (pad_new & 0x80) { current_state = STATE_MENU; }

        } else if (current_state == STATE_XMB_WAVES) {
            // Render multi-layered XMB wave background
            DrawXMBWave(frame_counter, 110, 12, 2,  30, 100, 120);
            DrawXMBWave(frame_counter, 130, 16, 3,  50, 180, 200);
            DrawXMBWave(frame_counter, 150, 20, 4, 120, 210, 255);

            frame_counter++;

            if (pad_new & 0x80) { current_state = STATE_MENU; }
        }
    }

    return 0;
}
