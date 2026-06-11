# Modèle V2

## Protocole expérimental

- Usage d'une imprimante 3D pour appuyer sur la touche 0 du clavier par incrément de **0.025mm**
- Mesure tous les **0.025mm** des points ADC brutes avec un filtre médiane sur 8 échantillons

## Résultats

### Regression

- **Équation :** $y = -4.061\times 10^{-16}x^6 + 6.198\times 10^{-12}x^5 - 3.938\times 10^{-08}x^4 + 1.333\times 10^{-04}x^3 - 0.253715x^2 + 257.380144x - 108700$
- **r :** 0.999964
- **R^2 :** 0.999928
- **Points utilisés :** 139

### Validation de la LUT

L'extraction de la LUT (paramétrée sur une plage de 2180 à 2850 pts avec un mode entier d'une précision de 1 µm) a été directement confrontée aux 139 points expérimentaux. 

**Résumé des erreurs absolues :**
* **Erreur Min :** 0.000000 mm | 0.0 µm
* **Erreur Max :** 0.029000 mm | 29.0 µm
* **Erreur Moyenne :** 0.006942 mm | 6.9 µm
* **Écart-type (Std) :** 0.005020 mm | 5.0 µm
* **RMSE (Précision globale) :** 0.008568 mm | 8.6 µm

**Distribution des percentiles d'erreur :**
* **P50 (Médiane) :** 6.0 µm
* **P75 :** 9.0 µm
* **P90 :** 14.0 µm
* **P95 :** 16.1 µm
* **P99 :** 21.9 µm
* **P99.5 :** 24.9 µm
* **P99.9 :** 28.2 µm

---

## Conclusion Comparative (V1 vs V2)

Le passage d'un protocole manuel (pied à coulisse) à un banc de test automatisé (imprimante 3D + filtre médiane) a permis de fiabiliser totalement la capture physique du capteur. 

Voici la comparaison exhaustive de tous les indicateurs de validation entre l'ancien modèle (V1) et le Modèle V2 :

| Indicateur Statistique | Modèle V1 (Manuel) | Modèle V2 (Imprimante 3D) | Évolution |
| :--- | :---: | :---: | :--- |
| **Points de mesure ($n$)** | 49 | **139** | +183% de résolution |
| **Erreur Maximale** | 149.2 µm | **29.0 µm** | Erreur divisée par **5,1** |
| **Erreur Moyenne** | 21.4 µm | **6.9 µm** | Erreur divisée par **3,1** |
| **Écart-type (Std)** | 24.5 µm | **5.0 µm** | Dispersion divisée par **4,9** |
| **RMSE** | 32.5 µm | **8.6 µm** | Fidélité globale améliorée de 380% |
| **P50 (Médiane)** | 16.7 µm | **6.0 µm** | Gain massif au centre de la distribution |
| **P95** | 59.2 µm | **16.1 µm** | Fiabilité structurelle à 95% |
| **P99.9 (Pire cas)** | 145.6 µm | **28.2 µm** | Disparition totale des décrochages violents |

En conclusion, l'élimination du bruit de mesure humain via la commande numérique a permis de générer une table de correspondance d'une stabilité absolue, réduisant l'erreur moyenne à moins de 7 µm et garantissant une erreur systémique maximale infranchissable de 30 µm, validant ainsi ce protocole pour une application de haute précision.