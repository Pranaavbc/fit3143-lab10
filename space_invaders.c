#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>


// Step 8: ASCII renderer (master)
void print_board(int tick, int nrows, int ncols, const int *alive,
                 int player_col, const Shot *shots, int max_shots) {
    if (tick >= 0) printf("t=%d\n", tick);

    // print from top row down to bottom row
    for (int r = nrows - 1; r >= 0; --r) {
        for (int c = 0; c < ncols; ++c) {
            char cell = alive[IDX(r,c)] ? 'V' : '.' ;  // choose symbols
            // overlay bullets that are inside the grid:
            for (int i = 0; i < max_shots; ++i) {
                if (!shots[i].active) continue;
                // compute current row of the shot if it's inside grid:
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

    // player row (under the grid)
    for (int c = 0; c < ncols; ++c) {
        printf(c == player_col ? "[^] " : "[ ] ");
    }
    printf("\n\n");
}



int main(int argc, char *argv[]) {

    //rank, size, and dimensions (column) 
    int rank, size;
    int nrows, ncols;

    // 1. Initialize MPI
    MPI_Init(&argc, &argv);

    // 2. Get total processes and my rank
    MPI_Comm_size(MPI_COMM_WORLD, &size); //fills size with total processes 
    MPI_Comm_rank(MPI_COMM_WORLD, &rank); //fills rank with process's ID 

    // 3. Parse command line args, also check if user provided two arguements. 
    if (argc == 3) {
        nrows = atoi(argv[1]);
        ncols = atoi(argv[2]);
    } else {
        if (rank == 0) {
            printf("Usage: mpirun -np X ./program nrows ncols\n");
        }
        MPI_Finalize();
        return 0;
    }

    // number of processess 
    int required = 1 + nrows * ncols;

    // total required doesn't match the number of processess. 
    // error message is printed. 
    if (size != required) {
        if (rank == 0) {
            printf("ERROR: need %d processes (1 player + %d invaders), but got %d\n",
                required, nrows*ncols, size);
        }
        MPI_Finalize();
        return 0;
    }

    if (rank == 0) {
        printf("OK: Using %d processes (1 player + %d invaders)\n",
            size, nrows*ncols);
    }


    // Debug print
    if (rank == 0) {
        printf("Grid requested: %d x %d with %d processes\n", nrows, ncols, size);
    }

    //Every process after rank 0 is a invader. -----------------------------------------
    
    if (rank > 0){

        // we are figuring out, row and column position for each invader. 
        int invader_rank = rank - 1; 
        int row = invader_rank / ncols; 
        int col = invader_rank % ncols; 


        // possibly optional (Cartisan topology)
        // simplifies neighbour lookups. 
        MPI_Comm cart_comm;
        int dims[2] = {nrows, ncols};
        int periods[2] = {0, 0};
        int reorder = 1;
        MPI_Cart_create(invaders_world, 2, dims, periods, reorder, &cart_comm);


        printf("Rank %d is invader at (row=%d, col=%d)\n", rank, row, col);
        printf("Rank %d cart coords = (%d,%d)\n", rank, coords[0], coords[1]);

    }

    // Dead or Alive Grid ----------------------------------------------------------------

    int *alive = NULL;
    // MACRO - wherever we need to write r * ncols + c, we can write IDX(r,c) instead. 
    #define IDX(r,c) ( (r) * ncols + (c) )

    
    if (rank == 0) {
        // allocate nrows * ncols ints (1 = alive, 0 = dead)
        alive = (int*) malloc( nrows * ncols * sizeof(int) );

        // init all to alive
        for (int r = 0; r < nrows; ++r) {
            for (int c = 0; c < ncols; ++c) {
                alive[ IDX(r,c) ] = 1;   // use IDX(r,c)
            }
        }

        // quick sanity print (optional)
        printf("Master: initial alive count = %d\n", nrows * ncols);
    }

    int player_col = 0; // start at leftmost 
    int player_alive = 1;  //alive flag 
    
    // track players cannon 
    if (rank == 0) {
        //clamp at lowerbound 
        if (player_col < 0) player_col = 0;
        // clamp at upperbound. 
        if (player_col >= ncols) player_col = ncols - 1;
        printf("Master: player starts at col=%d\n", player_col);

    }


    //// Track Bullets ----------------------------------------
    #define MAX_SHOTS 1024 

    typedef struct {
        int col; 
        int from_row; 
        int delta; 

        unsigned char from_Player; 
        unsigned char active; 
    } Shot; 

    Shot shots[MAX_SHOTS];
    int nshots = 0;   // optional: number of active shots

    if (rank == 0) {

        for (int i = 0; i < MAX_SHOTS; ++i){
            shots[i].active = free;   // set to "free"
            shots[i].delta  = 0;
            shots[i].col    = 0;
            shots[i].from_row = 0;
            shots[i].from_player = 0;
        }

        printf("Master: bullet pool ready (MAX_SHOTS=%d)\n", MAX_SHOTS);
    }

    // Tiny helpers (declarations for now; you can implement later)
    int add_player_shot(Shot *pool, int max, int col) {
        // find a free slot, set: active=1, from_player=1, col=col, from_row = -1, delta=0
        // return index or -1 if full
        // TODO: fill this in

        for(int i = 0; i < max; ++i){

            if(pool[i].active == 0){
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
        // find a free slot, set: active=1, from_player=0, col=col, from_row=shooter_row, delta=0
        // return index or -1 if full
        // TODO: fill this in

          for(int i = 0; i < max; ++i){

            if(pool[i].active == 0){
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


    //// 

    if (rank == 0) {
    print_board(/*tick=*/0, nrows, ncols, alive, player_col, shots, MAX_SHOTS);
    }
    

    if (rank == 0) {
        free(alive);
    }

    MPI_Finalize();
    return 0;
}