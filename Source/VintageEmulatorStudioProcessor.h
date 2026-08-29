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
    Failed,
    InstanceConflict
};

struct EmbeddedDiagnosticSnapshot
{
    EmbeddedEngineState engineState = EmbeddedEngineState::Stopped;
    juce::String engineStateText;
    juce::String machineName = "Yamaha TX81Z";
    uint64_t bootElapsedMs = 0;
    bool ready = false;
    bool mameThreadRunning = false;
    bool instanceConflict = false;
    uint64_t hostSampleRate = 0;
    uint64_t mameSampleRate = 0;
    uint64_t currentQueuedAudioFrames = 0;
    double queuedAudioLatencyMs = 0.0;
    uint64_t maxQueuedAudioFrames = 0;
    double maxQueuedAudioLatencyMs = 0.0;
    uint64_t midiMessagesReceived = 0;
    uint64_t midiBytesQueued = 0;
    uint64_t midiBytesConsumed = 0;
    uint64_t midiDroppedBeforeReady = 0;
    uint64_t audioFramesProduced = 0;
    uint64_t audioFramesConsumed = 0;
    uint64_t staleAudioFramesDiscarded = 0;
    uint64_t audioUnderruns = 0;
    uint64_t audioOverflows = 0;
    float audioPeak = 0.0f;
    uint64_t midiToAudioOnsetMs = 0;
    uint64_t bootReadyQueuedFrames = 0;
    double bootReadyQueuedLatencyMs = 0.0;
    juce::String romPath;
    bool selectedMachineRomFound = false;
    juce::String nvramStatus;
    juce::String lastError;
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
    uint64_t videoRasterError = 0;
    uint64_t videoRasterErrorIndex = 0;
    uint64_t videoTargetFrameRate = 0;
    juce::String videoState;
    double videoMeasuredFrameRate = 0.0;
    uint64_t videoLastFrameTimestampMs = 0;
    bool videoCaptureEnabled = false;
    bool videoEditorDisplayActive = false;
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
    bool hasInstanceConflict() const;
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
    void setVideoDisplayActive (bool active);
    void requestVideoCaptureWidth (int width);
    bool enqueueMouseEvent (ves::EmbeddedMouseEventType type, int x, int y);
    void requestMouseRelease();

private:
    void startEngineIfNeeded (double sampleRate);
    void stopEngine();
    void restartSelectedMachine();
    bool prepareNvramState();
    juce::File getRomsDirectory() const;
    juce::File getNvramSeedFile() const;
    juce::File getPluginDataDirectory() const;
    juce::String loadPersistedRomsPath() const;
    void persistRomsPath() const;
    juce::String loadPersistedArtworkPath() const;
    void persistArtworkPath() const;
    juce::String getMediaStateKey (const juce::String& driverName, const juce::String& instanceName) const;
    juce::String getMediaPathForSelectedMachine() const;
    juce::String getEffectiveMameArtworkPath() const;
    void addRecentMediaPath (RecentMediaType type, const juce::File& file);
    void updateBootState();
    void handleReadyTransition (ves::EmbeddedEmulatorEngine& localEngine);
    void trimAudioBacklog (ves::EmbeddedEmulatorEngine& localEngine);
    void flushAudioForTransportBoundary (ves::EmbeddedEmulatorEngine& localEngine);
    void timerCallback() override;
    static bool isSupportedMidiForPrototype (const juce::MidiMessage& message);

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
    std::atomic<bool> lastTransportPlaying { false };
    std::atomic<int> editorWidth { 1700 };
    std::atomic<int> editorHeight { 1100 };
    std::atomic<uint64_t> stateRestorationRevision { 0 };
    mutable juce::CriticalSection machineSelectionLock;
    juce::String selectedMachineDriverName { "tx81z" };
    std::atomic<int> requestedVideoCaptureWidth { 1024 };
    juce::String configuredRomsPath;
    juce::String configuredArtworkPath;
    std::map<juce::String, juce::String> configuredMediaPaths;
    std::atomic<uint64_t> floppyHotSwapRequestId { 0 };
    std::atomic<bool> floppyHotSwapPending { false };
    std::atomic<uint64_t> floppyHotSwapRevision { 0 };
    juce::String floppyHotSwapMessage;
    juce::String lastError;
    // This is deliberately invalid until JUCE has opened its selected device and
    // called prepareToPlay.  In particular, standalone state restoration must
    // not start MAME using a guessed sample rate.
    double currentSampleRate = 0.0;
    int maxBlockSize = 0;
    std::array<ves::StereoFrame, 8192> audioScratch {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VintageEmulatorStudioProcessor)
};
