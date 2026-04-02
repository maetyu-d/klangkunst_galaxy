#include "GameModel.h"

namespace
{
constexpr const char* syllables[] = { "ka", "ly", "or", "ve", "na", "tu", "zen", "phi", "dra", "sol", "ae", "mir" };
constexpr int syllableCount = static_cast<int> (std::size (syllables));

int normalisePlanetFootprint (int width, int depth)
{
    static constexpr std::array<int, 4> allowed { 12, 16, 20, 32 };

    auto nearestAllowed = [&] (int value)
    {
        int best = allowed.front();
        int bestDistance = std::abs (value - best);
        for (auto candidate : allowed)
        {
            const int distance = std::abs (value - candidate);
            if (distance < bestDistance)
            {
                best = candidate;
                bestDistance = distance;
            }
        }
        return best;
    };

    const bool widthAllowed = std::find (allowed.begin(), allowed.end(), width) != allowed.end();
    const bool depthAllowed = std::find (allowed.begin(), allowed.end(), depth) != allowed.end();
    if (widthAllowed && depthAllowed && width == depth)
        return width;

    const int seedValue = juce::jmax (width, depth);
    return nearestAllowed (seedValue > 0 ? seedValue : 16);
}

bool isLegalPlanetFootprint (int width, int depth)
{
    return width == depth && (width == 12 || width == 16 || width == 20 || width == 32);
}

juce::String makeName (juce::Random& random, int minParts, int maxParts)
{
    juce::String name;
    const auto count = random.nextInt ({ minParts, maxParts + 1 });

    for (int i = 0; i < count; ++i)
        name += syllables[random.nextInt (syllableCount)];

    return name.substring (0, 1).toUpperCase() + name.substring (1);
}

juce::Colour colourFromSeed (int seed, float saturation, float brightness)
{
    juce::Random random (seed);
    return juce::Colour::fromHSV (random.nextFloat(), saturation, brightness, 1.0f);
}
}

int PlanetSurfaceState::getIndex (int x, int y, int z) const noexcept
{
    return x + (y * maxWidth) + (z * maxWidth * maxDepth);
}

int PlanetSurfaceState::getBlock (int x, int y, int z) const noexcept
{
    if (! juce::isPositiveAndBelow (x, width)
        || ! juce::isPositiveAndBelow (y, depth)
        || ! juce::isPositiveAndBelow (z, height))
        return 0;

    return blocks[static_cast<size_t> (getIndex (x, y, z))];
}

void PlanetSurfaceState::setBlock (int x, int y, int z, int value) noexcept
{
    if (! juce::isPositiveAndBelow (x, width)
        || ! juce::isPositiveAndBelow (y, depth)
        || ! juce::isPositiveAndBelow (z, height))
        return;

    blocks[static_cast<size_t> (getIndex (x, y, z))] = value;
}

int PlanetSurfaceState::getTopBlock (int x, int y, int maxLayer) const noexcept
{
    for (int z = juce::jlimit (0, height - 1, maxLayer); z >= 0; --z)
        if (const auto block = getBlock (x, y, z); block != 0)
            return block;

    return 0;
}

PersistenceManager::PersistenceManager()
{
    auto baseDir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                       .getChildFile ("KlangKunstGalaxy");
    baseDir.createDirectory();
    saveFile = baseDir.getChildFile ("planet_persistence.json");
}

std::unique_ptr<PlanetSurfaceState> PersistenceManager::loadPlanet (const juce::String& planetId)
{
    ensureLoaded();

    if (auto* planets = getPlanetsObject())
        if (planets->hasProperty (planetId))
            return deserialiseState (planetId, planets->getProperty (planetId));

    return {};
}

void PersistenceManager::savePlanet (const PlanetSurfaceState& state)
{
    ensureLoaded();

    if (auto* planets = getPlanetsObject())
    {
        planets->setProperty (state.planetId, serialiseState (state));
        saveFile.replaceWithText (juce::JSON::toString (rootData, true));
    }
}

void PersistenceManager::ensureLoaded()
{
    if (! rootData.isVoid())
        return;

    if (saveFile.existsAsFile())
        rootData = juce::JSON::parse (saveFile);

    if (! rootData.isObject())
        rootData = juce::var (new juce::DynamicObject());

    if (auto* object = rootData.getDynamicObject(); object != nullptr && ! object->hasProperty ("planets"))
        object->setProperty ("planets", juce::var (new juce::DynamicObject()));
}

juce::DynamicObject* PersistenceManager::getPlanetsObject()
{
    if (auto* object = rootData.getDynamicObject())
        return object->getProperty ("planets").getDynamicObject();

    return nullptr;
}

juce::var PersistenceManager::serialiseState (const PlanetSurfaceState& state)
{
    auto* object = new juce::DynamicObject();
    object->setProperty ("seed", state.seed);
    object->setProperty ("width", state.width);
    object->setProperty ("depth", state.depth);
    object->setProperty ("skyColour", state.skyColour.toDisplayString (true));
    object->setProperty ("fogColour", state.fogColour.toDisplayString (true));

    juce::Array<juce::var> blockData;
    blockData.ensureStorageAllocated (static_cast<int> (state.blocks.size()));

    for (auto block : state.blocks)
        blockData.add (block);

    object->setProperty ("blocks", juce::var (blockData));
    return juce::var (object);
}

std::unique_ptr<PlanetSurfaceState> PersistenceManager::deserialiseState (const juce::String& id, const juce::var& data)
{
    auto* object = data.getDynamicObject();
    if (object == nullptr)
        return {};

    auto state = std::make_unique<PlanetSurfaceState>();
    state->planetId = id;
    state->seed = static_cast<int> (object->getProperty ("seed"));
    const int storedWidth = static_cast<int> (object->getProperty ("width"));
    const int storedDepth = static_cast<int> (object->getProperty ("depth"));
    const int normalisedFootprint = normalisePlanetFootprint (storedWidth, storedDepth);
    state->width = normalisedFootprint;
    state->depth = normalisedFootprint;
    state->skyColour = juce::Colour::fromString (object->getProperty ("skyColour").toString());
    state->fogColour = juce::Colour::fromString (object->getProperty ("fogColour").toString());

    if (const auto* blockArray = object->getProperty ("blocks").getArray(); blockArray != nullptr)
    {
        const auto count = juce::jmin (static_cast<int> (state->blocks.size()), blockArray->size());
        for (int i = 0; i < count; ++i)
            state->blocks[static_cast<size_t> (i)] = static_cast<int> (blockArray->getReference (i));
    }

    for (int z = 0; z < PlanetSurfaceState::height; ++z)
        for (int y = 0; y < PlanetSurfaceState::maxDepth; ++y)
            for (int x = 0; x < PlanetSurfaceState::maxWidth; ++x)
                if (x >= state->width || y >= state->depth)
                    state->blocks[static_cast<size_t> (state->getIndex (x, y, z))] = 0;

    for (int y = 0; y < state->depth; ++y)
        for (int x = 0; x < state->width; ++x)
            if (state->getBlock (x, y, 0) == 0)
                state->setBlock (x, y, 0, 1);

    return state;
}

GalaxyMetadata GalaxyGenerator::generateGalaxy (int seed)
{
    juce::Random random (seed);
    GalaxyMetadata galaxy;
    galaxy.seed = seed;
    galaxy.name = makeName (random, 2, 3) + " Galaxy";

    const auto systemCount = 18;
    for (int systemIndex = 0; systemIndex < systemCount; ++systemIndex)
    {
        auto* system = galaxy.systems.add (new StarSystemMetadata());
        system->seed = random.nextInt();
        system->id = "system_" + juce::String (systemIndex);
        system->name = makeName (random, 2, 3);

        const auto angle = juce::MathConstants<float>::twoPi * (static_cast<float> (systemIndex) / static_cast<float> (systemCount));
        const auto radius = 0.18f + random.nextFloat() * 0.72f;
        system->galaxyPosition = { 0.5f + std::cos (angle) * radius * 0.45f, 0.5f + std::sin (angle) * radius * 0.35f };

        juce::Random systemRandom (system->seed);
        const auto planetCount = systemRandom.nextInt ({ 3, 8 });

        for (int planetIndex = 0; planetIndex < planetCount; ++planetIndex)
        {
            auto* planet = system->planets.add (new PlanetMetadata());
            planet->seed = systemRandom.nextInt();
            planet->orbitIndex = planetIndex;
            planet->id = system->id + "_planet_" + juce::String (planetIndex);
            planet->name = makeName (systemRandom, 2, 3);
            planet->musicalRootHz = 110.0f + systemRandom.nextFloat() * 330.0f;
            planet->energy = systemRandom.nextFloat();
            planet->water = systemRandom.nextFloat();
            planet->atmosphere = systemRandom.nextFloat();
            planet->accent = colourFromSeed (planet->seed, 0.55f + 0.25f * planet->water, 0.65f + 0.15f * planet->energy);
        }
    }

    return galaxy;
}

PlanetSurfaceState GalaxyGenerator::generateSurface (const PlanetMetadata& planet)
{
    PlanetSurfaceState state;
    state.planetId = planet.id;
    state.seed = planet.seed;
    juce::Random sizeRandom (planet.seed ^ 0x51A7E123);
    const float roll = sizeRandom.nextFloat();
    if (roll < 0.10f)
        state.width = state.depth = 12;
    else if (roll < 0.60f)
        state.width = state.depth = 16;
    else if (roll < 0.80f)
        state.width = state.depth = 20;
    else
        state.width = state.depth = 32;
    state.skyColour = planet.accent.interpolatedWith (juce::Colours::black, 0.35f);
    state.fogColour = planet.accent.interpolatedWith (juce::Colours::white, 0.4f);

    for (int y = 0; y < state.depth; ++y)
    {
        for (int x = 0; x < state.width; ++x)
            state.setBlock (x, y, 0, 1);
    }

    return state;
}
