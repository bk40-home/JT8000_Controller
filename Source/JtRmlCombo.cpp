/*
 * JtRmlCombo.cpp — implementation
 * See JtRmlCombo.h for why the popup is a document element rather than a
 * native menu, and why the value is quantised on the way in.
 */

#include "JtRmlCombo.h"

#include "gen/JtParamTable.h"   // N4: JtParams::kParams — the APVTS's own option lists

#include <algorithm>
#include <cstring>
#include <cmath>
#include <sstream>

namespace jt
{

namespace
{
    constexpr const char* kAttrParam = "key";   // APVTS id == dotted key; was
                                                // "param" (old underscored id).
    constexpr const char* kAttrOptions = "options";
    constexpr const char* kAttrIndex = "index";

    /* In DP, matching the skin. These were applied as raw pixels while every
     * other measurement is dp, so at a 0.5 density ratio each 46px row held
     * 13px of text - which is the dead space between items.
     *
     * The list is positioned in document space (pixels), so these are scaled by
     * the context's ratio at the point of use. */
    constexpr float kItemHeightDp = 46.0f;
    constexpr float kListPadDp = 4.0f;
}

// =============================================================================
JtRmlCombo::JtRmlCombo(const Rml::String& tag)
    : Rml::Element(tag)
{
}

JtRmlCombo::~JtRmlCombo()
{
    /* The list is parented to the document, so it would outlive this element if
     * the combo were removed while open. */
    closeList();
}

void JtRmlCombo::registerElement()
{
    static Rml::ElementInstancerGeneric<JtRmlCombo> instancer;
    Rml::Factory::RegisterElementInstancer("combo", &instancer);
}

// =============================================================================
// Attributes
// =============================================================================
void JtRmlCombo::parseOptions(const Rml::String& csv)
{
    m_options.clear();
    std::stringstream ss(std::string(csv.c_str()));
    std::string item;
    while (std::getline(ss, item, ','))
    {
        /* Trim: the emitter writes "SIN, SAW, SQR" for legibility, and a
         * leading space would show in the readout. */
        const auto b = item.find_first_not_of(" \t");
        const auto e = item.find_last_not_of(" \t");
        if (b != std::string::npos)
            m_options.push_back(item.substr(b, e - b + 1));
    }
    if (m_options.empty())
        m_options.push_back("");

    m_index = std::clamp(m_index, 0, static_cast<int>(m_options.size()) - 1);
    refreshText();
}

bool JtRmlCombo::loadOptionsFromTable()
{
    if (m_param.empty())
        return false;

    /* Linear scan of ~300 rows, once per combo at skin load. A map would cost
     * more to build than the handful of combos ever look up. */
    for (int i = 0; i < JtParams::kNumParams; ++i)
    {
        const JtParams::Param& p = JtParams::kParams[i];
        if (std::strcmp(p.juceId, m_param.c_str()) != 0)
            continue;
        if (p.type != JtParams::Type::Select || p.options == nullptr || p.optionCount == 0)
            return false;                       // known key, but not a list

        m_options.clear();
        for (int o = 0; o < p.optionCount; ++o)
            m_options.emplace_back(p.options[o].name);

        m_index = std::clamp(m_index, 0, static_cast<int>(m_options.size()) - 1);
        refreshText();
        return true;
    }
    return false;                               // skin-only key: use the attribute
}

void JtRmlCombo::OnAttributeChange(const Rml::ElementAttributes& changed)
{
    Rml::Element::OnAttributeChange(changed);

    if (changed.count(kAttrParam))
    {
        m_param = GetAttribute<Rml::String>(kAttrParam, "");
        m_tableOptions = loadOptionsFromTable();

        /* Key no longer in the table: fall back to the skin's own list. */
        if (!m_tableOptions && HasAttribute(kAttrOptions))
            parseOptions(GetAttribute<Rml::String>(kAttrOptions, ""));
    }

    /* N4: the table wins over a stale skin list (see the header).  Skipped
     * when the key change above already handled the attribute. */
    if (changed.count(kAttrOptions) && !m_tableOptions && !changed.count(kAttrParam))
        parseOptions(GetAttribute<Rml::String>(kAttrOptions, ""));

    if (changed.count(kAttrIndex))
        setIndex(GetAttribute<int>(kAttrIndex, 0), false);
}

// =============================================================================
// Value
// =============================================================================
float JtRmlCombo::getNormalised() const
{
    const int n = static_cast<int>(m_options.size());
    if (n <= 1)
        return 0.0f;
    return static_cast<float>(m_index) / static_cast<float>(n - 1);
}

void JtRmlCombo::setNormalised(const float value, const bool notify)
{
    const int n = static_cast<int>(m_options.size());
    if (n <= 0)
        return;

    /* Quantise on the way in. A combo sitting between two options is
     * meaningless, and an un-quantised value would drift a little further on
     * every automation pass. */
    const float v = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
    setIndex(static_cast<int>(std::lround(v * static_cast<float>(n - 1))),
             notify);
}

void JtRmlCombo::setIndex(const int index, const bool notify)
{
    const int n = static_cast<int>(m_options.size());
    const int i = std::clamp(index, 0, std::max(0, n - 1));
    if (i == m_index)
        return;

    m_index = i;
    refreshText();

    if (notify && onValueChanged)
        onValueChanged(*this, getNormalised());
}

void JtRmlCombo::refreshText()
{
    if (m_options.empty())
        return;
    /* SetInnerRML rather than a child element: the combo has exactly one piece
     * of text and no structure worth preserving. */
    SetInnerRML(Rml::String(m_options[static_cast<std::size_t>(m_index)]));
}

// =============================================================================
// Popup
// =============================================================================
void JtRmlCombo::openList()
{
    if (m_list != nullptr)
        return;

    Rml::ElementDocument* doc = GetOwnerDocument();
    if (doc == nullptr || m_options.empty())
        return;

    Rml::ElementPtr list = doc->CreateElement("div");
    list->SetClass("combo-list", true);

    /* dp -> px for the absolutely positioned list. */
    const float dpr = (GetContext() != nullptr)
                      ? GetContext()->GetDensityIndependentPixelRatio()
                      : 1.0f;
    const float itemH = kItemHeightDp * dpr;
    const float padH = kListPadDp * dpr;

    const float w = GetOffsetWidth();
    const float h = static_cast<float>(m_options.size()) * itemH + padH * 2.0f;

    const Rml::Vector2f pos = GetAbsoluteOffset();
    float top = pos.y + GetOffsetHeight();

    /* Flip above the control if the list would run off the foot of the panel -
     * a list you cannot see the bottom of is worse than one that opens upward. */
    const float docH = doc->GetOffsetHeight();
    if (top + h > docH && pos.y - h > 0.0f)
        top = pos.y - h;

    list->SetProperty(Rml::PropertyId::Position,
                      Rml::Property(Rml::Style::Position::Absolute));
    list->SetProperty(Rml::PropertyId::Left, Rml::Property(pos.x, Rml::Unit::PX));
    list->SetProperty(Rml::PropertyId::Top, Rml::Property(top, Rml::Unit::PX));
    list->SetProperty(Rml::PropertyId::Width, Rml::Property(w, Rml::Unit::PX));
    list->SetProperty(Rml::PropertyId::Height, Rml::Property(h, Rml::Unit::PX));

    for (std::size_t i = 0; i < m_options.size(); ++i)
    {
        Rml::ElementPtr item = doc->CreateElement("div");
        item->SetClass("combo-item", true);
        item->SetClass("selected", static_cast<int>(i) == m_index);
        item->SetAttribute("data-index", static_cast<int>(i));
        item->SetInnerRML(Rml::String(m_options[i]));
        item->SetProperty(Rml::PropertyId::Height,
                          Rml::Property(itemH, Rml::Unit::PX));
        /* MOUSEDOWN, not Click. Pressing an item moves focus, so the combo
         * receives Blur first - which hides the list - and RmlUi does not
         * dispatch a click to a display:none element. The click therefore never
         * arrived and selecting an option did nothing. Mousedown fires before
         * the focus change, so it always lands. */
        item->AddEventListener(Rml::EventId::Mousedown, this);
        list->AppendChild(std::move(item));
    }

    m_list = doc->AppendChild(std::move(list));
}

void JtRmlCombo::closeList()
{
    if (m_list == nullptr)
        return;
    if (Rml::Element* parent = m_list->GetParentNode())
        parent->RemoveChild(m_list);
    m_list = nullptr;
}

// =============================================================================
// Events
// =============================================================================
void JtRmlCombo::ProcessEvent(Rml::Event& event)
{
    /* Only the popup's items are registered with this listener, and only for
     * Mousedown - see openList() for why it is not Click. */
    if (event.GetId() != Rml::EventId::Mousedown)
        return;

    /* The target is not necessarily the item: SetInnerRML wraps the option's
     * text in an anonymous child, and THAT is what gets clicked. Reading
     * data-index off the target alone found nothing, fell back to the current
     * index, and so selecting an option changed nothing at all.
     *
     * Walk up to whichever ancestor actually carries the attribute. */
    Rml::Element* item = event.GetTargetElement();
    while (item != nullptr && !item->HasAttribute("data-index"))
        item = item->GetParentNode();

    if (item == nullptr)
        return;

    setIndex(item->GetAttribute<int>("data-index", m_index));

    /* DEFERRED, not immediate. Destroying the list here frees the very element
     * whose click is still being dispatched, and RmlUi keeps using it after
     * this returns - which crashed the host the moment an option was chosen.
     * It is hidden now and removed on the next update instead. */
    if (m_list != nullptr)
        m_list->SetProperty(Rml::PropertyId::Display,
                            Rml::Property(Rml::Style::Display::None));
    m_closePending = true;

    event.StopPropagation();
}

void JtRmlCombo::OnUpdate()
{
    Rml::Element::OnUpdate();

    /* Safe point: no event is in flight here. */
    if (m_closePending)
    {
        m_closePending = false;
        closeList();
    }
}

void JtRmlCombo::ProcessDefaultAction(Rml::Event& event)
{
    Rml::Element::ProcessDefaultAction(event);

    switch (event.GetId())
    {
    case Rml::EventId::Click:
        if (m_list != nullptr)
        {
            m_list->SetProperty(Rml::PropertyId::Display,
                                Rml::Property(Rml::Style::Display::None));
            m_closePending = true;
        }
        else
        {
            openList();
        }
        event.StopPropagation();
        break;

    case Rml::EventId::Blur:
        /* Dismiss on focus loss, but do NOT hide here: blur arrives BEFORE the
         * press that caused it is handled, and hiding would make the item
         * undispatchable. Closing is deferred to OnUpdate, by which time the
         * item's Mousedown has already been processed. */
        m_closePending = true;
        break;

    case Rml::EventId::Mousescroll:
    {
        /* Wheel steps through the options. Deliberately not wrapping: reaching
         * the end of a list and finding yourself at the start is disorienting
         * when you are turning a control while listening. */
        const float w = event.GetParameter<float>("wheel_delta", 0.0f);
        if (w != 0.0f)
        {
            setIndex(m_index + (w > 0.0f ? 1 : -1));
            event.StopPropagation();
        }
        break;
    }

    case Rml::EventId::Mouseover:
        if (onHover) onHover(*this, true);
        break;

    case Rml::EventId::Mouseout:
        if (onHover) onHover(*this, false);
        break;

    default:
        break;
    }
}

} // namespace jt
