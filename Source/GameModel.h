#pragma once

#include <JuceHeader.h>
#include <array>

enum class PlanetBuildMode
{
    isometric,
    firstPerson,
    cellularAutomata,
    tetris
};

enum class PlanetPerformanceMode
{
    snakes,
    trains,
    ripple,
    sequencer
};

struct PlanetMetadata
{
    juce::String id;
    juce::String name;
    int seed = 0;
    int orbitIndex = 0;
    float musicalRootHz = 220.0f;
    float energy = 0.0f;
    float water = 0.0f;
    float atmosphere = 0.0f;
    PlanetBuildMode assignedBuildMode = PlanetBuildMode::isometric;
    PlanetPerformanceMode assignedPerformanceMode = PlanetPerformanceMode::snakes;
    juce::Colour accent = juce::Colours::white;
};

struct StarSystemMetadata
{
    juce::String id;
    juce::String name;
    int seed = 0;
    juce::Point<float> galaxyPosition;
    juce::OwnedArray<PlanetMetadata> planets;
};

struct GalaxyMetadata
{
    juce::String name;
    int seed = 0;
    juce::OwnedArray<StarSystemMetadata> systems;
};

struct VisitLogEntry
{
    juce::String planetId;
    juce::String planetName;
    juce::String systemId;
    juce::String systemName;
    int planetSeed = 0;
    int systemSeed = 0;
    int orbitIndex = 0;
    float musicalRootHz = 0.0f;
    float energy = 0.0f;
    float water = 0.0f;
    float atmosphere = 0.0f;
    PlanetBuildMode assignedBuildMode = PlanetBuildMode::isometric;
    PlanetPerformanceMode assignedPerformanceMode = PlanetPerformanceMode::snakes;
    juce::Colour accent = juce::Colours::white;
    juce::String firstVisitedUtc;
    juce::String lastVisitedUtc;
    int visitCount = 0;
    int performanceWidth = 0;
    int performanceDepth = 0;
    double performanceSeconds = 0.0;
    int performanceSessions = 0;
    std::vector<int> performanceMovementHeat;
    std::vector<int> performanceTriggerHeat;
    std::vector<int> performanceNoteHeat;
};

struct PlanetSurfaceState
{
    static constexpr int maxWidth = 32;
    static constexpr int maxDepth = 32;
    static constexpr int height = 32;

    juce::String planetId;
    int seed = 0;
    int width = 16;
    int depth = 16;
    juce::Colour skyColour = juce::Colours::black;
    juce::Colour fogColour = juce::Colours::black;
    std::array<int, maxWidth * maxDepth * height> blocks {};

    int getIndex (int x, int y, int z) const noexcept;
    int getBlock (int x, int y, int z) const noexcept;
    void setBlock (int x, int y, int z, int value) noexcept;
    int getTopBlock (int x, int y, int maxLayer) const noexcept;
};

class PersistenceManager
{
public:
    PersistenceManager();

    std::unique_ptr<PlanetSurfaceState> loadPlanet (const juce::String& planetId);
    void savePlanet (const PlanetSurfaceState& state);
    void recordPlanetVisit (const StarSystemMetadata& system, const PlanetMetadata& planet);
    void recordPerformanceSnapshot (const StarSystemMetadata& system, const PlanetMetadata& planet,
                                    int width, int depth, double seconds,
                                    const std::vector<int>& movementHeat,
                                    const std::vector<int>& triggerHeat,
                                    const std::vector<int>& noteHeat);
    std::vector<VisitLogEntry> getVisitLog();

private:
    juce::File saveFile;
    juce::var rootData;

    void ensureLoaded();
    juce::DynamicObject* getPlanetsObject();
    juce::Array<juce::var>* getVisitLogArray();
    static juce::var serialiseState (const PlanetSurfaceState& state);
    static std::unique_ptr<PlanetSurfaceState> deserialiseState (const juce::String& id, const juce::var& data);
};

class GalaxyGenerator
{
public:
    static GalaxyMetadata generateGalaxy (int seed);
    static PlanetSurfaceState generateSurface (const PlanetMetadata& planet);
};
