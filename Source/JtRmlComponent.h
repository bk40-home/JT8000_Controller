/*
 * JtRmlComponent.h — the juce::Component that hosts an RmlUi context
 *
 * This is the last piece of the bridge. It owns the context, translates JUCE
 * input into RmlUi, drives the update tick, and paints through JtRmlRenderer.
 *
 * BUILD NOTE
 *   RmlUi is built with RMLUI_FONT_ENGINE=none. The skin bakes every legend
 *   into its background image, so nothing needs a glyph, and FreeType drops out
 *   of the dependency list entirely. If text is ever needed at runtime, that
 *   option has to change and a font engine has to be supplied.
 *
 * SCALING
 *   The document is authored at 3840x2160 dp. The context is told the pixel
 *   size of this component and a density-independent pixel ratio, and RmlUi
 *   does the rest - there is no second set of coordinates anywhere.
 *
 * THE UPDATE TICK
 *   Context::Update() must run even when nothing has been touched: animations,
 *   transitions and hover states advance there. It is driven from a Timer at a
 *   modest rate rather than from paint(), because a plugin editor may be
 *   repainted by the host at an unpredictable rate, or not at all.
 *
 *   repaint() is only called when Update() reports the document changed, or
 *   when a parameter has moved. Repainting every tick would burn CPU on a
 *   static panel, and a 141-control panel is static most of the time.
 *
 * INPUT
 *   RmlUi wants button indices, a modifier bitfield and its own key ids. The
 *   translation lives in this file so nothing else has to know about
 *   Rml::Input.
 *
 *   One subtlety: RmlUi tracks the mouse itself and expects a ProcessMouseMove
 *   for every movement including drags. JUCE delivers mouseDrag separately from
 *   mouseMove, so both are forwarded to the same handler; omitting the drag
 *   case is why a control can "stick" after the pointer leaves it.
 */

#pragma once

#include <RmlUi/Core/EventListener.h>

#include <juce_gui_basics/juce_gui_basics.h>

#include <atomic>
#include <functional>
#include <string>
#include <memory>

namespace Rml
{
    class Context;
    class ElementDocument;
}

namespace jt
{

class JtRmlRenderer;
class JtRmlSystem;
class JtRmlFile;class JtRmlFont;

class JtRmlComponent : public juce::Component,
                       private juce::Timer
{
public:
    /* `documentName` is resolved through JtRmlFile, so it is the embedded
     * asset's name rather than a path. */
    explicit JtRmlComponent(juce::String documentName = "jt8000.rml");
    ~JtRmlComponent() override;

    /* Authoring size of the document. The component scales to fit it. */
    void setDesignSize(int widthDp, int heightDp);

    /* Development override: a directory whose files win over the embedded
     * copies, so the skin can be edited without rebuilding. */
    void setSkinDiskRoot(const juce::File& dir);

    /* The file interface, so the editor can register its BinaryData before the
     * document loads. */
    JtRmlFile& fileInterface() { return *m_files; }

    /* Visits every <stepgrid> in the document. Used to attach the step-write
     * protocol and to push the playhead, without the caller needing to know how
     * the document is structured. */
    void forEachStepGrid(const std::function<void(class JtRmlStepGrid&)>& fn);

    /* Raised when a header hit area is clicked: the layer name, or "shift".
     * The header artwork is painted into the background, so these are
     * transparent elements over it - the look stays baked, the behaviour is
     * real. */
    std::function<void(const juce::String&)> onHeaderButton;

    /* Sets the text of an element by id. Used for the LCD, whose two lines are
     * elements rather than baked artwork because they change constantly. */
    void setElementText(const juce::String& id, const juce::String& text);

    /* Adds or removes a class on an element by id. Used for the voice dots and
     * the lit layer button, both of which are state rather than text. */
    void setElementClass(const juce::String& id, const juce::String& cls,
                         bool on);

    /* Raised by the top row of header buttons: "sendall", "loadsyx", "save",
     * "load", "init". The editor keeps its existing handlers. */
    std::function<void(const juce::String&)> onAction;

    /* Reloads the document from scratch. Cheap enough to bind to a key. */
    void reloadDocument();

    /* Switches the view - "main", "shift", "reverb", "seq", "arp", "setup".
     * A view is a class on <body>; the background decorator and which controls
     * are visible both follow from it. */
    void setView(const juce::String& view);
    const juce::String& view() const { return m_view; }

    /* Raised when a control moves: (param key, normalised value). The editor
     * hooks this to the APVTS. */
    std::function<void(const juce::String&, float)> onParameterChanged;

    /* Raised when the pointer enters or leaves a control, so the LCD can show
     * what it is over. An empty key means the pointer left. */
    std::function<void(const juce::String&)> onHover;

    /* Pushes a value in from the host without echoing back through
     * onParameterChanged - for automation, patch loads and incoming NRPN. */
    void setParameterValue(const juce::String& key, float normalised);

    // --- juce::Component -----------------------------------------------------
    void paint(juce::Graphics&) override;
    void resized() override;

    void mouseMove(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&,
                        const juce::MouseWheelDetails&) override;
    bool keyPressed(const juce::KeyPress&) override;
    bool keyStateChanged(bool isKeyDown) override;

private:
    void timerCallback() override;

    /* One-time RmlUi startup, shared by every instance: Initialise() is global,
     * so it is reference counted rather than tied to one editor. A plugin can
     * have several editors open at once, and the second must not re-initialise
     * the library or the first will lose its interfaces. */
    static void acquireCore(JtRmlSystem*, JtRmlRenderer*, JtRmlFile*,
                            JtRmlFont*);
    static void releaseCore();

    void sendMouse(const juce::MouseEvent&, bool isDrag);
    static int modifiers(const juce::ModifierKeys&);
    juce::Point<int> toContext(const juce::MouseEvent&) const;

    void bindElements();

    /* Rml::EventListener cannot be this class, because Element already brings
     * its own ProcessEvent into scope. A small nested listener keeps them
     * separate. */
    struct HeaderListener : Rml::EventListener
    {
        JtRmlComponent* owner = nullptr;
        void ProcessEvent(Rml::Event& event) override;
    };
    HeaderListener m_headerListener;

    std::unique_ptr<JtRmlRenderer> m_renderer;
    std::unique_ptr<JtRmlSystem> m_system;
    std::unique_ptr<JtRmlFile> m_files;
    std::unique_ptr<JtRmlFont> m_font;

    Rml::Context* m_context = nullptr;          // owned by RmlUi
    Rml::ElementDocument* m_document = nullptr; // owned by the context

    Rml::String m_contextName;      // unique per instance
    juce::String m_documentName;
    juce::String m_view { "main" };
    int m_designW = 3840;
    int m_designH = 2160;
    bool m_dirty = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(JtRmlComponent)
};

} // namespace jt
