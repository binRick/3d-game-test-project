#ifndef IRONFIST_POSTFX_H
#define IRONFIST_POSTFX_H

void PostFxInit(int w, int h);
void PostFxShutdown(void);
// Begin offscreen capture: replaces BeginDrawing for the frame.
void PostFxBeginCapture(void);
// End offscreen capture, run the postprocess pass, present to backbuffer.
// Replaces EndDrawing for the frame.
void PostFxEndCapture(void);

#endif
