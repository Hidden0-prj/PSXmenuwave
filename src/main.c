#include <stdint.h>
#include <stdbool.h>
#include "gpu.h"
#include "gte.h"

// --- APP STATES ---
enum AppState {
    STATE_MENU,
    STATE_3D_MODEL,
    STATE_XMB_WAVES
};

enum AppState current_state = STATE_MENU;
int menu_selection = 0;       // 0 = 3D Model, 1 = XMB Waves
int frame_counter = 0;        // Used for wave animation

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

// --- WAVE RENDERING ROUTINE ---
#define WAVE_SEGMENTS 16
#define SCREEN_WIDTH 320

void DrawXMBWave(int frame, int base_y, int phase_gap, int speed, int r, int g, int b) {
    POLY_G4 *poly;
    int i;
    int seg_width = SCREEN_WIDTH / WAVE_SEGMENTS;
    
    for (i = 0; i < WAVE_SEGMENTS; i++) {
        // Allocate primitive from standard ordering table
        poly = (POLY_G4 *)nextpri; 
        setPolyG4(poly);
        
        // Wrap index using bitwise & 255
        int idx0 = ((i * phase_gap) + (frame * speed)) & 255;
        int idx1 = (((i + 1) * phase_gap) + (frame * speed)) & 255;
        
        int y_offset0 = SINE_LUT[idx0];
        int y_offset1 = SINE_LUT[idx1];

        // Enable Semi-Transparency (Additive Blending)
        poly->tpage = getTPage(0, 1, 0, 0); 
        setSemiTrans(poly, 1);

        // Map vertices
        setXY4(poly, 
            i * seg_width,       base_y + y_offset0 - 10,  // Top Left
            (i + 1) * seg_width, base_y + y_offset1 - 10,  // Top Right
            i * seg_width,       base_y + y_offset0 + 50,  // Bottom Left
            (i + 1) * seg_width, base_y + y_offset1 + 50   // Bottom Right
        );

        // Gouraud Shading: Bright at the top, pure black at the bottom for smooth fade
        setRGB0(poly, r, g, b); // Top Left
        setRGB1(poly, r, g, b); // Top Right
        setRGB2(poly, 0, 0, 0); // Bottom Left
        setRGB3(poly, 0, 0, 0); // Bottom Right

        addPrim(ot[db], poly); 
        nextpri += sizeof(POLY_G4);
    }
}

// --- MAIN LOOP ---
int main() {
    // 1. Initialize System & Graphics
    ResetGraph(0);
    InitGeom();
    // [Insert any specific texture loading / FntLoad from your old main.c here]
    
    // Variables for Controller Input
    uint16_t pad_old = 0;
    uint16_t pad_new = 0;

    while(1) {
        // Clear screen based on state
        if (current_state == STATE_XMB_WAVES) {
            setRGB0(&draw[db].isenv, 10, 10, 25); // Dark PS3-style background
        } else {
            setRGB0(&draw[db].isenv, 50, 50, 50); // Standard gray for Menu/Model
        }

        // --- READ CONTROLLER ---
        // (Modify PAD_UP/PAD_DOWN/PAD_CROSS macros if your bare-metal setup uses different names)
        uint16_t pad = PadRead(0);
        pad_new = pad & ~pad_old; // Detect button presses (trigger once)
        pad_old = pad;

        // --- STATE MACHINE ---
        if (current_state == STATE_MENU) {
            
            // Render Menu Text
            FntPrint("--- MAIN MENU ---\n\n");
            
            if (menu_selection == 0) {
                FntPrint("> 1. View 3D PlayStation Model\n");
                FntPrint("  2. View XMB Waves\n");
            } else {
                FntPrint("  1. View 3D PlayStation Model\n");
                FntPrint("> 2. View XMB Waves\n");
            }
            
            FntPrint("\n\nPress CROSS to select.");

            // Menu Navigation Logic
            if (pad_new & PAD_UP) {
                menu_selection = 0;
            }
            if (pad_new & PAD_DOWN) {
                menu_selection = 1;
            }
            if (pad_new & PAD_CROSS) {
                if (menu_selection == 0) {
                    current_state = STATE_3D_MODEL;
                } else {
                    current_state = STATE_XMB_WAVES;
                }
            }

        } else if (current_state == STATE_3D_MODEL) {
            
            // Render Help Text
            FntPrint("3D MODEL VIEWER\nPress TRIANGLE to return to menu.\n");

            // ==========================================
            // PASTE YOUR PREVIOUS 3D MODEL ROTATION CODE HERE
            // e.g., SetRotMatrix(), SetTransMatrix(), DrawModel()
            // ==========================================


            // Return to menu logic
            if (pad_new & PAD_TRIANGLE) {
                current_state = STATE_MENU;
            }

        } else if (current_state == STATE_XMB_WAVES) {
            
            // Render Help Text
            FntPrint("XMB WAVES\nPress TRIANGLE to return to menu.\n");

            // Draw Parallax Gouraud Waves
            // Background slow wave (Dim Cyan)
            DrawXMBWave(frame_counter, 120, 12, 2,  30, 100, 120);
            // Middle medium wave (Bright Cyan)
            DrawXMBWave(frame_counter, 135, 16, 3,  50, 180, 200);
            // Front fast wave (White/Cyan highlight)
            DrawXMBWave(frame_counter, 150, 20, 4, 150, 220, 255);

            frame_counter++; // Advance animation

            // Return to menu logic
            if (pad_new & PAD_TRIANGLE) {
                current_state = STATE_MENU;
            }
        }

        // Flush text and swap buffers
        FntFlush(-1);
        display();
    }

    return 0;
}
