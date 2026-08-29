#pragma once

#include <JuceHeader.h>

#include "VintageEmulatorStudioProcessor.h"

class VESMediaIconButton;
class VESToolbarPopup;

class EmbeddedMissingRomLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    void setInterTypefaces (juce::Typeface::Ptr regular, juce::Typeface::Ptr semiBold);
    juce::Font regularFont (float height) const;
    juce::Font semiBoldFont (float height) const;

    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    juce::Font getPopupMenuFont() override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getLabelFont (juce::Label&) override;

    void drawComboBox (juce::Graphics&,
                       int width,
                       int height,
                       bool isButtonDown,
                       int buttonX,
                       int buttonY,
                       int buttonW,
                       int buttonH,
                       juce::ComboBox&) override;

    void drawPopupMenuItem (juce::Graphics&,
                            const juce::Rectangle<int>& area,
                            bool isSeparator,
                            bool isActive,
                            bool isHighlighted,
                            bool isTicked,
                            bool hasSubMenu,
                            const juce::String& text,
                            const juce::String& shortcutKeyText,
                            const juce::Drawable* icon,
                            const juce::Colour* textColour) override;

private:
    juce::Typeface::Ptr interRegular;
    juce::Typeface::Ptr interSemiBold;
};

class EmbeddedEmulatorDisplayComponent final : public juce::Component
{
public:
    explicit EmbeddedEmulatorDisplayComponent (VintageEmulatorStudioProcessor&);
    ~EmbeddedEmulatorDisplayComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool updateFrame();

    void mouseMove (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> getImageDestination() const;
    juce::Rectangle<int> getSourceBounds() const;
    void updateSourceBounds();
    void clearDisplayedFrame (uint64_t engineGeneration);
    void updateCaptureWidthForCurrentDisplay();
    void publishStableCaptureWidth();
    void recordMouseEvent (const juce::MouseEvent&, const juce::String&, int buttonOverride = 0);

    VintageEmulatorStudioProcessor& processor;
    juce::Image image;
    juce::Rectangle<int> sourceBounds;
    int pendingCaptureWidth = 1024;
    int publishedCaptureWidth = 0;
    juce::uint32 captureWidthDeadlineMs = 0;
    uint64_t displayedGeneration = 0;
    uint64_t displayedEngineGeneration = 0;
    juce::String lastError;
    juce::Point<float> lastJuceMouse;
    juce::Point<int> lastMameMouse { -1, -1 };
    juce::Point<float> lastMouseNormalized;
    bool lastMouseInsideImage = false;
    bool mouseIsDown = false;
    int lastMouseButton = 0;
    juce::String lastMouseEvent = "none";

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EmbeddedEmulatorDisplayComponent)
};

class VintageEmulatorStudioEditor final : public juce::AudioProcessorEditor,
                                                     private juce::ComboBox::Listener,
                                                     private juce::MultiTimer
{
public:
    explicit VintageEmulatorStudioEditor (VintageEmulatorStudioProcessor&);
    ~VintageEmulatorStudioEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    enum class ToolbarPopupType { none, rom, artwork, floppy, cdRom, hardDisk };

    void comboBoxChanged (juce::ComboBox* comboBoxThatHasChanged) override;
    void timerCallback (int timerId) override;

    void rebuildSynthList();
    void updateSynthBoxPresentation();
    void updateControlState();
    void updateStatus();
    void addLabel (juce::Label& label, const juce::String& text);
    juce::Rectangle<int> getCurrentDisplaySafeUserArea() const;
    void updateFitToScreenButtonVisibility();
    void setEditorSizeFittedToCurrentDisplay (int requestedWidth, int requestedHeight, bool preserveSavedSize);
    void saveSettledEditorSize();
    void showToolbarPopup (ToolbarPopupType type, juce::Component& anchor);
    void launchToolbarPopup (ToolbarPopupType type, juce::Component& anchor);
    void dismissUnsupportedToolbarPopup();
    void refreshActiveToolbarPopup();
    void browseRoms();
    void browseArtworks();
    void browseFloppy();
    void clearFloppy();
    void browseCdRom();
    void clearCdRom();
    void browseHardDisk();
    void clearHardDisk();

    VintageEmulatorStudioProcessor& processor;
    uint64_t lastFloppyHotSwapRevision = 0;
    EmbeddedMissingRomLookAndFeel lookAndFeel;
    EmbeddedEmulatorDisplayComponent mameDisplay;

    juce::Label synthLabel;
    juce::Label statusLabel;
    juce::Label romStatusLabel;
    juce::Label synthBoxPresentationLabel;

    juce::ComboBox synthBox;
    juce::Image logoImage;

    std::unique_ptr<VESMediaIconButton> romButton;
    std::unique_ptr<VESMediaIconButton> artworkButton;
    std::unique_ptr<VESMediaIconButton> floppyButton;
    std::unique_ptr<VESMediaIconButton> cdRomButton;
    std::unique_ptr<VESMediaIconButton> hardDiskButton;
    std::unique_ptr<VESMediaIconButton> fitToScreenButton;
    juce::Label romIconLabel;
    juce::Label artworkIconLabel;
    juce::Label floppyIconLabel;
    juce::Label cdRomIconLabel;
    juce::Label hardDiskIconLabel;
    juce::Component::SafePointer<juce::CallOutBox> activeToolbarPopup;
    juce::Component::SafePointer<VESToolbarPopup> activeToolbarPopupContent;
    ToolbarPopupType activeToolbarPopupType = ToolbarPopupType::none;
    uint64_t toolbarPopupRequest = 0;
    std::unique_ptr<juce::FileChooser> romsChooser;
    std::unique_ptr<juce::FileChooser> artworksChooser;
    std::unique_ptr<juce::FileChooser> mediaChooser;
    std::unique_ptr<juce::FileChooser> cdRomChooser;
    std::unique_ptr<juce::FileChooser> hardDiskChooser;
    int pendingEditorWidth = 1700;
    int pendingEditorHeight = 1100;
    juce::uint32 editorResizeDeadlineMs = 0;
    bool editorSizePending = false;
    uint64_t lastAppliedRestorationRevision = 0;
    uint64_t resizeRevisionAtRequest = 0;
    bool applyingRestoredSize = false;
    bool statusOnSecondaryRow = false;
    int secondaryHeaderBottom = 100;
    juce::Rectangle<int> statusSeparatorBounds;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VintageEmulatorStudioEditor)
};
