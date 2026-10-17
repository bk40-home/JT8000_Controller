/*
 * JtRmlComponent.cpp — implementation
 * See JtRmlComponent.h for the scaling model and the update-tick reasoning.
 */

#include "JtRmlComponent.h"

#include "JtRmlFile.h"
#include "JtRmlFont.h"
#include "JtRmlCombo.h"
#include "JtRmlElements.h"
#include "JtRmlBassGrid.h"
#include "JtRmlKnob.h"
#include "JtRmlRenderer.h"
#include "JtRmlSystem.h"

#include <RmlUi/Core.h>

namespace jt
{

namespace
{
    /* Rml::Initialise() is global. Reference counted so a second editor does
     * not re-initialise the library and strip the first of its interfaces. */
    int g_coreRefs = 0;

    constexpr int kTickHz = 30;

    Rml::Input::KeyIdentifier toRmlKey(const juce::KeyPress& kp)
    {
        using namespace Rml::Input;
        const int c = kp.getKeyCode();

        if (c >= '0' && c <= '9')
            return static_cast<KeyIdentifier>(KI_0 + (c - '0'));
        if (c >= 'A' && c <= 'Z')
            return static_cast<KeyIdentifier>(KI_A + (c - 'A'));
        if (c >= 'a' && c <= 'z')
            return static_cast<KeyIdentifier>(KI_A + (c - 'a'));

        if (c == juce::KeyPress::escapeKey)     return KI_ESCAPE;
        if (c == juce::KeyPress::returnKey)     return KI_RETURN;
        if (c == juce::KeyPress::tabKey)        return KI_TAB;
        if (c == juce::KeyPress::spaceKey)      return KI_SPACE;
        if (c == juce::KeyPress::backspaceKey)  return KI_BACK;
        if (c == juce::KeyPress::deleteKey)     return KI_DELETE;
        if (c == juce::KeyPress::leftKey)       return KI_LEFT;
        if (c == juce::KeyPress::rightKey)      return KI_RIGHT;
        if (c == juce::KeyPress::upKey)         return KI_UP;
        if (c == juce::KeyPress::downKey)       return KI_DOWN;
        if (c == juce::KeyPress::homeKey)       return KI_HOME;
        if (c == juce::KeyPress::endKey)        return KI_END;

        return KI_UNKNOWN;
    }
}

// =============================================================================
JtRmlComponent::JtRmlComponent(juce::String documentName)
    : m_documentName(std::move(documentName))
{
    setOpaque(true);
    setWantsKeyboardFocus(true);

    m_renderer = std::make_unique<JtRmlRenderer>();
    m_system = std::make_unique<JtRmlSystem>();
    m_files = std::make_unique<JtRmlFile>();
    m_font = std::make_unique<JtRmlFont>();

    /* Embedded assets are registered by the editor before the document loads;
     * see JtRmlFile::addEmbedded. */
    acquireCore(m_system.get(), m_renderer.get(), m_files.get(),
                m_font.get());

    m_headerListener.owner = this;

    /* Unique per instance: contexts are held in a global registry keyed by
     * name, so two plugin instances sharing "jt8000" would fight over one
     * context and RemoveContext would take the wrong one. */
    static std::atomic<int> s_nextId{1};
    m_contextName = "jt8000_"
                    + std::to_string(s_nextId.fetch_add(1));

    m_context = Rml::CreateContext(m_contextName,
                                   Rml::Vector2i(m_designW, m_designH));
    if (m_context != nullptr)
    {
        JtRmlKnob::registerElement();
        registerJtElements();          // fader, toggle, stepgrid, combo
    }

    /* The document is NOT loaded here. Assets are registered by the owner
     * (JtRmlPanel) after construction, and loading first would fail, fill the
     * log with missing-file errors, and mask whatever the real problem turns
     * out to be. The owner calls reloadDocument() when it is ready. */

    startTimerHz(kTickHz);
}

void JtRmlComponent::HeaderListener::ProcessEvent(Rml::Event& event)
{
    Rml::Element* e = event.GetCurrentElement();
    if (e == nullptr || owner == nullptr)
        return;

    /* An action button is a separate channel: the editor already has handlers
     * for Send All, Load SYX and the rest, so they are routed rather than
     * reimplemented. */
    const Rml::String action = e->GetAttribute<Rml::String>("data-action", "");
    if (!action.empty())
    {
        if (owner->onAction)
            owner->onAction(juce::String(action.c_str()));
        event.StopPropagation();
        return;
    }

    const Rml::String layer = e->GetAttribute<Rml::String>("data-layer", "");
    owner->onHeaderButton(juce::String(layer.empty() ? "shift" : layer.c_str()));
    event.StopPropagation();
}

JtRmlComponent::~JtRmlComponent()
{
    stopTimer();

    /* ORDER MATTERS, and getting it wrong crashes the host on close.
     *
     * JtRmlFont caches Rml::CallbackTexture objects. Those are render resources
     * OWNED BY RmlUi's RenderManager, which Rml::Shutdown() destroys. The font
     * engine is a member, so it would otherwise be destructed after the body of
     * this destructor has already called Shutdown - and each cached texture
     * would then release itself into freed memory.
     *
     * So the cache is emptied first, while the manager is still alive. */
    if (m_font != nullptr)
        m_font->ReleaseFontResources();

    if (m_context != nullptr)
    {
        Rml::RemoveContext(m_contextName);
        m_context = nullptr;
        m_document = nullptr;
    }

    releaseCore();
}

void JtRmlComponent::acquireCore(JtRmlSystem* sys, JtRmlRenderer* ren,
                                 JtRmlFile* files, JtRmlFont* font)
{
    if (g_coreRefs++ > 0)
        return;

    /* All four interfaces must be installed BEFORE Initialise(): RmlUi caches
     * them at startup, and a font engine set afterwards is ignored - which
     * shows up as a panel with every combo and the LCD blank. */
    Rml::SetSystemInterface(sys);
    Rml::SetRenderInterface(ren);
    Rml::SetFileInterface(files);
    Rml::SetFontEngineInterface(font);
    Rml::Initialise();
}

void JtRmlComponent::releaseCore()
{
    if (--g_coreRefs > 0)
        return;
    Rml::Shutdown();
}

// =============================================================================
void JtRmlComponent::setDesignSize(const int widthDp, const int heightDp)
{
    m_designW = widthDp;
    m_designH = heightDp;
    resized();
}

void JtRmlComponent::setSkinDiskRoot(const juce::File& dir)
{
    m_files->setDiskRoot(dir.getFullPathName().toStdString());
}

void JtRmlComponent::setDocumentName(juce::String name)
{
    /* Empty resets to the factory default so callers can pass a cleared name
     * alongside an empty disk-root to return to the embedded skin. */
    m_documentName = name.isEmpty() ? juce::String("jt8000.rml") : std::move(name);
}

void JtRmlComponent::reloadDocument()
{
    if (m_context == nullptr)
        return;

    if (m_document != nullptr)
    {
        m_document->Close();
        m_document = nullptr;
    }

    m_document = m_context->LoadDocument(m_documentName.toStdString());
    if (m_document == nullptr)
    {
        /* The System interface has already logged why. Leaving the panel blank
         * rather than crashing is the right outcome for a skin problem. */
        return;
    }

    m_document->Show();
    setView(m_view);
    bindElements();
    m_dirty = true;
    repaint();
}

void JtRmlComponent::setView(const juce::String& viewName)
{
    m_view = viewName;
    if (m_document == nullptr)
        return;

    /* A view is a class on <body>: the background decorator and which controls
     * are visible both follow from it, so switching costs one class swap and no
     * relayout. */
    Rml::Element* body = m_document;
    for (const char* v : { "main", "shift", "reverb", "seq", "arp", "setup" })
        body->SetClass(v, m_view == v);
    // State flags ride along, so a reloaded document gets them back.
    for (const auto& f : m_bodyFlags)
        body->SetClass(f.first.toStdString(), f.second);

    m_dirty = true;
    repaint();
}

void JtRmlComponent::setBodyFlag(const juce::String& cls, bool on)
{
    const auto it = m_bodyFlags.find(cls);
    if (it != m_bodyFlags.end() && it->second == on)
        return;                                    // unchanged: no restyle
    m_bodyFlags[cls] = on;
    if (m_document == nullptr)
        return;                                    // applied on the next load
    m_document->SetClass(cls.toStdString(), on);
    m_dirty = true;
    repaint();
}

void JtRmlComponent::bindElements()
{
    if (m_document == nullptr)
        return;

    /* Header hit areas. Listened to on the document, so a rebuilt document does
     * not need re-binding element by element. */
    Rml::ElementList header;
    m_document->GetElementsByTagName(header, "div");
    for (Rml::Element* e : header)
    {
        const bool isLayer = e->IsClassSet("layerbtn");
        const bool isShift = e->IsClassSet("shiftbtn");
        const bool isAction = e->IsClassSet("actionbtn");
        if (!isLayer && !isShift && !isAction)
            continue;

        e->AddEventListener(Rml::EventId::Click, &m_headerListener);
    }
    /* EVERY control type is wired, not just the knob. Combos, faders and
     * toggles were bound in neither direction: turning one changed nothing in
     * the APVTS, and a value arriving from the host never reached the control -
     * which is why a combo's displayed value never updated.
     *
     * The types do not share one base (JtRmlKnob predates JtRmlControl, and
     * JtRmlCombo is its own thing), so each is handled by tag. */
    const auto reportChange = [this](const Rml::String& param, float v)
    {
        if (onParameterChanged)
            onParameterChanged(juce::String(param.c_str()), v);
        m_dirty = true;
    };
    const auto reportHover = [this](const Rml::String& param, bool entered)
    {
        if (onHover)
            onHover(entered ? juce::String(param.c_str()) : juce::String());
    };

    Rml::ElementList found;
    m_document->GetElementsByTagName(found, "knob");
    for (Rml::Element* e : found)
        if (auto* k = dynamic_cast<JtRmlKnob*>(e))
        {
            k->onValueChanged = [reportChange](JtRmlKnob& c, float v)
                                { reportChange(c.getParam(), v); };
            k->onHover = [reportHover](JtRmlKnob& c, bool in)
                         { reportHover(c.getParam(), in); };
        }

    for (const char* tag : { "fader", "toggle", "stepgrid" })
    {
        found.clear();
        m_document->GetElementsByTagName(found, tag);
        for (Rml::Element* e : found)
            if (auto* c = dynamic_cast<JtRmlControl*>(e))
            {
                c->onValueChanged = [reportChange](JtRmlControl& x, float v)
                                    { reportChange(x.getParam(), v); };
                c->onHover = [reportHover](JtRmlControl& x, bool in)
                             { reportHover(x.getParam(), in); };
            }
    }

    found.clear();
    m_document->GetElementsByTagName(found, "combo");
    for (Rml::Element* e : found)
        if (auto* c = dynamic_cast<JtRmlCombo*>(e))
        {
            c->onValueChanged = [reportChange](JtRmlCombo& x, float v)
                                { reportChange(x.getParam(), v); };
            c->onHover = [reportHover](JtRmlCombo& x, bool in)
                         { reportHover(x.getParam(), in); };
        }
}

void JtRmlComponent::forEachStepGrid(
    const std::function<void(JtRmlStepGrid&)>& fn)
{
    if (m_document == nullptr)
        return;

    Rml::ElementList grids;
    m_document->GetElementsByTagName(grids, "stepgrid");
    for (Rml::Element* e : grids)
        if (auto* g = dynamic_cast<JtRmlStepGrid*>(e))
            fn(*g);
}

void JtRmlComponent::forEachBassGrid(
    const std::function<void(JtRmlBassGrid&)>& fn)
{
    if (m_document == nullptr)
        return;

    Rml::ElementList grids;
    m_document->GetElementsByTagName(grids, "bassgrid");
    for (Rml::Element* e : grids)
        if (auto* g = dynamic_cast<JtRmlBassGrid*>(e))
            fn(*g);
}

void JtRmlComponent::setElementText(const juce::String& id,
                                    const juce::String& text)
{
    if (m_document == nullptr)
        return;

    Rml::Element* e = m_document->GetElementById(id.toStdString());
    if (e == nullptr)
        return;

    /* Escaped: a patch name may legitimately contain '&' or '<', and unescaped
     * it would either vanish or break the element's content. */
    Rml::String out;
    for (const auto c : text.toStdString())
    {
        if (c == '&')      out += "&amp;";
        else if (c == '<') out += "&lt;";
        else if (c == '>') out += "&gt;";
        else               out += c;
    }

    if (e->GetInnerRML() != out)
    {
        e->SetInnerRML(out);
        m_dirty = true;
    }
}

juce::Rectangle<int> JtRmlComponent::getElementBounds(const juce::String& id) const
{
    if (m_document == nullptr)
        return {};

    Rml::Element* e = m_document->GetElementById(id.toStdString());
    if (e == nullptr)
        return {};

    /* GetAbsoluteOffset()/GetOffset{Width,Height}() report pixels within the
     * context, and the context is sized to this component (see resized()), so
     * these ARE component coordinates. Same accessors JtRmlCombo uses to place
     * its dropdown. */
    const Rml::Vector2f pos = e->GetAbsoluteOffset();
    const int w = (int) e->GetOffsetWidth();
    const int h = (int) e->GetOffsetHeight();
    return { (int) pos.x, (int) pos.y, w, h };
}

void JtRmlComponent::setElementClass(const juce::String& id,
                                     const juce::String& cls, const bool on)
{
    if (m_document == nullptr)
        return;
    if (Rml::Element* e = m_document->GetElementById(id.toStdString()))
    {
        if (e->IsClassSet(cls.toStdString()) != on)
        {
            e->SetClass(cls.toStdString(), on);
            m_dirty = true;
        }
    }
}

void JtRmlComponent::setParameterValue(const juce::String& key,
                                       const float normalised)
{
    if (m_document == nullptr)
        return;

    const std::string want = key.toStdString();

    /* notify = false throughout: a value pushed in from the host must not echo
     * back out, or automation and the UI fight each other. */
    Rml::ElementList found;
    m_document->GetElementsByTagName(found, "knob");
    for (Rml::Element* e : found)
        if (auto* k = dynamic_cast<JtRmlKnob*>(e))
            if (k->getParam() == want)
            {
                k->setNormalised(normalised, false);
                m_dirty = true;
                return;
            }

    for (const char* tag : { "fader", "toggle", "stepgrid" })
    {
        found.clear();
        m_document->GetElementsByTagName(found, tag);
        for (Rml::Element* e : found)
            if (auto* c = dynamic_cast<JtRmlControl*>(e))
                if (c->getParam() == want)
                {
                    c->setNormalised(normalised, false);
                    m_dirty = true;
                    return;
                }
    }

    found.clear();
    m_document->GetElementsByTagName(found, "combo");
    for (Rml::Element* e : found)
        if (auto* c = dynamic_cast<JtRmlCombo*>(e))
            if (c->getParam() == want)
            {
                c->setNormalised(normalised, false);
                m_dirty = true;
                return;
            }
}

// =============================================================================
void JtRmlComponent::resized()
{
    if (m_context == nullptr)
        return;

    const int w = juce::jmax(1, getWidth());
    const int h = juce::jmax(1, getHeight());

    /* One ratio, derived from the authoring size. Everything in the skin is in
     * dp, so nothing else needs to know the window scale. */
    const float ratio = juce::jmin(static_cast<float>(w) / static_cast<float>(m_designW),
                                   static_cast<float>(h) / static_cast<float>(m_designH));

    m_context->SetDimensions(Rml::Vector2i(w, h));
    m_context->SetDensityIndependentPixelRatio(ratio);
    m_dirty = true;
}

void JtRmlComponent::timerCallback()
{
    if (m_context == nullptr)
        return;

    /* Update() must run even when nothing was touched - animations, transitions
     * and hover states advance here. */
    if (m_context->Update())
        m_dirty = true;

    Rml::String cursor;
    if (m_system->takeCursorIfChanged(cursor))
        setMouseCursor(cursor == "move" ? juce::MouseCursor::DraggingHandCursor
                                        : juce::MouseCursor::NormalCursor);

    /* Repaint only when something changed. A 141-control panel is static most
     * of the time, and repainting at the tick rate would burn CPU for nothing. */
    if (m_dirty)
    {
        m_dirty = false;
        repaint();
    }
}

void JtRmlComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff14171c));

    /* A blank panel is the hardest failure to diagnose from outside: the
     * context can be missing, the document can have failed to load, the assets
     * can be unregistered, or the component can simply have no size - and all
     * four look identical. So the panel says which. */
    const bool haveContext  = (m_context  != nullptr);
    const bool haveDocument = (m_document != nullptr);

    if (!haveContext || !haveDocument || getWidth() < 2 || getHeight() < 2)
    {
        g.setColour(juce::Colours::orange);
        g.setFont(juce::FontOptions(16.0f));
        juce::String msg;
        msg << "JT-8000 panel not drawing\n\n"
            << "context   : " << (haveContext ? "ok" : "NULL") << "\n"
            << "document  : " << (haveDocument ? "ok" : "NULL  <- check the log for 'rml'") << "\n"
            << "assets    : " << (int) m_files->embeddedCount() << " registered\n"
            << "bounds    : " << getWidth() << " x " << getHeight() << "\n"
            << "document  : " << m_documentName << "\n\n"
            << "--- RmlUi log ---\n"
            << m_system->recentLog().joinIntoString("\n");
        g.drawFittedText(msg, getLocalBounds().reduced(24),
                         juce::Justification::topLeft, 12);
        return;
    }

    /* Text is rasterised at the PHYSICAL pixel scale of this surface (250%
     * desktop scaling, zoom), not at logical size - otherwise each string
     * texture is stretched up when drawn and small glyphs blur (S read as 8).
     * The font engine is the process-wide one RmlUi was initialised with,
     * which may belong to another instance, so it is fetched from RmlUi rather
     * than using m_font. Unchanged scale costs one compare. */
    if (auto* font = dynamic_cast<JtRmlFont*>(Rml::GetFontEngineInterface()))
        font->setRasterScale(g.getInternalContext().getPhysicalPixelScaleFactor());

    m_renderer->beginFrame(g);
    m_context->Render();
    m_renderer->endFrame();

    /* If the document is live but produced no geometry at all, the skin loaded
     * and then drew nothing - almost always a decorator whose image failed to
     * resolve. Worth saying so rather than showing an empty panel. */
    const auto& st = m_renderer->stats();
    if (st.quads == 0 && st.fills == 0 && st.pathFallbacks == 0)
    {
        g.setColour(juce::Colours::orange);
        g.setFont(juce::FontOptions(16.0f));
        juce::String note;
        note << "Document loaded but drew nothing.\n"
             << "assets    : " << (int) m_files->embeddedCount() << " registered\n\n"
             << "--- RmlUi log ---\n"
             << m_system->recentLog().joinIntoString("\n");
        g.drawFittedText(note, getLocalBounds().reduced(24),
                         juce::Justification::topLeft, 12);
    }
}

// =============================================================================
// Input
// =============================================================================
int JtRmlComponent::modifiers(const juce::ModifierKeys& m)
{
    int out = 0;
    if (m.isCtrlDown())    out |= Rml::Input::KM_CTRL;
    if (m.isShiftDown())   out |= Rml::Input::KM_SHIFT;
    if (m.isAltDown())     out |= Rml::Input::KM_ALT;
    if (m.isCommandDown()) out |= Rml::Input::KM_META;
    return out;
}

juce::Point<int> JtRmlComponent::toContext(const juce::MouseEvent& e) const
{
    /* RmlUi works in pixels and the context is sized in pixels, so the
     * component's own coordinates go straight through. */
    return e.getPosition();
}

void JtRmlComponent::sendMouse(const juce::MouseEvent& e, bool)
{
    if (m_context == nullptr)
        return;
    const auto p = toContext(e);
    m_context->ProcessMouseMove(p.x, p.y, modifiers(e.mods));
    m_dirty = true;
}

void JtRmlComponent::mouseMove(const juce::MouseEvent& e) { sendMouse(e, false); }

void JtRmlComponent::mouseDrag(const juce::MouseEvent& e)
{
    /* RmlUi tracks the pointer itself and expects a move for every movement,
     * drags included. JUCE reports drags separately, so they are forwarded to
     * the same place; skipping this is why a control can stick to the cursor. */
    sendMouse(e, true);
}

void JtRmlComponent::mouseDown(const juce::MouseEvent& e)
{
    if (m_context == nullptr)
        return;
    const auto p = toContext(e);
    m_context->ProcessMouseMove(p.x, p.y, modifiers(e.mods));
    m_context->ProcessMouseButtonDown(e.mods.isRightButtonDown() ? 1 : 0,
                                      modifiers(e.mods));
    m_dirty = true;
}

void JtRmlComponent::mouseUp(const juce::MouseEvent& e)
{
    if (m_context == nullptr)
        return;
    m_context->ProcessMouseButtonUp(e.mods.isRightButtonDown() ? 1 : 0,
                                    modifiers(e.mods));
    m_dirty = true;
}

void JtRmlComponent::mouseExit(const juce::MouseEvent&)
{
    if (m_context == nullptr)
        return;
    m_context->ProcessMouseLeave();
    m_dirty = true;
}

void JtRmlComponent::mouseWheelMove(const juce::MouseEvent& e,
                                    const juce::MouseWheelDetails& w)
{
    if (m_context == nullptr)
        return;
    /* RmlUi's positive wheel delta means scrolling down; JUCE's deltaY is
     * positive upwards, hence the negation. */
    m_context->ProcessMouseWheel(-w.deltaY, modifiers(e.mods));
    m_dirty = true;
}

bool JtRmlComponent::keyPressed(const juce::KeyPress& kp)
{
    if (m_context == nullptr)
        return false;

    const auto key = toRmlKey(kp);
    bool consumed = false;
    if (key != Rml::Input::KI_UNKNOWN)
        consumed = !m_context->ProcessKeyDown(key, modifiers(kp.getModifiers()));

    const auto ch = kp.getTextCharacter();
    if (ch >= 32)
        m_context->ProcessTextInput(static_cast<Rml::Character>(ch));

    m_dirty = true;
    return consumed;
}

bool JtRmlComponent::keyStateChanged(bool)
{
    /* Key-up is not tracked per key by JUCE's component callbacks. RmlUi only
     * needs it for held-key behaviour, which this skin does not use. */
    return false;
}

} // namespace jt
