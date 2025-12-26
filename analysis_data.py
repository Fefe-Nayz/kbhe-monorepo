import pandas as pd
import matplotlib.pyplot as plt
import numpy as np
import glob
import os

def analyze_csv(filepath):
    print(f"\n--- Analyse de {filepath} ---")
    try:
        df = pd.read_csv(filepath)
    except Exception as e:
        print(f"Erreur lors de la lecture du fichier: {e}")
        return

    # Identification de la colonne de données
    # On suppose que la colonne s'appelle 'Value' comme vu dans l'exemple, 
    # sinon on prend la deuxième colonne, ou la première si une seule existe.
    if 'Value' in df.columns:
        val_col = 'Value'
    elif len(df.columns) >= 2:
        val_col = df.columns[1]
    else:
        val_col = df.columns[0]
    
    data = df[val_col]
    
    # Calculs statistiques
    moyenne = data.mean()
    mediane = data.median()
    ecart_type = data.std()
    min_val = data.min()
    max_val = data.max()
    ecart_max_range = max_val - min_val
    
    # Ecarts par rapport à la moyenne
    ecarts = data - moyenne
    max_abs_deviation = ecarts.abs().max()

    print(f"Colonne analysée: {val_col}")
    print(f"Moyenne: {moyenne:.4f}")
    print(f"Médiane: {mediane:.4f}")
    print(f"Écart type: {ecart_type:.4f}")
    print(f"Min: {min_val}")
    print(f"Max: {max_val}")
    print(f"Écart Max (Max - Min): {ecart_max_range}")
    print(f"Écart Max absolu par rapport à la moyenne: {max_abs_deviation:.4f}")

    # Création des graphiques
    fig, axs = plt.subplots(2, 1, figsize=(10, 10))
    
    # 1. Courbe des valeurs
    axs[0].plot(data, label='Valeurs', alpha=0.8)
    axs[0].axhline(moyenne, color='r', linestyle='--', label=f'Moyenne ({moyenne:.2f})')
    axs[0].axhline(min_val, color='g', linestyle=':', label=f'Min ({min_val})')
    axs[0].axhline(max_val, color='g', linestyle=':', label=f'Max ({max_val})')
    axs[0].fill_between(data.index, min_val, max_val, color='gray', alpha=0.1, label='Plage Min-Max') # Visuel "entre min et max"
    axs[0].set_title(f'Courbe des valeurs - {os.path.basename(filepath)}')
    axs[0].set_xlabel('Index')
    axs[0].set_ylabel('Valeur')
    axs[0].legend()
    axs[0].grid(True)

    # 2. Distribution des écarts (Histogramme)
    axs[1].hist(ecarts, bins=30, edgecolor='black', alpha=0.7)
    axs[1].set_title('Distribution des écarts par rapport à la moyenne')
    axs[1].set_xlabel('Écart')
    axs[1].set_ylabel('Fréquence')
    axs[1].grid(True)

    plt.tight_layout()
    plt.show()

if __name__ == "__main__":
    # Trouver tous les fichiers CSV dans le dossier courant
    # On utilise le chemin absolu du dossier où se trouve le script pour être sûr
    script_dir = os.path.dirname(os.path.abspath(__file__))
    csv_files = glob.glob(os.path.join(script_dir, '*.csv'))

    if not csv_files:
        print(f"Aucun fichier CSV trouvé dans {script_dir}.")
    else:
        print(f"Fichiers trouvés: {[os.path.basename(f) for f in csv_files]}")
        for csv_file in csv_files:
            analyze_csv(csv_file)
