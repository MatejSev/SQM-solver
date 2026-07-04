#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <algorithm>
#include <cmath>
#include <iomanip>
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

class QuatrominoSolver {
private:
    int R, C;
    vector<vector<int>> values;
    vector<vector<int>> current_board;
    vector<vector<int>> best_board;
    
    vector<char> piece_types;
    vector<char> best_piece_types;

    int best_cost;
    int trivial_lower_bound;
    bool optimal_found;
    int current_piece_id;

    // Spočítá triviální dolní mez jako součet k nejmenších hodnot na desce,
    // kde k = (R * C) mod 4.
    void calculate_trivial_lower_bound() {
        vector<int> all_values;
        for (int r = 0; r < R; ++r) {
            for (int c = 0; c < C; ++c) {
                all_values.push_back(values[r][c]);
            }
        }
        sort(all_values.begin(), all_values.end());
        
        int k = (R * C) % 4;
        trivial_lower_bound = 0;
        for (int i = 0; i < k; ++i) {
            trivial_lower_bound += all_values[i];
        }
    }

    // Kontrola kolizí s okraji desky a jinými dílky
    bool can_place(int r, int c, const Shape& shape) {
        for (const auto& offset : shape.offsets) {
            int nr = r + offset.first;
            int nc = c + offset.second;
            if (nr < 0 || nr >= R || nc < 0 || nc >= C) return false;
            if (current_board[nr][nc] != 0) return false;
        }
        return true;
    }

    // Zápis dílku na desku
    void place(int r, int c, const Shape& shape, int id, char type) {
        for (const auto& offset : shape.offsets) {
            current_board[r + offset.first][c + offset.second] = id;
        }
        if (id >= piece_types.size()) piece_types.resize(id + 1);
        piece_types[id] = type;
    }

    // Smazání dílku (používáno pro backtracking)
    void remove(int r, int c, const Shape& shape) {
        for (const auto& offset : shape.offsets) {
            current_board[r + offset.first][c + offset.second] = 0;
        }
    }

    // Hlavní sekvenční BB-DFS rekurze
    void solve_dfs(int cell_idx, int current_cost, int t_count, int z_count) {
        // Ořezávání
        if (optimal_found) return; 
        if (current_cost >= best_cost) return;

        int remaining_cells = R * C - cell_idx;
        int max_possible_pieces = remaining_cells / 4;
        if (abs(t_count - z_count) > max_possible_pieces + 1) return;

        // Koncový stav (zpracována celá deska)
        if (cell_idx == R * C) {
            if (abs(t_count - z_count) <= 1) { 
                if (current_cost < best_cost) {
                    best_cost = current_cost;
                    best_board = current_board;
                    best_piece_types = piece_types;
                    
                    if (best_cost == trivial_lower_bound) {
                        optimal_found = true;
                    }
                }
            }
            return;
        }

        int r = cell_idx / C;
        int c = cell_idx % C;

        // Pokud je aktuální políčko již obsazeno, posuneme se dál
        if (current_board[r][c] != 0) {
            solve_dfs(cell_idx + 1, current_cost, t_count, z_count);
            return;
        }

        // Zkoušíme postupně umístit všechny povolené tvary
        for (const auto& shape : SHAPES) {
            if (can_place(r, c, shape)) {
                place(r, c, shape, current_piece_id, shape.type);
                
                int next_t = t_count + (shape.type == 'T' ? 1 : 0);
                int next_z = z_count + (shape.type == 'Z' ? 1 : 0);
                
                current_piece_id++;
                solve_dfs(cell_idx + 1, current_cost, next_t, next_z);
                current_piece_id--;
                
                remove(r, c, shape);
            }
        }

        // Aktuální políčko záměrně vynecháme
        solve_dfs(cell_idx + 1, current_cost + values[r][c], t_count, z_count);
    }

public:
    // Načítání matice ze souboru
    bool load_from_file(const string& filename) {
        ifstream file(filename);
        if (!file.is_open()) return false;

        if (!(file >> R >> C)) return false;
        
        values.assign(R, vector<int>(C));
        for (int r = 0; r < R; ++r) {
            for (int c = 0; c < C; ++c) {
                file >> values[r][c];
            }
        }
        return true;
    }

    // Inicializace a spuštění hledání
    void solve() {
        current_board.assign(R, vector<int>(C, 0));
        best_board.assign(R, vector<int>(C, 0));
        best_cost = 1000000000; 
        optimal_found = false;
        current_piece_id = 1;

        calculate_trivial_lower_bound();
        solve_dfs(0, 0, 0, 0);
    }

    // Formátovaný výstup výsledků
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
                if (id == 0) {
                    cout << setw(4) << values[r][c] << " ";
                } else {
                    string piece = string(1, best_piece_types[id]) + to_string(id);
                    cout << setw(4) << piece << " ";
                }
            }
            cout << "\n";
        }
    }
};

int main(int argc, char* argv[]) {
    if (argc < 2) {
        cout << "Pouziti: " << argv[0] << " <cesta_k_souboru_mapy>" << endl;
        return 1;
    }

    QuatrominoSolver solver;
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