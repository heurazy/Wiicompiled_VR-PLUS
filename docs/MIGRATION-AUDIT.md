# Audit de migration — 26 septembre 2026

Comparaison du port local `mario-kart-wii-VR-port` avec `upstream-port-integration`,
y compris les modifications non commitées. Cet audit vérifie le code et ses raccordements,
pas le fonctionnement de chaque fonction dans un casque. **La migration n'est pas complète.**

## Complément intégré après cet audit

Les commandes exactes/raccourcis SteamVR, les profils Index/Vive/WMR/PICO,
la calibration et la résolution adaptative, l'opt-in du scan Wii Remote,
le renommage sans Mii NAND, le choix SteamVR et le packaging lanceur + portable
ont maintenant été intégrés. Les lignes correspondantes de la liste historique
« Manquant » ci-dessous sont remplacées par cet état.
Voir [commandes et packaging](PORT-CONTROLS-AND-PACKAGING.md) pour les chemins,
les réglages, les limites et les tests. Cela ne confirme pas les autres effets
historiques signalés comme non vérifiés dans cet audit.

## Présent dans le code

| Ajout | Vérification |
| --- | --- |
| Caméras Original, Première personne, Diorama | `vr_controls.h`, `mkw_vr_first_person.cpp`, cycle dans `settings_overlay.cpp`. Diorama possède distance, hauteur et échelle. |
| Caméra d'origine pendant l'introduction de course | `opening_pending` puis lecture du stade de course ; choix par défaut appliqué ensuite. Le saut de l'animation reste à vérifier en jeu. |
| Cockpit adapté au pilote, aux motos et aux changements de taille | Position des yeux, échelle de Movement, poses des poignées et restauration des modèles dans `mkw_vr_first_person.cpp`. |
| Volant réel du modèle, guidons et volant VR de secours | Extraction et publication du maillage natif, animation pilotée par `DrivingSnapshot`, option `native_steering_wheel`. |
| Prise du volant, conduite à deux mains, filtrage et continuité | `steering_wheel.h`, `openxr_driving.h` et `openxr_driving.cpp`, avec tests de conduite. |
| Mouvement du kart et mode Safe | `FirstPersonMotionLevel`, `SafeTiltFilter`, réglages et tests ; Safe par défaut. |
| Occlusion des mains | Profondeur du monde transmise au rendu cockpit, distincte du HUD. Validation visuelle nécessaire. |
| HUD/minicarte sur la main gauche | `BuildHandHud`, matrice dédiée dans Aurora ; HUD frontal en cockpit, secours frontal sans suivi de main. C'est le HUD complet qui est déplacé. |
| Textures persistantes de la minicarte | Distinction entre copies EFB récentes et contenu persistant ; attente des pipelines pour les textures produites une seule fois. |
| Pseudos spatialisés | Capture du joueur et ancrage mondial de CtrlRaceNameBalloon ; rendu distinct du HUD de main. Taille et hauteur restent à confirmer en versus/en ligne. |
| Introduction et deux tutoriels | Sélecteur au pointeur, progression sauvegardée, demande de pause et confirmation, réinitialisation. Les descriptions suivent les commandes de la nouvelle base. |
| Modèles de manettes SteamVR | Modèles, textures, composants animés et points d'attache chargés par `steam_controller_models.inl`. Modèle procédural de secours. |
| Shader Dielectric spatial et qualité | Rendu stéréo dans `vr_ui.hpp`, options Off/Low/Balanced/High ; crédits @XorDev et BigWalk conservés. |
| Menus ancrés et pointeur | Ancrage du menu, visée Wii Remote via poses OpenXR et pointeur du panneau VR natif. |
| Culling étendu | Extensions FRUSTUM et ClipInfoMgr ; le générateur respecte maintenant la priorité des remplacements natifs dans Retro Rewind. |
| Présentation à fréquence VR sans accélération du jeu | Images conservées/interpolées dans Aurora, cadence OpenXR indépendante du tick de jeu. Ce n'est pas une simulation à 90/120 Hz. |
| Corrections d'orientation et de panneaux noirs | Présentation native/composition de secours et orientation des projections dans la nouvelle chaîne de rendu. Validation matérielle nécessaire. |
| Volants USB et pédales | `physical_wheel.cpp`, calibration, palettes dérive/objet et vibrations ; découverte déclenchée par activation/options/hotplug plutôt que scan périodique permanent. |
| Joueur local en cockpit multijoueur | Résolution du racer à partir de la RaceCamera via `mkw_vr_player.h`, sans choisir arbitrairement le racer zéro. Partie en ligne non validée par cet audit. |

## Partiel ou manquant

| Écart | Preuve / conséquence |
| --- | --- |
| Commandes exactes de notre port | Le défaut reste Wii Remote/Nunchuk : A accélère, gâchette droite dérive, gâchette gauche utilise l'objet, B regarde derrière, Y ouvre les options VR. Notre combinaison gâchette gauche frein/recul, Y objet, X figure et A dérive en cockpit n'a pas été reprise. Le mode Gamepad dépend du mapping SDL. |
| Profils Index, Vive, WMR, Odyssey, PICO et Touch spécifiques | `OpenXRInput::SuggestBindings` ne suggère que Oculus Touch et simple_controller. Les modèles SteamVR n'ajoutent pas les bindings manquants. Une émulation du runtime peut marcher, mais la compatibilité annoncée n'est pas assurée. |
| Raccourcis SteamVR | Absence de `SteamVrTrickPause` et du couple X+Y de notre port. La nouvelle base ouvre le panneau avec Y en mode Wii Remote ou les deux clics de sticks en mode Gamepad. |
| Calibration du stick VR et permutations | Les clés `stick_center_x`, `stick_center_y`, `stick_deadzone`, `stick_outer`, `swap_item_trick`, `swap_cockpit_drift_brake` ne sont pas lues par la cible. Un ancien Config.toml ne restaure pas ces préférences. |
| Résolution adaptative | Ni clé `adaptive_resolution` ni contrôleur `AdaptiveResolution` dans la cible. Le README en parle à tort. |
| Recherche Wii Remote après mise à jour | Défaut désactivé et option existante dans le menu desktop Wii Remotes, mais la cible lit encore `wii_continuous_scan`. Pas de clé `wii_continuous_scan_opt_in`, ni de case au sommet du panneau VR. Une ancienne préférence activée reste donc acceptée. |
| Renommage d'une licence sans Mii NAND | Le patch WheelWizard ne contient que la protection contre l'édition pendant le jeu ; le correctif de repli du nom de licence décrit dans `VR-STABILITY-AND-LICENSE-FIXES.md` n'y figure pas. |
| Publication lanceur + portable | Patch WheelWizard présent, mais pas de `Launcher/Build-Portable.ps1` dans la cible malgré les instructions qui le mentionnent. Les deux EXE de test ne constituent pas une migration du packaging complet. |
| Forçage de SteamVR | Aucun environnement de lancement imposant le runtime SteamVR trouvé dans le service de lancement cible. Charger ses modèles ne force pas son runtime OpenXR. |
| Stabilisation initiale de l'ancrage | Écran droit par rotation limitée au yaw, mais ancrage dès la première pose marquée valide. La fenêtre de stabilisation de 400 ms décrite dans les documents n'est pas implémentée dans ce chemin. |
| Ancienne option de présentation asynchrone | `async_presentation` n'est pas reprise comme option. La cible possède sa propre architecture de présentation ; ce n'est pas automatiquement une fonctionnalité perdue, mais la préférence n'est pas migrée. |

## Ajouts historiques non confirmés

Le retour temporaire à la caméra vanilla sous Bill Balle et l'effet d'encre du Bloups ne possèdent
pas de chemin spécifique identifiable dans les fichiers locaux examinés, ni dans l'ancien runtime
VR disponible ni dans la cible. Leur annonce dans la conversation/README ne suffit pas à prouver
leur présence : vérifier les transformations/effets en jeu et retrouver leurs modifications d'origine.
Le moteur gère les effets EFB et le brouillard, mais cela ne prouve pas une spatialisation spécifique
de chaque effet HUD. Même limite pour le résultat visuel exact du correctif de reflet solaire double.

## WheelWizard

Le patch `integrations/wheelwizard-vr.patch` porte l'intégration du lanceur, les packs,
la préparation des assets de mods, les chemins NAND/sauvegardes, la réparation et les diagnostics.
Il ne rend pas toutes les modifications exécutables/Gecko ni tous les mods compatibles avec une
recompilation statique. Une intégration présente dans le patch doit encore être appliquée et compilée
dans le lanceur distribué ; les EXE de jeu ne l'intègrent pas eux-mêmes.

## Vérifications

- Neuf tests VR exécutés pendant cet audit : caméra, configuration, tutoriels, panneau,
  politique, conduite manuelle, changement de caméra, Wii Remote et sélection du joueur : tous passent.
- Les tables complètes des deux produits ont été validées après la correction du dispatch.
- Cinq tests du générateur de shards ont passé lors du correctif précédent, dont le nouveau cas
  d'un ancien profil Retro Rewind avec remplacement natif ajouté ensuite.
- Aucun essai en casque ou partie réseau effectué pendant cet audit.

Priorité : commandes et profils de manettes, scan Wii Remote hérité, lancement/ancrage initial,
puis renommage de licence et packaging. Ensuite reprendre les options graphiques manquantes
et tester les effets HUD en jeu. Le README doit être aligné sur ces états avant une release.
