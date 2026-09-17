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
        SlotView slotView (int slot1) const;
        LineView lineView (int slot1) const;
        TransportView transportView() const;
        MasterView masterView() const;
        std::array<MacroView, kMacros> macroViews() const;
        AboutView aboutView() const;
        PrefsView prefsView() const;
        PresetView presetView() const;
        UndoView undoView() const;

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
        void setLocked (int slot1, const juce::String& paramName, bool locked);
        void setRange (int slot1, const juce::String& paramName, float min, float max);
        void setProb (int slot1, const juce::String& paramName, float prob);
        void setTransition (int slot1, const juce::String& paramName, bool glide);
        void setStepOn (int slot1, int step1, bool on);
        void setStepExplicit (int slot1, int step1, const juce::String& paramName, float value);
        void generate (int slot1, int first1, int last1, float density);
        void capture (int slot1, int first1, int last1);
        void setMasterSeed (juce::uint32 seed);
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
        void setLineModeB (int slot1, bool modeB);
        void setShownParam (int slot1, const juce::String& paramName);
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

        PlugProcessor& proc;
        Prefs preferences;
        PresetLibrary library;          // la bibliothèque du menu [Preset ▾] (utilisateur puis livré)
        juce::String presetName;        // nom du preset courant, vide tant que rien n'a été chargé
        bool presetModified = false;    // l'état a bougé depuis : la barre affiche « geste1 * »
        juce::ListenerList<Listener> listeners;

        // Vrai tant qu'une commande, un geste de knob ou un remplacement d'état court.
        // Lu depuis parameterValueChanged, qui peut venir du thread audio : atomique.
        std::atomic<int> busy { 0 };
        std::atomic<juce::uint32> hostPending { 0 };   // emplacements marqués depuis le dernier tour
        juce::uint16 hostDriven = 0;                   // acquis pour la session

        std::vector<int> slotOfParameter;              // indice de paramètre → emplacement 1..16, 0 sinon

        ViewMask pending;
        int lastStep = -1000;                          // sentinelle : aucune notification de transport encore émise

        int selection = 1;
        int stepFirst = 1, stepLast = 32;
        std::array<bool, kSlots> lineModeB {};
        std::array<juce::String, kSlots> shownParam;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Presenter)
    };
}
