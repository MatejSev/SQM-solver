#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <queue>
#include <mpi.h>
#include <omp.h>

using namespace std;

// Zprávy pro komunikaci Master - Slave
#define TAG_WORK 1
#define TAG_FINISHED 2
#define TAG_TERMINATE 3
#define TAG_RESULT 4

const int MAX_CELLS = 256;
const int MAX_PIECES = 100;

// Tvar quatromina reprezentovaný pomocí relativních offsetů vůči [0,0]
struct Shape {
    char type;
    vector<pair<int, int>> offsets;
};

// Všech 8 unikátních rotací/zrcadlení pro T a Z.
// Definováno v reading order (shora dolů, zleva doprava)
const vector<Shape> SHAPES = {
    {'T', {{0,0}, {0,1}, {0,2}, {1,1}}},      
    {'T', {{0,0}, {1,-1}, {1,0}, {1,1}}},     
    {'T', {{0,0}, {1,0}, {1,1}, {2,0}}},      
    {'T', {{0,0}, {1,-1}, {1,0}, {2,0}}},     
    {'Z', {{0,0}, {0,1}, {1,1}, {1,2}}},      
    {'Z', {{0,0}, {1,-1}, {1,0}, {2,-1}}},    
    {'Z', {{0,0}, {0,1}, {1,-1}, {1,0}}},     
    {'Z', {{0,0}, {1,0}, {1,1}, {2,1}}}       
};

// Datová struktura reprezentující jeden rozpracovaný mezistav
struct SearchState {
    int cell_idx;
    int current_cost;
    int t_count;
    int z_count;
    vector<vector<int>> board;
    vector<char> p_types;
    int p_id;
};

// Stav pro přenos po síti přes MPI
struct SerializedState {
    int cell_idx;
    int current_cost;
    int t_count;
    int z_count;
    int p_id;
    int global_best_cost;
    int board[MAX_CELLS];
    char p_types[MAX_PIECES];
};

// Výsledek na konci od všech Slaves k Masterovi
struct FinalResult {
    int best_cost;
    int board[MAX_CELLS];
    char p_types[MAX_PIECES];
};

class QuatrominoSolverMPI {
private:
    int R, C;
    vector<vector<int>> values;
    
    vector<vector<int>> best_board;
    vector<char> best_piece_types;

    int best_cost;
    int trivial_lower_bound;
    bool optimal_found;
    
    vector<SearchState> initial_states;
    int my_rank;
    int num_procs;

    // Spočítá triviální dolní mez jako součet k nejmenších hodnot na desce,
    // kde k = (R * C) mod 4.
    void calculate_trivial_lower_bound() {
        vector<int> all_values;
        for (int r = 0; r < R; ++r) {
            for (int c = 0; c < C; ++c) all_values.push_back(values[r][c]);
        }
        sort(all_values.begin(), all_values.end());
        int k = (R * C) % 4;
        trivial_lower_bound = 0;
        for (int i = 0; i < k; ++i) trivial_lower_bound += all_values[i];
    }

    // Kontrola kolizí s okraji desky a jinými dílky
    bool can_place(const vector<vector<int>>& board, int r, int c, const Shape& shape) {
        for (const auto& offset : shape.offsets) {
            int nr = r + offset.first;
            int nc = c + offset.second;
            if (nr < 0 || nr >= R || nc < 0 || nc >= C) return false;
            if (board[nr][nc] != 0) return false;
        }
        return true;
    }

    // Zápis dílku na desku
    void place(vector<vector<int>>& board, vector<char>& p_types, int r, int c, const Shape& shape, int id, char type) {
        for (const auto& offset : shape.offsets) { 
            board[r + offset.first][c + offset.second] = id;
        }
        if (id >= p_types.size()) p_types.resize(id + 1);
        p_types[id] = type;
    }

    // Smazání dílku (používáno pro backtracking)
    void remove(vector<vector<int>>& board, int r, int c, const Shape& shape) {
        for (const auto& offset : shape.offsets) board[r + offset.first][c + offset.second] = 0;
    }

    // Bezpečně přepíše nejlepší řešení
    void update_best(int cost, const vector<vector<int>>& board, const vector<char>& p_types) {
        #pragma omp critical
        {
            if (cost < best_cost) {
                best_cost = cost;
                best_board = board;
                best_piece_types = p_types;
                if (best_cost <= trivial_lower_bound) {
                    #pragma omp atomic write
                    optimal_found = true;
                }
            }
        }
    }

    // Funkce pro serializaci dat
    SerializedState serialize(const SearchState& state, int global_best) {
        SerializedState s;
        s.cell_idx = state.cell_idx;
        s.current_cost = state.current_cost;
        s.t_count = state.t_count;
        s.z_count = state.z_count;
        s.p_id = state.p_id;
        s.global_best_cost = global_best;
        for (int i = 0; i < R * C; ++i) s.board[i] = state.board[i / C][i % C];
        for (int i = 0; i < state.p_types.size(); ++i) s.p_types[i] = state.p_types[i];
        return s;
    }

    SearchState deserialize(const SerializedState& s) {
        SearchState state;
        state.cell_idx = s.cell_idx;
        state.current_cost = s.current_cost;
        state.t_count = s.t_count;
        state.z_count = s.z_count;
        state.p_id = s.p_id;
        state.board.assign(R, vector<int>(C, 0));
        for (int i = 0; i < R * C; ++i) state.board[i / C][i % C] = s.board[i];
        state.p_types.assign(s.p_types, s.p_types + s.p_id + 1);
        return state;
    }

    // BFS pro plnění fronty
    void prepare_states_bfs(SearchState root, int target_states) {
        initial_states.clear();
        queue<SearchState> q;
        q.push(root);

        while (q.size() < target_states && !q.empty()) {
            SearchState curr = q.front();
            q.pop();

            int cell_idx = curr.cell_idx;
            
            while (cell_idx < R * C && curr.board[cell_idx / C][cell_idx % C] != 0) {
                cell_idx++;
            }

            if (cell_idx == R * C) {
                initial_states.push_back(curr);
                continue;
            }

            int r = cell_idx / C;
            int c = cell_idx % C;

            for (const auto& shape : SHAPES) {
                if (can_place(curr.board, r, c, shape)) {
                    SearchState next_state = curr;
                    place(next_state.board, next_state.p_types, r, c, shape, next_state.p_id, shape.type);
                    
                    next_state.t_count += (shape.type == 'T' ? 1 : 0);
                    next_state.z_count += (shape.type == 'Z' ? 1 : 0);
                    next_state.p_id++;
                    next_state.cell_idx = cell_idx + 1;
                    
                    int remaining_cells = R * C - next_state.cell_idx;
                    int max_pieces = remaining_cells / 4;
                    if (abs(next_state.t_count - next_state.z_count) <= max_pieces + 1) {
                        q.push(next_state);
                    }
                }
            }

            SearchState skip_state = curr;
            skip_state.current_cost += values[r][c];
            skip_state.cell_idx = cell_idx + 1;
            
            int remaining_cells = R * C - skip_state.cell_idx;
            int max_pieces = remaining_cells / 4;
            if (abs(skip_state.t_count - skip_state.z_count) <= max_pieces + 1) {
                q.push(skip_state);
            }
        }

        while (!q.empty()) {
            initial_states.push_back(q.front());
            q.pop();
        }
    }

    // Dořeší stav 1 vláknem pomocí sekvenčního DFS
    void solve_dfs_seq(int cell_idx, int current_cost, int t_count, int z_count, 
                       vector<vector<int>>& board, vector<char>& p_types, int& p_id) {
        
        bool opt;
        #pragma omp atomic read
        opt = optimal_found;
        if (opt) return;

        int b_cost;
        #pragma omp atomic read
        b_cost = best_cost;
        if (current_cost >= b_cost) return;

        int remaining_cells = R * C - cell_idx;
        if (abs(t_count - z_count) > (remaining_cells / 4) + 1) return;

        if (cell_idx == R * C) {
            if (abs(t_count - z_count) <= 1) { 
                int check_cost;
                #pragma omp atomic read
                check_cost = best_cost;
                if (current_cost < check_cost) {
                    update_best(current_cost, board, p_types);
                }
            }
            return;
        }

        int r = cell_idx / C;
        int c = cell_idx % C;

        if (board[r][c] != 0) {
            solve_dfs_seq(cell_idx + 1, current_cost, t_count, z_count, board, p_types, p_id);
            return;
        }

        for (const auto& shape : SHAPES) {
            if (can_place(board, r, c, shape)) {
                place(board, p_types, r, c, shape, p_id, shape.type);
                int next_t = t_count + (shape.type == 'T' ? 1 : 0);
                int next_z = z_count + (shape.type == 'Z' ? 1 : 0);
                
                p_id++;
                solve_dfs_seq(cell_idx + 1, current_cost, next_t, next_z, board, p_types, p_id);
                p_id--;
                
                remove(board, r, c, shape);
            }
        }

        solve_dfs_seq(cell_idx + 1, current_cost + values[r][c], t_count, z_count, board, p_types, p_id);
    }

    // Master uzel
    void run_master() {
        vector<vector<int>> empty_board(R, vector<int>(C, 0));
        vector<char> empty_p_types;
        SearchState root = {0, 0, 0, 0, empty_board, empty_p_types, 1};

        prepare_states_bfs(root, 200);

        int active_slaves = 0;
        int next_state_idx = 0;

        for (int dest = 1; dest < num_procs; ++dest) {
            if (next_state_idx < initial_states.size()) {
                SerializedState msg = serialize(initial_states[next_state_idx++], best_cost);
                MPI_Send(&msg, sizeof(SerializedState), MPI_BYTE, dest, TAG_WORK, MPI_COMM_WORLD);
                active_slaves++;
            } else {
                int dummy = 0;
                MPI_Send(&dummy, 1, MPI_INT, dest, TAG_TERMINATE, MPI_COMM_WORLD);
            }
        }

        while (active_slaves > 0) {
            FinalResult slave_res;
            MPI_Status status;
            
            MPI_Recv(&slave_res, sizeof(FinalResult), MPI_BYTE, MPI_ANY_SOURCE, TAG_FINISHED, MPI_COMM_WORLD, &status);
            int sender = status.MPI_SOURCE;

            if (slave_res.best_cost < best_cost) {
                best_cost = slave_res.best_cost;
                for (int i = 0; i < R * C; ++i) best_board[i / C][i % C] = slave_res.board[i];
                best_piece_types.assign(slave_res.p_types, slave_res.p_types + MAX_PIECES);
            }

            if (next_state_idx < initial_states.size()) {
                SerializedState msg = serialize(initial_states[next_state_idx++], best_cost);
                MPI_Send(&msg, sizeof(SerializedState), MPI_BYTE, sender, TAG_WORK, MPI_COMM_WORLD);
            } else {
                int dummy = 0;
                MPI_Send(&dummy, 1, MPI_INT, sender, TAG_TERMINATE, MPI_COMM_WORLD);
                active_slaves--;
            }
        }
    }

    // Slave uzel
    void run_slave() {
        while (true) {
            MPI_Status status;
            MPI_Probe(0, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
            
            if (status.MPI_TAG == TAG_TERMINATE) {
                int dummy;
                MPI_Recv(&dummy, 1, MPI_INT, 0, TAG_TERMINATE, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                break;
            } else if (status.MPI_TAG == TAG_WORK) {
                SerializedState s_msg;
                MPI_Recv(&s_msg, sizeof(SerializedState), MPI_BYTE, 0, TAG_WORK, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

                if (s_msg.global_best_cost < best_cost) {
                    #pragma omp critical
                    {
                        best_cost = s_msg.global_best_cost;
                        if (best_cost <= trivial_lower_bound) {
                            #pragma omp atomic write
                            optimal_found = true;
                        }
                    }
                }

                SearchState root = deserialize(s_msg);
                
                prepare_states_bfs(root, 500); 

                int num_states = initial_states.size();
                
                #pragma omp parallel for schedule(dynamic)
                for (int i = 0; i < num_states; ++i) {
                    SearchState state = initial_states[i];
                    solve_dfs_seq(state.cell_idx, state.current_cost, state.t_count, state.z_count, 
                                  state.board, state.p_types, state.p_id);
                }
                FinalResult res;
                res.best_cost = best_cost;
                for (int i = 0; i < R * C; ++i) res.board[i] = best_board[i / C][i % C];
                for (int i = 0; i < best_piece_types.size(); ++i) res.p_types[i] = best_piece_types[i];

                MPI_Send(&res, sizeof(FinalResult), MPI_BYTE, 0, TAG_FINISHED, MPI_COMM_WORLD);
            }
        }
    }

public:
    QuatrominoSolverMPI(int rank, int size) : my_rank(rank), num_procs(size) {}

    bool load_from_file(const string& filename) {
        ifstream file(filename);
        if (!file.is_open()) return false;
        if (!(file >> R >> C)) return false;
        
        values.assign(R, vector<int>(C));
        for (int r = 0; r < R; ++r) {
            for (int c = 0; c < C; ++c) file >> values[r][c];
        }
        return true;
    }

    void solve() {
        best_board.assign(R, vector<int>(C, 0));
        best_cost = 1000000000; 
        optimal_found = false;
        calculate_trivial_lower_bound();

        if (num_procs == 1) {
            vector<vector<int>> b(R, vector<int>(C, 0));
            vector<char> p;
            SearchState root = {0, 0, 0, 0, b, p, 1};
            prepare_states_bfs(root, 5000);
            #pragma omp parallel for schedule(dynamic)
            for (int i = 0; i < initial_states.size(); ++i) {
                SearchState s = initial_states[i];
                solve_dfs_seq(s.cell_idx, s.current_cost, s.t_count, s.z_count, s.board, s.p_types, s.p_id);
            }
            return;
        }

        if (my_rank == 0) {
            run_master();
        } else {
            run_slave();
        }
    }

    void print_solution() {
        if (my_rank != 0 && num_procs > 1) return;
        
        if (best_cost == 1000000000) {
            cout << "Zadne platne pokryti nebylo nalezeno." << endl;
            return;
        }
        cout << "-----------------------------------" << endl;
        cout << "Minimalni nalezena cena: " << best_cost << endl;
        cout << "Vysledna matice pokryti:" << endl;
        for (int r = 0; r < R; ++r) {
            for (int c = 0; c < C; ++c) {
                int id = best_board[r][c];
                if (id == 0) cout << setw(4) << values[r][c] << " ";
                else cout << setw(4) << (string(1, best_piece_types[id]) + to_string(id)) << " ";
            }
            cout << "\n";
        }
    }
};

int main(int argc, char* argv[]) {
    MPI_Init(&argc, &argv);
    
    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    if (argc < 2) {
        if (rank == 0) cout << "Pouziti: mpirun -np X " << argv[0] << " <mapa>" << endl;
        MPI_Finalize();
        return 1;
    }

    QuatrominoSolverMPI solver(rank, size);
    
    if (solver.load_from_file(argv[1])) {
        double start_time = MPI_Wtime();
        solver.solve();
        double end_time = MPI_Wtime();
        int rank;
        MPI_Comm_rank(MPI_COMM_WORLD, &rank);
        
        if (rank == 0) {
            cout << (end_time - start_time) << endl;
            // solver.print_solution(); 
        }    
    }
    
    MPI_Finalize();
    return 0;
}