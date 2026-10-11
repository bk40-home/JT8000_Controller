/*
 * JtRmlPanel.cpp — implementation
 * See JtRmlPanel.h for what this replaces and what it deliberately does not.
 */

#include "JtRmlPanel.h"

#include "JtRmlBinding.h"
#include "JtRmlComponent.h"
#include "JtRmlElements.h"
#include "JtRmlFile.h"

#include "BinaryData.h"

#include <map>
#include <vector>

namespace jt
{

juce::File JtRmlPanel::logFile()
{
    /* A plugin has no console, and DBG() is compiled out of a Release build, so
     * RmlUi's diagnostics would otherwise be invisible exactly when they are
     * needed. A file logger is the only reliable channel. */
    return juce::File::getSpecialLocation(juce::File::userDesktopDirectory)
               .getChildFile("JT8000_rml_log.txt");
}

JtRmlPanel::JtRmlPanel(juce::AudioProcessorValueTreeState& state)
    : m_state(state)
{
    /* Installed before anything else, so even a failure during construction is
     * captured. Owned here and cleared in the destructor. */
    if (juce::Logger::getCurrentLogger() == nullptr)
    {
        m_logger.reset(new juce::FileLogger(logFile(),
                                            "JT-8000 RmlUi panel", 0));
        juce::Logger::setCurrentLogger(m_logger.get());
    }
    juce::Logger::writeToLog("JtRmlPanel: constructing");

    m_panel = std::make_unique<JtRmlComponent>("jt8000.rml");
    m_panel->setDesignSize(kDesignW, kDesignH);

    /* Assets must be registered BEFORE the document loads, or every decorator
     * resolves to nothing and the panel comes up blank with only warnings in
     * the log to say why. */
    registerEmbeddedAssets();
    m_panel->reloadDocument();

    addAndMakeVisible(*m_panel);

    /* The skin's own header buttons now drive the view, so they work whether
     * or not the editor's Seq/Main buttons are used. */
    m_panel->onHeaderButton = [this](const juce::String& which)
    {
        if (which == "shift")
            setShift(!m_shift);
        else
            setView(which);
    };

    m_panel->onAction = [this](const juce::String& a)
    {
        if (onAction)
            onAction(a);
    };

    m_binding = std::make_unique<JtRmlBinding>(m_state, *m_panel);

    /* The LCD's two lines are elements in the document, so the binding's
     * readout lands on the panel instead of nowhere. When a menu has claimed
     * onLcdText, the readout is handed to it (shown only while the menu is
     * idle) rather than written straight to the LCD — that is what stops the
     * hover readout and the menu fighting over the two lines. */
    m_binding->onLcdChanged = [this](const juce::String& l1,
                                     const juce::String& l2)
    {
        if (onLcdText)
            onLcdText(l1, l2);
        else
            setLcd(l1, l2);
    };

    m_binding->syncAll();
    wireStepGrids();

    juce::String built;
    built << "JtRmlPanel: constructed, assets="
          << (int) m_panel->fileInterface().embeddedCount();
    juce::Logger::writeToLog(built);
}

JtRmlPanel::~JtRmlPanel()
{
    if (m_logger != nullptr && juce::Logger::getCurrentLogger() == m_logger.get())
        juce::Logger::setCurrentLogger(nullptr);
}

void JtRmlPanel::paint(juce::Graphics& g)
{
    /* The inner component paints over this whenever it is healthy, so anything
     * visible here means it is not. */
    g.fillAll(juce::Colour(0xff2a0f0f));

    juce::String msg;
    msg << "JT-8000 panel: outer wrapper is painting.\n\n"
        << "inner component : "
        << (m_panel != nullptr ? "created" : "NULL") << "\n";

    if (m_panel != nullptr)
        msg << "inner bounds    : " << m_panel->getWidth() << " x "
            << m_panel->getHeight() << "\n"
            << "inner visible   : " << (m_panel->isVisible() ? "yes" : "NO") << "\n"
            << "assets          : "
            << (int) m_panel->fileInterface().embeddedCount() << "\n";

    msg << "wrapper bounds  : " << getWidth() << " x " << getHeight() << "\n\n"
        << "log file        : " << logFile().getFullPathName();

    g.setColour(juce::Colours::orange);
    g.setFont(juce::FontOptions(15.0f));
    g.drawFittedText(msg, getLocalBounds().reduced(20),
                     juce::Justification::topLeft, 12);
}

// =============================================================================
void JtRmlPanel::registerEmbeddedAssets()
{
    /* juce_add_binary_data mangles names for the C identifier - "bg_main.png"
     * becomes "bg_main_png" - but the skin refers to the original filename.
     * getNamedResourceOriginalFilename is what maps back, and forgetting it is
     * the single most likely reason a first build shows nothing. */
    auto& files = m_panel->fileInterface();

    for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
    {
        const char* mangled = BinaryData::namedResourceList[i];
        int size = 0;
        if (const char* data = BinaryData::getNamedResource(mangled, size))
        {
            const char* original =
                BinaryData::getNamedResourceOriginalFilename(mangled);
            files.addEmbedded(original ? original : mangled, data,
                              static_cast<std::size_t>(size));
        }
    }
}

// =============================================================================
void JtRmlPanel::wireStepGrids()
{
    /* The grid speaks the hardware controller's protocol: a step edit is a
     * SELECT write followed by a VALUE write. Both arrive here as Write records
     * and are applied to the APVTS in the order given.
     *
     * Two flags carry meaning that a plain setValueNotifyingHost would lose:
     *
     *   forced — painting the same value onto a DIFFERENT step produces an
     *            equal float. setValueNotifyingHost suppresses no-change
     *            writes, so the engine would never hear an edit the grid is
     *            already showing. Nudging the value by one LSB and back forces
     *            the notification through.
     *
     *   quiet  — moving the cursor loads that step's value for display only.
     *            Writing it out would stamp the previous step's value onto the
     *            new one and corrupt the pattern just by browsing it.
     */
    m_panel->forEachStepGrid([this](JtRmlStepGrid& grid)
    {
        grid.onWrite = [this](const JtRmlStepGrid::Write& w)
        {
            if (w.quiet)
                return;                       // display only — see above

            auto* p = m_state.getParameter(juce::String(w.param.c_str()));
            if (p == nullptr)
                return;                       // unbound: reported at skin-emit

            if (w.forced && juce::approximatelyEqual(p->getValue(), w.value))
            {
                /* One LSB of a 14-bit control: enough to defeat the equality
                 * check, far below anything audible or visible. */
                constexpr float kNudge = 1.0f / 16384.0f;
                const float away = w.value > 0.5f ? w.value - kNudge
                                                  : w.value + kNudge;
                p->setValueNotifyingHost(away);
            }

            p->setValueNotifyingHost(w.value);
        };

        grid.onStepSelected = [this](int lane, int step, float value)
        {
            /* The LCD follows the cursor, which is how a step's exact value is
             * read - the bars show shape, not numbers. */
            m_binding->setPatchName(m_binding->patchName());
            juce::ignoreUnused(lane, step, value);
        };
    });
}

// =============================================================================
void JtRmlPanel::setPatchName(const juce::String& name)
{
    if (m_binding != nullptr)
        m_binding->setPatchName(name);
}

void JtRmlPanel::setLcd(const juce::String& line1, const juce::String& line2)
{
    if (m_panel == nullptr)
        return;
    m_panel->setElementText("lcd1", line1);
    m_panel->setElementText("lcd2", line2);
}

juce::Rectangle<int> JtRmlPanel::lcdBounds() const
{
    if (m_panel == nullptr)
        return {};

    /* m_panel fills this component (see resized()), so its element coordinates
     * are already this panel's coordinates. */
    const auto r1 = m_panel->getElementBounds("lcd1");
    const auto r2 = m_panel->getElementBounds("lcd2");
    if (r1.isEmpty()) return r2;
    if (r2.isEmpty()) return r1;
    return r1.getUnion(r2);
}

juce::Rectangle<int> JtRmlPanel::lcdLine1Bounds() const
{
    return (m_panel != nullptr) ? m_panel->getElementBounds("lcd1")
                                : juce::Rectangle<int>();
}

juce::Image JtRmlPanel::knobImage() const
{
    /* The knob art is ONE sprite sheet (knob_silver.png): 128 frames in a
     * 16 x 8 grid, sliced by the RCSS @spritesheet — not separate files. So we
     * find the sheet among the embedded assets (a PNG whose name mentions
     * "knob"), decode it, and return a single mid-arc frame (index 64, which is
     * row 4 / col 0 = pointer roughly upright) for the encoder to rotate. The
     * grid is read from the image size (width/16 x height/8), so it survives a
     * re-rendered sheet as long as it stays 16 x 8. */
    const char* sheetMangled = nullptr;
    for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
    {
        const char* mangled = BinaryData::namedResourceList[i];
        const char* orig    = BinaryData::getNamedResourceOriginalFilename(mangled);
        const juce::String name (orig != nullptr ? orig : mangled);
        if (name.containsIgnoreCase("knob") && name.endsWithIgnoreCase(".png"))
        {
            sheetMangled = mangled;
            break;
        }
    }
    if (sheetMangled == nullptr)
        return {};

    int size = 0;
    const char* data = BinaryData::getNamedResource(sheetMangled, size);
    if (data == nullptr)
        return {};

    const juce::Image sheet = juce::ImageFileFormat::loadFrom(data, (size_t) size);
    if (! sheet.isValid())
        return {};

    // 128 frames, 16 across x 8 down; frame 64 = row 4, col 0.
    constexpr int cols = 16, rows = 8, frame = 64;
    const int fw = sheet.getWidth()  / cols;
    const int fh = sheet.getHeight() / rows;
    if (fw <= 0 || fh <= 0)
        return sheet;                                   // unexpected: use whole

    const int col = frame % cols;
    const int row = frame / cols;
    return sheet.getClippedImage({ col * fw, row * fh, fw, fh });
}

void JtRmlPanel::setView(const juce::String& view)
{
    m_view = view;
    /* The lit fill is an element carrying a v-<layer> class, and the body's
     * view class shows exactly one of them - so the highlight follows the view
     * with no extra bookkeeping. */
    /* Shift is orthogonal to the layer: the skin's view class is the layer, and
     * SHIFT is a second class on top of it. */
    m_panel->setView(m_shift && view == "main" ? "shift" : view);
}

void JtRmlPanel::setShift(const bool on)
{
    if (on == m_shift)
        return;
    m_shift = on;
    setView(m_view);
}

void JtRmlPanel::setVoiceActivity(const uint8_t mask, const bool alive)
{
    for (int v = 0; v < 8; ++v)
    {
        const bool on = alive && ((mask >> v) & 1) != 0;
        juce::String id;
        id << "voice" << v;
        m_panel->setElementClass(id, "on", on);
    }
}

void JtRmlPanel::setPlayhead(const int step)
{
    m_panel->forEachStepGrid([step](JtRmlStepGrid& grid)
    {
        grid.setPlayhead(step);
    });
}

void JtRmlPanel::setSkinDiskRoot(const juce::File& dir)
{
    m_panel->setSkinDiskRoot(dir);
}

void JtRmlPanel::loadSkinFile(const juce::File& rmlFile)
{
    /* Parent folder becomes the disk-root; the file's leaf becomes the document
     * requested. reloadSkin() re-reads, re-syncs bindings and re-wires the step
     * grids - without it the disk-root would be set but nothing would change,
     * and any new controls would be inert. */
    m_panel->setSkinDiskRoot(rmlFile.getParentDirectory());
    m_panel->setDocumentName(rmlFile.getFileName());
    reloadSkin();
}

void JtRmlPanel::resetSkin()
{
    m_panel->setSkinDiskRoot(juce::File{});
    m_panel->setDocumentName({});          // restores jt8000.rml
    reloadSkin();
}

void JtRmlPanel::reloadSkin()
{
    m_panel->reloadDocument();
    if (m_binding != nullptr)
        m_binding->syncAll();
    wireStepGrids();
}

void JtRmlPanel::resized()
{
    m_panel->setBounds(getLocalBounds());
}

} // namespace jt
