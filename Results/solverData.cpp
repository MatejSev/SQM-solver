#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <queue>
#include <omp.h>
#include <chrono>

using namespace std;

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

class QuatrominoSolver {
private:
    int R, C;
    vector<vector<int>> values;
    
    vector<vector<int>> best_board;
    vector<char> best_piece_types;

    int best_cost;
    int trivial_lower_bound;
    bool optimal_found;
    
    vector<SearchState> initial_states;

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
                if (best_cost == trivial_lower_bound) {
                    #pragma omp atomic write
                    optimal_found = true;
                }
            }
        }
    }

    // Příprava počátečních stavů pomocí BFS
    void prepare_states_bfs(SearchState root) {
        queue<SearchState> q;
        q.push(root);

        // Dokud nemáme dostatek stavů pro všechna vlákna a fronta není prázdná
        while (q.size() < EnoughStates && !q.empty()) {
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
        int max_possible_pieces = remaining_cells / 4;
        if (abs(t_count - z_count) > max_possible_pieces + 1) return;

        if (cell_idx == R * C) {
            if (current_cost < best_cost) {
                if (abs(t_count - z_count) <= 1) { 
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

public:
    int EnoughStates = 5000; 

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

    // Inicializuje proměnné a spustí paralelní blok
    void solve() {
        vector<vector<int>> board(R, vector<int>(C, 0));
        vector<char> p_types;
        
        best_board.assign(R, vector<int>(C, 0));
        best_cost = 1000000000; 
        optimal_found = false;
        initial_states.clear();

        calculate_trivial_lower_bound();

        SearchState root = {0, 0, 0, 0, board, p_types, 1};
        prepare_states_bfs(root);
        int num_states = initial_states.size();
        
        #pragma omp parallel for schedule(dynamic)
        for (int i = 0; i < num_states; ++i) {
            SearchState state = initial_states[i];
            solve_dfs_seq(state.cell_idx, state.current_cost, state.t_count, state.z_count, 
                          state.board, state.p_types, state.p_id);
        }
    }

    // Vypíše hotovou matici a nejlepší cenu
    void print_solution() {
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
    if (argc < 2) {
        cout << "Pouziti: " << argv[0] << " <cesta_k_souboru_mapy> [dostatek_stavu]" << endl;
        return 1;
    }

    QuatrominoSolver solver;
    
    if (argc >= 3) solver.EnoughStates = atoi(argv[2]);

    if (solver.load_from_file(argv[1])) {
        auto start = std::chrono::high_resolution_clock::now();
        solver.solve();
        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> diff = end - start;
        cout << diff.count() << endl;
        // solver.print_solution();
    }
    return 0;
}