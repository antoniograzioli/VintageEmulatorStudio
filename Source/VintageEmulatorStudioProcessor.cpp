// SPDX-License-Identifier: AGPL-3.0-only

#include "VintageEmulatorStudioProcessor.h"
#include "VintageEmulatorStudioEditor.h"
#include "EmbeddedStandaloneRuntimeResources.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstdint>
#include <cmath>
#include <chrono>
#include <utility>

#if JUCE_WINDOWS
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
 #include "EmbeddedStandaloneRuntimeResourcesWindows.h"
#endif

namespace
{
constexpr auto romDirectoryProperty = "romDirectory";
constexpr auto legacyRomPathProperty = "romPath";
constexpr auto artworkDirectoryProperty = "artworkDirectory";
constexpr auto floppyMediaPathProperty = "media.floppy.path";
constexpr auto cdRomMediaPathProperty = "media.cdrom.path";
constexpr auto hardDiskMediaPathProperty = "media.harddisk.path";
constexpr auto floppyRecentMediaPropertyPrefix = "media.floppy.recent.";
constexpr auto cdRomRecentMediaPropertyPrefix = "media.cdrom.recent.";
constexpr auto hardDiskRecentMediaPropertyPrefix = "media.harddisk.recent.";
constexpr int maximumRecentMediaEntries = 10;
constexpr auto recentPreferencesVersionProperty = "recent.persistence.version";
constexpr auto recentPreferencesCountSuffix = ".count";
constexpr auto recentPreferencesEntrySeparator = ".";
constexpr auto runtimeResourcesLockName = "net.autodafe.VintageEmulatorStudio.runtime.resources";
constexpr auto runtimeResourcesReadyMarker = ".ves-runtime-resources-ready";
constexpr auto guiPerformanceModePreferenceKey = "gui.performanceMode";
constexpr auto backgroundColourPreferenceKey = "appearance.backgroundColour";
const auto defaultVesBackgroundColour = juce::Colour::fromRGB (111, 110, 186);
constexpr std::array<std::uint8_t, 8> experimentalStateMagic { 'V', 'E', 'S', 'F', 'B', '0', '1', 0 };
constexpr std::uint32_t experimentalStateFormatVersion = 2;
constexpr std::uint64_t maximumExperimentalStateBlobSize = 64ULL * 1024ULL * 1024ULL;
constexpr auto embeddedMameVersion = "0.289";
constexpr std::array<std::uint8_t, 8> dawStateMagic { 'V', 'E', 'S', 'D', 'A', 'W', '0', '1' };
constexpr std::uint32_t dawStateFormatVersion = 1;
#if JucePlugin_Build_Standalone
constexpr auto standaloneMasterVolumePreferenceKey = "standalone.masterVolume";
#endif

ves::standalone_resources::Payload standaloneRuntimeResourcesPayload;

juce::File stateFileForDriver (const juce::String& driver)
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("VintageEmulatorStudio")
        .getChildFile ("experimental-states")
        .getChildFile (driver + ".state");
}

juce::File autosaveFileForDriver (const juce::String& driver)
{
    return stateFileForDriver (driver).getSiblingFile (driver + "-autosave.state");
}

void writeLittleEndian32 (juce::OutputStream& stream, std::uint32_t value)
{
    for (int shift = 0; shift < 32; shift += 8)
        stream.writeByte (static_cast<char> ((value >> shift) & 0xff));
}

void writeLittleEndian64 (juce::OutputStream& stream, std::uint64_t value)
{
    for (int shift = 0; shift < 64; shift += 8)
        stream.writeByte (static_cast<char> ((value >> shift) & 0xff));
}

bool readLittleEndian32 (const std::uint8_t*& cursor, const std::uint8_t* end, std::uint32_t& value)
{
    if (end - cursor < 4)
        return false;
    value = 0;
    for (int shift = 0; shift < 32; shift += 8)
        value |= static_cast<std::uint32_t> (*cursor++) << shift;
    return true;
}

bool readLittleEndian64 (const std::uint8_t*& cursor, const std::uint8_t* end, std::uint64_t& value)
{
    if (end - cursor < 8)
        return false;
    value = 0;
    for (int shift = 0; shift < 64; shift += 8)
        value |= static_cast<std::uint64_t> (*cursor++) << shift;
    return true;
}

juce::String heldMidiNotesToHex (const ves::HeldMidiNotes& notes)
{
    static constexpr char digits[] = "0123456789abcdef";
    juce::String result;
    result.preallocateBytes (notes.size() * 16);
    for (const auto word : notes)
        for (int shift = 60; shift >= 0; shift -= 4)
            result += juce::String::charToString (digits[(word >> shift) & 0xf]);
    return result;
}

bool heldMidiNotesFromHex (const juce::String& text, ves::HeldMidiNotes& notes)
{
    notes.fill (0);
    if (text.isEmpty())
        return true;
    if (text.length() != static_cast<int> (notes.size() * 16))
        return false;
    for (std::size_t wordIndex = 0; wordIndex < notes.size(); ++wordIndex)
    {
        std::uint64_t word = 0;
        for (int digitIndex = 0; digitIndex < 16; ++digitIndex)
        {
            const auto c = text[static_cast<int> (wordIndex * 16) + digitIndex];
            const int value = c >= '0' && c <= '9' ? c - '0'
                            : c >= 'a' && c <= 'f' ? c - 'a' + 10
                            : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
            if (value < 0)
                return false;
            word = (word << 4) | static_cast<std::uint64_t> (value);
        }
        notes[wordIndex] = word;
    }
    return true;
}

struct DawStatePayload
{
    juce::MemoryBlock xmlState;
    std::vector<std::uint8_t> snapshot;
    juce::String driver;
    std::uint32_t compatibilityRevision = 0;
    ves::HeldMidiNotes heldMidiNotes {};
    bool containerFound = false;
    bool snapshotAccepted = false;
    juce::String validationResult { "no embedded blob" };
};

void writeDawStatePayload (juce::MemoryBlock& destination, const juce::MemoryBlock& xmlState,
                           const juce::String& driver, const std::vector<std::uint8_t>& snapshot,
                           const ves::HeldMidiNotes& heldMidiNotes)
{
    auto metadataObject = std::make_unique<juce::DynamicObject>();
    metadataObject->setProperty ("machineDriver", driver);
    metadataObject->setProperty ("saveStateCompatibilityRevision", static_cast<int> (experimentalStateFormatVersion));
    metadataObject->setProperty ("mameVersion", embeddedMameVersion);
    metadataObject->setProperty ("vesStateFormatRevision", static_cast<int> (dawStateFormatVersion));
    metadataObject->setProperty ("heldMidiNotes", heldMidiNotesToHex (heldMidiNotes));
    const auto metadata = juce::JSON::toString (juce::var (metadataObject.release()), false);
    const auto metadataSize = static_cast<std::uint32_t> (metadata.getNumBytesAsUTF8());

    destination.reset();
    juce::MemoryOutputStream output (destination, false);
    output.write (dawStateMagic.data(), dawStateMagic.size());
    writeLittleEndian32 (output, dawStateFormatVersion);
    writeLittleEndian32 (output, metadataSize);
    writeLittleEndian64 (output, xmlState.getSize());
    writeLittleEndian64 (output, snapshot.size());
    output.write (metadata.toRawUTF8(), metadataSize);
    output.write (xmlState.getData(), xmlState.getSize());
    output.write (snapshot.data(), snapshot.size());
}

DawStatePayload readDawStatePayload (const void* data, int sizeInBytes)
{
    DawStatePayload payload;
    if (data == nullptr || sizeInBytes <= 0)
        return payload;

    const auto* cursor = static_cast<const std::uint8_t*> (data);
    const auto* const end = cursor + static_cast<std::size_t> (sizeInBytes);
    if (end - cursor < static_cast<std::ptrdiff_t> (dawStateMagic.size())
        || ! std::equal (dawStateMagic.begin(), dawStateMagic.end(), cursor))
    {
        payload.xmlState.append (data, static_cast<std::size_t> (sizeInBytes));
        return payload;
    }

    payload.containerFound = true;
    cursor += dawStateMagic.size();
    std::uint32_t formatVersion = 0, metadataSize = 0;
    std::uint64_t xmlSize = 0, snapshotSize = 0;
    if (! readLittleEndian32 (cursor, end, formatVersion) || ! readLittleEndian32 (cursor, end, metadataSize)
        || ! readLittleEndian64 (cursor, end, xmlSize) || ! readLittleEndian64 (cursor, end, snapshotSize))
    {
        payload.validationResult = "rejected: truncated DAW state header";
        return payload;
    }

    const auto remaining = static_cast<std::uint64_t> (end - cursor);
    if (metadataSize > remaining || xmlSize > remaining - metadataSize)
    {
        payload.validationResult = "rejected: truncated DAW XML state";
        return payload;
    }

    const juce::String metadata (reinterpret_cast<const char*> (cursor), metadataSize);
    cursor += metadataSize;
    payload.xmlState.append (cursor, static_cast<std::size_t> (xmlSize));
    cursor += xmlSize;

    if (formatVersion != dawStateFormatVersion)
        payload.validationResult = "rejected: unsupported VES state-format revision";
    else if (snapshotSize == 0 || snapshotSize > maximumExperimentalStateBlobSize
             || static_cast<std::uint64_t> (end - cursor) != snapshotSize)
        payload.validationResult = "rejected: invalid or truncated state blob";
    else
    {
        const auto parsed = juce::JSON::parse (metadata);
        const auto* object = parsed.getDynamicObject();
        if (object == nullptr)
            payload.validationResult = "rejected: invalid compatibility metadata";
        else if (object->getProperty ("machineDriver").toString().isEmpty())
            payload.validationResult = "rejected: missing machine driver";
        else if (static_cast<int> (object->getProperty ("saveStateCompatibilityRevision"))
                 != static_cast<int> (experimentalStateFormatVersion))
            payload.validationResult = "rejected: unsupported save-state compatibility revision";
        else if (object->getProperty ("mameVersion").toString() != embeddedMameVersion)
            payload.validationResult = "rejected: incompatible MAME version";
        else if (static_cast<int> (object->getProperty ("vesStateFormatRevision"))
                 != static_cast<int> (dawStateFormatVersion))
            payload.validationResult = "rejected: incompatible VES state-format revision";
        else
        {
            payload.driver = object->getProperty ("machineDriver").toString();
            payload.compatibilityRevision = static_cast<std::uint32_t> (
                static_cast<int> (object->getProperty ("saveStateCompatibilityRevision")));
            if (! heldMidiNotesFromHex (object->getProperty ("heldMidiNotes").toString(), payload.heldMidiNotes))
            {
                payload.validationResult = "rejected: invalid held-note metadata";
                return payload;
            }
            payload.snapshot.assign (cursor, end);
            payload.snapshotAccepted = true;
            payload.validationResult = "accepted";
        }
    }
    return payload;
}

bool writeExperimentalStateFile (const juce::File& file, const std::vector<std::uint8_t>& blob,
                                 const juce::String& driver, double sampleRate,
                                 std::uint64_t engineGeneration, const ves::HeldMidiNotes& heldMidiNotes,
                                 juce::String& error)
{
    auto metadataObject = std::make_unique<juce::DynamicObject>();
    metadataObject->setProperty ("vesVersion", JucePlugin_VersionString);
    metadataObject->setProperty ("mameVersion", embeddedMameVersion);
    metadataObject->setProperty ("machineDriver", driver);
    metadataObject->setProperty ("saveStateCompatibilityRevision", static_cast<int> (experimentalStateFormatVersion));
    metadataObject->setProperty ("sampleRate", sampleRate);
    metadataObject->setProperty ("engineGeneration", static_cast<juce::int64> (engineGeneration));
    metadataObject->setProperty ("blobSize", static_cast<juce::int64> (blob.size()));
    metadataObject->setProperty ("heldMidiNotes", heldMidiNotesToHex (heldMidiNotes));
    const auto metadata = juce::JSON::toString (juce::var (metadataObject.release()), true);
    const auto metadataBytes = metadata.toRawUTF8();
    const auto metadataSize = static_cast<std::uint32_t> (metadata.getNumBytesAsUTF8());

    if (file.getParentDirectory().createDirectory().failed())
    {
        error = "Unable to create experimental state directory";
        return false;
    }
    juce::TemporaryFile temporary (file);
    auto output = temporary.getFile().createOutputStream();
    if (output == nullptr)
    {
        error = "Unable to open experimental state file";
        return false;
    }
    output->setPosition (0);
    output->truncate();
    output->write (experimentalStateMagic.data(), experimentalStateMagic.size());
    writeLittleEndian32 (*output, experimentalStateFormatVersion);
    writeLittleEndian32 (*output, metadataSize);
    writeLittleEndian64 (*output, blob.size());
    output->write (metadataBytes, metadataSize);
    output->write (blob.data(), blob.size());
    output->flush();
    if (output->getStatus().failed())
    {
        error = output->getStatus().getErrorMessage();
        return false;
    }
    output.reset();
    if (! temporary.overwriteTargetFileWithTemporary())
    {
        error = "Unable to atomically replace experimental state file";
        return false;
    }
    if (! file.existsAsFile() || file.getSize() <= 0)
    {
        error = "Atomic state file replacement did not produce a complete file";
        return false;
    }
    return true;
}

bool readExperimentalStateFile (const juce::File& file, const juce::String& expectedDriver,
                                std::vector<std::uint8_t>& blob, ves::HeldMidiNotes& heldMidiNotes,
                                juce::String& error)
{
    if (! file.existsAsFile())
    {
        error = "No state file";
        return false;
    }
    juce::MemoryBlock contents;
    if (! file.loadFileAsData (contents))
    {
        error = "Unable to read state file";
        return false;
    }
    const auto* cursor = static_cast<const std::uint8_t*> (contents.getData());
    const auto* const end = cursor + contents.getSize();
    if (end - cursor < static_cast<std::ptrdiff_t> (experimentalStateMagic.size())
        || ! std::equal (experimentalStateMagic.begin(), experimentalStateMagic.end(), cursor))
    {
        error = "Incompatible state file: invalid magic";
        return false;
    }
    cursor += experimentalStateMagic.size();
    std::uint32_t formatVersion = 0, metadataSize = 0;
    std::uint64_t blobSize = 0;
    if (! readLittleEndian32 (cursor, end, formatVersion) || ! readLittleEndian32 (cursor, end, metadataSize)
        || ! readLittleEndian64 (cursor, end, blobSize))
    {
        error = "Incompatible state file: invalid or truncated wrapper";
        return false;
    }
    if (formatVersion != experimentalStateFormatVersion)
    {
        error = "Incompatible state file: unsupported compatibility revision";
        return false;
    }
    if (blobSize == 0 || blobSize > maximumExperimentalStateBlobSize
        || static_cast<std::uint64_t> (end - cursor) != static_cast<std::uint64_t> (metadataSize) + blobSize)
    {
        error = "Incompatible state file: invalid or truncated wrapper";
        return false;
    }
    const auto metadataText = juce::String::fromUTF8 (reinterpret_cast<const char*> (cursor), static_cast<int> (metadataSize));
    cursor += metadataSize;
    const auto metadata = juce::JSON::parse (metadataText);
    const auto* object = metadata.getDynamicObject();
    if (object == nullptr || object->getProperty ("machineDriver").toString() != expectedDriver
        || object->getProperty ("vesVersion").toString() != JucePlugin_VersionString
        || object->getProperty ("mameVersion").toString() != embeddedMameVersion
        || static_cast<int> (object->getProperty ("saveStateCompatibilityRevision")) != static_cast<int> (experimentalStateFormatVersion)
        || static_cast<std::uint64_t> (static_cast<juce::int64> (object->getProperty ("blobSize"))) != blobSize)
    {
        error = "Incompatible state file: metadata mismatch";
        return false;
    }
    if (! heldMidiNotesFromHex (object->getProperty ("heldMidiNotes").toString(), heldMidiNotes))
    {
        error = "Incompatible state file: invalid held-note metadata";
        return false;
    }
    blob.assign (cursor, end);
    return true;
}

#if JUCE_WINDOWS
ves::standalone_resources::Payload loadWindowsStandaloneRuntimeResourcesPayload()
{
    const auto module = GetModuleHandleW (nullptr);
    if (module == nullptr)
        return {};

    const auto resource = FindResourceW (module,
                                         MAKEINTRESOURCEW (VES_STANDALONE_RUNTIME_RESOURCES_ZIP_ID),
                                         MAKEINTRESOURCEW (10));
    if (resource == nullptr)
        return {};

    const auto resourceSize = SizeofResource (module, resource);
    if (resourceSize == 0 || static_cast<unsigned long long> (resourceSize) != VES_STANDALONE_RUNTIME_RESOURCES_ZIP_SIZE)
        return {};

    const auto loadedResource = LoadResource (module, resource);
    if (loadedResource == nullptr)
        return {};

    const auto data = LockResource (loadedResource);
    if (data == nullptr)
        return {};

    return {
        static_cast<const unsigned char*> (data),
        static_cast<std::size_t> (resourceSize),
        VES_STANDALONE_RUNTIME_RESOURCES_SHA256
    };
}
#endif

const char* guiPerformanceModeToString (GuiPerformanceMode mode)
{
    switch (mode)
    {
        case GuiPerformanceMode::Normal:   return "Normal";
        case GuiPerformanceMode::Reduced:  return "Reduced";
        case GuiPerformanceMode::Static:   return "Static";
        case GuiPerformanceMode::Disabled: return "Disabled";
    }

    return "Normal";
}

GuiPerformanceMode guiPerformanceModeFromString (const juce::String& text)
{
    if (text.equalsIgnoreCase ("Reduced"))
        return GuiPerformanceMode::Reduced;
    if (text.equalsIgnoreCase ("Static"))
        return GuiPerformanceMode::Static;
    if (text.equalsIgnoreCase ("Disabled"))
        return GuiPerformanceMode::Disabled;
    return GuiPerformanceMode::Normal;
}

struct RuntimeResourceResolution
{
    juce::File directory;
    StartupDiagnostic diagnostic;
};

bool hasStartupDiagnostic (const StartupDiagnostic& diagnostic)
{
    return diagnostic.category != StartupError::None;
}

StartupDiagnostic makeRuntimeResourcesDiagnostic (const juce::String& technicalDetails)
{
    StartupDiagnostic diagnostic;
    diagnostic.category = StartupError::RuntimeResources;
    diagnostic.summary = "VES runtime resources could not be loaded.";
    diagnostic.recovery = "Please reinstall VES.\nIf the problem persists, check that your user application data folder is writable.";
    diagnostic.technicalDetails = technicalDetails;
    return diagnostic;
}

StartupError mapEmbeddedStartupError (ves::EmbeddedStartupError category)
{
    switch (category)
    {
        case ves::EmbeddedStartupError::None:                 return StartupError::None;
        case ves::EmbeddedStartupError::RuntimeResources:     return StartupError::RuntimeResources;
        case ves::EmbeddedStartupError::PrimaryRomMissing:    return StartupError::PrimaryRomMissing;
        case ves::EmbeddedStartupError::MissingRom:           return StartupError::MissingRom;
        case ves::EmbeddedStartupError::RomChecksumMismatch:  return StartupError::RomChecksumMismatch;
        case ves::EmbeddedStartupError::InvalidRomSet:        return StartupError::InvalidRomSet;
        case ves::EmbeddedStartupError::MediaUnavailable:    return StartupError::MediaUnavailable;
        case ves::EmbeddedStartupError::MediaLoad:            return StartupError::MediaLoad;
        case ves::EmbeddedStartupError::Configuration:        return StartupError::Configuration;
        case ves::EmbeddedStartupError::Nvram:                return StartupError::Nvram;
        case ves::EmbeddedStartupError::LuaPlugin:            return StartupError::LuaPlugin;
        case ves::EmbeddedStartupError::DeviceInitialization: return StartupError::DeviceInitialization;
        case ves::EmbeddedStartupError::AudioInitialization:  return StartupError::AudioInitialization;
        case ves::EmbeddedStartupError::EngineFailure:        return StartupError::EngineFailure;
        case ves::EmbeddedStartupError::StartupTimeout:       return StartupError::StartupTimeout;
        case ves::EmbeddedStartupError::Unknown:              return StartupError::Unknown;
    }

    return StartupError::Unknown;
}

StartupDiagnostic convertEmbeddedStartupDiagnostic (const ves::EmbeddedStartupDiagnostic& source)
{
    StartupDiagnostic diagnostic;
    diagnostic.category = mapEmbeddedStartupError (source.category);
    diagnostic.summary = juce::String::fromUTF8 (source.summary.c_str());
    diagnostic.details = juce::String::fromUTF8 (source.details.c_str());
    diagnostic.recovery = juce::String::fromUTF8 (source.recovery.c_str());
    diagnostic.technicalDetails = juce::String::fromUTF8 (source.technical_details.c_str());

    diagnostic.issues.reserve (source.issues.size());
    for (const auto& sourceIssue : source.issues)
    {
        StartupIssue issue;
        issue.name = juce::String::fromUTF8 (sourceIssue.name.c_str());
        issue.owner = juce::String::fromUTF8 (sourceIssue.owner.c_str());
        issue.expectedCrc = juce::String::fromUTF8 (sourceIssue.expected_crc.c_str());
        issue.expectedSha1 = juce::String::fromUTF8 (sourceIssue.expected_sha1.c_str());
        issue.actualCrc = juce::String::fromUTF8 (sourceIssue.actual_crc.c_str());
        issue.actualSha1 = juce::String::fromUTF8 (sourceIssue.actual_sha1.c_str());
        issue.expectedLength = sourceIssue.expected_length;
        issue.actualLength = sourceIssue.actual_length;
        diagnostic.issues.push_back (std::move (issue));
    }

    return diagnostic;
}

int recentMediaIndex (VintageEmulatorStudioProcessor::RecentMediaType type)
{
    return static_cast<int> (type);
}

const char* recentMediaPropertyPrefix (VintageEmulatorStudioProcessor::RecentMediaType type)
{
    switch (type)
    {
        case VintageEmulatorStudioProcessor::RecentMediaType::Floppy:   return floppyRecentMediaPropertyPrefix;
        case VintageEmulatorStudioProcessor::RecentMediaType::CdRom:    return cdRomRecentMediaPropertyPrefix;
        case VintageEmulatorStudioProcessor::RecentMediaType::HardDisk: return hardDiskRecentMediaPropertyPrefix;
    }

    return "";
}

void readRecentMediaPaths (const juce::ValueTree& stateTree,
                           VintageEmulatorStudioProcessor::RecentMediaType type,
                           std::vector<juce::String>& paths)
{
    paths.clear();
    const auto prefix = juce::String (recentMediaPropertyPrefix (type));
    for (int index = 0; index < maximumRecentMediaEntries; ++index)
    {
        const auto path = stateTree.getProperty (prefix + juce::String (index), {}).toString();
        if (path.isNotEmpty())
            paths.push_back (path);
    }
}

const char* recentPreferenceKeyPrefix (VintageEmulatorStudioProcessor::RecentMediaType type)
{
    switch (type)
    {
        case VintageEmulatorStudioProcessor::RecentMediaType::Floppy:   return "recent.floppy";
        case VintageEmulatorStudioProcessor::RecentMediaType::CdRom:    return "recent.cdrom";
        case VintageEmulatorStudioProcessor::RecentMediaType::HardDisk: return "recent.harddisk";
    }

    return "recent";
}

class GlobalRecentMediaPreferences final
{
public:
    GlobalRecentMediaPreferences()
    {
        juce::PropertiesFile::Options options;
        options.applicationName = "VintageEmulatorStudio";
        options.filenameSuffix = "settings";
        options.folderName = "VintageEmulatorStudio";
        options.osxLibrarySubFolder = "Application Support";
        options.commonToAllUsers = false;
        options.ignoreCaseOfKeyNames = false;
        options.millisecondsBeforeSaving = 0;
        applicationProperties.setStorageParameters (options);

        const juce::ScopedLock scopedLock (lock);
        reloadLocked();
    }

    std::vector<juce::String> get (VintageEmulatorStudioProcessor::RecentMediaType type)
    {
        const juce::ScopedLock scopedLock (lock);
        reloadLocked();
        return paths[static_cast<std::size_t> (recentMediaIndex (type))];
    }

    void add (VintageEmulatorStudioProcessor::RecentMediaType type, const juce::File& file)
    {
        const auto path = normalisePath (file.getFullPathName());
        if (path.isEmpty())
            return;

        const juce::ScopedLock scopedLock (lock);
        reloadLocked(); // Merge the latest on-disk state before any mutation.
        auto& entries = paths[static_cast<std::size_t> (recentMediaIndex (type))];
        entries.erase (std::remove (entries.begin(), entries.end(), path), entries.end());
        entries.insert (entries.begin(), path);
        trimAndDeduplicate (entries);
        saveLocked();
    }

    void remove (VintageEmulatorStudioProcessor::RecentMediaType type, const juce::String& path)
    {
        const auto normalisedPath = normalisePath (path);
        if (normalisedPath.isEmpty())
            return;

        const juce::ScopedLock scopedLock (lock);
        reloadLocked();
        auto& entries = paths[static_cast<std::size_t> (recentMediaIndex (type))];
        const auto oldSize = entries.size();
        entries.erase (std::remove (entries.begin(), entries.end(), normalisedPath), entries.end());
        if (entries.size() != oldSize)
            saveLocked();
    }

    void migrateLegacyState (const std::array<std::vector<juce::String>, 3>& legacyPaths)
    {
        const juce::ScopedLock scopedLock (lock);
        reloadLocked();

        auto* settings = applicationProperties.getUserSettings();
        if (settings == nullptr || settings->getIntValue (recentPreferencesVersionProperty, 0) >= 1)
            return;

        bool hasGlobalEntries = false;
        for (const auto& entries : paths)
            hasGlobalEntries = hasGlobalEntries || ! entries.empty();

        if (! hasGlobalEntries)
        {
            for (std::size_t index = 0; index < paths.size(); ++index)
            {
                paths[index] = legacyPaths[index];
                trimAndDeduplicate (paths[index]);
            }
        }

        settings->setValue (recentPreferencesVersionProperty, 1);
        saveLocked();
    }

private:
    static juce::String normalisePath (const juce::String& path)
    {
        return path.isNotEmpty() ? juce::File (path).getFullPathName() : juce::String();
    }

    static void trimAndDeduplicate (std::vector<juce::String>& entries)
    {
        std::vector<juce::String> cleaned;
        cleaned.reserve (maximumRecentMediaEntries);
        for (const auto& entry : entries)
        {
            const auto path = normalisePath (entry);
            if (path.isNotEmpty() && std::find (cleaned.begin(), cleaned.end(), path) == cleaned.end())
                cleaned.push_back (path);
            if (static_cast<int> (cleaned.size()) == maximumRecentMediaEntries)
                break;
        }
        entries = std::move (cleaned);
    }

    void reloadLocked()
    {
        auto* settings = applicationProperties.getUserSettings();
        if (settings == nullptr)
            return;

        settings->reload();
        for (int typeIndex = 0; typeIndex < 3; ++typeIndex)
        {
            const auto type = static_cast<VintageEmulatorStudioProcessor::RecentMediaType> (typeIndex);
            const auto keyPrefix = juce::String (recentPreferenceKeyPrefix (type));
            auto& entries = paths[static_cast<std::size_t> (typeIndex)];
            entries.clear();
            const auto count = juce::jlimit (0, maximumRecentMediaEntries,
                                              settings->getIntValue (keyPrefix + recentPreferencesCountSuffix, 0));
            for (int index = 0; index < count; ++index)
                entries.push_back (settings->getValue (keyPrefix + recentPreferencesEntrySeparator + juce::String (index)));
            trimAndDeduplicate (entries);
        }
    }

    void saveLocked()
    {
        auto* settings = applicationProperties.getUserSettings();
        if (settings == nullptr)
        {
            juce::Logger::writeToLog ("VintageEmulatorStudio could not access global Recent Media preferences");
            return;
        }

        for (int typeIndex = 0; typeIndex < 3; ++typeIndex)
        {
            const auto type = static_cast<VintageEmulatorStudioProcessor::RecentMediaType> (typeIndex);
            const auto keyPrefix = juce::String (recentPreferenceKeyPrefix (type));
            const auto& entries = paths[static_cast<std::size_t> (typeIndex)];
            settings->setValue (keyPrefix + recentPreferencesCountSuffix, static_cast<int> (entries.size()));
            for (int index = 0; index < maximumRecentMediaEntries; ++index)
                settings->setValue (keyPrefix + recentPreferencesEntrySeparator + juce::String (index),
                                    index < static_cast<int> (entries.size()) ? entries[static_cast<std::size_t> (index)] : juce::String());
        }

        if (! settings->save())
            juce::Logger::writeToLog ("VintageEmulatorStudio could not save global Recent Media preferences");
    }

    juce::CriticalSection lock;
    juce::ApplicationProperties applicationProperties;
    std::array<std::vector<juce::String>, 3> paths;
};

GlobalRecentMediaPreferences& globalRecentMediaPreferences()
{
    static GlobalRecentMediaPreferences preferences;
    return preferences;
}

class GlobalAppearancePreferences final
{
public:
    GlobalAppearancePreferences()
    {
        juce::PropertiesFile::Options options;
        options.applicationName = "VintageEmulatorStudio";
        options.filenameSuffix = "settings";
        options.folderName = "VintageEmulatorStudio";
        options.osxLibrarySubFolder = "Application Support";
        options.commonToAllUsers = false;
        options.ignoreCaseOfKeyNames = false;
        options.millisecondsBeforeSaving = 0;
        applicationProperties.setStorageParameters (options);
    }

    juce::Colour loadBackgroundColour()
    {
        const juce::ScopedLock scopedLock (lock);
        auto* settings = applicationProperties.getUserSettings();
        if (settings == nullptr)
            return defaultVesBackgroundColour;

        settings->reload();
        const auto encoded = settings->getValue (backgroundColourPreferenceKey);
        return encoded.isNotEmpty() ? juce::Colour::fromString (encoded) : defaultVesBackgroundColour;
    }

    void saveBackgroundColour (juce::Colour colour)
    {
        const juce::ScopedLock scopedLock (lock);
        auto* settings = applicationProperties.getUserSettings();
        if (settings == nullptr)
            return;

        settings->setValue (backgroundColourPreferenceKey, colour.toString());
        settings->save();
    }

private:
    juce::CriticalSection lock;
    juce::ApplicationProperties applicationProperties;
};

GlobalAppearancePreferences& globalAppearancePreferences()
{
    static GlobalAppearancePreferences preferences;
    return preferences;
}

juce::File romPathSettingsFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("VintageEmulatorStudio")
        .getChildFile ("settings")
        .getChildFile ("rom-directory.txt");
}

juce::File artworkPathSettingsFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("VintageEmulatorStudio")
        .getChildFile ("settings")
        .getChildFile ("artwork-directory.txt");
}

struct EmbeddedMachineProfile
{
	enum class SaveStateSupport { Unsupported, Experimental, Validated };
	struct MidiOptionNames
	{
		const char* inputOptionName;
		const char* outputOptionName;
	};

    const char* displayName;
    const char* driverName;
    bool requiresSeededNvram;
    bool experimental;
    bool nativeMidiIn = false;
    bool nativeMidiOut = false;
    bool usesVirtualMidiRetrofit = false;
    // This names the MIDI image option exposed by the retrofit's MIDI_PORT.
    // The MIDI_PORT tag itself is a slot option, not the provider option.
    const char* retrofitMidiInputOptionName = nullptr;
    enum class MediaType { Floppy, CdRom, HardDisk, Cassette };
    enum class MediaChangePolicy { RestartRequired, LiveChange };
    struct MediaDeviceProfile
    {
        MediaType type;
        const char* displayName;
        const char* mameOptionName;
        const char* mameInstanceName;
        const char* mameDeviceTag;
        const char* const* extensions;
        int extensionCount;
        MediaChangePolicy policy;
    };
    const MediaDeviceProfile* mediaDevices = nullptr;
    int mediaDeviceCount = 0;
	const MidiOptionNames* nativeMidiOptions = nullptr;
	// Hidden profiles remain addressable by driver name so existing sessions can restore.
	bool visibleInSelector = true;
	SaveStateSupport saveStateSupport = SaveStateSupport::Unsupported;
};

constexpr EmbeddedMachineProfile::MidiOptionNames dd9MidiOptions { "", "" };
constexpr EmbeddedMachineProfile::MidiOptionNames fb01MidiOptions { "midiin", "" };
// MU2000 has two MIDI input image devices.  MAME names their command-line
// options midiin1 and midiin2 in configuration order; midiin1 is mdin_a.
constexpr EmbeddedMachineProfile::MidiOptionNames mu2000MidiOptions { "midiin1", "midiout" };
constexpr EmbeddedMachineProfile::MidiOptionNames mpc60MidiOptions { "midiin1", "midiout1" };
constexpr EmbeddedMachineProfile::MidiOptionNames mpc3000MidiOptions { "midiin", "midiout" };

constexpr const char* akaiFloppyExtensions[] { ".hfe" };
constexpr const char* ensoniqFloppyExtensions[] { ".img", ".hfe" };
constexpr const char* akaiCdRomExtensions[] { ".iso", ".chd", ".cue", ".toc", ".nrg", ".gdi", ".cdr" };
constexpr const char* akaiHardDiskExtensions[] { ".chd", ".hd", ".hdv", ".2mg", ".hdi", ".hds" };
constexpr EmbeddedMachineProfile::MediaDeviceProfile akaiSamplerMediaDevices[] {
    { EmbeddedMachineProfile::MediaType::Floppy, "Floppy", "flop", "floppydisk", ":fdc:0",
      akaiFloppyExtensions, static_cast<int> (std::size (akaiFloppyExtensions)),
      EmbeddedMachineProfile::MediaChangePolicy::LiveChange },
    // The SCSI 2x CD-ROM is the default target at SCSI ID 4 for all five Akai configs.
    // MAME accepts both the full instance name "cdrom" and brief name "cdrm"; VES uses
    // the full, stable instance name for the startup option.
    { EmbeddedMachineProfile::MediaType::CdRom, "CD-ROM", "cdrom", "cdrom", ":scsi:4:cdrom",
      akaiCdRomExtensions, static_cast<int> (std::size (akaiCdRomExtensions)),
      EmbeddedMachineProfile::MediaChangePolicy::LiveChange },
    // The default SCSI hard disk target is always present at ID 5 in the shared Akai base config.
    { EmbeddedMachineProfile::MediaType::HardDisk, "Hard Disk", "harddisk", "harddisk", ":scsi:5:harddisk:image",
      akaiHardDiskExtensions, static_cast<int> (std::size (akaiHardDiskExtensions)),
      EmbeddedMachineProfile::MediaChangePolicy::RestartRequired }
};

// Kept separate so S3000XL can retain an explicit profile entry while sharing
// the same audited runtime floppy policy as the other Akai samplers.
constexpr EmbeddedMachineProfile::MediaDeviceProfile s3000xlMediaDevices[] {
    { EmbeddedMachineProfile::MediaType::Floppy, "Floppy", "flop", "floppydisk", ":fdc:0",
      akaiFloppyExtensions, static_cast<int> (std::size (akaiFloppyExtensions)),
      EmbeddedMachineProfile::MediaChangePolicy::LiveChange },
    { EmbeddedMachineProfile::MediaType::CdRom, "CD-ROM", "cdrom", "cdrom", ":scsi:4:cdrom",
      akaiCdRomExtensions, static_cast<int> (std::size (akaiCdRomExtensions)),
      EmbeddedMachineProfile::MediaChangePolicy::LiveChange },
    { EmbeddedMachineProfile::MediaType::HardDisk, "Hard Disk", "harddisk", "harddisk", ":scsi:5:harddisk:image",
      akaiHardDiskExtensions, static_cast<int> (std::size (akaiHardDiskExtensions)),
      EmbeddedMachineProfile::MediaChangePolicy::RestartRequired }
};

constexpr EmbeddedMachineProfile::MediaDeviceProfile ensoniqFloppyMediaDevices[] {
    { EmbeddedMachineProfile::MediaType::Floppy, "Floppy", "flop", "floppydisk", ":wd1772:0",
      ensoniqFloppyExtensions, static_cast<int> (std::size (ensoniqFloppyExtensions)),
      EmbeddedMachineProfile::MediaChangePolicy::LiveChange }
};
constexpr const char* mpcFloppyExtensions[] { ".dfi", ".mfm", ".td0", ".imd", ".dsk", ".ima", ".img", ".ufi", ".360", ".ipf", ".hfe" };
constexpr EmbeddedMachineProfile::MediaDeviceProfile mpcFloppyMediaDevices[] {
    { EmbeddedMachineProfile::MediaType::Floppy, "Floppy", "flop", "floppydisk", ":fdc:0", mpcFloppyExtensions, static_cast<int> (std::size (mpcFloppyExtensions)), EmbeddedMachineProfile::MediaChangePolicy::LiveChange }
};

constexpr EmbeddedMachineProfile machineProfiles[] {
    { "Akai CD3000i", "cd3000i", false, true, true, true, false, nullptr, akaiSamplerMediaDevices, static_cast<int> (std::size (akaiSamplerMediaDevices)) },
    { "Akai CD3000XL", "cd3000xl", false, true, true, true, false, nullptr, akaiSamplerMediaDevices, static_cast<int> (std::size (akaiSamplerMediaDevices)) },
    { "Akai MPC60", "mpc60", false, true, true, true, false, nullptr, mpcFloppyMediaDevices, static_cast<int> (std::size (mpcFloppyMediaDevices)), &mpc60MidiOptions },
    { "Akai MPC3000", "mpc3000", false, true, true, true, false, nullptr, mpcFloppyMediaDevices, static_cast<int> (std::size (mpcFloppyMediaDevices)), &mpc3000MidiOptions },
    { "Akai S2000", "s2000", false, true, true, true, false, nullptr, akaiSamplerMediaDevices, static_cast<int> (std::size (akaiSamplerMediaDevices)) },
    { "Akai S3000", "s3000", false, true, true, true, false, nullptr, akaiSamplerMediaDevices, static_cast<int> (std::size (akaiSamplerMediaDevices)) },
    { "Akai S3000XL", "s3000xl", false, true, true, true, false, nullptr, s3000xlMediaDevices, static_cast<int> (std::size (s3000xlMediaDevices)) },
    { "Casio AP-10", "ap10", false, false, true, false, false, nullptr, nullptr, 0, nullptr,
      true, EmbeddedMachineProfile::SaveStateSupport::Validated },
    { "Yamaha TX81Z", "tx81z", true, false, true, true },
    { "Yamaha FB-01", "fb01", false, false, true, true, false, nullptr, nullptr, 0, &fb01MidiOptions,
      true, EmbeddedMachineProfile::SaveStateSupport::Validated },
    { "Yamaha TG100", "tg100", false, false, true, true },
    { "Casio RZ-1", "rz1", false, false, true, false, false, nullptr, nullptr, 0, nullptr,
      true, EmbeddedMachineProfile::SaveStateSupport::Experimental },
    { "Casio CT-8000", "ct8000", false, false, true, false, false, nullptr, nullptr, 0, nullptr,
      true, EmbeddedMachineProfile::SaveStateSupport::Experimental },
    { "Casio CT-FK1", "ctfk1", false, false, true, false, false, nullptr, nullptr, 0, nullptr,
      true, EmbeddedMachineProfile::SaveStateSupport::Experimental },
    { "Casio CZ-101", "cz101", false, false, true, false, false, nullptr, nullptr, 0, nullptr,
      true, EmbeddedMachineProfile::SaveStateSupport::Experimental },
    { "Casio CZ-1", "cz1", false, false, true, false, false, nullptr, nullptr, 0, nullptr,
      true, EmbeddedMachineProfile::SaveStateSupport::Experimental },
    { "Casio CZ-230S", "cz230s", false, false, true, false, false, nullptr, nullptr, 0, nullptr,
      true, EmbeddedMachineProfile::SaveStateSupport::Experimental },
    { "Ensoniq ESQ-1", "esq1", false, false, true, true },
    { "PAiA FatMan", "fatman", false, false, true, true, false, nullptr, nullptr, 0, nullptr, false },
    { "Ensoniq VFX", "vfx", false, false, true, true },
    { "Ensoniq VFX-SD", "vfxsd", false, false, true, true, false, nullptr, ensoniqFloppyMediaDevices, static_cast<int> (std::size (ensoniqFloppyMediaDevices)) },
    { "LinnDrum", "linndrum", false, false, false, false, true, "midiin", nullptr, 0, nullptr,
      true, EmbeddedMachineProfile::SaveStateSupport::Experimental },
	{ "Oberheim DMX", "obdmx", false, false, false, false, true, "midiin", nullptr, 0, nullptr,
	  true, EmbeddedMachineProfile::SaveStateSupport::Experimental },
    { "Roland TR-707", "tr707", false, false, true, true, false, nullptr, nullptr, 0, nullptr,
      true, EmbeddedMachineProfile::SaveStateSupport::Experimental },
    { "Roland TR-727", "tr727", false, false, true, true, false, nullptr, nullptr, 0, nullptr,
      true, EmbeddedMachineProfile::SaveStateSupport::Experimental },
    { "Sequential Prophet-5", "prophet5r30", false, false, false, false, true, "midiin", nullptr, 0, nullptr,
      true, EmbeddedMachineProfile::SaveStateSupport::Experimental },
    { "Sequential Six-Trak", "sixtrak", false, false, true, true, false, nullptr, nullptr, 0, nullptr,
      true, EmbeddedMachineProfile::SaveStateSupport::Experimental },
    { "Yamaha DD-9", "dd9", false, false, false, false, false, nullptr, nullptr, 0, &dd9MidiOptions,
      true, EmbeddedMachineProfile::SaveStateSupport::Experimental },
    { "Yamaha DX100", "dx100", false, false, true, true },
    { "Yamaha MU-50", "mu50", false, false, true, true },
    { "Yamaha MU-2000", "mu2000", false, false, true, true, false, nullptr, nullptr, 0, &mu2000MidiOptions,
      true, EmbeddedMachineProfile::SaveStateSupport::Experimental },
    { "Yamaha PSR-11", "psr11", false, false, false, false, true, "midiin", nullptr, 0, nullptr,
      true, EmbeddedMachineProfile::SaveStateSupport::Experimental },
    { "Yamaha PSR-70", "psr70", false, false, true },
    { "Yamaha PSR-75", "psr75", false, false, false, false, true, "midiin", nullptr, 0, nullptr,
      true, EmbeddedMachineProfile::SaveStateSupport::Experimental },
    { "Yamaha PSR-76", "psr76", false, false, false, false, true, "midiin", nullptr, 0, nullptr,
      true, EmbeddedMachineProfile::SaveStateSupport::Experimental },
    { "Yamaha PSR-110", "psr110", false, false, false, false, true, "midiin", nullptr, 0, nullptr,
      true, EmbeddedMachineProfile::SaveStateSupport::Experimental },
    { "Yamaha PSR-150", "psr150", false, false, false, false, true, "midiin", nullptr, 0, nullptr,
      true, EmbeddedMachineProfile::SaveStateSupport::Experimental },
    { "Yamaha PSR-180", "psr180", false, false, false, false, true, "midiin", nullptr, 0, nullptr,
      true, EmbeddedMachineProfile::SaveStateSupport::Experimental },
    { "Yamaha PSR-60", "psr60", false, false, true, true },
    { "Yamaha PSS-6", "pss6", false, false, false, false, true, "midiin", nullptr, 0, nullptr,
      true, EmbeddedMachineProfile::SaveStateSupport::Experimental },
    { "Yamaha PSS-11", "pss11", false, false, false, false, true, "midiin", nullptr, 0, nullptr,
      true, EmbeddedMachineProfile::SaveStateSupport::Experimental },
    { "Yamaha PSS-12", "pss12", false, false, false, false, true, "midiin", nullptr, 0, nullptr,
      true, EmbeddedMachineProfile::SaveStateSupport::Experimental },
    { "Yamaha PSS-21", "pss21", false, false, false, false, true, "midiin", nullptr, 0, nullptr,
      true, EmbeddedMachineProfile::SaveStateSupport::Experimental },
    { "Yamaha PSS-31", "pss31", false, false, false, false, true, "midiin", nullptr, 0, nullptr,
      true, EmbeddedMachineProfile::SaveStateSupport::Experimental },
    { "Ensoniq SD-1 32 Voice", "sd132", false, false, true, true, false, nullptr, ensoniqFloppyMediaDevices, static_cast<int> (std::size (ensoniqFloppyMediaDevices)) },
};

constexpr int machineProfileCount = static_cast<int> (sizeof (machineProfiles) / sizeof (machineProfiles[0]));

constexpr const char* defaultMachineDriverName = "tx81z";

// This order is intentionally frozen: it maps numeric states written before
// selectedMachineDriver became the persistent identity.
constexpr const char* legacyMachineDriverNames[] {
    "ap10", "tx81z", "fb01", "rz1", "ct8000", "ctfk1", "cz101", "cz1", "cz230s", "esq1", "vfx",
    "vfxsd", "linndrum", "tr707", "sixtrak", "dd9", "dx100", "tx81z", "mu50", "mu2000", "psr60", "sd132"
};

static_assert (std::size (legacyMachineDriverNames) == 22);

const EmbeddedMachineProfile* findMachineProfileByDriverName (const juce::String& driverName)
{
    for (const auto& profile : machineProfiles)
        if (driverName == profile.driverName)
            return &profile;

    return nullptr;
}

bool profileSupportsMediaType (const EmbeddedMachineProfile* profile, EmbeddedMachineProfile::MediaType type)
{
    if (profile == nullptr)
        return false;

    for (int i = 0; i < profile->mediaDeviceCount; ++i)
        if (profile->mediaDevices[i].type == type)
            return true;
    return false;
}

const EmbeddedMachineProfile::MediaDeviceProfile* profileMediaDevice (const EmbeddedMachineProfile* profile,
                                                                       EmbeddedMachineProfile::MediaType type)
{
    if (profile == nullptr)
        return nullptr;
    for (int i = 0; i < profile->mediaDeviceCount; ++i)
        if (profile->mediaDevices[i].type == type)
            return &profile->mediaDevices[i];
    return nullptr;
}

const char* mediaStatePropertyForType (EmbeddedMachineProfile::MediaType type)
{
    switch (type)
    {
        case EmbeddedMachineProfile::MediaType::Floppy: return floppyMediaPathProperty;
        case EmbeddedMachineProfile::MediaType::CdRom:  return cdRomMediaPathProperty;
        case EmbeddedMachineProfile::MediaType::HardDisk: return hardDiskMediaPathProperty;
        default:                                         return "";
    }
}

const EmbeddedMachineProfile& defaultMachineProfile()
{
    if (const auto* profile = findMachineProfileByDriverName (defaultMachineDriverName))
        return *profile;

    return machineProfiles[0];
}

const std::vector<int>& visibleMachineProfileDisplayOrder()
{
    static const auto displayOrder = []
    {
        std::vector<int> indices;
        indices.reserve (machineProfileCount);
        for (int i = 0; i < machineProfileCount; ++i)
            if (machineProfiles[i].visibleInSelector)
                indices.push_back (i);

        std::sort (indices.begin(), indices.end(), [] (int lhs, int rhs)
        {
            const auto displayComparison = juce::String (machineProfiles[lhs].displayName)
                .compareNatural (machineProfiles[rhs].displayName, false);
            if (displayComparison != 0)
                return displayComparison < 0;

            return juce::String (machineProfiles[lhs].driverName)
                .compareIgnoreCase (machineProfiles[rhs].driverName) < 0;
        });
        return indices;
    }();

    return displayOrder;
}

const EmbeddedMachineProfile& machineProfileForDisplayPosition (int displayPosition)
{
    const auto& displayOrder = visibleMachineProfileDisplayOrder();

    return machineProfiles[displayOrder[static_cast<std::size_t> (juce::jlimit (0, static_cast<int> (displayOrder.size()) - 1, displayPosition))]];
}

bool hasPrimaryRom (const juce::File& romsDirectory, const EmbeddedMachineProfile& profile)
{
    const juce::String driverName (profile.driverName);
    return romsDirectory.getChildFile (driverName + ".zip").existsAsFile()
        || romsDirectory.getChildFile (driverName + ".7z").existsAsFile()
        || romsDirectory.getChildFile (driverName).isDirectory();
}

bool hasValidPackagedResources (const juce::File& directory)
{
    const auto plugins = directory.getChildFile ("plugins");
    const auto artwork = directory.getChildFile ("artwork");
    return plugins.getChildFile ("boot.lua").existsAsFile()
        && plugins.getChildFile ("layout").getChildFile ("init.lua").existsAsFile()
        && artwork.isDirectory()
        && artwork.getNumberOfChildFiles (juce::File::findFilesAndDirectories) > 0;
}

juce::File standaloneRuntimeResourcesRoot()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("VintageEmulatorStudio")
        .getChildFile ("runtime")
        .getChildFile ("resources");
}

bool zipEntryPathIsSafe (const juce::String& path)
{
    if (path.isEmpty() || path.startsWithChar ('/') || path.startsWithChar ('\\'))
        return false;

    if (path.contains (":"))
        return false;

    const auto normalised = path.replaceCharacter ('\\', '/');
    juce::StringArray parts;
    parts.addTokens (normalised, "/", {});

    for (const auto& part : parts)
        if (part == "..")
            return false;

    return true;
}

bool zipHasExpectedRuntimePayload (const juce::ZipFile& zip)
{
    bool hasBootLua = false;
    bool hasLayoutLua = false;
    bool hasArtwork = false;

    for (int i = 0; i < zip.getNumEntries(); ++i)
    {
        const auto* entry = zip.getEntry (i);
        if (entry == nullptr)
            return false;

        const auto filename = entry->filename.replaceCharacter ('\\', '/');
        if (! zipEntryPathIsSafe (filename) || entry->isSymbolicLink)
            return false;

        if (filename == "plugins/boot.lua")
            hasBootLua = true;
        else if (filename == "plugins/layout/init.lua")
            hasLayoutLua = true;
        else if (filename.startsWith ("artwork/") && ! filename.endsWithChar ('/'))
            hasArtwork = true;
    }

    return hasBootLua && hasLayoutLua && hasArtwork;
}

bool isRuntimeResourceStagingDirectoryName (const juce::String& name)
{
    return name.startsWith (".extracting-");
}

void cleanupAbandonedStandaloneRuntimeResourceStagingDirectories (const juce::File& activeDirectory)
{
    const auto root = activeDirectory.getParentDirectory();
    for (const auto& entry : juce::RangedDirectoryIterator (root, false, "*", juce::File::findDirectories))
    {
        const auto directory = entry.getFile();
        if (directory != activeDirectory && isRuntimeResourceStagingDirectoryName (directory.getFileName()))
            directory.deleteRecursively();
    }
}

RuntimeResourceResolution resolveEmbeddedStandaloneRuntimeResourcesDirectory()
{
#if (JUCE_WINDOWS || JUCE_LINUX)
    const auto payload = ves::standalone_resources::getPayload();
    if (payload.zipData == nullptr || payload.zipSize == 0 || payload.sha256 == nullptr || payload.sha256[0] == '\0')
        return {};

    const auto root = standaloneRuntimeResourcesRoot();
    const auto payloadDirectory = root.getChildFile (payload.sha256);
    const auto readyMarker = payloadDirectory.getChildFile (runtimeResourcesReadyMarker);
    if (readyMarker.existsAsFile() && hasValidPackagedResources (payloadDirectory))
        return { payloadDirectory, {} };

    juce::InterProcessLock lock (runtimeResourcesLockName);
    if (! lock.enter())
    {
        juce::Logger::writeToLog ("VintageEmulatorStudio could not acquire runtime resources extraction lock");
        return { {}, makeRuntimeResourcesDiagnostic ("Could not acquire runtime resources extraction lock: " + juce::String (runtimeResourcesLockName)) };
    }

    struct LockExit
    {
        juce::InterProcessLock& lock;
        ~LockExit() { lock.exit(); }
    } lockExit { lock };

    if (readyMarker.existsAsFile() && hasValidPackagedResources (payloadDirectory))
        return { payloadDirectory, {} };

    if (! root.createDirectory())
    {
        juce::Logger::writeToLog ("VintageEmulatorStudio could not create runtime resources directory: " + root.getFullPathName());
        return { {}, makeRuntimeResourcesDiagnostic ("Could not create runtime resources directory: " + root.getFullPathName()) };
    }

    juce::MemoryInputStream zipStream (payload.zipData, payload.zipSize, false);
    juce::ZipFile zip (zipStream);
    if (! zipHasExpectedRuntimePayload (zip))
    {
        juce::Logger::writeToLog ("VintageEmulatorStudio embedded runtime resources ZIP failed validation");
        return { {}, makeRuntimeResourcesDiagnostic ("Embedded runtime resources ZIP failed validation.") };
    }

    const auto stagingDirectory = root.getChildFile (".extracting-" + juce::String (payload.sha256)
        + "-" + juce::String::toHexString (static_cast<juce::int64> (juce::Time::getHighResolutionTicks())));
    stagingDirectory.deleteRecursively();
    if (! stagingDirectory.createDirectory())
    {
        juce::Logger::writeToLog ("VintageEmulatorStudio could not create runtime resources staging directory: " + stagingDirectory.getFullPathName());
        return { {}, makeRuntimeResourcesDiagnostic ("Could not create runtime resources staging directory: " + stagingDirectory.getFullPathName()) };
    }

    const auto result = zip.uncompressTo (stagingDirectory, true);
    if (result.failed())
    {
        juce::Logger::writeToLog ("VintageEmulatorStudio could not extract embedded runtime resources: " + result.getErrorMessage());
        stagingDirectory.deleteRecursively();
        return { {}, makeRuntimeResourcesDiagnostic ("Could not extract embedded runtime resources to " + stagingDirectory.getFullPathName()
            + ": " + result.getErrorMessage()) };
    }

    if (! hasValidPackagedResources (stagingDirectory))
    {
        juce::Logger::writeToLog ("VintageEmulatorStudio extracted runtime resources are incomplete");
        stagingDirectory.deleteRecursively();
        return { {}, makeRuntimeResourcesDiagnostic ("Extracted runtime resources are incomplete: " + stagingDirectory.getFullPathName()) };
    }

    if (payloadDirectory.exists())
        payloadDirectory.deleteRecursively();

    if (! stagingDirectory.moveFileTo (payloadDirectory))
    {
        juce::Logger::writeToLog ("VintageEmulatorStudio could not activate extracted runtime resources: " + payloadDirectory.getFullPathName());
        stagingDirectory.deleteRecursively();
        return { {}, makeRuntimeResourcesDiagnostic ("Could not activate extracted runtime resources: " + payloadDirectory.getFullPathName()) };
    }

    if (! payloadDirectory.getChildFile (runtimeResourcesReadyMarker).replaceWithText (juce::String (payload.sha256) + "\n"))
    {
        juce::Logger::writeToLog ("VintageEmulatorStudio could not mark runtime resources ready");
        payloadDirectory.deleteRecursively();
        return { {}, makeRuntimeResourcesDiagnostic ("Could not mark runtime resources ready: " + readyMarker.getFullPathName()) };
    }

    cleanupAbandonedStandaloneRuntimeResourceStagingDirectories (payloadDirectory);
    return { payloadDirectory, {} };
#else
    return {};
#endif
}

RuntimeResourceResolution resolvePackagedResourcesDirectory()
{
    const auto module = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
#if JUCE_MAC
    return { module.getParentDirectory().getParentDirectory().getChildFile ("Resources"), {} };
#elif JUCE_WINDOWS
    if (module.hasFileExtension (".vst3"))
        return { module.getParentDirectory().getParentDirectory().getChildFile ("Resources"), {} };

   #if JucePlugin_Build_Standalone
    const auto embeddedResources = resolveEmbeddedStandaloneRuntimeResourcesDirectory();
    if (embeddedResources.directory.isDirectory() || hasStartupDiagnostic (embeddedResources.diagnostic))
        return embeddedResources;
   #endif

   #ifndef NDEBUG
    return { module.getParentDirectory().getChildFile ("Resources"), {} };
   #else
    return { {}, makeRuntimeResourcesDiagnostic ("Embedded standalone runtime resources are unavailable.") };
   #endif
#elif JUCE_LINUX
   #if JucePlugin_Build_Standalone
    const auto embeddedResources = resolveEmbeddedStandaloneRuntimeResourcesDirectory();
    if (embeddedResources.directory.isDirectory() || hasStartupDiagnostic (embeddedResources.diagnostic))
        return embeddedResources;
   #endif

    const auto vst3Resources = module.getParentDirectory()
        .getParentDirectory()
        .getChildFile ("Resources");
    if (vst3Resources.isDirectory())
        return { vst3Resources, {} };

   #ifndef NDEBUG
    const auto standaloneResources = module.getParentDirectory().getChildFile ("Resources");
    return { standaloneResources.isDirectory() ? standaloneResources : juce::File {}, {} };
   #else
    return { {}, makeRuntimeResourcesDiagnostic ("Embedded standalone runtime resources are unavailable.") };
   #endif
#else
    return { module.getParentDirectory().getChildFile ("Resources"), {} };
#endif
}

juce::File resolveMamePluginsDirectory()
{
    const auto bundled = resolvePackagedResourcesDirectory().directory.getChildFile ("plugins");
    if (bundled.getChildFile ("boot.lua").existsAsFile()
        && bundled.getChildFile ("layout").getChildFile ("init.lua").existsAsFile())
        return bundled;

    return {};
}

juce::File resolveMameArtworkDirectory()
{
    const auto hasArtworkResources = [] (const juce::File& directory)
    {
        return directory.isDirectory()
            && directory.getNumberOfChildFiles (juce::File::findFilesAndDirectories) > 0;
    };

    const auto bundled = resolvePackagedResourcesDirectory().directory.getChildFile ("artwork");
    if (hasArtworkResources (bundled))
        return bundled;

    return {};
}

uint64_t nowMs()
{
    return static_cast<uint64_t> (juce::Time::getMillisecondCounterHiRes());
}

juce::File lifecycleLogFile()
{
    return juce::File::getSpecialLocation (juce::File::userHomeDirectory)
        .getChildFile ("Library/Logs/VES-lifecycle.log");
}

void appendLifecycleLog (const juce::String& line)
{
    const auto file = lifecycleLogFile();
    file.getParentDirectory().createDirectory();
    file.appendText (line + "\n", false, false);
}

}

namespace ves::standalone_resources
{
void registerPayload (Payload payload)
{
    standaloneRuntimeResourcesPayload = payload;
}

Payload getPayload()
{
#if JUCE_WINDOWS
    static const auto windowsResourcePayload = loadWindowsStandaloneRuntimeResourcesPayload();
    if (windowsResourcePayload.zipData != nullptr)
        return windowsResourcePayload;
#endif

    return standaloneRuntimeResourcesPayload;
}
}

VintageEmulatorStudioProcessor::VintageEmulatorStudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{

    // Initialise the process-shared, cross-format Recent Media store before any editor opens.
    (void) globalRecentMediaPreferences();
    backgroundColourArgb.store (globalAppearancePreferences().loadBackgroundColour().getARGB(), std::memory_order_release);
    configuredRomsPath = loadPersistedRomsPath();
    configuredArtworkPath = loadPersistedArtworkPath();
    guiPerformanceMode.store (static_cast<int> (loadPersistedGuiPerformanceMode()), std::memory_order_release);
#if JucePlugin_Build_Standalone
    standaloneMasterVolume.store (loadPersistedStandaloneMasterVolume(), std::memory_order_release);
    standaloneMasterGain.setCurrentAndTargetValue (standaloneMasterVolume.load (std::memory_order_acquire));
#endif
    startTimerHz (20);
}

VintageEmulatorStudioProcessor::~VintageEmulatorStudioProcessor()
{

    stopTimer();
    saveStandaloneAutosaveOnShutdown();
    stopEngine ("~VintageEmulatorStudioProcessor", "processor_destructor");
}

void VintageEmulatorStudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // AudioProcessorPlayer calls this only after the active JUCE device is open.
    // Recreate the embedded run only when the device sample rate changes so
    // MAME's fixed internal rate matches the device-selected rate.  Buffer-size
    // changes only affect host-side queue tuning and diagnostics.
    const auto newSampleRate = sampleRate > 0.0 ? sampleRate : 0.0;
    const auto newBlockSize = juce::jmax (samplesPerBlock, 0);
    const auto oldSampleRate = currentSampleRate;
    const auto sampleRateChanged = oldSampleRate != newSampleRate;
    const auto engineExists = std::atomic_load (&engine) != nullptr;
    const auto mayReuseExistingEngine = engineExists && ! sampleRateChanged;

    currentSampleRate = newSampleRate;
    maxBlockSize = newBlockSize;
    if (sampleRateChanged && engineExists)
        stopEngine ("prepareToPlay", "sample_rate_changed");

    startEngineIfNeeded (currentSampleRate, "prepareToPlay", engineExists ? "after_prepare" : "initial_prepare");
    if (auto localEngine = std::atomic_load (&engine))
    {
        localEngine->noteHostAudioConfiguration (currentSampleRate, maxBlockSize);
        if (mayReuseExistingEngine)
            localEngine->discardQueuedAudio();
    }

#if JucePlugin_Build_Standalone
    standaloneMasterGain.reset (currentSampleRate > 0.0 ? currentSampleRate : 44100.0, 0.02);
    standaloneMasterGain.setCurrentAndTargetValue (standaloneMasterVolume.load (std::memory_order_acquire));
#endif
}

void VintageEmulatorStudioProcessor::releaseResources()
{
    // JUCE has stopped/suspended its callback before this is invoked.  Keep the
    // emulated machine alive and remember the last valid sample rate so a
    // buffer-size-only device reconfiguration can resume without rebooting.
    if (auto localEngine = std::atomic_load (&engine))
        localEngine->discardQueuedAudio();
}

bool VintageEmulatorStudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
        || layouts.getMainOutputChannelSet() == juce::AudioChannelSet::mono();
}

void VintageEmulatorStudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    const auto samples = buffer.getNumSamples();

    buffer.clear();

    auto localEngine = std::atomic_load (&engine);
    if (localEngine == nullptr)
        return;

    updateBootState();

    const bool ready = state.load (std::memory_order_relaxed) == static_cast<int> (EmbeddedEngineState::Ready);
    if (experimentalStateRestoreInProgress.load (std::memory_order_acquire))
        return;
    if (ready)
    {
        trimAudioBacklog (*localEngine);

        bool transportPlaying = false;
        if (const auto* playHead = getPlayHead())
            if (const auto position = playHead->getPosition())
                transportPlaying = position->getIsPlaying();
        const bool wasPlaying = lastTransportPlaying.exchange (transportPlaying, std::memory_order_relaxed);
        if (transportPlaying != wasPlaying)
            flushAudioForTransportBoundary (*localEngine);
    }

    for (const auto metadata : midiMessages)
    {
        const auto message = metadata.getMessage();
        const auto size = message.getRawDataSize();
        if (size <= 0)
            continue;

        if (! ready)
            continue;

        trackHostMidiMessage (message);
        localEngine->sendMidiBytes (message.getRawData(), static_cast<std::size_t> (size));
    }

    if (! ready)
        return;

    const auto channels = buffer.getNumChannels();
    auto* const leftChannel = channels > 0 ? buffer.getWritePointer (0) : nullptr;
    auto* const rightChannel = channels > 1 ? buffer.getWritePointer (1) : nullptr;
    int offset = 0;
    while (offset < samples)
    {
        const auto request = static_cast<std::size_t> (juce::jmin (samples - offset, static_cast<int> (audioScratch.size())));
        const auto read = localEngine->readAudioFrames (audioScratch.data(), request);
        for (std::size_t i = 0; i < read; ++i)
        {
            const auto targetSample = offset + static_cast<int> (i);
            if (leftChannel != nullptr)
                leftChannel[targetSample] = audioScratch[i].left;
            if (rightChannel != nullptr)
                rightChannel[targetSample] = audioScratch[i].right;
        }

        if (read < request)
            break;

        offset += static_cast<int> (read);
    }

#if JucePlugin_Build_Standalone
    standaloneMasterGain.setTargetValue (standaloneMasterVolume.load (std::memory_order_acquire));
    if (standaloneMasterGain.isSmoothing())
    {
        auto* const standaloneLeftChannel = channels > 0 ? buffer.getWritePointer (0) : nullptr;
        auto* const standaloneRightChannel = channels > 1 ? buffer.getWritePointer (1) : nullptr;
        for (int sample = 0; sample < samples; ++sample)
        {
            const auto gain = standaloneMasterGain.getNextValue();
            if (standaloneLeftChannel != nullptr)
                standaloneLeftChannel[sample] *= gain;
            if (standaloneRightChannel != nullptr)
                standaloneRightChannel[sample] *= gain;
        }
    }
    else
    {
        buffer.applyGain (standaloneMasterGain.getCurrentValue());
    }
#endif
}

juce::AudioProcessorEditor* VintageEmulatorStudioProcessor::createEditor()
{

    return new VintageEmulatorStudioEditor (*this);
}

void VintageEmulatorStudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree stateTree ("VintageEmulatorStudioEmbeddedState");
    stateTree.setProperty ("selectedMachineDriver", getSelectedMachineDriverName(), nullptr);
    stateTree.setProperty ("editorWidth", editorWidth.load (std::memory_order_relaxed), nullptr);
    stateTree.setProperty ("editorHeight", editorHeight.load (std::memory_order_relaxed), nullptr);
    stateTree.setProperty (romDirectoryProperty, configuredRomsPath, nullptr);
    stateTree.setProperty (artworkDirectoryProperty, configuredArtworkPath, nullptr);
    stateTree.setProperty (floppyMediaPathProperty, getSelectedMediaPath(), nullptr);
    stateTree.setProperty (cdRomMediaPathProperty, getSelectedCdRomPath(), nullptr);
    stateTree.setProperty (hardDiskMediaPathProperty, getSelectedHardDiskPath(), nullptr);

    juce::MemoryBlock xmlState;
    if (auto xml = stateTree.createXml())
        copyXmlToBinary (*xml, xmlState);

    std::shared_ptr<const std::vector<std::uint8_t>> cachedSnapshot;
    juce::String cachedDriver;
    ves::HeldMidiNotes cachedHeldMidiNotes {};
    if (isDawPluginWrapper() && selectedMachineSupportsSaveState())
    {
        const juce::ScopedLock lock (dawStateLock);
        const auto generation = videoEngineGeneration.load (std::memory_order_acquire);
        const auto selectedDriver = getSelectedMachineDriverName();
        if (dawCachedSnapshot.blob != nullptr && ! dawCachedSnapshot.blob->empty()
            && dawCachedSnapshot.driver == selectedDriver
            && dawCachedSnapshot.engineGeneration == generation
            && dawCachedSnapshot.compatibilityRevision == experimentalStateFormatVersion)
        {
            cachedSnapshot = dawCachedSnapshot.blob;
            cachedDriver = dawCachedSnapshot.driver;
            cachedHeldMidiNotes = dawCachedSnapshot.heldMidiNotes;
        }
    }

    if (cachedSnapshot != nullptr)
        writeDawStatePayload (destData, xmlState, cachedDriver, *cachedSnapshot, cachedHeldMidiNotes);
    else
        destData = xmlState;

}

void VintageEmulatorStudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{

    auto dawPayload = readDawStatePayload (data, sizeInBytes);
    {
        const juce::ScopedLock lock (dawStateLock);
        dawPendingRestoreSnapshot.clear();
        dawPendingRestoreDriver.clear();
        dawPendingRestoreCompatibilityRevision = 0;
        dawPendingRestoreHeldMidiNotes.fill (0);
        dawPendingRestoreArmed = false;
    }

    if (auto xml = getXmlFromBinary (dawPayload.xmlState.getData(), static_cast<int> (dawPayload.xmlState.getSize())))
    {
        const auto stateTree = juce::ValueTree::fromXml (*xml);
        if (stateTree.hasType ("VintageEmulatorStudioEmbeddedState")
            || stateTree.hasType ("MAMESynthsEmbeddedState"))
        {
            editorWidth.store (juce::jlimit (800, 2400, static_cast<int> (stateTree.getProperty ("editorWidth", 1700))), std::memory_order_relaxed);
            editorHeight.store (juce::jlimit (520, 1600, static_cast<int> (stateTree.getProperty ("editorHeight", 1100))), std::memory_order_relaxed);
            const auto previousDriver = getSelectedMachineDriverName();
            const auto previousRomsPath = configuredRomsPath;
            const auto previousArtworkPath = configuredArtworkPath;
            const auto previousMediaPaths = configuredMediaPaths;
            auto restoredRomsPath = stateTree.getProperty (romDirectoryProperty,
                                                            stateTree.getProperty (legacyRomPathProperty, {})).toString();
            if (juce::File (restoredRomsPath).isDirectory())
            {
                configuredRomsPath = restoredRomsPath;
                persistRomsPath();
            }
            if (stateTree.hasProperty (artworkDirectoryProperty))
            {
                const auto restoredArtworkPath = stateTree.getProperty (artworkDirectoryProperty).toString();
                if (juce::File (restoredArtworkPath).isDirectory())
                {
                    configuredArtworkPath = restoredArtworkPath;
                }
                else
                {
                    configuredArtworkPath.clear();
                    if (restoredArtworkPath.isNotEmpty())
                        juce::Logger::writeToLog ("VintageEmulatorStudio custom artwork folder is unavailable; using bundled/default artwork");
                }
                persistArtworkPath();
            }
            if (stateTree.hasProperty (floppyMediaPathProperty))
            {
                configuredMediaPaths[floppyMediaPathProperty] = stateTree.getProperty (floppyMediaPathProperty).toString();
                configuredMediaPathOrigins[floppyMediaPathProperty] = isDawPluginWrapper()
                    ? "restored_plugin_state" : "restored_standalone_state";
            }
            else
            {
                // Migrate the original S3000XL-only assignment without requiring users to reselect it.
                const auto legacyKey = getMediaStateKey ("s3000xl", "floppydisk");
                if (stateTree.hasProperty (legacyKey))
                {
                    configuredMediaPaths[floppyMediaPathProperty] = stateTree.getProperty (legacyKey).toString();
                    configuredMediaPathOrigins[floppyMediaPathProperty] = "legacy_persisted_state";
                }
            }
            if (stateTree.hasProperty (cdRomMediaPathProperty))
            {
                configuredMediaPaths[cdRomMediaPathProperty] = stateTree.getProperty (cdRomMediaPathProperty).toString();
                configuredMediaPathOrigins[cdRomMediaPathProperty] = isDawPluginWrapper()
                    ? "restored_plugin_state" : "restored_standalone_state";
            }
            if (stateTree.hasProperty (hardDiskMediaPathProperty))
            {
                configuredMediaPaths[hardDiskMediaPathProperty] = stateTree.getProperty (hardDiskMediaPathProperty).toString();
                configuredMediaPathOrigins[hardDiskMediaPathProperty] = isDawPluginWrapper()
                    ? "restored_plugin_state" : "restored_standalone_state";
            }
            std::array<std::vector<juce::String>, 3> legacyRecentPaths;
            readRecentMediaPaths (stateTree, RecentMediaType::Floppy,
                                  legacyRecentPaths[static_cast<std::size_t> (recentMediaIndex (RecentMediaType::Floppy))]);
            readRecentMediaPaths (stateTree, RecentMediaType::CdRom,
                                  legacyRecentPaths[static_cast<std::size_t> (recentMediaIndex (RecentMediaType::CdRom))]);
            readRecentMediaPaths (stateTree, RecentMediaType::HardDisk,
                                  legacyRecentPaths[static_cast<std::size_t> (recentMediaIndex (RecentMediaType::HardDisk))]);
            globalRecentMediaPreferences().migrateLegacyState (legacyRecentPaths);
            auto restoredDriver = stateTree.getProperty ("selectedMachineDriver", {}).toString();
            if (findMachineProfileByDriverName (restoredDriver) == nullptr)
            {
                const auto legacyIndexProperty = stateTree.hasProperty ("selectedMachineIndex")
                    ? stateTree.getProperty ("selectedMachineIndex")
                    : stateTree.getProperty ("machine", -1);
                const auto legacyIndex = static_cast<int> (legacyIndexProperty);
                restoredDriver = legacyIndex >= 0 && legacyIndex < static_cast<int> (std::size (legacyMachineDriverNames))
                    ? legacyMachineDriverNames[legacyIndex]
                    : defaultMachineDriverName;
            }

            const auto mediaPathFor = [] (const std::map<juce::String, juce::String>& paths, const char* key)
            {
                const auto it = paths.find (key);
                return it != paths.end() ? it->second : juce::String {};
            };
            const auto previousFloppyPath = mediaPathFor (previousMediaPaths, floppyMediaPathProperty);
            const auto restoredFloppyPath = mediaPathFor (configuredMediaPaths, floppyMediaPathProperty);
            const auto previousCdRomPath = mediaPathFor (previousMediaPaths, cdRomMediaPathProperty);
            const auto restoredCdRomPath = mediaPathFor (configuredMediaPaths, cdRomMediaPathProperty);
            const auto previousHardDiskPath = mediaPathFor (previousMediaPaths, hardDiskMediaPathProperty);
            const auto restoredHardDiskPath = mediaPathFor (configuredMediaPaths, hardDiskMediaPathProperty);
            const auto machineChanged = restoredDriver != previousDriver;
            const auto romPathChanged = configuredRomsPath != previousRomsPath;
            const auto artworkPathChanged = configuredArtworkPath != previousArtworkPath;
            const auto floppyPathChanged = restoredFloppyPath != previousFloppyPath;
            const auto cdRomPathChanged = restoredCdRomPath != previousCdRomPath;
            const auto hardDiskPathChanged = restoredHardDiskPath != previousHardDiskPath;
            const auto mediaChanged = floppyPathChanged || cdRomPathChanged || hardDiskPathChanged;
            const auto changed = machineChanged || romPathChanged || artworkPathChanged || mediaChanged;

            {
                const juce::ScopedLock lock (machineSelectionLock);
                selectedMachineDriverName = restoredDriver;
            }
            const auto* restoredProfile = findMachineProfileByDriverName (restoredDriver);
            const bool saveStatePermitted = restoredProfile != nullptr
                && restoredProfile->saveStateSupport != EmbeddedMachineProfile::SaveStateSupport::Unsupported;
            const bool armDawRestore = isDawPluginWrapper() && saveStatePermitted
                && dawPayload.snapshotAccepted && dawPayload.driver == restoredDriver;
            if (armDawRestore)
            {
                const juce::ScopedLock lock (dawStateLock);
                dawPendingRestoreSnapshot = std::move (dawPayload.snapshot);
                dawPendingRestoreDriver = dawPayload.driver;
                dawPendingRestoreCompatibilityRevision = dawPayload.compatibilityRevision;
                dawPendingRestoreHeldMidiNotes = dawPayload.heldMidiNotes;
                dawPendingRestoreArmed = true;
                experimentalStateStatus = "DAW session state accepted; waiting for normal boot";
            }
            else if (dawPayload.containerFound && isDawPluginWrapper())
            {
                experimentalStateStatus = "DAW session state ignored: "
                    + (dawPayload.snapshotAccepted && dawPayload.driver != restoredDriver
                        ? juce::String ("rejected: metadata/XML driver mismatch") : dawPayload.validationResult);
            }
            if (changed)
            {
                juce::StringArray reasons;
                if (machineChanged) reasons.add ("selected_machine_changed");
                if (romPathChanged) reasons.add ("rom_path_changed");
                if (artworkPathChanged) reasons.add ("artwork_path_changed");
                if (floppyPathChanged) reasons.add ("floppy_media_changed");
                if (cdRomPathChanged) reasons.add ("cdrom_media_changed");
                if (hardDiskPathChanged) reasons.add ("harddisk_media_changed");
                restartSelectedMachine ("setStateInformation", reasons.joinIntoString ("|"));
            }
            else
            {

            }

            stateRestorationRevision.fetch_add (1, std::memory_order_release);
        }
        else
        {

        }
    }
    else
    {

    }
}

void VintageEmulatorStudioProcessor::startEngineIfNeeded (double sampleRate, const juce::String& caller, const juce::String& reason)
{
    // StandalonePluginHolder restores processor state before attaching the
    // processor to its already-open audio device.  Never boot before that
    // prepareToPlay callback supplies the actual device sample rate.
    if (sampleRate <= 0.0 || maxBlockSize <= 0)
    {

        return;
    }

    if (std::atomic_load (&engine) != nullptr)
    {

        return;
    }

    lastError.clear();
    clearStartupDiagnostic();
    bootStartMs.store (0, std::memory_order_relaxed);
    const auto selectedDriver = getSelectedMachineDriverName();
    const auto& profile = findMachineProfileByDriverName (selectedDriver) != nullptr
                            ? *findMachineProfileByDriverName (selectedDriver)
                            : defaultMachineProfile();
    state.store (static_cast<int> (EmbeddedEngineState::Starting), std::memory_order_relaxed);

    auto roms = getRomsDirectory();
    if (! hasPrimaryRom (roms, profile))
    {
        lastError = "Missing primary ROM set for " + juce::String (profile.driverName) + " in " + roms.getFullPathName();
        StartupDiagnostic diagnostic;
        diagnostic.category = StartupError::PrimaryRomMissing;
        diagnostic.summary = "The selected machine ROM was not found.";
        diagnostic.details = "Missing primary ROM set for " + juce::String (profile.driverName) + " in " + roms.getFullPathName();
        diagnostic.recovery = "Please add the ROM ZIP/file for this machine to your VES ROMs folder.";
        diagnostic.technicalDetails = lastError;
        setStartupDiagnostic (std::move (diagnostic));
        state.store (static_cast<int> (EmbeddedEngineState::Failed), std::memory_order_relaxed);
        return;
    }

    const auto packagedResources = resolvePackagedResourcesDirectory();
    if (hasStartupDiagnostic (packagedResources.diagnostic))
    {
        setStartupDiagnostic (packagedResources.diagnostic);
        lastError = packagedResources.diagnostic.summary;
        state.store (static_cast<int> (EmbeddedEngineState::Failed), std::memory_order_relaxed);
        return;
    }

    const auto pluginsDir = packagedResources.directory.getChildFile ("plugins");
    juce::StringArray artworkSearchPaths;
    if (configuredArtworkPath.isNotEmpty())
        artworkSearchPaths.add (configuredArtworkPath);

    const auto bundledArtwork = packagedResources.directory.getChildFile ("artwork");
    if (bundledArtwork.isDirectory()
        && bundledArtwork.getNumberOfChildFiles (juce::File::findFilesAndDirectories) > 0)
        artworkSearchPaths.addIfNotAlreadyThere (bundledArtwork.getFullPathName());

    const auto artworkPath = artworkSearchPaths.joinIntoString (";");
    std::vector<ves::EmbeddedEmulatorEngineSettings::StartupMediaOption> startupMediaOptions;
    juce::StringArray unavailableMedia;
    for (int i = 0; i < profile.mediaDeviceCount; ++i)
    {
        const auto& media = profile.mediaDevices[i];
        const auto key = juce::String (mediaStatePropertyForType (media.type));
        if (key.isEmpty())
            continue;
        const auto it = configuredMediaPaths.find (key);
        if (it == configuredMediaPaths.end() || it->second.isEmpty())
            continue;

        // The field is intentionally opaque: MAME validates the image at load time.
        const juce::File mediaFile (it->second);
        const auto mediaReadable = mediaFile.existsAsFile() && mediaFile.createInputStream() != nullptr;
        const auto mediaType = media.type == EmbeddedMachineProfile::MediaType::Floppy ? "floppy"
                             : media.type == EmbeddedMachineProfile::MediaType::CdRom ? "cdrom"
                             : "harddisk";
        if (! mediaReadable)
            unavailableMedia.add (juce::String (mediaType).toUpperCase() + ": " + it->second
                                  + "\nFile unavailable.");
        startupMediaOptions.push_back ({ media.mameOptionName, it->second.toStdString() });
    }
    if (! unavailableMedia.isEmpty())
    {
        StartupDiagnostic diagnostic;
        diagnostic.category = StartupError::MediaUnavailable;
        diagnostic.summary = "Media unavailable";
        diagnostic.details = "The configured floppy, CD-ROM or hard disk image could not be accessed.\n\n"
            + unavailableMedia.joinIntoString ("\n");
        diagnostic.recovery = "Check that the drive containing the media file is connected and that the configured image still exists.";
        diagnostic.technicalDetails = "Configured startup media was unavailable before MAME launch.";
        setStartupDiagnostic (std::move (diagnostic));
        lastError = "Configured startup media unavailable for " + selectedDriver;
        state.store (static_cast<int> (EmbeddedEngineState::Failed), std::memory_order_relaxed);
        appendLifecycleLog ("[VES startup] event=MEDIA_UNAVAILABLE driver=" + selectedDriver
            + " count=" + juce::String (unavailableMedia.size()));
        return;
    }
    if (! pluginsDir.isDirectory())
    {
        StartupDiagnostic diagnostic = makeRuntimeResourcesDiagnostic ("MAME plugins directory containing boot.lua and layout/init.lua was not available: "
            + pluginsDir.getFullPathName());
        lastError = diagnostic.summary;
        setStartupDiagnostic (std::move (diagnostic));
        state.store (static_cast<int> (EmbeddedEngineState::Failed), std::memory_order_relaxed);
        return;
    }
    if (! pluginsDir.getChildFile ("boot.lua").existsAsFile()
        || ! pluginsDir.getChildFile ("layout").getChildFile ("init.lua").existsAsFile()
        || artworkPath.isEmpty())
    {
        StartupDiagnostic diagnostic = makeRuntimeResourcesDiagnostic ("Packaged runtime resources failed startup validation: "
            + packagedResources.directory.getFullPathName());
        lastError = diagnostic.summary;
        setStartupDiagnostic (std::move (diagnostic));
        state.store (static_cast<int> (EmbeddedEngineState::Failed), std::memory_order_relaxed);
        return;
    }

    const auto transientRootDir = juce::File::getSpecialLocation (juce::File::tempDirectory)
        .getChildFile ("VintageEmulatorStudio")
        .getChildFile ("instance-" + juce::String::toHexString (static_cast<juce::int64> (reinterpret_cast<std::uintptr_t> (this))));
    transientRootDir.deleteRecursively();
    const auto transientCfgDir = transientRootDir.getChildFile ("cfg");
    const auto transientNvramDir = transientRootDir.getChildFile ("nvram");
    if (! transientCfgDir.createDirectory() || ! transientNvramDir.createDirectory())
    {
        lastError = "Could not create per-instance MAME runtime directory: " + transientRootDir.getFullPathName();
        StartupDiagnostic diagnostic;
        diagnostic.category = StartupError::Configuration;
        diagnostic.summary = "VES could not create its per-user runtime files.";
        diagnostic.recovery = "Check that your temporary directory is writable and has enough free space.";
        diagnostic.technicalDetails = lastError;
        setStartupDiagnostic (std::move (diagnostic));
        state.store (static_cast<int> (EmbeddedEngineState::Failed), std::memory_order_relaxed);
        return;
    }

    if (! prepareNvramState (transientNvramDir))
    {
        state.store (static_cast<int> (EmbeddedEngineState::Failed), std::memory_order_relaxed);
        return;
    }

    const auto nativeMidiInputOption = profile.nativeMidiOptions != nullptr
        ? profile.nativeMidiOptions->inputOptionName
        : (profile.nativeMidiIn ? "midiin" : "");
    const auto nativeMidiOutputOption = profile.nativeMidiOptions != nullptr
        ? profile.nativeMidiOptions->outputOptionName
        : (profile.nativeMidiOut ? "midiout" : "");
    const auto engineGeneration = videoEngineGeneration.load (std::memory_order_acquire) + 1;

    auto newEngine = std::make_shared<ves::EmbeddedEmulatorEngine> (ves::EmbeddedEmulatorEngineSettings {
        roms.getFullPathName().toStdString(),
        transientCfgDir.getFullPathName().toStdString(),
        transientNvramDir.getFullPathName().toStdString(),
        pluginsDir.getFullPathName().toStdString(),
        artworkPath.toStdString(),
        profile.driverName,
        static_cast<int> (std::round (sampleRate)),
        nativeMidiInputOption,
        nativeMidiOutputOption,
        profile.retrofitMidiInputOptionName != nullptr ? profile.retrofitMidiInputOptionName : "",
        std::move (startupMediaOptions),
        engineGeneration,
        profile.saveStateSupport != EmbeddedMachineProfile::SaveStateSupport::Unsupported
    });

    newEngine->requestVideoCaptureWidth (getEffectiveVideoCaptureWidth (requestedVideoCaptureWidth.load (std::memory_order_acquire)));
    newEngine->start();
    newEngine->noteHostAudioConfiguration (sampleRate, maxBlockSize);
    std::atomic_store (&engine, std::move (newEngine));
    applyGuiPerformanceModeToEngine();
    videoEngineGeneration.fetch_add (1, std::memory_order_acq_rel);
    bootStartMs.store (nowMs(), std::memory_order_relaxed);
    state.store (static_cast<int> (EmbeddedEngineState::Booting), std::memory_order_relaxed);

}

void VintageEmulatorStudioProcessor::stopEngine (const juce::String& caller, const juce::String& reason)
{
    floppyHotSwapPending.store (false, std::memory_order_release);
    floppyHotSwapMessage.clear();
    experimentalStatePending.store (false, std::memory_order_release);
    experimentalStateRestoreInProgress.store (false, std::memory_order_release);
    experimentalStateStatus.clear();
    auto oldEngine = std::atomic_exchange (&engine, std::shared_ptr<ves::EmbeddedEmulatorEngine> {});
    if (oldEngine != nullptr)
    {

        state.store (static_cast<int> (EmbeddedEngineState::Stopping), std::memory_order_relaxed);
        oldEngine->stopAndJoin (std::chrono::seconds (5));
    }
    state.store (static_cast<int> (EmbeddedEngineState::Stopped), std::memory_order_relaxed);
}

void VintageEmulatorStudioProcessor::restartSelectedMachine (const juce::String& caller, const juce::String& reason)
{

    videoEngineGeneration.fetch_add (1, std::memory_order_acq_rel);
    stopEngine ("restartSelectedMachine", reason);
    if (currentSampleRate > 0.0 && maxBlockSize > 0)
        startEngineIfNeeded (currentSampleRate, "restartSelectedMachine", reason);
}

bool VintageEmulatorStudioProcessor::prepareNvramState (const juce::File& runtimeNvramDirectory)
{
    const auto selectedDriver = getSelectedMachineDriverName();
    const auto& profile = findMachineProfileByDriverName (selectedDriver) != nullptr
                            ? *findMachineProfileByDriverName (selectedDriver)
                            : defaultMachineProfile();
    if (! profile.requiresSeededNvram)
        return true;

    auto nvramDir = runtimeNvramDirectory.getChildFile (selectedDriver);
    if (! nvramDir.createDirectory())
    {
        lastError = "Could not create NVRAM directory: " + nvramDir.getFullPathName();
        StartupDiagnostic diagnostic;
        diagnostic.category = StartupError::Nvram;
        diagnostic.summary = "VES could not create its per-machine NVRAM directory.";
        diagnostic.recovery = "Check that your user application data folder is writable and has enough free space.";
        diagnostic.technicalDetails = lastError;
        setStartupDiagnostic (std::move (diagnostic));
        return false;
    }

    auto destination = nvramDir.getChildFile ("nvram");
    const auto seed = getNvramSeedFile();
    if (! destination.existsAsFile() && seed.existsAsFile() && ! seed.copyFileTo (destination))
    {
        lastError = "Could not copy required NVRAM seed";
        StartupDiagnostic diagnostic;
        diagnostic.category = StartupError::Nvram;
        diagnostic.summary = "VES could not prepare the machine NVRAM state.";
        diagnostic.recovery = "Check that your user application data folder is writable and has enough free space.";
        diagnostic.technicalDetails = lastError + ": " + destination.getFullPathName();
        setStartupDiagnostic (std::move (diagnostic));
        return false;
    }

    // TX81Z's MAME driver registers its NVRAM with DEFAULT_ALL_0.  An available
    // seed is therefore an optional migration input, not a prerequisite for a
    // clean first boot.  MAME creates the initial in-memory NVRAM state when no
    // previously initialized file is available.

    return true;
}

juce::File VintageEmulatorStudioProcessor::getRomsDirectory() const
{
    if (configuredRomsPath.isNotEmpty())
    {
        const juce::File configured (configuredRomsPath);
        if (configured.isDirectory())
            return configured;
    }

    auto appSupport = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("VintageEmulatorStudio")
        .getChildFile ("roms");
    return appSupport;
}

juce::File VintageEmulatorStudioProcessor::getNvramSeedFile() const
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("VintageEmulatorStudio")
        .getChildFile ("Embedded")
        .getChildFile ("seed")
        .getChildFile (getSelectedMachineDriverName())
        .getChildFile ("nvram");
}

juce::File VintageEmulatorStudioProcessor::getPluginDataDirectory() const
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("VintageEmulatorStudio")
        .getChildFile ("Embedded");
}

juce::String VintageEmulatorStudioProcessor::loadPersistedRomsPath() const
{
    const auto settings = romPathSettingsFile();
    if (! settings.existsAsFile())
        return {};

    const auto path = settings.loadFileAsString().trim();
    return juce::File (path).isDirectory() ? path : juce::String();
}

void VintageEmulatorStudioProcessor::persistRomsPath() const
{
    if (! juce::File (configuredRomsPath).isDirectory())
        return;

    const auto settings = romPathSettingsFile();
    settings.getParentDirectory().createDirectory();
    settings.replaceWithText (configuredRomsPath + "\n");
}

juce::String VintageEmulatorStudioProcessor::loadPersistedArtworkPath() const
{
    const auto settings = artworkPathSettingsFile();
    if (! settings.existsAsFile())
        return {};

    const auto path = settings.loadFileAsString().trim();
    if (path.isEmpty() || juce::File (path).isDirectory())
        return path;

    juce::Logger::writeToLog ("VintageEmulatorStudio custom artwork folder is unavailable; using bundled/default artwork");
    return {};
}

void VintageEmulatorStudioProcessor::persistArtworkPath() const
{
    if (configuredArtworkPath.isNotEmpty() && ! juce::File (configuredArtworkPath).isDirectory())
        return;

    const auto settings = artworkPathSettingsFile();
    if (settings.getParentDirectory().createDirectory())
        settings.replaceWithText (configuredArtworkPath + "\n");
}

GuiPerformanceMode VintageEmulatorStudioProcessor::loadPersistedGuiPerformanceMode() const
{
    juce::PropertiesFile::Options options;
    options.applicationName = "VintageEmulatorStudio";
    options.filenameSuffix = "settings";
    options.folderName = "VintageEmulatorStudio";
    options.osxLibrarySubFolder = "Application Support";
    options.storageFormat = juce::PropertiesFile::storeAsXML;
    options.ignoreCaseOfKeyNames = false;
    options.millisecondsBeforeSaving = 0;

    juce::ApplicationProperties properties;
    properties.setStorageParameters (options);
    auto* settings = properties.getUserSettings();
    return settings != nullptr ? guiPerformanceModeFromString (settings->getValue (guiPerformanceModePreferenceKey, "Normal"))
                               : GuiPerformanceMode::Normal;
}

void VintageEmulatorStudioProcessor::persistGuiPerformanceMode() const
{
    juce::PropertiesFile::Options options;
    options.applicationName = "VintageEmulatorStudio";
    options.filenameSuffix = "settings";
    options.folderName = "VintageEmulatorStudio";
    options.osxLibrarySubFolder = "Application Support";
    options.storageFormat = juce::PropertiesFile::storeAsXML;
    options.ignoreCaseOfKeyNames = false;
    options.millisecondsBeforeSaving = 0;

    juce::ApplicationProperties properties;
    properties.setStorageParameters (options);
    auto* settings = properties.getUserSettings();
    if (settings == nullptr)
        return;

    settings->setValue (guiPerformanceModePreferenceKey,
                        guiPerformanceModeToString (getGuiPerformanceMode()));
    if (! settings->save())
        juce::Logger::writeToLog ("VintageEmulatorStudio could not save GUI performance mode preference");
}

#if JucePlugin_Build_Standalone
float VintageEmulatorStudioProcessor::loadPersistedStandaloneMasterVolume() const
{
    juce::PropertiesFile::Options options;
    options.applicationName = "VintageEmulatorStudio";
    options.filenameSuffix = "settings";
    options.folderName = "VintageEmulatorStudio";
    options.osxLibrarySubFolder = "Application Support";
    options.storageFormat = juce::PropertiesFile::storeAsXML;
    options.ignoreCaseOfKeyNames = false;
    options.millisecondsBeforeSaving = 0;

    juce::ApplicationProperties properties;
    properties.setStorageParameters (options);
    auto* settings = properties.getUserSettings();
    return settings != nullptr ? juce::jlimit (0.0f, 1.0f, static_cast<float> (settings->getDoubleValue (standaloneMasterVolumePreferenceKey, 1.0)))
                               : 1.0f;
}

void VintageEmulatorStudioProcessor::persistStandaloneMasterVolume() const
{
    juce::PropertiesFile::Options options;
    options.applicationName = "VintageEmulatorStudio";
    options.filenameSuffix = "settings";
    options.folderName = "VintageEmulatorStudio";
    options.osxLibrarySubFolder = "Application Support";
    options.storageFormat = juce::PropertiesFile::storeAsXML;
    options.ignoreCaseOfKeyNames = false;
    options.millisecondsBeforeSaving = 0;

    juce::ApplicationProperties properties;
    properties.setStorageParameters (options);
    auto* settings = properties.getUserSettings();
    if (settings == nullptr)
        return;

    settings->setValue (standaloneMasterVolumePreferenceKey,
                        static_cast<double> (getStandaloneMasterVolume()));
    if (! settings->save())
        juce::Logger::writeToLog ("VintageEmulatorStudio could not save standalone master volume preference");
}
#endif

int VintageEmulatorStudioProcessor::getEffectiveVideoCaptureWidth (int requestedWidth) const
{
    const auto mode = getGuiPerformanceMode();
    const auto minimum = mode == GuiPerformanceMode::Normal ? 1792
                         : mode == GuiPerformanceMode::Reduced ? 1280
                         : mode == GuiPerformanceMode::Static ? 1536 : 512;
    const auto maximum = mode == GuiPerformanceMode::Normal ? 1792
                         : mode == GuiPerformanceMode::Reduced ? 1280
                         : mode == GuiPerformanceMode::Static ? 1536 : 1024;
    return juce::jlimit (minimum, maximum, ((juce::jmax (requestedWidth, 1) + 63) / 64) * 64);
}

void VintageEmulatorStudioProcessor::applyGuiPerformanceModeToEngine()
{
    auto localEngine = std::atomic_load (&engine);
    if (localEngine == nullptr)
        return;

    const auto mode = getGuiPerformanceMode();
    const auto intervalMs = mode == GuiPerformanceMode::Reduced ? 1000
                            : mode == GuiPerformanceMode::Normal ? 250 : 200;
    const auto requestedWidth = getEffectiveVideoCaptureWidth (requestedVideoCaptureWidth.load (std::memory_order_acquire));
    localEngine->setVideoCaptureIntervalMs (intervalMs);
    localEngine->requestVideoCaptureWidth (requestedWidth);
    localEngine->setVideoCaptureSingleFrame (mode == GuiPerformanceMode::Static);
    localEngine->setVideoCaptureEnabled (mode != GuiPerformanceMode::Disabled);
    localEngine->setVideoDisplayActive (mode != GuiPerformanceMode::Disabled);

}

juce::String VintageEmulatorStudioProcessor::getEffectiveMameArtworkPath() const
{
    // MAME's path_iterator uses ';' for multipath options on every platform.
    juce::StringArray searchPaths;
    // The picker and state restoration validate this path before storing it.
    // Preserve that exact selected root for MAME rather than re-validating it
    // during engine startup and accidentally dropping it from -artpath.
    if (configuredArtworkPath.isNotEmpty())
        searchPaths.add (configuredArtworkPath);

    const auto bundledArtwork = resolveMameArtworkDirectory();
    if (bundledArtwork.isDirectory())
        searchPaths.addIfNotAlreadyThere (bundledArtwork.getFullPathName());

    return searchPaths.joinIntoString (";");
}

void VintageEmulatorStudioProcessor::updateBootState()
{
    constexpr uint64_t startupTimeoutMs = 30000;

    if (state.load (std::memory_order_relaxed) != static_cast<int> (EmbeddedEngineState::Booting))
        return;

    if (auto localEngine = std::atomic_load (&engine))
    {
        const auto& diag = localEngine->diagnostics();
        if (diag.machine_exited.load (std::memory_order_relaxed) != 0)
        {
            auto engineDiagnostic = convertEmbeddedStartupDiagnostic (localEngine->startupDiagnostic());
            if (! hasStartupDiagnostic (engineDiagnostic))
            {
                engineDiagnostic.category = StartupError::EngineFailure;
                engineDiagnostic.summary = "The emulator failed to start.";
                engineDiagnostic.technicalDetails = "MAME exited before the first video frame (result "
                    + juce::String (localEngine->mameResult()) + ")";
            }

            lastError = engineDiagnostic.summary;
            setStartupDiagnostic (std::move (engineDiagnostic));
            state.store (static_cast<int> (EmbeddedEngineState::Failed), std::memory_order_relaxed);
            return;
        }

        const auto selectedDriver = getSelectedMachineDriverName();
        const auto* profile = findMachineProfileByDriverName (selectedDriver);
        const bool midiRequiredForReady = profile != nullptr
            && (profile->nativeMidiIn
                || profile->usesVirtualMidiRetrofit
                || (profile->nativeMidiOptions != nullptr
                    && juce::String (profile->nativeMidiOptions->inputOptionName).isNotEmpty())
                || (profile->retrofitMidiInputOptionName != nullptr
                    && juce::String (profile->retrofitMidiInputOptionName).isNotEmpty()));
        const bool generationMatches = localEngine->engineGeneration()
            == videoEngineGeneration.load (std::memory_order_acquire);
        const bool driverMatches = juce::String (localEngine->driverName()) == selectedDriver;
        const bool machineRunning = diag.machine_running.load (std::memory_order_acquire);
        const bool schedulerReady = diag.normal_scheduler_iterations.load (std::memory_order_acquire) != 0;
        const bool videoInitialized = diag.video_initialized.load (std::memory_order_acquire);
        const bool audioReady = diag.audio_sink_open_count.load (std::memory_order_acquire) != 0;
        const bool midiReady = ! midiRequiredForReady
            || diag.midi_input_open_count.load (std::memory_order_acquire) != 0;
        const bool videoRequiredForReady = getGuiPerformanceMode() != GuiPerformanceMode::Disabled
            && diag.video_capture_enabled.load (std::memory_order_acquire)
            && diag.video_editor_display_active.load (std::memory_order_acquire);
        const bool videoReady = ! videoRequiredForReady
            || (diag.video_render_target_available.load (std::memory_order_acquire)
                && diag.video_frames_produced.load (std::memory_order_acquire) != 0);
        const auto engineDiagnostic = convertEmbeddedStartupDiagnostic (localEngine->startupDiagnostic());
        const bool startupHealthy = ! hasStartupDiagnostic (engineDiagnostic);
        const bool ready = generationMatches
            && driverMatches
            && diag.machine_started.load (std::memory_order_acquire) != 0
            && machineRunning
            && schedulerReady
            && videoInitialized
            && diag.machine_exited.load (std::memory_order_acquire) == 0
            && startupHealthy
            && audioReady
            && midiReady
            && videoReady;
        if (ready)
        {
            handleReadyTransition (*localEngine);
            state.store (static_cast<int> (EmbeddedEngineState::Ready), std::memory_order_relaxed);
            return;
        }

        if (getBootElapsedMs() < startupTimeoutMs)
            return;

        juce::StringArray missing;
        if (! generationMatches) missing.add ("engine generation mismatch");
        if (! driverMatches) missing.add ("driver mismatch");
        if (diag.machine_started.load (std::memory_order_acquire) == 0) missing.add ("machine did not start");
        if (! machineRunning) missing.add ("machine never reached RUNNING");
        if (! schedulerReady) missing.add ("no completed normal scheduler iteration");
        if (! videoInitialized) missing.add ("video/OSD initialization incomplete");
        if (! audioReady) missing.add ("audio provider not opened");
        if (! midiReady) missing.add ("required MIDI input provider not opened");
        if (videoRequiredForReady && ! diag.video_render_target_available.load (std::memory_order_acquire))
            missing.add ("render target unavailable");
        if (videoRequiredForReady && diag.video_frames_produced.load (std::memory_order_acquire) == 0)
            missing.add ("no published video frame");
        if (! startupHealthy) missing.add ("startup diagnostic reported a failure");

        StartupDiagnostic diagnostic;
        diagnostic.category = StartupError::StartupTimeout;
        diagnostic.summary = "The emulator did not become ready during startup.";
        diagnostic.technicalDetails = "Readiness timeout after " + juce::String (startupTimeoutMs / 1000)
            + " seconds. Missing: " + missing.joinIntoString (", ");
        lastError = diagnostic.summary;
        setStartupDiagnostic (std::move (diagnostic));
        state.store (static_cast<int> (EmbeddedEngineState::Failed), std::memory_order_relaxed);
        return;
    }
}

void VintageEmulatorStudioProcessor::handleReadyTransition (ves::EmbeddedEmulatorEngine& localEngine)
{
    auto& diag = localEngine.diagnostics();
    const auto queued = localEngine.queuedAudioFrames();
    const auto sampleRate = diag.mame_sample_rate.load (std::memory_order_relaxed);
    const auto latencyUs = sampleRate != 0 ? (static_cast<uint64_t> (queued) * 1000000ULL) / sampleRate : 0;
    const auto stale = localEngine.discardQueuedAudio();

    diag.boot_ready_queued_frames.store (static_cast<uint64_t> (queued), std::memory_order_relaxed);
    diag.boot_ready_queued_latency_us.store (latencyUs, std::memory_order_relaxed);
    diag.boot_ready_stale_frames.store (static_cast<uint64_t> (stale), std::memory_order_relaxed);
    diag.boot_ready_flush_count.fetch_add (1, std::memory_order_relaxed);
    diag.audio_underruns.store (0, std::memory_order_relaxed);
    localEngine.resetLatencyDiagnostics();
    diag.audio_overflows.store (0, std::memory_order_relaxed);
    waitingForMidiAudioOnset.store (false, std::memory_order_relaxed);
    clearStartupDiagnostic();
    sendMidiPanic ("startup", &localEngine);
}

void VintageEmulatorStudioProcessor::sendMidiPanic()
{
    sendMidiPanic ("user");
}

void VintageEmulatorStudioProcessor::sendMidiPanic (const char*,
                                                     ves::EmbeddedEmulatorEngine* readyEngine,
                                                     const ves::HeldMidiNotes* snapshotHeldNotes)
{
    auto localEngine = std::atomic_load (&engine);
    if (localEngine == nullptr
        || (getEngineState() != EmbeddedEngineState::Ready && localEngine.get() != readyEngine))
        return;

    waitingForMidiAudioOnset.store (false, std::memory_order_release);
    lastMidiSentToEngineMs.store (0, std::memory_order_release);
    const auto heldNotes = snapshotHeldNotes != nullptr ? *snapshotHeldNotes : captureActiveMidiNotes();
    clearActiveMidiNotes();
    localEngine->sendMidiPanic (heldNotes);
}

void VintageEmulatorStudioProcessor::trackHostMidiMessage (const juce::MidiMessage& message)
{
    const auto* data = message.getRawData();
    if (data == nullptr || message.getRawDataSize() < 3)
        return;
    const auto status = data[0] & 0xf0;
    if (status != 0x80 && status != 0x90)
        return;
    const auto channel = data[0] & 0x0f;
    const auto note = data[1] & 0x7f;
    const auto index = static_cast<std::size_t> (channel) * 2 + (note >> 6);
    const auto mask = std::uint64_t { 1 } << (note & 63);
    if (status == 0x90 && data[2] != 0)
        activeHostMidiNotes[index].fetch_or (mask, std::memory_order_release);
    else
        activeHostMidiNotes[index].fetch_and (~mask, std::memory_order_release);
}

ves::HeldMidiNotes VintageEmulatorStudioProcessor::captureActiveMidiNotes() const
{
    ves::HeldMidiNotes notes {};
    for (std::size_t i = 0; i < notes.size(); ++i)
        notes[i] = activeHostMidiNotes[i].load (std::memory_order_acquire);
    return notes;
}

void VintageEmulatorStudioProcessor::clearActiveMidiNotes()
{
    for (auto& word : activeHostMidiNotes)
        word.store (0, std::memory_order_release);
}

void VintageEmulatorStudioProcessor::trimAudioBacklog (ves::EmbeddedEmulatorEngine& localEngine)
{
    auto& diag = localEngine.diagnostics();
    const auto queued = localEngine.queuedAudioFrames();
    const auto maxTolerated = static_cast<std::size_t> (diag.audio_max_tolerated_queue_frames.load (std::memory_order_relaxed));
    const auto target = static_cast<std::size_t> (diag.audio_target_queue_frames.load (std::memory_order_relaxed));

    if (queued > maxTolerated && queued > target)
    {
        const auto trimmed = localEngine.discardOldestAudioFrames (queued - target);
        if (trimmed != 0)
            diag.audio_trimmed_frames.fetch_add (static_cast<uint64_t> (trimmed), std::memory_order_relaxed);
    }
}

void VintageEmulatorStudioProcessor::flushAudioForTransportBoundary (ves::EmbeddedEmulatorEngine& localEngine)
{
    const auto queued = localEngine.queuedAudioFrames();
    const auto target = static_cast<std::size_t> (localEngine.diagnostics().audio_target_queue_frames.load (std::memory_order_relaxed));
    if (queued > target)
        localEngine.discardOldestAudioFrames (queued - target);
}

EmbeddedEngineState VintageEmulatorStudioProcessor::getEngineState() const
{
    return static_cast<EmbeddedEngineState> (state.load (std::memory_order_relaxed));
}

juce::String VintageEmulatorStudioProcessor::getEngineStateText() const
{
    switch (getEngineState())
    {
        case EmbeddedEngineState::Stopped: return "Stopped";
        case EmbeddedEngineState::Starting: return "Starting";
        case EmbeddedEngineState::Booting: return "Booting";
        case EmbeddedEngineState::Ready: return "Ready";
        case EmbeddedEngineState::Stopping: return "Stopping";
        case EmbeddedEngineState::Failed: return "Failed";
    }
    return "Unknown";
}

juce::String VintageEmulatorStudioProcessor::getLastError() const
{
    return lastError;
}

void VintageEmulatorStudioProcessor::clearStartupDiagnostic()
{
    const juce::ScopedLock lock (startupDiagnosticLock);
    startupDiagnostic = {};
}

void VintageEmulatorStudioProcessor::setStartupDiagnostic (StartupDiagnostic diagnostic)
{
    const juce::ScopedLock lock (startupDiagnosticLock);
    startupDiagnostic = std::move (diagnostic);
}

uint64_t VintageEmulatorStudioProcessor::getBootElapsedMs() const
{
    const auto start = bootStartMs.load (std::memory_order_relaxed);
    return start == 0 ? 0 : nowMs() - start;
}

bool VintageEmulatorStudioProcessor::isReady() const
{
    return getEngineState() == EmbeddedEngineState::Ready;
}

const ves::EngineDiagnostics* VintageEmulatorStudioProcessor::getEngineDiagnostics() const
{
    return engine != nullptr ? &engine->diagnostics() : nullptr;
}

EmbeddedDiagnosticSnapshot VintageEmulatorStudioProcessor::getDiagnosticSnapshot() const
{
    EmbeddedDiagnosticSnapshot snapshot;
    snapshot.engineState = getEngineState();
    snapshot.engineStateText = getEngineStateText();
    snapshot.machineName = getSelectedMachineName();
    snapshot.bootElapsedMs = getBootElapsedMs();
    snapshot.ready = snapshot.engineState == EmbeddedEngineState::Ready;
    snapshot.midiMessagesReceived = midiMessagesReceived.load (std::memory_order_relaxed);
    snapshot.midiDroppedBeforeReady = midiDroppedBeforeReady.load (std::memory_order_relaxed);
    snapshot.romPath = getRomsDirectory().getFullPathName();
    snapshot.selectedMachineRomFound = hasMachineRomByDriverName (getSelectedMachineDriverName());
    snapshot.nvramStatus = getNvramStatusText();
    snapshot.lastError = getLastError();
    snapshot.guiPerformanceMode = getGuiPerformanceMode();
    {
        const juce::ScopedLock lock (startupDiagnosticLock);
        snapshot.startupDiagnostic = startupDiagnostic;
    }

    auto localEngine = std::atomic_load (&engine);
    if (localEngine != nullptr)
    {
        const auto engineDiagnostic = convertEmbeddedStartupDiagnostic (localEngine->startupDiagnostic());
        if (hasStartupDiagnostic (engineDiagnostic))
            snapshot.startupDiagnostic = engineDiagnostic;

        const auto& diag = localEngine->diagnostics();
        snapshot.mameThreadRunning = diag.machine_started.load (std::memory_order_relaxed) != 0
            && diag.machine_exited.load (std::memory_order_relaxed) == 0;
        snapshot.hostSampleRate = diag.juce_sample_rate.load (std::memory_order_relaxed);
        snapshot.mameSampleRate = diag.mame_sample_rate.load (std::memory_order_relaxed);
        snapshot.currentQueuedAudioFrames = diag.audio_current_queued_frames.load (std::memory_order_relaxed);
        snapshot.queuedAudioLatencyMs = static_cast<double> (diag.audio_queued_latency_us.load (std::memory_order_relaxed)) / 1000.0;
        snapshot.maxQueuedAudioFrames = diag.audio_max_queued_frames.load (std::memory_order_relaxed);
        snapshot.maxQueuedAudioLatencyMs = static_cast<double> (diag.audio_max_queued_latency_us.load (std::memory_order_relaxed)) / 1000.0;
        snapshot.targetQueuedAudioFrames = diag.audio_target_queue_frames.load (std::memory_order_relaxed);
        snapshot.maxToleratedQueuedAudioFrames = diag.audio_max_tolerated_queue_frames.load (std::memory_order_relaxed);
        snapshot.trimmedAudioFrames = diag.audio_trimmed_frames.load (std::memory_order_relaxed);
        snapshot.midiBytesQueued = diag.midi_bytes_queued.load (std::memory_order_relaxed);
        snapshot.midiBytesConsumed = diag.midi_bytes_consumed.load (std::memory_order_relaxed);
        snapshot.audioFramesProduced = diag.audio_frames_written.load (std::memory_order_relaxed);
        snapshot.audioFramesConsumed = diag.audio_frames_read.load (std::memory_order_relaxed);
        snapshot.staleAudioFramesDiscarded = diag.audio_stale_frames_dropped.load (std::memory_order_relaxed);
        snapshot.audioUnderruns = diag.audio_underruns.load (std::memory_order_relaxed);
        snapshot.audioUnderrunCallbacks = diag.audio_underrun_callbacks.load (std::memory_order_relaxed);
        snapshot.audioMissingOutputFrames = diag.audio_missing_output_frames.load (std::memory_order_relaxed);
        snapshot.audioOverflows = diag.audio_overflows.load (std::memory_order_relaxed);
        const auto lowWatermark = diag.audio_queue_read_low_watermark_frames.load (std::memory_order_relaxed);
        snapshot.audioQueueReadLowWatermarkFrames = lowWatermark == UINT64_MAX ? 0 : lowWatermark;
        snapshot.audioSinkLatestInterarrivalUs = diag.audio_sink_latest_interarrival_us.load (std::memory_order_relaxed);
        snapshot.audioSinkMaxInterarrivalUs = diag.audio_sink_max_interarrival_us.load (std::memory_order_relaxed);
        snapshot.audioSinkLatestBlockFrames = diag.audio_sink_latest_block_frames.load (std::memory_order_relaxed);
        const auto minimumSinkBlock = diag.audio_sink_min_block_frames.load (std::memory_order_relaxed);
        snapshot.audioSinkMinBlockFrames = minimumSinkBlock == UINT64_MAX ? 0 : minimumSinkBlock;
        snapshot.audioSinkMaxBlockFrames = diag.audio_sink_max_block_frames.load (std::memory_order_relaxed);
        snapshot.audioSinkRecentMaxBlockFrames = diag.audio_sink_recent_max_block_frames.load (std::memory_order_relaxed);
        snapshot.audioSinkBlocksApprox240 = diag.audio_sink_blocks_approx_240.load (std::memory_order_relaxed);
        snapshot.audioSinkBlocksApprox480 = diag.audio_sink_blocks_approx_480.load (std::memory_order_relaxed);
        snapshot.audioSinkBlocksApprox720 = diag.audio_sink_blocks_approx_720.load (std::memory_order_relaxed);
        snapshot.audioSinkBlocks960Plus = diag.audio_sink_blocks_960_plus.load (std::memory_order_relaxed);
        snapshot.audioSinkBlocksOther = diag.audio_sink_blocks_other.load (std::memory_order_relaxed);
        snapshot.audioPeak = diag.peak_abs.load (std::memory_order_relaxed);
        snapshot.midiToAudioOnsetMs = diag.midi_to_audio_onset_ms.load (std::memory_order_relaxed);
        snapshot.bootReadyQueuedFrames = diag.boot_ready_queued_frames.load (std::memory_order_relaxed);
        snapshot.bootReadyQueuedLatencyMs = static_cast<double> (diag.boot_ready_queued_latency_us.load (std::memory_order_relaxed)) / 1000.0;
        snapshot.videoInitialized = diag.video_initialized.load (std::memory_order_relaxed);
        snapshot.videoRenderTargetAvailable = diag.video_render_target_available.load (std::memory_order_relaxed);
        snapshot.videoFrameWidth = static_cast<int> (diag.video_frame_width.load (std::memory_order_relaxed));
        snapshot.videoFrameHeight = static_cast<int> (diag.video_frame_height.load (std::memory_order_relaxed));
        snapshot.videoSourceAspectRatio = static_cast<double> (diag.video_source_aspect_x1000.load (std::memory_order_relaxed)) / 1000.0;
        snapshot.videoFramesProduced = diag.video_frames_produced.load (std::memory_order_relaxed);
        snapshot.videoFramesDisplayed = diag.video_frames_displayed.load (std::memory_order_relaxed);
        snapshot.videoFramesDropped = diag.video_frames_dropped.load (std::memory_order_relaxed);
        snapshot.videoFramesReplaced = diag.video_frames_replaced.load (std::memory_order_relaxed);
        snapshot.videoFramesSkippedDeadline = diag.video_frames_skipped_deadline.load (std::memory_order_relaxed);
        snapshot.videoFramesSkippedInactive = diag.video_frames_skipped_inactive.load (std::memory_order_relaxed);
        snapshot.videoFramesSkippedPaused = diag.video_frames_skipped_paused.load (std::memory_order_relaxed);
        snapshot.videoFrameGeneration = diag.video_frame_generation.load (std::memory_order_relaxed);
        snapshot.videoCaptureRequested = diag.video_capture_requested.load (std::memory_order_relaxed);
        snapshot.videoCaptureStarted = diag.video_capture_started.load (std::memory_order_relaxed);
        snapshot.videoCaptureCompleted = diag.video_capture_completed.load (std::memory_order_relaxed);
        snapshot.videoCaptureInProgress = diag.video_capture_in_progress.load (std::memory_order_acquire);
        snapshot.videoRasterizationDurationUs = diag.video_rasterization_duration_us.load (std::memory_order_relaxed);
        snapshot.videoRasterizationTotalUs = diag.video_rasterization_total_us.load (std::memory_order_relaxed);
        snapshot.videoRasterizationMaxUs = diag.video_rasterization_max_us.load (std::memory_order_relaxed);
        snapshot.videoJobsSubmitted = diag.video_jobs_submitted.load (std::memory_order_relaxed);
        snapshot.videoJobsReplaced = diag.video_jobs_replaced.load (std::memory_order_relaxed);
        snapshot.videoJobsCompleted = diag.video_jobs_completed.load (std::memory_order_relaxed);
        snapshot.videoWorkerPendingDepth = diag.video_worker_pending_depth.load (std::memory_order_relaxed);
        snapshot.videoRasterError = diag.video_raster_error_code.load (std::memory_order_relaxed);
        snapshot.videoRasterErrorIndex = diag.video_raster_error_index.load (std::memory_order_relaxed);
        snapshot.videoTargetFrameRate = diag.video_target_frame_rate.load (std::memory_order_relaxed);
        switch (diag.video_state.load (std::memory_order_relaxed))
        {
            case 1: snapshot.videoState = "waiting for machine"; break;
            case 2: snapshot.videoState = "waiting for first frame"; break;
            case 3: snapshot.videoState = "running"; break;
            case 4: snapshot.videoState = "unavailable"; break;
            case 5: snapshot.videoState = "error"; break;
            default: snapshot.videoState = "disabled"; break;
        }
        snapshot.videoMeasuredFrameRate = static_cast<double> (diag.video_measured_frame_rate_x1000.load (std::memory_order_relaxed)) / 1000.0;
        snapshot.videoLastFrameTimestampMs = diag.video_last_frame_timestamp_ms.load (std::memory_order_relaxed);
        snapshot.videoCaptureEnabled = diag.video_capture_enabled.load (std::memory_order_relaxed);
        snapshot.videoEditorDisplayActive = diag.video_editor_display_active.load (std::memory_order_relaxed);
        snapshot.videoTargetFlags = diag.video_target_flags.load (std::memory_order_relaxed);
        snapshot.videoTargetGeneration = diag.video_target_generation.load (std::memory_order_relaxed);
        snapshot.videoTargetOrientation = diag.video_target_orientation.load (std::memory_order_relaxed);
        snapshot.videoTargetPixelAspect = static_cast<double> (diag.video_target_pixel_aspect_x1000.load (std::memory_order_relaxed)) / 1000.0;
        snapshot.videoSelectedView = snapshot.videoRenderTargetAvailable ? "selected" : juce::String();
        switch (diag.video_last_error_code.load (std::memory_order_relaxed))
        {
            case 0: break;
            case 2: snapshot.lastVideoError = "MAME render target creation failed"; break;
            case 3: snapshot.lastVideoError = "Unsupported MAME render primitive"; break;
            default: snapshot.lastVideoError = "MAME render target capture failed"; break;
        }
        snapshot.mouseForwardingEnabled = diag.mouse_forwarding_enabled.load (std::memory_order_relaxed);
        snapshot.mouseEventsEnqueued = diag.mouse_events_enqueued.load (std::memory_order_relaxed);
        snapshot.mouseEventsConsumed = diag.mouse_events_consumed.load (std::memory_order_relaxed);
        snapshot.mouseDroppedMoveEvents = diag.mouse_dropped_move_events.load (std::memory_order_relaxed);
        snapshot.mouseCriticalEventFailures = diag.mouse_critical_event_failures.load (std::memory_order_relaxed);
        snapshot.mouseLastError = diag.mouse_last_error.load (std::memory_order_relaxed);
        snapshot.mouseQueueHighWatermark = diag.mouse_queue_high_watermark.load (std::memory_order_relaxed);
        snapshot.mouseLastEventType = diag.mouse_last_event_type.load (std::memory_order_relaxed);
        snapshot.mouseCurrentX = diag.mouse_current_x.load (std::memory_order_relaxed);
        snapshot.mouseCurrentY = diag.mouse_current_y.load (std::memory_order_relaxed);
        snapshot.mouseLeftDown = diag.mouse_left_down.load (std::memory_order_relaxed);
        snapshot.mouseSyntheticReleaseCount = diag.mouse_synthetic_release_count.load (std::memory_order_relaxed);
        snapshot.mousePointerTargetIndex = diag.mouse_pointer_target_index.load (std::memory_order_relaxed);
        snapshot.mouseHitItem = diag.mouse_hit_item.load (std::memory_order_relaxed);
        snapshot.mouseHitInputTag = diag.mouse_hit_input_tag.load (std::memory_order_relaxed);
        snapshot.mouseHitInputMask = diag.mouse_hit_input_mask.load (std::memory_order_relaxed);
        snapshot.mouseInputFieldActive = diag.mouse_input_field_active.load (std::memory_order_relaxed);
    }

    return snapshot;
}

juce::File VintageEmulatorStudioProcessor::getExternalRomsDirectory() const
{
    return getRomsDirectory();
}

void VintageEmulatorStudioProcessor::setExternalRomsDirectory (const juce::File& directory)
{
    if (! directory.isDirectory())
        return;

    configuredRomsPath = directory.getFullPathName();
    persistRomsPath();
    restartSelectedMachine ("setExternalRomsDirectory", "rom_path_changed_by_user");
}

juce::File VintageEmulatorStudioProcessor::getExternalArtworkDirectory() const
{
    return juce::File (configuredArtworkPath);
}

juce::String VintageEmulatorStudioProcessor::getExternalArtworkDirectoryPath() const
{
    return configuredArtworkPath;
}

void VintageEmulatorStudioProcessor::setExternalArtworkDirectory (const juce::File& directory)
{
    if (! directory.isDirectory())
        return;

    const auto newPath = directory.getFullPathName();
    configuredArtworkPath = newPath;
    persistArtworkPath();
    // Re-selecting the same folder is an explicit reload action after its
    // layouts or referenced images have been edited.
    restartSelectedMachine ("setExternalArtworkDirectory", "artwork_path_changed_by_user");
}

void VintageEmulatorStudioProcessor::clearExternalArtworkDirectory()
{
    if (configuredArtworkPath.isEmpty())
        return;

    configuredArtworkPath.clear();
    persistArtworkPath();
    restartSelectedMachine ("clearExternalArtworkDirectory", "artwork_path_cleared_by_user");
}

bool VintageEmulatorStudioProcessor::hasSelectedMachineRom() const
{
    return hasMachineRomByDriverName (getSelectedMachineDriverName());
}

juce::String VintageEmulatorStudioProcessor::getSelectedMachineDriverName() const
{
    const juce::ScopedLock lock (machineSelectionLock);
    return selectedMachineDriverName;
}

juce::String VintageEmulatorStudioProcessor::getSelectedMachineName() const
{
    if (const auto* profile = findMachineProfileByDriverName (getSelectedMachineDriverName()))
        return profile->displayName;
    return defaultMachineProfile().displayName;
}

juce::String VintageEmulatorStudioProcessor::getMachineNameForDisplayPosition (int displayPosition) const
{
    return machineProfileForDisplayPosition (displayPosition).displayName;
}

juce::String VintageEmulatorStudioProcessor::getMachineDriverNameForDisplayPosition (int displayPosition) const
{
    return machineProfileForDisplayPosition (displayPosition).driverName;
}

bool VintageEmulatorStudioProcessor::hasMachineRomByDriverName (const juce::String& driverName) const
{
    if (const auto* profile = findMachineProfileByDriverName (driverName))
        return hasPrimaryRom (getRomsDirectory(), *profile);
    return false;
}

int VintageEmulatorStudioProcessor::getNumMachineProfiles() const
{
    return static_cast<int> (visibleMachineProfileDisplayOrder().size());
}

bool VintageEmulatorStudioProcessor::selectMachineByDriverName (const juce::String& driverName)
{
    const auto* requestedProfile = findMachineProfileByDriverName (driverName);
    if (requestedProfile == nullptr)
        return false;

    const auto selectedState = getEngineState();
    const auto sameDriver = driverName == getSelectedMachineDriverName();
    if (sameDriver && selectedState == EmbeddedEngineState::Ready)
        return true;

    if (wrapperType == wrapperType_Standalone && selectedState == EmbeddedEngineState::Ready
        && selectedMachineSupportsStandaloneAutosave()
        && ! experimentalStatePending.load (std::memory_order_acquire))
    {
        auto localEngine = std::atomic_load (&engine);
        if (localEngine != nullptr)
        {
            ves::StateOperationRequest request;
            request.operation = ves::StateOperation::Save;
            request.engine_generation = videoEngineGeneration.load (std::memory_order_acquire);
            request.request_id = experimentalStateRequestId.fetch_add (1, std::memory_order_relaxed) + 1;
            request.held_midi_notes = captureActiveMidiNotes();
            if (localEngine->requestStateOperation (request))
            {
                pendingStandaloneSwitchDriver = getSelectedMachineDriverName();
                pendingStandaloneSwitchTarget = driverName;
                experimentalStateTarget = ExperimentalStateTarget::StandaloneSwitchSave;
                experimentalStatePending.store (true, std::memory_order_release);
                experimentalStateStatus = "Saving state before machine switch...";
                return true;
            }
        }
    }

    {
        const juce::ScopedLock lock (machineSelectionLock);
        selectedMachineDriverName = driverName;
    }
    restartSelectedMachine ("selectMachineByDriverName", "selected_machine_changed_by_user");
    return true;
}

bool VintageEmulatorStudioProcessor::selectedMachineHasMedia() const
{
    if (const auto* profile = findMachineProfileByDriverName (getSelectedMachineDriverName()))
        return profile->mediaDeviceCount > 0;
    return false;
}

bool VintageEmulatorStudioProcessor::selectedMachineSupportsFloppy() const
{
    return profileSupportsMediaType (findMachineProfileByDriverName (getSelectedMachineDriverName()),
                                     EmbeddedMachineProfile::MediaType::Floppy);
}

bool VintageEmulatorStudioProcessor::selectedMachineSupportsFloppyHotSwap() const
{
    const auto* profile = findMachineProfileByDriverName (getSelectedMachineDriverName());
    const auto* device = profileMediaDevice (profile, EmbeddedMachineProfile::MediaType::Floppy);
    return device != nullptr && device->policy == EmbeddedMachineProfile::MediaChangePolicy::LiveChange;
}

bool VintageEmulatorStudioProcessor::selectedMachineSupportsCdRomHotSwap() const
{
    const auto* profile = findMachineProfileByDriverName (getSelectedMachineDriverName());
    const auto* device = profileMediaDevice (profile, EmbeddedMachineProfile::MediaType::CdRom);
    return device != nullptr && device->policy == EmbeddedMachineProfile::MediaChangePolicy::LiveChange;
}

bool VintageEmulatorStudioProcessor::selectedMachineSupportsCdRom() const
{
    return profileSupportsMediaType (findMachineProfileByDriverName (getSelectedMachineDriverName()),
                                     EmbeddedMachineProfile::MediaType::CdRom);
}

bool VintageEmulatorStudioProcessor::selectedMachineSupportsHardDisk() const
{
    return profileSupportsMediaType (findMachineProfileByDriverName (getSelectedMachineDriverName()),
                                     EmbeddedMachineProfile::MediaType::HardDisk);
}

juce::String VintageEmulatorStudioProcessor::getSelectedMediaDisplayName() const
{
    return "Floppy";
}

juce::String VintageEmulatorStudioProcessor::getMediaStateKey (const juce::String& driverName, const juce::String& instanceName) const
{
    return "media." + driverName + "." + instanceName + ".path";
}

juce::String VintageEmulatorStudioProcessor::getMediaPathForSelectedMachine() const
{
    if (const auto it = configuredMediaPaths.find (floppyMediaPathProperty); it != configuredMediaPaths.end())
        return it->second;
    return {};
}

juce::String VintageEmulatorStudioProcessor::getSelectedMediaPath() const
{
    return getMediaPathForSelectedMachine();
}

void VintageEmulatorStudioProcessor::setSelectedMediaFile (const juce::File& file)
{
    const auto* profile = findMachineProfileByDriverName (getSelectedMachineDriverName());
    if (! file.existsAsFile())
        return;

    configuredMediaPaths[floppyMediaPathProperty] = file.getFullPathName();
    configuredMediaPathOrigins[floppyMediaPathProperty] = "current_user_selection";
    addRecentMediaPath (RecentMediaType::Floppy, file);
    if (profileSupportsMediaType (profile, EmbeddedMachineProfile::MediaType::Floppy) && std::atomic_load (&engine) != nullptr)
        restartSelectedMachine ("setSelectedMediaFile", "floppy_media_changed_by_user");
}

void VintageEmulatorStudioProcessor::ejectSelectedMedia()
{
    const auto* profile = findMachineProfileByDriverName (getSelectedMachineDriverName());
    configuredMediaPaths[floppyMediaPathProperty].clear();
    configuredMediaPathOrigins[floppyMediaPathProperty] = "current_user_selection";
    if (profileSupportsMediaType (profile, EmbeddedMachineProfile::MediaType::Floppy) && std::atomic_load (&engine) != nullptr)
        restartSelectedMachine ("ejectSelectedMedia", "floppy_media_ejected_by_user");
}

bool VintageEmulatorStudioProcessor::requestSelectedFloppyHotSwap (const juce::File& file)
{
    if (! file.existsAsFile() || ! selectedMachineSupportsFloppyHotSwap())
        return false;

    if (getEngineState() == EmbeddedEngineState::Booting || getEngineState() == EmbeddedEngineState::Starting)
    {
        floppyHotSwapMessage = "Floppy can be changed after the machine is Ready.";
        return false;
    }

    if (getEngineState() != EmbeddedEngineState::Ready)
        return false;

    auto localEngine = std::atomic_load (&engine);
    if (localEngine == nullptr || floppyHotSwapPending.load (std::memory_order_acquire))
        return false;

    ves::FloppyChangeRequest request;
    request.engine_generation = videoEngineGeneration.load (std::memory_order_acquire);
    request.request_id = floppyHotSwapRequestId.fetch_add (1, std::memory_order_relaxed) + 1;
    request.brief_instance_name = "flop";
    request.path = file.getFullPathName().toStdString();
    if (! localEngine->requestFloppyChange (request))
        return false;

    floppyHotSwapPending.store (true, std::memory_order_release);
    floppyHotSwapMessage = "Loading " + file.getFileName() + "...";
    return true;
}

bool VintageEmulatorStudioProcessor::requestSelectedFloppyEject()
{
    if (! selectedMachineSupportsFloppyHotSwap())
        return false;

    if (getEngineState() == EmbeddedEngineState::Booting || getEngineState() == EmbeddedEngineState::Starting)
    {
        floppyHotSwapMessage = "Floppy can be changed after the machine is Ready.";
        return false;
    }

    if (getEngineState() != EmbeddedEngineState::Ready)
        return false;

    auto localEngine = std::atomic_load (&engine);
    if (localEngine == nullptr || floppyHotSwapPending.load (std::memory_order_acquire))
        return false;

    ves::FloppyChangeRequest request;
    request.engine_generation = videoEngineGeneration.load (std::memory_order_acquire);
    request.request_id = floppyHotSwapRequestId.fetch_add (1, std::memory_order_relaxed) + 1;
    request.brief_instance_name = "flop";
    if (! localEngine->requestFloppyChange (request))
        return false;

    floppyHotSwapPending.store (true, std::memory_order_release);
    floppyHotSwapMessage = "Ejecting floppy...";
    return true;
}

bool VintageEmulatorStudioProcessor::requestSelectedCdRomHotSwap (const juce::File& file)
{
    if (! file.existsAsFile() || ! selectedMachineSupportsCdRomHotSwap())
        return false;
    if (getEngineState() == EmbeddedEngineState::Booting || getEngineState() == EmbeddedEngineState::Starting)
    {
        floppyHotSwapMessage = "CD-ROM can be changed after the machine is Ready.";
        return false;
    }
    if (getEngineState() != EmbeddedEngineState::Ready)
        return false;

    auto localEngine = std::atomic_load (&engine);
    if (localEngine == nullptr || floppyHotSwapPending.load (std::memory_order_acquire))
        return false;

    ves::MediaChangeRequest request;
    request.media_type = ves::EmbeddedMediaType::CdRom;
    request.engine_generation = videoEngineGeneration.load (std::memory_order_acquire);
    request.request_id = floppyHotSwapRequestId.fetch_add (1, std::memory_order_relaxed) + 1;
    request.brief_instance_name = "cdrm";
    request.path = file.getFullPathName().toStdString();
    if (! localEngine->requestMediaChange (request))
        return false;

    floppyHotSwapPending.store (true, std::memory_order_release);
    floppyHotSwapMessage = "Loading " + file.getFileName() + "...";
    return true;
}

bool VintageEmulatorStudioProcessor::requestSelectedCdRomEject()
{
    if (! selectedMachineSupportsCdRomHotSwap())
        return false;
    if (getEngineState() == EmbeddedEngineState::Booting || getEngineState() == EmbeddedEngineState::Starting)
    {
        floppyHotSwapMessage = "CD-ROM can be changed after the machine is Ready.";
        return false;
    }
    if (getEngineState() != EmbeddedEngineState::Ready)
        return false;

    auto localEngine = std::atomic_load (&engine);
    if (localEngine == nullptr || floppyHotSwapPending.load (std::memory_order_acquire))
        return false;

    ves::MediaChangeRequest request;
    request.media_type = ves::EmbeddedMediaType::CdRom;
    request.engine_generation = videoEngineGeneration.load (std::memory_order_acquire);
    request.request_id = floppyHotSwapRequestId.fetch_add (1, std::memory_order_relaxed) + 1;
    request.brief_instance_name = "cdrm";
    if (! localEngine->requestMediaChange (request))
        return false;

    floppyHotSwapPending.store (true, std::memory_order_release);
    floppyHotSwapMessage = "Ejecting CD-ROM...";
    return true;
}

bool VintageEmulatorStudioProcessor::isFloppyHotSwapPending() const
{
    return floppyHotSwapPending.load (std::memory_order_acquire);
}

bool VintageEmulatorStudioProcessor::isCdRomHotSwapPending() const
{
    return isFloppyHotSwapPending();
}

juce::String VintageEmulatorStudioProcessor::getFloppyHotSwapMessage() const
{
    return floppyHotSwapMessage;
}

bool VintageEmulatorStudioProcessor::processFloppyHotSwapResults()
{
    auto localEngine = std::atomic_load (&engine);
    if (localEngine == nullptr)
        return false;

    ves::MediaChangeResult result;
    if (! localEngine->pollMediaChangeResult (result))
        return false;

    if (result.engine_generation != videoEngineGeneration.load (std::memory_order_acquire))
        return false;

    floppyHotSwapPending.store (false, std::memory_order_release);
    const auto mediaPathKey = result.media_type == ves::EmbeddedMediaType::CdRom
        ? juce::String (cdRomMediaPathProperty)
        : juce::String (floppyMediaPathProperty);
    const auto recentType = result.media_type == ves::EmbeddedMediaType::CdRom
        ? RecentMediaType::CdRom
        : RecentMediaType::Floppy;
    if (result.success)
    {
        if (result.drive_empty)
        {
            configuredMediaPaths[mediaPathKey].clear();
            configuredMediaPathOrigins[mediaPathKey] = "current_user_selection";
        }
        else
        {
            configuredMediaPaths[mediaPathKey] = juce::String (result.applied_path);
            configuredMediaPathOrigins[mediaPathKey] = "current_user_selection";
            addRecentMediaPath (recentType, juce::File (result.applied_path));
        }
        floppyHotSwapMessage.clear();
    }
    else
    {
        configuredMediaPaths[mediaPathKey].clear();
        floppyHotSwapMessage = result.error_message.empty()
            ? "Unable to change floppy image."
            : juce::String (result.error_message);
    }
    floppyHotSwapRevision.fetch_add (1, std::memory_order_release);
    return true;
}

bool VintageEmulatorStudioProcessor::selectedMachineSupportsSaveState() const
{
    const auto* profile = findMachineProfileByDriverName (getSelectedMachineDriverName());
    return profile != nullptr
        && profile->saveStateSupport != EmbeddedMachineProfile::SaveStateSupport::Unsupported;
}

bool VintageEmulatorStudioProcessor::selectedMachineSupportsStandaloneAutosave() const
{
    const auto* profile = findMachineProfileByDriverName (getSelectedMachineDriverName());
    return profile != nullptr
        && profile->saveStateSupport != EmbeddedMachineProfile::SaveStateSupport::Unsupported;
}

bool VintageEmulatorStudioProcessor::processExperimentalStateResults()
{
    auto localEngine = std::atomic_load (&engine);
    if (localEngine == nullptr)
        return false;
    ves::StateOperationResult result;
    if (! localEngine->pollStateOperationResult (result))
        return false;
    const auto currentGeneration = videoEngineGeneration.load (std::memory_order_acquire);
    if (result.engine_generation != currentGeneration)
    {
        experimentalStatePending.store (false, std::memory_order_release);
        experimentalStateRestoreInProgress.store (false, std::memory_order_release);
        return false;
    }
    const bool successfulLoad = result.success && result.operation == ves::StateOperation::Load;
    if (! successfulLoad)
        experimentalStatePending.store (false, std::memory_order_release);
    if (result.operation == ves::StateOperation::Load)
    {
        experimentalStateGenerationAfterRestore = currentGeneration;
        localEngine->discardQueuedAudio();
        if (result.success)
            sendMidiPanic ("restore", nullptr, &result.held_midi_notes);
    }
    if (result.operation == ves::StateOperation::Save
        && experimentalStateTarget == ExperimentalStateTarget::StandaloneSwitchSave)
    {
        const auto outgoingDriver = pendingStandaloneSwitchDriver;
        const auto nextDriver = pendingStandaloneSwitchTarget;
        if (result.success)
        {
            juce::String error;
            const auto file = autosaveFileForDriver (outgoingDriver);
            if (! writeExperimentalStateFile (file, result.snapshot_blob, outgoingDriver,
                                               currentSampleRate, currentGeneration,
                                               result.held_midi_notes, error))
                appendLifecycleLog ("[VES state autosave] switch save failed driver="
                    + outgoingDriver + " error=" + error);
            else
                appendLifecycleLog ("[VES state autosave] switch save success driver=" + outgoingDriver);
        }
        else
            appendLifecycleLog ("[VES state autosave] switch save failed driver=" + outgoingDriver
                + " error=" + juce::String (result.error_message));

        pendingStandaloneSwitchDriver.clear();
        pendingStandaloneSwitchTarget.clear();
        experimentalStatePending.store (false, std::memory_order_release);
        {
            const juce::ScopedLock lock (machineSelectionLock);
            selectedMachineDriverName = nextDriver;
        }
        restartSelectedMachine ("selectMachineByDriverName", "selected_machine_changed_by_user");
        return true;
    }
    else if (result.success)
        experimentalStateStatus = result.operation == ves::StateOperation::Save
            ? "State saved (" + juce::String (static_cast<juce::int64> (result.snapshot_size)) + " bytes)"
            : "State restored; waiting for post-load audio/video...";
    else
        experimentalStateStatus = result.error_message.empty() ? "State operation failed" : juce::String (result.error_message);
    if (result.operation == ves::StateOperation::Load && ! result.success)
    {
        experimentalStateRestoreInProgress.store (false, std::memory_order_release);
        if (experimentalStateTarget == ExperimentalStateTarget::Autosave)
            appendLifecycleLog ("[VES state autosave] restore success=no error=" + experimentalStateStatus);
        else if (experimentalStateTarget == ExperimentalStateTarget::DawRestore)
            appendLifecycleLog ("[VES DAW state] restore success=no generation_before="
                + juce::String (static_cast<juce::int64> (experimentalStateGenerationBeforeRestore))
                + " generation_after=" + juce::String (static_cast<juce::int64> (currentGeneration))
                + " error=" + experimentalStateStatus);

        if (result.load_failed_requires_restart)
        {
            const bool autosave = experimentalStateTarget == ExperimentalStateTarget::Autosave;
            const auto failureStatus = experimentalStateStatus + "; clean engine restart required";
            restartSelectedMachine ("processExperimentalStateResults", "failed_state_load_requires_clean_restart");
            if (autosave)
                standaloneAutosaveRestoreAttemptedGeneration = videoEngineGeneration.load (std::memory_order_acquire);
            experimentalStateStatus = failureStatus;
        }
    }
    return true;
}

void VintageEmulatorStudioProcessor::finishExperimentalStateRestoreIfReady()
{
    if (! experimentalStateRestoreInProgress.load (std::memory_order_acquire))
        return;
    auto localEngine = std::atomic_load (&engine);
    if (localEngine == nullptr)
        return;
    const auto& diag = localEngine->diagnostics();
    const auto audioMs = diag.state_restore_first_audio_ms.load (std::memory_order_acquire);
    const auto secondAudioMs = diag.state_restore_second_audio_ms.load (std::memory_order_acquire);
    const auto videoMs = diag.state_restore_first_video_ms.load (std::memory_order_acquire);
    const auto firstTimesliceMs = diag.state_restore_first_timeslice_completed_ms.load (std::memory_order_acquire);
    const auto now = nowMs();
    constexpr uint64_t restoreGuardTimeoutMs = 2000;
    const bool timedOut = experimentalStateGuardStartedMs != 0 && now - experimentalStateGuardStartedMs >= restoreGuardTimeoutMs;
    if ((firstTimesliceMs == 0 || audioMs == 0 || secondAudioMs == 0 || videoMs == 0) && ! timedOut)
        return;

    localEngine->discardQueuedAudio();
    const auto generation = videoEngineGeneration.load (std::memory_order_acquire);
    experimentalStateGenerationAfterRestore = generation;
    const bool autosave = experimentalStateTarget == ExperimentalStateTarget::Autosave;
    const bool dawRestore = experimentalStateTarget == ExperimentalStateTarget::DawRestore;
    const bool fullyRecovered = firstTimesliceMs != 0 && audioMs != 0 && secondAudioMs != 0 && videoMs != 0;
    experimentalStateStatus = juce::String (autosave ? "Autosave restored"
                                                     : dawRestore ? "DAW session state restored" : "State restored")
        + (fullyRecovered ? "" : " (recovery timed out/degraded)");
    if (autosave)
        appendLifecycleLog ("[VES state autosave] restore success=" + juce::String (fullyRecovered ? "yes" : "degraded")
            + " path=" + autosaveFileForDriver (getSelectedMachineDriverName()).getFullPathName()
            + " generation_before=" + juce::String (static_cast<juce::int64> (experimentalStateGenerationBeforeRestore))
            + " generation_after=" + juce::String (static_cast<juce::int64> (generation)));
    else if (dawRestore)
        appendLifecycleLog ("[VES DAW state] restore success=" + juce::String (fullyRecovered ? "yes" : "degraded")
            + " generation_before="
            + juce::String (static_cast<juce::int64> (experimentalStateGenerationBeforeRestore))
            + " generation_after=" + juce::String (static_cast<juce::int64> (generation))
            + (timedOut ? " guard_timeout=yes" : " guard_timeout=no"));
    experimentalStatePending.store (false, std::memory_order_release);
    experimentalStateRestoreInProgress.store (false, std::memory_order_release);
}

bool VintageEmulatorStudioProcessor::isDawPluginWrapper() const
{
    return wrapperType == wrapperType_AudioUnit || wrapperType == wrapperType_VST3;
}

void VintageEmulatorStudioProcessor::tryDawPendingRestore()
{
    if (! isDawPluginWrapper() || getEngineState() != EmbeddedEngineState::Ready
        || ! selectedMachineSupportsSaveState()
        || experimentalStatePending.load (std::memory_order_acquire))
        return;

    auto localEngine = std::atomic_load (&engine);
    if (localEngine == nullptr || juce::String (localEngine->driverName()) != getSelectedMachineDriverName())
        return;

    ves::StateOperationRequest request;
    {
        const juce::ScopedLock lock (dawStateLock);
        const auto selectedDriver = getSelectedMachineDriverName();
        if (! dawPendingRestoreArmed || dawPendingRestoreSnapshot.empty()
            || dawPendingRestoreDriver != selectedDriver
            || dawPendingRestoreCompatibilityRevision != experimentalStateFormatVersion)
            return;
        request.snapshot_blob = std::move (dawPendingRestoreSnapshot);
        request.held_midi_notes = dawPendingRestoreHeldMidiNotes;
        dawPendingRestoreArmed = false;
    }

    request.operation = ves::StateOperation::Load;
    request.engine_generation = videoEngineGeneration.load (std::memory_order_acquire);
    request.request_id = experimentalStateRequestId.fetch_add (1, std::memory_order_relaxed) + 1;
    experimentalStateGenerationBeforeRestore = request.engine_generation;
    experimentalStateGuardStartedMs = nowMs();
    experimentalStateRestoreInProgress.store (true, std::memory_order_release);
    localEngine->discardQueuedAudio();
    if (! localEngine->requestStateOperation (request))
    {
        const juce::ScopedLock lock (dawStateLock);
        dawPendingRestoreSnapshot = std::move (request.snapshot_blob);
        dawPendingRestoreHeldMidiNotes = request.held_midi_notes;
        dawPendingRestoreArmed = true;
        experimentalStateRestoreInProgress.store (false, std::memory_order_release);
        return;
    }
    {
        const juce::ScopedLock lock (dawStateLock);
        dawPendingRestoreSnapshot.clear();
        dawPendingRestoreDriver.clear();
        dawPendingRestoreCompatibilityRevision = 0;
        dawPendingRestoreHeldMidiNotes.fill (0);
    }
    experimentalStateTarget = ExperimentalStateTarget::DawRestore;
    experimentalStatePending.store (true, std::memory_order_release);
    experimentalStateStatus = "Restoring DAW session state...";
    appendLifecycleLog ("[VES DAW state] restore requested=yes generation_before="
        + juce::String (static_cast<juce::int64> (request.engine_generation)));
}

void VintageEmulatorStudioProcessor::tryStandaloneAutosaveRestore()
{
    if (wrapperType != wrapperType_Standalone || getEngineState() != EmbeddedEngineState::Ready
        || ! selectedMachineSupportsStandaloneAutosave())
        return;

    const auto generation = videoEngineGeneration.load (std::memory_order_acquire);
    if (standaloneAutosaveRestoreAttemptedGeneration == generation)
        return;

    auto localEngine = std::atomic_load (&engine);
    if (localEngine == nullptr || experimentalStatePending.load (std::memory_order_acquire))
        return;
    standaloneAutosaveRestoreAttemptedGeneration = generation;

    const auto driver = getSelectedMachineDriverName();
    const auto file = autosaveFileForDriver (driver);
    const bool found = file.existsAsFile();
    appendLifecycleLog ("[VES state autosave] startup path=" + file.getFullPathName()
        + " found=" + juce::String (found ? "yes" : "no"));
    if (! found)
    {
        experimentalStateStatus = "Autosave not found; normal boot retained";
        return;
    }

    ves::StateOperationRequest request;
    request.operation = ves::StateOperation::Load;
    request.engine_generation = generation;
    request.request_id = experimentalStateRequestId.fetch_add (1, std::memory_order_relaxed) + 1;
    juce::String error;
    if (! readExperimentalStateFile (file, driver, request.snapshot_blob, request.held_midi_notes, error))
    {
        experimentalStateStatus = "Autosave ignored: " + error;
        appendLifecycleLog ("[VES state autosave] compatibility=rejected error=" + error);
        return;
    }

    appendLifecycleLog ("[VES state autosave] compatibility=accepted restore_requested=yes generation="
        + juce::String (static_cast<juce::int64> (generation)));
    experimentalStateGenerationBeforeRestore = generation;
    experimentalStateGuardStartedMs = nowMs();
    experimentalStateRestoreInProgress.store (true, std::memory_order_release);
    localEngine->discardQueuedAudio();
    if (! localEngine->requestStateOperation (request))
    {
        experimentalStateRestoreInProgress.store (false, std::memory_order_release);
        experimentalStateStatus = "Autosave restore request failed; normal boot retained";
        appendLifecycleLog ("[VES state autosave] restore_requested=no");
        return;
    }
    experimentalStateTarget = ExperimentalStateTarget::Autosave;
    experimentalStatePending.store (true, std::memory_order_release);
    experimentalStateStatus = "Restoring autosave...";
}

void VintageEmulatorStudioProcessor::saveStandaloneAutosaveOnShutdown()
{
    if (wrapperType != wrapperType_Standalone || getEngineState() != EmbeddedEngineState::Ready
        || ! selectedMachineSupportsStandaloneAutosave())
        return;

    const auto driver = getSelectedMachineDriverName();
    const auto file = autosaveFileForDriver (driver);
    const auto started = nowMs();
    auto localEngine = std::atomic_load (&engine);
    if (localEngine == nullptr || experimentalStatePending.load (std::memory_order_acquire))
    {
        appendLifecycleLog ("[VES state autosave] close requested=no reason=state_operation_pending path=" + file.getFullPathName());
        return;
    }

    ves::StateOperationRequest request;
    request.operation = ves::StateOperation::Save;
    request.engine_generation = videoEngineGeneration.load (std::memory_order_acquire);
    request.request_id = experimentalStateRequestId.fetch_add (1, std::memory_order_relaxed) + 1;
    request.held_midi_notes = captureActiveMidiNotes();
    appendLifecycleLog ("[VES state autosave] close requested=yes path=" + file.getFullPathName());
    if (! localEngine->requestStateOperation (request))
    {
        appendLifecycleLog ("[VES state autosave] close save_success=no error=request_rejected path=" + file.getFullPathName());
        return;
    }

    constexpr uint64_t shutdownAutosaveTimeoutMs = 2000;
    ves::StateOperationResult result;
    bool completed = false;
    while (nowMs() - started < shutdownAutosaveTimeoutMs)
    {
        if (localEngine->pollStateOperationResult (result))
        {
            if (result.request_id == request.request_id)
            {
                completed = true;
                break;
            }
        }
        juce::Thread::sleep (2);
    }

    if (! completed || ! result.success)
    {
        appendLifecycleLog ("[VES state autosave] close save_success=no error="
            + juce::String (! completed ? "timeout" : result.error_message)
            + " path=" + file.getFullPathName());
        return;
    }

    juce::String error;
    const bool written = writeExperimentalStateFile (file, result.snapshot_blob, driver, currentSampleRate,
        request.engine_generation, result.held_midi_notes, error);
    appendLifecycleLog ("[VES state autosave] close path=" + file.getFullPathName()
        + " atomic_write=" + juce::String (written ? "success" : "failure")
        + (error.isNotEmpty() ? " error=" + error : juce::String()));
}

void VintageEmulatorStudioProcessor::timerCallback()
{
    processFloppyHotSwapResults();
    processExperimentalStateResults();
    finishExperimentalStateRestoreIfReady();
    tryStandaloneAutosaveRestore();
    tryDawPendingRestore();
}

juce::String VintageEmulatorStudioProcessor::getSelectedCdRomPath() const
{
    if (const auto it = configuredMediaPaths.find (cdRomMediaPathProperty); it != configuredMediaPaths.end())
        return it->second;
    return {};
}

void VintageEmulatorStudioProcessor::setSelectedCdRomFile (const juce::File& file)
{
    const auto* profile = findMachineProfileByDriverName (getSelectedMachineDriverName());
    if (file.getFullPathName().isEmpty())
        return;

    configuredMediaPaths[cdRomMediaPathProperty] = file.getFullPathName();
    configuredMediaPathOrigins[cdRomMediaPathProperty] = "current_user_selection";
    addRecentMediaPath (RecentMediaType::CdRom, file);
    if (profileSupportsMediaType (profile, EmbeddedMachineProfile::MediaType::CdRom) && std::atomic_load (&engine) != nullptr)
        restartSelectedMachine ("setSelectedCdRomFile", "cdrom_media_changed_by_user");
}

void VintageEmulatorStudioProcessor::clearSelectedCdRom()
{
    const auto* profile = findMachineProfileByDriverName (getSelectedMachineDriverName());
    configuredMediaPaths[cdRomMediaPathProperty].clear();
    configuredMediaPathOrigins[cdRomMediaPathProperty] = "current_user_selection";
    if (profileSupportsMediaType (profile, EmbeddedMachineProfile::MediaType::CdRom) && std::atomic_load (&engine) != nullptr)
        restartSelectedMachine ("clearSelectedCdRom", "cdrom_media_cleared_by_user");
}

juce::String VintageEmulatorStudioProcessor::getSelectedHardDiskPath() const
{
    if (const auto it = configuredMediaPaths.find (hardDiskMediaPathProperty); it != configuredMediaPaths.end())
        return it->second;
    return {};
}

void VintageEmulatorStudioProcessor::setSelectedHardDiskFile (const juce::File& file)
{
    const auto* profile = findMachineProfileByDriverName (getSelectedMachineDriverName());
    if (file.getFullPathName().isEmpty())
        return;

    configuredMediaPaths[hardDiskMediaPathProperty] = file.getFullPathName();
    configuredMediaPathOrigins[hardDiskMediaPathProperty] = "current_user_selection";
    addRecentMediaPath (RecentMediaType::HardDisk, file);
    if (profileSupportsMediaType (profile, EmbeddedMachineProfile::MediaType::HardDisk) && std::atomic_load (&engine) != nullptr)
        restartSelectedMachine ("setSelectedHardDiskFile", "harddisk_media_changed_by_user");
}

void VintageEmulatorStudioProcessor::clearSelectedHardDisk()
{
    const auto* profile = findMachineProfileByDriverName (getSelectedMachineDriverName());
    configuredMediaPaths[hardDiskMediaPathProperty].clear();
    configuredMediaPathOrigins[hardDiskMediaPathProperty] = "current_user_selection";
    if (profileSupportsMediaType (profile, EmbeddedMachineProfile::MediaType::HardDisk) && std::atomic_load (&engine) != nullptr)
        restartSelectedMachine ("clearSelectedHardDisk", "harddisk_media_cleared_by_user");
}

std::vector<juce::String> VintageEmulatorStudioProcessor::getRecentMediaPaths (RecentMediaType type) const
{
    return globalRecentMediaPreferences().get (type);
}

void VintageEmulatorStudioProcessor::removeRecentMediaPath (RecentMediaType type, const juce::String& path)
{
    globalRecentMediaPreferences().remove (type, path);
}

void VintageEmulatorStudioProcessor::addRecentMediaPath (RecentMediaType type, const juce::File& file)
{
    globalRecentMediaPreferences().add (type, file);
}

juce::String VintageEmulatorStudioProcessor::getNvramStatusText() const
{
    const auto selectedDriver = getSelectedMachineDriverName();
    const auto& profile = findMachineProfileByDriverName (selectedDriver) != nullptr
                            ? *findMachineProfileByDriverName (selectedDriver)
                            : defaultMachineProfile();
    if (! profile.requiresSeededNvram)
        return "Not required";

    if (getNvramSeedFile().existsAsFile())
        return "Seed NVRAM available";
    return "Clean NVRAM will be initialized";
}

void VintageEmulatorStudioProcessor::setSavedEditorSize (int width, int height)
{
    editorWidth.store (juce::jlimit (800, 2400, width), std::memory_order_relaxed);
    editorHeight.store (juce::jlimit (520, 1600, height), std::memory_order_relaxed);
}

bool VintageEmulatorStudioProcessor::copyLatestVideoFrame (EmbeddedVideoFrameForEditor& snapshot)
{
    auto localEngine = std::atomic_load (&engine);
    if (localEngine == nullptr)
        return false;

    ves::VideoFrameSnapshot engineFrame;
    if (! localEngine->copyLatestVideoFrame (engineFrame))
        return false;

    const auto engineGeneration = videoEngineGeneration.load (std::memory_order_acquire);
    if (localEngine != std::atomic_load (&engine))
        return false;

    snapshot.width = engineFrame.width;
    snapshot.height = engineFrame.height;
    snapshot.generation = engineFrame.generation;
    snapshot.engineGeneration = engineGeneration;
    snapshot.timestampMs = engineFrame.timestamp_ms;
    snapshot.pixels = std::move (engineFrame.pixels);
    return true;
}

uint64_t VintageEmulatorStudioProcessor::getVideoFrameResetGeneration() const
{
    auto localEngine = std::atomic_load (&engine);
    return localEngine != nullptr
        ? localEngine->diagnostics().video_frame_reset_generation.load (std::memory_order_acquire)
        : 0;
}

void VintageEmulatorStudioProcessor::setGuiPerformanceMode (GuiPerformanceMode mode)
{
    const auto previous = static_cast<GuiPerformanceMode> (guiPerformanceMode.exchange (static_cast<int> (mode), std::memory_order_acq_rel));
    if (previous == mode)
        return;

    persistGuiPerformanceMode();
    applyGuiPerformanceModeToEngine();
}

GuiPerformanceMode VintageEmulatorStudioProcessor::getGuiPerformanceMode() const
{
    const auto value = guiPerformanceMode.load (std::memory_order_acquire);
    switch (static_cast<GuiPerformanceMode> (value))
    {
        case GuiPerformanceMode::Normal:
        case GuiPerformanceMode::Reduced:
        case GuiPerformanceMode::Static:
        case GuiPerformanceMode::Disabled:
            return static_cast<GuiPerformanceMode> (value);
    }

    return GuiPerformanceMode::Normal;
}

juce::Colour VintageEmulatorStudioProcessor::getBackgroundColour() const
{
    return juce::Colour (backgroundColourArgb.load (std::memory_order_acquire));
}

void VintageEmulatorStudioProcessor::setBackgroundColour (juce::Colour colour)
{
    backgroundColourArgb.store (colour.getARGB(), std::memory_order_release);
    globalAppearancePreferences().saveBackgroundColour (colour);
}

#if JucePlugin_Build_Standalone
void VintageEmulatorStudioProcessor::setStandaloneMasterVolume (float volume)
{
    const auto clamped = juce::jlimit (0.0f, 1.0f, volume);
    const auto previous = standaloneMasterVolume.exchange (clamped, std::memory_order_acq_rel);
    if (std::abs (previous - clamped) < 0.0001f)
        return;

    persistStandaloneMasterVolume();
}

float VintageEmulatorStudioProcessor::getStandaloneMasterVolume() const
{
    return juce::jlimit (0.0f, 1.0f, standaloneMasterVolume.load (std::memory_order_acquire));
}
#endif

void VintageEmulatorStudioProcessor::setVideoDisplayActive (bool active)
{
    if (auto localEngine = std::atomic_load (&engine))
        localEngine->setVideoDisplayActive (active);
}

void VintageEmulatorStudioProcessor::requestVideoCaptureWidth (int width)
{
    const auto quantized = getEffectiveVideoCaptureWidth (width);
    requestedVideoCaptureWidth.store (quantized, std::memory_order_release);
    if (auto localEngine = std::atomic_load (&engine))
        localEngine->requestVideoCaptureWidth (quantized);
}

bool VintageEmulatorStudioProcessor::enqueueMouseEvent (ves::EmbeddedMouseEventType type, int x, int y)
{
    if (auto localEngine = std::atomic_load (&engine))
        return localEngine->enqueueMouseEvent (type, static_cast<std::int32_t> (x), static_cast<std::int32_t> (y));
    return false;
}

void VintageEmulatorStudioProcessor::requestMouseRelease()
{
    if (auto localEngine = std::atomic_load (&engine))
        localEngine->requestMouseRelease();
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VintageEmulatorStudioProcessor();
}
