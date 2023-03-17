#ifndef FGRAPHICS_
#define FGRAPHICS_
#ifdef __cplusplus
extern "C" {
#endif
#define ROTATE_0  0
#define ROTATE_90  90
#define ROTATE_180  180
#define ROTATE_270  270
void finitgraphics(int rotation, int width, int height);
void fupdate(const char *graphicsbuffer);
void fstartframe() ;
void fendframe() ;
void fwritescanline(size_t len, const char *line);
void fset_backlight(uint8_t brightness);
#ifdef __cplusplus
}
#endif
#endif