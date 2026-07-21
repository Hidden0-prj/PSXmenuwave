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

// --- NATIVE SDK WAVE DRAWING ---
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

        // Construct native ps1-bare-metal Gouraud quadrilateral block
        PolyG4 p;
        p.code = 0x38; // POLY_G4 command header with shading
        
        p.x0 = i * seg_width;         p.y0 = base_y + y_offset0 - 10;
        p.x1 = (i + 1) * seg_width;   p.y1 = base_y + y_offset1 - 10;
        p.x2 = i * seg_width;         p.y2 = base_y + y_offset0 + 50;
        p.x3 = (i + 1) * seg_width;   p.y3 = base_y + y_offset1 + 50;

        // Top gradient color
        p.r0 = r; p.g0 = g; p.b0 = b;
        p.r1 = r; p.g1 = g; p.b1 = b;
        
        // Bottom fades to black for clean background blending
        p.r2 = 0; p.g2 = 0; p.b2 = 0;
        p.r3 = 0; p.g3 = 0; p.b3 = 0;

        draw_poly_g4(&p);
    }
}

int main(void) {
    gpu_init();

    uint16_t pad_old = 0;
    uint16_t pad_new = 0;

    while (1) {
        gpu_update();

        // Simple polling read pattern common in bare-metal setups
        uint16_t pad = 0; // If your SDK provides a controller read function, replace here.
        pad_new = pad & ~pad_old;
        pad_old = pad;

        if (current_state == STATE_MENU) {
            // Basic layout rendering
            if (pad_new & 0x10) { // Example Up input trigger
                menu_selection = 0;
            }
            if (pad_new & 0x40) { // Example Down input trigger
                menu_selection = 1;
            }
            if (pad_new & 0x20) { // Example Cross/Select trigger
                if (menu_selection == 0) current_state = STATE_3D_MODEL;
                else current_state = STATE_XMB_WAVES;
            }

        } else if (current_state == STATE_3D_MODEL) {
            // ==========================================
            // PASTE YOUR SPINNING CUBE / 3D MODEL LOGIC HERE
            // ==========================================

            if (pad_new & 0x80) { // Example Triangle/Back trigger
                current_state = STATE_MENU;
            }

        } else if (current_state == STATE_XMB_WAVES) {
            // Render multi-layered XMB wave background
            DrawXMBWave(frame_counter, 120, 12, 2,  30, 100, 120);
            DrawXMBWave(frame_counter, 135, 16, 3,  50, 180, 200);
            DrawXMBWave(frame_counter, 150, 20, 4, 150, 220, 255);

            frame_counter++;

            if (pad_new & 0x80) {
                current_state = STATE_MENU;
            }
        }
    }

    return 0;
}
