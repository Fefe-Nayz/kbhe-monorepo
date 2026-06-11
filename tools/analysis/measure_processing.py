import os
import pandas as pd


def process_csv_exact():
    print("=== Traitement de Fichier CSV avec Spécifications ===")

    # 1. Demander le chemin du fichier
    file_path = input(
        "Glissez-déposez le fichier CSV ici (ou entrez son chemin) : "
    ).strip()
    file_path = file_path.strip("'\"")

    if not os.path.exists(file_path):
        print(f"Erreur : Le fichier '{file_path}' est introuvable.")
        return

    # 2. Demander la distance correspondant au zéro
    try:
        target_zero = float(
            input("Entrez la distance qui correspond au zéro (ex: 1.0) : ")
        )
    except ValueError:
        print("Erreur : Vous devez entrer une valeur numérique valide.")
        return

    try:
        # 3. Lecture du CSV en conservant l'en-tête d'origine
        df = pd.read_csv(file_path)

        # Extraction des noms de colonnes d'origine pour les réutiliser à la fin
        col_distance = df.columns[0]
        col_adc = df.columns[1]

        # 4. Trouver la ligne la plus proche du "zéro" choisi
        idx_zero = (df[col_distance] - target_zero).abs().idxmin()
        actual_zero_val = df.loc[idx_zero, col_distance]

        # 5. Tronquer le DataFrame pour commencer à partir de cette ligne
        df_truncated = df.iloc[idx_zero:].copy()

        # 6. Appliquer l'offset et formater les types
        # Calcul de l'offset
        df_truncated[col_distance] = df_truncated[col_distance] - actual_zero_val

        # Arrondi strict à 3 décimales pour la distance
        df_truncated[col_distance] = df_truncated[col_distance].round(3)

        # Conversion forcée en entier (int) pour la colonne ADC
        df_truncated[col_adc] = df_truncated[col_adc].astype(int)

        # 7. Définir le nom du fichier de sortie
        dir_name, file_name = os.path.split(file_path)
        base_name, ext = os.path.splitext(file_name)
        output_file = os.path.join(dir_name, f"{base_name}_offset_0{ext}")

        # 8. Sauvegarde du résultat en gardant l'en-tête (header=True)
        # float_format="%.3f" garantit que le CSV final affiche bien les 3 décimales (ex: 0.000, 0.025)
        df_truncated.to_csv(output_file, index=False, header=True, float_format="%.3f")

        print("\n--- Traitement terminé avec succès ! ---")
        print(f"• Ligne de départ réelle trouvée à : {actual_zero_val} mm")
        print(f"• Fichier sauvegardé sous : {output_file}")

    except Exception as e:
        print(f"\nUne erreur est survenue lors du traitement :\n{e}")


if __name__ == "__main__":
    process_csv_exact()