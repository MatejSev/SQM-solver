import pandas as pd
import matplotlib.pyplot as plt

try:
    df = pd.read_csv('vysledky.csv')
except FileNotFoundError:
    print("Soubor vysledky.csv nebyl nalezen.")
    exit(1)

threads = ['1', '2', '4', '8', '16', '32', '48']

df_melt = df.melt(id_vars=['Mapa', 'Cutoff'], value_vars=threads, var_name='Threads', value_name='Time_s')
df_melt['Threads'] = df_melt['Threads'].astype(int)
df_melt['Time_s'] = pd.to_numeric(df_melt['Time_s'], errors='coerce')

for map_name in df['Mapa'].unique():
    map_data = df[df['Mapa'] == map_name].drop(columns=['Mapa'])
    
    print(f"\n{'='*40}")
    print(f" VÝSLEDKY PRO MAPU: {map_name}")
    print(f"{'='*40}")
    
    print("\nČas výpočtu v sekundách:")
    print(map_data.to_markdown(index=False))
    
    plt.figure(figsize=(10, 6))
    
    for cutoff in sorted(map_data['Cutoff'].unique()):
        subset = df_melt[(df_melt['Mapa'] == map_name) & (df_melt['Cutoff'] == cutoff)]
        plt.plot(subset['Threads'], subset['Time_s'], marker='o', label=f'Cutoff {cutoff}')

    plt.title(f'Vliv Task Cutoffu a počtu vláken na mapě {map_name}')
    plt.xlabel('Počet vláken (Threads)')
    plt.ylabel('Čas výpočtu (s)')
    plt.xticks([1, 2, 4, 8, 16, 32, 48])
    plt.yscale('log')
    plt.grid(True, which="both", ls="--")
    plt.legend(title="Hodnota Cutoff")
    
    filename = f'graf_{map_name.replace(".txt", "")}.png'
    plt.savefig(filename, bbox_inches='tight')
    plt.close()
    
    print(f"\nGraf byl uložen jako: {filename}")

print("\nVšechna data zpracována.")