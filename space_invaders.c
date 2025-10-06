#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <mpi.h>

// ---------- Constants & Macros ----------
#define IDX(r,c) ((r) * ncols + (c))
#define MAX_SHOTS 1024

// ---------- Data Structures ----------

// bullet
typedef struct {
    int  col;
    int  from_row;
    int  delta;
    unsigned char from_player;
    unsigned char active;
} Shot;

// tick message (broadcast each tick)
typedef struct {
    int tick;
    int player_col;
    int game_over;
} TickMsg;

// invader -> master report
typedef struct {
    int fired;
    int row;
    int col;
} InvaderEvent;

// ---------- Step 7 helpers ----------
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

// ---------- Step 8: board printer ----------
void print_board(int tick, int nrows, int ncols, const int *alive,
                 int player_col, const Shot *shots, int max_shots) {
    printf("t=%d\n", tick);
    for (int r = nrows - 1; r >= 0; --r) {
        for (int c = 0; c < ncols; ++c) {
            char cell = alive[IDX(r,c)] ? 'V' : '.';
            for (int i = 0; i < max_shots; ++i) {
                if (!shots[i].active) continue;
                int cr = -999;
                if (shots[i].from_player) {
                    if (shots[i].delta >= 2) cr = shots[i].delta - 2;
                } else {
                    if (shots[i].delta >= 2) cr = shots[i].from_row - (shots[i].delta - 1);
                }
                if (cr == r && shots[i].col == c) {
                    cell = shots[i].from_player ? '|' : '!';
                }
            }
            printf("[%c] ", cell);
        }
        printf("\n");
    }
    for (int c = 0; c < ncols; ++c) {
        printf(c == player_col ? "[^] " : "[ ] ");
    }
    printf("\n\n");
}

// ---------- Step 10: fire decision ----------
int decide_fire(int tick, int eligible, int rank_seed) {
    if (!eligible) return 0;
    if (tick % 4 != 0) return 0;
    unsigned int s = (unsigned int)(tick * 1103515245u + rank_seed * 12345u);
    double u = (double)(s % 1000) / 1000.0;
    return u < 0.1 ? 1 : 0;
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
        return s->delta - 2;
    } else {
        if (s->delta < 2) return INT_MIN;
        return s->from_row - (s->delta - 1);
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
                alive[IDX(br, pool[i].col)] = 0;
                pool[i].active = 0;
            }
        } else {
            int cr = pool[i].from_row - (pool[i].delta - 1);
            if (pool[i].col == player_col && cr == -1) {
                *player_alive = 0;
                pool[i].active = 0;
            }
            if (cr < -1) pool[i].active = 0;
        }
    }
}

// ---------- Step 15: cleanup ----------
void cull_shots(Shot *pool, int max, int nrows) {
    for (int i = 0; i < max; ++i) {
        if (!pool[i].active) continue;
        int cr = shot_row_now(&pool[i]);
        if (pool[i].from_player) {
            if (cr > nrows - 1) pool[i].active = 0;
        } else {
            if (cr < -1) pool[i].active = 0;
        }
    }
}

int all_invaders_dead(const int *alive, int nrows, int ncols) {
    for (int r = 0; r < nrows; ++r)
        for (int c = 0; c < ncols; ++c)
            if (alive[IDX(r,c)] == 1) return 0;
    return 1;
}

// ---------- main ----------
int main(int argc, char *argv[]) {
    int rank, size;
    int nrows, ncols;

    MPI_Init(&argc, &argv);
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    // Steps 1–2
    if (argc == 3) {
        nrows = atoi(argv[1]);
        ncols = atoi(argv[2]);
    } else {
        if (rank == 0)
            printf("Usage: mpirun -np X ./program nrows ncols\n");
        MPI_Finalize();
        return 0;
    }

    int required = 1 + nrows * ncols;
    if (size != required) {
        if (rank == 0)
            printf("ERROR: need %d processes (1 player + %d invaders), but got %d\n",
                   required, nrows*ncols, size);
        MPI_Finalize();
        return 0;
    }

    if (rank == 0)
        printf("OK: Using %d processes (1 player + %d invaders)\n",
               size, nrows*ncols);

    // Step 3: mapping (debug only)
    if (rank > 0) {
        int invader_rank = rank - 1;
        int row = invader_rank / ncols;
        int col = invader_rank % ncols;
        printf("Rank %d is invader at (row=%d, col=%d)\n", rank, row, col);
    }

    // Step 5: alive grid
    int *alive = NULL;
    if (rank == 0) {
        alive = (int*) malloc(nrows * ncols * sizeof(int));
        for (int r = 0; r < nrows; ++r)
            for (int c = 0; c < ncols; ++c)
                alive[IDX(r,c)] = 1;
    }

    // Step 6: player state
    int player_col = 0;
    int player_alive = 1;

    // Step 7: bullet pool
    Shot shots[MAX_SHOTS];
    if (rank == 0)
        for (int i = 0; i < MAX_SHOTS; ++i) shots[i].active = 0;

    // ----------- Step 16: main tick loop -----------
    TickMsg tmsg;
    int game_over = 0;
    int tick = 0;

    while (!game_over) {
        // broadcast tick packet
        if (rank == 0) {
            tmsg.tick = tick;
            tmsg.player_col = player_col;
            tmsg.game_over = 0;
        }
        MPI_Bcast(&tmsg, 3, MPI_INT, 0, MPI_COMM_WORLD);

        // invaders decide + gather
        int fired = 0;
        if (rank > 0) {
            int eligible = 1; // simplified
            fired = decide_fire(tmsg.tick, eligible, rank);
        }

        InvaderEvent my_ev = {0, -1, -1};
        if (rank > 0) {
            int invader_rank = rank - 1;
            my_ev.row = invader_rank / ncols;
            my_ev.col = invader_rank % ncols;
            my_ev.fired = fired;
        }

        InvaderEvent *all = NULL;
        if (rank == 0) all = (InvaderEvent*) malloc(sizeof(InvaderEvent) * size);
        MPI_Gather(&my_ev, 3, MPI_INT, all, 3, MPI_INT, 0, MPI_COMM_WORLD);

        // master updates world
        if (rank == 0) {
            // spawn bullets
            add_player_shot(shots, MAX_SHOTS, player_col);
            for (int r = 1; r < size; ++r)
                if (all[r].fired)
                    add_invader_shot(shots, MAX_SHOTS, all[r].col, all[r].row);
            free(all);

            // move & resolve
            advance_shots(shots, MAX_SHOTS);
            resolve_collisions(shots, MAX_SHOTS, alive, nrows, ncols, player_col, &player_alive);
            cull_shots(shots, MAX_SHOTS, nrows);

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

        MPI_Bcast(&game_over, 1, MPI_INT, 0, MPI_COMM_WORLD);
        tick++;
        if (tick > 20) game_over = 1; // safety cap for testing
    }

    if (rank == 0 && alive) free(alive);
    MPI_Finalize();
    return 0;
}