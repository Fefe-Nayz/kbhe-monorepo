import hid
import time
import sys

# --- Configuration ---
# Doit correspondre à usb_descriptors.h
VID = 0x9172
PID = 0x0002

# Taille du packet définie dans le firmware (64 octets)
# +1 pour le Report ID (requis par hidapi sur Windows)
PACKET_SIZE = 64

def find_device_path():
    """
    Parcourt les périphériques pour trouver l'interface Raw HID.
    On cherche Interface 1 OU Usage Page 0xFF00.
    """
    print(f"Recherche du périphérique VID=0x{VID:04x} PID=0x{PID:04x}...")
    
    for d in hid.enumerate(VID, PID):
        print(f"  -> Trouvé: {d['product_string']} (Interface: {d['interface_number']}, UsagePage: 0x{d['usage_page']:04x})")
        
        # Critères pour identifier le Raw HID :
        # 1. Interface 1 (car 0 est le clavier)
        # 2. Usage Page 0xFF00 (Vendor Defined)
        if d['interface_number'] == 1 or d['usage_page'] == 0xFF00:
            return d['path']
    
    return None

def main():
    # 1. Trouver le chemin d'accès (Path)
    # On ne peut pas juste faire hid.device().open(VID, PID) car cela pourrait
    # ouvrir le clavier (interface 0) qui est verrouillé par l'OS.
    path = find_device_path()

    if path is None:
        print("❌ Erreur : Périphérique Raw HID introuvable.")
        print("   Vérifie que le STM32 est branché et que le code tourne.")
        # Astuce pour Linux
        if sys.platform.startswith('linux'):
            print("   Linux : As-tu configuré les règles udev ?")
        return

    print(f"✅ Interface Raw HID trouvée : {path}")

    try:
        # 2. Ouverture de la connexion
        h = hid.device()
        h.open_path(path)
        
        # Mode non-bloquant : read() retourne immédiatement si vide
        h.set_nonblocking(1)

        print("\n--- Démarrage de la boucle de commande ---")
        print("Le script envoie un compteur incrémental et attend l'écho.")
        print("Appuie sur CTRL+C pour arrêter.\n")

        counter = 0

        while True:
            # --- ÉTAPE A : Préparation de la commande ---
            # Structure : [ReportID=0, CmdType, Valeur, ...Remplissage...]
            buffer = [0] * (PACKET_SIZE + 1)
            
            buffer[0] = 0x00  # Report ID (Toujours 0 pour hidapi/Generic)
            buffer[1] = 0xA1  # Exemple de code commande (ex: "Set Config")
            buffer[2] = counter % 255 # Une donnée variable

            # --- ÉTAPE B : Envoi (OUT Endpoint) ---
            h.write(buffer)
            print(f"Envoi >> CMD: 0xA1 | Val: {buffer[2]}")

            # --- ÉTAPE C : Lecture (IN Endpoint / Echo) ---
            # On attend un peu pour laisser le temps au STM32 de traiter
            time.sleep(0.05) 
            
            # Lecture de 64 octets max
            data = h.read(PACKET_SIZE)
            
            if data:
                # Conversion en Hex pour affichage propre
                hex_str = " ".join([f"{x:02x}" for x in data[:8]]) # Affiche les 8 premiers
                print(f"Reçu  << {hex_str} ... (Total {len(data)} bytes)")
                
                # Vérification simple de l'écho
                if len(data) > 1 and data[1] == counter % 255:
                    print("         [Echo Validé OK]")
                else:
                    print("         [Données inattendues]")
            else:
                print("Reçu  << (Rien)")

            counter += 1
            print("-" * 30)
            time.sleep(1) # Pause d'une seconde entre chaque envoi

    except IOError as ex:
        print(f"❌ Erreur de communication USB (Débranché ?) : {ex}")
    except KeyboardInterrupt:
        print("\nArrêt demandé par l'utilisateur.")
    finally:
        try:
            h.close()
        except:
            pass
        print("Connexion fermée.")

if __name__ == "__main__":
    main()