// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <JuceHeader.h>

#include "../validation/mame-0.289-patched/src/ves/embeddedinstruments/EmbeddedEmulatorEngine.h"

#include <array>
#include <atomic>
#include <memory>
#include <map>
#include <vector>

enum class EmbeddedEngineState : int
{
    Stopped,
    Starting,
    Booting,
    Ready,
    Stopping,
    Failed
};

enum class StartupError
{
    None,
    RuntimeResources,
    PrimaryRomMissing,
    MissingRom,
    RomChecksumMismatch,
    InvalidRomSet,
    MediaUnavailable,
    MediaLoad,
    Configuration,
    Nvram,
    LuaPlugin,
    DeviceInitialization,
    AudioInitialization,
    EngineFailure,
    StartupTimeout,
    Unknown
};

enum class GuiPerformanceMode
{
    Normal,
    Reduced,
    Static,
    Disabled
};

struct StartupIssue
{
    juce::String name;
    juce::String owner;
    juce::String expectedCrc;
    juce::String expectedSha1;
    juce::String actualCrc;
    juce::String actualSha1;
    uint64_t expectedLength = 0;
    uint64_t actualLength = 0;
};

struct StartupDiagnostic
{
    StartupError category = StartupError::None;
    juce::String summary;
    juce::String details;
    juce::String recovery;
    juce::String technicalDetails;
    std::vector<StartupIssue> issues;
};

struct EmbeddedDiagnosticSnapshot
{
    EmbeddedEngineState engineState = EmbeddedEngineState::Stopped;
    juce::String engineStateText;
    juce::String machineName = "Yamaha TX81Z";
    uint64_t bootElapsedMs = 0;
    bool ready = false;
    bool mameThreadRunning = false;
    uint64_t hostSampleRate = 0;
    uint64_t mameSampleRate = 0;
    uint64_t currentQueuedAudioFrames = 0;
    double queuedAudioLatencyMs = 0.0;
    uint64_t maxQueuedAudioFrames = 0;
    double maxQueuedAudioLatencyMs = 0.0;
    uint64_t targetQueuedAudioFrames = 0;
    uint64_t maxToleratedQueuedAudioFrames = 0;
    uint64_t trimmedAudioFrames = 0;
    uint64_t midiMessagesReceived = 0;
    uint64_t midiBytesQueued = 0;
    uint64_t midiBytesConsumed = 0;
    uint64_t midiDroppedBeforeReady = 0;
    uint64_t audioFramesProduced = 0;
    uint64_t audioFramesConsumed = 0;
    uint64_t staleAudioFramesDiscarded = 0;
    uint64_t audioUnderruns = 0;
    uint64_t audioUnderrunCallbacks = 0;
    uint64_t audioMissingOutputFrames = 0;
    uint64_t audioOverflows = 0;
    uint64_t audioQueueReadLowWatermarkFrames = 0;
    uint64_t audioSinkLatestInterarrivalUs = 0;
    uint64_t audioSinkMaxInterarrivalUs = 0;
    uint64_t audioSinkLatestBlockFrames = 0;
    uint64_t audioSinkMinBlockFrames = 0;
    uint64_t audioSinkMaxBlockFrames = 0;
    uint64_t audioSinkRecentMaxBlockFrames = 0;
    uint64_t audioSinkBlocksApprox240 = 0;
    uint64_t audioSinkBlocksApprox480 = 0;
    uint64_t audioSinkBlocksApprox720 = 0;
    uint64_t audioSinkBlocks960Plus = 0;
    uint64_t audioSinkBlocksOther = 0;
    float audioPeak = 0.0f;
    uint64_t midiToAudioOnsetMs = 0;
    uint64_t bootReadyQueuedFrames = 0;
    double bootReadyQueuedLatencyMs = 0.0;
    juce::String romPath;
    bool selectedMachineRomFound = false;
    juce::String nvramStatus;
    juce::String lastError;
    StartupDiagnostic startupDiagnostic;
    bool videoInitialized = false;
    bool videoRenderTargetAvailable = false;
    int videoFrameWidth = 0;
    int videoFrameHeight = 0;
    double videoSourceAspectRatio = 0.0;
    uint64_t videoFramesProduced = 0;
    uint64_t videoFramesDisplayed = 0;
    uint64_t videoFramesDropped = 0;
    uint64_t videoFramesReplaced = 0;
    uint64_t videoFramesSkippedDeadline = 0;
    uint64_t videoFramesSkippedInactive = 0;
    uint64_t videoFramesSkippedPaused = 0;
    uint64_t videoFrameGeneration = 0;
    uint64_t videoCaptureRequested = 0;
    uint64_t videoCaptureStarted = 0;
    uint64_t videoCaptureCompleted = 0;
    bool videoCaptureInProgress = false;
    uint64_t videoRasterizationDurationUs = 0;
    uint64_t videoRasterizationTotalUs = 0;
    uint64_t videoRasterizationMaxUs = 0;
    uint64_t videoJobsSubmitted = 0;
    uint64_t videoJobsReplaced = 0;
    uint64_t videoJobsCompleted = 0;
    uint64_t videoWorkerPendingDepth = 0;
    uint64_t videoRasterError = 0;
    uint64_t videoRasterErrorIndex = 0;
    uint64_t videoTargetFrameRate = 0;
    juce::String videoState;
    double videoMeasuredFrameRate = 0.0;
    uint64_t videoLastFrameTimestampMs = 0;
    bool videoCaptureEnabled = false;
    bool videoEditorDisplayActive = false;
    GuiPerformanceMode guiPerformanceMode = GuiPerformanceMode::Normal;
    uint64_t videoTargetFlags = 0;
    uint64_t videoTargetGeneration = 0;
    uint64_t videoTargetOrientation = 0;
    double videoTargetPixelAspect = 0.0;
    juce::String videoSelectedView;
    juce::String lastVideoError;
    bool mouseForwardingEnabled = false;
    uint64_t mouseEventsEnqueued = 0;
    uint64_t mouseEventsConsumed = 0;
    uint64_t mouseDroppedMoveEvents = 0;
    uint64_t mouseCriticalEventFailures = 0;
    uint64_t mouseLastError = 0;
    uint64_t mouseQueueHighWatermark = 0;
    uint64_t mouseLastEventType = 0;
    int mouseCurrentX = -1;
    int mouseCurrentY = -1;
    bool mouseLeftDown = false;
    uint64_t mouseSyntheticReleaseCount = 0;
    uint64_t mousePointerTargetIndex = 0;
    bool mouseHitItem = false;
    uint64_t mouseHitInputTag = 0;
    uint64_t mouseHitInputMask = 0;
    bool mouseInputFieldActive = false;
};

struct EmbeddedVideoFrameForEditor
{
    int width = 0;
    int height = 0;
    uint64_t generation = 0;
    uint64_t engineGeneration = 0;
    uint64_t timestampMs = 0;
    std::vector<uint32_t> pixels;
};

class VintageEmulatorStudioProcessor final : public juce::AudioProcessor,
                                             private juce::Timer
{
public:
    enum class RecentMediaType { Floppy, CdRom, HardDisk };

    VintageEmulatorStudioProcessor();
    ~VintageEmulatorStudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    EmbeddedEngineState getEngineState() const;
    juce::String getEngineStateText() const;
    juce::String getLastError() const;
    uint64_t getBootElapsedMs() const;
    bool isReady() const;
    void sendMidiPanic();
    juce::String getSelectedMachineDriverName() const;
    juce::String getSelectedMachineName() const;
    juce::String getMachineNameForDisplayPosition (int displayPosition) const;
    juce::String getMachineDriverNameForDisplayPosition (int displayPosition) const;
    bool hasMachineRomByDriverName (const juce::String& driverName) const;
    int getNumMachineProfiles() const;
    bool selectMachineByDriverName (const juce::String& driverName);
    bool selectedMachineHasMedia() const;
    bool selectedMachineSupportsFloppy() const;
    bool selectedMachineSupportsFloppyHotSwap() const;
    bool selectedMachineSupportsCdRomHotSwap() const;
    bool selectedMachineSupportsCdRom() const;
    bool selectedMachineSupportsHardDisk() const;
    juce::String getSelectedMediaDisplayName() const;
    juce::String getSelectedMediaPath() const;
    void setSelectedMediaFile (const juce::File& file);
    void ejectSelectedMedia();
    bool requestSelectedFloppyHotSwap (const juce::File& file);
    bool requestSelectedFloppyEject();
    bool requestSelectedCdRomHotSwap (const juce::File& file);
    bool requestSelectedCdRomEject();
    bool isFloppyHotSwapPending() const;
    bool isCdRomHotSwapPending() const;
    juce::String getFloppyHotSwapMessage() const;
    bool processFloppyHotSwapResults();
    uint64_t getFloppyHotSwapRevision() const { return floppyHotSwapRevision.load (std::memory_order_acquire); }
    bool selectedMachineSupportsSaveState() const;
    bool selectedMachineSupportsStandaloneAutosave() const;
    bool processExperimentalStateResults();
    void finishExperimentalStateRestoreIfReady();
    juce::String getSelectedCdRomPath() const;
    void setSelectedCdRomFile (const juce::File& file);
    void clearSelectedCdRom();
    juce::String getSelectedHardDiskPath() const;
    void setSelectedHardDiskFile (const juce::File& file);
    void clearSelectedHardDisk();
    std::vector<juce::String> getRecentMediaPaths (RecentMediaType type) const;
    void removeRecentMediaPath (RecentMediaType type, const juce::String& path);

    uint64_t getMidiMessagesReceived() const { return midiMessagesReceived.load (std::memory_order_relaxed); }
    uint64_t getMidiDroppedBeforeReady() const { return midiDroppedBeforeReady.load (std::memory_order_relaxed); }
    uint64_t getAudioFramesReadByPlugin() const { return audioFramesReadByPlugin.load (std::memory_order_relaxed); }
    uint64_t getJuceMidiFirstReceivedMs() const { return juceMidiFirstReceivedMs.load (std::memory_order_relaxed); }
    uint64_t getJuceMidiLastReceivedMs() const { return juceMidiLastReceivedMs.load (std::memory_order_relaxed); }

    const ves::EngineDiagnostics* getEngineDiagnostics() const;
    EmbeddedDiagnosticSnapshot getDiagnosticSnapshot() const;
    juce::File getExternalRomsDirectory() const;
    void setExternalRomsDirectory (const juce::File& directory);
    juce::File getExternalArtworkDirectory() const;
    juce::String getExternalArtworkDirectoryPath() const;
    void setExternalArtworkDirectory (const juce::File& directory);
    void clearExternalArtworkDirectory();
    bool hasSelectedMachineRom() const;
    juce::String getNvramStatusText() const;
    int getSavedEditorWidth() const { return editorWidth.load (std::memory_order_relaxed); }
    int getSavedEditorHeight() const { return editorHeight.load (std::memory_order_relaxed); }
    uint64_t getStateRestorationRevision() const { return stateRestorationRevision.load (std::memory_order_acquire); }
    void setSavedEditorSize (int width, int height);
    bool copyLatestVideoFrame (EmbeddedVideoFrameForEditor& snapshot);
    uint64_t getVideoEngineGeneration() const { return videoEngineGeneration.load (std::memory_order_acquire); }
    uint64_t getVideoFrameResetGeneration() const;
    void setGuiPerformanceMode (GuiPerformanceMode mode);
    GuiPerformanceMode getGuiPerformanceMode() const;
    juce::Colour getBackgroundColour() const;
    void setBackgroundColour (juce::Colour colour);
#if JucePlugin_Build_Standalone
    void setStandaloneMasterVolume (float volume);
    float getStandaloneMasterVolume() const;
#endif
    void setVideoDisplayActive (bool active);
    void requestVideoCaptureWidth (int width);
    bool enqueueMouseEvent (ves::EmbeddedMouseEventType type, int x, int y);
    void requestMouseRelease();

private:
    void startEngineIfNeeded (double sampleRate, const juce::String& caller, const juce::String& reason);
    void stopEngine (const juce::String& caller, const juce::String& reason);
    void restartSelectedMachine (const juce::String& caller, const juce::String& reason);
    bool prepareNvramState (const juce::File& runtimeNvramDirectory);
    juce::File getRomsDirectory() const;
    juce::File getNvramSeedFile() const;
    juce::File getPluginDataDirectory() const;
    juce::String loadPersistedRomsPath() const;
    void persistRomsPath() const;
    juce::String loadPersistedArtworkPath() const;
    void persistArtworkPath() const;
    GuiPerformanceMode loadPersistedGuiPerformanceMode() const;
    void persistGuiPerformanceMode() const;
#if JucePlugin_Build_Standalone
    float loadPersistedStandaloneMasterVolume() const;
    void persistStandaloneMasterVolume() const;
#endif
    void applyGuiPerformanceModeToEngine();
    int getEffectiveVideoCaptureWidth (int requestedWidth) const;
    juce::String getMediaStateKey (const juce::String& driverName, const juce::String& instanceName) const;
    juce::String getMediaPathForSelectedMachine() const;
    juce::String getEffectiveMameArtworkPath() const;
    void addRecentMediaPath (RecentMediaType type, const juce::File& file);
    void updateBootState();
    void clearStartupDiagnostic();
    void setStartupDiagnostic (StartupDiagnostic diagnostic);
    void handleReadyTransition (ves::EmbeddedEmulatorEngine& localEngine);
    void sendMidiPanic (const char* source, ves::EmbeddedEmulatorEngine* readyEngine = nullptr,
                        const ves::HeldMidiNotes* snapshotHeldNotes = nullptr);
    void trackHostMidiMessage (const juce::MidiMessage& message);
    ves::HeldMidiNotes captureActiveMidiNotes() const;
    void clearActiveMidiNotes();
    void trimAudioBacklog (ves::EmbeddedEmulatorEngine& localEngine);
    void flushAudioForTransportBoundary (ves::EmbeddedEmulatorEngine& localEngine);
    void tryStandaloneAutosaveRestore();
    void saveStandaloneAutosaveOnShutdown();
    bool isDawPluginWrapper() const;
    void tryDawPendingRestore();
    void timerCallback() override;

    std::shared_ptr<ves::EmbeddedEmulatorEngine> engine;
    std::atomic<uint64_t> videoEngineGeneration { 0 };
    mutable std::atomic<int> state { static_cast<int> (EmbeddedEngineState::Stopped) };
    std::atomic<uint64_t> bootStartMs { 0 };
    std::atomic<uint64_t> midiMessagesReceived { 0 };
    std::atomic<uint64_t> midiDroppedBeforeReady { 0 };
    std::atomic<uint64_t> audioFramesReadByPlugin { 0 };
    std::atomic<uint64_t> juceMidiFirstReceivedMs { 0 };
    std::atomic<uint64_t> juceMidiLastReceivedMs { 0 };
    std::atomic<uint64_t> lastMidiSentToEngineMs { 0 };
    std::atomic<bool> waitingForMidiAudioOnset { false };
    std::array<std::atomic<std::uint64_t>, 32> activeHostMidiNotes {};
    std::atomic<bool> lastTransportPlaying { false };
    std::atomic<int> editorWidth { 1700 };
    std::atomic<int> editorHeight { 1100 };
    std::atomic<uint64_t> stateRestorationRevision { 0 };
    mutable juce::CriticalSection machineSelectionLock;
    juce::String selectedMachineDriverName { "tx81z" };
    std::atomic<int> requestedVideoCaptureWidth { 1792 };
    std::atomic<int> guiPerformanceMode { static_cast<int> (GuiPerformanceMode::Normal) };
    std::atomic<std::uint32_t> backgroundColourArgb { 0xff6f6eba };
#if JucePlugin_Build_Standalone
    std::atomic<float> standaloneMasterVolume { 1.0f };
    juce::LinearSmoothedValue<float> standaloneMasterGain { 1.0f };
#endif
    juce::String configuredRomsPath;
    juce::String configuredArtworkPath;
    std::map<juce::String, juce::String> configuredMediaPaths;
    std::map<juce::String, juce::String> configuredMediaPathOrigins;
    mutable juce::CriticalSection startupDiagnosticLock;
    StartupDiagnostic startupDiagnostic;
    std::atomic<uint64_t> floppyHotSwapRequestId { 0 };
    std::atomic<bool> floppyHotSwapPending { false };
    std::atomic<uint64_t> floppyHotSwapRevision { 0 };
    juce::String floppyHotSwapMessage;
    std::atomic<uint64_t> experimentalStateRequestId { 0 };
    std::atomic<bool> experimentalStatePending { false };
    std::atomic<bool> experimentalStateRestoreInProgress { false };
    juce::String experimentalStateStatus;
    uint64_t experimentalStateGenerationBeforeRestore = 0;
    uint64_t experimentalStateGenerationAfterRestore = 0;
    uint64_t experimentalStateGuardStartedMs = 0;
    enum class ExperimentalStateTarget { Autosave, DawRestore, StandaloneSwitchSave };
    ExperimentalStateTarget experimentalStateTarget = ExperimentalStateTarget::Autosave;
    uint64_t standaloneAutosaveRestoreAttemptedGeneration = 0;
    mutable juce::CriticalSection dawStateLock;
    struct CachedStateSnapshot
    {
        juce::String driver;
        uint64_t engineGeneration = 0;
        uint32_t compatibilityRevision = 0;
        std::shared_ptr<const std::vector<std::uint8_t>> blob;
        ves::HeldMidiNotes heldMidiNotes {};
        uint64_t capturedAtMs = 0;
    };
    CachedStateSnapshot dawCachedSnapshot;
    std::vector<std::uint8_t> dawPendingRestoreSnapshot;
    juce::String dawPendingRestoreDriver;
    uint32_t dawPendingRestoreCompatibilityRevision = 0;
    ves::HeldMidiNotes dawPendingRestoreHeldMidiNotes {};
    bool dawPendingRestoreArmed = false;
    juce::String pendingStandaloneSwitchDriver;
    juce::String pendingStandaloneSwitchTarget;
    juce::String lastError;
    // This is deliberately invalid until JUCE has opened its selected device and
    // called prepareToPlay.  In particular, standalone state restoration must
    // not start MAME using a guessed sample rate.
    double currentSampleRate = 0.0;
    int maxBlockSize = 0;
    std::array<ves::StereoFrame, 8192> audioScratch {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VintageEmulatorStudioProcessor)
};
