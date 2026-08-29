#include "VintageEmulatorStudioEditor.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <vector>

namespace
{
constexpr int margin = 22;
constexpr int rowHeight = 32;
constexpr int headerHeight = 100;
constexpr int logoWidth = 708;
constexpr int minimumEditorWidth = 800;
constexpr int minimumEditorHeight = 520;
constexpr int maximumEditorWidth = 2400;
constexpr int maximumEditorHeight = 1600;
constexpr int fitToScreenSafetyMargin = 24;
constexpr double fitToScreenUserAreaFraction = 0.95;
constexpr float fieldLabelFontSize = 16.0f;
const auto bootingStatusColour = juce::Colour::fromRGB (255, 210, 63);
const auto readyStatusColour = juce::Colour::fromRGB (69, 227, 90);
const auto failedStatusColour = juce::Colour::fromRGB (255, 69, 69);
constexpr int statusLabelWidth = 50;
constexpr int statusSeparatorWidth = 8;
constexpr int statusRomWidth = 86;
constexpr int statusGroupGap = 4;
constexpr int statusGroupRightMargin = 18;

juce::String formatMachineNameForSelector (const juce::String& displayName)
{
    const auto firstSpace = displayName.indexOfChar (' ');
    return firstSpace > 0 ? displayName.substring (0, firstSpace) + " — " + displayName.substring (firstSpace + 1)
                          : displayName;
}

}

class VESMediaIconButton final : public juce::Button
{
public:
    enum class VisualState { normal, selected, popupOpen };

    VESMediaIconButton (const juce::String& tooltip,
                        const void* resourceData,
                        int resourceSize,
                        const juce::String& fallbackText)
        : juce::Button (tooltip), fallback (fallbackText)
    {
        setTooltip (tooltip);
        setWantsKeyboardFocus (false);

        if (resourceData != nullptr && resourceSize > 0)
            icon = juce::Drawable::createFromImageData (resourceData, static_cast<size_t> (resourceSize));
    }

    void setVisualState (VisualState newState)
    {
        if (visualState != newState)
        {
            visualState = newState;
            repaint();
        }
    }

    void paintButton (juce::Graphics& g, bool isMouseOverButton, bool isButtonDown) override
    {
        const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
        const auto enabled = isEnabled();
        const auto popupOpen = enabled && visualState == VisualState::popupOpen;
        const auto selected = enabled && visualState == VisualState::selected;
        const auto background = ! enabled ? juce::Colour::fromRGB (27, 27, 30)
                              : (popupOpen ? juce::Colour::fromRGB (63, 83, 112)
                              : (selected ? juce::Colour::fromRGB (34, 74, 48)
                              : (isButtonDown ? juce::Colour::fromRGB (57, 100, 115)
                              : (isMouseOverButton ? juce::Colour::fromRGB (50, 83, 95)
                                                   : juce::Colour::fromRGB (38, 64, 74)))));
        g.setColour (background);
        g.fillRoundedRectangle (bounds, 4.0f);
        if (enabled)
        {
            g.setColour (popupOpen ? juce::Colour::fromRGB (138, 184, 241)
                                   : (selected ? juce::Colour::fromRGB (69, 139, 88)
                                               : juce::Colour::fromRGB (60, 151, 182)));
            g.drawRoundedRectangle (bounds, 4.0f, 1.0f);
        }

        if (icon != nullptr)
        {
            juce::Graphics::ScopedSaveState state (g);
            auto drawable = icon->createCopy();
            drawable->replaceColour (juce::Colours::white,
                                     ! enabled ? juce::Colour::fromRGB (100, 100, 105)
                                               : (popupOpen ? juce::Colours::white
                                                            : (selected ? juce::Colour::fromRGB (81, 161, 105)
                                                                        : juce::Colour::fromRGB (53, 201, 232))));
            g.setOpacity (enabled ? 1.0f : 0.28f);
            drawable->drawWithin (g, getLocalBounds().reduced (8).toFloat(), juce::RectanglePlacement::centred, 1.0f);
            return;
        }

        g.setColour (enabled ? (popupOpen ? juce::Colours::white
                                          : (selected ? juce::Colour::fromRGB (81, 161, 105)
                                                      : juce::Colour::fromRGB (53, 201, 232)))
                             : juce::Colour::fromRGB (100, 100, 105));
        g.setOpacity (enabled ? 1.0f : 0.28f);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawFittedText (fallback, getLocalBounds().reduced (2), juce::Justification::centred, 2);

        if (isMouseOverButton)
        {
            g.setColour (juce::Colours::white.withAlpha (enabled ? 0.08f : 0.025f));
            g.fillRoundedRectangle (bounds.reduced (2.0f), 3.0f);
        }
    }

private:
    std::unique_ptr<juce::Drawable> icon;
    juce::String fallback;
    VisualState visualState = VisualState::normal;
};

class VESRecentMediaButton final : public juce::TextButton
{
public:
    void setCurrentMedia (bool isCurrent)
    {
        if (currentMedia != isCurrent)
        {
            currentMedia = isCurrent;
            repaint();
        }
    }

    void paintButton (juce::Graphics& g, bool isMouseOverButton, bool isButtonDown) override
    {
        getLookAndFeel().drawButtonBackground (g, *this, findColour (buttonColourId), isMouseOverButton, isButtonDown);

        const auto textColour = findColour (isEnabled() ? textColourOnId : textColourOffId);
        g.setColour (textColour);
        g.setFont (getLookAndFeel().getTextButtonFont (*this, getHeight()));
        g.drawFittedText (getButtonText(), getLocalBounds().withTrimmedLeft (24).reduced (4, 0),
                          juce::Justification::centredLeft, 1);

        if (currentMedia)
        {
            constexpr float indicatorDiameter = 9.0f;
            const auto bounds = getLocalBounds().toFloat();
            const auto indicator = juce::Rectangle<float> (8.0f,
                                                             bounds.getCentreY() - indicatorDiameter * 0.5f,
                                                             indicatorDiameter,
                                                             indicatorDiameter);
            g.setColour (juce::Colour::fromRGB (69, 139, 88));
            g.fillEllipse (indicator);
        }
    }

private:
    bool currentMedia = false;
};

class VESToolbarPopup final : public juce::Component,
                              private juce::Timer
{
public:
    using StringProvider = std::function<juce::String()>;
    using Action = std::function<void()>;
    using RecentPathProvider = std::function<std::vector<juce::String>()>;
    using RecentSelectionAction = std::function<juce::String (const juce::String&)>;

    VESToolbarPopup (const juce::String& popupTitle,
                     juce::LookAndFeel& popupLookAndFeel,
                     const juce::Font& regularFont,
                     const juce::Font& semiBoldFont,
                     StringProvider currentPath,
                     const juce::String& emptyPathText,
                     StringProvider currentStatus,
                     const juce::String& detailText,
                     Action browseAction,
                     Action clearAction,
                     const juce::String& clearButtonText,
                     RecentPathProvider recentPaths = {},
                     RecentSelectionAction recentSelection = {},
                     std::function<bool()> operationPending = {},
                     bool showBusyIndicatorForPendingLoad = false)
        : pathProvider (std::move (currentPath)),
          statusProvider (std::move (currentStatus)),
          onBrowse (std::move (browseAction)),
          onClear (std::move (clearAction)),
          recentPathsProvider (std::move (recentPaths)),
          onRecentSelection (std::move (recentSelection)),
          operationPendingProvider (std::move (operationPending)),
          showBusyIndicatorForPendingLoad (showBusyIndicatorForPendingLoad),
          emptyPath (emptyPathText),
          detail (detailText),
          browseButton ("Browse..."),
          clearButton (clearButtonText)
    {
        setLookAndFeel (&popupLookAndFeel);
        titleLabel.setText (popupTitle, juce::dontSendNotification);
        titleLabel.setFont (semiBoldFont.withHeight (22.0f));
        titleLabel.setColour (juce::Label::textColourId, juce::Colour::fromRGB (25, 25, 25));
        pathLabel.setFont (regularFont.withHeight (18.0f));
        pathLabel.setJustificationType (juce::Justification::centredLeft);
        pathLabel.setColour (juce::Label::textColourId, juce::Colour::fromRGB (20, 20, 20));
        pathLabel.setColour (juce::Label::backgroundColourId, juce::Colours::white);
        pathLabel.setColour (juce::Label::outlineColourId, juce::Colour::fromRGB (180, 180, 180));
        statusLabel.setFont (regularFont.withHeight (15.5f));
        statusLabel.setColour (juce::Label::textColourId, juce::Colour::fromRGB (45, 45, 45));
        detailLabel.setFont (regularFont.withHeight (15.5f));
        detailLabel.setJustificationType (juce::Justification::centredLeft);
        detailLabel.setColour (juce::Label::textColourId, juce::Colour::fromRGB (55, 55, 55));
        detailLabel.setText (detail, juce::dontSendNotification);
        recentLabel.setText ("Recent", juce::dontSendNotification);
        recentLabel.setFont (semiBoldFont.withHeight (15.5f));
        recentLabel.setColour (juce::Label::textColourId, juce::Colour::fromRGB (45, 45, 45));
        browseButton.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
        browseButton.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        clearButton.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
        clearButton.setColour (juce::TextButton::textColourOnId, juce::Colours::white);

        addAndMakeVisible (titleLabel);
        addAndMakeVisible (pathLabel);
        addAndMakeVisible (browseButton);
        if (onClear)
            addAndMakeVisible (clearButton);
        if (statusProvider)
            addAndMakeVisible (statusLabel);
        if (detail.isNotEmpty())
            addAndMakeVisible (detailLabel);
        if (recentPathsProvider)
        {
            addAndMakeVisible (recentLabel);
            for (int index = 0; index < maximumRecentItems; ++index)
            {
                auto button = std::make_unique<VESRecentMediaButton>();
                button->setColour (juce::TextButton::textColourOffId, juce::Colours::white);
                button->setColour (juce::TextButton::textColourOnId, juce::Colours::white);
                button->setMouseCursor (juce::MouseCursor::PointingHandCursor);
                button->onClick = [this, index] { selectRecentPath (index); };
                addChildComponent (*button);
                recentButtons[static_cast<std::size_t> (index)] = std::move (button);
            }
        }

        browseButton.onClick = [this]
        {
            if (onBrowse)
                onBrowse();
            refreshFromProcessor();
        };
        clearButton.onClick = [this]
        {
            if (onClear)
                onClear();
            refreshFromProcessor();
        };

        setSize (360, baseHeight());
        refreshFromProcessor();
    }

    ~VESToolbarPopup() override
    {
        stopTimer();
        setLookAndFeel (nullptr);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour::fromRGB (246, 246, 246));
        if (recentPathsProvider)
        {
            auto dividerY = 12 + 28 + 6 + 34 + 8 + 34;
            if (hasStatusLine())
                dividerY += 6 + statusHeight();
            if (detail.isNotEmpty())
                dividerY += 6 + detailHeight();
            g.setColour (juce::Colour::fromRGB (210, 210, 210));
            g.drawLine (14.0f, static_cast<float> (dividerY + 4), static_cast<float> (getWidth() - 14), static_cast<float> (dividerY + 4));
        }

        if (busyIndicatorVisible && ! busyIndicatorBounds.isEmpty())
        {
            juce::Path arc;
            arc.addCentredArc (busyIndicatorBounds.getCentreX(), busyIndicatorBounds.getCentreY(),
                               busyIndicatorBounds.getWidth() * 0.5f, busyIndicatorBounds.getHeight() * 0.5f,
                               0.0f, spinnerAngle, spinnerAngle + juce::MathConstants<float>::twoPi * 0.72f, true);
            g.setColour (juce::Colour::fromRGB (67, 122, 86));
            g.strokePath (arc, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (14, 12);
        titleLabel.setBounds (area.removeFromTop (28));
        area.removeFromTop (6);
        pathLabel.setBounds (area.removeFromTop (34));
        area.removeFromTop (8);

        auto buttons = area.removeFromTop (34);
        browseButton.setBounds (buttons.removeFromLeft (104));
        if (onClear)
        {
            buttons.removeFromLeft (8);
            clearButton.setBounds (buttons.removeFromLeft (112));
        }

        if (hasStatusLine())
        {
            area.removeFromTop (6);
            auto statusArea = area.removeFromTop (statusHeight());
            busyIndicatorBounds = {};
            if (busyIndicatorVisible)
            {
                busyIndicatorBounds = statusArea.removeFromRight (20).withSizeKeepingCentre (14, 14);
                statusArea.removeFromRight (4);
            }
            statusLabel.setBounds (statusArea);
        }

        if (detail.isNotEmpty())
        {
            area.removeFromTop (6);
            detailLabel.setBounds (area.removeFromTop (detailHeight()));
        }

        if (recentPathsProvider)
        {
            area.removeFromTop (8);
            recentLabel.setBounds (area.removeFromTop (20));
            for (int index = 0; index < visibleRecentItemCount; ++index)
            {
                area.removeFromTop (2);
                recentButtons[static_cast<std::size_t> (index)]->setBounds (area.removeFromTop (26));
            }
        }
    }

    void refreshFromProcessor()
    {
        const auto path = pathProvider ? pathProvider() : juce::String();
        pathLabel.setText (path.isNotEmpty() ? path : emptyPath, juce::dontSendNotification);
        pathLabel.setTooltip (path.isNotEmpty() ? path : emptyPath);
        const auto operationPending = operationPendingProvider && operationPendingProvider();
        browseButton.setEnabled (! operationPending);
        if (onClear)
            clearButton.setEnabled (path.isNotEmpty() && ! operationPending);

        auto status = statusProvider ? statusProvider() : juce::String();
        busyIndicatorVisible = showBusyIndicatorForPendingLoad && operationPending
                            && status.startsWithIgnoreCase ("Loading ");
        if (busyIndicatorVisible)
        {
            auto fileName = status.fromFirstOccurrenceOf ("Loading ", false, false).trim();
            if (fileName.endsWith ("..."))
                fileName = fileName.dropLastCharacters (3);
            status = "Loading CD-ROM...\n" + fileName;
            if (! isTimerRunning())
                startTimerHz (25);
        }
        else
        {
            stopTimer();
        }
        if (status.isEmpty() && recentError.isNotEmpty())
            status = recentError;
        statusLabel.setText (status, juce::dontSendNotification);
        statusLabel.setVisible (hasStatusLine());

        if (recentPathsProvider)
        {
            recentPaths = recentPathsProvider();
            if (static_cast<int> (recentPaths.size()) > maximumRecentItems)
                recentPaths.resize (maximumRecentItems);

            visibleRecentItemCount = static_cast<int> (recentPaths.size());
            for (int index = 0; index < maximumRecentItems; ++index)
            {
                auto& button = recentButtons[static_cast<std::size_t> (index)];
                const auto isVisible = index < visibleRecentItemCount;
                button->setVisible (isVisible);
                if (isVisible)
                {
                    const auto& recentPath = recentPaths[static_cast<std::size_t> (index)];
                    button->setButtonText (juce::File (recentPath).getFileName());
                    button->setCurrentMedia (recentPath == path);
                    button->setTooltip (recentPath);
                    button->setEnabled (! operationPending);
                }
            }

            const auto requiredHeight = baseHeight() + 28 + (visibleRecentItemCount * 28);
            if (getHeight() != requiredHeight)
                setSize (getWidth(), requiredHeight);
        }

        resized();
        repaint();
    }

    void timerCallback() override
    {
        // This only polls the processor's atomic pending flag; it never waits for MAME or touches the filesystem.
        if (! (operationPendingProvider && operationPendingProvider()))
        {
            refreshFromProcessor();
            return;
        }

        spinnerAngle += juce::MathConstants<float>::twoPi / 25.0f;
        repaint (busyIndicatorBounds.expanded (2));
    }

    int detailHeight() const
    {
        return detail.containsChar ('\n') ? 42 : 24;
    }

    int baseHeight() const
    {
        return 136 + (hasStatusLine() ? statusHeight() + 8 : 0) + (detail.isNotEmpty() ? detailHeight() + 4 : 0);
    }

    int statusHeight() const { return busyIndicatorVisible ? 42 : 20; }

    bool hasStatusLine() const { return statusProvider || recentError.isNotEmpty(); }

    void selectRecentPath (int index)
    {
        if (operationPendingProvider && operationPendingProvider())
            return;
        if (index < 0 || index >= static_cast<int> (recentPaths.size()) || ! onRecentSelection)
            return;

        recentError = onRecentSelection (recentPaths[static_cast<std::size_t> (index)]);
        if (recentError.isNotEmpty() && ! statusLabel.isVisible())
            addAndMakeVisible (statusLabel);
        refreshFromProcessor();
    }

    static constexpr int maximumRecentItems = 10;
    StringProvider pathProvider;
    StringProvider statusProvider;
    Action onBrowse;
    Action onClear;
    RecentPathProvider recentPathsProvider;
    RecentSelectionAction onRecentSelection;
    std::function<bool()> operationPendingProvider;
    bool showBusyIndicatorForPendingLoad = false;
    bool busyIndicatorVisible = false;
    float spinnerAngle = 0.0f;
    juce::String emptyPath;
    juce::String detail;
    juce::String recentError;
    std::vector<juce::String> recentPaths;
    int visibleRecentItemCount = 0;
    juce::Label titleLabel;
    juce::Label pathLabel;
    juce::Label statusLabel;
    juce::Label detailLabel;
    juce::Label recentLabel;
    juce::TextButton browseButton;
    juce::TextButton clearButton;
    juce::Rectangle<int> busyIndicatorBounds;
    std::array<std::unique_ptr<VESRecentMediaButton>, maximumRecentItems> recentButtons;
};

void EmbeddedMissingRomLookAndFeel::setInterTypefaces (juce::Typeface::Ptr regular,
                                                        juce::Typeface::Ptr semiBold)
{
    interRegular = std::move (regular);
    interSemiBold = std::move (semiBold);
}

juce::Font EmbeddedMissingRomLookAndFeel::regularFont (float height) const
{
    auto options = juce::FontOptions().withHeight (height);
    if (interRegular != nullptr)
        options = options.withTypeface (interRegular);
    return juce::Font (options);
}

juce::Font EmbeddedMissingRomLookAndFeel::semiBoldFont (float height) const
{
    auto options = juce::FontOptions().withHeight (height);
    if (interSemiBold != nullptr)
        options = options.withTypeface (interSemiBold);
    else if (interRegular != nullptr)
        options = options.withTypeface (interRegular);
    return juce::Font (options);
}

juce::Font EmbeddedMissingRomLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return semiBoldFont (juce::jlimit (15.0f, 17.0f, static_cast<float> (buttonHeight) * 0.48f));
}

juce::Font EmbeddedMissingRomLookAndFeel::getPopupMenuFont()
{
    return regularFont (17.0f);
}

juce::Font EmbeddedMissingRomLookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return regularFont (18.0f);
}

juce::Font EmbeddedMissingRomLookAndFeel::getLabelFont (juce::Label&)
{
    return regularFont (16.0f);
}

void EmbeddedMissingRomLookAndFeel::drawComboBox (juce::Graphics& g,
                                                  int width,
                                                  int height,
                                                  bool isButtonDown,
                                                  int buttonX,
                                                  int buttonY,
                                                  int buttonW,
                                                  int buttonH,
                                                  juce::ComboBox& box)
{
    const auto bounds = juce::Rectangle<float> (0.5f, 0.5f,
                                                static_cast<float> (width) - 1.0f,
                                                static_cast<float> (height) - 1.0f);
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (bounds, 3.0f);
    const auto focused = box.hasKeyboardFocus (true) || box.isMouseOverOrDragging (true);
    g.setColour (focused ? juce::Colour::fromRGB (53, 201, 232)
                         : box.findColour (juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle (bounds, 3.0f, isButtonDown || focused ? 2.0f : 1.0f);

    const auto arrowArea = juce::Rectangle<float> (static_cast<float> (buttonX),
                                                    static_cast<float> (buttonY),
                                                    static_cast<float> (buttonW),
                                                    static_cast<float> (buttonH)).reduced (8.0f);
    juce::Path arrow;
    arrow.addTriangle (arrowArea.getX(), arrowArea.getY(),
                       arrowArea.getRight(), arrowArea.getY(),
                       arrowArea.getCentreX(), arrowArea.getBottom());
    g.setColour (box.findColour (juce::ComboBox::arrowColourId));
    g.fillPath (arrow);
}

void EmbeddedMissingRomLookAndFeel::drawPopupMenuItem (juce::Graphics& g,
                                                       const juce::Rectangle<int>& area,
                                                       bool isSeparator,
                                                       bool isActive,
                                                       bool isHighlighted,
                                                       bool isTicked,
                                                       bool hasSubMenu,
                                                       const juce::String& text,
                                                       const juce::String& shortcutKeyText,
                                                       const juce::Drawable* icon,
                                                       const juce::Colour* textColour)
{
    juce::Colour colour;

    if (text.contains ("(Missing ROM)"))
        colour = juce::Colours::red;
    else if (textColour != nullptr)
        colour = *textColour;
    else
        colour = findColour (juce::PopupMenu::textColourId);

    LookAndFeel_V4::drawPopupMenuItem (g, area, isSeparator, isActive, isHighlighted, isTicked, hasSubMenu,
                                       text, shortcutKeyText, icon, &colour);
}

EmbeddedEmulatorDisplayComponent::EmbeddedEmulatorDisplayComponent (VintageEmulatorStudioProcessor& p)
    : processor (p)
{
}

EmbeddedEmulatorDisplayComponent::~EmbeddedEmulatorDisplayComponent()
{
}

bool EmbeddedEmulatorDisplayComponent::updateFrame()
{
    updateCaptureWidthForCurrentDisplay();
    publishStableCaptureWidth();

    const auto activeEngineGeneration = processor.getVideoEngineGeneration();
    if (activeEngineGeneration != displayedEngineGeneration)
        clearDisplayedFrame (activeEngineGeneration);

    EmbeddedVideoFrameForEditor frame;
    if (! processor.copyLatestVideoFrame (frame))
        return false;

    if (frame.engineGeneration != activeEngineGeneration)
        return false;

    if (frame.engineGeneration == displayedEngineGeneration && frame.generation == displayedGeneration)
        return false;

    if (frame.width <= 0 || frame.height <= 0
        || frame.pixels.size() != static_cast<std::size_t> (frame.width) * static_cast<std::size_t> (frame.height))
    {
        lastError = "Invalid MAME video frame";
        return false;
    }

    if (! image.isValid() || image.getWidth() != frame.width || image.getHeight() != frame.height)
        image = juce::Image (juce::Image::ARGB, frame.width, frame.height, true);

    juce::Image::BitmapData data (image, juce::Image::BitmapData::writeOnly);
    for (int y = 0; y < frame.height; ++y)
    {
        for (int x = 0; x < frame.width; ++x)
        {
            const auto argb = frame.pixels[static_cast<std::size_t> (y) * static_cast<std::size_t> (frame.width) + static_cast<std::size_t> (x)];
            auto* pixel = reinterpret_cast<juce::PixelARGB*> (data.getPixelPointer (x, y));
            pixel->setARGB (0xff,
                            static_cast<uint8_t> ((argb >> 16) & 0xff),
                            static_cast<uint8_t> ((argb >> 8) & 0xff),
                            static_cast<uint8_t> (argb & 0xff));
        }
    }

    displayedGeneration = frame.generation;
    displayedEngineGeneration = frame.engineGeneration;
    updateSourceBounds();
    lastError.clear();
    repaint();
    return true;
}

void EmbeddedEmulatorDisplayComponent::clearDisplayedFrame (uint64_t engineGeneration)
{
    image = {};
    sourceBounds = {};
    displayedGeneration = 0;
    displayedEngineGeneration = engineGeneration;
    lastError.clear();
    repaint();
}

void EmbeddedEmulatorDisplayComponent::resized()
{
    updateCaptureWidthForCurrentDisplay();
}

void EmbeddedEmulatorDisplayComponent::updateCaptureWidthForCurrentDisplay()
{
    const auto innerWidth = juce::jmax (1, getLocalBounds().reduced (10).getWidth());
    const auto& displays = juce::Desktop::getInstance().getDisplays();
    auto* display = displays.getDisplayForRect (getScreenBounds());
    if (display == nullptr)
        display = displays.getPrimaryDisplay();

    const auto scale = display != nullptr ? display->scale : getDesktopScaleFactor();
    const auto physicalWidth = juce::jmax (1, juce::roundToInt (innerWidth * scale));
    const auto quantized = juce::jlimit (1024, 4096, ((physicalWidth + 63) / 64) * 64);
    if (quantized != pendingCaptureWidth)
    {
        pendingCaptureWidth = quantized;
        captureWidthDeadlineMs = juce::Time::getMillisecondCounter() + 400;
    }
}

void EmbeddedEmulatorDisplayComponent::publishStableCaptureWidth()
{
    if (captureWidthDeadlineMs == 0
        || static_cast<juce::int32> (juce::Time::getMillisecondCounter() - captureWidthDeadlineMs) < 0
        || pendingCaptureWidth == publishedCaptureWidth)
        return;

    processor.requestVideoCaptureWidth (pendingCaptureWidth);
    publishedCaptureWidth = pendingCaptureWidth;
}

void EmbeddedEmulatorDisplayComponent::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds();
    g.fillAll (juce::Colour::fromRGB (111, 110, 186));
    g.setColour (juce::Colour::fromRGB (70, 70, 70));
    g.drawRect (bounds);

    if (! image.isValid())
    {
        auto area = bounds.reduced (10);
        g.setColour (juce::Colour::fromRGB (35, 35, 35));
        g.fillRect (area);
        g.setColour (juce::Colours::white);
        g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
        const auto snapshot = processor.getDiagnosticSnapshot();
        const auto text = snapshot.lastVideoError.isNotEmpty() ? snapshot.lastVideoError
            : (! snapshot.videoRenderTargetAvailable ? "Starting " + snapshot.machineName + "..."
                                                      : "Waiting for " + snapshot.machineName + " frame...");
        g.drawFittedText (text,
                          area, juce::Justification::centred, 2);
        return;
    }

    if (image.isValid())
    {
        const auto target = getImageDestination();
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        const auto source = getSourceBounds();
        g.drawImage (image,
                     juce::roundToInt (target.getX()), juce::roundToInt (target.getY()),
                     juce::roundToInt (target.getWidth()), juce::roundToInt (target.getHeight()),
                     source.getX(), source.getY(), source.getWidth(), source.getHeight(), false);

        return;
    }

    const auto snapshot = processor.getDiagnosticSnapshot();
    g.setColour (juce::Colour::fromRGB (210, 210, 210));
    g.setFont (juce::FontOptions (16.0f, juce::Font::bold));

    juce::String text;
    if (snapshot.lastVideoError.isNotEmpty() || lastError.isNotEmpty())
        text = snapshot.lastVideoError.isNotEmpty() ? snapshot.lastVideoError : lastError;
    else if (! snapshot.videoInitialized)
        text = "Starting " + snapshot.machineName + "...";
    else if (! snapshot.videoRenderTargetAvailable)
        text = "MAME render target unavailable";
    else
        text = "Waiting for " + snapshot.machineName + " frame...";

    g.drawFittedText (text, bounds.reduced (20), juce::Justification::centred, 2);
}

juce::Rectangle<float> EmbeddedEmulatorDisplayComponent::getImageDestination() const
{
    if (! image.isValid() || getWidth() <= 20 || getHeight() <= 20)
        return {};

    const auto area = getLocalBounds().reduced (10).toFloat();
    const auto source = getSourceBounds();
    const auto scale = juce::jmin (area.getWidth() / static_cast<float> (source.getWidth()),
                                   area.getHeight() / static_cast<float> (source.getHeight()));
    if (! std::isfinite (scale) || scale <= 0.0f)
        return {};

    const auto drawWidth = static_cast<float> (source.getWidth()) * scale;
    const auto drawHeight = static_cast<float> (source.getHeight()) * scale;
    return { area.getCentreX() - drawWidth * 0.5f,
             area.getCentreY() - drawHeight * 0.5f,
             drawWidth,
             drawHeight };
}

juce::Rectangle<int> EmbeddedEmulatorDisplayComponent::getSourceBounds() const
{
    return sourceBounds.isEmpty() ? image.getBounds() : sourceBounds;
}

void EmbeddedEmulatorDisplayComponent::updateSourceBounds()
{
    if (! image.isValid())
        return;

    juce::Image::BitmapData pixels (image, juce::Image::BitmapData::readOnly);
    int minX = image.getWidth();
    int minY = image.getHeight();
    int maxX = -1;
    int maxY = -1;

    for (int y = 0; y < image.getHeight(); ++y)
    {
        for (int x = 0; x < image.getWidth(); ++x)
        {
            const auto* pixel = reinterpret_cast<const juce::PixelARGB*> (pixels.getPixelPointer (x, y));
            const auto red = static_cast<unsigned> (pixel->getRed());
            const auto green = static_cast<unsigned> (pixel->getGreen());
            const auto blue = static_cast<unsigned> (pixel->getBlue());
            if (red + green + blue > 24)
            {
                minX = juce::jmin (minX, x);
                minY = juce::jmin (minY, y);
                maxX = juce::jmax (maxX, x);
                maxY = juce::jmax (maxY, y);
            }
        }
    }

    if (maxX >= minX && maxY >= minY)
        sourceBounds = { minX, minY, maxX - minX + 1, maxY - minY + 1 };
    else
        sourceBounds = image.getBounds();
}

void EmbeddedEmulatorDisplayComponent::recordMouseEvent (const juce::MouseEvent& event,
                                                      const juce::String& eventName,
                                                      int buttonOverride)
{
    lastMouseEvent = eventName;
    lastJuceMouse = event.position;
    if (buttonOverride != 0)
        lastMouseButton = buttonOverride;

    const auto target = getImageDestination();
    lastMouseInsideImage = image.isValid() && target.getWidth() > 0.0f && target.contains (event.position);
    if (lastMouseInsideImage)
    {
        lastMouseNormalized.x = juce::jlimit (0.0f, 1.0f,
                                              (event.position.x - target.getX()) / target.getWidth());
        lastMouseNormalized.y = juce::jlimit (0.0f, 1.0f,
                                              (event.position.y - target.getY()) / target.getHeight());
        const auto source = getSourceBounds();
        lastMameMouse.x = juce::jlimit (source.getX(), source.getRight() - 1,
                                        source.getX() + static_cast<int> (std::floor (lastMouseNormalized.x * source.getWidth())));
        lastMameMouse.y = juce::jlimit (source.getY(), source.getBottom() - 1,
                                        source.getY() + static_cast<int> (std::floor (lastMouseNormalized.y * source.getHeight())));
    }
    else
    {
        lastMameMouse = { -1, -1 };
        lastMouseNormalized = {};
    }
    repaint();
}

void EmbeddedEmulatorDisplayComponent::mouseMove (const juce::MouseEvent& event)
{
    recordMouseEvent (event, "move");
    if (lastMouseInsideImage)
        processor.enqueueMouseEvent (ves::EmbeddedMouseEventType::Move, lastMameMouse.x, lastMameMouse.y);
}

void EmbeddedEmulatorDisplayComponent::mouseEnter (const juce::MouseEvent& event)
{
    recordMouseEvent (event, "enter");
    if (lastMouseInsideImage)
        processor.enqueueMouseEvent (ves::EmbeddedMouseEventType::Move, lastMameMouse.x, lastMameMouse.y);
}

void EmbeddedEmulatorDisplayComponent::mouseExit (const juce::MouseEvent& event)
{
    recordMouseEvent (event, "exit");
    lastMouseInsideImage = false;
    lastMameMouse = { -1, -1 };
    repaint();
}

void EmbeddedEmulatorDisplayComponent::mouseDown (const juce::MouseEvent& event)
{
    const auto button = event.mods.isLeftButtonDown() ? 1 : (event.mods.isRightButtonDown() ? 2 : 0);
    mouseIsDown = button == 1;
    recordMouseEvent (event, "down", button);
    if (mouseIsDown && lastMouseInsideImage)
        processor.enqueueMouseEvent (ves::EmbeddedMouseEventType::LeftDown, lastMameMouse.x, lastMameMouse.y);
}

void EmbeddedEmulatorDisplayComponent::mouseDrag (const juce::MouseEvent& event)
{
    recordMouseEvent (event, "drag");
    if (mouseIsDown && lastMouseInsideImage)
        processor.enqueueMouseEvent (ves::EmbeddedMouseEventType::Move, lastMameMouse.x, lastMameMouse.y);
}

void EmbeddedEmulatorDisplayComponent::mouseUp (const juce::MouseEvent& event)
{
    const auto wasDown = mouseIsDown;
    const auto fallback = lastMameMouse;
    mouseIsDown = false;
    recordMouseEvent (event, "up");
    if (wasDown)
    {
        const auto point = lastMouseInsideImage ? lastMameMouse : fallback;
        if (point.x >= 0 && point.y >= 0)
            processor.enqueueMouseEvent (ves::EmbeddedMouseEventType::LeftUp, point.x, point.y);
        else
            processor.requestMouseRelease();
    }
}

VintageEmulatorStudioEditor::VintageEmulatorStudioEditor (VintageEmulatorStudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p), mameDisplay (p)
{
    lookAndFeel.setInterTypefaces (
        BinaryData::InterRegular_ttfSize > 0
            ? juce::Typeface::createSystemTypefaceFor (BinaryData::InterRegular_ttf,
                                                        static_cast<size_t> (BinaryData::InterRegular_ttfSize))
            : nullptr,
        BinaryData::InterSemiBold_ttfSize > 0
            ? juce::Typeface::createSystemTypefaceFor (BinaryData::InterSemiBold_ttf,
                                                        static_cast<size_t> (BinaryData::InterSemiBold_ttfSize))
            : nullptr);
    setLookAndFeel (&lookAndFeel);

    // These components must exist before setSize(), which synchronously calls resized().
    romButton = std::make_unique<VESMediaIconButton> ("ROM Folder", BinaryData::rom_svg, BinaryData::rom_svgSize, "ROM");
    artworkButton = std::make_unique<VESMediaIconButton> ("Artwork Folder", BinaryData::artwork_svg, BinaryData::artwork_svgSize, "ART");
    floppyButton = std::make_unique<VESMediaIconButton> ("Floppy Disk", BinaryData::floppy_svg, BinaryData::floppy_svgSize, "FLP");
    cdRomButton = std::make_unique<VESMediaIconButton> ("CD-ROM", BinaryData::cdrom_svg, BinaryData::cdrom_svgSize, "CD");
    hardDiskButton = std::make_unique<VESMediaIconButton> ("Hard Disk", BinaryData::harddisk_svg, BinaryData::harddisk_svgSize, "HD");
    fitToScreenButton = std::make_unique<VESMediaIconButton> ("Fit Window", nullptr, 0, "Fit Window");

    addAndMakeVisible (*romButton);
    addAndMakeVisible (*artworkButton);
    addAndMakeVisible (*floppyButton);
    addAndMakeVisible (*cdRomButton);
    addAndMakeVisible (*hardDiskButton);
    addAndMakeVisible (*fitToScreenButton);

    const auto configureIconLabel = [this] (juce::Label& label, const juce::String& text)
    {
        addAndMakeVisible (label);
        label.setText (text, juce::dontSendNotification);
        label.setFont (lookAndFeel.semiBoldFont (12.5f));
        label.setJustificationType (juce::Justification::centred);
        label.setColour (juce::Label::textColourId, juce::Colour::fromRGB (216, 228, 235));
        label.setInterceptsMouseClicks (false, false);
    };
    configureIconLabel (romIconLabel, "ROMs");
    configureIconLabel (artworkIconLabel, "Artworks");
    configureIconLabel (floppyIconLabel, "Floppy");
    configureIconLabel (cdRomIconLabel, "CD-ROM");
    configureIconLabel (hardDiskIconLabel, "Hard-Disk");

    juce::Component::SafePointer<VintageEmulatorStudioEditor> safeThis (this);
    romButton->onClick = [safeThis]
    {
        if (safeThis != nullptr && safeThis->romButton != nullptr)
            safeThis->showToolbarPopup (ToolbarPopupType::rom, *safeThis->romButton);
    };
    artworkButton->onClick = [safeThis]
    {
        if (safeThis != nullptr && safeThis->artworkButton != nullptr)
            safeThis->showToolbarPopup (ToolbarPopupType::artwork, *safeThis->artworkButton);
    };
    floppyButton->onClick = [safeThis]
    {
        if (safeThis != nullptr && safeThis->floppyButton != nullptr)
            safeThis->showToolbarPopup (ToolbarPopupType::floppy, *safeThis->floppyButton);
    };
    cdRomButton->onClick = [safeThis]
    {
        if (safeThis != nullptr && safeThis->cdRomButton != nullptr)
            safeThis->showToolbarPopup (ToolbarPopupType::cdRom, *safeThis->cdRomButton);
    };
    hardDiskButton->onClick = [safeThis]
    {
        if (safeThis != nullptr && safeThis->hardDiskButton != nullptr)
            safeThis->showToolbarPopup (ToolbarPopupType::hardDisk, *safeThis->hardDiskButton);
    };
    fitToScreenButton->onClick = [safeThis]
    {
        if (safeThis != nullptr)
            safeThis->setEditorSizeFittedToCurrentDisplay (safeThis->getWidth(), safeThis->getHeight(), false);
    };

    setResizable (true, true);
    setResizeLimits (minimumEditorWidth, minimumEditorHeight, maximumEditorWidth, maximumEditorHeight);
    setEditorSizeFittedToCurrentDisplay (processor.getSavedEditorWidth(), processor.getSavedEditorHeight(), true);
    editorSizePending = false;
    lastAppliedRestorationRevision = processor.getStateRestorationRevision();

    logoImage = juce::ImageCache::getFromMemory (BinaryData::veslogo_png,
                                                  BinaryData::veslogo_pngSize);

    addLabel (synthLabel, "Emulator");
    synthLabel.setFont (lookAndFeel.regularFont (fieldLabelFontSize));
    synthLabel.setColour (juce::Label::textColourId, juce::Colour::fromRGB (238, 240, 243));
    addLabel (statusLabel, "");
    statusLabel.setFont (lookAndFeel.semiBoldFont (17.0f));
    statusLabel.setJustificationType (juce::Justification::centredRight);
    addLabel (romStatusLabel, "");
    romStatusLabel.setFont (lookAndFeel.semiBoldFont (17.0f));
    romStatusLabel.setJustificationType (juce::Justification::centredRight);

    addAndMakeVisible (synthBox);
    synthBox.addListener (this);
    synthBox.setColour (juce::ComboBox::backgroundColourId, juce::Colour::fromRGB (20, 21, 25));
    synthBox.setColour (juce::ComboBox::outlineColourId, juce::Colour::fromRGB (75, 80, 88));
    synthBox.setColour (juce::ComboBox::arrowColourId, juce::Colour::fromRGB (210, 220, 225));
    synthBox.setColour (juce::ComboBox::textColourId, juce::Colours::transparentBlack);

    addAndMakeVisible (synthBoxPresentationLabel);
    synthBoxPresentationLabel.setFont (lookAndFeel.regularFont (18.0f));
    synthBoxPresentationLabel.setColour (juce::Label::textColourId, juce::Colour::fromRGB (238, 240, 243));
    synthBoxPresentationLabel.setJustificationType (juce::Justification::centredLeft);
    synthBoxPresentationLabel.setInterceptsMouseClicks (false, false);

    lookAndFeel.setColour (juce::PopupMenu::backgroundColourId, juce::Colour::fromRGB (20, 21, 25));
    lookAndFeel.setColour (juce::PopupMenu::textColourId, juce::Colour::fromRGB (238, 240, 243));
    lookAndFeel.setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour::fromRGB (33, 104, 123));
    lookAndFeel.setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);

    addAndMakeVisible (mameDisplay);
    processor.setVideoDisplayActive (true);

    rebuildSynthList();

    updateControlState();
    updateStatus();
    startTimer (1, 200);
}

VintageEmulatorStudioEditor::~VintageEmulatorStudioEditor()
{
    stopTimer (1);
    if (activeToolbarPopup != nullptr)
        activeToolbarPopup->dismiss();
    activeToolbarPopup = nullptr;
    activeToolbarPopupContent = nullptr;
    saveSettledEditorSize();
    processor.requestMouseRelease();
    processor.setVideoDisplayActive (false);
    setLookAndFeel (nullptr);
}

juce::Rectangle<int> VintageEmulatorStudioEditor::getCurrentDisplaySafeUserArea() const
{
    const auto& displays = juce::Desktop::getInstance().getDisplays();
    auto* display = displays.getDisplayForRect (getScreenBounds());
    if (display == nullptr)
        display = displays.getPrimaryDisplay();

    return display != nullptr ? display->userBounds.toNearestInt().reduced (fitToScreenSafetyMargin)
                              : juce::Rectangle<int>();
}

void VintageEmulatorStudioEditor::updateFitToScreenButtonVisibility()
{
    if (fitToScreenButton == nullptr)
        return;

    const auto safeUserArea = getCurrentDisplaySafeUserArea();
    const auto comfortableWidth = static_cast<int> (std::floor (safeUserArea.getWidth() * fitToScreenUserAreaFraction));
    const auto comfortableHeight = static_cast<int> (std::floor (safeUserArea.getHeight() * fitToScreenUserAreaFraction));
    fitToScreenButton->setVisible (! safeUserArea.isEmpty()
                                   && (getWidth() > comfortableWidth || getHeight() > comfortableHeight));
}

void VintageEmulatorStudioEditor::setEditorSizeFittedToCurrentDisplay (int requestedWidth,
                                                                        int requestedHeight,
                                                                        bool preserveSavedSize)
{
    auto targetWidth = juce::jlimit (minimumEditorWidth, maximumEditorWidth, requestedWidth);
    auto targetHeight = juce::jlimit (minimumEditorHeight, maximumEditorHeight, requestedHeight);

    const auto safeUserArea = getCurrentDisplaySafeUserArea();
    if (safeUserArea.isEmpty())
        return;

    const auto comfortableWidth = juce::jmax (minimumEditorWidth,
                                               static_cast<int> (std::floor (safeUserArea.getWidth() * fitToScreenUserAreaFraction)));
    const auto comfortableHeight = juce::jmax (minimumEditorHeight,
                                                static_cast<int> (std::floor (safeUserArea.getHeight() * fitToScreenUserAreaFraction)));
    if (targetWidth > comfortableWidth || targetHeight > comfortableHeight)
    {
        const auto scale = std::min (static_cast<double> (comfortableWidth) / targetWidth,
                                     static_cast<double> (comfortableHeight) / targetHeight);
        targetWidth = juce::jlimit (minimumEditorWidth, maximumEditorWidth,
                                    static_cast<int> (std::floor (targetWidth * scale)));
        targetHeight = juce::jlimit (minimumEditorHeight, maximumEditorHeight,
                                     static_cast<int> (std::floor (targetHeight * scale)));
    }

    if (targetWidth == getWidth() && targetHeight == getHeight())
    {
        updateFitToScreenButtonVisibility();
        return;
    }

    const juce::ScopedValueSetter<bool> restoringSize (applyingRestoredSize,
                                                        preserveSavedSize || applyingRestoredSize);
    setSize (targetWidth, targetHeight);
    updateFitToScreenButtonVisibility();
}

void VintageEmulatorStudioEditor::paint (juce::Graphics& g)
{
    auto header = getLocalBounds().removeFromTop (headerHeight);

    g.fillAll (juce::Colour::fromRGB (246, 246, 246));
    g.setColour (juce::Colour::fromRGB (15, 16, 19));
    g.fillRect (header);

    if (secondaryHeaderBottom > headerHeight)
    {
        g.setColour (juce::Colour::fromRGB (232, 232, 232));
        g.fillRect (0, headerHeight, getWidth(), secondaryHeaderBottom - headerHeight);
    }

    if (logoImage.isValid())
        g.drawImageAt (logoImage, 0, 0);

    g.setColour (statusOnSecondaryRow ? juce::Colour::fromRGB (45, 45, 45)
                                      : juce::Colour::fromRGB (150, 150, 150));
    g.setFont (lookAndFeel.semiBoldFont (17.0f));
    g.drawFittedText ("|", statusSeparatorBounds,
                      juce::Justification::centred, 1);

    g.setColour (juce::Colour::fromRGB (214, 214, 214));
    g.drawHorizontalLine (header.getBottom(), 0.0f, static_cast<float> (getWidth()));
}

void VintageEmulatorStudioEditor::resized()
{
    if (! applyingRestoredSize)
    {
        pendingEditorWidth = getWidth();
        pendingEditorHeight = getHeight();
        editorResizeDeadlineMs = juce::Time::getMillisecondCounter() + 400;
        resizeRevisionAtRequest = processor.getStateRestorationRevision();
        editorSizePending = true;
    }

    constexpr int iconButtonSize = 44;
    constexpr int iconItemWidth = 60;
    constexpr int iconItemGap = 8;
    constexpr int iconControlCount = 5;
    constexpr int iconLabelHeight = 20;
    constexpr int headerMediaGap = 28;
    constexpr int mediaToSelectorGap = 16;
    constexpr int headerControlToStatusGap = 18;
    constexpr int selectorBoxWidth = 240;
    const auto iconGroupWidth = (iconControlCount * iconItemWidth) + ((iconControlCount - 1) * iconItemGap);
    const auto mainRowIconX = logoWidth + headerMediaGap;
    const auto mainRowIconGroupRight = mainRowIconX + iconGroupWidth;
    const auto mainRowSelectorX = mainRowIconGroupRight + mediaToSelectorGap;
    constexpr int statusGroupWidth = statusLabelWidth + statusSeparatorWidth + statusRomWidth + (2 * statusGroupGap);
    const auto primaryStatusGroupX = getWidth() - statusGroupRightMargin - statusGroupWidth;
    const auto statusFitsInPrimaryHeader = logoWidth + headerControlToStatusGap <= primaryStatusGroupX;
    const auto iconsFitInHeader = mainRowIconGroupRight + headerControlToStatusGap <= primaryStatusGroupX;
    const auto selectorFitsInHeader = mainRowSelectorX + selectorBoxWidth + headerControlToStatusGap <= primaryStatusGroupX;
    const auto iconX = iconsFitInHeader ? mainRowIconX : margin;
    const auto iconLabelY = iconsFitInHeader ? 4 : headerHeight + 4;
    const auto iconY = iconsFitInHeader ? 28 : headerHeight + iconLabelHeight + 8;
    const auto iconButtonInset = (iconItemWidth - iconButtonSize) / 2;
    const auto iconItemX = [iconX, iconItemWidth, iconItemGap] (int index) { return iconX + index * (iconItemWidth + iconItemGap); };

    // Keep these checks even though the constructor creates the buttons before setSize().
    // JUCE can call resized() during partial construction or teardown.
    if (romButton != nullptr)
        romButton->setBounds (iconItemX (0) + iconButtonInset, iconY, iconButtonSize, iconButtonSize);
    if (artworkButton != nullptr)
        artworkButton->setBounds (iconItemX (1) + iconButtonInset, iconY, iconButtonSize, iconButtonSize);
    if (floppyButton != nullptr)
        floppyButton->setBounds (iconItemX (2) + iconButtonInset, iconY, iconButtonSize, iconButtonSize);
    if (cdRomButton != nullptr)
        cdRomButton->setBounds (iconItemX (3) + iconButtonInset, iconY, iconButtonSize, iconButtonSize);
    if (hardDiskButton != nullptr)
        hardDiskButton->setBounds (iconItemX (4) + iconButtonInset, iconY, iconButtonSize, iconButtonSize);
    if (fitToScreenButton != nullptr)
        fitToScreenButton->setBounds (94, 3, 54, 18);

    const auto mediaLabelColour = iconsFitInHeader ? juce::Colour::fromRGB (216, 228, 235)
                                                    : juce::Colours::black;
    const std::array<juce::Label*, 5> mediaLabels {
        &romIconLabel, &artworkIconLabel, &floppyIconLabel, &cdRomIconLabel, &hardDiskIconLabel
    };
    for (size_t index = 0; index < mediaLabels.size(); ++index)
    {
        auto* label = mediaLabels[index];
        label->setColour (juce::Label::textColourId, mediaLabelColour);
        label->setBounds (iconItemX (static_cast<int> (index)), iconLabelY, iconItemWidth, iconLabelHeight);
    }

    if (selectorFitsInHeader)
    {
        synthLabel.setColour (juce::Label::textColourId, juce::Colour::fromRGB (238, 240, 243));
        synthLabel.setBounds (mainRowSelectorX, 6, selectorBoxWidth, 20);
        synthBox.setBounds (mainRowSelectorX, (headerHeight - rowHeight) / 2, selectorBoxWidth, rowHeight);
        synthBoxPresentationLabel.setBounds (synthBox.getBounds().withTrimmedLeft (6).withTrimmedRight (28));
    }

    auto viewportY = headerHeight;
    auto secondaryRowY = headerHeight + 6;
    if (! selectorFitsInHeader)
    {
        constexpr int selectorLabelWidth = 80;
        constexpr int selectorGap = 8;
        secondaryRowY = iconsFitInHeader ? headerHeight + 6 : iconY + iconButtonSize + 8;
        synthLabel.setColour (juce::Label::textColourId, juce::Colours::black);
        synthLabel.setBounds (margin, secondaryRowY, selectorLabelWidth, rowHeight);
        synthBox.setBounds (margin + selectorLabelWidth + selectorGap, secondaryRowY, selectorBoxWidth, rowHeight);
        synthBoxPresentationLabel.setBounds (synthBox.getBounds().withTrimmedLeft (6).withTrimmedRight (28));
        viewportY = secondaryRowY + rowHeight + 8;
    }

    secondaryHeaderBottom = viewportY;

    statusOnSecondaryRow = ! statusFitsInPrimaryHeader;
    const auto statusGroupX = statusOnSecondaryRow
                            ? margin + 80 + 8 + selectorBoxWidth + headerControlToStatusGap
                            : primaryStatusGroupX;
    const auto statusGroupY = statusOnSecondaryRow ? secondaryRowY : (headerHeight - 28) / 2;
    statusLabel.setBounds (statusGroupX, statusGroupY, statusLabelWidth, 28);
    statusSeparatorBounds = { statusGroupX + statusLabelWidth + statusGroupGap, statusGroupY,
                              statusSeparatorWidth, 28 };
    romStatusLabel.setBounds (statusGroupX + statusLabelWidth + statusSeparatorWidth + (2 * statusGroupGap),
                              statusGroupY, statusRomWidth, 28);

    updateFitToScreenButtonVisibility();

    mameDisplay.setBounds (12, viewportY, getWidth() - 24, getHeight() - viewportY - 12);
}

void VintageEmulatorStudioEditor::comboBoxChanged (juce::ComboBox* comboBoxThatHasChanged)
{
    if (comboBoxThatHasChanged == &synthBox)
    {
        const auto displayPosition = synthBox.getSelectedId() - 1;
        const auto driverName = processor.getMachineDriverNameForDisplayPosition (displayPosition);
        processor.selectMachineByDriverName (driverName);
        updateSynthBoxPresentation();
    }
    updateControlState();
    updateStatus();
}

void VintageEmulatorStudioEditor::browseRoms()
{
    auto startDirectory = processor.getExternalRomsDirectory();
    if (! startDirectory.isDirectory())
        startDirectory = startDirectory.getParentDirectory();

    romsChooser = std::make_unique<juce::FileChooser> ("Select MAME ROM folder", startDirectory, juce::String(), true);
    const auto chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories;
    juce::Component::SafePointer<VintageEmulatorStudioEditor> safeThis (this);
    romsChooser->launchAsync (chooserFlags, [safeThis] (const juce::FileChooser& chooser)
    {
        if (safeThis == nullptr)
            return;

        const auto folder = chooser.getResult();
        if (folder.isDirectory())
        {
            safeThis->processor.setExternalRomsDirectory (folder);
            safeThis->rebuildSynthList();
            safeThis->updateControlState();
            safeThis->updateStatus();
            safeThis->refreshActiveToolbarPopup();
        }

        safeThis->romsChooser.reset();
    });
}

void VintageEmulatorStudioEditor::browseArtworks()
{
    auto startDirectory = processor.getExternalArtworkDirectory();
    if (! startDirectory.isDirectory())
        startDirectory = juce::File::getSpecialLocation (juce::File::userHomeDirectory);

    artworksChooser = std::make_unique<juce::FileChooser> ("Select MAME artwork folder", startDirectory, juce::String(), true);
    const auto chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories;
    juce::Component::SafePointer<VintageEmulatorStudioEditor> safeThis (this);
    artworksChooser->launchAsync (chooserFlags, [safeThis] (const juce::FileChooser& chooser)
    {
        if (safeThis == nullptr)
            return;

        const auto folder = chooser.getResult();
        if (folder.isDirectory())
        {
            safeThis->processor.setExternalArtworkDirectory (folder);
            safeThis->updateControlState();
            safeThis->updateStatus();
            safeThis->refreshActiveToolbarPopup();
        }

        safeThis->artworksChooser.reset();
    });
}

void VintageEmulatorStudioEditor::browseFloppy()
{
    const auto startFile = juce::File (processor.getSelectedMediaPath());
    const auto startDirectory = startFile.existsAsFile() ? startFile.getParentDirectory()
                                                         : juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    mediaChooser = std::make_unique<juce::FileChooser> ("Select floppy image", startDirectory,
        "*.hfe;*.img;*.ima;*.dsk;*.mfi;*.dfi;*.mfm;*.td0;*.imd;*.ufi;*.360;*.ipf;*", true);
    const auto chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
    juce::Component::SafePointer<VintageEmulatorStudioEditor> safeThis (this);
    mediaChooser->launchAsync (chooserFlags, [safeThis] (const juce::FileChooser& chooser)
    {
        if (safeThis == nullptr)
            return;

        const auto file = chooser.getResult();
        if (file.existsAsFile())
        {
            const auto useHotSwap = safeThis->processor.selectedMachineSupportsFloppyHotSwap();
            if (! useHotSwap || safeThis->processor.getEngineState() != EmbeddedEngineState::Ready)
            {
                if (useHotSwap && (safeThis->processor.getEngineState() == EmbeddedEngineState::Booting
                                   || safeThis->processor.getEngineState() == EmbeddedEngineState::Starting))
                    safeThis->processor.requestSelectedFloppyHotSwap (file);
                else
                    safeThis->processor.setSelectedMediaFile (file);
            }
            else
                safeThis->processor.requestSelectedFloppyHotSwap (file);
        }
        safeThis->updateControlState();
        safeThis->updateStatus();
        safeThis->refreshActiveToolbarPopup();
        safeThis->mediaChooser.reset();
    });
}

void VintageEmulatorStudioEditor::clearFloppy()
{
    const auto useHotSwap = processor.selectedMachineSupportsFloppyHotSwap();
    if (! useHotSwap || processor.getEngineState() != EmbeddedEngineState::Ready)
    {
        if (useHotSwap && (processor.getEngineState() == EmbeddedEngineState::Booting
                           || processor.getEngineState() == EmbeddedEngineState::Starting))
            processor.requestSelectedFloppyEject();
        else
            processor.ejectSelectedMedia();
    }
    else
        processor.requestSelectedFloppyEject();
    updateControlState();
    updateStatus();
    refreshActiveToolbarPopup();
}

void VintageEmulatorStudioEditor::browseCdRom()
{
    const auto startFile = juce::File (processor.getSelectedCdRomPath());
    const auto startDirectory = startFile.existsAsFile() ? startFile.getParentDirectory()
                                                         : juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    cdRomChooser = std::make_unique<juce::FileChooser> ("Select CD-ROM image", startDirectory,
        "*.iso;*.chd;*.cue;*.toc;*.nrg;*.gdi;*.cdr;*", true);
    const auto chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
    juce::Component::SafePointer<VintageEmulatorStudioEditor> safeThis (this);
    cdRomChooser->launchAsync (chooserFlags, [safeThis] (const juce::FileChooser& chooser)
    {
        if (safeThis == nullptr)
            return;

        const auto file = chooser.getResult();
        if (file.existsAsFile())
        {
            const auto useHotSwap = safeThis->processor.selectedMachineSupportsCdRomHotSwap();
            if (! useHotSwap || safeThis->processor.getEngineState() != EmbeddedEngineState::Ready)
            {
                if (useHotSwap && (safeThis->processor.getEngineState() == EmbeddedEngineState::Booting
                                   || safeThis->processor.getEngineState() == EmbeddedEngineState::Starting))
                    safeThis->processor.requestSelectedCdRomHotSwap (file);
                else
                    safeThis->processor.setSelectedCdRomFile (file);
            }
            else
                safeThis->processor.requestSelectedCdRomHotSwap (file);
        }
        safeThis->updateControlState();
        safeThis->updateStatus();
        safeThis->refreshActiveToolbarPopup();
        safeThis->cdRomChooser.reset();
    });
}

void VintageEmulatorStudioEditor::clearCdRom()
{
    const auto useHotSwap = processor.selectedMachineSupportsCdRomHotSwap();
    if (! useHotSwap || processor.getEngineState() != EmbeddedEngineState::Ready)
    {
        if (useHotSwap && (processor.getEngineState() == EmbeddedEngineState::Booting
                           || processor.getEngineState() == EmbeddedEngineState::Starting))
            processor.requestSelectedCdRomEject();
        else
            processor.clearSelectedCdRom();
    }
    else
        processor.requestSelectedCdRomEject();
    updateControlState();
    updateStatus();
    refreshActiveToolbarPopup();
}

void VintageEmulatorStudioEditor::browseHardDisk()
{
    const auto startFile = juce::File (processor.getSelectedHardDiskPath());
    const auto startDirectory = startFile.existsAsFile() ? startFile.getParentDirectory()
                                                         : juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    hardDiskChooser = std::make_unique<juce::FileChooser> ("Select hard-disk image", startDirectory,
        "*.chd;*.hd;*.hdv;*.2mg;*.hdi;*.hds;*", true);
    const auto chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
    juce::Component::SafePointer<VintageEmulatorStudioEditor> safeThis (this);
    hardDiskChooser->launchAsync (chooserFlags, [safeThis] (const juce::FileChooser& chooser)
    {
        if (safeThis == nullptr)
            return;

        const auto file = chooser.getResult();
        if (file.existsAsFile())
            safeThis->processor.setSelectedHardDiskFile (file);
        safeThis->updateControlState();
        safeThis->updateStatus();
        safeThis->refreshActiveToolbarPopup();
        safeThis->hardDiskChooser.reset();
    });
}

void VintageEmulatorStudioEditor::clearHardDisk()
{
    processor.clearSelectedHardDisk();
    updateControlState();
    updateStatus();
    refreshActiveToolbarPopup();
}

void VintageEmulatorStudioEditor::showToolbarPopup (ToolbarPopupType type, juce::Component& anchor)
{
    const auto request = ++toolbarPopupRequest;

    if (activeToolbarPopup != nullptr)
    {
        activeToolbarPopup->dismiss();
        juce::Component::SafePointer<VintageEmulatorStudioEditor> safeThis (this);
        juce::Component::SafePointer<juce::Component> safeAnchor (&anchor);
        juce::MessageManager::callAsync ([safeThis, safeAnchor, type, request]
        {
            if (safeThis != nullptr && safeAnchor != nullptr && safeThis->toolbarPopupRequest == request)
                safeThis->launchToolbarPopup (type, *safeAnchor);
        });
        return;
    }

    launchToolbarPopup (type, anchor);
}

void VintageEmulatorStudioEditor::launchToolbarPopup (ToolbarPopupType type, juce::Component& anchor)
{
    if (activeToolbarPopup != nullptr)
        return;

    if ((type == ToolbarPopupType::floppy && ! processor.selectedMachineSupportsFloppy())
        || (type == ToolbarPopupType::cdRom && ! processor.selectedMachineSupportsCdRom())
        || (type == ToolbarPopupType::hardDisk && ! processor.selectedMachineSupportsHardDisk()))
        return;

    juce::Component::SafePointer<VintageEmulatorStudioEditor> safeThis (this);
    auto noStatus = VESToolbarPopup::StringProvider {};
    std::unique_ptr<VESToolbarPopup> popup;
    const auto popupRegularFont = lookAndFeel.regularFont (18.0f);
    const auto popupSemiBoldFont = lookAndFeel.semiBoldFont (22.0f);

    switch (type)
    {
        case ToolbarPopupType::rom:
            popup = std::make_unique<VESToolbarPopup> (
                "ROM Folder",
                lookAndFeel,
                popupRegularFont,
                popupSemiBoldFont,
                [safeThis] { return safeThis != nullptr ? safeThis->processor.getExternalRomsDirectory().getFullPathName() : juce::String(); },
                "No ROM folder selected",
                [safeThis]
                {
                    if (safeThis == nullptr)
                        return juce::String();
                    return safeThis->processor.hasSelectedMachineRom() ? juce::String ("Status: ROM Found")
                                                                      : juce::String ("Status: ROM Not Found");
                },
                juce::String {},
                [safeThis]
                {
                    if (safeThis != nullptr)
                        safeThis->browseRoms();
                },
                VESToolbarPopup::Action {}, juce::String {});
            break;

        case ToolbarPopupType::artwork:
            popup = std::make_unique<VESToolbarPopup> (
                "Artwork Folder",
                lookAndFeel,
                popupRegularFont,
                popupSemiBoldFont,
                [safeThis] { return safeThis != nullptr ? safeThis->processor.getExternalArtworkDirectoryPath() : juce::String(); },
                "Bundled / Default Artwork",
                noStatus,
                juce::String {},
                [safeThis]
                {
                    if (safeThis != nullptr)
                        safeThis->browseArtworks();
                },
                [safeThis]
                {
                    if (safeThis == nullptr)
                        return;
                    safeThis->processor.clearExternalArtworkDirectory();
                    safeThis->updateControlState();
                    safeThis->updateStatus();
                },
                "Use Default");
            break;

        case ToolbarPopupType::floppy:
        {
            const auto floppyHotSwap = processor.selectedMachineSupportsFloppyHotSwap();
            popup = std::make_unique<VESToolbarPopup> (
                "Floppy Disk",
                lookAndFeel,
                popupRegularFont,
                popupSemiBoldFont,
                [safeThis] { return safeThis != nullptr ? safeThis->processor.getSelectedMediaPath() : juce::String(); },
                "No disk selected",
                floppyHotSwap ? VESToolbarPopup::StringProvider ([safeThis]
                {
                    return safeThis != nullptr ? safeThis->processor.getFloppyHotSwapMessage() : juce::String();
                }) : noStatus,
                floppyHotSwap ? juce::String() : "Changing media restarts the emulation.",
                [safeThis]
                {
                    if (safeThis != nullptr)
                        safeThis->browseFloppy();
                },
                [safeThis]
                {
                    if (safeThis != nullptr)
                        safeThis->clearFloppy();
                },
                "Eject",
                [safeThis]
                {
                    return safeThis != nullptr
                        ? safeThis->processor.getRecentMediaPaths (VintageEmulatorStudioProcessor::RecentMediaType::Floppy)
                        : std::vector<juce::String> {};
                },
                [safeThis] (const juce::String& path)
                {
                    if (safeThis == nullptr)
                        return juce::String();

                    const juce::File file (path);
                    if (! file.existsAsFile())
                    {
                        safeThis->processor.removeRecentMediaPath (VintageEmulatorStudioProcessor::RecentMediaType::Floppy, path);
                        return juce::String ("Selected media file is no longer available.");
                    }

                    const auto useHotSwap = safeThis->processor.selectedMachineSupportsFloppyHotSwap();
                    if (! useHotSwap || safeThis->processor.getEngineState() != EmbeddedEngineState::Ready)
                    {
                        if (useHotSwap && (safeThis->processor.getEngineState() == EmbeddedEngineState::Booting
                                           || safeThis->processor.getEngineState() == EmbeddedEngineState::Starting))
                            safeThis->processor.requestSelectedFloppyHotSwap (file);
                        else
                            safeThis->processor.setSelectedMediaFile (file);
                    }
                    else
                        safeThis->processor.requestSelectedFloppyHotSwap (file);
                    safeThis->updateControlState();
                    safeThis->updateStatus();
                    return juce::String();
                },
                [safeThis]
                {
                    return safeThis != nullptr && safeThis->processor.isFloppyHotSwapPending();
                });
            break;
        }

        case ToolbarPopupType::cdRom:
        {
            const auto cdRomHotSwap = processor.selectedMachineSupportsCdRomHotSwap();
            popup = std::make_unique<VESToolbarPopup> (
                "CD-ROM",
                lookAndFeel,
                popupRegularFont,
                popupSemiBoldFont,
                [safeThis] { return safeThis != nullptr ? safeThis->processor.getSelectedCdRomPath() : juce::String(); },
                "No disc selected",
                cdRomHotSwap ? VESToolbarPopup::StringProvider ([safeThis]
                {
                    return safeThis != nullptr ? safeThis->processor.getFloppyHotSwapMessage() : juce::String();
                }) : noStatus,
                cdRomHotSwap ? juce::String() : "Changing media restarts the emulation.",
                [safeThis]
                {
                    if (safeThis != nullptr)
                        safeThis->browseCdRom();
                },
                [safeThis]
                {
                    if (safeThis != nullptr)
                        safeThis->clearCdRom();
                },
                cdRomHotSwap ? "Eject" : "Clear",
                [safeThis]
                {
                    return safeThis != nullptr
                        ? safeThis->processor.getRecentMediaPaths (VintageEmulatorStudioProcessor::RecentMediaType::CdRom)
                        : std::vector<juce::String> {};
                },
                [safeThis] (const juce::String& path)
                {
                    if (safeThis == nullptr)
                        return juce::String();

                    const juce::File file (path);
                    if (! file.existsAsFile())
                    {
                        safeThis->processor.removeRecentMediaPath (VintageEmulatorStudioProcessor::RecentMediaType::CdRom, path);
                        return juce::String ("Selected media file is no longer available.");
                    }

                    const auto useHotSwap = safeThis->processor.selectedMachineSupportsCdRomHotSwap();
                    if (! useHotSwap || safeThis->processor.getEngineState() != EmbeddedEngineState::Ready)
                    {
                        if (useHotSwap && (safeThis->processor.getEngineState() == EmbeddedEngineState::Booting
                                           || safeThis->processor.getEngineState() == EmbeddedEngineState::Starting))
                            safeThis->processor.requestSelectedCdRomHotSwap (file);
                        else
                            safeThis->processor.setSelectedCdRomFile (file);
                    }
                    else
                        safeThis->processor.requestSelectedCdRomHotSwap (file);
                    safeThis->refreshActiveToolbarPopup();
                    safeThis->updateControlState();
                    safeThis->updateStatus();
                    return juce::String();
                },
                [safeThis]
                {
                    return safeThis != nullptr && safeThis->processor.isCdRomHotSwapPending();
                },
                cdRomHotSwap);
            break;
        }

        case ToolbarPopupType::hardDisk:
            popup = std::make_unique<VESToolbarPopup> (
                "Hard Disk",
                lookAndFeel,
                popupRegularFont,
                popupSemiBoldFont,
                [safeThis] { return safeThis != nullptr ? safeThis->processor.getSelectedHardDiskPath() : juce::String(); },
                "No disk selected",
                noStatus,
                "Mounted hard-disk images may be modified.\nChanging media restarts the emulation.",
                [safeThis]
                {
                    if (safeThis != nullptr)
                        safeThis->browseHardDisk();
                },
                [safeThis]
                {
                    if (safeThis != nullptr)
                        safeThis->clearHardDisk();
                },
                "Unmount",
                [safeThis]
                {
                    return safeThis != nullptr
                        ? safeThis->processor.getRecentMediaPaths (VintageEmulatorStudioProcessor::RecentMediaType::HardDisk)
                        : std::vector<juce::String> {};
                },
                [safeThis] (const juce::String& path)
                {
                    if (safeThis == nullptr)
                        return juce::String();

                    const juce::File file (path);
                    if (! file.existsAsFile())
                    {
                        safeThis->processor.removeRecentMediaPath (VintageEmulatorStudioProcessor::RecentMediaType::HardDisk, path);
                        return juce::String ("Selected media file is no longer available.");
                    }

                    safeThis->processor.setSelectedHardDiskFile (file);
                    safeThis->updateControlState();
                    safeThis->updateStatus();
                    return juce::String();
                });
            break;

        case ToolbarPopupType::none:
            return;
    }

    if (popup == nullptr)
        return;

    auto* popupContent = popup.get();
    auto& callout = juce::CallOutBox::launchAsynchronously (std::move (popup), anchor.getBounds(), this);
    activeToolbarPopup = &callout;
    activeToolbarPopupContent = popupContent;
    activeToolbarPopupType = type;
}

void VintageEmulatorStudioEditor::dismissUnsupportedToolbarPopup()
{
    if (activeToolbarPopup == nullptr)
        return;

    const auto supported = activeToolbarPopupType == ToolbarPopupType::rom
                        || activeToolbarPopupType == ToolbarPopupType::artwork
                        || (activeToolbarPopupType == ToolbarPopupType::floppy && processor.selectedMachineSupportsFloppy())
                        || (activeToolbarPopupType == ToolbarPopupType::cdRom && processor.selectedMachineSupportsCdRom())
                        || (activeToolbarPopupType == ToolbarPopupType::hardDisk && processor.selectedMachineSupportsHardDisk());
    if (! supported)
        activeToolbarPopup->dismiss();
}

void VintageEmulatorStudioEditor::refreshActiveToolbarPopup()
{
    if (activeToolbarPopupContent != nullptr)
        activeToolbarPopupContent->refreshFromProcessor();
}

void VintageEmulatorStudioEditor::timerCallback (int timerId)
{
    if (timerId == 1)
    {
        const auto restorationRevision = processor.getStateRestorationRevision();
        const auto restorationPending = restorationRevision > lastAppliedRestorationRevision;
        const auto resizeSettled = ! editorSizePending
                                 || static_cast<int32_t> (juce::Time::getMillisecondCounter() - editorResizeDeadlineMs) >= 0;

        if (restorationPending && resizeSettled)
        {
            if (editorSizePending && resizeRevisionAtRequest >= restorationRevision)
            {
                saveSettledEditorSize();
            }
            else
            {
                editorSizePending = false;
                setEditorSizeFittedToCurrentDisplay (processor.getSavedEditorWidth(),
                                                      processor.getSavedEditorHeight(), true);
            }

            rebuildSynthList();
            lastAppliedRestorationRevision = restorationRevision;
        }
        else if (editorSizePending && resizeSettled)
        {
            saveSettledEditorSize();
        }
    }

    mameDisplay.updateFrame();
    updateFitToScreenButtonVisibility();
    const auto floppyHotSwapRevision = processor.getFloppyHotSwapRevision();
    if (floppyHotSwapRevision != lastFloppyHotSwapRevision)
    {
        lastFloppyHotSwapRevision = floppyHotSwapRevision;
        refreshActiveToolbarPopup();
    }
    updateStatus();
    updateControlState();
}

void VintageEmulatorStudioEditor::saveSettledEditorSize()
{
    if (! editorSizePending)
        return;

    processor.setSavedEditorSize (pendingEditorWidth, pendingEditorHeight);
    editorSizePending = false;
}

void VintageEmulatorStudioEditor::rebuildSynthList()
{
    struct ModelItem
    {
        juce::String name;
        int id = 0;
    };
    struct ManufacturerGroup
    {
        juce::String name;
        std::vector<ModelItem> models;
    };

    const auto profileCount = processor.getNumMachineProfiles();
    std::vector<ManufacturerGroup> manufacturerGroups;
    synthBox.clear (juce::dontSendNotification);
    for (int i = 0; i < profileCount; ++i)
    {
        const auto displayName = processor.getMachineNameForDisplayPosition (i);
        const auto firstSpace = displayName.indexOfChar (' ');
        const auto manufacturer = firstSpace > 0 ? displayName.substring (0, firstSpace) : displayName;
        const auto model = firstSpace > 0 ? displayName.substring (firstSpace + 1) : displayName;
        const auto driverName = processor.getMachineDriverNameForDisplayPosition (i);
        const auto romFound = processor.hasMachineRomByDriverName (driverName);
        auto group = std::find_if (manufacturerGroups.begin(), manufacturerGroups.end(), [&manufacturer] (const ManufacturerGroup& candidate)
        {
            return candidate.name.equalsIgnoreCase (manufacturer);
        });
        if (group == manufacturerGroups.end())
        {
            manufacturerGroups.push_back ({ manufacturer, {} });
            group = std::prev (manufacturerGroups.end());
        }
        group->models.push_back ({ model + juce::String (romFound ? "" : " (Missing ROM)"), i + 1 });
    }

    std::sort (manufacturerGroups.begin(), manufacturerGroups.end(), [] (const ManufacturerGroup& lhs, const ManufacturerGroup& rhs)
    {
        return lhs.name.compareNatural (rhs.name, false) < 0;
    });
    for (auto& manufacturer : manufacturerGroups)
    {
        std::sort (manufacturer.models.begin(), manufacturer.models.end(), [] (const ModelItem& lhs, const ModelItem& rhs)
        {
            return lhs.name.compareNatural (rhs.name, false) < 0;
        });

        juce::PopupMenu modelsMenu;
        for (const auto& model : manufacturer.models)
            modelsMenu.addItem (model.id, model.name);
        synthBox.getRootMenu()->addSubMenu (manufacturer.name, modelsMenu);
    }

    const auto selectedDriver = processor.getSelectedMachineDriverName();
    for (int i = 0; i < profileCount; ++i)
        if (processor.getMachineDriverNameForDisplayPosition (i) == selectedDriver)
            synthBox.setSelectedId (i + 1, juce::dontSendNotification);
    updateSynthBoxPresentation();
    synthBox.setEnabled (true);
}

void VintageEmulatorStudioEditor::updateSynthBoxPresentation()
{
    const auto displayPosition = synthBox.getSelectedId() - 1;
    if (displayPosition < 0 || displayPosition >= processor.getNumMachineProfiles())
        return;

    const auto formattedName = formatMachineNameForSelector (processor.getMachineNameForDisplayPosition (displayPosition));
    synthBoxPresentationLabel.setText (formattedName, juce::dontSendNotification);
    synthBox.setTooltip (formattedName);
}

void VintageEmulatorStudioEditor::updateControlState()
{
    synthBox.setColour (juce::ComboBox::textColourId, juce::Colours::transparentBlack);

    const auto popupIsOpen = [this] (ToolbarPopupType type)
    {
        return activeToolbarPopup != nullptr && activeToolbarPopup->isVisible() && activeToolbarPopupType == type;
    };
    const auto updateIcon = [&popupIsOpen] (std::unique_ptr<VESMediaIconButton>& button,
                                            ToolbarPopupType type,
                                            bool enabled,
                                            bool selected,
                                            const juce::String& enabledTooltip,
                                            const juce::String& disabledTooltip)
    {
        if (button == nullptr)
            return;

        button->setEnabled (enabled);
        button->setMouseCursor (enabled ? juce::MouseCursor::PointingHandCursor
                                        : juce::MouseCursor::NormalCursor);
        button->setTooltip (enabled ? enabledTooltip : disabledTooltip);
        button->setVisualState (popupIsOpen (type) ? VESMediaIconButton::VisualState::popupOpen
                                                    : (selected ? VESMediaIconButton::VisualState::selected
                                                                : VESMediaIconButton::VisualState::normal));
    };

    updateIcon (romButton, ToolbarPopupType::rom, true,
                processor.getExternalRomsDirectory().isDirectory(),
                "ROM Folder", {});
    updateIcon (artworkButton, ToolbarPopupType::artwork, true,
                processor.getExternalArtworkDirectoryPath().isNotEmpty(),
                "Artwork Folder", {});
    updateIcon (floppyButton, ToolbarPopupType::floppy, processor.selectedMachineSupportsFloppy(),
                processor.getSelectedMediaPath().isNotEmpty(),
                "Floppy Disk", "Floppy Disk — not supported by this machine");
    updateIcon (cdRomButton, ToolbarPopupType::cdRom, processor.selectedMachineSupportsCdRom(),
                processor.getSelectedCdRomPath().isNotEmpty(),
                "CD-ROM", "CD-ROM — not supported by this machine");
    updateIcon (hardDiskButton, ToolbarPopupType::hardDisk, processor.selectedMachineSupportsHardDisk(),
                processor.getSelectedHardDiskPath().isNotEmpty(),
                "Hard Disk", "Hard Disk — not supported by this machine");

    dismissUnsupportedToolbarPopup();
}

void VintageEmulatorStudioEditor::updateStatus()
{
    const auto snapshot = processor.getDiagnosticSnapshot();
    juce::String stateText;

    juce::Colour stateColour = juce::Colour::fromRGB (205, 205, 205);
    if (snapshot.engineState == EmbeddedEngineState::Failed)
    {
        stateText = "Failed";
        stateColour = failedStatusColour;
    }
    else if (snapshot.engineState == EmbeddedEngineState::Stopping || snapshot.engineState == EmbeddedEngineState::Starting)
        stateText = "Switching";
    else if (snapshot.ready)
    {
        stateText = "Ready";
        stateColour = readyStatusColour;
    }
    else if (snapshot.engineState == EmbeddedEngineState::Booting)
    {
        stateText = "Booting";
        stateColour = bootingStatusColour;
    }
    else
        stateText = snapshot.engineStateText;

    const auto romName = processor.getSelectedMachineDriverName();
    const auto romFound = processor.hasMachineRomByDriverName (romName);
    const auto romText = romFound ? juce::String ("ROM Found") : juce::String ("ROM Not Found");
    statusLabel.setColour (juce::Label::textColourId, stateColour);
    romStatusLabel.setColour (juce::Label::textColourId, romFound ? readyStatusColour : failedStatusColour);
    statusLabel.setText (stateText, juce::dontSendNotification);
    romStatusLabel.setText (romText, juce::dontSendNotification);
}

void VintageEmulatorStudioEditor::addLabel (juce::Label& label, const juce::String& text)
{
    addAndMakeVisible (label);
    label.setText (text, juce::dontSendNotification);
    label.setFont (lookAndFeel.regularFont (14.0f));
    label.setColour (juce::Label::textColourId, juce::Colours::black);
    label.setJustificationType (juce::Justification::centredLeft);
}
