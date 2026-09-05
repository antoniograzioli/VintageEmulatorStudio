// SPDX-License-Identifier: AGPL-3.0-only

#include "VintageEmulatorStudioProcessor.h"
#include "VintageEmulatorStudioEditor.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstdint>
#include <cmath>
#include <utility>

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
    { "Casio AP-10", "ap10", false, false, true },
    { "Yamaha TX81Z", "tx81z", true, false, true, true },
    { "Yamaha FB-01", "fb01", false, false, true, true, false, nullptr, nullptr, 0, &fb01MidiOptions },
    { "Yamaha TG100", "tg100", false, false, true, true },
    { "Casio RZ-1", "rz1", false, false, true },
    { "Casio CT-8000", "ct8000", false, false, true },
    { "Casio CT-FK1", "ctfk1", false, false, true },
    { "Casio CZ-101", "cz101", false, false, true },
    { "Casio CZ-1", "cz1", false, false, true },
    { "Casio CZ-230S", "cz230s", false, false, true },
    { "Ensoniq ESQ-1", "esq1", false, false, true, true },
    { "PAiA FatMan", "fatman", false, false, true, true, false, nullptr, nullptr, 0, nullptr, false },
    { "Ensoniq VFX", "vfx", false, false, true, true },
    { "Ensoniq VFX-SD", "vfxsd", false, false, true, true, false, nullptr, ensoniqFloppyMediaDevices, static_cast<int> (std::size (ensoniqFloppyMediaDevices)) },
    { "LinnDrum", "linndrum", false, false, false, false, true, "midiin" },
	{ "Oberheim DMX", "obdmx", false, false, false, false, true, "midiin" },
    { "Roland TR-707", "tr707", false, false, true, true },
    { "Roland TR-727", "tr727", false, false, true, true },
    { "Sequential Prophet-5", "prophet5r30", false, false, false, false, true, "midiin" },
    { "Sequential Six-Trak", "sixtrak", false, false, true, true },
    { "Yamaha DD-9", "dd9", false, false, false, false, false, nullptr, nullptr, 0, &dd9MidiOptions },
    { "Yamaha DX100", "dx100", false, false, true, true },
    { "Yamaha MU-50", "mu50", false, false, true, true },
    { "Yamaha MU-2000", "mu2000", false, false, true, true, false, nullptr, nullptr, 0, &mu2000MidiOptions },
    { "Yamaha PSR-11", "psr11", false, false, false, false, true, "midiin" },
    { "Yamaha PSR-70", "psr70", false, false, true },
    { "Yamaha PSR-75", "psr75", false, false, false, false, true, "midiin" },
    { "Yamaha PSR-76", "psr76", false, false, false, false, true, "midiin" },
    { "Yamaha PSR-110", "psr110", false, false, false, false, true, "midiin" },
    { "Yamaha PSR-150", "psr150", false, false, false, false, true, "midiin" },
    { "Yamaha PSR-180", "psr180", false, false, false, false, true, "midiin" },
    { "Yamaha PSR-60", "psr60", false, false, true, true },
    { "Yamaha PSS-6", "pss6", false, false, false, false, true, "midiin" },
    { "Yamaha PSS-11", "pss11", false, false, false, false, true, "midiin" },
    { "Yamaha PSS-12", "pss12", false, false, false, false, true, "midiin" },
    { "Yamaha PSS-21", "pss21", false, false, false, false, true, "midiin" },
    { "Yamaha PSS-31", "pss31", false, false, false, false, true, "midiin" },
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

juce::File resolvePackagedResourcesDirectory()
{
    const auto module = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
#if JUCE_MAC
    return module.getParentDirectory().getParentDirectory().getChildFile ("Resources");
#elif JUCE_WINDOWS
    if (module.hasFileExtension (".vst3"))
        return module.getParentDirectory().getParentDirectory().getChildFile ("Resources");

    return module.getParentDirectory().getChildFile ("Resources");
#elif JUCE_LINUX
    const auto standaloneResources = module.getParentDirectory().getChildFile ("Resources");
    if (standaloneResources.isDirectory())
        return standaloneResources;

    const auto vst3Resources = module.getParentDirectory()
        .getParentDirectory()
        .getChildFile ("Resources");
    return vst3Resources.isDirectory() ? vst3Resources : juce::File {};
#else
    return module.getParentDirectory().getChildFile ("Resources");
#endif
}

juce::File resolveMamePluginsDirectory()
{
    const auto bundled = resolvePackagedResourcesDirectory().getChildFile ("plugins");
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

    const auto bundled = resolvePackagedResourcesDirectory().getChildFile ("artwork");
    if (hasArtworkResources (bundled))
        return bundled;

    return {};
}

uint64_t nowMs()
{
    return static_cast<uint64_t> (juce::Time::getMillisecondCounterHiRes());
}

}

VintageEmulatorStudioProcessor::VintageEmulatorStudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    // Initialise the process-shared, cross-format Recent Media store before any editor opens.
    (void) globalRecentMediaPreferences();
    configuredRomsPath = loadPersistedRomsPath();
    configuredArtworkPath = loadPersistedArtworkPath();
    startTimerHz (20);
}

VintageEmulatorStudioProcessor::~VintageEmulatorStudioProcessor()
{
    stopTimer();
    stopEngine();
}

void VintageEmulatorStudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // AudioProcessorPlayer calls this only after the active JUCE device is open.
    // Recreate the embedded run on every device restart so MAME's fixed internal
    // rate always matches the device-selected rate, while preserving driver and
    // ROM-path state in this processor.
    const auto newSampleRate = sampleRate > 0.0 ? sampleRate : 0.0;
    const auto newBlockSize = std::max (samplesPerBlock, 0);
    const auto configurationChanged = currentSampleRate != newSampleRate || maxBlockSize != newBlockSize;
    currentSampleRate = newSampleRate;
    maxBlockSize = newBlockSize;

    if (configurationChanged && std::atomic_load (&engine) != nullptr)
        stopEngine();

    startEngineIfNeeded (currentSampleRate);
    if (auto localEngine = std::atomic_load (&engine))
        localEngine->noteHostAudioConfiguration (currentSampleRate, maxBlockSize);
}

void VintageEmulatorStudioProcessor::releaseResources()
{
    // JUCE has stopped/suspended its callback before this is invoked.  Drop the
    // producer and its queues rather than allowing stale samples to survive a
    // device, rate, or buffer-size change.
    currentSampleRate = 0.0;
    maxBlockSize = 0;
    stopEngine();
}

bool VintageEmulatorStudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
        || layouts.getMainOutputChannelSet() == juce::AudioChannelSet::mono();
}

bool VintageEmulatorStudioProcessor::isSupportedMidiForPrototype (const juce::MidiMessage& message)
{
    return message.isNoteOnOrOff()
        || message.isController()
        || message.isProgramChange()
        || message.isPitchWheel();
}

void VintageEmulatorStudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    buffer.clear();

    auto localEngine = std::atomic_load (&engine);
    if (localEngine == nullptr)
        return;

    updateBootState();
    localEngine->diagnostics().juce_process_block_count.fetch_add (1, std::memory_order_relaxed);
    localEngine->diagnostics().juce_block_size.store (static_cast<uint64_t> (std::max (buffer.getNumSamples(), 0)), std::memory_order_relaxed);

    const bool ready = state.load (std::memory_order_relaxed) == static_cast<int> (EmbeddedEngineState::Ready);
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
        if (! isSupportedMidiForPrototype (message))
            continue;

        const auto midiNow = nowMs();
        if (juceMidiFirstReceivedMs.load (std::memory_order_relaxed) == 0)
            juceMidiFirstReceivedMs.store (midiNow, std::memory_order_relaxed);
        juceMidiLastReceivedMs.store (midiNow, std::memory_order_relaxed);
        midiMessagesReceived.fetch_add (1, std::memory_order_relaxed);

        if (! ready)
        {
            midiDroppedBeforeReady.fetch_add (1, std::memory_order_relaxed);
            continue;
        }

        const auto size = message.getRawDataSize();
        if (size > 0)
        {
            lastMidiSentToEngineMs.store (midiNow, std::memory_order_relaxed);
            waitingForMidiAudioOnset.store (true, std::memory_order_relaxed);
            localEngine->sendMidiBytes (message.getRawData(), static_cast<std::size_t> (size));
        }
    }

    if (! ready)
        return;

    const auto channels = buffer.getNumChannels();
    const auto samples = buffer.getNumSamples();
    int offset = 0;
    while (offset < samples)
    {
        const auto request = static_cast<std::size_t> (std::min<int> (samples - offset, static_cast<int> (audioScratch.size())));
        const auto read = localEngine->readAudioFrames (audioScratch.data(), request);
        audioFramesReadByPlugin.fetch_add (read, std::memory_order_relaxed);

        for (std::size_t i = 0; i < read; ++i)
        {
            if (waitingForMidiAudioOnset.load (std::memory_order_relaxed)
                && (std::abs (audioScratch[i].left) > 0.0001f || std::abs (audioScratch[i].right) > 0.0001f))
            {
                const auto onsetNow = nowMs();
                const auto midiSent = lastMidiSentToEngineMs.load (std::memory_order_relaxed);
                auto& diag = localEngine->diagnostics();
                if (diag.midi_first_audio_onset_ms.load (std::memory_order_relaxed) == 0)
                    diag.midi_first_audio_onset_ms.store (onsetNow, std::memory_order_relaxed);
                if (midiSent != 0 && onsetNow >= midiSent)
                    diag.midi_to_audio_onset_ms.store (onsetNow - midiSent, std::memory_order_relaxed);
                waitingForMidiAudioOnset.store (false, std::memory_order_relaxed);
            }

            if (channels > 0)
                buffer.setSample (0, offset + static_cast<int> (i), audioScratch[i].left);
            if (channels > 1)
                buffer.setSample (1, offset + static_cast<int> (i), audioScratch[i].right);
        }

        if (read < request)
            break;

        offset += static_cast<int> (read);
    }
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

    if (auto xml = stateTree.createXml())
        copyXmlToBinary (*xml, destData);
}

void VintageEmulatorStudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        const auto stateTree = juce::ValueTree::fromXml (*xml);
        if (stateTree.hasType ("VintageEmulatorStudioEmbeddedState")
            || stateTree.hasType ("MAMESynthsEmbeddedState"))
        {
            editorWidth.store (juce::jlimit (800, 2400, static_cast<int> (stateTree.getProperty ("editorWidth", 1700))), std::memory_order_relaxed);
            editorHeight.store (juce::jlimit (520, 1600, static_cast<int> (stateTree.getProperty ("editorHeight", 1100))), std::memory_order_relaxed);
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
            }
            else
            {
                // Migrate the original S3000XL-only assignment without requiring users to reselect it.
                const auto legacyKey = getMediaStateKey ("s3000xl", "floppydisk");
                if (stateTree.hasProperty (legacyKey))
                    configuredMediaPaths[floppyMediaPathProperty] = stateTree.getProperty (legacyKey).toString();
            }
            if (stateTree.hasProperty (cdRomMediaPathProperty))
                configuredMediaPaths[cdRomMediaPathProperty] = stateTree.getProperty (cdRomMediaPathProperty).toString();
            if (stateTree.hasProperty (hardDiskMediaPathProperty))
                configuredMediaPaths[hardDiskMediaPathProperty] = stateTree.getProperty (hardDiskMediaPathProperty).toString();
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

            const auto changed = restoredDriver != getSelectedMachineDriverName()
                              || configuredRomsPath != previousRomsPath
                              || configuredArtworkPath != previousArtworkPath
                              || configuredMediaPaths != previousMediaPaths;
            {
                const juce::ScopedLock lock (machineSelectionLock);
                selectedMachineDriverName = restoredDriver;
            }
            if (changed)
                restartSelectedMachine();

            stateRestorationRevision.fetch_add (1, std::memory_order_release);
        }
    }
}

void VintageEmulatorStudioProcessor::startEngineIfNeeded (double sampleRate)
{
    // StandalonePluginHolder restores processor state before attaching the
    // processor to its already-open audio device.  Never boot before that
    // prepareToPlay callback supplies the actual device sample rate.
    if (sampleRate <= 0.0 || maxBlockSize <= 0)
        return;

    if (std::atomic_load (&engine) != nullptr)
        return;

    lastError.clear();
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
        state.store (static_cast<int> (EmbeddedEngineState::Failed), std::memory_order_relaxed);
        return;
    }

    const auto pluginsDir = resolveMamePluginsDirectory();
    const auto artworkPath = getEffectiveMameArtworkPath();
    std::vector<ves::EmbeddedEmulatorEngineSettings::StartupMediaOption> startupMediaOptions;
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
        startupMediaOptions.push_back ({ media.mameOptionName, it->second.toStdString() });
    }
    if (! pluginsDir.isDirectory())
    {
        lastError = "MAME plugins directory containing boot.lua and layout/init.lua was not found";
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
        engineGeneration
    });

    newEngine->setVideoDisplayActive (true);
    newEngine->requestVideoCaptureWidth (requestedVideoCaptureWidth.load (std::memory_order_acquire));
    newEngine->start();
    newEngine->noteHostAudioConfiguration (sampleRate, maxBlockSize);
    std::atomic_store (&engine, std::move (newEngine));
    videoEngineGeneration.fetch_add (1, std::memory_order_acq_rel);
    bootStartMs.store (nowMs(), std::memory_order_relaxed);
    state.store (static_cast<int> (EmbeddedEngineState::Booting), std::memory_order_relaxed);
}

void VintageEmulatorStudioProcessor::stopEngine()
{
    floppyHotSwapPending.store (false, std::memory_order_release);
    floppyHotSwapMessage.clear();
    auto oldEngine = std::atomic_exchange (&engine, std::shared_ptr<ves::EmbeddedEmulatorEngine> {});
    if (oldEngine != nullptr)
    {
        state.store (static_cast<int> (EmbeddedEngineState::Stopping), std::memory_order_relaxed);
        oldEngine->stopAndJoin (std::chrono::seconds (5));
    }

    state.store (static_cast<int> (EmbeddedEngineState::Stopped), std::memory_order_relaxed);
}

void VintageEmulatorStudioProcessor::restartSelectedMachine()
{
    videoEngineGeneration.fetch_add (1, std::memory_order_acq_rel);
    stopEngine();
    if (currentSampleRate > 0.0 && maxBlockSize > 0)
        startEngineIfNeeded (currentSampleRate);
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
        return false;
    }

    auto destination = nvramDir.getChildFile ("nvram");
    const auto seed = getNvramSeedFile();
    if (! destination.existsAsFile() && seed.existsAsFile() && ! seed.copyFileTo (destination))
    {
        lastError = "Could not copy required NVRAM seed";
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
    if (state.load (std::memory_order_relaxed) != static_cast<int> (EmbeddedEngineState::Booting))
        return;

    if (auto localEngine = std::atomic_load (&engine))
    {
        const auto& diag = localEngine->diagnostics();
        if (diag.machine_exited.load (std::memory_order_relaxed) != 0)
        {
            lastError = "MAME exited before the first video frame (result " + juce::String (localEngine->mameResult()) + ")";
            state.store (static_cast<int> (EmbeddedEngineState::Failed), std::memory_order_relaxed);
            return;
        }

        if (getBootElapsedMs() < 10000)
            return;

        const bool healthy = juce::String (localEngine->driverName()) == getSelectedMachineDriverName()
                          && diag.machine_started.load (std::memory_order_relaxed) != 0
                          && diag.video_frames_produced.load (std::memory_order_relaxed) != 0;
        if (! healthy)
            return;

        handleReadyTransition (*localEngine);
        state.store (static_cast<int> (EmbeddedEngineState::Ready), std::memory_order_relaxed);
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
    diag.audio_overflows.store (0, std::memory_order_relaxed);
    waitingForMidiAudioOnset.store (false, std::memory_order_relaxed);
}

void VintageEmulatorStudioProcessor::trimAudioBacklog (ves::EmbeddedEmulatorEngine& localEngine)
{
    auto& diag = localEngine.diagnostics();
    const auto queued = localEngine.queuedAudioFrames();
    const auto maxTolerated = static_cast<std::size_t> (diag.audio_max_tolerated_queue_frames.load (std::memory_order_relaxed));
    const auto target = static_cast<std::size_t> (diag.audio_target_queue_frames.load (std::memory_order_relaxed));

    if (queued > maxTolerated && queued > target)
        localEngine.discardOldestAudioFrames (queued - target);
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

    auto localEngine = std::atomic_load (&engine);
    if (localEngine != nullptr)
    {
        const auto& diag = localEngine->diagnostics();
        snapshot.mameThreadRunning = diag.machine_started.load (std::memory_order_relaxed) != 0
            && diag.machine_exited.load (std::memory_order_relaxed) == 0;
        snapshot.hostSampleRate = diag.juce_sample_rate.load (std::memory_order_relaxed);
        snapshot.mameSampleRate = diag.mame_sample_rate.load (std::memory_order_relaxed);
        snapshot.currentQueuedAudioFrames = diag.audio_current_queued_frames.load (std::memory_order_relaxed);
        snapshot.queuedAudioLatencyMs = static_cast<double> (diag.audio_queued_latency_us.load (std::memory_order_relaxed)) / 1000.0;
        snapshot.maxQueuedAudioFrames = diag.audio_max_queued_frames.load (std::memory_order_relaxed);
        snapshot.maxQueuedAudioLatencyMs = static_cast<double> (diag.audio_max_queued_latency_us.load (std::memory_order_relaxed)) / 1000.0;
        snapshot.midiBytesQueued = diag.midi_bytes_queued.load (std::memory_order_relaxed);
        snapshot.midiBytesConsumed = diag.midi_bytes_consumed.load (std::memory_order_relaxed);
        snapshot.audioFramesProduced = diag.audio_frames_written.load (std::memory_order_relaxed);
        snapshot.audioFramesConsumed = diag.audio_frames_read.load (std::memory_order_relaxed);
        snapshot.staleAudioFramesDiscarded = diag.audio_stale_frames_dropped.load (std::memory_order_relaxed);
        snapshot.audioUnderruns = diag.audio_underruns.load (std::memory_order_relaxed);
        snapshot.audioOverflows = diag.audio_overflows.load (std::memory_order_relaxed);
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
    restartSelectedMachine();
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
    restartSelectedMachine();
}

void VintageEmulatorStudioProcessor::clearExternalArtworkDirectory()
{
    if (configuredArtworkPath.isEmpty())
        return;

    configuredArtworkPath.clear();
    persistArtworkPath();
    restartSelectedMachine();
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

    {
        const juce::ScopedLock lock (machineSelectionLock);
        selectedMachineDriverName = driverName;
    }
    restartSelectedMachine();
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
    addRecentMediaPath (RecentMediaType::Floppy, file);
    if (profileSupportsMediaType (profile, EmbeddedMachineProfile::MediaType::Floppy) && std::atomic_load (&engine) != nullptr)
        restartSelectedMachine();
}

void VintageEmulatorStudioProcessor::ejectSelectedMedia()
{
    const auto* profile = findMachineProfileByDriverName (getSelectedMachineDriverName());
    configuredMediaPaths[floppyMediaPathProperty].clear();
    if (profileSupportsMediaType (profile, EmbeddedMachineProfile::MediaType::Floppy) && std::atomic_load (&engine) != nullptr)
        restartSelectedMachine();
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
            configuredMediaPaths[mediaPathKey].clear();
        else
        {
            configuredMediaPaths[mediaPathKey] = juce::String (result.applied_path);
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

void VintageEmulatorStudioProcessor::timerCallback()
{
    processFloppyHotSwapResults();
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
    addRecentMediaPath (RecentMediaType::CdRom, file);
    if (profileSupportsMediaType (profile, EmbeddedMachineProfile::MediaType::CdRom) && std::atomic_load (&engine) != nullptr)
        restartSelectedMachine();
}

void VintageEmulatorStudioProcessor::clearSelectedCdRom()
{
    const auto* profile = findMachineProfileByDriverName (getSelectedMachineDriverName());
    configuredMediaPaths[cdRomMediaPathProperty].clear();
    if (profileSupportsMediaType (profile, EmbeddedMachineProfile::MediaType::CdRom) && std::atomic_load (&engine) != nullptr)
        restartSelectedMachine();
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
    addRecentMediaPath (RecentMediaType::HardDisk, file);
    if (profileSupportsMediaType (profile, EmbeddedMachineProfile::MediaType::HardDisk) && std::atomic_load (&engine) != nullptr)
        restartSelectedMachine();
}

void VintageEmulatorStudioProcessor::clearSelectedHardDisk()
{
    const auto* profile = findMachineProfileByDriverName (getSelectedMachineDriverName());
    configuredMediaPaths[hardDiskMediaPathProperty].clear();
    if (profileSupportsMediaType (profile, EmbeddedMachineProfile::MediaType::HardDisk) && std::atomic_load (&engine) != nullptr)
        restartSelectedMachine();
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

void VintageEmulatorStudioProcessor::setVideoDisplayActive (bool active)
{
    if (auto localEngine = std::atomic_load (&engine))
        localEngine->setVideoDisplayActive (active);
}

void VintageEmulatorStudioProcessor::requestVideoCaptureWidth (int width)
{
    const auto quantized = juce::jlimit (1024, 4096, ((juce::jmax (width, 1) + 63) / 64) * 64);
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
