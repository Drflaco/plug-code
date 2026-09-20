// Presenter — la couche de présentation (J4b a), ETAT Rév. 9).
// Invariants et leur raison :
//   · il vit dans PlugProcessor, pas dans l'éditeur : Live ferme et rouvre la fenêtre
//     sans arrêt, et l'état de session (emplacement sélectionné, pas sélectionnés,
//     mode A/B par ligne) doit survivre à la fenêtre, pas au projet (Q7) ;
//   · message thread uniquement : JUCE_ASSERT_MESSAGE_THREAD dans chaque commande.
//     Rien ici ne touche le thread audio, et la position de lecture se lit par le
//     seul Engine::uiSnapshot() (c-1) — jamais valueCurve, qui appartient à l'audio ;
//   · un widget n'inclut que ce fichier et ViewTypes.h : cet en-tête ne tire donc ni
//     StateSchema.h, ni Engine.h, ni PlugProcessor.h, ni Skill.h. C'est la frontière
//     v1/v2, et scripts/check_ui_boundary.ps1 la rend exécutable ;
//   · chaque commande annulable ouvre UNE transaction nommée en français : le pilote
//     lit son propre geste dans l'historique, pas « setProperty ».
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "../PresetLibrary.h"
#include "Prefs.h"
#include "ViewTypes.h"
#include <array>
#include <atomic>
#include <memory>

namespace plug { class PlugProcessor; }

namespace plug::ui
{
    class Presenter : private juce::ValueTree::Listener,
                      private juce::AsyncUpdater,
                      private juce::Timer,
                      private juce::AudioProcessorParameter::Listener
    {
    public:
        enum class MoveMode { Swap, Insert, Copy };

        class Listener
        {
        public:
            virtual ~Listener() = default;
            // Une notification par tour de boucle, avec le masque de ce qui a bougé :
            // le widget relit sa View, il ne suit pas les propriétés une par une.
            virtual void viewChanged (const ViewMask& mask) = 0;
            // Émis seulement quand le PAS change : 30 Hz de timer, pas 30 Hz de repeints.
            virtual void transportChanged (const TransportView&) {}
        };

        explicit Presenter (PlugProcessor& processor);
        ~Presenter() override;

        void addListener (Listener* l);
        void removeListener (Listener* l);

        //======================================================================
        // Les vues — des valeurs, calculées à la demande depuis l'arbre et le registre.
        SlotView slotView (int slot1) const;                  // valeurs du premier pas sélectionné (c-6)
        SlotView slotViewAt (int slot1, int step1) const;     // valeurs d'un pas donné : la case survolée (6b)
        LineView lineView (int slot1) const;
        TransportView transportView() const;
        SequencerView sequencerView() const;
        MasterView masterView() const;
        std::array<MacroView, kMacros> macroViews() const;
        AboutView aboutView() const;
        PrefsView prefsView() const;
        PresetView presetView() const;
        UndoView undoView() const;
        // De quoi est faite une sélection de pas, et où en est la génération.
        StepCountsView stepCountsView (int slot1, int first1, int last1) const;
        GenerationView generationView() const;

        // Emplacements affichés = max (préférence, plus haut emplacement occupé) (Q6).
        int displayedSlots() const;
        // Catalogue proposé au menu d'un emplacement : identifiants du registre.
        juce::StringArray skillIds() const;
        juce::String skillLabel (const juce::String& skillId) const;
        // Texte d'avertissement du glisser (d-1) ; vide s'il n'y a rien à dire.
        juce::String moveWarning (int from1) const;
        // La liste « À venir », embarquée à la compilation. Affichée à l'étape 6.
        juce::String avenirText() const;

        //======================================================================
        // Commandes annulables. Chacune ouvre une transaction nommée.
        void beginGesture (const juce::String& gridId);
        void setParam (const juce::String& gridId, float raw);
        void endGesture (const juce::String& gridId);

        void setSkill (int slot1, const juce::String& skillId);
        void clearSlot (int slot1);
        void moveSlot (int from1, int to1, MoveMode mode);
        void setActive (int slot1, bool on);
        void setTail (int slot1, bool ring);
        // Phase 3 (pilote, 20/09) : le Dry / Wet GÉNÉRAL de l'emplacement, surcouche sur
        // le mix séquencé — attribut du Slot, hors grille, hors automation hôte. Seul,
        // setWet ouvre sa transaction « Dry / Wet 65 % · emplacement 3 » ; dans un geste
        // (begin / end), une seule transaction renommée à chaque valeur.
        void setWet (int slot1, float wet);
        void beginWetGesture (int slot1);
        void endWetGesture();
        // L'amortissement (phase 3) : même logement, même discipline de transaction.
        void setDamp (int slot1, float damp);
        void beginDampGesture (int slot1);
        void endDampGesture();
        void setLocked (int slot1, const juce::String& paramName, bool locked);
        void setRange (int slot1, const juce::String& paramName, float min, float max);
        void setProb (int slot1, const juce::String& paramName, float prob);
        void setTransition (int slot1, const juce::String& paramName, bool glide);
        void setStepOn (int slot1, int step1, bool on);
        void setStepExplicit (int slot1, int step1, const juce::String& paramName, float value);
        // Correction 4 de la phase 2 (geste A, 18/09) : une MÊME valeur explicite sur tous
        // les pas de la sélection, qui passent en mode figé — comme « Figer », mais avec
        // une valeur posée par le pilote. Une transaction : « Coupure à 333 Hz sur 16 pas ».
        void setStepsExplicit (int slot1, int first1, int last1, const juce::String& paramName, float value);
        // Un mouvement continu (knob, curseur) sur la sélection : UNE transaction de la
        // prise au relâché, renommée à chaque valeur — l'historique dit la dernière posée.
        void beginStepsGesture (int slot1, int first1, int last1, const juce::String& paramName);
        // `finalName` non vide : la transaction prend ce nom en fermant — « Coupure dessinée · pas 4–12 » (6b).
        void endStepsGesture (const juce::String& finalName = {});
        void generate (int slot1, int first1, int last1, float density);
        void capture (int slot1, int first1, int last1);
        void setMasterSeed (juce::uint32 seed);
        void setDensity (float density);
        // Les réglages de la grille (§3.3.3), en unités lisibles : la conversion vers
        // seq.length / seq.division vit ici, pas dans le widget. Le swing, lui, est
        // un geste continu sur « seq.swing » : beginGesture / setParam / endGesture.
        void setSeqLength (int steps);          // 2..32, transaction « Longueur 32 pas »
        void setSeqDivision (int index);        // 0..14, transaction « Division 1/16 »

        // Correction 5 de la phase 2 (18/09) : la ligne d'un emplacement comme MOTIF,
        // indépendant de l'effet (un preset sauve tout l'état ; un .seqline ne sauve
        // qu'une ligne). Reset remet une séquence neutre : 32 pas actifs, mode base,
        // aucune valeur explicite ni générée, plages et probabilités à leurs défauts —
        // l'effet reste chargé et actif, les verrous restent ceux de la skill.
        // Enregistrer / Charger : le sous-arbre Line (les Step et leurs V) plus les plages,
        // probabilités et transitions des Param, même sérialisation que le .plugstate.
        // Charger ne change JAMAIS la skill de l'emplacement : on charge un motif.
        void resetLine (int slot1);                              // transaction « Reset séquence Filtre »
        bool saveLine (int slot1, const juce::File& f) const;    // hors undo, comme savePreset
        bool loadLine (int slot1, const juce::File& f);          // transaction « Charger séquence Filtre »
        juce::File defaultLineFile (int slot1) const;            // dossier des presets, SEQ_FILTRE_1.seqline
        bool loadPreset (const juce::File& f);
        // Charge une entrée de la bibliothèque (même route que loadPreset : un fichier).
        bool loadPresetIndex (int index);
        bool savePreset (const juce::File& f);
        void rescanPresets();
        void undo();
        void redo();

        //======================================================================
        // Hors undo : état de session et préférences. Rien de tout cela n'entre
        // dans le preset (décision pilote 16/09 pour le mode A/B).
        void selectSlot (int slot1);
        int selectedSlot() const noexcept { return selection; }
        void selectSteps (int first1, int last1);
        int firstSelectedStep() const noexcept { return stepFirst; }
        int lastSelectedStep() const noexcept { return stepLast; }
        // Décision pilote du 18/09 (correction 4) : la ligne ENTIÈRE sélectionnée (1–32,
        // le défaut) → les contrôles règlent la base ; une sélection PARTIELLE → ils
        // posent leur valeur sur ces pas et les figent. Une règle, partout : knob,
        // curseurs du panneau, colonne « Pas » de l'inspecteur.
        bool selectionIsPartial() const noexcept { return stepFirst != 1 || stepLast != kSteps; }
        // 6b (pilote, 18/09) : la case SURVOLÉE dans le séquenceur. L'inspecteur l'affiche ;
        // rien d'autre ne la lit — les commandes (Générer, Figer, valeurs) visent toujours
        // la sélection. (0, 0) = aucune ; la souris qui sort de la grille l'efface.
        void setHover (int slot1, int step1);
        int hoverSlot() const noexcept { return hovSlot; }
        int hoverStep() const noexcept { return hovStep; }
        void setLineModeB (int slot1, bool modeB);
        void setShownParam (int slot1, const juce::String& paramName);
        // Point 6a (pilote, 18/09) : cliquer un paramètre dans le panneau de l'effet ou
        // dans l'inspecteur = la ligne de l'emplacement le MONTRE en barres. Une commande,
        // une notification : paramètre montré + mode B.
        void showParam (int slot1, const juce::String& paramName);
        void toggleRatio();
        void setPrefZoom (double zoom);
        void setPrefHoverHelp (bool on);
        void setPrefHelpDelayMs (int ms);
        void setPrefDefaultMixLaw (int law);
        void setPrefShownSlots (int count);

        //======================================================================
        // Appelé par PlugProcessor autour d'un remplacement d'état (projet ouvert,
        // preset chargé, programme changé) : sans cette parenthèse, les 284 valeurs
        // qui arrivent d'un coup seraient prises pour de l'automation hôte (d-1).
        struct ScopedStateReplacement
        {
            explicit ScopedStateReplacement (Presenter* p) noexcept;
            ~ScopedStateReplacement() noexcept;
            Presenter* presenter;
        };

    private:
        void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
        void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override;
        void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;
        void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override;
        void valueTreeParentChanged (juce::ValueTree&) override;
        void valueTreeRedirected (juce::ValueTree&) override;
        void handleAsyncUpdate() override;
        void timerCallback() override;
        void parameterValueChanged (int parameterIndex, float newValue) override;
        void parameterGestureChanged (int, bool) override {}

        void mark (juce::uint32 bits, int slot1 = 0);
        void markGridId (const juce::String& gridId);
        void markNode (const juce::ValueTree& t);

        // Ouvre une transaction et marque la commande : pendant sa durée, aucune valeur
        // reçue n'est comptée comme venant de l'hôte (d-1, précision pilote Q4).
        struct Command
        {
            Command (Presenter& p, const juce::String& name);
            ~Command();
            Presenter& presenter;
        };
        std::unique_ptr<Command> stepsGesture;   // ouvert par beginStepsGesture, fermé par endStepsGesture
        std::unique_ptr<Command> slotGesture;    // Dry / Wet ou amortissement : begin…Gesture / end…Gesture

        PlugProcessor& proc;
        Prefs preferences;
        PresetLibrary library;          // la bibliothèque du menu [Preset ▾] (utilisateur puis livré)
        juce::String presetName;        // nom du preset courant, vide tant que rien n'a été chargé
        bool presetModified = false;    // l'état a bougé depuis : la barre affiche « geste1 * »
        juce::ListenerList<Listener> listeners;

        // Deux compteurs, deux questions différentes :
        //  · `busy` — une commande, un geste OU un remplacement d'état court. Il répond
        //    « cette valeur ne vient pas de l'hôte » (hostDriven, d-1). Lu depuis
        //    parameterValueChanged, qui peut venir du thread audio : atomique.
        //  · `editing` — le PILOTE édite, et rien d'autre. Il répond « le preset affiché
        //    ne décrit plus l'état » (l'étoile de la barre). Un chargement de preset et un
        //    flush venu de l'hôte ne le lèvent jamais : même discrimination que hostDriven
        //    (décision pilote du 17/09).
        std::atomic<int> busy { 0 };
        std::atomic<int> editing { 0 };
        std::atomic<juce::uint32> hostPending { 0 };   // emplacements marqués depuis le dernier tour
        juce::uint16 hostDriven = 0;                   // acquis pour la session

        std::vector<int> slotOfParameter;              // indice de paramètre → emplacement 1..16, 0 sinon

        ViewMask pending;
        int lastStep = -1000;                          // sentinelle : aucune notification de transport encore émise

        int selection = 1;
        int stepFirst = 1, stepLast = 32;
        int hovSlot = 0, hovStep = 0;                  // case survolée (6b), 0 = aucune
        std::array<bool, kSlots> lineModeB {};
        std::array<juce::String, kSlots> shownParam;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Presenter)
    };
}
