#ifndef ASCII_RENDERER_SNAKE_H
#define ASCII_RENDERER_SNAKE_H

/* Pure Snake game logic: no terminal, no rendering, no I/O. A caller drives it
   by buffering turns from input (snake_turn) and advancing on a fixed timestep
   (snake_step), then reads the public state to draw a frame. This keeps the
   simulation testable in isolation from the renderer. */

typedef enum { DIR_UP, DIR_DOWN, DIR_LEFT, DIR_RIGHT } Direction;

typedef struct {
    int x;
    int y;
} Point;

typedef struct {
    int width;         /* playfield size in grid cells */
    int height;
    Point *body;       /* body[0] is the head; the first `length` cells are live */
    int length;
    int capacity;      /* == width * height; the snake can never exceed the field */
    Direction dir;     /* the committed heading, advanced each step */
    Direction pending; /* the buffered heading from input, applied next step */
    Point food;
    int score;
    int alive;
    unsigned rng;      /* LCG state for food placement */
} Snake;

/* Create a length-3 snake centered in a width x height field, heading right,
   with food placed. `seed` seeds food placement. Returns NULL on allocation
   failure or if the field is too small (< 4 wide, < 1 tall). */
Snake *snake_create(int width, int height, unsigned seed);

/* Free a snake created by snake_create. Safe to call with NULL. */
void snake_destroy(Snake *s);

/* Buffer a new heading for the next step. A 180-degree reversal into the neck
   is ignored (you cannot turn back on yourself). No-op once dead. */
void snake_turn(Snake *s, Direction dir);

/* Advance one tick: move (and possibly grow) the snake, or mark it dead on a
   wall or self collision. No-op once dead. */
void snake_step(Snake *s);

#endif /* ASCII_RENDERER_SNAKE_H */
