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

juce::String buildModeToString (PlanetBuildMode mode)
{
    switch (mode)
    {
        case PlanetBuildMode::isometric: return "isometric";
        case PlanetBuildMode::firstPerson: return "firstPerson";
        case PlanetBuildMode::cellularAutomata: return "cellularAutomata";
        case PlanetBuildMode::tetris: return "tetris";
    }

    return "isometric";
}

PlanetBuildMode buildModeFromString (const juce::String& text)
{
    if (text == "firstPerson")
        return PlanetBuildMode::firstPerson;
    if (text == "cellularAutomata")
        return PlanetBuildMode::cellularAutomata;
    if (text == "tetris")
        return PlanetBuildMode::tetris;
    return PlanetBuildMode::isometric;
}

juce::String performanceModeToString (PlanetPerformanceMode mode)
{
    switch (mode)
    {
        case PlanetPerformanceMode::snakes: return "snakes";
        case PlanetPerformanceMode::trains: return "trains";
        case PlanetPerformanceMode::ripple: return "ripple";
        case PlanetPerformanceMode::sequencer: return "beam";
        case PlanetPerformanceMode::tenori: return "tenori";
    }

    return "snakes";
}

PlanetPerformanceMode performanceModeFromString (const juce::String& text)
{
    if (text == "trains")
        return PlanetPerformanceMode::trains;
    if (text == "ripple")
        return PlanetPerformanceMode::ripple;
    if (text == "beam" || text == "sequencer")
        return PlanetPerformanceMode::sequencer;
    if (text == "tenori")
        return PlanetPerformanceMode::tenori;
    return PlanetPerformanceMode::snakes;
}

juce::var serialiseIntVector (const std::vector<int>& values)
{
    juce::Array<juce::var> array;
    array.ensureStorageAllocated (static_cast<int> (values.size()));
    for (const auto value : values)
        array.add (value);
    return juce::var (array);
}

std::vector<int> deserialiseIntVector (const juce::var& value)
{
    std::vector<int> result;
    if (const auto* array = value.getArray(); array != nullptr)
    {
        result.reserve (static_cast<size_t> (array->size()));
        for (const auto& item : *array)
            result.push_back (static_cast<int> (item));
    }
    return result;
}

juce::String normaliseSlotKey (juce::String name)
{
    name = name.trim();
    if (name.isEmpty())
        name = "Voyage";

    juce::String key;
    key.preallocateBytes (name.getNumBytesAsUTF8());

    for (auto ch : name)
    {
        if (juce::CharacterFunctions::isLetterOrDigit (ch))
            key << juce::CharacterFunctions::toLowerCase (ch);
        else if (ch == ' ' || ch == '-' || ch == '_')
            key << '-';
    }

    while (key.contains ("--"))
        key = key.replace ("--", "-");

    key = key.trimCharactersAtStart ("-").trimCharactersAtEnd ("-");
    return key.isEmpty() ? "voyage" : key;
}

juce::var deepCloneVar (const juce::var& value)
{
    return juce::JSON::parse (juce::JSON::toString (value, true));
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
    baseDir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                  .getChildFile ("KlangKunstGalaxy");
    baseDir.createDirectory();
    slotsDir = baseDir.getChildFile ("slots");
    slotsDir.createDirectory();
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
        writeWorkingData();
    }
}

void PersistenceManager::recordPlanetVisit (const StarSystemMetadata& system, const PlanetMetadata& planet)
{
    ensureLoaded();

    auto* visitLog = getVisitLogArray();
    if (visitLog == nullptr)
        return;

    const auto nowIso = juce::Time::getCurrentTime().toISO8601 (true);

    for (auto& entryVar : *visitLog)
    {
        if (auto* entry = entryVar.getDynamicObject(); entry != nullptr)
        {
            if (entry->getProperty ("planetId").toString() != planet.id)
                continue;

            entry->setProperty ("planetName", planet.name);
            entry->setProperty ("systemId", system.id);
            entry->setProperty ("systemName", system.name);
            entry->setProperty ("planetSeed", planet.seed);
            entry->setProperty ("systemSeed", system.seed);
            entry->setProperty ("orbitIndex", planet.orbitIndex);
            entry->setProperty ("musicalRootHz", planet.musicalRootHz);
            entry->setProperty ("energy", planet.energy);
            entry->setProperty ("water", planet.water);
            entry->setProperty ("atmosphere", planet.atmosphere);
            entry->setProperty ("assignedBuildMode", buildModeToString (planet.assignedBuildMode));
            entry->setProperty ("assignedPerformanceMode", performanceModeToString (planet.assignedPerformanceMode));
            entry->setProperty ("accent", planet.accent.toDisplayString (true));
            entry->setProperty ("lastVisitedUtc", nowIso);
            entry->setProperty ("visitCount", static_cast<int> (entry->getProperty ("visitCount")) + 1);
            writeWorkingData();
            return;
        }
    }

    auto* entry = new juce::DynamicObject();
    entry->setProperty ("planetId", planet.id);
    entry->setProperty ("planetName", planet.name);
    entry->setProperty ("systemId", system.id);
    entry->setProperty ("systemName", system.name);
    entry->setProperty ("planetSeed", planet.seed);
    entry->setProperty ("systemSeed", system.seed);
    entry->setProperty ("orbitIndex", planet.orbitIndex);
    entry->setProperty ("musicalRootHz", planet.musicalRootHz);
    entry->setProperty ("energy", planet.energy);
    entry->setProperty ("water", planet.water);
    entry->setProperty ("atmosphere", planet.atmosphere);
    entry->setProperty ("assignedBuildMode", buildModeToString (planet.assignedBuildMode));
    entry->setProperty ("assignedPerformanceMode", performanceModeToString (planet.assignedPerformanceMode));
    entry->setProperty ("accent", planet.accent.toDisplayString (true));
    entry->setProperty ("firstVisitedUtc", nowIso);
    entry->setProperty ("lastVisitedUtc", nowIso);
    entry->setProperty ("visitCount", 1);
    visitLog->add (juce::var (entry));
    writeWorkingData();
}

void PersistenceManager::recordPerformanceSnapshot (const StarSystemMetadata& system, const PlanetMetadata& planet,
                                                    int width, int depth, double seconds,
                                                    const std::vector<int>& movementHeat,
                                                    const std::vector<int>& triggerHeat,
                                                    const std::vector<int>& noteHeat)
{
    ensureLoaded();

    auto* visitLog = getVisitLogArray();
    if (visitLog == nullptr)
        return;

    const auto nowIso = juce::Time::getCurrentTime().toISO8601 (true);

    for (auto& entryVar : *visitLog)
    {
        if (auto* entry = entryVar.getDynamicObject(); entry != nullptr)
        {
            if (entry->getProperty ("planetId").toString() != planet.id)
                continue;

            entry->setProperty ("planetName", planet.name);
            entry->setProperty ("systemId", system.id);
            entry->setProperty ("systemName", system.name);
            entry->setProperty ("lastVisitedUtc", nowIso);
            entry->setProperty ("performanceWidth", width);
            entry->setProperty ("performanceDepth", depth);
            entry->setProperty ("performanceSeconds", static_cast<double> (entry->getProperty ("performanceSeconds")) + seconds);
            entry->setProperty ("performanceSessions", static_cast<int> (entry->getProperty ("performanceSessions")) + 1);

            auto mergeVectorProperty = [&] (const juce::Identifier& name, const std::vector<int>& incoming)
            {
                auto existing = deserialiseIntVector (entry->getProperty (name));
                if (existing.size() < incoming.size())
                    existing.resize (incoming.size(), 0);

                for (size_t i = 0; i < incoming.size(); ++i)
                    existing[i] += incoming[i];

                entry->setProperty (name, serialiseIntVector (existing));
            };

            mergeVectorProperty ("performanceMovementHeat", movementHeat);
            mergeVectorProperty ("performanceTriggerHeat", triggerHeat);
            mergeVectorProperty ("performanceNoteHeat", noteHeat);

            writeWorkingData();
            return;
        }
    }
}

std::vector<VisitLogEntry> PersistenceManager::getVisitLog()
{
    ensureLoaded();
    std::vector<VisitLogEntry> entries;

    if (auto* visitLog = getVisitLogArray())
    {
        entries.reserve (static_cast<size_t> (visitLog->size()));
        for (auto& entryVar : *visitLog)
        {
            if (auto* object = entryVar.getDynamicObject(); object != nullptr)
            {
                VisitLogEntry entry;
                entry.planetId = object->getProperty ("planetId").toString();
                entry.planetName = object->getProperty ("planetName").toString();
                entry.systemId = object->getProperty ("systemId").toString();
                entry.systemName = object->getProperty ("systemName").toString();
                entry.planetSeed = static_cast<int> (object->getProperty ("planetSeed"));
                entry.systemSeed = static_cast<int> (object->getProperty ("systemSeed"));
                entry.orbitIndex = static_cast<int> (object->getProperty ("orbitIndex"));
                entry.musicalRootHz = static_cast<float> (double (object->getProperty ("musicalRootHz")));
                entry.energy = static_cast<float> (double (object->getProperty ("energy")));
                entry.water = static_cast<float> (double (object->getProperty ("water")));
                entry.atmosphere = static_cast<float> (double (object->getProperty ("atmosphere")));
                entry.assignedBuildMode = buildModeFromString (object->getProperty ("assignedBuildMode").toString());
                entry.assignedPerformanceMode = performanceModeFromString (object->getProperty ("assignedPerformanceMode").toString());
                entry.accent = juce::Colour::fromString (object->getProperty ("accent").toString());
                entry.firstVisitedUtc = object->getProperty ("firstVisitedUtc").toString();
                entry.lastVisitedUtc = object->getProperty ("lastVisitedUtc").toString();
                entry.visitCount = static_cast<int> (object->getProperty ("visitCount"));
                entry.performanceWidth = static_cast<int> (object->getProperty ("performanceWidth"));
                entry.performanceDepth = static_cast<int> (object->getProperty ("performanceDepth"));
                entry.performanceSeconds = static_cast<double> (object->getProperty ("performanceSeconds"));
                entry.performanceSessions = static_cast<int> (object->getProperty ("performanceSessions"));
                entry.performanceMovementHeat = deserialiseIntVector (object->getProperty ("performanceMovementHeat"));
                entry.performanceTriggerHeat = deserialiseIntVector (object->getProperty ("performanceTriggerHeat"));
                entry.performanceNoteHeat = deserialiseIntVector (object->getProperty ("performanceNoteHeat"));
                entries.push_back (std::move (entry));
            }
        }
    }

    std::sort (entries.begin(), entries.end(), [] (const VisitLogEntry& a, const VisitLogEntry& b)
    {
        return a.lastVisitedUtc > b.lastVisitedUtc;
    });

    return entries;
}

std::vector<SaveSlotSummary> PersistenceManager::getSaveSlots()
{
    std::vector<SaveSlotSummary> slots;

    if (! slotsDir.isDirectory())
        slotsDir.createDirectory();

    juce::Array<juce::File> files;
    slotsDir.findChildFiles (files, juce::File::findFiles, false, "*.drd");

    slots.reserve (static_cast<size_t> (files.size()));
    for (const auto& file : files)
    {
        const auto data = juce::JSON::parse (file);
        auto* object = data.getDynamicObject();
        if (object == nullptr)
            continue;

        SaveSlotSummary slot;
        slot.slotKey = object->getProperty ("slotKey").toString();
        slot.slotName = object->getProperty ("slotName").toString();
        slot.fileName = file.getFileName();
        slot.savedUtc = object->getProperty ("savedUtc").toString();
        slot.isAutosave = static_cast<bool> (object->getProperty ("isAutosave"));

        if (const auto* galaxyObject = object->getProperty ("galaxy").getDynamicObject(); galaxyObject != nullptr)
        {
            slot.galaxyName = galaxyObject->getProperty ("name").toString();
            slot.galaxySeed = static_cast<int> (galaxyObject->getProperty ("seed"));
            if (const auto* systems = galaxyObject->getProperty ("systems").getArray(); systems != nullptr)
                slot.systemCount = systems->size();
        }

        if (const auto* persistenceObject = object->getProperty ("persistence").getDynamicObject(); persistenceObject != nullptr)
        {
            if (const auto* visitLog = persistenceObject->getProperty ("visitLog").getArray(); visitLog != nullptr)
                slot.visitedPlanets = visitLog->size();
        }

        if (slot.slotName.isEmpty())
            slot.slotName = file.getFileNameWithoutExtension();
        if (slot.slotKey.isEmpty())
            slot.slotKey = normaliseSlotKey (slot.slotName);
        if (slot.slotKey == "recovery-autosave" || slot.isAutosave)
            continue;

        slots.push_back (std::move (slot));
    }

    std::sort (slots.begin(), slots.end(), [] (const SaveSlotSummary& a, const SaveSlotSummary& b)
    {
        return a.savedUtc > b.savedUtc;
    });

    return slots;
}

bool PersistenceManager::saveVoyageSlot (const juce::String& slotName, const GalaxyMetadata& galaxy, const juce::var& sessionState)
{
    ensureLoaded();
    slotsDir.createDirectory();

    const auto cleanedName = slotName.trim().isEmpty() ? "Voyage" : slotName.trim();
    const auto slotKey = normaliseSlotKey (cleanedName);
    const auto slotFile = slotsDir.getChildFile (slotKey + ".drd");

    return writeVoyageSlotFile (slotFile, slotKey, cleanedName, false, galaxy, sessionState);
}

bool PersistenceManager::saveRecoveryVoyage (const GalaxyMetadata& galaxy, const juce::var& sessionState)
{
    ensureLoaded();
    slotsDir.createDirectory();
    const auto slotKey = juce::String ("recovery-autosave");
    const auto slotFile = slotsDir.getChildFile (slotKey + ".drd");
    return writeVoyageSlotFile (slotFile, slotKey, "Recovery Autosave", true, galaxy, sessionState);
}

bool PersistenceManager::writeVoyageSlotFile (const juce::File& slotFile, const juce::String& slotKey, const juce::String& slotName,
                                              bool isAutosave, const GalaxyMetadata& galaxy, const juce::var& sessionState)
{
    auto* object = new juce::DynamicObject();
    object->setProperty ("fileType", "KlangKunstGalaxyVoyage");
    object->setProperty ("formatVersion", 1);
    object->setProperty ("slotKey", slotKey);
    object->setProperty ("slotName", slotName.trim().isEmpty() ? "Voyage" : slotName.trim());
    object->setProperty ("savedUtc", juce::Time::getCurrentTime().toISO8601 (true));
    object->setProperty ("isAutosave", isAutosave);
    object->setProperty ("galaxy", serialiseGalaxy (galaxy));
    object->setProperty ("session", deepCloneVar (sessionState));
    object->setProperty ("persistence", deepCloneVar (rootData));

    return slotFile.replaceWithText (juce::JSON::toString (juce::var (object), true));
}

bool PersistenceManager::loadVoyageSlot (const juce::String& slotKey, GalaxyMetadata& galaxy, juce::var& sessionState)
{
    slotsDir.createDirectory();
    const auto slotFile = slotsDir.getChildFile (normaliseSlotKey (slotKey) + ".drd");
    if (! slotFile.existsAsFile())
        return false;

    const auto data = juce::JSON::parse (slotFile);
    auto* object = data.getDynamicObject();
    if (object == nullptr)
        return false;

    galaxy = deserialiseGalaxy (object->getProperty ("galaxy"));
    sessionState = deepCloneVar (object->getProperty ("session"));
    rootData = deepCloneVar (object->getProperty ("persistence"));
    if (! rootData.isObject())
        rootData = juce::var (new juce::DynamicObject());
    ensureLoaded();
    writeWorkingData();
    return true;
}

bool PersistenceManager::deleteVoyageSlot (const juce::String& slotKey)
{
    slotsDir.createDirectory();
    const auto slotFile = slotsDir.getChildFile (normaliseSlotKey (slotKey) + ".drd");
    return ! slotFile.existsAsFile() || slotFile.deleteFile();
}

bool PersistenceManager::renameVoyageSlot (const juce::String& slotKey, const juce::String& newSlotName)
{
    slotsDir.createDirectory();
    const auto currentKey = normaliseSlotKey (slotKey);
    const auto nextKey = normaliseSlotKey (newSlotName);
    if (nextKey.isEmpty())
        return false;

    const auto sourceFile = slotsDir.getChildFile (currentKey + ".drd");
    if (! sourceFile.existsAsFile())
        return false;

    auto data = juce::JSON::parse (sourceFile);
    auto* object = data.getDynamicObject();
    if (object == nullptr)
        return false;

    object->setProperty ("slotName", newSlotName.trim().isEmpty() ? "Voyage" : newSlotName.trim());
    object->setProperty ("slotKey", nextKey);

    const auto targetFile = slotsDir.getChildFile (nextKey + ".drd");
    if (currentKey == nextKey)
        return sourceFile.replaceWithText (juce::JSON::toString (data, true));

    if (targetFile.existsAsFile())
        return false;

    if (! targetFile.replaceWithText (juce::JSON::toString (data, true)))
        return false;

    return sourceFile.deleteFile();
}

bool PersistenceManager::getRecoverySlotSummary (SaveSlotSummary& summary)
{
    slotsDir.createDirectory();
    const auto slotFile = slotsDir.getChildFile ("recovery-autosave.drd");
    if (! slotFile.existsAsFile())
        return false;

    const auto data = juce::JSON::parse (slotFile);
    auto* object = data.getDynamicObject();
    if (object == nullptr)
        return false;

    summary = {};
    summary.slotKey = object->getProperty ("slotKey").toString();
    summary.slotName = object->getProperty ("slotName").toString();
    summary.fileName = slotFile.getFileName();
    summary.savedUtc = object->getProperty ("savedUtc").toString();
    summary.isAutosave = static_cast<bool> (object->getProperty ("isAutosave"));

    if (const auto* galaxyObject = object->getProperty ("galaxy").getDynamicObject(); galaxyObject != nullptr)
    {
        summary.galaxyName = galaxyObject->getProperty ("name").toString();
        summary.galaxySeed = static_cast<int> (galaxyObject->getProperty ("seed"));
        if (const auto* systems = galaxyObject->getProperty ("systems").getArray(); systems != nullptr)
            summary.systemCount = systems->size();
    }

    if (const auto* persistenceObject = object->getProperty ("persistence").getDynamicObject(); persistenceObject != nullptr)
    {
        if (const auto* visitLog = persistenceObject->getProperty ("visitLog").getArray(); visitLog != nullptr)
            summary.visitedPlanets = visitLog->size();
    }

    return true;
}

void PersistenceManager::clearWorkingData()
{
    rootData = juce::var (new juce::DynamicObject());
    ensureLoaded();
    writeWorkingData();
}

void PersistenceManager::ensureLoaded()
{
    if (! rootData.isVoid())
        return;

    if (saveFile.existsAsFile())
        rootData = juce::JSON::parse (saveFile);

    if (! rootData.isObject())
        rootData = juce::var (new juce::DynamicObject());

    if (auto* object = rootData.getDynamicObject(); object != nullptr)
    {
        if (! object->hasProperty ("planets"))
            object->setProperty ("planets", juce::var (new juce::DynamicObject()));

        if (! object->hasProperty ("visitLog"))
            object->setProperty ("visitLog", juce::var (juce::Array<juce::var>()));
    }
}

void PersistenceManager::writeWorkingData()
{
    saveFile.replaceWithText (juce::JSON::toString (rootData, true));
}

juce::DynamicObject* PersistenceManager::getPlanetsObject()
{
    if (auto* object = rootData.getDynamicObject())
        return object->getProperty ("planets").getDynamicObject();

    return nullptr;
}

juce::Array<juce::var>* PersistenceManager::getVisitLogArray()
{
    if (auto* object = rootData.getDynamicObject())
        return object->getProperty ("visitLog").getArray();

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

juce::var PersistenceManager::serialiseGalaxy (const GalaxyMetadata& galaxy)
{
    auto* galaxyObject = new juce::DynamicObject();
    galaxyObject->setProperty ("name", galaxy.name);
    galaxyObject->setProperty ("seed", galaxy.seed);

    juce::Array<juce::var> systems;
    systems.ensureStorageAllocated (galaxy.systems.size());

    for (const auto* system : galaxy.systems)
    {
        auto* systemObject = new juce::DynamicObject();
        systemObject->setProperty ("id", system->id);
        systemObject->setProperty ("name", system->name);
        systemObject->setProperty ("seed", system->seed);
        systemObject->setProperty ("x", system->galaxyPosition.x);
        systemObject->setProperty ("y", system->galaxyPosition.y);

        juce::Array<juce::var> planets;
        planets.ensureStorageAllocated (system->planets.size());
        for (const auto* planet : system->planets)
        {
            auto* planetObject = new juce::DynamicObject();
            planetObject->setProperty ("id", planet->id);
            planetObject->setProperty ("name", planet->name);
            planetObject->setProperty ("seed", planet->seed);
            planetObject->setProperty ("orbitIndex", planet->orbitIndex);
            planetObject->setProperty ("musicalRootHz", planet->musicalRootHz);
            planetObject->setProperty ("energy", planet->energy);
            planetObject->setProperty ("water", planet->water);
            planetObject->setProperty ("atmosphere", planet->atmosphere);
            planetObject->setProperty ("assignedBuildMode", buildModeToString (planet->assignedBuildMode));
            planetObject->setProperty ("assignedPerformanceMode", performanceModeToString (planet->assignedPerformanceMode));
            planetObject->setProperty ("accent", planet->accent.toDisplayString (true));
            planets.add (juce::var (planetObject));
        }

        systemObject->setProperty ("planets", juce::var (planets));
        systems.add (juce::var (systemObject));
    }

    galaxyObject->setProperty ("systems", juce::var (systems));
    return juce::var (galaxyObject);
}

GalaxyMetadata PersistenceManager::deserialiseGalaxy (const juce::var& data)
{
    GalaxyMetadata galaxy;
    auto* galaxyObject = data.getDynamicObject();
    if (galaxyObject == nullptr)
        return galaxy;

    galaxy.name = galaxyObject->getProperty ("name").toString();
    galaxy.seed = static_cast<int> (galaxyObject->getProperty ("seed"));

    if (const auto* systems = galaxyObject->getProperty ("systems").getArray(); systems != nullptr)
    {
        for (const auto& systemVar : *systems)
        {
            auto* systemObject = systemVar.getDynamicObject();
            if (systemObject == nullptr)
                continue;

            auto* system = galaxy.systems.add (new StarSystemMetadata());
            system->id = systemObject->getProperty ("id").toString();
            system->name = systemObject->getProperty ("name").toString();
            system->seed = static_cast<int> (systemObject->getProperty ("seed"));
            system->galaxyPosition = {
                static_cast<float> (double (systemObject->getProperty ("x"))),
                static_cast<float> (double (systemObject->getProperty ("y")))
            };

            if (const auto* planets = systemObject->getProperty ("planets").getArray(); planets != nullptr)
            {
                for (const auto& planetVar : *planets)
                {
                    auto* planetObject = planetVar.getDynamicObject();
                    if (planetObject == nullptr)
                        continue;

                    auto* planet = system->planets.add (new PlanetMetadata());
                    planet->id = planetObject->getProperty ("id").toString();
                    planet->name = planetObject->getProperty ("name").toString();
                    planet->seed = static_cast<int> (planetObject->getProperty ("seed"));
                    planet->orbitIndex = static_cast<int> (planetObject->getProperty ("orbitIndex"));
                    planet->musicalRootHz = static_cast<float> (double (planetObject->getProperty ("musicalRootHz")));
                    planet->energy = static_cast<float> (double (planetObject->getProperty ("energy")));
                    planet->water = static_cast<float> (double (planetObject->getProperty ("water")));
                    planet->atmosphere = static_cast<float> (double (planetObject->getProperty ("atmosphere")));
                    planet->assignedBuildMode = buildModeFromString (planetObject->getProperty ("assignedBuildMode").toString());
                    planet->assignedPerformanceMode = performanceModeFromString (planetObject->getProperty ("assignedPerformanceMode").toString());
                    planet->accent = juce::Colour::fromString (planetObject->getProperty ("accent").toString());
                }
            }
        }
    }

    return galaxy;
}

GalaxyMetadata GalaxyGenerator::generateGalaxy (int seed)
{
    juce::Random random (seed);
    GalaxyMetadata galaxy;
    galaxy.seed = seed;
    galaxy.name = makeName (random, 2, 3) + " Galaxy";

    const auto systemCount = 30;
    for (int systemIndex = 0; systemIndex < systemCount; ++systemIndex)
    {
        auto* system = galaxy.systems.add (new StarSystemMetadata());
        system->seed = random.nextInt();
        system->id = "system_" + juce::String (systemIndex);
        system->name = makeName (random, 2, 3);

        const auto angle = juce::MathConstants<float>::twoPi * (static_cast<float> (systemIndex) / static_cast<float> (systemCount));
        const auto radius = 0.10f + random.nextFloat() * 0.90f;
        system->galaxyPosition = { 0.5f + std::cos (angle) * radius * 0.48f, 0.5f + std::sin (angle) * radius * 0.40f };

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
            planet->assignedBuildMode = static_cast<PlanetBuildMode> (systemRandom.nextInt (4));
            planet->assignedPerformanceMode = static_cast<PlanetPerformanceMode> (systemRandom.nextInt (5));
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
