#include <libc.h>

#define WIDTH 80
#define HEIGHT 22
#define SIZE (WIDTH * HEIGHT)

int main() {
    float A = 0, B = 0;
    float i, j;

    static float z[SIZE];
    static char b[SIZE];

    /* +2 per line for "\r\n" */
    static char output[SIZE + HEIGHT * 2];

    /* Clear screen once */
    write(1, "\x1b[2J", 4);

    for (;;) {
        memset(b, 32, SIZE);
        memset(z, 0, sizeof(z));

        for (j = 0; j < 6.28; j += 0.07) {
            for (i = 0; i < 6.28; i += 0.02) {
                float c = sin(i),
                      d = cos(j),
                      e = sin(A),
                      f = sin(j),
                      g = cos(A),
                      h = d + 2,
                      D = 1 / (c * h * e + f * g + 5),
                      l = cos(i),
                      m = cos(B),
                      n = sin(B),
                      t = c * h * g - f * e;

                int x = 40 + 30 * D * (l * h * m - t * n);
                int y = 12 + 15 * D * (l * h * n + t * m);
                int o = x + WIDTH * y;
                int N = (int)(8 * (
                    (f * e - c * d * g) * m
                    - c * d * e
                    - f * g
                    - l * d * n
                ));

                if (y > 0 && y < HEIGHT &&
                    x > 0 && x < WIDTH &&
                    D > z[o]) {

                    z[o] = D;
                    b[o] = ".,-~:;=!*#$@"[N > 0 ? N : 0];
                }
            }
        }

        /* Move cursor to home */
        write(1, "\x1b[H", 3);

        /* Build full frame into output buffer */
        int p = 0;
        for (int y = 0; y < HEIGHT; y++) {
            for (int x = 0; x < WIDTH; x++) {
                output[p++] = b[x + WIDTH * y];
            }
            output[p++] = '\r';
            output[p++] = '\n';
        }

        /* Single write per frame */
        write(1, output, p);

        A += 0.04;
        B += 0.02;

        /* Optional frame limiting */
        /* usleep(30000); */
    }

    return 0;
}