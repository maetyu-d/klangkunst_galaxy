#pragma once

#include <JuceHeader.h>
#include <array>

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

private:
    juce::File saveFile;
    juce::var rootData;

    void ensureLoaded();
    juce::DynamicObject* getPlanetsObject();
    static juce::var serialiseState (const PlanetSurfaceState& state);
    static std::unique_ptr<PlanetSurfaceState> deserialiseState (const juce::String& id, const juce::var& data);
};

class GalaxyGenerator
{
public:
    static GalaxyMetadata generateGalaxy (int seed);
    static PlanetSurfaceState generateSurface (const PlanetMetadata& planet);
};
