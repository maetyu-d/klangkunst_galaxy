#pragma once

#include "GameModel.h"

class GameComponent final : public juce::AudioAppComponent,
                            private juce::Timer
{
public:
    GameComponent();
    ~GameComponent() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

    void prepareToPlay (int samplesPerBlockExpected, double sampleRate) override;
    void getNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill) override;
    void releaseResources() override;

    bool keyPressed (const juce::KeyPress& key) override;
    void mouseMove (const juce::MouseEvent& event) override;
    void mouseDrag (const juce::MouseEvent& event) override;
    void mouseDown (const juce::MouseEvent& event) override;
    void mouseUp (const juce::MouseEvent& event) override;
    void mouseExit (const juce::MouseEvent& event) override;
    void mouseWheelMove (const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;

private:
    enum class Scene
    {
        title,
        galaxy,
        landing,
        builder
    };

    enum class TitleAction
    {
        none,
        resumeVoyage,
        loadVoyage,
        newVoyage,
        saveVoyage
    };

    enum class BuilderViewMode
    {
        isometric,
        firstPerson
    };

    enum class TopDownBuildMode
    {
        none,
        tetris,
        cellularAutomata
    };

    enum class PerformanceAgentMode
    {
        snakes,
        trains,
        orbiters,
        automata,
        ripple,
        sequencer
    };

    enum class PerformancePlacementMode
    {
        selectOnly,
        placeDisc,
        placeTrack
    };

    enum class SynthEngine
    {
        digitalV4,
        fmGlass,
        titleBloom,
        velvetNoise,
        chipPulse,
        guitarPluck
    };

    enum class ScaleType
    {
        chromatic,
        major,
        minor,
        dorian,
        pentatonic
    };

    enum class DrumMode
    {
        reactiveBreakbeat,
        rezStraight,
        tightPulse,
        forwardStep,
        railLine
    };

    enum class SnakeTriggerMode
    {
        headOnly,
        wholeBody
    };

    struct FirstPersonState
    {
        float x = 8.0f;
        float y = 8.0f;
        float eyeZ = 2.35f;
        float verticalVelocity = 0.0f;
        float yaw = 0.0f;
        float pitch = -0.08f;
    };

    struct IsometricCamera
    {
        int rotation = 0;
        float zoom = 3.168f;
        float heightScale = 1.0f;
        float panX = 0.0f;
        float panY = 0.0f;
    };

    struct TargetedVoxel
    {
        bool valid = false;
        int hitX = 0;
        int hitY = 0;
        int hitZ = 0;
        int placeX = 0;
        int placeY = 0;
        int placeZ = 0;
    };

    enum class TetrominoType
    {
        I,
        O,
        T,
        L,
        J,
        S,
        Z
    };

    struct TetrisPiece
    {
        TetrominoType type = TetrominoType::T;
        int rotation = 0;
        juce::Point<int> anchor;
        int z = 1;
        bool active = false;
    };

    struct PerformanceSnake
    {
        std::vector<juce::Point<int>> body;
        juce::Point<int> direction { 1, 0 };
        juce::Colour colour = juce::Colours::white;
        bool clockwise = true;
        int orbitIndex = 0;
    };

    struct PerformanceDisc
    {
        juce::Point<int> cell;
        juce::Point<int> direction { 1, 0 };
    };

    struct PerformanceTrack
    {
        juce::Point<int> cell;
        bool horizontal = true;
    };

    struct PerformanceRipple
    {
        juce::Point<int> centre;
        int radius = 0;
        int maxRadius = 6;
        juce::Colour colour = juce::Colours::white;
    };

    struct PerformanceSequencer
    {
        juce::Point<int> cell;
        juce::Point<int> direction { 1, 0 };
        juce::Point<int> previousCell;
        juce::Colour colour = juce::Colours::white;
        bool hasPreviousCell = false;
    };

    struct PerformanceFlash
    {
        juce::Point<int> cell;
        juce::Colour colour = juce::Colours::white;
        float life = 1.0f;
        bool pulse = false;
    };

    struct LogHeatmapHitTarget
    {
        juce::String planetId;
        juce::Rectangle<float> bounds;
    };

    struct PerformanceSelection
    {
        enum class Kind
        {
            none,
            disc,
            track
        };

        Kind kind = Kind::none;
        juce::Point<int> cell;

        bool isValid() const noexcept { return kind != Kind::none; }
    };

    struct PendingNoteOff
    {
        int note = 60;
        float secondsRemaining = 0.2f;
    };

    class WaveVoice final : public juce::SynthesiserVoice
    {
    public:
        explicit WaveVoice (SynthEngine& engineRef) : engine (engineRef) {}

        bool canPlaySound (juce::SynthesiserSound* s) override;
        void startNote (int midiNoteNumber, float velocity, juce::SynthesiserSound*, int) override;
        void stopNote (float velocity, bool allowTailOff) override;
        void pitchWheelMoved (int) override {}
        void controllerMoved (int, int) override {}
        void renderNextBlock (juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples) override;

    private:
        SynthEngine& engine;
        juce::ADSR adsr;
        juce::ADSR::Parameters adsrParams;
        float level = 0.0f;
        double currentAngle = 0.0;
        double angleDelta = 0.0;
        double modAngle = 0.0;
        double modDelta = 0.0;
        double subAngle = 0.0;
        double subDelta = 0.0;
        float noteAgeSeconds = 0.0f;
        uint32_t noiseSeed = 0u;
        float sampleHoldValue = 0.0f;
        int sampleHoldCounter = 0;
        int sampleHoldPeriod = 1;
        float lpState = 0.0f;
        float hpState = 0.0f;
        bool percussionMode = false;
        int percussionType = 0;
        float noiseLP = 0.0f;
        float noiseHP = 0.0f;
        float lastNoise = 0.0f;
        int chipSfxType = 0;
        std::vector<float> ksDelay;
        int ksIndex = 0;
        float ksLast = 0.0f;
    };

    class WaveSound final : public juce::SynthesiserSound
    {
    public:
        bool appliesToNote (int) override { return true; }
        bool appliesToChannel (int) override { return true; }
    };

    enum class IsometricChordType
    {
        single,
        power,
        majorTriad,
        minorTriad,
        sus2,
        sus4,
        majorSeventh,
        minorSeventh
    };

    GalaxyMetadata galaxy;
    PersistenceManager persistence;
    Scene currentScene = Scene::title;
    bool galaxyLogOpen = false;
    float galaxyLogScroll = 0.0f;
    juce::String expandedLogHeatmapPlanetId;
    std::vector<LogHeatmapHitTarget> logHeatmapHitTargets;
    int selectedSystemIndex = 0;
    int selectedPlanetIndex = 0;
    TitleAction hoveredTitleAction = TitleAction::none;
    bool titleResumeAvailable = false;
    std::unique_ptr<PlanetSurfaceState> activePlanetState;
    BuilderViewMode builderViewMode = BuilderViewMode::isometric;
    TopDownBuildMode topDownBuildMode = TopDownBuildMode::none;
    FirstPersonState firstPersonState;
    IsometricCamera isometricCamera;
    int builderCursorX = 0;
    int builderCursorY = 0;
    int builderLayer = 1;
    int isometricPlacementHeight = 1;
    IsometricChordType isometricChordType = IsometricChordType::single;
    int firstPersonPlacementOffset = 1;
    bool performanceMode = false;
    BuilderViewMode performanceEntryView = BuilderViewMode::isometric;
    TopDownBuildMode performanceEntryTopDownMode = TopDownBuildMode::none;
    TetrisPiece tetrisPiece;
    TetrominoType nextTetrisType = TetrominoType::L;
    int tetrisBuildLayer = 1;
    int tetrisGravityTick = 0;
    int tetrisGravityFrames = 8;
    int automataBuildLayer = 1;
    std::optional<juce::Point<int>> automataHoverCell;
    int performanceRegionMode = 2;
    int performanceAgentCount = 1;
    PerformanceAgentMode performanceAgentMode = PerformanceAgentMode::snakes;
    std::vector<PerformanceSnake> performanceSnakes;
    std::vector<PerformanceDisc> performanceDiscs;
    std::vector<PerformanceTrack> performanceTracks;
    std::vector<PerformanceRipple> performanceRipples;
    std::vector<PerformanceSequencer> performanceSequencers;
    std::vector<juce::Point<int>> performanceOrbitCenters;
    std::vector<juce::Point<int>> performanceAutomataCells;
    std::vector<PerformanceFlash> performanceFlashes;
    std::optional<juce::Point<int>> performanceHoverCell;
    juce::Point<int> performanceSelectedDirection { 1, 0 };
    bool performanceTrackHorizontal = true;
    PerformancePlacementMode performancePlacementMode = PerformancePlacementMode::selectOnly;
    PerformanceSelection performanceSelection;
    int performanceTick = 0;
    float performanceBeatEnergy = 0.0f;
    double performanceBpm = 168.0;
    double performanceStepAccumulator = 0.0;
    SnakeTriggerMode snakeTriggerMode = SnakeTriggerMode::headOnly;
    int performanceSynthIndex = 0;
    int performanceDrumIndex = 0;
    int performanceScaleIndex = 0;
    int performanceKeyRoot = 0;
    std::vector<int> performanceRecentHitNotes;
    int performanceImprovCounter = 0;
    int performanceLastImprovMidi = -1;
    bool performanceBeatMuted = true;
    double performanceSessionSeconds = 0.0;
    std::vector<int> performanceMovementHeat;
    std::vector<int> performanceTriggerHeat;
    std::vector<int> performanceNoteHeat;
    juce::Point<float> lastMousePosition;
    bool hasMouseAnchor = false;
    bool suppressNextMouseMove = false;
    bool firstPersonCursorCaptured = false;
    bool firstPersonJumpWasDown = false;
    double currentSampleRate = 44100.0;
    SynthEngine synthEngine = SynthEngine::digitalV4;
    DrumMode drumMode = DrumMode::reactiveBreakbeat;
    ScaleType scaleType = ScaleType::minor;
    juce::Synthesiser synth;
    juce::Synthesiser beatSynth;
    juce::CriticalSection synthLock;
    std::vector<PendingNoteOff> pendingNoteOffs;
    std::vector<PendingNoteOff> pendingBeatNoteOffs;
    double beatStepAccumulator = 0.0;
    int beatStepIndex = 0;
    int beatBarIndex = 0;
    double ambientStepAccumulator = 0.0;
    int ambientStepIndex = 0;
    float transportPhase = 0.0f;
    float transportRate = 0.2f;

    const StarSystemMetadata& getSelectedSystem() const;
    const PlanetMetadata& getSelectedPlanet() const;

    void setScene (Scene newScene);
    void moveSystemSelection (int delta);
    void movePlanetSelection (int delta);
    void landOnSelectedPlanet();
    void enterBuilder();
    void leavePlanet();
    void ensureActivePlanetLoaded();
    void saveActivePlanet();
    void updateMusicState();
    int getSurfaceWidth() const noexcept;
    int getSurfaceDepth() const noexcept;
    int getSurfaceHeight() const noexcept;
    int getTopSolidZAt (int x, int y) const noexcept;
    int getGroundZAt (int x, int y) const noexcept;
    int getHighestOccupiedZ() const noexcept;
    bool isWalkable (float x, float y, float eyeZ) const;
    juce::Point<int> rotateIsometricXY (int x, int y) const;
    juce::Point<float> getIsometricProjectionOffset (juce::Rectangle<float> area) const;
    juce::Point<float> projectIsometricPoint (int x, int y, int z, juce::Rectangle<float> area) const;
    int getIsometricGridLineStep() const;
    juce::Rectangle<int> getBuilderGridArea() const;
    juce::Rectangle<float> getHotbarBoundsForGridArea (juce::Rectangle<int> area) const;
    juce::Rectangle<float> getHotbarSlotBounds (juce::Rectangle<int> area, int blockType) const;
    juce::Rectangle<float> getPerformanceBoardBounds (juce::Rectangle<float> area) const;
    juce::Rectangle<int> getPerformanceRegionBounds() const noexcept;
    std::optional<juce::Point<int>> getPerformanceCellAtPosition (juce::Point<float> position, juce::Rectangle<float> area) const;
    void triggerPerformanceNotesAtCell (juce::Point<int> cell);
    void beginPerformanceLogSession();
    void flushPerformanceLogSession();
    void recordPerformanceMovementCell (juce::Point<int> cell, int amount = 1);
    void resetPerformanceState();
    void resetPerformanceAgents();
    void setPerformanceAgentCount (int count);
    void stepPerformanceAgents();
    void stepPerformanceSnakes();
    void stepPerformanceOrbiters();
    void stepPerformanceAutomata();
    void stepPerformanceRipples();
    void stepPerformanceSequencers();
    bool hasPerformanceTrackAt (juce::Point<int> cell) const noexcept;
    bool getPerformanceTrackHorizontalAt (juce::Point<int> cell) const noexcept;
    void drawPerformanceView (juce::Graphics& g, juce::Rectangle<float> area);
    void drawPerformanceSidebar (juce::Graphics& g, juce::Rectangle<float> area);
    juce::String getPerformanceAgentModeName() const;
    juce::String getPlanetPerformanceModeName (PlanetPerformanceMode mode) const;
    juce::String getPerformancePlacementModeName() const;
    juce::String getSnakeTriggerModeName() const;
    int getPerformanceSnakeLength() const noexcept;
    juce::String getPerformanceSynthName() const;
    juce::String getPerformanceDrumName() const;
    juce::String getPerformanceScaleName() const;
    juce::String getPerformanceKeyName() const;
    std::vector<int> getPerformanceScaleSteps() const;
    int quantizePerformanceMidi (int midi) const;
    int getPerformanceMidiForHeight (int z) const;
    void schedulePendingNoteOff (std::vector<PendingNoteOff>& queue, int midiNote, float lengthSeconds);
    void addPerformanceImprovResponse (const std::vector<int>& hitNotes);
    void applyPerformanceEntryDefaults();
    int getAmbientRootMidi() const;
    std::vector<int> getAmbientChordMidiNotes() const;
    void addBeatEvent (juce::MidiBuffer& buffer, int midiNote, float velocity, int sampleOffset, int blockSamples);
    void applyPerformancePresetForPlanet();
    bool updateIsometricCursorFromPosition (juce::Point<float> position);
    void moveIsometricCursor (int dx, int dy, int dz);
    void applyIsometricPlacement (bool filled);
    void clearPlanetSurface();
    std::vector<int> getIsometricChordIntervals() const;
    std::vector<int> getActiveChordStackIntervals() const;
    juce::String getIsometricChordName() const;
    juce::String getTopDownBuildModeName() const;
    juce::String getPlanetBuildModeName (PlanetBuildMode mode) const;
    void applyAssignedBuildModeForPlanet();
    juce::String getTetrominoTypeName (TetrominoType type) const;
    std::array<juce::Point<int>, 4> getTetrominoOffsets (TetrominoType type, int rotation) const;
    std::vector<juce::Point<int>> getTetrisPlacementCells (const TetrisPiece& piece) const;
    bool tetrisPieceFits (const TetrisPiece& piece) const;
    bool tetrisPieceCollidesWithVoxels (const TetrisPiece& piece) const;
    void clampTetrisPieceToSurface (TetrisPiece& piece) const;
    TetrominoType getRandomTetrominoType() const;
    void spawnTetrisPiece (bool randomizeType);
    void moveTetrisPiece (int dx, int dy, int dz);
    void rotateTetrisPiece();
    void placeTetrisPiece (bool filled);
    void softDropTetrisPiece();
    void hardDropTetrisPiece();
    void advanceTetrisLayer();
    int getAutomataNeighbourCount (int x, int y, int z) const;
    void toggleAutomataCell (juce::Point<int> cell, bool filled);
    void randomiseAutomataSeed();
    void advanceAutomataLayer();
    void placeFirstPersonAtSafeSpawn();
    void syncCursorToFirstPersonTarget();
    TargetedVoxel findFirstPersonTarget() const;
    void applyFirstPersonAction (bool placeBlock);
    void updateFirstPersonMouseCapture();
    void recenterFirstPersonMouse();
    juce::String getSceneTitle() const;
    juce::String getBuilderViewName() const;
    juce::Rectangle<float> titleCardBounds (juce::Rectangle<float> area) const;
    juce::Rectangle<float> titleButtonBounds (juce::Rectangle<float> area, int index) const;
    TitleAction titleActionAt (juce::Point<float> position, juce::Rectangle<float> area) const;
    juce::String titleActionLabel (TitleAction action) const;
    bool isTitleActionEnabled (TitleAction action) const;
    void enterGalaxyFromTitle (bool regenerateGalaxy);
    void drawHeader (juce::Graphics& g, juce::Rectangle<int> area);
    void drawTitleScene (juce::Graphics& g, juce::Rectangle<int> area);
    void drawGalaxyScene (juce::Graphics& g, juce::Rectangle<int> area);
    void drawGalaxyLogbook (juce::Graphics& g, juce::Rectangle<int> area);
    float getGalaxyLogMaxScroll (juce::Rectangle<int> area);
    void drawLandingScene (juce::Graphics& g, juce::Rectangle<int> area);
    void drawBuilderScene (juce::Graphics& g, juce::Rectangle<int> area);
    void drawHotbar (juce::Graphics& g, juce::Rectangle<int> area);
    void drawIsometricBuilder (juce::Graphics& g, juce::Rectangle<int> area);
    void drawTetrisBuildView (juce::Graphics& g, juce::Rectangle<float> area);
    void drawAutomataBuildView (juce::Graphics& g, juce::Rectangle<float> area);
    void drawFirstPersonBuilder (juce::Graphics& g, juce::Rectangle<int> area);
    void drawTexturedCube (juce::Graphics& g, juce::Point<float> origin, float halfWidth, float halfHeight, int blockType, bool selected) const;
    void fillTexturedDiamond (juce::Graphics& g, const juce::Path& path, int blockType, int zLayer, float brightness, bool topFace) const;
    void fillTexturedRect (juce::Graphics& g, juce::Rectangle<float> area, int blockType, int zLayer, float brightness, bool topFace) const;
    juce::Colour getBlockColour (int blockType) const;
    juce::Colour getNoteColourForLayer (int zLayer) const;
    juce::String getNoteNameForLayer (int zLayer) const;
    void timerCallback() override;
};
