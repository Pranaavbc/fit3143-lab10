// space_invaders.c
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <time.h>
#include <mpi.h>

//#define DEBUG 1

// ---------- Constants & Macros ----------
#define MAX_SHOTS 1024
#define IDX(r,c) ((r) * ncols + (c))

// ---------- Small sleep helper for pacing (Step 20b) ----------
static void sleep_ms(int ms){
    struct timespec ts = { ms/1000, (ms%1000)*1000000 };
    nanosleep(&ts, NULL);
}

// ---------- Data Structures ----------
typedef struct {
    int  col;
    int  from_row;            // player uses -1
    int  delta;               // ticks since fired
    unsigned char from_player; // 1 = player bullet, 0 = invader
    unsigned char active;      // 1 = in flight
} Shot;

typedef struct {
    int tick;
    int player_col;
    int game_over;
} TickMsg;

typedef struct {
    int fired;  // 0/1
    int row;
    int col;
} InvaderEvent;

// ---------- Step 7: bullet helpers ----------
int add_player_shot(Shot *pool, int max, int col) {
    for (int i = 0; i < max; ++i) {
        if (pool[i].active == 0) {
            pool[i].active = 1;
            pool[i].from_player = 1;
            pool[i].col = col;
            pool[i].from_row = -1;
            pool[i].delta = 0;
            return i;
        }
    }
    return -1;
}

int add_invader_shot(Shot *pool, int max, int col, int shooter_row) {
    for (int i = 0; i < max; ++i) {
        if (pool[i].active == 0) {
            pool[i].active = 1;
            pool[i].from_player = 0;
            pool[i].col = col;
            pool[i].from_row = shooter_row;
            pool[i].delta = 0;
            return i;
        }
    }
    return -1;
}

// ---------- Step 8: ASCII board ----------
void print_board(int tick, int nrows, int ncols, const int *alive,
                 int player_col, const Shot *shots, int max_shots) {
    // (Step 20c) Clear screen for a cleaner "animation"
    printf("\033[2J\033[H");
    printf("Space Invaders (n=%d, m=%d)  tick=%d  player_col=%d\n",
           nrows, ncols, tick, player_col);

    for (int r = nrows - 1; r >= 0; --r) {
        for (int c = 0; c < ncols; ++c) {
            char cell = alive[IDX(r,c)] ? 'V' : '.';
            // overlay shots
            for (int i = 0; i < max_shots; ++i) {
                if (!shots[i].active) continue;
                int cr = INT_MIN;
                if (shots[i].from_player) {
                    if (shots[i].delta >= 2) cr = shots[i].delta - 2;               // 0,1,2,...
                } else {
                    if (shots[i].delta >= 2) cr = shots[i].from_row - (shots[i].delta - 1); // r-1,r-2,...
                }
                if (cr == r && shots[i].col == c) {
                    cell = shots[i].from_player ? '|' : '!';
                }
            }
            printf("[%c] ", cell);
        }
        printf("\n");
    }
    // player row
    for (int c = 0; c < ncols; ++c) {
        printf(c == player_col ? "[^] " : "[ ] ");
    }
    printf("\n");

    // (Step 20e) small shots summary
    int pc=0, ic=0;
    for (int i=0;i<max_shots;++i)
        if (shots[i].active) (shots[i].from_player ? pc : ic)++;
    printf("Shots: player=%d  invaders=%d\n\n", pc, ic);
}

// ---------- Step 10: invader fire decision ----------
int decide_fire(int tick, int eligible, int rank_seed) {
    if (!eligible) return 0;
    if (tick % 4 != 0) return 0;
    unsigned int s = (unsigned int)(tick * 1103515245u + rank_seed * 12345u);
    double u = (double)(s % 1000) / 1000.0; // [0,1)
    return u < 0.1 ? 1 : 0; // 10%
}

// ---------- Step 13: bullet advancement ----------
void advance_shots(Shot *pool, int max) {
    for (int i = 0; i < max; ++i)
        if (pool[i].active) pool[i].delta += 1;
}

int shot_row_now(const Shot *s) {
    if (!s->active) return INT_MIN;
    if (s->from_player) {
        if (s->delta < 2) return INT_MIN;
        return s->delta - 2;  // 0,1,2,...
    } else {
        if (s->delta < 2) return INT_MIN;
        return s->from_row - (s->delta - 1); // r-1,r-2,...
    }
}

// ---------- Step 14: collisions ----------
int bottom_alive_row(const int *alive, int nrows, int ncols, int col) {
    for (int r = 0; r < nrows; ++r)
        if (alive[IDX(r,col)] == 1) return r;
    return -1;
}

void resolve_collisions(Shot *pool, int max, int *alive,
                        int nrows, int ncols, int player_col, int *player_alive) {
    for (int i = 0; i < max; ++i) {
        if (!pool[i].active) continue;
        if (pool[i].from_player) {
            int br = bottom_alive_row(alive, nrows, ncols, pool[i].col);
            int cr = shot_row_now(&pool[i]);
            if (br >= 0 && cr == br) {
                alive[IDX(br, pool[i].col)] = 0; // kill invader
                pool[i].active = 0;
            }
        } else {
            // compute exact row (need -1 check)
            int cr = pool[i].from_row - (pool[i].delta - 1);
            if (pool[i].col == player_col && cr == -1) {
                *player_alive = 0;
                pool[i].active = 0;
            }
            if (cr < -1) pool[i].active = 0; // below player row
        }
    }
}

// ---------- Step 15: cleanup & victory check ----------
void cull_shots(Shot *pool, int max, int nrows) {
    for (int i = 0; i < max; ++i) {
        if (!pool[i].active) continue;
        int cr = shot_row_now(&pool[i]);
        if (pool[i].from_player) {
            if (cr > nrows - 1) pool[i].active = 0; // past top
        } else {
            if (cr < -1) pool[i].active = 0;        // past player
        }
    }
}

int all_invaders_dead(const int *alive, int nrows, int ncols) {
    for (int r = 0; r < nrows; ++r)
        for (int c = 0; c < ncols; ++c)
            if (alive[IDX(r,c)] == 1) return 0;
    return 1;
}

// ---------- Step 17: player movement ----------
int decide_player_move(int tick) {
    // example: right 3 ticks, left 3 ticks, repeat
    int phase = (tick / 3) % 2;
    return phase ? -1 : +1;  // -1 left, +1 right
}

void apply_player_move(int *player_col, int ncols, int move) {
    *player_col += move;
    if (*player_col < 0) *player_col = 0;
    if (*player_col >= ncols) *player_col = ncols - 1;
}

// ---------- Step 18: bottom-most eligibility ----------
void compute_bottom_flags(const int *alive, int nrows, int ncols, int *flags /* nrows*ncols */) {
    for (int i = 0; i < nrows*ncols; ++i) flags[i] = 0;
    for (int c = 0; c < ncols; ++c) {
        for (int r = 0; r < nrows; ++r) {
            if (alive[IDX(r,c)] == 1) { flags[IDX(r,c)] = 1; break; }
        }
    }
}

// ---------- main ----------
int main(int argc, char *argv[]) {
    int rank, size;
    int nrows, ncols;

    MPI_Init(&argc, &argv);
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    // Args: nrows ncols [max_ticks]
    if (argc < 3) {
        if (rank == 0)
            printf("Usage: mpirun -np (1+n*m) ./space_invaders nrows ncols [max_ticks]\n");
        MPI_Finalize();
        return 0;
    }
    nrows = atoi(argv[1]);
    ncols = atoi(argv[2]);
    int max_ticks = 50;                 // Step 20a: default
    if (argc >= 4) max_ticks = atoi(argv[3]);

    int required = 1 + nrows * ncols;
    if (size != required) {
        if (rank == 0)
            printf("ERROR: need %d processes (1 player + %d invaders), but got %d\n",
                   required, nrows*ncols, size);
        MPI_Finalize();
        return 0;
    }

#ifdef DEBUG
    if (rank == 0)
        printf("OK: Using %d processes (1 player + %d invaders)\n",
               size, nrows*ncols);
#endif

    // Step 3: rank -> (row,col) mapping (debug only)
#ifdef DEBUG
    if (rank > 0) {
        int invader_rank = rank - 1;
        int row = invader_rank / ncols;
        int col = invader_rank % ncols;
        printf("Rank %d is invader at (row=%d, col=%d)\n", rank, row, col);
    }
#endif

    // Step 5: master alive grid
    int *alive = NULL;
    if (rank == 0) {
        alive = (int*) malloc(nrows * ncols * sizeof(int));
        if (!alive) { fprintf(stderr, "alloc alive failed\n"); MPI_Abort(MPI_COMM_WORLD, 1); }
        for (int r = 0; r < nrows; ++r)
            for (int c = 0; c < ncols; ++c)
                alive[IDX(r,c)] = 1;
    }

    // Step 18 buffers: eligibility flags (all ranks hold a copy)
    int *elig_flags = (int*) malloc(nrows * ncols * sizeof(int));
    if (!elig_flags) { fprintf(stderr, "alloc flags failed (rank %d)\n", rank); MPI_Abort(MPI_COMM_WORLD, 1); }

    // Step 6: player state (master)
    int player_col = 0;
    int player_alive = 1;

    // Step 7: bullet pool (master)
    Shot shots[MAX_SHOTS];
    if (rank == 0) {
        for (int i = 0; i < MAX_SHOTS; ++i) shots[i].active = 0;
    }

    // ----------- Step 16: main tick loop -----------
    TickMsg tmsg;
    int game_over = 0;
    int tick = 0;

    while (!game_over) {
        // Player movement (before broadcasting new state)
        if (rank == 0) {
            int move = decide_player_move(tick);
            apply_player_move(&player_col, ncols, move);
            // compute eligibility this tick
            compute_bottom_flags(alive, nrows, ncols, elig_flags);
        }

        // broadcast eligibility & tick packet
        MPI_Bcast(elig_flags, nrows*ncols, MPI_INT, 0, MPI_COMM_WORLD);

        if (rank == 0) {
            tmsg.tick = tick;
            tmsg.player_col = player_col;
            tmsg.game_over = 0;
        }
        MPI_Bcast(&tmsg, 3, MPI_INT, 0, MPI_COMM_WORLD);

        // Invaders decide & report
        int fired = 0;
        InvaderEvent my_ev = (InvaderEvent){0,-1,-1};
        if (rank > 0) {
            int invader_rank = rank - 1;
            int row = invader_rank / ncols;
            int col = invader_rank % ncols;
            int eligible = elig_flags[IDX(row, col)];
            fired = decide_fire(tmsg.tick, eligible, rank);
            my_ev.fired = fired; my_ev.row = row; my_ev.col = col;
        }

        InvaderEvent *all = NULL;
        if (rank == 0) {
            all = (InvaderEvent*) malloc(sizeof(InvaderEvent) * size);
            if (!all) { fprintf(stderr,"alloc gather buf failed\n"); MPI_Abort(MPI_COMM_WORLD,1); }
        }
        MPI_Gather(&my_ev, 3, MPI_INT, all, 3, MPI_INT, 0, MPI_COMM_WORLD);

        // Master processes world
        if (rank == 0) {
            // Spawn shots (player + eligible invaders who fired)
            (void) add_player_shot(shots, MAX_SHOTS, player_col);
            for (int r = 1; r < size; ++r)
                if (all[r].fired)
                    (void) add_invader_shot(shots, MAX_SHOTS, all[r].col, all[r].row);
            free(all);

            // Move, collide, cull
            advance_shots(shots, MAX_SHOTS);
            resolve_collisions(shots, MAX_SHOTS, alive, nrows, ncols, player_col, &player_alive);
            cull_shots(shots, MAX_SHOTS, nrows);

            // Win/lose
            int win = all_invaders_dead(alive, nrows, ncols);
            print_board(tmsg.tick, nrows, ncols, alive, player_col, shots, MAX_SHOTS);

            if (player_alive == 0) {
                printf(">>> Player was hit. Game over (tick=%d)\n", tick);
                game_over = 1;
            } else if (win) {
                printf(">>> All invaders eliminated. You win! (tick=%d)\n", tick);
                game_over = 1;
            }
        }

        // Share end flag so everyone exits loop together
        MPI_Bcast(&game_over, 1, MPI_INT, 0, MPI_COMM_WORLD);

        // Step 20a/20b: tick cap + pacing
        if (rank == 0) sleep_ms(200);          // ~5 FPS; tweak as you like
        if (++tick >= max_ticks && rank == 0) game_over = 1;
        MPI_Bcast(&game_over, 1, MPI_INT, 0, MPI_COMM_WORLD);
    }

    // ---------- Step 19: summary & cleanup ----------
    if (rank == 0) {
        printf("\n=== Simulation complete ===\n");
        printf("Total ticks: %d\n", tick);
        if (player_alive)
            printf("Result: Player survived (victory or tick cap reached)\n");
        else
            printf("Result: Player was defeated.\n");
    }

    if (rank == 0 && alive) free(alive);
    if (elig_flags) free(elig_flags);

    MPI_Finalize();
    return 0;
}